#include "audio_transport.h"

#include <windows.h>
#include <objbase.h>
#include <iasiodrv.h>

#include <array>
#include <algorithm>
#include <cstdio>
#include <future>
#include <string>
#include <vector>

#include "vasio_compatibility_profile.h"

namespace {
constexpr long kFrames = 256;
constexpr long kTransportChannels = 256;
constexpr long kChannelsPerDirection = 3;
std::array<long, kChannelsPerDirection> gSelectedChannels{};
std::array<ASIOBufferInfo, kChannelsPerDirection * 2> gBuffers{};
HANDLE gCallbackEvent = nullptr;
volatile LONG gCallbackErrors = 0;

void bufferSwitch(long index, ASIOBool) {
    for (const auto& info : gBuffers) {
        auto* samples = static_cast<float*>(info.buffers[index]);
        if (!samples) { InterlockedIncrement(&gCallbackErrors); continue; }
        const float value = static_cast<float>(info.channelNum + 1) * 0.01f;
        if (info.isInput) {
            for (long frame = 0; frame < kFrames; ++frame)
                if (samples[frame] != value) InterlockedIncrement(&gCallbackErrors);
        } else {
            const float output = static_cast<float>(info.channelNum + 1) * 0.1f;
            for (long frame = 0; frame < kFrames; ++frame) samples[frame] = output;
        }
    }
    SetEvent(gCallbackEvent);
}

void sampleRateChanged(ASIOSampleRate) {}
long asioMessages(long, long, void*, double*) { return 0; }
ASIOTime* bufferSwitchTimeInfo(ASIOTime*, long index, ASIOBool direct) {
    bufferSwitch(index, direct);
    return nullptr;
}

bool waitOutput(AudioClientMapping& mapping) {
    const ULONGLONG deadline = GetTickCount64() + 3000;
    while (GetTickCount64() < deadline) {
        if (mapping.ClientOutputAvailable() >= kFrames) return true;
        Sleep(1);
    }
    return false;
}

bool verifyBufferLimits(IASIO* driver, ASIOCallbacks* callbacks) {
    const auto announcedChannels = VasioCompatibilityProfile::ChannelCountsForCurrentProcess();
    std::array<ASIOBufferInfo, 513> requests{};
    for (long channel = 0; channel < announcedChannels.inputs; ++channel) {
        requests[channel].isInput = ASIOTrue;
        requests[channel].channelNum = channel;
    }
    for (long channel = 0; channel < announcedChannels.outputs; ++channel) {
        const auto index = announcedChannels.inputs + channel;
        requests[index].isInput = ASIOFalse;
        requests[index].channelNum = channel;
    }
    const long totalChannels = announcedChannels.inputs + announcedChannels.outputs;
    if (driver->createBuffers(requests.data(), totalChannels, kFrames, callbacks) != ASE_OK) return false;
    if (driver->disposeBuffers() != ASE_OK) return false;
    if (driver->createBuffers(requests.data(), 513, kFrames, callbacks) != ASE_InvalidParameter) return false;

    for (const auto direction : {ASIOTrue, ASIOFalse}) {
        ASIOBufferInfo invalid{};
        invalid.isInput = direction;
        invalid.channelNum = direction == ASIOTrue
            ? announcedChannels.inputs : announcedChannels.outputs;
        if (driver->createBuffers(&invalid, 1, kFrames, callbacks) != ASE_InvalidParameter) return false;
    }

    std::array<ASIOBufferInfo, 2> duplicate{};
    duplicate[0].isInput = duplicate[1].isInput = ASIOTrue;
    duplicate[0].channelNum = duplicate[1].channelNum = 0;
    return driver->createBuffers(duplicate.data(), 2, kFrames, callbacks) == ASE_InvalidParameter;
}
}

int wmain(int argc, wchar_t** argv) {
    const bool expectUnavailable = argc == 6 && std::wstring(argv[5]) == L"--expect-unavailable";
    if ((argc != 5 && !expectUnavailable) || std::wstring(argv[1]) != L"--dll" || std::wstring(argv[3]) != L"--id") {
        std::fwprintf(stderr, L"Usage: DriverAudioProbe --dll <path> --id 1 [--expect-unavailable]\n");
        return 2;
    }
    const int driverId = _wtoi(argv[4]);
    if (driverId != 1) return 2;
    constexpr wchar_t clsidText[] = L"{A4D39126-78CB-4D89-9E0A-54494D4F5856}";
    CLSID clsid{};
    if (FAILED(CLSIDFromString(clsidText, &clsid))) return 3;

    HMODULE module = LoadLibraryW(argv[2]);
    if (!module) { std::fwprintf(stderr, L"LoadLibraryW failed: %lu\n", GetLastError()); return 4; }
    using GetClassObject = HRESULT (WINAPI*)(REFCLSID, REFIID, void**);
    auto getClassObject = reinterpret_cast<GetClassObject>(GetProcAddress(module, "DllGetClassObject"));
    if (!getClassObject) { FreeLibrary(module); return 5; }
    IClassFactory* factory = nullptr;
    if (FAILED(getClassObject(clsid, IID_IClassFactory, reinterpret_cast<void**>(&factory)))) {
        FreeLibrary(module); return 6;
    }
    IASIO* driver = nullptr;
    const HRESULT created = factory->CreateInstance(nullptr, clsid, reinterpret_cast<void**>(&driver));
    factory->Release();
    if (FAILED(created) || !driver) { FreeLibrary(module); return 7; }

    long inputChannels = 0;
    long outputChannels = 0;
    const auto announcedChannels = VasioCompatibilityProfile::ChannelCountsForCurrentProcess();
    if (driver->getChannels(&inputChannels, &outputChannels) != ASE_OK ||
        inputChannels != announcedChannels.inputs || outputChannels != announcedChannels.outputs) {
        std::fprintf(stderr, "expected %ld/%ld ASIO channels, got %ld/%ld\n",
                     announcedChannels.inputs, announcedChannels.outputs,
                     inputChannels, outputChannels);
        driver->Release();
        FreeLibrary(module);
        return 9;
    }
    ASIOChannelInfo highChannel{};
    highChannel.isInput = ASIOTrue;
    highChannel.channel = announcedChannels.inputs - 1;
    if (driver->getChannelInfo(&highChannel) != ASE_OK) {
        driver->Release();
        FreeLibrary(module);
        return 10;
    }
    highChannel.isInput = ASIOFalse;
    highChannel.channel = announcedChannels.outputs - 1;
    if (driver->getChannelInfo(&highChannel) != ASE_OK) {
        driver->Release();
        FreeLibrary(module);
        return 10;
    }

    if (expectUnavailable) {
        const bool refused = driver->init(GetConsoleWindow()) == ASIOFalse;
        char error[128]{};
        driver->getErrorMessage(error);
        const bool passed = refused && std::string(error).find("not connected") != std::string::npos;
        driver->Release();
        FreeLibrary(module);
        if (!passed) {
            std::fprintf(stderr, "driver did not report a missing engine connection\n");
            return 8;
        }
        std::printf("PASS: TimoxVasio refuses initialization when the engine is absent.\n");
        return 0;
    }

    auto engineFuture = std::async(std::launch::async, [driverId]() {
        const auto pid = GetCurrentProcessId();
        const ULONGLONG deadline = GetTickCount64() + 2000;
        while (GetTickCount64() < deadline) {
            auto engine = AudioClientMapping::OpenEngine(static_cast<std::uint32_t>(driverId), pid, 0);
            if (engine) {
                engine->SetEngineAttached(true);
                return engine;
            }
            Sleep(2);
        }
        return std::unique_ptr<AudioClientMapping>{};
    });

    bool passed = driver->init(GetConsoleWindow()) == ASIOTrue;
    auto engine = engineFuture.get();
    passed = passed && engine != nullptr;
    if (passed) passed = driver->setSampleRate(48000.0) == ASE_OK;

    const long commonChannels = (std::min)(announcedChannels.inputs, announcedChannels.outputs);
    gSelectedChannels = {{0, (std::min)(127L, commonChannels - 2), commonChannels - 1}};
    for (long index = 0; index < kChannelsPerDirection; ++index) {
        const long channel = gSelectedChannels[index];
        gBuffers[index] = {};
        gBuffers[index].isInput = ASIOTrue;
        gBuffers[index].channelNum = channel;
        gBuffers[kChannelsPerDirection + index] = {};
        gBuffers[kChannelsPerDirection + index].isInput = ASIOFalse;
        gBuffers[kChannelsPerDirection + index].channelNum = channel;
    }
    ASIOCallbacks callbacks{};
    callbacks.bufferSwitch = bufferSwitch;
    callbacks.sampleRateDidChange = sampleRateChanged;
    callbacks.asioMessage = asioMessages;
    callbacks.bufferSwitchTimeInfo = bufferSwitchTimeInfo;
    if (passed) passed = verifyBufferLimits(driver, &callbacks);
    if (passed) passed = driver->createBuffers(gBuffers.data(), static_cast<long>(gBuffers.size()), kFrames, &callbacks) == ASE_OK;
    gCallbackEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!gCallbackEvent) passed = false;
    if (passed) passed = driver->start() == ASE_OK;

    if (passed) {
        std::vector<float> input(kFrames * kTransportChannels);
        for (long frame = 0; frame < kFrames; ++frame)
            for (long channelIndex = 0; channelIndex < kChannelsPerDirection; ++channelIndex) {
                const auto channel = gSelectedChannels[channelIndex];
                input[frame * kTransportChannels + channel] = static_cast<float>(channel + 1) * 0.01f;
            }
        passed = engine->WriteEngineInput(input.data(), kFrames) && engine->SignalClient();
        passed = passed && WaitForSingleObject(gCallbackEvent, 3000) == WAIT_OBJECT_0;
        passed = passed && waitOutput(*engine);
        std::vector<float> output(kFrames * kTransportChannels);
        passed = passed && engine->ReadClientOutput(output.data(), kFrames) == kFrames;
        for (long frame = 0; frame < kFrames && passed; ++frame)
            for (long channelIndex = 0; channelIndex < kChannelsPerDirection; ++channelIndex) {
                const auto channel = gSelectedChannels[channelIndex];
                if (output[frame * kTransportChannels + channel] != static_cast<float>(channel + 1) * 0.1f)
                    passed = false;
            }
        passed = passed && InterlockedCompareExchange(&gCallbackErrors, 0, 0) == 0;
    }

    if (driver->stop() != ASE_OK) passed = false;
    if (driver->disposeBuffers() != ASE_OK) passed = false;
    if (gCallbackEvent) CloseHandle(gCallbackEvent);
    driver->Release();
    FreeLibrary(module);
    if (!passed) {
        std::fprintf(stderr, "TimoxVasio callback transport probe failed.\n");
        return 8;
    }
    std::printf("PASS: TimoxVasio ASIO callbacks exchange sparse channels up to %ld through shared memory.\n",
                gSelectedChannels.back());
    return 0;
}
