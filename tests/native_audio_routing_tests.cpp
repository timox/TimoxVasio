#include "audio_routing_runtime.h"
#include "native_audio_backend.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <thread>

namespace {
thread_local bool tracking=false;
thread_local unsigned allocations=0;
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
bool Near(float a,float b){return std::abs(a-b)<1e-5f;}
auto Generation(std::uint64_t epoch=1,std::uint64_t wire=1){
    auto source=std::make_shared<WasapiBridgeIo>(WasapiBridgeFormat{WasapiBridgeDirection::Capture,2,48000,16,epoch,64});
    auto destination=std::make_shared<WasapiBridgeIo>(WasapiBridgeFormat{WasapiBridgeDirection::Render,2,48000,16,epoch,64});
    return std::make_shared<NativeAudioGeneration>(NativeAudioGeneration{epoch,wire,source,destination});
}
AudioRoutingLayout Layout(const std::shared_ptr<NativeAudioGeneration>& generation){
    AudioRoutingLayout layout;layout.sampleRate=48000;layout.bufferFrames=16;
    layout.nativeBindings={{"windows",generation}};return layout;
}
auto Runtime(AudioRoutingLayout layout){std::string error;auto runtime=AudioRoutingRuntime::Create(std::move(layout),error);if(!runtime)throw std::runtime_error(error);return runtime;}
void Transfer(){
    auto generation=Generation();auto layout=Layout(generation);layout.physicalDriverId="card";
    layout.physicalInputChannels=layout.physicalOutputChannels=1;
    layout.routes={{"physical","native:windows:input:1","physical:card:output:1",-6.020599913,false},
        {"native","physical:card:input:1","native:windows:output:2",0,false},
        {"mix","native:windows:input:2","native:windows:output:2",-6.020599913,false},
        {"mute","native:windows:input:1","native:windows:output:1",0,true}};
    auto runtime=Runtime(std::move(layout));float source[32],rendered[32],physicalIn[16],physicalOut[16]{};
    for(unsigned n=0;n<16;++n){source[2*n]=0.5f;source[2*n+1]=0.25f;physicalIn[n]=0.2f;}
    generation->windowsRenderSource->WriteCaptureInterleaved(source,16,32);
    const float* inputs[]={physicalIn};float* outputs[]={physicalOut};
    const auto owners=generation.use_count(),sourceOwners=generation->windowsRenderSource.use_count();
    tracking=true;allocations=0;runtime->Process(inputs,outputs,16,true);tracking=false;
    Check(!allocations && generation.use_count()==owners && generation->windowsRenderSource.use_count()==sourceOwners,"callback allocates or changes ownership");
    Check(generation->windowsCaptureDestination->ReadRenderInterleaved(rendered,16,32)==16,"native output absent");
    for(unsigned n=0;n<16;++n)Check(Near(physicalOut[n],0.25f)&&rendered[2*n]==0&&Near(rendered[2*n+1],0.325f),"native gains/mix/mute/directions");
    const auto meters=runtime->ReadMeters();bool sourceMeter=false,destMeter=false;
    for(const auto& meter:meters){if(meter.endpointId=="native:windows:input:1")sourceMeter=Near(meter.peakLinear,0.5f);if(meter.endpointId=="native:windows:output:2")destMeter=Near(meter.peakLinear,0.325f);}
    Check(sourceMeter&&destMeter,"native source/destination meters");
    for(float& value:physicalIn)value=0;
    runtime->Process(inputs,outputs,16,false);generation->windowsCaptureDestination->ReadRenderInterleaved(rendered,16,32);
    for(float value:rendered)Check(value==0,"empty source retained stale destination");
}
void WasapiTransfer(){
    auto generation=Generation();auto capture=std::make_shared<WasapiBridgeIo>(WasapiBridgeFormat{WasapiBridgeDirection::Capture,2,48000,16,7,32});
    auto render=std::make_shared<WasapiBridgeIo>(WasapiBridgeFormat{WasapiBridgeDirection::Render,2,48000,16,7,32});
    auto layout=Layout(generation);layout.wasapiBindings={{"capture",capture,L"in",false},{"render",render,L"out",false}};
    layout.routes={{"in","wasapi:capture:input:2","native:windows:output:1",0,false},
        {"out","native:windows:input:1","wasapi:render:output:2",0,false}};
    auto runtime=Runtime(std::move(layout));float data[32],out[32];for(unsigned n=0;n<16;++n){data[2*n]=0.4f;data[2*n+1]=-0.3f;}
    capture->WriteCaptureInterleaved(data,16,32);generation->windowsRenderSource->WriteCaptureInterleaved(data,16,32);
    runtime->Process(nullptr,nullptr,16,true);render->ReadRenderInterleaved(out,16,32);
    for(unsigned n=0;n<16;++n)Check(out[2*n]==0&&Near(out[2*n+1],0.4f),"native to WASAPI");
    generation->windowsCaptureDestination->ReadRenderInterleaved(out,16,32);
    for(unsigned n=0;n<16;++n)Check(Near(out[2*n],-0.3f)&&out[2*n+1]==0,"WASAPI to native");
}
void Validation(){
    const auto reject=[](AudioRoutingLayout layout){std::string error;Check(!AudioRoutingRuntime::Create(std::move(layout),error)&&!error.empty(),"invalid native binding accepted");};
    auto generation=Generation(2,1);auto valid=Layout(generation);Runtime(valid);
    auto bad=valid;bad.nativeBindings.push_back(bad.nativeBindings[0]);reject(bad);
    bad=valid;bad.nativeBindings[0].id="other";reject(bad);
    bad=valid;bad.nativeBindings[0].generation.reset();reject(bad);
    bad=valid;bad.sampleRate=44100;reject(bad);
    bad=valid;bad.bufferFrames=32;reject(bad);
    for(auto format : {WasapiBridgeFormat{WasapiBridgeDirection::Capture,1,48000,16,2,64},
        WasapiBridgeFormat{WasapiBridgeDirection::Render,2,48000,16,2,64},
        WasapiBridgeFormat{WasapiBridgeDirection::Capture,2,48000,32,2,64}}) {
        auto wrong=std::make_shared<WasapiBridgeIo>(format);
        bad=Layout(std::make_shared<NativeAudioGeneration>(NativeAudioGeneration{2,1,wrong,generation->windowsCaptureDestination}));reject(bad);
    }
    auto other=Generation(3,1);
    bad=Layout(std::make_shared<NativeAudioGeneration>(NativeAudioGeneration{2,1,generation->windowsRenderSource,other->windowsCaptureDestination}));reject(bad);
    bad=Layout(std::make_shared<NativeAudioGeneration>(NativeAudioGeneration{2,1,generation->windowsRenderSource,generation->windowsRenderSource}));reject(bad);
    bad=Layout(std::make_shared<NativeAudioGeneration>(NativeAudioGeneration{0,1,generation->windowsRenderSource,generation->windowsCaptureDestination}));reject(bad);
    bad=Layout(std::make_shared<NativeAudioGeneration>(NativeAudioGeneration{2,0,generation->windowsRenderSource,generation->windowsCaptureDestination}));reject(bad);
    bad=valid;bad.wasapiBindings={{"alias",generation->windowsRenderSource,L"alias",false}};reject(bad);
    Check(RoutingGraph::SupportsLink(RoutingEndpointType::NativeSource,RoutingEndpointType::VirtualInput)&&
        RoutingGraph::SupportsLink(RoutingEndpointType::VirtualOutput,RoutingEndpointType::NativeDestination),"native virtual-ASIO directions absent");
    Check(!RoutingGraph::SupportsLink(RoutingEndpointType::PhysicalInput,RoutingEndpointType::PhysicalOutput)&&
        !RoutingGraph::SupportsLink(RoutingEndpointType::NativeDestination,RoutingEndpointType::NativeSource),"forbidden directions enabled");
}
void VirtualTransfer(){
    const auto pid=GetCurrentProcessId();
    auto created=AudioClientMapping::CreateClient(0x4e415449u,pid,48000,16);
    Check(!!created&&created->SetEngineAttached(true),"isolated native application mapping");
    AudioClientMapping::ChannelMask inputs{},outputs{};inputs[0]=1ULL<<2;outputs[0]=1ULL<<6;
    Check(created->SetActiveChannels(inputs,outputs),"native application channels");
    auto mapping=std::shared_ptr<AudioClientMapping>(std::move(created));auto generation=Generation();
    auto layout=Layout(generation);layout.clients={{{1,pid,L"native-routing-test.exe"},mapping}};
    const auto prefix="virtual:TimoxVasio:"+std::to_string(pid);
    layout.routes={{"to-app","native:windows:input:2",prefix+":input:3",0,false},
        {"from-app",prefix+":output:7","native:windows:output:1",-6.020599913,false}};
    auto runtime=Runtime(std::move(layout));float nativeIn[32],applicationOut[16],applicationIn[16]{},nativeOut[32];
    for(unsigned n=0;n<16;++n){nativeIn[n*2]=0.1f;nativeIn[n*2+1]=0.3f+n*0.001f;applicationOut[n]=0.5f;}
    generation->windowsRenderSource->WriteCaptureInterleaved(nativeIn,16,32);
    Check(mapping->WriteClientOutput(applicationOut,16),"native application output write");
    tracking=true;allocations=0;runtime->Process(nullptr,nullptr,16);tracking=false;
    Check(!allocations&&mapping->WaitForClientSignal(0),"native application callback allocation/signal");
    Check(mapping->ReadClientInput(applicationIn,16)==16,"native application input absent");
    generation->windowsCaptureDestination->ReadRenderInterleaved(nativeOut,16,32);
    for(unsigned n=0;n<16;++n)Check(Near(applicationIn[n],nativeIn[n*2+1])&&Near(nativeOut[n*2],0.25f)&&nativeOut[n*2+1]==0,"native virtual-ASIO samples/gain/channel");
}
void ExplicitRoutesAndPriming(){
    auto generation=Generation();auto runtime=Runtime(Layout(generation));float data[32],out[32];
    for(float& value:data)value=0.5f;
    generation->windowsRenderSource->WriteCaptureInterleaved(data,16,32);runtime->Process(nullptr,nullptr,16);
    generation->windowsCaptureDestination->ReadRenderInterleaved(out,16,32);
    for(float value:out)Check(value==0,"native route was created automatically");
    Check(runtime->ReadMeters().empty(),"unrouted native endpoints acquired meters");
    runtime.reset();generation=Generation(2,1);
    generation->windowsRenderSource->BeginCapturePriming();auto layout=Layout(generation);
    layout.routes={{"explicit","native:windows:input:1","native:windows:output:1",0,false}};
    runtime=Runtime(std::move(layout));generation->windowsRenderSource->WriteCaptureInterleaved(data,16,32);
    runtime->Process(nullptr,nullptr,16);generation->windowsCaptureDestination->ReadRenderInterleaved(out,16,32);
    Check(out[0]==0&&generation->windowsRenderSource->BufferedFrames()==16,"native graph bypassed source priming gate");
    generation->windowsRenderSource->CompleteCapturePriming();runtime->Process(nullptr,nullptr,16);
    generation->windowsCaptureDestination->ReadRenderInterleaved(out,16,32);Check(out[0]==0.5f&&out[1]==0,"native prefill was not preserved");
}
void EpochLifetime(){
    auto old=Generation(1,9);std::weak_ptr<NativeAudioGeneration> retired=old;
    auto layout=Layout(old);layout.routes={{"persisted","native:windows:input:1","native:windows:output:1",0,false}};
    auto oldRuntime=Runtime(std::move(layout));old.reset();
    std::atomic<bool> entered{false},stop{false};std::atomic<unsigned> callbackAllocations{0};
    std::thread callback([&]{tracking=true;allocations=0;entered=true;while(!stop.load())oldRuntime->Process(nullptr,nullptr,16);callbackAllocations=allocations;tracking=false;});
    while(!entered.load())std::this_thread::yield();
    auto next=Generation(2,9);auto nextLayout=Layout(next);nextLayout.routes={{"persisted","native:windows:input:1","native:windows:output:1",0,false}};
    auto nextRuntime=Runtime(std::move(nextLayout));const bool alive=!retired.expired();
    stop=true;callback.join();Check(alive&&!callbackAllocations,"borrowed callback generation destroyed or allocated");
    oldRuntime.reset();Check(retired.expired(),"retired generation survived off-thread retirement");
    float data[32],result[32];for(float& value:data)value=0.75f;next->windowsRenderSource->WriteCaptureInterleaved(data,16,32);
    nextRuntime->Process(nullptr,nullptr,16);next->windowsCaptureDestination->ReadRenderInterleaved(result,16,32);
    Check(result[0]==0.75f&&result[1]==0,"same wire generation confused new lifetime epoch");
}
}
void* operator new(std::size_t size){if(tracking)++allocations;if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
int main(){try{Transfer();WasapiTransfer();VirtualTransfer();ExplicitRoutesAndPriming();Validation();EpochLifetime();std::puts("Native audio routing tests passed");return 0;}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
