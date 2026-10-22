#pragma once
#include "audio_routing_runtime.h"
#include "native_audio_backend.h"
#include "realtime_callback_slot.h"
#include "software_clock_host.h"

inline bool NativeAudioLabSnapshotCompleted(bool observedFailure,std::uint64_t callbacks,
    bool fixturePassed,const NativeAudioHealth& health) noexcept {
    return !observedFailure && callbacks>0 && fixturePassed &&
        health.state==NativeAudioState::Running && health.error==NativeTransferCode::Ok;
}

// Laboratory control owner, not production activation. All methods except the
// private graph callback are serialized by the caller. No driver is selected or
// opened here: Attach receives an explicitly constructed injectable backend.
class NativeAudioLabSession final {
public:
    NativeAudioLabSession():slot_(&AudioRoutingRuntime::PhysicalCallback){}
    ~NativeAudioLabSession(){Stop();}
    NativeAudioLabSession(const NativeAudioLabSession&)=delete;
    NativeAudioLabSession& operator=(const NativeAudioLabSession&)=delete;
    bool StartClock(std::uint32_t rate,std::uint32_t quantum,std::string& error){
        if(running_){error="Laboratory clock already running";return false;}
        AudioClockCapabilities caps;
        if(!clock_.open(L"software",nullptr,caps,error)||!clock_.setSampleRate(rate,error))return false;
        rate_=rate;quantum_=quantum;
        AudioRoutingLayout empty;empty.sampleRate=rate;empty.bufferFrames=quantum;
        emptyRuntime_=AudioRoutingRuntime::Create(std::move(empty),error);if(!emptyRuntime_)return false;
        slot_.Publish(emptyRuntime_.get());
        if(!clock_.start(quantum,{}, {},&Dispatch,this,error)){
            slot_.Publish(nullptr);slot_.WaitForQuiescence();emptyRuntime_.reset();clock_.close();return false;
        }
        running_=true;return true;
    }
    bool SetRoutes(std::vector<AudioRoute> routes,std::string& error){
        if(!running_){error="Start laboratory clock first";return false;}
        const std::vector<RoutingEndpoint> endpoints={
            {"native:windows:input:1",RoutingEndpointType::NativeSource,rate_},
            {"native:windows:input:2",RoutingEndpointType::NativeSource,rate_},
            {"native:windows:output:1",RoutingEndpointType::NativeDestination,rate_},
            {"native:windows:output:2",RoutingEndpointType::NativeDestination,rate_}};
        if(!RoutingGraph::Create(endpoints,routes,rate_,error))return false;
        if(backend_){
            auto replacement=MakeRuntime(backend_->Generation(),routes,error);if(!replacement)return false;
            auto retired=std::move(runtime_);runtime_=std::move(replacement);
            slot_.Publish(runtime_.get());slot_.WaitForQuiescence();retired.reset();
        }
        desired_=std::move(routes);return true;
    }
    NativeTransferCode Attach(std::unique_ptr<NativeAudioBackend> candidate,std::string& error){
        if(!running_||!candidate||nextEpoch_==0){error="Invalid laboratory attachment state";return NativeTransferCode::Failed;}
        if(backend_){error="Detach the previous owner before opening a replacement";return NativeTransferCode::OwnedElsewhere;}
        const auto result=candidate->Start(rate_,quantum_,nextEpoch_++);
        if(result!=NativeTransferCode::Ok){lastHealth_=candidate->Health();candidate->Stop();error="Native start failed";return result;}
        auto replacement=MakeRuntime(candidate->Generation(),desired_,error);
        if(!replacement){candidate->Stop();return NativeTransferCode::InvalidPacket;}
        backend_=std::move(candidate);runtime_=std::move(replacement);
        slot_.Publish(runtime_.get());slot_.WaitForQuiescence();error.clear();return NativeTransferCode::Ok;
    }
    void Detach() noexcept {
        slot_.Publish(emptyRuntime_.get());slot_.WaitForQuiescence();runtime_.reset();
        // The graph no longer borrows this generation before worker Stop/join.
        if(backend_){lastHealth_=backend_->Health();backend_->Stop();backend_.reset();}
    }
    bool PollFailure() noexcept {
        if(backend_&&backend_->Health().state==NativeAudioState::Failed){Detach();return true;}
        return false;
    }
    void Stop() noexcept {
        Detach();clock_.stop();slot_.Publish(nullptr);slot_.WaitForQuiescence();
        emptyRuntime_.reset();clock_.close();running_=false;
    }
    std::shared_ptr<NativeAudioGeneration> Generation()const noexcept{return backend_?backend_->Generation():nullptr;}
    NativeAudioHealth Health()const noexcept{return backend_?backend_->Health():lastHealth_;}
    std::uint64_t Callbacks()const noexcept{return callbacks_.load(std::memory_order_relaxed);}
    const std::vector<AudioRoute>& DesiredRoutes()const noexcept{return desired_;}
    std::vector<AudioMeterSnapshot> Meters(){return runtime_?runtime_->ReadMeters():std::vector<AudioMeterSnapshot>{};}
private:
    std::unique_ptr<AudioRoutingRuntime> MakeRuntime(std::shared_ptr<NativeAudioGeneration> generation,
        const std::vector<AudioRoute>& routes,std::string& error){
        AudioRoutingLayout layout;layout.sampleRate=rate_;layout.bufferFrames=quantum_;
        layout.nativeBindings={{"windows",std::move(generation)}};layout.routes=routes;
        return AudioRoutingRuntime::Create(std::move(layout),error);
    }
    static void Dispatch(const float* const* inputs,float* const* outputs,std::uint32_t frames,void* context)noexcept{
        auto& self=*static_cast<NativeAudioLabSession*>(context);
        RealtimeCallbackSlot::Dispatch(inputs,outputs,frames,&self.slot_);
        self.callbacks_.fetch_add(1,std::memory_order_relaxed);
    }
    SoftwareClockHost clock_;
    RealtimeCallbackSlot slot_;
    std::unique_ptr<AudioRoutingRuntime> emptyRuntime_,runtime_;
    std::unique_ptr<NativeAudioBackend> backend_;
    std::vector<AudioRoute> desired_;
    NativeAudioHealth lastHealth_;
    std::atomic<std::uint64_t> callbacks_{0};
    std::uint64_t nextEpoch_=1;
    std::uint32_t rate_=0,quantum_=0;
    bool running_=false;
};
