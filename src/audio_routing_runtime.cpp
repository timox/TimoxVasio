#include "audio_routing_runtime.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace {
std::string virtualPrefix(std::uint32_t driverId, std::uint32_t processId) {
    (void)driverId;
    return "virtual:TimoxVasio:" + std::to_string(processId) + ":";
}

bool physicalChannel(const std::string& endpoint, const std::string& prefix, std::uint32_t& channel) {
    if (endpoint.rfind(prefix, 0) != 0) return false;
    const auto text = endpoint.data() + prefix.size();
    const auto end = endpoint.data() + endpoint.size();
    std::uint32_t oneBased = 0;
    const auto result = std::from_chars(text, end, oneBased);
    if (result.ec != std::errc{} || result.ptr != end || oneBased == 0) return false;
    channel = oneBased - 1;
    return true;
}
}

AudioRoutingRuntime::AudioRoutingRuntime(std::uint32_t frames, std::uint32_t sampleRate,
    std::size_t endpointCount,
    long inputChannels, long outputChannels)
    : bufferFrames_(frames), sampleRate_(sampleRate), endpointCount_(endpointCount),
      physicalInputChannels_(inputChannels), physicalOutputChannels_(outputChannels),
      physicalOutputBase_(static_cast<std::size_t>(inputChannels)),
      sourceSamples_(endpointCount * frames, 0.0f),
      destinationSamples_(endpointCount * frames, 0.0f),
      sourceBlocks_(endpointCount, nullptr), destinationBlocks_(endpointCount, nullptr),
      peakLinear_(std::make_unique<std::atomic<float>[]>(endpointCount)) {
    static_assert(std::atomic<float>::is_always_lock_free,
        "Realtime peak meters require lock-free float atomics");
    for (std::size_t index = 0; index < endpointCount_; ++index)
        peakLinear_[index].store(0.0f, std::memory_order_relaxed);
}

std::unique_ptr<AudioRoutingRuntime> AudioRoutingRuntime::Create(AudioRoutingLayout layout,
    std::string& error) {
    error.clear();
    if (layout.sampleRate == 0 || layout.bufferFrames == 0 ||
        layout.physicalInputChannels < 0 || layout.physicalOutputChannels < 0) {
        error = "Audio runtime requires positive rate and buffer size and non-negative channel counts";
        return {};
    }

    std::vector<RoutingEndpoint> endpoints;
    endpoints.reserve(static_cast<std::size_t>(layout.physicalInputChannels) +
        static_cast<std::size_t>(layout.physicalOutputChannels));
    for (long channel = 0; channel < layout.physicalInputChannels; ++channel) {
        endpoints.push_back({"physical:" + layout.physicalDriverId + ":input:" +
            std::to_string(channel + 1), RoutingEndpointType::PhysicalInput, layout.sampleRate});
    }
    for (long channel = 0; channel < layout.physicalOutputChannels; ++channel) {
        endpoints.push_back({"physical:" + layout.physicalDriverId + ":output:" +
            std::to_string(channel + 1), RoutingEndpointType::PhysicalOutput, layout.sampleRate});
    }

    struct ClientIndices {
        std::array<std::size_t, AudioClientMapping::kChannelCount> inputs;
        std::array<std::size_t, AudioClientMapping::kChannelCount> outputs;
    };
    std::vector<ClientIndices> clientIndices;
    clientIndices.reserve(layout.clients.size());
    std::vector<VasioClientSnapshot> activeClients;
    activeClients.reserve(layout.clients.size());
    for (auto& client : layout.clients) {
        if (client.info.driverId != 1 || !client.mapping) continue;
        if (client.mapping->SampleRate() != layout.sampleRate) {
            error = "VASIO client sample rate differs from the selected physical clock";
            return {};
        }
        if (!client.mapping->EngineAttached()) {
            error = "VASIO client is no longer attached to this engine";
            return {};
        }
        AudioClientMapping::ChannelMask activeInputs{};
        AudioClientMapping::ChannelMask activeOutputs{};
        if (!client.mapping->GetActiveChannels(activeInputs, activeOutputs)) {
            error = "Unable to read a consistent TimoxVasio channel snapshot";
            return {};
        }
        ClientIndices indices{};
        constexpr auto inactiveEndpoint = static_cast<std::size_t>(-1);
        indices.inputs.fill(inactiveEndpoint);
        indices.outputs.fill(inactiveEndpoint);
        const auto prefix = virtualPrefix(client.info.driverId, client.info.processId);
        for (std::uint32_t channel = 0; channel < AudioClientMapping::kChannelCount; ++channel) {
            const auto apiChannel = channel + 1;
            const auto active = std::uint64_t{1} << (channel % 64);
            if (activeInputs[channel / 64] & active) {
                indices.inputs[channel] = endpoints.size();
                endpoints.push_back({prefix + "input:" + std::to_string(apiChannel),
                    RoutingEndpointType::VirtualInput, layout.sampleRate});
            }
            if (activeOutputs[channel / 64] & active) {
                indices.outputs[channel] = endpoints.size();
                endpoints.push_back({prefix + "output:" + std::to_string(apiChannel),
                    RoutingEndpointType::VirtualOutput, layout.sampleRate});
            }
        }
        clientIndices.push_back(std::move(indices));
        activeClients.push_back(std::move(client));
    }

    const auto actualEndpointCount = endpoints.size();
    auto runtime = std::unique_ptr<AudioRoutingRuntime>(new AudioRoutingRuntime(layout.bufferFrames,
        layout.sampleRate,
        actualEndpointCount, layout.physicalInputChannels, layout.physicalOutputChannels));
    std::string graphError;
    runtime->graph_ = RoutingGraph::Create(endpoints, layout.routes, layout.sampleRate, graphError);
    if (!runtime->graph_) {
        error = std::move(graphError);
        return {};
    }
    std::set<std::uint32_t> routedInputs;
    std::set<std::uint32_t> routedOutputs;
    const auto physicalPrefix = "physical:" + layout.physicalDriverId + ":";
    for (const auto& route : layout.routes) {
        std::uint32_t channel = 0;
        const bool input = physicalChannel(route.sourceEndpointId, physicalPrefix + "input:", channel);
        if (input)
            routedInputs.insert(channel);
        const bool output = physicalChannel(route.destinationEndpointId, physicalPrefix + "output:", channel);
        if (output)
            routedOutputs.insert(channel);
    }
    runtime->routedPhysicalInputChannels_.assign(routedInputs.begin(), routedInputs.end());
    runtime->routedPhysicalOutputChannels_.assign(routedOutputs.begin(), routedOutputs.end());
    runtime->clients_.reserve(activeClients.size());
    for (std::size_t index = 0; index < activeClients.size(); ++index) {
        ClientBuffers buffers;
        buffers.client = std::move(activeClients[index]);
        buffers.inputEndpointIndices = clientIndices[index].inputs;
        buffers.outputEndpointIndices = clientIndices[index].outputs;
        const auto samples = static_cast<std::size_t>(AudioClientMapping::kChannelCount) * layout.bufferFrames;
        buffers.clientOutput.resize(samples);
        buffers.clientInput.resize(samples);
        runtime->clients_.push_back(std::move(buffers));
    }

    for (std::size_t endpointIndex = 0; endpointIndex < endpoints.size(); ++endpointIndex) {
        const auto& endpoint = endpoints[endpointIndex];
        const bool usedByRoute = std::any_of(layout.routes.begin(), layout.routes.end(),
            [&endpoint](const AudioRoute& route) {
                return route.sourceEndpointId == endpoint.id ||
                    route.destinationEndpointId == endpoint.id;
            });
        if (!usedByRoute) continue;

        MeterBinding binding;
        binding.endpointId = endpoint.id;
        binding.endpointIndex = endpointIndex;
        binding.clientOutput = endpoint.type == RoutingEndpointType::VirtualOutput;
        if (endpoint.type == RoutingEndpointType::VirtualInput || binding.clientOutput) {
            for (auto& client : runtime->clients_) {
                const auto prefix = virtualPrefix(client.client.info.driverId,
                    client.client.info.processId);
                if (endpoint.id.rfind(prefix, 0) == 0) {
                    binding.clientMapping = client.client.mapping.get();
                    break;
                }
            }
        }
        runtime->meterBindings_.push_back(std::move(binding));
    }
    return runtime;
}

std::vector<AudioMeterSnapshot> AudioRoutingRuntime::ReadMeters() {
    std::vector<AudioMeterSnapshot> result;
    result.reserve(meterBindings_.size());
    for (const auto& binding : meterBindings_) {
        AudioMeterSnapshot meter;
        meter.endpointId = binding.endpointId;
        meter.peakLinear = peakLinear_[binding.endpointIndex].load(std::memory_order_acquire);
        if (binding.clientMapping) {
            if (binding.clientOutput) {
                meter.underruns = binding.clientMapping->ClientOutputUnderruns();
                meter.overruns = binding.clientMapping->ClientOutputOverruns();
            } else {
                meter.underruns = binding.clientMapping->ClientInputUnderruns();
                meter.overruns = binding.clientMapping->ClientInputOverruns();
            }
        }
        result.push_back(std::move(meter));
    }
    return result;
}

bool AudioRoutingRuntime::SetCorrelationPair(const std::string& leftEndpointId,
                                             const std::string& rightEndpointId) noexcept {
    std::size_t left = static_cast<std::size_t>(-1);
    std::size_t right = static_cast<std::size_t>(-1);
    for (const auto& binding : meterBindings_) {
        if (!binding.clientOutput) continue;
        if (binding.endpointId == leftEndpointId) left = binding.endpointIndex;
        if (binding.endpointId == rightEndpointId) right = binding.endpointIndex;
    }
    if (left == static_cast<std::size_t>(-1) || right == static_cast<std::size_t>(-1) || left == right)
        return false;

    correlationGeneration_.fetch_add(1, std::memory_order_acq_rel);
    correlationLeftIndex_.store(left, std::memory_order_relaxed);
    correlationRightIndex_.store(right, std::memory_order_relaxed);
    correlationGeneration_.fetch_add(1, std::memory_order_release);
    return true;
}

void AudioRoutingRuntime::StopCorrelation() noexcept {
    correlationGeneration_.fetch_add(1, std::memory_order_acq_rel);
    correlationLeftIndex_.store(static_cast<std::size_t>(-1), std::memory_order_relaxed);
    correlationRightIndex_.store(static_cast<std::size_t>(-1), std::memory_order_relaxed);
    correlationGeneration_.fetch_add(1, std::memory_order_release);
}

AudioCorrelationSnapshot AudioRoutingRuntime::ReadCorrelation() const noexcept {
    const auto generation = correlationGeneration_.load(std::memory_order_acquire);
    if ((generation & 1u) || generation == 0 ||
        correlationLeftIndex_.load(std::memory_order_relaxed) == static_cast<std::size_t>(-1) ||
        correlationRightIndex_.load(std::memory_order_relaxed) == static_cast<std::size_t>(-1) ||
        correlationPublishedGeneration_.load(std::memory_order_acquire) != generation)
        return {};
    return {true, correlationHasSignal_.load(std::memory_order_relaxed),
        correlationValue_.load(std::memory_order_relaxed)};
}

void AudioRoutingRuntime::Process(const float* const* physicalInputs, float* const* physicalOutputs,
                                  std::uint32_t frames) noexcept {
    if (frames != bufferFrames_ || !graph_) {
        if (physicalOutputs) {
            for (long channel = 0; channel < physicalOutputChannels_; ++channel)
                if (physicalOutputs[channel]) std::fill_n(physicalOutputs[channel], frames, 0.0f);
        }
        return;
    }

    std::fill(sourceBlocks_.begin(), sourceBlocks_.end(), nullptr);
    std::fill(destinationBlocks_.begin(), destinationBlocks_.end(), nullptr);
    for (long channel = 0; channel < physicalInputChannels_; ++channel)
        sourceBlocks_[static_cast<std::size_t>(channel)] = physicalInputs ? physicalInputs[channel] : nullptr;
    for (long channel = 0; channel < physicalOutputChannels_; ++channel)
        destinationBlocks_[physicalOutputBase_ + static_cast<std::size_t>(channel)] =
            physicalOutputs ? physicalOutputs[channel] : nullptr;

    for (auto& client : clients_) {
        auto& mapping = *client.client.mapping;
        const bool attached = mapping.EngineAttached();
        if (attached) mapping.ReadClientOutput(client.clientOutput.data(), frames);
        else std::fill(client.clientOutput.begin(), client.clientOutput.end(), 0.0f);
        for (std::uint32_t channel = 0; channel < AudioClientMapping::kChannelCount; ++channel) {
            const auto sourceIndex = client.outputEndpointIndices[channel];
            if (sourceIndex != static_cast<std::size_t>(-1)) {
                auto* plane = sourceSamples_.data() + sourceIndex * bufferFrames_;
                for (std::uint32_t frame = 0; frame < frames; ++frame)
                    plane[frame] = client.clientOutput[static_cast<std::size_t>(frame) *
                        AudioClientMapping::kChannelCount + channel];
                sourceBlocks_[sourceIndex] = plane;
            }

            const auto destinationIndex = client.inputEndpointIndices[channel];
            if (destinationIndex == static_cast<std::size_t>(-1)) continue;
            auto* destination = destinationSamples_.data() + destinationIndex * bufferFrames_;
            destinationBlocks_[destinationIndex] = destination;
        }
    }

    graph_->Process(sourceBlocks_.data(), destinationBlocks_.data(), frames);

    const auto correlationGeneration = correlationGeneration_.load(std::memory_order_acquire);
    if ((correlationGeneration & 1u) != 0) {
        correlationSamples_ = 0;
        correlationSumLeft_ = correlationSumRight_ = 0.0;
        correlationSumLeftSquared_ = correlationSumRightSquared_ = correlationSumProduct_ = 0.0;
    } else if (correlationGeneration != callbackCorrelationGeneration_) {
        callbackCorrelationGeneration_ = correlationGeneration;
        correlationSamples_ = 0;
        correlationSumLeft_ = correlationSumRight_ = 0.0;
        correlationSumLeftSquared_ = correlationSumRightSquared_ = correlationSumProduct_ = 0.0;
    }
    const auto leftIndex = correlationLeftIndex_.load(std::memory_order_relaxed);
    const auto rightIndex = correlationRightIndex_.load(std::memory_order_relaxed);
    const auto confirmedCorrelationGeneration = correlationGeneration_.load(std::memory_order_acquire);
    if (confirmedCorrelationGeneration != correlationGeneration) {
        correlationSamples_ = 0;
        correlationSumLeft_ = correlationSumRight_ = 0.0;
        correlationSumLeftSquared_ = correlationSumRightSquared_ = correlationSumProduct_ = 0.0;
    }
    if (confirmedCorrelationGeneration == correlationGeneration &&
        correlationGeneration != 0 && (correlationGeneration & 1u) == 0 &&
        leftIndex != static_cast<std::size_t>(-1) && rightIndex != static_cast<std::size_t>(-1) &&
        leftIndex < endpointCount_ && rightIndex < endpointCount_) {
        const float* leftSamples = sourceBlocks_[leftIndex]
            ? sourceBlocks_[leftIndex] : destinationBlocks_[leftIndex];
        const float* rightSamples = sourceBlocks_[rightIndex]
            ? sourceBlocks_[rightIndex] : destinationBlocks_[rightIndex];
        if (leftSamples && rightSamples) {
            for (std::uint32_t frame = 0; frame < frames; ++frame) {
                const double left = std::isfinite(leftSamples[frame]) ? leftSamples[frame] : 0.0;
                const double right = std::isfinite(rightSamples[frame]) ? rightSamples[frame] : 0.0;
                correlationSumLeft_ += left;
                correlationSumRight_ += right;
                correlationSumLeftSquared_ += left * left;
                correlationSumRightSquared_ += right * right;
                correlationSumProduct_ += left * right;
            }
            correlationSamples_ += frames;
            const auto windowSamples = (std::max)(std::uint64_t{1},
                static_cast<std::uint64_t>(sampleRate_) / 10);
            if (correlationSamples_ >= windowSamples) {
                const double count = static_cast<double>(correlationSamples_);
                const double covariance = correlationSumProduct_ -
                    correlationSumLeft_ * correlationSumRight_ / count;
                const double leftEnergy = correlationSumLeftSquared_ -
                    correlationSumLeft_ * correlationSumLeft_ / count;
                const double rightEnergy = correlationSumRightSquared_ -
                    correlationSumRight_ * correlationSumRight_ / count;
                constexpr double minimumRms = 0.00003162277660168379; // -90 dBFS
                const bool hasSignal = leftEnergy > 0.0 && rightEnergy > 0.0 &&
                    std::sqrt(leftEnergy / count) >= minimumRms &&
                    std::sqrt(rightEnergy / count) >= minimumRms;
                const double denominator = hasSignal ? std::sqrt(leftEnergy * rightEnergy) : 1.0;
                const double coefficient = hasSignal ? covariance / denominator : 0.0;
                correlationValue_.store(static_cast<float>(std::clamp(coefficient, -1.0, 1.0)),
                    std::memory_order_relaxed);
                correlationHasSignal_.store(hasSignal, std::memory_order_relaxed);
                correlationPublishedGeneration_.store(correlationGeneration, std::memory_order_release);
                correlationSamples_ = 0;
                correlationSumLeft_ = correlationSumRight_ = 0.0;
                correlationSumLeftSquared_ = correlationSumRightSquared_ = correlationSumProduct_ = 0.0;
            }
        } else {
            correlationSamples_ = 0;
            correlationSumLeft_ = correlationSumRight_ = 0.0;
            correlationSumLeftSquared_ = correlationSumRightSquared_ = correlationSumProduct_ = 0.0;
            correlationHasSignal_.store(false, std::memory_order_relaxed);
            correlationPublishedGeneration_.store(correlationGeneration, std::memory_order_release);
        }
    }

    for (const auto& binding : meterBindings_) {
        const auto index = binding.endpointIndex;
        const float* samples = sourceBlocks_[index]
            ? sourceBlocks_[index] : destinationBlocks_[index];
        if (!samples) continue;
        float peak = 0.0f;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const float sample = samples[frame];
            if (std::isfinite(sample)) peak = (std::max)(peak, std::abs(sample));
        }
        peak = (std::min)(peak, 1.0f);
        peakLinear_[index].store(peak, std::memory_order_release);
    }

    for (auto& client : clients_) {
        auto& mapping = *client.client.mapping;
        if (!mapping.EngineAttached()) continue;
        std::fill(client.clientInput.begin(), client.clientInput.end(), 0.0f);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            for (std::uint32_t channel = 0; channel < AudioClientMapping::kChannelCount; ++channel) {
                const auto sourceIndex = client.inputEndpointIndices[channel];
                if (sourceIndex == static_cast<std::size_t>(-1)) continue;
                client.clientInput[static_cast<std::size_t>(frame) *
                    AudioClientMapping::kChannelCount + channel] =
                    destinationSamples_[sourceIndex * bufferFrames_ + frame];
            }
        }
        mapping.WriteEngineInput(client.clientInput.data(), frames);
        mapping.SignalClient();
    }
}

void AudioRoutingRuntime::PhysicalCallback(const float* const* inputs, float* const* outputs,
    std::uint32_t frames, void* context) noexcept {
    if (context) static_cast<AudioRoutingRuntime*>(context)->Process(inputs, outputs, frames);
}
