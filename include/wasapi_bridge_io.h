#pragma once

#include "spsc_audio_ring.h"
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>

enum class WasapiBridgeDirection { Capture, Render };
struct WasapiBridgeFormat {
    WasapiBridgeDirection direction = WasapiBridgeDirection::Capture;
    std::size_t channels = 0;
    std::uint32_t graphSampleRate = 0;
    std::size_t maxQuantum = 0;
    std::uint64_t generationId = 0;
    std::size_t capacityFrames = 0;
};

// One immutable generation at GRAPH sample rate. Capture/loopback backends
// convert device samples to this rate before WriteCaptureInterleaved; render
// backends perform conversion after ReadRenderInterleaved. No SRC runs here.
//
// SPSC ownership for the entire published lifetime:
// Capture: backend worker writes interleaved; graph reads planar.
// Render: graph writes planar; backend worker reads interleaved.
// No alternate producer/consumer, Reset, live resize or clear is permitted.
// Construct/pre-touch on the control thread, then publish a borrowed pointer.
// Retire and destroy only after BOTH graph and backend have quiesced. Do not
// release owning smart pointers or destroy this object inside an audio callback.
//
// Storage is bounded to 65536 frames * 256 channels * sizeof(float) = 64 MiB
// per generation. Several simultaneous/retired generations consume that budget
// separately. Construction can throw; every transfer/stats method is noexcept
// and performs no allocation, lock, wait, OS call or logging.
class WasapiBridgeIo final {
public:
    explicit WasapiBridgeIo(const WasapiBridgeFormat& format)
        : format_(Validate(format)), ring_(format_.channels, format_.capacityFrames) {}
    WasapiBridgeIo(const WasapiBridgeIo&) = delete;
    WasapiBridgeIo& operator=(const WasapiBridgeIo&) = delete;
    WasapiBridgeIo(WasapiBridgeIo&&) = delete;
    WasapiBridgeIo& operator=(WasapiBridgeIo&&) = delete;
    const WasapiBridgeFormat& Format() const noexcept { return format_; }

    // Each plane must have at least frames writable/readable samples and the
    // pointer array must contain planeCount entries. Graph frames <= maxQuantum.
    // Return actual ring frames; valid reads silence the entire unread tail.
    // Invalid direction/dimensions/null pointers return zero without touching
    // buffers, ring or statistics. Zero-frame requests are inert (null allowed).
    std::size_t ReadCaptureBlock(float* const* planes, std::size_t frames, std::size_t planeCount) noexcept {
        if (format_.direction != WasapiBridgeDirection::Capture || !frames ||
            !ValidPlanes(planes, frames, planeCount)) return 0;
        const auto read = ring_.ReadPlanar(planes, frames);
        for (std::size_t channel = 0; channel < format_.channels; ++channel)
            std::fill_n(planes[channel] + read, frames - read, 0.0f);
        silencedFrames_.fetch_add(frames - read, std::memory_order_relaxed);
        return read;
    }
    std::size_t WriteRenderBlock(const float* const* planes, std::size_t frames, std::size_t planeCount) noexcept {
        if (format_.direction != WasapiBridgeDirection::Render || !frames ||
            !ValidPlanes(planes, frames, planeCount)) return 0;
        const auto written = ring_.WritePlanar(planes, frames);
        rejectedFrames_.fetch_add(frames - written, std::memory_order_relaxed);
        return written;
    }

    // Backend worker, with frames <= capacityFrames and sampleCount >=
    // frames * channels. Caller retains an unwritten tail and decides retry/drop.
    // Render reads always explicitly zero-pad a valid short destination block.
    std::size_t WriteCaptureInterleaved(const float* samples, std::size_t frames, std::size_t sampleCount) noexcept {
        if (format_.direction != WasapiBridgeDirection::Capture || !frames ||
            !ValidInterleaved(samples, frames, sampleCount)) return 0;
        const auto written = ring_.Write(samples, frames);
        rejectedFrames_.fetch_add(frames - written, std::memory_order_relaxed);
        return written;
    }
    std::size_t ReadRenderInterleaved(float* samples, std::size_t frames, std::size_t sampleCount) noexcept {
        if (format_.direction != WasapiBridgeDirection::Render || !frames ||
            !ValidInterleaved(samples, frames, sampleCount)) return 0;
        const auto read = ring_.Read(samples, frames);
        std::fill_n(samples + read * format_.channels, (frames - read) * format_.channels, 0.0f);
        silencedFrames_.fetch_add(frames - read, std::memory_order_relaxed);
        return read;
    }

    // Cumulative FRAMES, not ring incidents or per-channel samples. Rejected
    // frames are not necessarily lost if the writer retries them. No reset;
    // each generation starts at zero. Individually sampled, not a transaction.
    std::uint64_t RejectedFrames() const noexcept { return rejectedFrames_.load(std::memory_order_relaxed); }
    std::uint64_t SilencedFrames() const noexcept { return silencedFrames_.load(std::memory_order_relaxed); }
    // Snapshot bounded to [0, capacityFrames], not a reservation. Exact while
    // quiescent; concurrent producer/consumer movement may change it immediately.
    // Suitable for drift observations, never for deciding transfer success.
    std::size_t BufferedFrames() const noexcept { return ring_.Available(); }
private:
    static WasapiBridgeFormat Validate(const WasapiBridgeFormat& format) {
        if ((format.direction != WasapiBridgeDirection::Capture && format.direction != WasapiBridgeDirection::Render) ||
            !format.channels || format.channels > 256 ||
            format.graphSampleRate < 8000 || format.graphSampleRate > 384000 ||
            format.maxQuantum < 16 || format.maxQuantum > 8192 ||
            format.capacityFrames < format.maxQuantum || format.capacityFrames > 65536 || !format.generationId)
            throw std::invalid_argument("Invalid WASAPI bridge generation dimensions");
        return format;
    }
    template<class T>
    bool ValidPlanes(T* const* planes, std::size_t frames, std::size_t planeCount) const noexcept {
        if (!planes || frames > format_.maxQuantum || planeCount != format_.channels) return false;
        for (std::size_t channel = 0; channel < planeCount; ++channel)
            if (!planes[channel]) return false;
        return true;
    }
    bool ValidInterleaved(const float* samples, std::size_t frames, std::size_t sampleCount) const noexcept {
        // Check the bound BEFORE multiplication, including adversarial SIZE_MAX.
        return samples && frames <= format_.capacityFrames && sampleCount >= frames * format_.channels;
    }
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "Bridge frame counters must be lock-free");
    const WasapiBridgeFormat format_;
    SpscAudioRing ring_;
    std::atomic<std::uint64_t> rejectedFrames_{0};
    std::atomic<std::uint64_t> silencedFrames_{0};
};
