#include "native_audio_packet_queue.h"
#include "native_audio_content_policy.h"
#include <vector>
#include <cstdio>
using namespace timox_native;
static void put(std::vector<unsigned char>& p,unsigned offset,timox_u64 value,unsigned count){
    for(unsigned i=0;i<count;++i)p[offset+i]=static_cast<unsigned char>(value>>(i*8));
}
static std::vector<unsigned char> packet(timox_u64 generation,timox_u64 sequence,unsigned frames=2){
    std::vector<unsigned char> p(64+frames*8,0);
    put(p,0,0x41575654,4);put(p,4,64,4);put(p,8,1,2);put(p,12,2,2);put(p,14,2,2);
    put(p,16,generation,8);put(p,24,sequence,8);put(p,40,frames,4);put(p,44,48000,4);
    put(p,48,2,2);put(p,50,1,2);put(p,52,frames*8,4);
    for(unsigned i=64;i<p.size();++i)p[i]=static_cast<unsigned char>(i+11);
    return p;
}
int main(){
    PacketQueues q;
    unsigned failures=0;
    auto check=[&](bool b){if(!b)++failures;};
    RenderContentPolicy content;
    check(!content.Update(true,false,false));
    check(content.Update(true,true,false));
    check(content.Update(true,false,false)); // downgrade cannot expose old DMA
    RenderContentPolicy unknown;
    check(unknown.Update(false,false,false));
    check(unknown.Update(true,false,false));
    RenderContentPolicy digital;
    check(digital.Update(true,false,true));
    timox_u64 generation=0;
    unsigned char output[32832]{};
    check(q.Open(11,generation)&&generation==1);
    auto p=packet(generation,1);
    check(q.WriteCapture(12,generation,p.data(),p.size())==Transfer::Owner);
    check(q.WriteCapture(11,2,p.data(),p.size())==Transfer::Owner);
    for(unsigned size=0;size<p.size();++size)
        check(q.WriteCapture(11,generation,p.data(),size)==Transfer::Packet);
    check(q.WriteCapture(11,generation,p.data(),p.size())==Transfer::Ok);
    check(q.WriteCapture(11,generation,p.data(),p.size())==Transfer::Sequence);
    check(q.ReadCapture(output,3)==2);
    for(unsigned i=0;i<16;++i)check(output[i]==p[64+i]);
    for(unsigned i=16;i<24;++i)check(output[i]==0);
    check(q.CaptureSilence()==1);
    auto empty=q.ReadRender(11,generation,output,sizeof(output),4096);
    check(empty.status==Transfer::Empty && empty.bytes==0 && q.CaptureSilence()==1);
    check(q.PushRender(p.data()+64,2));
    check(q.ReadRender(12,generation,output,sizeof(output),2).status==Transfer::Owner);
    check(q.ReadRender(11,generation,output,79,2).status==Transfer::Buffer);
    auto render=q.ReadRender(11,generation,output,sizeof(output),4096);
    auto validated=ValidatePacket(output,render.bytes,{generation,1,4096});
    check(render.status==Transfer::Ok && validated.error==PacketError::None);
    check(validated.metadata.frames==2 && validated.metadata.sequence==1);
    check((validated.metadata.flags&TIMOX_NATIVE_DISCONTINUITY)!=0);
    check(q.ReadRender(11,generation,output,sizeof(output),1).status==Transfer::Empty);
    check(q.PushRender(p.data()+64,2));
    q.SetRenderBlocked(true);
    check(!q.PushRender(p.data()+64,2));
    check(q.ReadRender(11,generation,output,sizeof(output),2).status==Transfer::Protected);
    q.SetRenderBlocked(false);
    check(q.ReadRender(11,generation,output,sizeof(output),2).status==Transfer::Empty);
    q.AccountGap(false,10);
    check(q.RenderDropped()==14); // queued 2 + blocked 2 + elapsed gap 10
    check(q.PushRender(p.data()+64,2));
    check(!q.Close(12));
    check(q.Close(11));
    check(q.Open(12,generation)&&generation==2);
    check(q.ReadRender(12,generation,output,sizeof(output),2).status==Transfer::Empty);
    check(q.ReadCapture(output,2)==0);
    check(q.WriteCapture(12,generation,p.data(),p.size())==Transfer::Packet);
    auto full=packet(generation,1,4096);
    check(q.WriteCapture(12,generation,full.data(),full.size())==Transfer::Ok);
    p=packet(generation,2);
    check(q.WriteCapture(12,generation,p.data(),p.size())==Transfer::Full);
    check(q.ReadCapture(output,4096)==4096);
    check(q.WriteCapture(12,generation,p.data(),p.size())==Transfer::Ok); // retry same sequence
    // Deterministic malformed-header fuzz: each mutated version must be rejected.
    for(unsigned i=0;i<256;++i){auto bad=packet(generation,3);put(bad,8,i+2,2);
        check(q.WriteCapture(12,generation,bad.data(),bad.size())==Transfer::Packet);}
    auto last=packet(generation,~timox_u64{0});
    check(q.WriteCapture(12,generation,last.data(),last.size())==Transfer::Ok);
    auto wrapped=packet(generation,0);
    check(q.WriteCapture(12,generation,wrapped.data(),wrapped.size())==Transfer::Sequence);
    q.Remove();
    check(!q.PushRender(p.data()+64,2));
    check(q.ReadRender(12,generation,output,sizeof(output),2).status==Transfer::Owner);
    check(!q.Open(13,generation));
    PacketQueues protectedQueue;
    check(protectedQueue.Open(1,generation));
    protectedQueue.SetRenderBlocked(true);
    check(protectedQueue.Close(1));
    check(protectedQueue.Open(2,generation));
    check(protectedQueue.ReadRender(2,generation,output,sizeof(output),2).status==Transfer::Protected);
    PacketQueues delayedCapture;
    check(delayedCapture.Open(1,generation));
    auto oldPcm=packet(generation,1);
    check(delayedCapture.WriteCapture(1,generation,oldPcm.data(),oldPcm.size())==Transfer::Ok);
    delayedCapture.AccountGap(true,50000);
    check(delayedCapture.CaptureDropped()==50002);
    check(delayedCapture.ReadCapture(output,2)==0);
    for(unsigned i=0;i<16;++i)check(output[i]==0); // no stale pre-stall PCM
    if(failures)std::fprintf(stderr,"%u packet queue failures\n",failures);
    return failures?1:0;
}
