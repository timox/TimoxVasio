#ifndef TIMOX_NATIVE_AUDIO_PROTOCOL_H
#define TIMOX_NATIVE_AUDIO_PROTOCOL_H

#include <stdint.h>

/* Packet wire format is little-endian, 64-byte header followed by interleaved
 * signed PCM32. These are offsets, not an ABI-dependent packed C structure. */
#define TIMOX_NATIVE_HEADER_BYTES 64u
#define TIMOX_NATIVE_MAX_FRAMES 4096u
#define TIMOX_NATIVE_SAMPLE_RATE 48000u
#define TIMOX_NATIVE_CHANNELS 2u
#define TIMOX_NATIVE_PCM_S32 1u
#define TIMOX_NATIVE_RENDER_PACKET 1u
#define TIMOX_NATIVE_CAPTURE_PACKET 2u
#define TIMOX_NATIVE_FROM_WINDOWS_RENDER 1u
#define TIMOX_NATIVE_TO_WINDOWS_CAPTURE 2u
#define TIMOX_NATIVE_DISCONTINUITY 1u
#define TIMOX_NATIVE_QPC_VALID 2u

/* Decoded metadata only; NEVER cast a packet buffer to this structure. */
typedef struct TimoxNativePacketMetadata {
    uint64_t generation;
    uint64_t sequence;
    uint64_t qpc;
    uint32_t frames;
    uint32_t payloadBytes;
    uint32_t flags;
    uint16_t direction;
} TimoxNativePacketMetadata;

#endif
