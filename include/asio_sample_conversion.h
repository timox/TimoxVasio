#pragma once

#include "asiosys.h"
#include "asio.h"

#include <cstdint>

namespace AsioSampleConversion {
bool IsSupported(ASIOSampleType type) noexcept;
std::uint32_t BytesPerSample(ASIOSampleType type) noexcept;
float Decode(const void* bytes, ASIOSampleType type) noexcept;
void Encode(void* bytes, ASIOSampleType type, float sample) noexcept;
}
