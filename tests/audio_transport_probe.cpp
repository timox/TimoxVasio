#include "audio_transport.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr std::uint32_t kRate = 48000, kFrames = 256;
using Mask = AudioClientMapping::ChannelMask;

Mask mask(std::initializer_list<unsigned> channels) {
    Mask result{};
    for (auto c : channels) result[c / 64] |= std::uint64_t{1} << (c % 64);
    return result;
}

bool roundTrip(AudioClientMapping& client, AudioClientMapping& engine, unsigned frames) {
    const auto out = client.OutputStride(), in = client.InputStride();
    std::vector<float> source(static_cast<std::size_t>(frames) * out);
    std::vector<float> destination(source.size(), -9.0f);
    for (unsigned f = 0; f < frames; ++f)
        for (unsigned s = 0; s < out; ++s)
            source[static_cast<std::size_t>(f) * out + s] = static_cast<float>(f * 1000 + client.OutputChannelAt(s));
    if (!client.WriteClientOutput(source.data(), frames)) return false;
    if (engine.ClientOutputAvailable() != frames ||
        engine.ReadClientOutput(destination.data(), frames) != frames || destination != source) return false;
    std::vector<float> reply(static_cast<std::size_t>(frames) * in);
    std::vector<float> received(reply.size(), -9.0f);
    for (unsigned f = 0; f < frames; ++f)
        for (unsigned s = 0; s < in; ++s)
            reply[static_cast<std::size_t>(f) * in + s] = -static_cast<float>(f * 1000 + client.InputChannelAt(s));
    if (!engine.WriteEngineInput(reply.data(), frames)) return false;
    return client.ReadClientInput(received.data(), frames) == frames && received == reply;
}

bool caseFor(unsigned driver, const Mask& inputs, const Mask& outputs,
    unsigned expectedInput, unsigned expectedOutput) {
    const auto pid = GetCurrentProcessId();
    auto client = AudioClientMapping::CreateClient(driver, pid, kRate, kFrames);
    if (!client || client->AudioEpoch() != 0) return false;
    AudioClientMapping::StatusSnapshot status{};
    if (!AudioClientMapping::ReadStatusReadOnly(driver, pid, status) ||
        !status.available || status.layoutVersion != 9 || status.processId != pid ||
        status.requestedSampleRate != kRate) return false;
    auto preAudioEngine = AudioClientMapping::OpenEngine(driver, pid, kRate);
    if (!preAudioEngine || preAudioEngine->AudioEpoch() != 0) return false;
    if (!client->SetActiveChannels(inputs, outputs) || client->AudioEpoch() == 0) return false;
    auto engine = AudioClientMapping::OpenEngine(driver, pid, kRate);
    if (!engine || engine->AudioEpoch() != client->AudioEpoch() ||
        engine->InputStride() != expectedInput || engine->OutputStride() != expectedOutput ||
        client->AudioGenerationAcknowledged()) return false;
    if (engine->AckAudioGeneration(44100, kFrames) || engine->AckAudioGeneration(kRate, 512) ||
        !engine->AckAudioGeneration(kRate, kFrames) || !client->AudioGenerationAcknowledged()) return false;
    Mask readIn{}, readOut{};
    if (!engine->GetActiveChannels(readIn, readOut) || readIn != inputs || readOut != outputs) return false;
    for (unsigned c = 0; c < 256; ++c) {
        const auto inSlot = engine->InputSlot(c), outSlot = engine->OutputSlot(c);
        if ((inSlot >= 0) != ((inputs[c / 64] & (std::uint64_t{1} << (c % 64))) != 0) ||
            (outSlot >= 0) != ((outputs[c / 64] & (std::uint64_t{1} << (c % 64))) != 0)) return false;
        if (inSlot >= 0 && engine->InputChannelAt(inSlot) != static_cast<int>(c)) return false;
        if (outSlot >= 0 && engine->OutputChannelAt(outSlot) != static_cast<int>(c)) return false;
    }
    if (!roundTrip(*client, *engine, 17)) return false;
    // Bring each ring to the boundary, then cross it in two contiguous copies.
    if (!roundTrip(*client, *engine, AudioClientMapping::kRingCapacityFrames - 17)) return false;
    if (!roundTrip(*client, *engine, 31)) return false;
    std::vector<float> silence(7u * expectedInput, 5.0f);
    if (client->ReadClientInput(silence.data(), 7) != 0 ||
        !std::all_of(silence.begin(), silence.end(), [](float x) { return x == 0.0f; }) ||
        client->ClientInputUnderruns() != 1) return false;
    std::vector<float> full(static_cast<std::size_t>(AudioClientMapping::kRingCapacityFrames) * expectedOutput);
    if (!client->WriteClientOutput(full.data(), AudioClientMapping::kRingCapacityFrames) ||
        client->WriteClientOutput(full.data(), 1) || client->ClientOutputOverruns() != 1) return false;
    if (engine->ReadClientOutput(full.data(), AudioClientMapping::kRingCapacityFrames) !=
        AudioClientMapping::kRingCapacityFrames) return false;
    // Stable control counters survive the next generation; old view remains pinned.
    const auto oldEpoch = engine->AudioEpoch();
    if (!client->SetActiveChannels({}, {}) || client->AudioEpoch() <= oldEpoch ||
        engine->PublishedAudioEpoch() == oldEpoch || engine->AudioEpoch() != oldEpoch ||
        engine->RefreshAudioGeneration()) return false;
    auto next = AudioClientMapping::OpenEngine(driver, pid, kRate);
    if (!next || next->AudioEpoch() != client->AudioEpoch() ||
        next->InputStride() || next->OutputStride() ||
        next->ClientOutputOverruns() != 1 || !next->AckAudioGeneration(kRate, kFrames)) return false;
    if (!client->RetireAudioGeneration() || client->PublishedAudioEpoch() != 0 ||
        next->PublishedAudioEpoch() != 0 || next->AudioEpoch() == 0) return false;
    if (!AudioClientMapping::ReadStatusReadOnly(driver, pid, status) ||
        status.outputOverruns != 1 || status.activeInputs != Mask{} || status.activeOutputs != Mask{}) return false;
    const auto retiredEpoch = next->AudioEpoch();
    if (!client->SetActiveChannels(inputs, outputs) || client->AudioEpoch() <= retiredEpoch ||
        next->AudioEpoch() != retiredEpoch) return false;
    auto afterRetire = AudioClientMapping::OpenEngine(driver, pid, kRate);
    return afterRetire && afterRetire->AudioEpoch() == client->AudioEpoch() &&
        afterRetire->AudioEpoch() > retiredEpoch &&
        afterRetire->InputStride() == expectedInput && afterRetire->OutputStride() == expectedOutput;
}

bool oldFormatRejected() {
    const auto pid = GetCurrentProcessId();
    for (unsigned version : {6u, 7u, 8u}) {
        const unsigned driver = 0xFF80 + version;
        const auto name = L"Local\\VASIO-Map" + std::wstring(version == 8 ? L"-v8-" : version == 7 ? L"-v7-" : L"-") +
            std::to_wstring(pid) + L"-" + std::to_wstring(driver);
        HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
            0, 4096, name.c_str());
        if (!map) return false;
        const bool rejected = !AudioClientMapping::OpenEngine(driver, pid, kRate);
        CloseHandle(map);
        if (!rejected) return false;
    }
    return true;
}

struct AudioHeader {
    std::uint32_t magic, version, size, driver, pid;
    alignas(8) std::uint64_t creation, epoch;
    std::uint32_t capacity, inputStride, outputStride;
    std::uint16_t inputChannels[256], outputChannels[256];
};

bool invalidAudioRejected() {
    const auto pid = GetCurrentProcessId();
    constexpr unsigned driver = 0xFF30;
    auto client = AudioClientMapping::CreateClient(driver, pid, kRate, kFrames);
    if (!client || !client->SetActiveChannels(mask({0, 255}), mask({3, 7}))) return false;
    const auto name = L"Local\\VASIO-Audio-v9-" + std::to_wstring(pid) + L"-" +
        std::to_wstring(driver) + L"-" + std::to_wstring(client->ProcessCreationTime()) +
        L"-" + std::to_wstring(client->AudioEpoch());
    HANDLE map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    if (!map) return false;
    auto* header = static_cast<AudioHeader*>(MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(AudioHeader)));
    if (!header) { CloseHandle(map); return false; }
    const auto rejects = [&](auto& field, auto bad) {
        const auto original = field;
        field = bad;
        const bool rejected = !AudioClientMapping::OpenEngine(driver, pid, kRate);
        field = original;
        return rejected;
    };
    bool okay = rejects(header->magic, 0u) && rejects(header->version, 7u) &&
        rejects(header->size, 0u) && rejects(header->creation, std::uint64_t{1}) &&
        rejects(header->epoch, std::uint64_t{0}) &&
        rejects(header->inputStride, 257u) &&
        rejects(header->inputChannels[1], std::uint16_t{0}) &&
        rejects(header->outputChannels[1], std::uint16_t{3});
    okay = okay && AudioClientMapping::OpenEngine(driver, pid, kRate) != nullptr;
    UnmapViewOfFile(header);
    CloseHandle(map);
    return okay;
}

bool sampledStages() {
    constexpr unsigned driver = 0xFF32;
    const auto pid = GetCurrentProcessId();
    auto client = AudioClientMapping::CreateClient(driver, pid, kRate, kFrames);
    if (!client || !client->SetActiveChannels({}, mask({0}))) return false;
    auto engine = AudioClientMapping::OpenEngine(driver, pid, kRate);
    if (!engine || !engine->AckAudioGeneration(kRate, kFrames)) return false;
    float block[16]{};
    float received[AudioClientMapping::kRingCapacityFrames]{};
    if (client->StageMeasurementsEnabled() ||
        !client->WriteClientOutputMeasured(block, 16, 10, 20, 1) ||
        engine->ReadClientOutput(received, 16) != 16) return false;
    auto counters = engine->ConsumeClientStageCounters();
    if (counters.hostCallback.samples || counters.ringWrite.samples ||
        counters.ringWait.samples || counters.ringWaitDiscardedBlocks) return false;
    engine->SetStageMeasurementsEnabled(true);
    if (!client->StageMeasurementsEnabled() ||
        !client->WriteClientOutputMeasured(block, 16, 10, 20, 1) ||
        engine->ReadClientOutput(received, 16) != 16) return false;
    counters = engine->ConsumeClientStageCounters();
    if (counters.epoch != client->AudioEpoch() || counters.hostCallback.samples != 1 ||
        counters.hostCallback.sumTicks != 10 || counters.ringWrite.samples != 1 ||
        counters.ringWait.samples != 1 || counters.ringWaitDiscardedBlocks) return false;
    constexpr auto capacity = AudioClientMapping::kRingCapacityFrames;
    if (!client->WriteClientOutput(received, capacity - 40) ||
        engine->ReadClientOutput(received, capacity - 40) != capacity - 40 ||
        !client->WriteClientOutputMeasured(block, 16, 11, 21, 1) ||
        engine->ReadClientOutput(received, 16) != 16) return false;
    counters = engine->ConsumeClientStageCounters();
    if (counters.ringWait.samples != 1 || counters.ringWaitDiscardedBlocks) return false;
    if (!client->WriteClientOutputMeasured(block, 16, 12, 22, 1) ||
        engine->ReadClientOutput(received, 8) != 8 ||
        engine->ReadClientOutput(received, 8) != 8) return false;
    counters = engine->ConsumeClientStageCounters();
    if (counters.ringWait.samples || counters.ringWaitDiscardedBlocks != 1) return false;
    if (!client->WriteClientOutput(received, capacity) ||
        client->WriteClientOutputMeasured(block, 16, 13, 23, 1) ||
        engine->ReadClientOutput(received, capacity) != capacity) return false;
    counters = engine->ConsumeClientStageCounters();
    if (counters.hostCallback.samples || counters.ringWrite.samples ||
        counters.ringWait.samples || counters.ringWaitDiscardedBlocks) return false;
    if (!client->WriteClientOutputMeasured(block, 16, 14, 24, 1) ||
        !client->SetActiveChannels({}, mask({0})) ||
        engine->ReadClientOutput(received, 16) != 16) return false;
    counters = engine->ConsumeClientStageCounters();
    if (counters.ringWait.samples || counters.ringWaitDiscardedBlocks != 1) return false;
    engine->SetStageMeasurementsEnabled(false);
    return !client->StageMeasurementsEnabled() &&
        engine->ConsumeClientStageCounters().ringWaitDiscardedBlocks == 0;
}

int engineChild(std::uint32_t pid) {
    constexpr unsigned driver = 0xFF31;
    auto engine = AudioClientMapping::OpenEngine(driver, pid, kRate);
    if (!engine || engine->AudioEpoch() == 0 || engine->InputStride() != 2 ||
        engine->OutputStride() != 2 || engine->InputChannelAt(0) != 0 ||
        engine->InputChannelAt(1) != 255 || engine->OutputChannelAt(0) != 3 ||
        engine->OutputChannelAt(1) != 255 || !engine->AckAudioGeneration(kRate, kFrames)) return 1;
    float output[8]{};
    if (engine->ReadClientOutput(output, 4) != 4) return 2;
    for (unsigned f = 0; f < 4; ++f)
        if (output[2 * f] != static_cast<float>(f + 3) ||
            output[2 * f + 1] != static_cast<float>(f + 255)) return 3;
    for (unsigned f = 0; f < 4; ++f) {
        output[2 * f] = -static_cast<float>(f);
        output[2 * f + 1] = -static_cast<float>(f + 255);
    }
    return engine->WriteEngineInput(output, 4) && engine->SignalClient() ? 0 : 4;
}

bool crossProcess() {
    constexpr unsigned driver = 0xFF31;
    const auto pid = GetCurrentProcessId();
    auto client = AudioClientMapping::CreateClient(driver, pid, kRate, kFrames);
    if (!client || !client->SetActiveChannels(mask({0, 255}), mask({3, 255}))) return false;
    const float output[8] = {3, 255, 4, 256, 5, 257, 6, 258};
    if (!client->WriteClientOutput(output, 4)) return false;
    wchar_t executable[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, executable, MAX_PATH)) return false;
    wchar_t command[2 * MAX_PATH]{};
    swprintf_s(command, L"\"%s\" --engine-child %u", executable, pid);
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command, nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, nullptr, &startup, &process)) return false;
    const auto waited = WaitForSingleObject(process.hProcess, 5000);
    DWORD code = 99;
    if (waited == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &code);
    else TerminateProcess(process.hProcess, 99);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (code != 0 || !client->AudioGenerationAcknowledged()) return false;
    float input[8]{};
    if (client->ReadClientInput(input, 4) != 4) return false;
    for (unsigned f = 0; f < 4; ++f)
        if (input[2 * f] != -static_cast<float>(f) ||
            input[2 * f + 1] != -static_cast<float>(f + 255)) return false;
    return true;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc == 3 && std::wstring(argv[1]) == L"--engine-child")
        return engineChild(static_cast<std::uint32_t>(_wtoi(argv[2])));
    Mask all{}; all.fill(~std::uint64_t{0});
    const bool passed =
        caseFor(0xFF20, {}, {}, 0, 0) &&
        caseFor(0xFF21, mask({0, 255}), mask({1, 254}), 2, 2) &&
        caseFor(0xFF22, mask({0, 3, 7, 63, 64, 127, 192, 255}), {}, 8, 0) &&
        caseFor(0xFF23, {}, mask({0, 3, 7, 63, 64, 127, 192, 255}), 0, 8) &&
        caseFor(0xFF24, all, all, 256, 256) &&
        invalidAudioRejected() && oldFormatRejected() && sampledStages() && crossProcess();
    std::puts(passed ? "PASS: compact v9 0/2/8/256, asymmetric, sparse, wrap, silence, overflow, epoch, stage samples, legacy rejection, cross-process exchange"
                     : "FAIL: compact v9 transport");
    return passed ? 0 : 1;
}
