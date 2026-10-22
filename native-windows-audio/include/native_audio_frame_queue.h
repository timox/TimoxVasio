#pragma once
#include "native_audio_geometry.h"

namespace timox_native {
// Trusted PCM only, NOT a wire-packet entry point. Validate N0 + owner first.
// Caller serializes all operations and owns lifetime. Allocate this object in
// nonpaged storage before audio starts, never on the kernel stack (32 KiB).
class FrameQueue final {
public:
    static constexpr timox_u32 Capacity=4096, FrameBytes=8;
    FrameQueue() noexcept = default;
    FrameQueue(const FrameQueue&)=delete;
    FrameQueue& operator=(const FrameQueue&)=delete;
    bool Push(const unsigned char* pcm, timox_u32 frames) noexcept {
        if (!pcm || frames==0 || frames>Capacity) return false;
        if (frames>Capacity-size_) {
            AddSaturating(dropped_,frames);
            return false; // whole block rejected, existing frames preserved
        }
        for (timox_u32 frame=0;frame<frames;++frame) {
            const auto offset=((head_+size_+frame)%Capacity)*FrameBytes;
            for(timox_u32 byte=0;byte<FrameBytes;++byte)
                data_[offset+byte]=pcm[frame*FrameBytes+byte];
        }
        size_+=frames;
        return true;
    }
    // Returns real frames; remaining requested frames are deterministically zero.
    // Invalid request returns zero without touching destination or queue.
    timox_u32 Read(unsigned char* pcm, timox_u32 frames) noexcept {
        if (!pcm || frames==0 || frames>Capacity) return 0;
        const auto available=frames<size_ ? frames:size_;
        for(timox_u32 frame=0;frame<frames;++frame) {
            const auto offset=((head_+frame)%Capacity)*FrameBytes;
            for(timox_u32 byte=0;byte<FrameBytes;++byte)
                pcm[frame*FrameBytes+byte]=frame<available ? data_[offset+byte]:0;
        }
        head_=(head_+available)%Capacity;
        size_-=available;
        AddSaturating(silent_,frames-available);
        return available;
    }
    // Must be serialized with admission and transfers on session replacement.
    void Reset() noexcept {
        head_=size_=0;
        dropped_=silent_=0;
        // No stale samples can be read: Read only accesses frames below size_.
    }
    timox_u64 DroppedFrames() const noexcept { return dropped_; }
    timox_u64 SilentFrames() const noexcept { return silent_; }
    timox_u32 Available() const noexcept { return size_; }
    void RecordDrop(timox_u64 frames) noexcept {AddSaturating(dropped_,frames);}
    void Discard() noexcept {RecordDrop(size_);head_=size_=0;}
private:
    static void AddSaturating(timox_u64& value,timox_u64 increment) noexcept {
        const auto maximum=~timox_u64{0};
        value=maximum-value<increment ? maximum:value+increment;
    }
    unsigned char data_[Capacity*FrameBytes]{};
    timox_u32 head_=0,size_=0;
    timox_u64 dropped_=0,silent_=0;
};
}
