#include "cadence.h"
#include "cadence_stats.h"
#include "clock_regression.h"
#include "callback_ring.h"
#include "driver_registry.h"
#include "asiolist.h"
#include "iasiodrv.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
using Json = nlohmann::json;
enum : std::uint32_t { Callback = 1, Message = 2, RateChanged = 3 };

std::uint64_t tick() noexcept {
    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    return static_cast<std::uint64_t>(qpc.QuadPart);
}

struct Context {
    ProbeCallbackRing<8192> ring;
    std::atomic<std::uint64_t> callbacks{0};
    std::atomic<std::uint64_t> dropped{0};
    std::atomic<bool> done{false};
    void* buffers[2]{};
    std::size_t bytesPerBuffer = 0;

    void push(const ProbeCallbackEvent& event) noexcept {
        if (!ring.tryPush(event)) dropped.fetch_add(1, std::memory_order_relaxed);
    }
};

std::atomic<Context*> active{nullptr};

void clearOutput(Context* context, long index) noexcept {
    if (index >= 0 && index < 2 && context->buffers[index])
        std::memset(context->buffers[index], 0, context->bytesPerBuffer);
}

void onBuffer(long index, ASIOBool) noexcept {
    auto* context = active.load(std::memory_order_acquire);
    if (!context) return;
    const auto timestamp = tick();
    clearOutput(context, index);
    context->callbacks.fetch_add(1, std::memory_order_relaxed);
    context->push({timestamp, Callback, static_cast<std::int32_t>(index)});
}

ASIOTime* onTimeInfo(ASIOTime* time, long index, ASIOBool) noexcept {
    auto* context = active.load(std::memory_order_acquire);
    if (!context) return time;
    const auto timestamp = tick();
    clearOutput(context, index);
    std::uint64_t position = 0;
    bool valid = false;
    if (time && (time->timeInfo.flags & kSamplePositionValid)) {
        position = (static_cast<std::uint64_t>(time->timeInfo.samplePosition.hi) << 32) |
            static_cast<std::uint32_t>(time->timeInfo.samplePosition.lo);
        valid = true;
    }
    context->callbacks.fetch_add(1, std::memory_order_relaxed);
    context->push({timestamp, Callback, static_cast<std::int32_t>(index), position, valid});
    return time;
}

void onRateChanged(ASIOSampleRate rate) noexcept {
    auto* context = active.load(std::memory_order_acquire);
    if (context) context->push({tick(), RateChanged, static_cast<std::int32_t>(rate)});
}

long onMessage(long selector, long value, void*, double*) noexcept {
    auto* context = active.load(std::memory_order_acquire);
    if (context) context->push({tick(), Message, static_cast<std::int32_t>(selector)});
    if (selector == kAsioSelectorSupported)
        return value == kAsioResetRequest || value == kAsioBufferSizeChange ||
            value == kAsioResyncRequest || value == kAsioLatenciesChanged ||
            value == kAsioSupportsTimeInfo ? 1 : 0;
    if (selector == kAsioSupportsTimeInfo || selector == kAsioResetRequest ||
        selector == kAsioBufferSizeChange || selector == kAsioResyncRequest ||
        selector == kAsioLatenciesChanged) return 1;
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

void pumpMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
}

Json runCadence(AsioDriverList& drivers, const std::string& driverName,
    HWND hostWindow, std::uint32_t durationSeconds, const std::string& eventPath,
    const Json& thresholds, std::uint64_t frequency) {
    Json report = {{"mode", "cadence"}, {"driver", driverName},
        {"requestedSeconds", durationSeconds}, {"qpcFrequency", frequency},
        {"startedQpc", tick()}, {"eventsPath", eventPath},
        {"findings", Json::array()}, {"verdict", "inconclusive"}};
    const auto& limits = thresholds.at("cadence");
    const double maxJitterFraction = limits.at("maxJitterFraction").get<double>();
    const auto maxMissing = limits.at("maxMissingCallbacks").get<std::uint64_t>();
    const auto maxDuplicates = limits.at("maxDuplicateCallbacks").get<std::uint64_t>();
    const auto maxPositionErrors = limits.at("maxPositionStepErrors").get<std::uint64_t>();
    const auto minCallbacks = limits.at("minCallbacks").get<std::uint64_t>();
    if (maxJitterFraction <= 0 || minCallbacks < 2 || durationSeconds == 0) {
        report["error"] = "invalid cadence thresholds or duration";
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
    if (drivers.asioGetDriverCLSID(selected, &clsid) == 0) {
        const auto path = registeredAsioDllPath(clsid);
        if (!path.empty()) report["driverPath"] = utf8FromWide(path);
    }
    IASIO* driver = nullptr;
    if (drivers.asioOpenDriver(selected, reinterpret_cast<void**>(&driver)) != 0 || !driver) {
        report["findings"].push_back({{"code", "open-failed"}});
        report["verdict"] = "fail";
        report["finishedQpc"] = tick();
        return report;
    }
    if (driver->init(hostWindow) != ASIOTrue) {
        report["findings"].push_back({{"code", "init-failed"}});
        report["verdict"] = "fail";
        drivers.asioCloseDriver(selected);
        report["finishedQpc"] = tick();
        return report;
    }
    long inputs = 0, outputs = 0, minimum = 0, maximum = 0, preferred = 0, granularity = 0;
    ASIOSampleRate rate = 0;
    bool shape = driver->getChannels(&inputs, &outputs) == ASE_OK &&
        driver->getBufferSize(&minimum, &maximum, &preferred, &granularity) == ASE_OK &&
        driver->getSampleRate(&rate) == ASE_OK && preferred > 0 && rate > 0 &&
        (outputs > 0 || inputs > 0);
    ASIOBufferInfo buffer{};
    buffer.isInput = outputs > 0 ? ASIOFalse : ASIOTrue;
    buffer.channelNum = 0;
    ASIOChannelInfo channel{};
    channel.isInput = buffer.isInput;
    channel.channel = 0;
    if (shape) shape = driver->getChannelInfo(&channel) == ASE_OK && sampleBytes(channel.type) != 0;
    if (!shape) {
        report["findings"].push_back({{"code", "unsupported-buffer-shape"}});
        report["verdict"] = "fail";
        drivers.asioCloseDriver(selected);
        report["finishedQpc"] = tick();
        return report;
    }
    report["sampleRate"] = rate;
    report["bufferFrames"] = preferred;
    auto context = std::make_unique<Context>();
    ASIOCallbacks callbacks{};
    callbacks.bufferSwitch = onBuffer;
    callbacks.bufferSwitchTimeInfo = onTimeInfo;
    callbacks.sampleRateDidChange = onRateChanged;
    callbacks.asioMessage = onMessage;
    active.store(context.get(), std::memory_order_release);
    const bool created = driver->createBuffers(&buffer, 1, preferred, &callbacks) == ASE_OK;
    if (!created) {
        report["findings"].push_back({{"code", "create-buffers-failed"}});
        report["verdict"] = "fail";
        active.store(nullptr, std::memory_order_release);
        drivers.asioCloseDriver(selected);
        report["finishedQpc"] = tick();
        return report;
    }
    context->buffers[0] = buffer.buffers[0];
    context->buffers[1] = buffer.buffers[1];
    context->bytesPerBuffer = static_cast<std::size_t>(preferred) * sampleBytes(channel.type);
    std::ofstream eventFile(eventPath, std::ios::binary | std::ios::trunc);
    if (!eventFile) {
        report["findings"].push_back({{"code", "event-file-open-failed"}});
        report["verdict"] = "fail";
        active.store(nullptr, std::memory_order_release);
        driver->disposeBuffers();
        drivers.asioCloseDriver(selected);
        report["finishedQpc"] = tick();
        return report;
    }
    std::vector<CadenceSample> samples;
    std::vector<std::uint64_t> callbackQpcs;
    std::uint64_t callbacksWithoutPosition = 0;
    std::thread writer([&] {
        ProbeCallbackEvent event{};
        const auto consume = [&] {
            eventFile << Json{{"qpc", event.qpc}, {"kind", event.kind},
                {"value", event.value}, {"samplePosition", event.samplePosition},
                {"positionValid", event.positionValid}}.dump() << '\n';
            if (event.kind == Callback) {
                callbackQpcs.push_back(event.qpc);
                if (event.positionValid) samples.push_back({event.qpc, event.samplePosition});
                else ++callbacksWithoutPosition;
            }
        };
        while (!context->done.load(std::memory_order_acquire)) {
            if (context->ring.tryPop(event)) consume();
            else Sleep(1);
        }
        while (context->ring.tryPop(event)) consume();
        eventFile.flush();
    });
    const bool running = driver->start() == ASE_OK;
    const auto runStarted = tick();
    std::vector<CadenceSample> polledPositions;
    std::uint64_t positionQueryFailures = 0;
    if (running) {
        const auto deadline = runStarted + static_cast<std::uint64_t>(durationSeconds) * frequency;
        while (tick() < deadline) {
            ASIOSamples position{};
            ASIOTimeStamp systemTime{};
            const auto queriedQpc = tick();
            if (driver->getSamplePosition(&position, &systemTime) == ASE_OK) {
                const auto frames = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(position.hi)) << 32) |
                    static_cast<std::uint32_t>(position.lo);
                polledPositions.push_back({queriedQpc, frames});
            } else {
                ++positionQueryFailures;
            }
            MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            pumpMessages();
        }
    }
    const auto runEnded = tick();
    const bool stopped = !running || driver->stop() == ASE_OK;
    active.store(nullptr, std::memory_order_release);
    const bool disposed = driver->disposeBuffers() == ASE_OK;
    const bool closed = drivers.asioCloseDriver(selected) == 0;
    context->done.store(true, std::memory_order_release);
    writer.join();
    if (!eventFile.good())
        report["findings"].push_back({{"code", "event-file-write-failed"}});
    const auto pollPath = eventPath + ".polls.jsonl";
    std::ofstream pollFile(pollPath, std::ios::binary | std::ios::trunc);
    if (pollFile) {
        for (const auto& sample : polledPositions)
            pollFile << "{\"qpc\":" << sample.qpc << ",\"samplePosition\":" <<
                sample.position << "}\n";
        pollFile.flush();
    }
    const bool pollsSaved = pollFile && pollFile.good();
    report["positionPollsPath"] = pollPath;
    report["positionPollsSaved"] = pollsSaved;
    if (!pollsSaved) report["findings"].push_back({{"code", "position-polls-file-failed"}});
    report["runStartedQpc"] = runStarted;
    report["runEndedQpc"] = runEnded;
    report["callbackCount"] = context->callbacks.load(std::memory_order_relaxed);
    report["callbacksWithoutPosition"] = callbacksWithoutPosition;
    report["droppedCallbackEvents"] = context->dropped.load(std::memory_order_relaxed);
    report["stopped"] = stopped;
    report["disposed"] = disposed;
    report["closed"] = closed;
    if (!running) report["findings"].push_back({{"code", "start-failed"}});
    if (!stopped) report["findings"].push_back({{"code", "stop-failed"}});
    if (!disposed) report["findings"].push_back({{"code", "dispose-failed"}});
    if (!closed) report["findings"].push_back({{"code", "unload-failed"}});
    if (context->dropped.load(std::memory_order_relaxed))
        report["findings"].push_back({{"code", "callback-ring-overflow"}});
    const auto stats = summarizeCadence(samples, rate, static_cast<std::uint32_t>(preferred), frequency);
    report["statistics"] = {{"measuredCallbacks", stats.callbackCount},
        {"expectedIntervalMs", stats.expectedIntervalMs},
        {"intervalMeanMs", stats.intervalMeanMs}, {"intervalMinMs", stats.intervalMinMs},
        {"intervalMaxMs", stats.intervalMaxMs}, {"intervalP99Ms", stats.intervalP99Ms},
        {"jitterP99Ms", stats.jitterP99Ms}, {"missingCallbacks", stats.missingCallbacks},
        {"duplicateCallbacks", stats.duplicateCallbacks},
        {"backwardsPositions", stats.backwardsPositions},
        {"positionStepErrors", stats.positionStepErrors},
        {"nonMonotonicTimestamps", stats.nonMonotonicTimestamps}, {"driftPpm", stats.driftPpm}};
    const double theoreticalMs = static_cast<double>(preferred) * 1000.0 / rate;
    std::vector<double> callbackJitter;
    double intervalSum = 0.0;
    double intervalMin = 0.0, intervalMax = 0.0;
    std::uint64_t estimatedGaps = 0;
    for (std::size_t index = 1; index < callbackQpcs.size(); ++index) {
        const auto interval = static_cast<double>(callbackQpcs[index] - callbackQpcs[index - 1]) *
            1000.0 / frequency;
        if (index == 1 || interval < intervalMin) intervalMin = interval;
        if (interval > intervalMax) intervalMax = interval;
        intervalSum += interval;
        callbackJitter.push_back(std::abs(interval - theoreticalMs));
        if (interval > theoreticalMs * 1.5) ++estimatedGaps;
    }
    std::sort(callbackJitter.begin(), callbackJitter.end());
    const auto p99Index = callbackJitter.empty() ? 0 :
        static_cast<std::size_t>(std::ceil(callbackJitter.size() * 0.99) - 1);
    report["callbackTiming"] = {{"count", callbackQpcs.size()},
        {"expectedIntervalMs", theoreticalMs},
        {"intervalMeanMs", callbackJitter.empty() ? 0.0 : intervalSum / callbackJitter.size()},
        {"intervalMinMs", intervalMin}, {"intervalMaxMs", intervalMax},
        {"jitterP99Ms", callbackJitter.empty() ? 0.0 : callbackJitter[p99Index]},
        {"estimatedLongGaps", estimatedGaps}};
    std::uint64_t backwardsPolls = 0, advancingPolls = 0;
    for (std::size_t index = 1; index < polledPositions.size(); ++index) {
        if (polledPositions[index].position < polledPositions[index - 1].position)
            ++backwardsPolls;
        else if (polledPositions[index].position > polledPositions[index - 1].position)
            ++advancingPolls;
    }
    double polledDrift = 0.0;
    if (polledPositions.size() >= 2 &&
        polledPositions.back().position >= polledPositions.front().position &&
        polledPositions.back().qpc > polledPositions.front().qpc) {
        const auto elapsed = static_cast<double>(polledPositions.back().qpc -
            polledPositions.front().qpc) / frequency;
        const auto observedRate = static_cast<double>(polledPositions.back().position -
            polledPositions.front().position) / elapsed;
        polledDrift = (observedRate / rate - 1.0) * 1e6;
    }
    report["samplePositionPolling"] = {{"source", "ASIOGetSamplePosition"},
        {"polls", polledPositions.size()}, {"queryFailures", positionQueryFailures},
        {"advancingPolls", advancingPolls}, {"backwardsPolls", backwardsPolls},
        {"firstQpc", polledPositions.empty() ? 0 : polledPositions.front().qpc},
        {"lastQpc", polledPositions.empty() ? 0 : polledPositions.back().qpc},
        {"firstPosition", polledPositions.empty() ? 0 : polledPositions.front().position},
        {"lastPosition", polledPositions.empty() ? 0 : polledPositions.back().position},
        {"driftPpm", polledDrift},
        {"precision", "QPC polling on the host thread; no per-callback position when TimeInfo is absent"}};
    const double blockSeconds = limits.value("regressionBlockSeconds", 10.0);
    const auto regression = estimateClockRegression(polledPositions, rate,
        static_cast<std::uint32_t>(preferred), frequency, blockSeconds);
    report["clockRegression"] = {{"available", regression.available},
        {"source", "ASIOGetSamplePosition polled against QueryPerformanceCounter"},
        {"sampleCount", regression.samples}, {"blockCount", regression.blocks},
        {"blockSeconds", blockSeconds}, {"durationSeconds", regression.durationSeconds},
        {"observedRateHz", regression.observedRateHz}, {"driftPpm", regression.driftPpm},
        {"uncertaintyHalfWidthPpm", regression.uncertaintyHalfWidthPpm},
        {"lowerPpm", regression.lowerPpm}, {"upperPpm", regression.upperPpm},
        {"blockStandardErrorPpm", regression.blockStandardErrorPpm},
        {"endpointQuantizationBoundPpm", regression.endpointQuantizationBoundPpm},
        {"uncertaintyMethod", "max(2 x standard error across non-overlapping blocks, two-buffer endpoint quantization bound); empirical interval, not a calibrated confidence interval"}};
    report["timeInfoAvailable"] = stats.callbackCount >= minCallbacks &&
        callbacksWithoutPosition == 0;
    if (callbacksWithoutPosition && callbackQpcs.size() >= minCallbacks &&
        callbackJitter[p99Index] > theoreticalMs * maxJitterFraction)
        report["findings"].push_back({{"code", "jitter-p99-exceeded-without-timeinfo"},
            {"measuredMs", callbackJitter[p99Index]}});
    if (stats.callbackCount >= minCallbacks && callbacksWithoutPosition == 0 &&
        context->dropped.load(std::memory_order_relaxed) == 0) {
        if (stats.missingCallbacks > maxMissing)
            report["findings"].push_back({{"code", "missing-callbacks"}, {"count", stats.missingCallbacks}});
        if (stats.duplicateCallbacks > maxDuplicates)
            report["findings"].push_back({{"code", "duplicate-callbacks"}, {"count", stats.duplicateCallbacks}});
        if (stats.positionStepErrors > maxPositionErrors)
            report["findings"].push_back({{"code", "position-step-errors"}, {"count", stats.positionStepErrors}});
        if (stats.nonMonotonicTimestamps)
            report["findings"].push_back({{"code", "nonmonotonic-qpc"}, {"count", stats.nonMonotonicTimestamps}});
        if (stats.jitterP99Ms > stats.expectedIntervalMs * maxJitterFraction)
            report["findings"].push_back({{"code", "jitter-p99-exceeded"},
                {"measuredMs", stats.jitterP99Ms}});
        report["verdict"] = report["findings"].empty() ? "pass" : "fail";
    } else {
        report["precondition"] = "Too few TimeInfo positions, callback ring overflow, or missing TimeInfo";
        report["verdict"] = report["findings"].empty() ? "inconclusive" : "fail";
    }
    report["finishedQpc"] = tick();
    return report;
}
