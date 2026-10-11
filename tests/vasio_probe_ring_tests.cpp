#include "callback_ring.h"

#include <atomic>
#include <cstdio>
#include <thread>

int main() {
    ProbeCallbackRing<4> ring;
    for (int index = 0; index < 4; ++index)
        if (!ring.tryPush({static_cast<std::uint64_t>(index), 1, index})) return 1;
    if (ring.tryPush({4, 1, 4})) return 2;
    ProbeCallbackEvent event{};
    for (int index = 0; index < 4; ++index)
        if (!ring.tryPop(event) || event.value != index) return 3;
    if (ring.tryPop(event)) return 4;

    ProbeCallbackRing<1024> concurrent;
    std::atomic<int> producersDone{0};
    std::atomic<int> consumed{0};
    std::thread a([&] {
        for (int index = 0; index < 10000; ++index)
            while (!concurrent.tryPush({static_cast<std::uint64_t>(index), 1, index}))
                std::this_thread::yield();
        producersDone.fetch_add(1);
    });
    std::thread b([&] {
        for (int index = 0; index < 10000; ++index)
            while (!concurrent.tryPush({static_cast<std::uint64_t>(index), 2, index}))
                std::this_thread::yield();
        producersDone.fetch_add(1);
    });
    int lastA = -1, lastB = -1;
    while (producersDone.load() != 2 || consumed.load() < 20000) {
        if (!concurrent.tryPop(event)) { std::this_thread::yield(); continue; }
        auto& last = event.kind == 1 ? lastA : lastB;
        if (event.value != last + 1) return 5;
        last = event.value;
        consumed.fetch_add(1);
    }
    a.join(); b.join();
    if (consumed != 20000 || lastA != 9999 || lastB != 9999) return 6;
    std::puts("PASS: bounded callback ring preserves each producer's order and reports full capacity.");
    return 0;
}
