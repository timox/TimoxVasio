#pragma once
// CTL_CODE: private device type 0x8337, READ|WRITE, METHOD_BUFFERED.
#define TIMOX_CONTROL_DEVICE_TYPE 0x8337u
#define TIMOX_IOCTL_SESSION ((0x8337u<<16)|(3u<<14)|(0x800u<<2))
#define TIMOX_IOCTL_WRITE_CAPTURE ((0x8337u<<16)|(3u<<14)|(0x801u<<2))
#define TIMOX_IOCTL_READ_RENDER ((0x8337u<<16)|(3u<<14)|(0x802u<<2))
#define TIMOX_IOCTL_STATUS ((0x8337u<<16)|(3u<<14)|(0x803u<<2))
// SESSION: no input, 8-byte little-endian generation output.
// WRITE_CAPTURE: N0 packet input, no output.
// READ_RENDER: input generation u64, maxFrames u32, reserved u32=0 (16 bytes).
// Output N0 packet, or STATUS_NO_MORE_ENTRIES with zero bytes if empty.
// STATUS: generation u64 input; output renderDropped/captureDropped/captureSilence,
// three little-endian u64 counters. No pointer or native structure is serialized.
