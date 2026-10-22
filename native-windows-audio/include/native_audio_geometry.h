#pragma once
// Fundamental types avoid pulling the user CRT into WDK kernel translation units.
using timox_u32 = unsigned int;
using timox_u64 = unsigned long long;
static_assert(sizeof(timox_u32) == 4 && sizeof(timox_u64) == 8, "integer widths" );

// N1 deliberately supports one PCM format and at most one second per ring.
static inline bool timox_pcm32_fields_valid(timox_u32 channels, timox_u32 rate,
    timox_u32 average, timox_u32 align, timox_u32 bits)
{
    return channels == 2 && rate == 48000 && average == 384000 &&
           align == 8 && bits == 32;
}

// Caller must first ensure the base WAVEFORMATEX itself is readable.
static inline bool timox_format_extent_valid(timox_u32 bytes, timox_u32 prefix,
    timox_u32 wave_bytes, timox_u32 extension, bool extensible)
{
    if (bytes < prefix || bytes-prefix < wave_bytes) return false;
    if (extension > bytes-prefix-wave_bytes) return false;
    return extensible ? extension >= 22 : extension == 0;
}

static inline bool timox_buffer_geometry(timox_u32 requested, timox_u32 notifications,
    timox_u32 average, timox_u32* aligned, timox_u32* period_ms)
{
    if (requested < 8 || requested > 384000 || average != 384000 ||
        notifications == 0) return false;
    const timox_u32 size = requested - requested % 8;
    if (size % notifications != 0 || (size / notifications) % 8 != 0) return false;
    const timox_u64 duration = (timox_u64(size) * 1000) / average;
    const timox_u64 period = duration / notifications;
    if (period == 0) return false;
    *aligned = size;
    *period_ms = timox_u32(period);
    return true;
}
