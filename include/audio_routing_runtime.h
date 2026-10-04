#pragma once

#include "audio_meter.h"
#include "physical_asio_host.h"
#include "routing_graph.h"
#include "vasio_client_manager.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

struct AudioRoutingLayout {
    std::string physicalDriverId;
    std::uint32_t sampleRate = 0;
    std::uint32_t bufferFrames = 0;
    long physicalInputChannels = 0;
    long physicalOutputChannels = 0;
    std::vector<VasioClientSnapshot> clients;
    std::vector<AudioRoute> routes;
};

// All vectors and the immutable graph are built off the audio thread. Process()
// only reads/writes their fixed storage and the shared-memory rings.
class AudioRoutingRuntime final {
public:
    static std::unique_ptr<AudioRoutingRuntime> Create(AudioRoutingLayout layout,
        std::string& error);
    AudioRoutingRuntime(const AudioRoutingRuntime&) = delete;
    AudioRoutingRuntime& operator=(const AudioRoutingRuntime&) = delete;

    void Process(const float* const* physicalInputs, float* const* physicalOutputs,
                 std::uint32_t frames) noexcept;
    std::vector<AudioMeterSnapshot> ReadMeters();
    const std::vector<std::uint32_t>& RoutedPhysicalInputChannels() const noexcept {
        return routedPhysicalInputChannels_;
    }
    const std::vector<std::uint32_t>& RoutedPhysicalOutputChannels() const noexcept {
        return routedPhysicalOutputChannels_;
    }
    static void PhysicalCallback(const float* const* inputs, float* const* outputs,
        std::uint32_t frames, void* context) noexcept;

private:
    struct ClientBuffers {
        VasioClientSnapshot client;
        std::array<std::size_t, AudioClientMapping::kChannelCount> inputEndpointIndices{};
        std::array<std::size_t, AudioClientMapping::kChannelCount> outputEndpointIndices{};
        std::vector<float> clientOutput;
        std::vector<float> clientInput;
    };
    struct MeterBinding {
        std::string endpointId;
        std::size_t endpointIndex = 0;
        AudioClientMapping* clientMapping = nullptr;
        bool clientOutput = false;
    };

    AudioRoutingRuntime(std::uint32_t frames, std::size_t endpointCount,
        long inputChannels, long outputChannels);

    std::uint32_t bufferFrames_;
    std::size_t endpointCount_;
    long physicalInputChannels_;
    long physicalOutputChannels_;
    std::size_t physicalOutputBase_;
    std::vector<std::uint32_t> routedPhysicalInputChannels_;
    std::vector<std::uint32_t> routedPhysicalOutputChannels_;
    std::unique_ptr<RoutingGraph> graph_;
    std::vector<float> sourceSamples_;
    std::vector<float> destinationSamples_;
    std::vector<const float*> sourceBlocks_;
    std::vector<float*> destinationBlocks_;
    std::vector<ClientBuffers> clients_;
    std::unique_ptr<std::atomic<float>[]> peakLinear_;
    std::vector<MeterBinding> meterBindings_;
};
