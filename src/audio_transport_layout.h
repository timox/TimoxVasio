#pragma once

#include "audio_transport.h"

#include <cstdint>

// Private v7 shared-memory layout. Keep the v6 prefix unchanged.
struct alignas(64) ClientStatusV2 {
    std::uint32_t magic;
    std::uint32_t schemaVersion;
    std::uint32_t size;
    volatile LONG ready;
    std::uint32_t dllVersion[4];
    wchar_t dllPath[1024];
    alignas(8) volatile LONG64 qpcFrequency;
    volatile LONG state;
    volatile LONG requestedSampleRate;
    volatile LONG requestedBufferFrames;
    alignas(8) volatile LONG64 exchangeGeneration;
    alignas(8) volatile LONG64 firstCallbackQpc;
    volatile LONG64 lastCallbackQpc;
    volatile LONG64 lastSuccessfulExchangeQpc;
    volatile LONG64 lastIntervalQpc;
    volatile LONG64 callbackCount;
    volatile LONG64 successfulExchangeCount;
    volatile LONG64 exchangedFrames;
    volatile LONG64 resetRequests;
};

struct alignas(64) AudioClientMapping::Shared {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t driverId;
    std::uint32_t processId;
    volatile LONG sampleRate;
    std::uint32_t channelCount;
    std::uint32_t capacityFrames;
    volatile LONG blockFrames;
    volatile LONG preferredBufferFrames;
    alignas(8) volatile LONG64 generation;
    alignas(8) volatile LONG64 activeChannelsGeneration;
    volatile LONG64 activeInputs[AudioClientMapping::kChannelMaskWords];
    volatile LONG64 activeOutputs[AudioClientMapping::kChannelMaskWords];
    volatile LONG64 clientWriteFrame;
    volatile LONG64 engineReadFrame;
    volatile LONG64 engineWriteFrame;
    volatile LONG64 clientReadFrame;
    volatile LONG64 inputUnderruns;
    volatile LONG64 outputOverruns;
    volatile LONG64 inputOverruns;
    volatile LONG64 outputUnderruns;
    volatile LONG64 engineAttached;
    volatile LONG engineProcessId;
    alignas(64) float clientToEngine[AudioClientMapping::kRingCapacityFrames * AudioClientMapping::kChannelCount];
    alignas(64) float engineToClient[AudioClientMapping::kRingCapacityFrames * AudioClientMapping::kChannelCount];
    alignas(64) ClientStatusV2 status;
};
