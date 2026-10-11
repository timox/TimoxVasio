#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

struct ProbeCallbackEvent {
    std::uint64_t qpc = 0;
    std::uint32_t kind = 0;
    std::int32_t value = 0;
    std::uint64_t samplePosition = 0;
    bool positionValid = false;
    std::uint64_t counter = 0;
    std::uint32_t corrupted = 0;
    std::uint32_t missing = 0;
    std::uint32_t duplicated = 0;
    std::uint32_t preSync = 0;
};

// Bounded, lock-free MPMC queue. A full queue drops the event; producers never wait.
template <std::size_t Capacity>
class ProbeCallbackRing {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
        "Capacity must be a power of two");
    static_assert(std::atomic<std::size_t>::is_always_lock_free,
        "Callback indices must use lock-free atomics");

    struct Cell {
        std::atomic<std::size_t> sequence{0};
        ProbeCallbackEvent event{};
    };

public:
    ProbeCallbackRing() noexcept {
        for (std::size_t index = 0; index < Capacity; ++index)
            cells_[index].sequence.store(index, std::memory_order_relaxed);
    }

    bool tryPush(const ProbeCallbackEvent& event) noexcept {
        auto position = enqueue_.load(std::memory_order_relaxed);
        for (int attempt = 0; attempt < 4; ++attempt) {
            Cell& cell = cells_[position & (Capacity - 1)];
            const auto sequence = cell.sequence.load(std::memory_order_acquire);
            const auto difference = static_cast<std::intptr_t>(sequence) -
                static_cast<std::intptr_t>(position);
            if (difference < 0) return false;
            if (difference == 0 && enqueue_.compare_exchange_weak(position, position + 1,
                std::memory_order_relaxed, std::memory_order_relaxed)) {
                cell.event = event;
                cell.sequence.store(position + 1, std::memory_order_release);
                return true;
            }
            position = enqueue_.load(std::memory_order_relaxed);
        }
        return false;
    }

    bool tryPop(ProbeCallbackEvent& event) noexcept {
        auto position = dequeue_.load(std::memory_order_relaxed);
        Cell& cell = cells_[position & (Capacity - 1)];
        const auto sequence = cell.sequence.load(std::memory_order_acquire);
        const auto difference = static_cast<std::intptr_t>(sequence) -
            static_cast<std::intptr_t>(position + 1);
        if (difference != 0) return false;
        if (!dequeue_.compare_exchange_strong(position, position + 1,
            std::memory_order_relaxed, std::memory_order_relaxed)) return false;
        event = cell.event;
        cell.sequence.store(position + Capacity, std::memory_order_release);
        return true;
    }

private:
    std::array<Cell, Capacity> cells_{};
    std::atomic<std::size_t> enqueue_{0};
    std::atomic<std::size_t> dequeue_{0};
};
