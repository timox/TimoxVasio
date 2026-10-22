#include "native_audio_backend.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>
#include <array>
#include <vector>
#include <cmath>
#include <cstring>
#include <new>
#include "native_audio_packet_validation.h"

thread_local bool countAllocations=false;
std::atomic<unsigned> pumpAllocations{0};
void* operator new(std::size_t bytes) {
    if(countAllocations)++pumpAllocations;
    if(auto* p=std::malloc(bytes?bytes:1))return p;throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void* operator new[](std::size_t bytes){return ::operator new(bytes);}
void operator delete[](void* p) noexcept{::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept{::operator delete(p);}

namespace {
void Require(bool value,const char* message) {
    if(!value){std::cerr << "FAIL: " << message << '\n';std::exit(1);}
}
class Clock final : public INativeAudioScheduler {
public:
    std::mutex mutex;std::condition_variable changed;
    std::uint64_t now=0, waits=0;bool stopped=false;
    std::uint64_t NowNanoseconds() noexcept override {std::lock_guard<std::mutex> l(mutex);return now;}
    bool WaitUntil(std::uint64_t deadline) noexcept override {
        countAllocations=true;
        std::unique_lock<std::mutex> l(mutex);++waits;changed.notify_all();
        changed.wait(l,[&]{return stopped || now>=deadline;});return !stopped;
    }
    void RequestStop() noexcept override {std::lock_guard<std::mutex> l(mutex);stopped=true;changed.notify_all();}
    void Tick(std::uint64_t ns=5000000) {
        std::unique_lock<std::mutex> l(mutex);const auto previous=waits;now+=ns;changed.notify_all();
        Require(changed.wait_for(l,std::chrono::seconds(3),[&]{return stopped || waits>previous;}),"worker returns to bounded wait");
    }
    void AwaitWait() {
        std::unique_lock<std::mutex> l(mutex);
        Require(changed.wait_for(l,std::chrono::seconds(3),[&]{return waits>0;}),"worker reaches first deadline");
    }
};
class Transport final : public INativeAudioTransport {
public:
    std::atomic<unsigned> opens{0},closes{0},reads{0},writes{0};
    std::atomic<bool> cancelled{false};
    std::atomic<bool> blockOpen{false},openEntered{false};
    std::mutex openMutex;std::condition_variable openEvent;
    NativeTransferCode openResult=NativeTransferCode::Ok;
    unsigned renderPackets=0;std::uint64_t sequence=0;bool accept=false,malformed=false;
    std::vector<unsigned char> pending;
    bool retryChanged=false,wrongCapture=false,heardCapture=false;
    std::uint64_t captureFrames=0;
    unsigned statusCalls=0;
    Transport(){pending.reserve(4160);}
    static void Put(unsigned char* p,unsigned offset,std::uint64_t value,unsigned width) noexcept {
        for(unsigned i=0;i<width;++i)p[offset+i]=static_cast<unsigned char>(value>>(i*8));
    }
    NativeTransferResult Open(NativeSessionInfo& info) noexcept override {
        ++opens;openEntered=true;
        if(blockOpen){std::unique_lock<std::mutex> lock(openMutex);openEvent.wait(lock,[&]{return cancelled.load();});return {NativeTransferCode::Cancelled};}
        info.wireGeneration=7;return {openResult,0,openResult==NativeTransferCode::Ok?0u:5u};
    }
    NativeTransferResult ReadRender(std::uint32_t,void* output,std::uint32_t) noexcept override {
        ++reads;if(!renderPackets)return {NativeTransferCode::Empty};--renderPackets;
        auto* p=static_cast<unsigned char*>(output);std::memset(p,0,1984);
        Put(p,0,0x41575654,4);Put(p,4,64,4);Put(p,8,1,2);Put(p,12,1,2);Put(p,14,1,2);
        Put(p,16,malformed?8:7,8);Put(p,24,sequence++,8);Put(p,40,240,4);Put(p,44,48000,4);
        Put(p,48,2,2);Put(p,50,1,2);Put(p,52,1920,4);
        for(unsigned frame=0;frame<240;++frame){Put(p,64+frame*8,0x20000000,4);Put(p,68+frame*8,0xe0000000,4);}
        return {NativeTransferCode::Ok,1984};
    }
    NativeTransferResult WriteCapture(const void* data,std::uint32_t bytes) noexcept override {
        ++writes;const auto* p=static_cast<const unsigned char*>(data);
        if(!pending.empty() && (bytes!=pending.size() || std::memcmp(pending.data(),p,bytes)))retryChanged=true;
        const auto validated=timox_native::ValidatePacket(data,bytes,{7,2,512});
        if(validated.error!=timox_native::PacketError::None)wrongCapture=true;
        if(!accept){pending.assign(p,p+bytes);return {NativeTransferCode::Full};}
        pending.clear();captureFrames+=validated.metadata.frames;
        for(unsigned frame=0;frame<validated.metadata.frames;++frame){
            const auto left=static_cast<std::uint32_t>(timox_native::detail::ReadLittleEndian(p,64+frame*8,4));
            const auto right=static_cast<std::uint32_t>(timox_native::detail::ReadLittleEndian(p,68+frame*8,4));
            if(left>0x10000000 && left<0x30000000 && right>0xd0000000 && right<0xf0000000)heardCapture=true;
        }
        return {NativeTransferCode::Ok};
    }
    NativeTransferResult ReadStatus(std::uint64_t (&c)[3]) noexcept override {++statusCalls;c[0]=11;c[1]=22;c[2]=33;return {NativeTransferCode::Ok};}
    void RequestCancel() noexcept override {std::lock_guard<std::mutex> lock(openMutex);cancelled=true;openEvent.notify_all();}
    void Close() noexcept override {++closes;}
};
void OneDuplexSessionAndBoundedBackpressure() {
    auto transport=std::make_unique<Transport>();auto* t=transport.get();
    auto clock=std::make_unique<Clock>();auto* c=clock.get();
    NativeAudioBackend backend(std::move(transport),std::move(clock));
    Require(backend.Start(48000,256,19)==NativeTransferCode::Ok,"duplex backend opens successfully");
    c->AwaitWait();const auto generation=backend.Generation();
    Require(generation && generation->epoch==19 && generation->wireGeneration==7,"local epoch independent from wire generation");
    Require(t->opens==1,"duplex uses exactly one session");
    for(unsigned tick=0;tick<1000;++tick){const auto reads=t->reads.load(),writes=t->writes.load();c->Tick();
        Require(t->reads<=reads+4 && t->writes<=writes+4,"per-turn operations bounded on empty/full");}
    Require(t->reads>=1000 && t->writes>=1000,"full capture cannot starve render reads");
    Require(!t->retryChanged,"Full retains exactly one immutable packet across clock ticks");
    Require(t->statusCalls==20,"kernel stats polled at 250ms instead of every wake");
    const auto health=backend.Health();
    Require(health.kernelRenderDroppedNativeFrames==11 && health.kernelCaptureDroppedNativeFrames==22 &&
        health.kernelCaptureSilencedNativeFrames==33,"kernel native frame counters preserved independently");
    backend.Stop();Require(t->cancelled && t->closes==1,"stop cancels and joins before closing session");
    Require(backend.Generation()==generation,"retired IO lifetime stays available until graph quiescence");
}
void DuplexPcmAtIndependentGraphRates() {
    for(const auto rate:{44100u,48000u,96000u}) {
        auto transport=std::make_unique<Transport>();auto* t=transport.get();t->accept=true;
        auto clock=std::make_unique<Clock>();auto* c=clock.get();
        NativeAudioBackend backend(std::move(transport),std::move(clock));
        Require(backend.Start(rate,512,51)==NativeTransferCode::Ok,"supported cross-rate graph starts");c->AwaitWait();
        const auto g=backend.Generation();std::array<float,512> left{},right{},outLeft{},outRight{};
        left.fill(.25f);right.fill(-.25f);const float* input[]={left.data(),right.data()};float* output[]={outLeft.data(),outRight.data()};
        bool heard=false;
        for(unsigned tick=0;tick<250;++tick){
            const unsigned count=rate/200+((rate==44100 && (tick&1))?1u:0u);
            g->windowsCaptureDestination->WriteRenderBlock(input,count,2);
            t->renderPackets=1;c->Tick();g->windowsRenderSource->ReadCaptureBlock(output,count,2);
            if(tick>100 && std::abs(outLeft[count-1]-.25f)<.002f && std::abs(outRight[count-1]+.25f)<.002f)heard=true;
        }
        Require(heard,"native PCM passes real SRC to graph preserving channel polarity");
        Require(t->heardCapture && !t->wrongCapture,"graph PCM passes reverse SRC into valid native packets");
        Require(t->captureFrames==60000,"capture paced at native 48000 Hz independently of graph rate");
        for(unsigned tick=0;tick<100;++tick)c->Tick();
        Require(backend.Health().captureSilencedGraphFrames>0,"partial shortage reported in graph frames before SRC");
        backend.Stop();
    }
}
void FailuresCancelAndPacing() {
    {
        auto t=std::make_unique<Transport>();auto* raw=t.get();raw->openResult=NativeTransferCode::AccessDenied;
        NativeAudioBackend backend(std::move(t),std::make_unique<Clock>());
        Require(backend.Start(48000,256,1)==NativeTransferCode::AccessDenied && !backend.Generation(),"failed session never publishes generation");
        Require(backend.Health().systemError==5,"startup preserves native error");
        Require(backend.Start(48000,256,2)==NativeTransferCode::Failed && raw->opens==1,"failed Start remains single-use");backend.Stop();backend.Stop();
        Require(raw->closes==1,"idempotent Stop closes failed owner once");
    }
    {
        auto t=std::make_unique<Transport>();auto* raw=t.get();raw->blockOpen=true;
        NativeAudioBackend backend(std::move(t),std::make_unique<Clock>());
        NativeTransferCode result=NativeTransferCode::Ok;
        std::thread starter([&]{result=backend.Start(48000,256,9);});
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(!raw->openEntered && std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
        Require(raw->openEntered,"pending Open entered");backend.RequestStop();starter.join();backend.Stop();
        Require(result==NativeTransferCode::Cancelled && !backend.Generation() && raw->closes==1,"cancel during startup joins before session release");
    }
    {
        auto t=std::make_unique<Transport>();auto* raw=t.get();raw->accept=true;
        auto clock=std::make_unique<Clock>();auto* c=clock.get();NativeAudioBackend backend(std::move(t),std::move(clock));
        Require(backend.Start(48000,256,1)==NativeTransferCode::Ok,"pacing fixture starts");c->AwaitWait();c->Tick(1000000000);
        Require(raw->writes==4 && raw->captureFrames==2048,"long suspension caps catch-up at four512-frame packets");
        Require(backend.Health().discardedPacingFrames==45952 && backend.Health().skippedDeadlines==199,"lost pacing credit and missed deadlines measured");
        c->Tick();Require(raw->captureFrames==2288,"next normal wake has fresh240 frames, no stale catch-up debt");backend.Stop();
    }
    {
        auto t=std::make_unique<Transport>();auto* raw=t.get();raw->malformed=true;raw->renderPackets=1;
        auto clock=std::make_unique<Clock>();auto* c=clock.get();NativeAudioBackend backend(std::move(t),std::move(clock));
        Require(backend.Start(48000,256,1)==NativeTransferCode::Ok,"malformed fixture starts");c->AwaitWait();
        {std::lock_guard<std::mutex> lock(c->mutex);c->now+=5000000;c->changed.notify_all();}
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(backend.Health().state!=NativeAudioState::Failed && std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
        Require(backend.Health().error==NativeTransferCode::InvalidPacket && raw->writes==0,"wrong generation retires duplex before capture transfer");backend.Stop();
    }
    for(const auto rate:{0u,7999u,384001u}){
        auto t=std::make_unique<Transport>();auto* raw=t.get();NativeAudioBackend backend(std::move(t),std::make_unique<Clock>());
        Require(backend.Start(rate,256,1)==NativeTransferCode::InvalidPacket && raw->opens==0,"bad graph bounds rejected before opening transport");
    }
}
void IndependentClockDrift() {
    for(const int ppm:{-1000,1000}) {
        auto transport=std::make_unique<Transport>();auto* t=transport.get();t->accept=true;
        auto clock=std::make_unique<Clock>();auto* c=clock.get();
        NativeAudioBackend backend(std::move(transport),std::move(clock));
        Require(backend.Start(48000,256,61)==NativeTransferCode::Ok,"drift fixture starts");c->AwaitWait();
        const auto g=backend.Generation();std::array<float,256> left{},right{},outLeft{},outRight{};
        left.fill(.25f);right.fill(-.25f);const float* input[]={left.data(),right.data()};float* output[]={outLeft.data(),outRight.data()};
        std::uint64_t phase=0;
        for(unsigned tick=0;tick<12000;++tick){
            phase+=static_cast<std::uint64_t>(240*(1000000+ppm));
            const auto count=static_cast<unsigned>(phase/1000000);phase%=1000000;
            Require(g->windowsCaptureDestination->WriteRenderBlock(input,count,2)==count,"drifting graph input never overruns");
            t->renderPackets=1;c->Tick();const auto read=g->windowsRenderSource->ReadCaptureBlock(output,count,2);
            if(tick>1000)Require(read==count,"primed drifting graph source never underruns");
        }
        const auto health=backend.Health();
        Require(std::abs(health.renderSourceRatio-1)<=.002001 && std::abs(health.captureDestinationRatio-1)<=.002001,"both independent PI corrections bounded");
        Require((ppm>0?health.renderSourceRatio>1:health.renderSourceRatio<1) &&
            (ppm>0?health.captureDestinationRatio<1:health.captureDestinationRatio>1),"opposite SRC ratios correct independent clock mismatch");
        const auto target=g->windowsRenderSource->Format().capacityFrames*.5;
        Require(std::abs(g->windowsRenderSource->BufferedFrames()-target)<700 &&
            std::abs(g->windowsCaptureDestination->BufferedFrames()-target)<700,"60-second drift remains near target rather than accumulating2880frames");
        backend.Stop();
    }
}
void PreparationBounds() {
    struct Fixture {unsigned rate,quantum;std::uint64_t epoch;bool accepted;};
    for(const auto f:{Fixture{8000,16,1,true},Fixture{192000,16,2,true},Fixture{48000,8192,3,true},
        Fixture{48000,15,1,false},Fixture{48000,8193,1,false},Fixture{48000,256,0,false},
        Fixture{384000,16,1,false}}){
        auto transport=std::make_unique<Transport>();auto* t=transport.get();
        auto clock=std::make_unique<Clock>();auto* c=clock.get();NativeAudioBackend backend(std::move(transport),std::move(clock));
        const auto result=backend.Start(f.rate,f.quantum,f.epoch);
        Require((result==NativeTransferCode::Ok)==f.accepted,"graph quantum/rate/epoch and full-burst reserve bounds enforced");
        if(f.accepted){c->AwaitWait();t->accept=true;t->renderPackets=1;c->Tick();
            Require(backend.Health().state==NativeAudioState::Running,"bounded extreme supported conversion pump succeeds");}
        else Require(t->opens==0 && !backend.Generation(),"impossible reserve rejected before native session acquisition");
        backend.Stop();
    }
}
void RenderSequenceExhaustion() {
    auto transport=std::make_unique<Transport>();auto* t=transport.get();t->accept=true;
    t->sequence=~std::uint64_t{0};t->renderPackets=1;
    auto clock=std::make_unique<Clock>();auto* c=clock.get();
    NativeAudioBackend backend(std::move(transport),std::move(clock));
    Require(backend.Start(48000,256,81)==NativeTransferCode::Ok,"sequence exhaustion session starts");
    c->AwaitWait();c->Tick();
    const auto generation=backend.Generation();
    Require(backend.Health().state==NativeAudioState::Running && backend.Health().renderNativeFrames==240 &&
        generation->windowsRenderSource->BufferedFrames()==240,"UINT64_MAX render sequence must be accepted before exhaustion");
    Require(t->sequence==0,"test fixture must wrap its unsigned wire sequence");
    const auto writes=t->writes.load();t->renderPackets=1;
    // A rejected packet terminates Pump instead of reaching its next WaitUntil.
    // Signal time directly, then await the published terminal state, not a wait.
    {std::lock_guard<std::mutex> lock(c->mutex);c->now+=5000000;c->changed.notify_all();}
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(backend.Health().state!=NativeAudioState::Failed && std::chrono::steady_clock::now()<deadline)
        std::this_thread::yield();
    const auto failed=backend.Health();
    Require(failed.state==NativeAudioState::Failed && failed.error==NativeTransferCode::InvalidPacket,
        "render sequence zero after UINT64_MAX must retire the duplex session");
    Require(failed.renderNativeFrames==240 && generation->windowsRenderSource->BufferedFrames()==240 && t->writes==writes,
        "wrapped packet reached graph IO or allowed another capture transfer");
    backend.Stop();Require(t->closes==1,"exhausted sequence session did not close exactly once");
}
void RepeatedWireGenerationKeepsDistinctEpochs() {
    std::shared_ptr<NativeAudioGeneration> first;
    {
        auto transport=std::make_unique<Transport>();auto* t=transport.get();t->accept=true;
        t->sequence=~std::uint64_t{0};t->renderPackets=1;
        auto clock=std::make_unique<Clock>();auto* c=clock.get();
        NativeAudioBackend backend(std::move(transport),std::move(clock));
        Require(backend.Start(48000,256,91)==NativeTransferCode::Ok,"first lifetime epoch starts");
        c->AwaitWait();c->Tick();first=backend.Generation();backend.Stop();
        Require(t->opens==1&&t->closes==1,"first epoch owner lifecycle");
    }
    std::weak_ptr<NativeAudioGeneration> retired=first;
    auto transport=std::make_unique<Transport>();auto* t=transport.get();t->accept=true;t->renderPackets=1;
    auto clock=std::make_unique<Clock>();auto* c=clock.get();
    NativeAudioBackend backend(std::move(transport),std::move(clock));
    Require(backend.Start(48000,256,92)==NativeTransferCode::Ok,"replacement epoch starts with repeated wire generation");
    c->AwaitWait();const auto second=backend.Generation();
    Require(first->wireGeneration==second->wireGeneration && first->epoch==91 && second->epoch==92 &&
        first->windowsRenderSource!=second->windowsRenderSource && first->windowsCaptureDestination!=second->windowsCaptureDestination,
        "repeated device generation aliased retired engine IO");
    Require(first->windowsRenderSource->Format().generationId==91 && first->windowsCaptureDestination->Format().generationId==91 &&
        second->windowsRenderSource->Format().generationId==92 && second->windowsCaptureDestination->Format().generationId==92,
        "duplex IO identities must carry lifetime epoch, not wire generation");
    Require(first->windowsRenderSource->BufferedFrames()==240 && second->windowsRenderSource->BufferedFrames()==0 &&
        second->windowsCaptureDestination->BufferedFrames()==0,"new epoch inherited old buffered PCM");
    c->Tick();
    Require(backend.Health().state==NativeAudioState::Running && backend.Health().renderNativeFrames==240 &&
        first->windowsRenderSource->BufferedFrames()==240 && second->windowsRenderSource->BufferedFrames()==240,
        "new epoch sequence zero inherited old UINT64_MAX sequence state or mutated retired IO");
    first.reset();Require(retired.expired(),"replacement backend retained retired generation");
    backend.Stop();Require(t->opens==1&&t->closes==1,"replacement epoch owner lifecycle");
}
}
int main(){OneDuplexSessionAndBoundedBackpressure();DuplexPcmAtIndependentGraphRates();FailuresCancelAndPacing();
    IndependentClockDrift();PreparationBounds();RenderSequenceExhaustion();RepeatedWireGenerationKeepsDistinctEpochs();
    Require(pumpAllocations==0,"worker pumping performs no heap allocation");std::cout << "Native backend tests passed\n";}
