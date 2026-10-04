#include "asio_sample_conversion.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {
std::uint32_t sampleBytes(ASIOSampleType type) noexcept {
    switch (type) {
    case ASIOSTInt16MSB: case ASIOSTInt16LSB: return 2;
    case ASIOSTInt24MSB: case ASIOSTInt24LSB: return 3;
    case ASIOSTFloat64MSB: case ASIOSTFloat64LSB: return 8;
    default: return 4;
    }
}

std::uint32_t intBits(ASIOSampleType type) noexcept {
    switch (type) {
    case ASIOSTInt32MSB16: case ASIOSTInt32LSB16: return 16;
    case ASIOSTInt32MSB18: case ASIOSTInt32LSB18: return 18;
    case ASIOSTInt32MSB20: case ASIOSTInt32LSB20: return 20;
    case ASIOSTInt32MSB24: case ASIOSTInt32LSB24: return 24;
    case ASIOSTInt16MSB: case ASIOSTInt16LSB: return 16;
    case ASIOSTInt24MSB: case ASIOSTInt24LSB: return 24;
    case ASIOSTInt32MSB: case ASIOSTInt32LSB: return 32;
    default: return 0;
    }
}

bool isBigEndian(ASIOSampleType type) noexcept {
    return type == ASIOSTInt16MSB || type == ASIOSTInt24MSB || type == ASIOSTInt32MSB ||
        type == ASIOSTFloat32MSB || type == ASIOSTFloat64MSB || type == ASIOSTInt32MSB16 ||
        type == ASIOSTInt32MSB18 || type == ASIOSTInt32MSB20 || type == ASIOSTInt32MSB24;
}

std::uint64_t readUnsigned(const std::uint8_t* bytes, std::uint32_t size, bool bigEndian) noexcept {
    std::uint64_t value = 0;
    for (std::uint32_t i = 0; i < size; ++i) {
        const auto shift = bigEndian ? (size - 1 - i) * 8 : i * 8;
        value |= static_cast<std::uint64_t>(bytes[i]) << shift;
    }
    return value;
}

void writeUnsigned(std::uint8_t* bytes, std::uint32_t size, bool bigEndian,
                   std::uint64_t value) noexcept {
    for (std::uint32_t i = 0; i < size; ++i) {
        const auto shift = bigEndian ? (size - 1 - i) * 8 : i * 8;
        bytes[i] = static_cast<std::uint8_t>(value >> shift);
    }
}
}

std::uint32_t AsioSampleConversion::BytesPerSample(ASIOSampleType type) noexcept {
    return sampleBytes(type);
}

bool AsioSampleConversion::IsSupported(ASIOSampleType type) noexcept {
    switch (type) {
    case ASIOSTInt16MSB: case ASIOSTInt24MSB: case ASIOSTInt32MSB:
    case ASIOSTFloat32MSB: case ASIOSTFloat64MSB:
    case ASIOSTInt32MSB16: case ASIOSTInt32MSB18: case ASIOSTInt32MSB20: case ASIOSTInt32MSB24:
    case ASIOSTInt16LSB: case ASIOSTInt24LSB: case ASIOSTInt32LSB:
    case ASIOSTFloat32LSB: case ASIOSTFloat64LSB:
    case ASIOSTInt32LSB16: case ASIOSTInt32LSB18: case ASIOSTInt32LSB20: case ASIOSTInt32LSB24:
        return true;
    default:
        return false;
    }
}

float AsioSampleConversion::Decode(const void* data, ASIOSampleType type) noexcept {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    const bool big = isBigEndian(type);
    if (type == ASIOSTFloat32LSB || type == ASIOSTFloat32MSB) {
        const auto bits = static_cast<std::uint32_t>(readUnsigned(bytes, 4, big));
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return std::isfinite(value) ? value : 0.0f;
    }
    if (type == ASIOSTFloat64LSB || type == ASIOSTFloat64MSB) {
        const auto bits = readUnsigned(bytes, 8, big);
        double value;
        std::memcpy(&value, &bits, sizeof(value));
        return std::isfinite(value) ? static_cast<float>(value) : 0.0f;
    }
    const auto size = sampleBytes(type);
    const auto bits = intBits(type);
    std::int64_t value = static_cast<std::int64_t>(readUnsigned(bytes, size, big));
    if (size == 3 && (value & 0x800000)) value |= ~std::int64_t{0xffffff};
    else if (size == 2 && (value & 0x8000)) value |= ~std::int64_t{0xffff};
    else if (size == 4) value = static_cast<std::int32_t>(value);
    if (size == 4 && bits < 32) value /= (std::int64_t{1} << (32 - bits));
    return static_cast<float>(static_cast<double>(value) / std::ldexp(1.0, static_cast<int>(bits - 1)));
}

void AsioSampleConversion::Encode(void* data, ASIOSampleType type, float sample) noexcept {
    auto* bytes = static_cast<std::uint8_t*>(data);
    if (!std::isfinite(sample)) sample = 0.0f;
    sample = std::clamp(sample, -1.0f, 1.0f);
    const bool big = isBigEndian(type);
    if (type == ASIOSTFloat32LSB || type == ASIOSTFloat32MSB) {
        std::uint32_t bits;
        std::memcpy(&bits, &sample, sizeof(bits));
        writeUnsigned(bytes, 4, big, bits);
        return;
    }
    if (type == ASIOSTFloat64LSB || type == ASIOSTFloat64MSB) {
        const double value = sample;
        std::uint64_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        writeUnsigned(bytes, 8, big, bits);
        return;
    }
    const auto size = sampleBytes(type);
    const auto bits = intBits(type);
    const std::int64_t minValue = -(std::int64_t{1} << (bits - 1));
    const std::int64_t maxValue = (std::int64_t{1} << (bits - 1)) - 1;
    const auto value = sample <= -1.0f ? minValue :
        static_cast<std::int64_t>(std::llround(static_cast<double>(sample) * maxValue));
    std::uint64_t encoded = static_cast<std::uint64_t>(value);
    if (size == 4 && bits < 32)
        encoded = static_cast<std::uint32_t>(static_cast<std::int32_t>(value)) << (32 - bits);
    writeUnsigned(bytes, size, big, encoded);
}
