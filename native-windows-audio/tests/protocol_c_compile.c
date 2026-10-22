#include "native_audio_protocol.h"
/* The shared vocabulary must remain usable by a C driver translation unit. */
int NativeAudioProtocolCHeaderUsable(void) {
    TimoxNativePacketMetadata metadata = {0};
    return metadata.frames == 0 && TIMOX_NATIVE_HEADER_BYTES == 64;
}
