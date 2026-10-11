#pragma once

#include <cstdint>
#include <cstring>

namespace LoopbackPattern {
constexpr std::uint32_t kMask = 0x000fffffu;
constexpr std::uint32_t kMultiplier = 0x5bd1e995u;
constexpr std::uint32_t kOffset = 0x1f123bb5u;

constexpr std::uint32_t inverseMultiplier() noexcept {
    std::uint32_t inverse = kMultiplier;
    for (int iteration = 0; iteration < 5; ++iteration)
        inverse *= 2u - kMultiplier * inverse;
    return inverse;
}

constexpr std::uint32_t checksum(std::uint32_t index) noexcept {
    return ((index * 0x9e3779b9u) ^ (index >> 7) ^ (index >> 13)) & 7u;
}
}

inline float loopbackSample(std::uint32_t index) noexcept {
    index &= LoopbackPattern::kMask;
    const auto scrambled = (index * LoopbackPattern::kMultiplier +
        LoopbackPattern::kOffset) & LoopbackPattern::kMask;
    const std::uint32_t bits = 0x3f800000u | (LoopbackPattern::checksum(index) << 20) |
        scrambled;
    float sample = 0;
    std::memcpy(&sample, &bits, sizeof(sample));
    return sample;
}

inline bool decodeLoopbackSample(float sample, std::uint32_t& index) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &sample, sizeof(bits));
    if ((bits & 0xff800000u) != 0x3f800000u) return false;
    const auto scrambled = bits & LoopbackPattern::kMask;
    index = ((scrambled - LoopbackPattern::kOffset) *
        LoopbackPattern::inverseMultiplier()) & LoopbackPattern::kMask;
    return ((bits >> 20) & 7u) == LoopbackPattern::checksum(index);
}
