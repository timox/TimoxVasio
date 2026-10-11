#include "loopback_pattern.h"
#include <cstdint>
#include <cstring>
#include <initializer_list>

int main() {
    for (std::uint32_t index : {0u, 1u, 1023u, 1048575u, 1048576u}) {
        const auto value = loopbackSample(index);
        std::uint32_t decoded = 0;
        if (!decodeLoopbackSample(value, decoded)) return 1;
        if (decoded != (index & 0xfffffu)) return 2;
    }
    float corrupt = loopbackSample(12345);
    std::uint32_t bits = 0;
    std::memcpy(&bits, &corrupt, sizeof(bits));
    bits ^= 0x00100000u;
    std::memcpy(&corrupt, &bits, sizeof(bits));
    std::uint32_t decoded = 0;
    if (decodeLoopbackSample(corrupt, decoded)) return 3;
    if (decodeLoopbackSample(0.0f, decoded)) return 4;
}
