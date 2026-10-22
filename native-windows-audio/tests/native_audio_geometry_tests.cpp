#include "native_audio_geometry.h"
#include "native_audio_timing.h"
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
    timox_u64 carry=0;
    check(timox_elapsed_pcm_bytes(120000000,carry)==4608000 && carry==0);
    check(timox_elapsed_pcm_bytes(5000000000ULL,carry)==192000000 && carry==0);
    timox_u64 total=0;
    for(unsigned i=0;i<100000;++i)total+=timox_elapsed_pcm_bytes(100,carry);
    check(total==384000 && carry==0);
    auto span=timox_ring_span(16,80,32);
    check(span.bytes==32 && span.offset==0 && span.lostFrames==6);
    check(timox_write_position_valid(0,3840));
    check(timox_write_position_valid(3840,3840));
    check(!timox_write_position_valid(3839,3840));
    check(!timox_write_position_valid(3848,3840));
    check(timox_eos_bytes(64,0,32,32,false)==32);
    check(timox_eos_bytes(64,0,32,32,true)==0);
    check(timox_eos_bytes(64,16,8,32,false)==24);
    check(timox_eos_bytes(64,16,7,32,false)==0);
    check(timox_completed_packets(192000000,3840)==50000);
    check(timox_completed_packets(3839,3840)==0);
    // EOS stops transferred bytes while the presentation clock keeps advancing.
    // Packet completion describes transferred bytes, not the presentation clock.
    timox_u64 presentation=0, transferred=0;
    carry=0;
    auto elapsed=timox_elapsed_pcm_bytes(200000,carry);
    presentation+=elapsed;
    transferred+=timox_eos_bytes(elapsed,0,3840,3840,false);
    check(presentation==7680 && transferred==3840);
    check(timox_completed_packets(transferred,1920)==2);
    elapsed=timox_elapsed_pcm_bytes(5000000000ULL,carry);
    presentation+=elapsed;
    transferred+=timox_eos_bytes(elapsed,0,3840,3840,true);
    check(presentation==192007680 && transferred==3840);
    check(timox_completed_packets(transferred,1920)==2);
    check(timox_bytes_to_hns(192000000)==5000000000ULL);
    check(timox_last_packet_start(1,3840)==0);
    check(timox_last_packet_start(50000,3840)==191996160);
    carry=0;
    check(timox_elapsed_pcm_bytes(~timox_u64{0},carry)==708354972430446776ULL);
    check(carry<10000000);
    if (failures) std::fprintf(stderr,"%u geometry/format failures\n",failures);
    return failures ? 1 : 0;
}
