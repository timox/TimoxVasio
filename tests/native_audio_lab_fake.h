#pragma once
#include "native_audio_transport.h"
#include "native_audio_packet_validation.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

// In-process laboratory source/sink. No device handle, kernel driver, Windows
// endpoint or latency/fidelity qualification. Constant stereo PCM makes channel
// inversion and the explicitly configured left -6.0206 dB gain observable.
struct NativeAudioLabFakeState {
    NativeTransferCode openCode=NativeTransferCode::Ok;
    std::atomic<unsigned> opens{0},closes{0};
    std::atomic<std::uint64_t> captureFrames{0};
    std::atomic<bool> removed{false},heardAny{false},heardExpected{false},badPacket{false},wrongThread{false};
};
class NativeAudioLabFakeTransport final:public INativeAudioTransport {
public:
    explicit NativeAudioLabFakeTransport(std::shared_ptr<NativeAudioLabFakeState> state):state_(std::move(state)){}
    NativeTransferResult Open(NativeSessionInfo& session)noexcept override {
        thread_=std::this_thread::get_id();++state_->opens;session.wireGeneration=7;
        next_=Clock::now()+std::chrono::milliseconds(5);return {state_->openCode};
    }
    NativeTransferResult ReadRender(std::uint32_t maximum,void* output,std::uint32_t capacity)noexcept override {
        CheckThread();if(cancelled_)return {NativeTransferCode::Cancelled};if(state_->removed)return {NativeTransferCode::Removed};
        const auto now=Clock::now();if(now<next_)return {NativeTransferCode::Empty};
        if(maximum<240||capacity<1984||!output)return {NativeTransferCode::InvalidPacket};
        if(now-next_>std::chrono::milliseconds(20))next_=now;
        next_+=std::chrono::milliseconds(5);
        auto* packet=static_cast<unsigned char*>(output);std::memset(packet,0,1984);
        Put(packet,0,0x41575654,4);Put(packet,4,64,4);Put(packet,8,1,2);Put(packet,12,1,2);Put(packet,14,1,2);
        Put(packet,16,7,8);Put(packet,24,sequence_++,8);Put(packet,40,240,4);Put(packet,44,48000,4);
        Put(packet,48,2,2);Put(packet,50,1,2);Put(packet,52,1920,4);
        for(unsigned frame=0;frame<240;++frame){Put(packet,64+frame*8,0x20000000,4);Put(packet,68+frame*8,0xe0000000,4);}
        return {NativeTransferCode::Ok,1984};
    }
    NativeTransferResult WriteCapture(const void* input,std::uint32_t bytes)noexcept override {
        CheckThread();if(cancelled_)return {NativeTransferCode::Cancelled};if(state_->removed)return {NativeTransferCode::Removed};
        const auto packet=timox_native::ValidatePacket(input,bytes,{7,2,512});
        if(packet.error!=timox_native::PacketError::None){state_->badPacket=true;return {NativeTransferCode::InvalidPacket};}
        state_->captureFrames+=packet.metadata.frames;const auto* data=static_cast<const unsigned char*>(input);
        for(unsigned frame=0;frame<packet.metadata.frames;++frame){
            const auto left=timox_native::detail::ReadLittleEndian(data,64+frame*8,4);
            const auto right=timox_native::detail::ReadLittleEndian(data,68+frame*8,4);
            if(left||right)state_->heardAny=true;
            if(left>0x0f000000&&left<0x11000000&&right>0xdf000000&&right<0xe1000000)state_->heardExpected=true;
        }
        return {NativeTransferCode::Ok};
    }
    NativeTransferResult ReadStatus(std::uint64_t (&counters)[3])noexcept override {
        CheckThread();counters[0]=counters[1]=counters[2]=0;return {NativeTransferCode::Ok};
    }
    void RequestCancel()noexcept override{cancelled_=true;}
    void Close()noexcept override{CheckThread();++state_->closes;}
private:
    using Clock=std::chrono::steady_clock;
    static void Put(unsigned char* data,unsigned offset,std::uint64_t value,unsigned width)noexcept{
        for(unsigned i=0;i<width;++i)data[offset+i]=static_cast<unsigned char>(value>>(8*i));
    }
    void CheckThread()noexcept{if(thread_!=std::this_thread::get_id())state_->wrongThread=true;}
    std::shared_ptr<NativeAudioLabFakeState> state_;
    std::atomic<bool> cancelled_{false};
    std::thread::id thread_;
    Clock::time_point next_;
    std::uint64_t sequence_=0;
};
