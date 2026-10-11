#include "loopback.h"
#include "loopback_pattern.h"
#include "callback_ring.h"
#include "driver_registry.h"
#include "asiolist.h"
#include "iasiodrv.h"

#include <windows.h>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

namespace {
using Json = nlohmann::json;
enum : std::uint32_t { AudioBlock = 1, Message = 2, RateChanged = 3, HostRestart = 4 };

std::uint64_t tick() noexcept {
    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    return static_cast<std::uint64_t>(qpc.QuadPart);
}

struct Context {
    ProbeCallbackRing<8192> ring;
    std::atomic<std::uint64_t> callbacks{0};
    std::atomic<std::uint64_t> dropped{0};
    std::atomic<std::uint64_t> resetRequests{0};
    std::atomic<bool> everSynchronized{false};
    std::atomic<bool> done{false};
    bool sending = false;
    bool receiving = false;
    long frames = 0;
    float* inputBuffers[2]{};
    float* outputBuffers[2]{};
    std::uint64_t nextCounter = 0;
    std::uint32_t expectedCounter = 0;
    bool synchronized = false;

    void push(const ProbeCallbackEvent& event) noexcept {
        if (!ring.tryPush(event)) dropped.fetch_add(1, std::memory_order_relaxed);
    }
};

std::atomic<Context*> active{nullptr};

void processBuffer(long index, ASIOTime* time) noexcept {
    auto* context = active.load(std::memory_order_acquire);
    if (!context || index < 0 || index > 1 ||
        (context->receiving && !context->inputBuffers[index]) ||
        (context->sending && !context->outputBuffers[index])) return;
    ProbeCallbackEvent event{};
    event.qpc = tick();
    event.kind = AudioBlock;
    event.value = static_cast<std::int32_t>(index);
    if (time && (time->timeInfo.flags & kSamplePositionValid)) {
        event.samplePosition =
            (static_cast<std::uint64_t>(time->timeInfo.samplePosition.hi) << 32) |
            static_cast<std::uint32_t>(time->timeInfo.samplePosition.lo);
        event.positionValid = true;
    }
    if (context->receiving) {
        auto* buffer = context->inputBuffers[index];
        bool firstValid = false;
        for (long frame = 0; frame < context->frames; ++frame) {
            std::uint32_t counter = 0;
            if (!decodeLoopbackSample(buffer[frame], counter)) {
                if (context->synchronized) {
                    ++event.corrupted;
                    context->expectedCounter = (context->expectedCounter + 1) & LoopbackPattern::kMask;
                } else {
                    ++event.preSync;
                }
                continue;
            }
            if (!firstValid) { event.counter = counter; firstValid = true; }
            if (!context->synchronized) {
                context->synchronized = true;
                context->everSynchronized.store(true, std::memory_order_relaxed);
                context->expectedCounter = (counter + 1) & LoopbackPattern::kMask;
                continue;
            }
            if (counter == context->expectedCounter) {
                context->expectedCounter = (context->expectedCounter + 1) & LoopbackPattern::kMask;
            } else if (counter == ((context->expectedCounter - 1) & LoopbackPattern::kMask)) {
                ++event.duplicated;
            } else {
                const auto forward = (counter - context->expectedCounter) & LoopbackPattern::kMask;
                if (forward < (LoopbackPattern::kMask + 1) / 2) {
                    event.missing += forward;
                    context->expectedCounter = (counter + 1) & LoopbackPattern::kMask;
                } else {
                    ++event.corrupted;
                    context->expectedCounter = (context->expectedCounter + 1) & LoopbackPattern::kMask;
                }
            }
        }
        event.positionValid = event.positionValid && firstValid;
    }
    if (context->sending) {
        auto* buffer = context->outputBuffers[index];
        if (!context->receiving) event.counter = context->nextCounter;
        for (long frame = 0; frame < context->frames; ++frame)
            buffer[frame] = loopbackSample(static_cast<std::uint32_t>(context->nextCounter++));
    }
    context->callbacks.fetch_add(1, std::memory_order_relaxed);
    context->push(event);
}

void onBuffer(long index, ASIOBool) noexcept { processBuffer(index, nullptr); }
ASIOTime* onTimeInfo(ASIOTime* time, long index, ASIOBool) noexcept {
    processBuffer(index, time);
    return time;
}
void onRateChanged(ASIOSampleRate rate) noexcept {
    auto* context = active.load(std::memory_order_acquire);
    if (context) context->push({tick(), RateChanged, static_cast<std::int32_t>(rate)});
}
long onMessage(long selector, long value, void*, double*) noexcept {
    auto* context = active.load(std::memory_order_acquire);
    if (context) {
        context->push({tick(), Message, static_cast<std::int32_t>(selector)});
        if (selector == kAsioResetRequest)
            context->resetRequests.fetch_add(1, std::memory_order_release);
    }
    if (selector == kAsioSelectorSupported)
        return value == kAsioResetRequest || value == kAsioBufferSizeChange ||
            value == kAsioResyncRequest || value == kAsioLatenciesChanged ||
            value == kAsioSupportsTimeInfo ? 1 : 0;
    if (selector == kAsioSupportsTimeInfo || selector == kAsioResetRequest ||
        selector == kAsioBufferSizeChange || selector == kAsioResyncRequest ||
        selector == kAsioLatenciesChanged) return 1;
    return 0;
}

void pumpMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
}

Json runLoopback(AsioDriverList& drivers, const std::string& driverName,
    HWND hostWindow, const std::string& role, std::uint32_t durationSeconds,
    std::uint32_t channelIndex, const std::string& eventPath,
    const Json& thresholds, std::uint64_t frequency) {
    Json report = {{"mode", "loopback"}, {"role", role}, {"driver", driverName},
        {"requestedSeconds", durationSeconds}, {"qpcFrequency", frequency},
        {"startedQpc", tick()}, {"eventsPath", eventPath},
        {"findings", Json::array()}, {"verdict", "inconclusive"}};
    const bool sending = role == "send" || role == "both";
    const bool receiving = role == "receive" || role == "both";
    const auto& limits = thresholds.at("loopback");
    const auto minCallbacks = limits.at("minCallbacks").get<std::uint64_t>();
    const auto maxCorrupted = limits.at("maxCorruptedSamples").get<std::uint64_t>();
    const auto maxMissing = limits.at("maxMissingSamples").get<std::uint64_t>();
    const auto maxDuplicated = limits.at("maxDuplicatedSamples").get<std::uint64_t>();
    if ((role != "send" && role != "receive" && role != "both") || durationSeconds == 0 || minCallbacks < 1) {
        report["error"] = "invalid loopback role, duration or thresholds";
        report["finishedQpc"] = tick();
        return report;
    }
    int selected = -1, matches = 0;
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
        (!sending || outputs > static_cast<long>(channelIndex)) &&
        (!receiving || inputs > static_cast<long>(channelIndex));
    ASIOBufferInfo buffers[2]{};
    long bufferCount = 0;
    if (receiving) { buffers[bufferCount].isInput = ASIOTrue; buffers[bufferCount++].channelNum = static_cast<long>(channelIndex); }
    if (sending) { buffers[bufferCount].isInput = ASIOFalse; buffers[bufferCount++].channelNum = static_cast<long>(channelIndex); }
    for (long i = 0; shape && i < bufferCount; ++i) {
        ASIOChannelInfo channel{};
        channel.isInput = buffers[i].isInput;
        channel.channel = static_cast<long>(channelIndex);
        shape = driver->getChannelInfo(&channel) == ASE_OK && channel.type == ASIOSTFloat32LSB;
    }
    if (!shape) {
        report["precondition"] = "loopback requires a Float32LSB channel in the selected direction";
        drivers.asioCloseDriver(selected);
        report["finishedQpc"] = tick();
        return report;
    }
    report["sampleRate"] = rate;
    report["bufferFrames"] = preferred;
    report["channel"] = channelIndex;
    auto context = std::make_unique<Context>();
    context->sending = sending;
    context->receiving = receiving;
    context->frames = preferred;
    ASIOCallbacks callbacks{};
    callbacks.bufferSwitch = onBuffer;
    callbacks.bufferSwitchTimeInfo = onTimeInfo;
    callbacks.sampleRateDidChange = onRateChanged;
    callbacks.asioMessage = onMessage;
    active.store(context.get(), std::memory_order_release);
    const bool created = driver->createBuffers(buffers, bufferCount, preferred, &callbacks) == ASE_OK;
    if (!created) {
        report["findings"].push_back({{"code", "create-buffers-failed"}});
        report["verdict"] = "fail";
        active.store(nullptr, std::memory_order_release);
        drivers.asioCloseDriver(selected);
        report["finishedQpc"] = tick();
        return report;
    }
    if (receiving) {
        context->inputBuffers[0] = static_cast<float*>(buffers[0].buffers[0]);
        context->inputBuffers[1] = static_cast<float*>(buffers[0].buffers[1]);
    }
    if (sending) {
        const auto outputIndex = receiving ? 1 : 0;
        context->outputBuffers[0] = static_cast<float*>(buffers[outputIndex].buffers[0]);
        context->outputBuffers[1] = static_cast<float*>(buffers[outputIndex].buffers[1]);
    }
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
    std::uint64_t corrupted = 0, missing = 0, duplicated = 0, preSync = 0, measuredBlocks = 0;
    std::thread writer([&] {
        ProbeCallbackEvent event{};
        const auto consume = [&] {
            eventFile << Json{{"qpc", event.qpc}, {"kind", event.kind},
                {"value", event.value}, {"samplePosition", event.samplePosition},
                {"positionValid", event.positionValid}, {"counter", event.counter},
                {"corrupted", event.corrupted}, {"missing", event.missing},
                {"duplicated", event.duplicated}, {"preSync", event.preSync}}.dump() << '\n';
            if (event.kind == AudioBlock) {
                ++measuredBlocks;
                corrupted += event.corrupted;
                missing += event.missing;
                duplicated += event.duplicated;
                preSync += event.preSync;
                if ((measuredBlocks & 31u) == 0) eventFile.flush();
            }
        };
        while (!context->done.load(std::memory_order_acquire)) {
            if (context->ring.tryPop(event)) consume();
            else Sleep(1);
        }
        while (context->ring.tryPop(event)) consume();
        eventFile.flush();
    });
    bool running = driver->start() == ASE_OK;
    const bool initialStartSucceeded = running;
    bool buffersCreated = true;
    bool allStops = true, allDisposals = true, allCloses = true;
    bool restartFailed = false;
    std::uint64_t handledResetRequests = 0;
    Json hostRestarts = Json::array();
    const auto runStarted = tick();
    if (running) {
        const auto deadline = runStarted + static_cast<std::uint64_t>(durationSeconds) * frequency;
        while (tick() < deadline) {
            MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            pumpMessages();
            const auto pendingResets = context->resetRequests.load(std::memory_order_acquire);
            if (pendingResets == handledResetRequests) continue;
            handledResetRequests = pendingResets;
            Json restart = {{"requestQpc", tick()}, {"requestCount", pendingResets}};
            allStops = driver->stop() == ASE_OK && allStops;
            running = false;
            active.store(nullptr, std::memory_order_release);
            allDisposals = driver->disposeBuffers() == ASE_OK && allDisposals;
            buffersCreated = false;
            allCloses = drivers.asioCloseDriver(selected) == 0 && allCloses;
            driver = nullptr;
            if (!allStops || !allDisposals || !allCloses) {
                restart["error"] = "old driver did not close cleanly";
                restartFailed = true;
            } else if (drivers.asioOpenDriver(selected, reinterpret_cast<void**>(&driver)) != 0 || !driver ||
                driver->init(hostWindow) != ASIOTrue) {
                restart["error"] = "driver did not reopen or initialize";
                restartFailed = true;
            } else {
                long newInputs = 0, newOutputs = 0, newMinimum = 0, newMaximum = 0;
                long newPreferred = 0, newGranularity = 0;
                ASIOSampleRate newRate = 0;
                bool ready = driver->getChannels(&newInputs, &newOutputs) == ASE_OK &&
                    driver->getBufferSize(&newMinimum, &newMaximum, &newPreferred, &newGranularity) == ASE_OK &&
                    driver->getSampleRate(&newRate) == ASE_OK && newPreferred > 0 && newRate > 0 &&
                    (!sending || newOutputs > static_cast<long>(channelIndex)) &&
                    (!receiving || newInputs > static_cast<long>(channelIndex));
                for (long i = 0; ready && i < bufferCount; ++i) {
                    ASIOChannelInfo channel{};
                    channel.isInput = buffers[i].isInput;
                    channel.channel = static_cast<long>(channelIndex);
                    ready = driver->getChannelInfo(&channel) == ASE_OK &&
                        channel.type == ASIOSTFloat32LSB;
                }
                if (!ready) {
                    restart["error"] = "new format or channel is unavailable";
                    restartFailed = true;
                } else {
                    preferred = newPreferred;
                    rate = newRate;
                    context->frames = preferred;
                    context->inputBuffers[0] = context->inputBuffers[1] = nullptr;
                    context->outputBuffers[0] = context->outputBuffers[1] = nullptr;
                    active.store(context.get(), std::memory_order_release);
                    if (driver->createBuffers(buffers, bufferCount, preferred, &callbacks) != ASE_OK) {
                        restart["error"] = "new buffers could not be created";
                        restartFailed = true;
                        active.store(nullptr, std::memory_order_release);
                    } else {
                        buffersCreated = true;
                        if (receiving) {
                            context->inputBuffers[0] = static_cast<float*>(buffers[0].buffers[0]);
                            context->inputBuffers[1] = static_cast<float*>(buffers[0].buffers[1]);
                        }
                        if (sending) {
                            const auto outputIndex = receiving ? 1 : 0;
                            context->outputBuffers[0] = static_cast<float*>(buffers[outputIndex].buffers[0]);
                            context->outputBuffers[1] = static_cast<float*>(buffers[outputIndex].buffers[1]);
                        }
                        running = driver->start() == ASE_OK;
                        if (!running) {
                            restart["error"] = "driver did not restart";
                            restartFailed = true;
                        } else {
                            restart["sampleRate"] = rate;
                            restart["bufferFrames"] = preferred;
                            restart["resumedQpc"] = tick();
                            context->push({tick(), HostRestart, static_cast<std::int32_t>(preferred)});
                        }
                    }
                }
            }
            hostRestarts.push_back(std::move(restart));
            if (restartFailed) break;
        }
    }
    const auto runEnded = tick();
    if (driver && running) allStops = driver->stop() == ASE_OK && allStops;
    active.store(nullptr, std::memory_order_release);
    if (driver && buffersCreated)
        allDisposals = driver->disposeBuffers() == ASE_OK && allDisposals;
    if (driver) allCloses = drivers.asioCloseDriver(selected) == 0 && allCloses;
    const bool stopped = allStops;
    const bool disposed = allDisposals;
    const bool closed = allCloses;
    context->done.store(true, std::memory_order_release);
    writer.join();
    if (!eventFile.good())
        report["findings"].push_back({{"code", "event-file-write-failed"}});
    report["runStartedQpc"] = runStarted;
    report["runEndedQpc"] = runEnded;
    report["callbackCount"] = context->callbacks.load(std::memory_order_relaxed);
    report["resetRequests"] = context->resetRequests.load(std::memory_order_relaxed);
    report["hostRestarts"] = std::move(hostRestarts);
    report["finalSampleRate"] = rate;
    report["finalBufferFrames"] = preferred;
    report["measuredBlocks"] = measuredBlocks;
    report["droppedCallbackEvents"] = context->dropped.load(std::memory_order_relaxed);
    report["corruptedSamples"] = corrupted;
    report["missingSamples"] = missing;
    report["duplicatedSamples"] = duplicated;
    report["preSyncSamples"] = preSync;
    report["signalSynchronized"] = context->everSynchronized.load(std::memory_order_relaxed);
    report["stopped"] = stopped;
    report["disposed"] = disposed;
    report["closed"] = closed;
    if (!initialStartSucceeded) report["findings"].push_back({{"code", "start-failed"}});
    if (restartFailed) report["findings"].push_back({{"code", "host-reset-restart-failed"}});
    if (!stopped) report["findings"].push_back({{"code", "stop-failed"}});
    if (!disposed) report["findings"].push_back({{"code", "dispose-failed"}});
    if (!closed) report["findings"].push_back({{"code", "unload-failed"}});
    if (context->dropped.load(std::memory_order_relaxed))
        report["findings"].push_back({{"code", "callback-ring-overflow"}});
    if (receiving && !context->everSynchronized.load(std::memory_order_relaxed))
        report["findings"].push_back({{"code", "loopback-signal-not-detected"}});
    if (measuredBlocks >= minCallbacks &&
        context->dropped.load(std::memory_order_relaxed) == 0) {
        if (receiving && corrupted > maxCorrupted)
            report["findings"].push_back({{"code", "corrupted-samples"}, {"count", corrupted}});
        if (receiving && missing > maxMissing)
            report["findings"].push_back({{"code", "missing-samples"}, {"count", missing}});
        if (receiving && duplicated > maxDuplicated)
            report["findings"].push_back({{"code", "duplicated-samples"}, {"count", duplicated}});
        report["verdict"] = report["findings"].empty() ? "pass" : "fail";
    } else {
        report["precondition"] = "too few callbacks or callback ring overflow";
        report["verdict"] = report["findings"].empty() ? "inconclusive" : "fail";
    }
    report["finishedQpc"] = tick();
    return report;
}
