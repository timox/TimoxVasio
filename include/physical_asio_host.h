#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <cstdint>
#include <thread>
#include <vector>

struct PhysicalAsioDriverInfo {
    std::wstring id; // Stable COM CLSID.
    std::string name;
};

struct PhysicalAsioCapabilities {
    long inputChannels = 0;
    long outputChannels = 0;
    std::vector<std::wstring> inputChannelNames;
    std::vector<std::wstring> outputChannelNames;
    long minBufferFrames = 0;
    long maxBufferFrames = 0;
    long preferredBufferFrames = 0;
    long bufferGranularity = 0;
    double currentSampleRate = 0.0;
    std::vector<double> supportedSampleRates;
};

struct PhysicalAsioTimingCounters {
    std::uint64_t callbacks = 0;
    std::uint64_t delayedStarts = 0;
    std::uint64_t processingOverruns = 0;
    std::uint64_t worstProcessingNs = 0;
};

struct StageTimingCounters {
    std::uint64_t samples = 0;
    std::uint64_t sumTicks = 0;
    std::uint64_t worstTicks = 0;
};

class StageTimingAccumulator final {
public:
    void Add(std::uint64_t ticks) noexcept {
        const auto index = activeBank_.load(std::memory_order_seq_cst);
        auto& bank = banks_[index];
        bank.inFlight.fetch_add(1, std::memory_order_seq_cst);
        if (activeBank_.load(std::memory_order_seq_cst) != index) {
            bank.inFlight.fetch_sub(1, std::memory_order_seq_cst);
            return;
        }
        bank.sumTicks.fetch_add(ticks, std::memory_order_relaxed);
        auto worst = bank.worstTicks.load(std::memory_order_relaxed);
        while (ticks > worst && !bank.worstTicks.compare_exchange_weak(worst, ticks,
            std::memory_order_relaxed, std::memory_order_relaxed)) {}
        bank.samples.fetch_add(1, std::memory_order_relaxed);
        bank.inFlight.fetch_sub(1, std::memory_order_seq_cst);
    }
    StageTimingCounters Consume() noexcept {
        const auto index = activeBank_.fetch_xor(1u, std::memory_order_seq_cst);
        auto& bank = banks_[index];
        while (bank.inFlight.load(std::memory_order_seq_cst) != 0)
            std::this_thread::yield();
        return {bank.samples.exchange(0, std::memory_order_relaxed),
            bank.sumTicks.exchange(0, std::memory_order_relaxed),
            bank.worstTicks.exchange(0, std::memory_order_relaxed)};
    }
private:
    struct Bank {
        std::atomic<std::uint32_t> inFlight{0};
        std::atomic<std::uint64_t> samples{0};
        std::atomic<std::uint64_t> sumTicks{0};
        std::atomic<std::uint64_t> worstTicks{0};
    };
    std::atomic<std::uint32_t> activeBank_{0};
    Bank banks_[2];
};

using PhysicalAsioProcessCallback = void (*)(const float* const* inputs,
    float* const* outputs, std::uint32_t frames, void* context) noexcept;

// Enumerates the registered ASIO drivers without initializing or opening them.
class PhysicalAsioHost final {
public:
    PhysicalAsioHost();
    ~PhysicalAsioHost();
    PhysicalAsioHost(const PhysicalAsioHost&) = delete;
    PhysicalAsioHost& operator=(const PhysicalAsioHost&) = delete;

    std::vector<PhysicalAsioDriverInfo> enumerate() const;
    bool open(const std::wstring& driverId, void* systemHandle,
              PhysicalAsioCapabilities& capabilities, std::string& error);
    bool setSampleRate(double sampleRate, std::string& error);
    bool refreshCapabilities(PhysicalAsioCapabilities& capabilities, std::string& error);
    bool consumeSampleRateChange() noexcept;
    PhysicalAsioTimingCounters consumeTimingCounters() noexcept;
    void setStageMeasurementsEnabled(bool enabled) noexcept;
    StageTimingCounters consumeOutputCopyStageCounters() noexcept;
    bool start(std::uint32_t bufferFrames, const std::vector<std::uint32_t>& inputChannels,
               const std::vector<std::uint32_t>& outputChannels,
               PhysicalAsioProcessCallback callback, void* context, std::string& error);
    void stop() noexcept;
    void close() noexcept;

private:
    struct Session;
    std::unique_ptr<Session> session_;
};
