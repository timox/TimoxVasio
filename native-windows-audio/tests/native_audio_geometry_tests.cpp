#include "native_audio_geometry.h"
#include <cstdio>
#include <cstdint>
int main() {
    unsigned failures = 0;
    const auto check = [&failures](bool ok) { if (!ok) ++failures; };
    check(timox_pcm32_fields_valid(2,48000,384000,8,32));
    check(!timox_pcm32_fields_valid(2,48000,0,8,32));
    check(!timox_pcm32_fields_valid(2,48000,384001,8,32));
    check(!timox_pcm32_fields_valid(1,48000,384000,8,32));
    check(!timox_pcm32_fields_valid(2,44100,384000,8,32));
    check(!timox_pcm32_fields_valid(2,48000,384000,4,32));
    uint32_t bytes=0, period=0;
    check(timox_buffer_geometry(3840,2,384000,&bytes,&period) && bytes==3840 && period==5);
    check(timox_buffer_geometry(3841,2,384000,&bytes,&period) && bytes==3840 && period==5);
    check(!timox_buffer_geometry(1026,3,384000,&bytes,&period));
    check(!timox_buffer_geometry(8,1,384000,&bytes,&period));
    check(!timox_buffer_geometry(3840,0,384000,&bytes,&period));
    check(!timox_buffer_geometry(3840,2,0,&bytes,&period));
    check(!timox_buffer_geometry(UINT32_MAX,2,384000,&bytes,&period));
    check(!timox_buffer_geometry(384008,2,384000,&bytes,&period));
    check(timox_buffer_geometry(384000,1000,384000,&bytes,&period) && period==1);
    check(!timox_buffer_geometry(384000,1001,384000,&bytes,&period));
    check(timox_format_extent_valid(104,64,18,22,true));
    check(!timox_format_extent_valid(82,64,18,22,true));
    check(!timox_format_extent_valid(104,64,18,21,true));
    check(!timox_format_extent_valid(104,64,18,65535,true));
    check(timox_format_extent_valid(82,64,18,0,false));
    check(!timox_format_extent_valid(81,64,18,0,false));
    if (failures) std::fprintf(stderr,"%u geometry/format failures\n",failures);
    return failures ? 1 : 0;
}
