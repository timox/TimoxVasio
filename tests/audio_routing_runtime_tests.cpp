#include "audio_routing_runtime.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace {
bool closeTo(float left, float right) { return std::fabs(left - right) < 1.0e-5f; }
}

int main() {
    constexpr std::uint32_t frames = 2;
    const auto pid = GetCurrentProcessId();
    auto mapping = AudioClientMapping::CreateClient(1, pid, 48000, 256);
    if (!mapping) {
        std::fprintf(stderr, "Could not create test audio mapping.\n");
        return 1;
    }
    mapping->SetEngineAttached(true);
    AudioClientMapping::ChannelMask activeInputs{};
    AudioClientMapping::ChannelMask activeOutputs{};
    activeInputs[0] = (std::uint64_t{1} << 0) | (std::uint64_t{1} << 1);
    activeInputs[3] = std::uint64_t{1} << 63;
    activeOutputs[0] = std::uint64_t{1};
    activeOutputs[3] = std::uint64_t{1} << 63;
    if (!mapping->SetActiveChannels(activeInputs, activeOutputs)) return 6;
    auto pinned = std::shared_ptr<AudioClientMapping>(std::move(mapping));
    VasioClientSnapshot client{{1, pid, L"probe.exe"}, pinned};
    AudioRoutingLayout layout;
    layout.physicalDriverId = "{driver-1}";
    layout.sampleRate = 48000;
    layout.bufferFrames = frames;
    layout.physicalInputChannels = 10;
    layout.physicalOutputChannels = 10;
    layout.clients.push_back(client);
    layout.routes = {
        {"virtual-to-physical", "virtual:TimoxVasio:" + std::to_string(pid) + ":output:1",
            "physical:{driver-1}:output:1", 0.0, false},
        {"physical-to-virtual", "physical:{driver-1}:input:1",
            "virtual:TimoxVasio:" + std::to_string(pid) + ":input:2", 0.0, false},
        {"virtual-loop", "virtual:TimoxVasio:" + std::to_string(pid) + ":output:1",
            "virtual:TimoxVasio:" + std::to_string(pid) + ":input:1", 0.0, false},
        {"high-output", "virtual:TimoxVasio:" + std::to_string(pid) + ":output:256",
            "physical:{driver-1}:output:1", 0.0, false},
        {"high-input", "physical:{driver-1}:input:1",
            "virtual:TimoxVasio:" + std::to_string(pid) + ":input:256", 0.0, false},
        {"physical-input-ten", "physical:{driver-1}:input:10",
            "virtual:TimoxVasio:" + std::to_string(pid) + ":input:2", 0.0, false},
        {"physical-output-ten", "virtual:TimoxVasio:" + std::to_string(pid) + ":output:1",
            "physical:{driver-1}:output:10", 0.0, false},
    };
    std::string error;
    auto runtime = AudioRoutingRuntime::Create(std::move(layout), error);
    if (!runtime) {
        std::fprintf(stderr, "Could not build test runtime: %s\n", error.c_str());
        return 2;
    }
    if (runtime->RoutedPhysicalInputChannels() != std::vector<std::uint32_t>{0, 9} ||
        runtime->RoutedPhysicalOutputChannels() != std::vector<std::uint32_t>{0, 9}) {
        std::fprintf(stderr, "Physical buffers inputs:");
        for (const auto channel : runtime->RoutedPhysicalInputChannels()) std::fprintf(stderr, " %u", channel);
        std::fprintf(stderr, "; outputs:");
        for (const auto channel : runtime->RoutedPhysicalOutputChannels()) std::fprintf(stderr, " %u", channel);
        std::fprintf(stderr, "\n");
        return 7;
    }

    float clientOutput[frames * AudioClientMapping::kChannelCount]{};
    clientOutput[0] = 0.25f; clientOutput[AudioClientMapping::kChannelCount] = -0.5f;
    clientOutput[255] = 0.375f;
    clientOutput[2 * AudioClientMapping::kChannelCount - 1] = -0.625f;
    if (!pinned->WriteClientOutput(clientOutput, frames)) return 3;
    const float physicalInputData[frames]{0.75f, 0.125f};
    const float physicalInputTenData[frames]{0.5f, -0.25f};
    const float* physicalInputs[10]{};
    physicalInputs[0] = physicalInputData;
    physicalInputs[9] = physicalInputTenData;
    float physicalOutputData[frames]{};
    float physicalOutputTenData[frames]{};
    float* physicalOutputs[10]{};
    physicalOutputs[0] = physicalOutputData;
    physicalOutputs[9] = physicalOutputTenData;
    runtime->Process(physicalInputs, physicalOutputs, frames);

    float clientInput[frames * AudioClientMapping::kChannelCount]{};
    if (pinned->ReadClientInput(clientInput, frames) != frames) return 4;
    if (!closeTo(physicalOutputData[0], 0.625f) || !closeTo(physicalOutputData[1], -1.125f) ||
        !closeTo(clientInput[0], 0.25f) || !closeTo(clientInput[AudioClientMapping::kChannelCount], -0.5f) ||
        !closeTo(clientInput[1], 1.25f) || !closeTo(clientInput[AudioClientMapping::kChannelCount + 1], -0.125f) ||
        !closeTo(physicalOutputTenData[0], 0.25f) || !closeTo(physicalOutputTenData[1], -0.5f) ||
        !closeTo(clientInput[255], 0.75f) ||
        !closeTo(clientInput[2 * AudioClientMapping::kChannelCount - 1], 0.125f)) {
        std::fprintf(stderr, "Runtime sample mismatch: physical=%f,%f input0=%f,%f input1=%f,%f input255=%f,%f\n",
            physicalOutputData[0], physicalOutputData[1], clientInput[0], clientInput[AudioClientMapping::kChannelCount],
            clientInput[1], clientInput[AudioClientMapping::kChannelCount + 1], clientInput[255],
            clientInput[2 * AudioClientMapping::kChannelCount - 1]);
        return 5;
    }
    std::puts("PASS: physical/virtual routes transfer samples through the preallocated runtime.");
    return 0;
}
