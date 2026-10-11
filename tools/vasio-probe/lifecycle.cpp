#include <windows.h>
#include <objbase.h>
#include <psapi.h>

#include "lifecycle.h"
#include "callback_ring.h"
#include "cycle_report_sink.h"
#include "driver_registry.h"
#include "asiolist.h"
#include "iasiodrv.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

namespace {
using Json = nlohmann::json;

enum : std::uint32_t { BufferSwitch = 1, TimeInfo = 2, AsioMessage = 3, RateChanged = 4 };

std::uint64_t tick() noexcept {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return static_cast<std::uint64_t>(value.QuadPart);
}

struct Context {
    ProbeCallbackRing<8192> events;
    std::atomic<std::uint64_t> callbacks{0};
    std::atomic<std::uint64_t> dropped{0};
    std::atomic<std::uint64_t> resetRequests{0};
    std::atomic<std::uint64_t> bufferSizeChanges{0};
    std::atomic<std::uint64_t> rateNotifications{0};
    std::atomic<bool> done{false};
    void* buffers[2]{};
    std::size_t bytesPerBuffer = 0;

    void record(std::uint32_t kind, std::int32_t value) noexcept {
        if (!events.tryPush({tick(), kind, value}))
            dropped.fetch_add(1, std::memory_order_relaxed);
    }
};

std::atomic<Context*> active{nullptr};

void onBuffer(long index, ASIOBool) noexcept {
    auto* context = active.load(std::memory_order_acquire);
    if (!context) return;
    if (index >= 0 && index < 2 && context->buffers[index])
        std::memset(context->buffers[index], 0, context->bytesPerBuffer);
    context->callbacks.fetch_add(1, std::memory_order_relaxed);
    context->record(BufferSwitch, static_cast<std::int32_t>(index));
}

ASIOTime* onTimeInfo(ASIOTime* time, long index, ASIOBool direct) noexcept {
    onBuffer(index, direct);
    auto* context = active.load(std::memory_order_acquire);
    if (context) context->record(TimeInfo, static_cast<std::int32_t>(index));
    return time;
}

void onRateChanged(ASIOSampleRate rate) noexcept {
    auto* context = active.load(std::memory_order_acquire);
    if (context) {
        context->rateNotifications.fetch_add(1, std::memory_order_relaxed);
        context->record(RateChanged, static_cast<std::int32_t>(rate));
    }
}

long onMessage(long selector, long value, void*, double*) noexcept {
    auto* context = active.load(std::memory_order_acquire);
    if (context) {
        if (selector == kAsioResetRequest)
            context->resetRequests.fetch_add(1, std::memory_order_relaxed);
        if (selector == kAsioBufferSizeChange)
            context->bufferSizeChanges.fetch_add(1, std::memory_order_relaxed);
        context->record(AsioMessage, static_cast<std::int32_t>(selector));
    }
    if (selector == kAsioSelectorSupported)
        return value == kAsioResetRequest || value == kAsioBufferSizeChange ||
            value == kAsioResyncRequest || value == kAsioLatenciesChanged ||
            value == kAsioSupportsTimeInfo ? 1 : 0;
    if (selector == kAsioSupportsTimeInfo) return 1;
    if (selector == kAsioResetRequest || selector == kAsioBufferSizeChange ||
        selector == kAsioResyncRequest || selector == kAsioLatenciesChanged) return 1;
    return 0;
}

std::size_t sampleBytes(ASIOSampleType type) noexcept {
    switch (type) {
    case ASIOSTInt16MSB: case ASIOSTInt16LSB: return 2;
    case ASIOSTInt24MSB: case ASIOSTInt24LSB: return 3;
    case ASIOSTInt32MSB: case ASIOSTInt32LSB:
    case ASIOSTFloat32MSB: case ASIOSTFloat32LSB:
    case ASIOSTInt32MSB16: case ASIOSTInt32LSB16:
    case ASIOSTInt32MSB18: case ASIOSTInt32LSB18:
    case ASIOSTInt32MSB20: case ASIOSTInt32LSB20:
    case ASIOSTInt32MSB24: case ASIOSTInt32LSB24: return 4;
    case ASIOSTFloat64MSB: case ASIOSTFloat64LSB: return 8;
    default: return 0;
    }
}

Json processFootprint() {
    DWORD handles = 0;
    PROCESS_MEMORY_COUNTERS memory{};
    memory.cb = sizeof(memory);
    GetProcessHandleCount(GetCurrentProcess(), &handles);
    K32GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory));
    return {{"handles", handles}, {"workingSetBytes", memory.WorkingSetSize},
        {"privateBytes", memory.PagefileUsage}};
}

bool waitForCallbacks(const Context& context, std::uint64_t from,
    std::uint64_t required, std::uint64_t timeoutMs, std::uint64_t frequency) {
    const auto deadline = tick() + timeoutMs * frequency / 1000;
    while (context.callbacks.load(std::memory_order_relaxed) - from < required) {
        if (tick() >= deadline) return false;
        MsgWaitForMultipleObjectsEx(0, nullptr, 1, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return true;
}

void recordStage(Json& stages, std::ofstream& stageFile, const char* name, std::uint64_t started,
    std::uint64_t frequency, bool success, double maxStageMs, Json& findings,
    std::uint32_t cycle) {
    const auto ended = tick();
    const auto elapsedMs = static_cast<double>(ended - started) * 1000.0 / frequency;
    stages.push_back({{"name", name}, {"startedQpc", started}, {"endedQpc", ended},
        {"durationMs", elapsedMs}, {"success", success}});
    stageFile << Json{{"phase", "end"}, {"cycle", cycle}, {"stage", name},
        {"qpc", ended}, {"durationMs", elapsedMs}, {"success", success}}.dump() << '\n';
    stageFile.flush();
    if (!success || elapsedMs > maxStageMs)
        findings.push_back({{"code", success ? "stage-timeout" : "stage-failed"},
            {"cycle", cycle}, {"stage", name}, {"durationMs", elapsedMs}});
}
}

Json runLifecycle(AsioDriverList& drivers, const std::string& driverName,
    HWND hostWindow, std::uint32_t cycles, bool reloadDll,
    std::uint32_t requestedRate, std::uint32_t requestedBuffer,
    std::uint32_t liveRate,
    const std::string& eventPath,
    const Json& thresholds, std::uint64_t frequency) {
    Json report = {{"mode", "lifecycle"}, {"driver", driverName},
        {"requestedCycles", cycles}, {"completedCycles", 0}, {"reloadDll", reloadDll},
        {"requestedRate", requestedRate}, {"requestedBufferFrames", requestedBuffer},
        {"liveRate", liveRate},
        {"qpcFrequency", frequency}, {"startedQpc", tick()},
        {"eventsPath", eventPath}, {"findings", Json::array()},
        {"cycles", Json::array()}, {"verdict", "inconclusive"}};
    const double maxStageMs = thresholds.at("lifecycle").at("maxStageMs").get<double>();
    const auto waitMs = thresholds.at("lifecycle").at("waitCallbacksMs").get<std::uint64_t>();
    const auto minimumCallbacks = thresholds.at("lifecycle").at("minCallbacksPerCycle").get<std::uint64_t>();
    const auto maxHandleDelta = thresholds.at("lifecycle").at("maxHandleDelta").get<std::int64_t>();
    const auto maxInlineCycles = thresholds.at("lifecycle").at("maxInlineCycles").get<std::size_t>();
    const auto liveWaitMs = thresholds.at("lifecycle").value("liveRateNotificationWaitMs", 1000u);
    if (!(maxStageMs > 0) || !waitMs || !minimumCallbacks || !maxInlineCycles || cycles == 0 ||
        (liveRate && (cycles != 1 || requestedRate || !liveWaitMs))) {
        report["error"] = "invalid lifecycle thresholds or cycle count";
        report["finishedQpc"] = tick();
        return report;
    }
    int selected = -1;
    int matches = 0;
    for (int index = 0; index < drivers.asioGetNumDev(); ++index) {
        char name[MAXDRVNAMELEN]{};
        if (drivers.asioGetDriverName(index, name, sizeof(name)) == 0 && driverName == name) {
            selected = index;
            ++matches;
        }
    }
    if (matches != 1) {
        report["error"] = matches ? "ambiguous driver name" : "driver is not registered";
        report["finishedQpc"] = tick();
        return report;
    }
    CLSID clsid{};
    const bool hasClsid = drivers.asioGetDriverCLSID(selected, &clsid) == 0;
    const auto driverPath = hasClsid ? registeredAsioDllPath(clsid) : std::wstring{};
    if (!driverPath.empty()) report["driverPath"] = utf8FromWide(driverPath);

    auto context = std::make_unique<Context>();
    std::ofstream eventsFile(eventPath, std::ios::binary | std::ios::trunc);
    std::ofstream stageFile(eventPath + ".stages.jsonl", std::ios::binary | std::ios::trunc);
    std::ofstream cycleFile(eventPath + ".cycles.jsonl", std::ios::binary | std::ios::trunc);
    if (!eventsFile || !stageFile || !cycleFile) {
        report["error"] = "cannot open callback event, stage, or cycle file";
        report["finishedQpc"] = tick();
        return report;
    }
    report["stagesPath"] = eventPath + ".stages.jsonl";
    report["cyclesPath"] = eventPath + ".cycles.jsonl";
    report["cyclesRecorded"] = 0;
    report["cyclesInlineLimit"] = maxInlineCycles;
    LifecycleCycleSink cycleSink(report, cycleFile, maxInlineCycles);
    const auto appendCycle = [&](Json item) {
        if (!cycleSink.append(std::move(item)))
            report["findings"].push_back({{"code", "cycle-report-write-failed"}});
    };
    const auto startStage = [&](std::uint32_t cycle, const char* name) {
        const auto started = tick();
        stageFile << Json{{"phase", "start"}, {"cycle", cycle},
            {"stage", name}, {"qpc", started}}.dump() << '\n';
        stageFile.flush();
        return started;
    };
    std::thread writer([&] {
        ProbeCallbackEvent event{};
        while (!context->done.load(std::memory_order_acquire)) {
            if (context->events.tryPop(event))
                eventsFile << Json{{"qpc", event.qpc}, {"kind", event.kind},
                    {"value", event.value}}.dump() << '\n';
            else Sleep(1);
        }
        while (context->events.tryPop(event))
            eventsFile << Json{{"qpc", event.qpc}, {"kind", event.kind},
                {"value", event.value}}.dump() << '\n';
        eventsFile.flush();
    });
    report["before"] = processFootprint();
    Json afterWarmup;
    bool noCallbacks = false;

    for (std::uint32_t cycle = 0; cycle < cycles; ++cycle) {
        Json item = {{"cycle", cycle + 1}, {"stages", Json::array()}};
        IASIO* driver = nullptr;
        auto started = startStage(cycle + 1, "open");
        const auto openStatus = drivers.asioOpenDriver(selected, reinterpret_cast<void**>(&driver));
        recordStage(item["stages"], stageFile, "open", started, frequency,
            openStatus == 0 && driver, maxStageMs, report["findings"], cycle + 1);
        if (openStatus != 0 || !driver) { appendCycle(std::move(item)); break; }

        started = startStage(cycle + 1, "init");
        const bool initialized = driver->init(hostWindow) == ASIOTrue;
        recordStage(item["stages"], stageFile, "init", started, frequency,
            initialized, maxStageMs, report["findings"], cycle + 1);
        if (!initialized) {
            drivers.asioCloseDriver(selected);
            appendCycle(std::move(item));
            break;
        }

        ASIOSampleRate originalRate = 0;
        bool rateChanged = false;
        const auto restoreRate = [&] {
            if (!rateChanged) return;
            ASIOSampleRate restoredRate = 0;
            const auto restoreStatus = driver->setSampleRate(originalRate);
            const auto queryStatus = driver->getSampleRate(&restoredRate);
            const bool restored = restoreStatus == ASE_OK && queryStatus == ASE_OK &&
                restoredRate == originalRate;
            item["rateRestored"] = restored;
            item["restoredRate"] = restoredRate;
            if (!restored) report["findings"].push_back({{"code", "rate-restore-failed"},
                {"cycle", cycle + 1}, {"setStatus", restoreStatus}, {"queryStatus", queryStatus}});
        };
        if (requestedRate || liveRate) {
            if (driver->getSampleRate(&originalRate) != ASE_OK) {
                report["findings"].push_back({{"code", "original-rate-query-failed"}, {"cycle", cycle + 1}});
                drivers.asioCloseDriver(selected);
                appendCycle(std::move(item));
                break;
            }
            item["originalRate"] = originalRate;
        }
        if (requestedRate) {
            item["canRequestedRate"] = driver->canSampleRate(static_cast<ASIOSampleRate>(requestedRate));
            const auto setStatus = driver->setSampleRate(static_cast<ASIOSampleRate>(requestedRate));
            ASIOSampleRate confirmedRate = 0;
            const auto queryStatus = driver->getSampleRate(&confirmedRate);
            item["setRateStatus"] = setStatus;
            item["confirmedRate"] = confirmedRate;
            rateChanged = setStatus == ASE_OK;
            if (setStatus != ASE_OK || queryStatus != ASE_OK || confirmedRate != requestedRate) {
                report["findings"].push_back({{"code", "requested-rate-refused"},
                    {"cycle", cycle + 1}, {"setStatus", setStatus},
                    {"confirmedRate", confirmedRate}});
                restoreRate();
                drivers.asioCloseDriver(selected);
                appendCycle(std::move(item));
                break;
            }
        }
        long inputs = 0, outputs = 0, minimum = 0, maximum = 0, preferred = 0, granularity = 0;
        const bool shape = driver->getChannels(&inputs, &outputs) == ASE_OK &&
            driver->getBufferSize(&minimum, &maximum, &preferred, &granularity) == ASE_OK &&
            preferred > 0 && (outputs > 0 || inputs > 0);
        ASIOBufferInfo buffer{};
        buffer.isInput = outputs > 0 ? ASIOFalse : ASIOTrue;
        buffer.channelNum = 0;
        ASIOChannelInfo channel{};
        channel.isInput = buffer.isInput;
        channel.channel = 0;
        const bool channelReady = shape && driver->getChannelInfo(&channel) == ASE_OK;
        const auto bytes = channelReady ? sampleBytes(channel.type) : 0;
        if (!shape || !channelReady || !bytes) {
            report["findings"].push_back({{"code", "unsupported-buffer-shape"}, {"cycle", cycle + 1}});
            restoreRate();
            drivers.asioCloseDriver(selected);
            appendCycle(std::move(item));
            break;
        }
        ASIOCallbacks callbacks{};
        callbacks.bufferSwitch = onBuffer;
        callbacks.bufferSwitchTimeInfo = onTimeInfo;
        callbacks.sampleRateDidChange = onRateChanged;
        callbacks.asioMessage = onMessage;
        active.store(context.get(), std::memory_order_release);
        const long bufferFrames = requestedBuffer ? static_cast<long>(requestedBuffer) : preferred;
        item["declaredBuffer"] = {{"minimum", minimum}, {"maximum", maximum},
            {"preferred", preferred}, {"granularity", granularity}};
        item["attemptedBufferFrames"] = bufferFrames;
        started = startStage(cycle + 1, "createBuffers");
        const bool created = driver->createBuffers(&buffer, 1, bufferFrames, &callbacks) == ASE_OK;
        recordStage(item["stages"], stageFile, "createBuffers", started, frequency,
            created, maxStageMs, report["findings"], cycle + 1);
        if (!created) {
            active.store(nullptr, std::memory_order_release);
            restoreRate();
            drivers.asioCloseDriver(selected);
            appendCycle(std::move(item));
            break;
        }
        context->bytesPerBuffer = static_cast<std::size_t>(bufferFrames) * bytes;
        context->buffers[0] = buffer.buffers[0];
        context->buffers[1] = buffer.buffers[1];
        const auto beforeCallbacks = context->callbacks.load(std::memory_order_relaxed);
        started = startStage(cycle + 1, "start");
        const bool running = driver->start() == ASE_OK;
        recordStage(item["stages"], stageFile, "start", started, frequency,
            running, maxStageMs, report["findings"], cycle + 1);
        if (running) {
            const bool enough = waitForCallbacks(*context, beforeCallbacks, minimumCallbacks, waitMs, frequency);
            item["callbackCount"] = context->callbacks.load(std::memory_order_relaxed) - beforeCallbacks;
            if (!enough)
                if (item["callbackCount"].get<std::uint64_t>() == 0) {
                    noCallbacks = true;
                    report["precondition"] = "driver started but no engine callback reached this client; check active routing";
                } else
                    report["findings"].push_back({{"code", "callback-timeout"}, {"cycle", cycle + 1},
                        {"callbackCount", item["callbackCount"]}});
            if (liveRate && enough) {
                const auto beforeReset = context->resetRequests.load(std::memory_order_relaxed);
                const auto beforeBufferChange = context->bufferSizeChanges.load(std::memory_order_relaxed);
                const auto beforeRateNotice = context->rateNotifications.load(std::memory_order_relaxed);
                const auto beforeLiveCallbacks = context->callbacks.load(std::memory_order_relaxed);
                const auto canStatus = driver->canSampleRate(static_cast<ASIOSampleRate>(liveRate));
                const auto liveStarted = tick();
                const auto setStatus = driver->setSampleRate(static_cast<ASIOSampleRate>(liveRate));
                const auto liveEnded = tick();
                ASIOSampleRate confirmedRate = 0;
                const auto queryStatus = driver->getSampleRate(&confirmedRate);
                rateChanged = setStatus == ASE_OK ||
                    (queryStatus == ASE_OK && confirmedRate != originalRate);
                const auto deadline = tick() + liveWaitMs * frequency / 1000;
                while (tick() < deadline) {
                    MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                    MSG message{};
                    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                        TranslateMessage(&message);
                        DispatchMessageW(&message);
                    }
                }
                item["liveRateChange"] = {{"requestedRate", liveRate},
                    {"canSampleRateStatus", canStatus}, {"setSampleRateStatus", setStatus},
                    {"getSampleRateStatus", queryStatus}, {"confirmedRate", confirmedRate},
                    {"startedQpc", liveStarted}, {"endedQpc", liveEnded},
                    {"resetRequests", context->resetRequests.load(std::memory_order_relaxed) - beforeReset},
                    {"bufferSizeChanges", context->bufferSizeChanges.load(std::memory_order_relaxed) - beforeBufferChange},
                    {"rateNotifications", context->rateNotifications.load(std::memory_order_relaxed) - beforeRateNotice},
                    {"callbacksAfterChange", context->callbacks.load(std::memory_order_relaxed) - beforeLiveCallbacks},
                    {"interpretation", "Host requested this rate; a notification callback is observed but not required to tell the host its own request."}};
                if (setStatus == ASE_OK && (queryStatus != ASE_OK || confirmedRate != liveRate))
                    report["findings"].push_back({{"code", "live-rate-not-confirmed"},
                        {"setStatus", setStatus}, {"queryStatus", queryStatus},
                        {"confirmedRate", confirmedRate}});
            }
            started = startStage(cycle + 1, "stop");
            const bool stopped = driver->stop() == ASE_OK;
            recordStage(item["stages"], stageFile, "stop", started, frequency,
                stopped, maxStageMs, report["findings"], cycle + 1);
        }
        active.store(nullptr, std::memory_order_release);
        context->buffers[0] = context->buffers[1] = nullptr;
        started = startStage(cycle + 1, "disposeBuffers");
        const bool disposed = driver->disposeBuffers() == ASE_OK;
        recordStage(item["stages"], stageFile, "disposeBuffers", started, frequency,
            disposed, maxStageMs, report["findings"], cycle + 1);
        restoreRate();
        started = startStage(cycle + 1, "unload");
        const bool closed = drivers.asioCloseDriver(selected) == 0;
        recordStage(item["stages"], stageFile, "unload", started, frequency,
            closed, maxStageMs, report["findings"], cycle + 1);
        if (reloadDll && closed) {
            started = startStage(cycle + 1, "freeUnusedLibraries");
            CoFreeUnusedLibrariesEx(0, 0);
            const bool unloaded = !driverPath.empty() && GetModuleHandleW(driverPath.c_str()) == nullptr;
            item["dllUnloaded"] = unloaded;
            recordStage(item["stages"], stageFile, "freeUnusedLibraries", started, frequency,
                unloaded, maxStageMs, report["findings"], cycle + 1);
        }
        appendCycle(std::move(item));
        report["completedCycles"] = cycle + 1;
        if (cycle == 0) afterWarmup = processFootprint();
        if (noCallbacks || !cycleFile) break;
        if (!running || !disposed || !closed) break;
    }

    context->done.store(true, std::memory_order_release);
    writer.join();
    stageFile.flush();
    cycleFile.flush();
    if (!eventsFile || !stageFile)
        report["findings"].push_back({{"code", "event-or-stage-report-write-failed"}});
    if (!cycleFile) report["findings"].push_back({{"code", "cycle-report-write-failed"}});
    report["after"] = processFootprint();
    report["afterWarmup"] = afterWarmup.is_null() ? report["before"] : afterWarmup;
    report["callbackCount"] = context->callbacks.load(std::memory_order_relaxed);
    report["droppedCallbackEvents"] = context->dropped.load(std::memory_order_relaxed);
    const auto handleDelta = report["after"]["handles"].get<std::int64_t>() -
        report["afterWarmup"]["handles"].get<std::int64_t>();
    report["handleDelta"] = handleDelta;
    if (handleDelta > maxHandleDelta)
        report["findings"].push_back({{"code", "handle-growth"}, {"delta", handleDelta}});
    if (report["droppedCallbackEvents"].get<std::uint64_t>() != 0)
        report["findings"].push_back({{"code", "callback-ring-overflow"}});
    if (!noCallbacks && report["completedCycles"].get<std::uint32_t>() != cycles)
        report["findings"].push_back({{"code", "incomplete-cycles"}});
    report["verdict"] = !report["findings"].empty() ? "fail" :
        noCallbacks ? "inconclusive" : "pass";
    report["finishedQpc"] = tick();
    return report;
}
