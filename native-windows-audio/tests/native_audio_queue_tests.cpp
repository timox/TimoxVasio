#include "native_audio_frame_queue.h"
#include <cstdio>
int main() {
    timox_native::FrameQueue q;
    unsigned failures=0;
    auto check=[&](bool ok){if(!ok) ++failures;};
    unsigned char input[32768], output[32768];
    for(unsigned i=0;i<32768;++i) input[i]=static_cast<unsigned char>(i*13+7);
    check(q.Push(input,4096));
    check(!q.Push(input,1) && q.DroppedFrames()==1);
    check(q.Read(output,4095)==4095);
    for(unsigned i=0;i<4095*8;++i) check(output[i]==input[i]);
    check(q.Push(input,4095)); // wrap after one remaining frame
    check(q.Read(output,4096)==4096);
    for(unsigned i=0;i<8;++i) check(output[i]==input[4095*8+i]);
    for(unsigned i=8;i<32768;++i) check(output[i]==input[i-8]);
    check(q.Read(output,2)==0 && q.SilentFrames()==2);
    for(unsigned i=0;i<16;++i) check(output[i]==0);
    check(q.Push(input,1));
    check(!q.Push(nullptr,1));
    check(!q.Push(input,4097));
    check(q.Read(nullptr,1)==0);
    check(q.Read(output,4097)==0);
    check(q.Read(output,2)==1 && q.SilentFrames()==3);
    for(unsigned i=0;i<8;++i) check(output[i]==input[i]);
    for(unsigned i=8;i<16;++i) check(output[i]==0);
    check(q.Push(input,3));
    q.Reset();
    check(q.DroppedFrames()==0 && q.SilentFrames()==0);
    check(q.Read(output,3)==0);
    for(unsigned i=0;i<24;++i) check(output[i]==0);
    if(failures) std::fprintf(stderr,"%u queue failures\n",failures);
    return failures ? 1 : 0;
}
