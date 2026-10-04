#include "audio_transport.h"

#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr std::uint32_t kRate = 48000;
constexpr std::uint32_t kBlockFrames = 256;
constexpr std::uint32_t kConfiguredPreferredFrames = 512;
constexpr std::uint32_t kBlocks = 128;

bool waitForFrames(AudioClientMapping& mapping, bool engineReadsOutput, std::uint32_t frames) {
    const ULONGLONG deadline = GetTickCount64() + 5000;
    while (GetTickCount64() < deadline) {
        const auto available = engineReadsOutput ? mapping.ClientOutputAvailable() : mapping.ClientInputAvailable();
        if (available >= frames) return true;
        Sleep(1);
    }
    return false;
}

float signal(std::uint32_t frame, std::uint32_t channel, float scale) {
    return (static_cast<float>(frame % 1000) * 0.001f + static_cast<float>(channel) * 0.01f) * scale;
}

int engineChild(std::uint32_t driver, std::uint32_t clientPid) {
    auto mapping = AudioClientMapping::OpenEngine(driver, clientPid, kRate);
    if (!mapping) return 10;
    if (AudioClientMapping::OpenEngine(driver, clientPid, 44100)) return 11;
    if (!mapping->SetPreferredBufferFrames(kConfiguredPreferredFrames)) return 16;
    if (mapping->SetPreferredBufferFrames(96) || mapping->SetPreferredBufferFrames(16384)) return 17;
    mapping->SetEngineAttached(true);
    {
        auto temporaryOwner = AudioClientMapping::OpenEngine(driver, clientPid, kRate);
        if (!temporaryOwner) return 18;
    }
    if (!mapping->EngineAttached()) return 19;

    std::vector<float> block(kBlockFrames * AudioClientMapping::kChannelCount);
    std::uint32_t frame = 0;
    for (std::uint32_t blockIndex = 0; blockIndex < kBlocks; ++blockIndex) {
        if (!waitForFrames(*mapping, true, kBlockFrames)) return 12;
        const auto count = mapping->ReadClientOutput(block.data(), kBlockFrames);
        if (count != kBlockFrames) return 13;
        for (std::uint32_t i = 0; i < kBlockFrames; ++i, ++frame) {
            for (std::uint32_t channel = 0; channel < AudioClientMapping::kChannelCount; ++channel) {
                if (block[i * AudioClientMapping::kChannelCount + channel] != signal(frame, channel, 1.0f)) return 14;
                block[i * AudioClientMapping::kChannelCount + channel] = signal(frame, channel, -1.0f);
            }
        }
        if (!mapping->WriteEngineInput(block.data(), kBlockFrames)) return 15;
        mapping->SignalClient();
    }
    return 0;
}

bool runOverflowAndUnderflowProbe() {
    const auto pid = GetCurrentProcessId();
    auto mapping = AudioClientMapping::CreateClient(4, pid, kRate, kBlockFrames);
    if (!mapping) return false;
    std::vector<float> full(AudioClientMapping::kRingCapacityFrames * AudioClientMapping::kChannelCount, 0.25f);
    if (!mapping->WriteClientOutput(full.data(), AudioClientMapping::kRingCapacityFrames)) return false;
    if (mapping->WriteClientOutput(full.data(), 1)) return false;
    if (mapping->ClientOutputOverruns() != 1 || mapping->ClientInputOverruns() != 0) return false;
    if (mapping->ReadClientOutput(full.data(), AudioClientMapping::kRingCapacityFrames) !=
        AudioClientMapping::kRingCapacityFrames) return false;

    std::fill(full.begin(), full.begin() + 4 * AudioClientMapping::kChannelCount, 1.0f);
    const auto count = mapping->ReadClientInput(full.data(), 4);
    if (count != 0 || mapping->ClientInputUnderruns() != 1) return false;
    if (!std::all_of(full.begin(), full.begin() + 4 * AudioClientMapping::kChannelCount,
                     [](float sample) { return sample == 0.0f; })) return false;
    if (mapping->ReadClientOutput(full.data(), 4) != 0 || mapping->ClientOutputUnderruns() != 1) return false;
    if (!mapping->WriteEngineInput(full.data(), AudioClientMapping::kRingCapacityFrames)) return false;
    if (mapping->WriteEngineInput(full.data(), 1) || mapping->ClientInputOverruns() != 1) return false;
    return true;
}

bool runHighChannelProbe() {
    constexpr std::uint32_t kProbeDriver = 0xFF01;
    constexpr std::uint32_t kChannelCount = 256;
    const auto pid = GetCurrentProcessId();
    auto mapping = AudioClientMapping::CreateClient(kProbeDriver, pid, kRate, kBlockFrames);
    if (!mapping) return false;

    AudioClientMapping::ChannelMask inputChannels{};
    AudioClientMapping::ChannelMask outputChannels{};
    inputChannels[0] = (std::uint64_t{1} << 0) | (std::uint64_t{1} << 63);
    inputChannels[3] = std::uint64_t{1} << 63;
    outputChannels[1] = std::uint64_t{1} << 63;
    if (!mapping->SetActiveChannels(inputChannels, outputChannels)) return false;
    AudioClientMapping::ChannelMask readInputs{};
    AudioClientMapping::ChannelMask readOutputs{};
    if (!mapping->GetActiveChannels(readInputs, readOutputs) ||
        readInputs != inputChannels || readOutputs != outputChannels) return false;

    std::vector<float> block(kChannelCount, 0.0f);
    block[0] = 0.125f;
    block[127] = -0.375f;
    block[255] = 0.625f;
    if (!mapping->WriteClientOutput(block.data(), 1)) return false;
    std::fill(block.begin(), block.end(), 0.0f);
    if (mapping->ReadClientOutput(block.data(), 1) != 1 ||
        block[0] != 0.125f || block[127] != -0.375f || block[255] != 0.625f) return false;

    std::fill(block.begin(), block.end(), 0.0f);
    block[0] = -0.25f;
    block[127] = 0.5f;
    block[255] = -0.75f;
    if (!mapping->WriteEngineInput(block.data(), 1)) return false;
    std::fill(block.begin(), block.end(), 0.0f);
    if (mapping->ReadClientInput(block.data(), 1) != 1 ||
        block[0] != -0.25f || block[127] != 0.5f || block[255] != -0.75f) return false;
    return true;
}

struct LegacyTransportHeader {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t driverId;
    std::uint32_t processId;
    LONG sampleRate;
    std::uint32_t channelCount;
    std::uint32_t capacityFrames;
    LONG blockFrames;
    LONG preferredBufferFrames;
};

bool runLegacyVersionProbe() {
    const auto pid = GetCurrentProcessId();
    const auto rejectsOldMapping = [pid](std::uint32_t driverId, std::uint32_t version, std::uint32_t channels) {
        const auto mapName = L"Local\\VASIO-Map-" + std::to_wstring(pid) + L"-" + std::to_wstring(driverId);
        const auto eventName = L"Local\\VASIO-Event-" + std::to_wstring(pid) + L"-" + std::to_wstring(driverId);
        HANDLE legacyMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
            0, 64u * 1024u * 1024u, mapName.c_str());
        if (!legacyMap || GetLastError() == ERROR_ALREADY_EXISTS) {
            if (legacyMap) CloseHandle(legacyMap);
            return false;
        }
        HANDLE legacyEvent = CreateEventW(nullptr, FALSE, FALSE, eventName.c_str());
        void* view = MapViewOfFile(legacyMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LegacyTransportHeader));
        if (!legacyEvent || !view) {
            if (view) UnmapViewOfFile(view);
            if (legacyEvent) CloseHandle(legacyEvent);
            CloseHandle(legacyMap);
            return false;
        }
        auto* header = static_cast<LegacyTransportHeader*>(view);
        *header = {0x56534154, version, driverId, pid, static_cast<LONG>(kRate), channels,
            AudioClientMapping::kRingCapacityFrames, static_cast<LONG>(kBlockFrames), static_cast<LONG>(kBlockFrames)};
        UnmapViewOfFile(view);

        const bool rejected = !AudioClientMapping::CreateClient(driverId, pid, kRate, kBlockFrames);
        CloseHandle(legacyEvent);
        CloseHandle(legacyMap);
        return rejected;
    };

    return rejectsOldMapping(0xFF03, 4, 6) &&
           rejectsOldMapping(0xFF04, 5, AudioClientMapping::kChannelCount);
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc == 4 && std::wstring(argv[1]) == L"--engine") {
        return engineChild(static_cast<std::uint32_t>(_wtoi(argv[2])),
                           static_cast<std::uint32_t>(_wtoi(argv[3])));
    }

    if (!runLegacyVersionProbe()) {
        std::fprintf(stderr, "legacy shared mapping was not rejected\n");
        return 8;
    }

    const auto driver = 3u;
    const auto pid = GetCurrentProcessId();
    auto mapping = AudioClientMapping::CreateClient(driver, pid, kRate, kBlockFrames);
    if (!mapping) {
        std::fprintf(stderr, "CreateClient failed\n");
        return 1;
    }
    if (mapping->PreferredBufferFrames() != kBlockFrames) {
        std::fprintf(stderr, "initial preferred buffer size mismatch\n");
        return 5;
    }

    wchar_t executable[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, executable, MAX_PATH)) return 2;
    wchar_t commandLine[2 * MAX_PATH]{};
    swprintf_s(commandLine, L"\"%s\" --engine %u %u", executable, driver, pid);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, commandLine, nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &startup, &process)) {
        std::fprintf(stderr, "CreateProcessW failed: %lu\n", GetLastError());
        return 3;
    }

    const ULONGLONG preferenceDeadline = GetTickCount64() + 5000;
    while (mapping->PreferredBufferFrames() != kConfiguredPreferredFrames &&
           GetTickCount64() < preferenceDeadline) Sleep(1);
    if (mapping->PreferredBufferFrames() != kConfiguredPreferredFrames) {
        TerminateProcess(process.hProcess, 6);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        std::fprintf(stderr, "engine preferred buffer size was not shared with the VASIO client\n");
        return 6;
    }
    auto reopenedMapping = AudioClientMapping::CreateClient(driver, pid, 44100, kBlockFrames);
    if (!reopenedMapping || !reopenedMapping->EngineAttached() ||
        reopenedMapping->SampleRate() != kRate ||
        reopenedMapping->PreferredBufferFrames() != kConfiguredPreferredFrames) {
        TerminateProcess(process.hProcess, 7);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        std::fprintf(stderr, "an engine-held client mapping could not be reopened without resetting it\n");
        return 7;
    }

    std::vector<float> block(kBlockFrames * AudioClientMapping::kChannelCount);
    std::uint32_t frame = 0;
    bool passed = true;
    for (std::uint32_t blockIndex = 0; blockIndex < kBlocks && passed; ++blockIndex) {
        for (std::uint32_t i = 0; i < kBlockFrames; ++i, ++frame)
            for (std::uint32_t channel = 0; channel < AudioClientMapping::kChannelCount; ++channel)
                block[i * AudioClientMapping::kChannelCount + channel] = signal(frame, channel, 1.0f);

        const ULONGLONG deadline = GetTickCount64() + 5000;
        while (!mapping->WriteClientOutput(block.data(), kBlockFrames) && GetTickCount64() < deadline) Sleep(1);
        if (!waitForFrames(*mapping, false, kBlockFrames)) { passed = false; break; }
        if (mapping->ReadClientInput(block.data(), kBlockFrames) != kBlockFrames) { passed = false; break; }
        const auto firstFrame = frame - kBlockFrames;
        for (std::uint32_t i = 0; i < kBlockFrames && passed; ++i)
            for (std::uint32_t channel = 0; channel < AudioClientMapping::kChannelCount; ++channel)
                if (block[i * AudioClientMapping::kChannelCount + channel] != signal(firstFrame + i, channel, -1.0f))
                    passed = false;
    }

    WaitForSingleObject(process.hProcess, 7000);
    DWORD childExit = 0;
    GetExitCodeProcess(process.hProcess, &childExit);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    passed = passed && childExit == 0 && runOverflowAndUnderflowProbe();
    passed = passed && runHighChannelProbe();
    if (!passed) {
        std::fprintf(stderr, "cross-process ordering, wrap, overflow or underflow check failed (child=%lu)\n", childExit);
        return 4;
    }
    std::puts("PASS: two-process ordering, bidirectional wrap, overflow rejection and zero-filled underflow.");
    return 0;
}
