#pragma once

#include <windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

// Private data plane shared by a VASIO client and its routing engine.
class AudioClientMapping final {
public:
    static constexpr std::uint32_t kChannelCount = 256;
    static constexpr std::uint32_t kRingCapacityFrames = 16384;
    static constexpr std::size_t kChannelMaskWords = kChannelCount / 64;
    using ChannelMask = std::array<std::uint64_t, kChannelMaskWords>;

    static std::unique_ptr<AudioClientMapping> CreateClient(std::uint32_t driverId,
        std::uint32_t processId, std::uint32_t sampleRate, std::uint32_t blockFrames);
    static std::unique_ptr<AudioClientMapping> OpenEngine(std::uint32_t driverId,
        std::uint32_t processId, std::uint32_t sampleRate);
    ~AudioClientMapping();

    AudioClientMapping(const AudioClientMapping&) = delete;
    AudioClientMapping& operator=(const AudioClientMapping&) = delete;

    bool SetEngineAttached(bool attached) noexcept;
    bool EngineAttached() const noexcept;
    bool EngineProcessAlive() const noexcept;
    std::uint32_t SampleRate() const noexcept;
    std::uint32_t BlockFrames() const noexcept;
    std::uint32_t PreferredBufferFrames() const noexcept;
    bool SetSampleRate(std::uint32_t sampleRate) noexcept;
    bool SetBlockFrames(std::uint32_t blockFrames) noexcept;
    bool SetPreferredBufferFrames(std::uint32_t frames) noexcept;
    bool SetActiveChannels(const ChannelMask& inputs, const ChannelMask& outputs) noexcept;
    bool GetActiveChannels(ChannelMask& inputs, ChannelMask& outputs) const noexcept;
    bool WaitForClientSignal(DWORD timeoutMs) noexcept;
    std::uint32_t ClientOutputAvailable() const noexcept;
    std::uint32_t ClientInputAvailable() const noexcept;
    bool WriteClientOutput(const float* interleaved, std::uint32_t frames) noexcept;
    std::uint32_t ReadClientOutput(float* interleaved, std::uint32_t frames) noexcept;
    bool WriteEngineInput(const float* interleaved, std::uint32_t frames) noexcept;
    std::uint32_t ReadClientInput(float* interleaved, std::uint32_t frames) noexcept;
    bool SignalClient() noexcept;
    std::uint64_t ClientOutputOverruns() const noexcept;
    std::uint64_t ClientInputUnderruns() const noexcept;
    std::uint64_t ClientInputOverruns() const noexcept;
    std::uint64_t ClientOutputUnderruns() const noexcept;

private:
    struct Shared;
    AudioClientMapping(HANDLE mapping, HANDLE eventHandle, Shared* shared, bool engineOwner) noexcept;
    static std::unique_ptr<AudioClientMapping> Open(bool create, std::uint32_t driverId,
        std::uint32_t processId, std::uint32_t sampleRate, std::uint32_t blockFrames);
    std::uint32_t Available(volatile LONG64* write, volatile LONG64* read) const noexcept;
    bool Write(const float* src, std::uint32_t frames, volatile LONG64* write,
        volatile LONG64* read, float* ring, volatile LONG64* overruns) noexcept;
    std::uint32_t Read(float* dst, std::uint32_t frames, volatile LONG64* read,
        volatile LONG64* write, const float* ring, volatile LONG64* underruns) noexcept;

    HANDLE mapping_ = nullptr;
    HANDLE event_ = nullptr;
    Shared* shared_ = nullptr;
    bool engineOwner_ = false;
    bool ownsEngineAttachment_ = false;
};
