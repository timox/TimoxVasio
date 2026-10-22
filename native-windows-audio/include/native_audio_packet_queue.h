#pragma once
#include "native_audio_session.h"
#include "native_audio_frame_queue.h"
#include "native_audio_packet_validation.h"

namespace timox_native {
enum class Transfer { Ok, Owner, Packet, Sequence, Full, Empty, Buffer, Protected };
struct RenderResult { Transfer status; timox_u32 bytes; };

// All calls, including audio callbacks, must use the same external exclusion.
// Own in preallocated nonpaged storage, NOT kernel stack (two 32 KiB rings).
// Input/output spans are trusted readable/writable private buffers, never raw
// user addresses. ACL, FILE_OBJECT identity, locking and rundown are external.
class PacketQueues final {
public:
    bool Allows(timox_u64 owner,timox_u64 generation) const noexcept {
        return session_.Allows(owner,generation);
    }
    void SetRenderBlocked(bool blocked) noexcept {
        renderBlocked_=blocked;
        if(blocked){render_.Discard();renderDiscontinuity_=true;}
    }
    void AccountGap(bool capture,timox_u64 frames) noexcept {
        if(!frames)return;
        auto& queue=capture ? capture_:render_;
        queue.Discard();queue.RecordDrop(frames);
        if(!capture)renderDiscontinuity_=true;
    }
    bool Open(timox_u64 owner,timox_u64& generation) noexcept {
        if(!session_.Open(owner,generation))return false;
        generation_=generation; owner_=owner; Reset(); return true;
    }
    bool Close(timox_u64 owner) noexcept {
        if(!session_.Close(owner))return false;
        owner_=0; Reset(); return true;
    }
    void Remove() noexcept { session_.Remove();owner_=0;Reset(); }
    Transfer WriteCapture(timox_u64 owner,timox_u64 generation,const void* packet,
                          packet_size bytes) noexcept {
        if(!session_.Allows(owner,generation))return Transfer::Owner;
        const auto result=ValidatePacket(packet,bytes,{generation_,TIMOX_NATIVE_TO_WINDOWS_CAPTURE,4096});
        if(result.error!=PacketError::None)return Transfer::Packet;
        if(captureSeen_ && result.metadata.sequence<=captureSequence_)return Transfer::Sequence;
        if(!capture_.Push(static_cast<const unsigned char*>(packet)+64,result.metadata.frames))
            return Transfer::Full;
        captureSeen_=true; captureSequence_=result.metadata.sequence;
        return Transfer::Ok;
    }
    // Trusted WaveRT producer. Loss never blocks progression of its clock.
    bool PushRender(const unsigned char* pcm,timox_u32 frames) noexcept {
        if(renderBlocked_){render_.RecordDrop(frames);return false;}
        if(!owner_)return false;
        if(!render_.Push(pcm,frames)){renderDiscontinuity_=true;return false;}
        return true;
    }
    timox_u32 ReadCapture(unsigned char* pcm,timox_u32 frames) noexcept {
        // Close purges capture, so disconnected consumers receive silence.
        return capture_.Read(pcm,frames);
    }
    RenderResult ReadRender(timox_u64 owner,timox_u64 generation,unsigned char* output,
                            packet_size bytes,timox_u32 maxFrames) noexcept {
        if(!session_.Allows(owner,generation))return {Transfer::Owner,0};
        if(renderBlocked_)return {Transfer::Protected,0};
        if(!output || !maxFrames || maxFrames>4096)return {Transfer::Buffer,0};
        const auto available=render_.Available();
        if(!available)return {Transfer::Empty,0};
        const auto frames=maxFrames<available ? maxFrames:available;
        const auto packetBytes=64+frames*8;
        if(bytes<packetBytes)return {Transfer::Buffer,0};
        if(renderSequence_==~timox_u64{0})return {Transfer::Sequence,0};
        for(unsigned i=0;i<64;++i)output[i]=0;
        Put(output,0,0x41575654,4);Put(output,4,64,4);Put(output,8,1,2);
        Put(output,12,TIMOX_NATIVE_RENDER_PACKET,2);Put(output,14,TIMOX_NATIVE_FROM_WINDOWS_RENDER,2);
        Put(output,16,generation_,8);Put(output,24,++renderSequence_,8);
        Put(output,40,frames,4);Put(output,44,48000,4);Put(output,48,2,2);
        Put(output,50,TIMOX_NATIVE_PCM_S32,2);Put(output,52,frames*8,4);
        Put(output,56,renderDiscontinuity_ ? TIMOX_NATIVE_DISCONTINUITY:0,4);
        render_.Read(output+64,frames); // exact available frames: no silence on render
        renderDiscontinuity_=false;
        return {Transfer::Ok,packetBytes};
    }
    timox_u64 CaptureSilence() const noexcept {return capture_.SilentFrames();}
    timox_u64 RenderDropped() const noexcept {return render_.DroppedFrames();}
    timox_u64 CaptureDropped() const noexcept {return capture_.DroppedFrames();}
private:
    static void Put(unsigned char* output,unsigned offset,timox_u64 value,unsigned width) noexcept {
        for(unsigned i=0;i<width;++i)output[offset+i]=static_cast<unsigned char>(value>>(i*8));
    }
    void Reset() noexcept {
        render_.Reset();capture_.Reset();captureSeen_=false;captureSequence_=0;
        renderSequence_=0;renderDiscontinuity_=true;
    }
    Session session_;
    FrameQueue render_,capture_;
    timox_u64 owner_=0,generation_=0,captureSequence_=0,renderSequence_=0;
    bool captureSeen_=false,renderDiscontinuity_=true;
    bool renderBlocked_=false; // Never reset by client close/reopen.
};
}
