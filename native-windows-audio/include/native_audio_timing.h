#pragma once
#include "native_audio_geometry.h"
// Exact PCM32 stereo 48 kHz movement. Remainder is a fractional-frame numerator
// modulo 10^7, not milliseconds. Quotient first avoids elapsed*48000 overflow.
static inline timox_u64 timox_elapsed_pcm_bytes(timox_u64 elapsed100ns,timox_u64& remainder) noexcept {
    const timox_u64 seconds=elapsed100ns/10000000;
    const timox_u64 fraction=(elapsed100ns%10000000)*48000+remainder;
    remainder=fraction%10000000;
    return (seconds*48000+fraction/10000000)*8;
}
struct TimoxRingSpan {timox_u32 offset,bytes;timox_u64 lostFrames;};
static inline TimoxRingSpan timox_ring_span(timox_u64 position,timox_u64 displacement,timox_u32 ring) noexcept {
    if(!ring || ring%8 || position%8 || displacement%8)return {};
    const auto kept=displacement<ring ? static_cast<timox_u32>(displacement):ring;
    const auto skipped=displacement-kept;
    return {static_cast<timox_u32>((position%ring+skipped%ring)%ring),kept,skipped/8};
}
static inline bool timox_write_position_valid(timox_u32 position,timox_u32 ring) noexcept {
    return ring!=0 && ring%8==0 && position<=ring && position%8==0;
}
static inline timox_u64 timox_eos_bytes(timox_u64 displacement,timox_u32 position,
    timox_u32 end,timox_u32 ring,bool finished) noexcept {
    if(finished || !timox_write_position_valid(position,ring) ||
       !timox_write_position_valid(end,ring))return 0;
    const timox_u64 remaining=position<=end ? end-position:ring-position+end;
    return displacement<remaining ? displacement:remaining;
}
static inline timox_u64 timox_bytes_to_hns(timox_u64 bytes) noexcept {
    return (bytes/384000)*10000000+(bytes%384000)*10000000/384000;
}
static inline timox_u64 timox_completed_packets(timox_u64 bytes,timox_u32 packetBytes) noexcept {
    return packetBytes ? bytes/packetBytes:0;
}
static inline timox_u64 timox_last_packet_start(timox_u64 completed,timox_u32 packetBytes) noexcept {
    return completed ? (completed-1)*packetBytes:0;
}
