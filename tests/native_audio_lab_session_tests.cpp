#include "native_audio_lab_session.h"
#include "native_audio_lab_fake.h"
#include <cstdio>
#include <stdexcept>
#include <thread>

namespace {
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class P>void Await(P predicate,const char* message){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!predicate()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(2));
    Check(predicate(),message);
}
void PublicationAndFailure(){
    NativeAudioLabSession session;std::string error;
    Check(session.StartClock(48000,256,error),"laboratory master start");
    Check(session.SetRoutes({{"left","native:windows:input:1","native:windows:output:1",-6.020599913,false},
        {"right","native:windows:input:2","native:windows:output:2",0,false}},error),"explicit desired routes");
    auto first=std::make_shared<NativeAudioLabFakeState>();
    Check(session.Attach(std::make_unique<NativeAudioBackend>(std::make_unique<NativeAudioLabFakeTransport>(first)),error)==NativeTransferCode::Ok,"attach fake duplex");
    Await([&]{return first->heardExpected.load();},"real worker/graph/SRC did not return expected stereo PCM");
    auto old=session.Generation();std::weak_ptr<NativeAudioGeneration> oldLifetime=old;const auto epoch=old->epoch;
    const auto wire=old->wireGeneration;old.reset();
    auto rejected=std::make_shared<NativeAudioLabFakeState>();
    Check(session.Attach(std::make_unique<NativeAudioBackend>(std::make_unique<NativeAudioLabFakeTransport>(rejected)),error)==NativeTransferCode::OwnedElsewhere&&rejected->opens==0,"second native owner admitted");
    const auto before=session.Callbacks();first->removed=true;
    Await([&]{return session.Health().state==NativeAudioState::Failed;},"removed transport did not fail visibly");
    Check(session.PollFailure()&&!session.Generation()&&oldLifetime.expired(),"failed generation not retired after callback quiescence");
    Await([&]{return session.Callbacks()>before+5;},"native removal stopped software master");
    Check(first->closes==1&&session.DesiredRoutes().size()==2,"retirement lost routes or native owner");
    auto second=std::make_shared<NativeAudioLabFakeState>();
    Check(session.Attach(std::make_unique<NativeAudioBackend>(std::make_unique<NativeAudioLabFakeTransport>(second)),error)==NativeTransferCode::Ok,"reopen generation");
    Check(session.Generation()->epoch>epoch&&session.Generation()->wireGeneration==wire,"repeated wire generation reused engine epoch");
    Await([&]{return second->heardExpected.load();},"desired routes not restored on replacement epoch");
    session.Detach();Check(second->closes==1&&!session.Generation(),"normal detach failed");
    const auto after=session.Callbacks();Await([&]{return session.Callbacks()>after+5;},"detach stopped master");
    session.Stop();const auto stopped=session.Callbacks();std::this_thread::sleep_for(std::chrono::milliseconds(20));
    Check(session.Callbacks()==stopped,"stop returned with active callback");
}
void UnavailableAndExplicitRoutes(){
    NativeAudioLabSession session;std::string error;Check(session.StartClock(44100,256,error),"fallback-free master");
    for(const auto code:{NativeTransferCode::Unavailable,NativeTransferCode::AccessDenied,NativeTransferCode::OwnedElsewhere}){
        auto state=std::make_shared<NativeAudioLabFakeState>();state->openCode=code;
        const auto before=session.Callbacks();
        Check(session.Attach(std::make_unique<NativeAudioBackend>(std::make_unique<NativeAudioLabFakeTransport>(state)),error)==code&&!session.Generation(),"typed native unavailability hidden");
        Await([&]{return session.Callbacks()>before+2;},"failed native start stopped clock");
    }
    auto state=std::make_shared<NativeAudioLabFakeState>();
    Check(session.Attach(std::make_unique<NativeAudioBackend>(std::make_unique<NativeAudioLabFakeTransport>(state)),error)==NativeTransferCode::Ok,"no-route session start");
    Await([&]{return state->captureFrames>12000;},"capture pacing absent");
    Check(!state->heardAny.load()&&session.DesiredRoutes().empty(),"automatic loopback route created");
    Check(!session.SetRoutes({{"bad","native:windows:input:3","native:windows:output:1",0,false}},error),"invalid route changed live graph");
    const auto epoch=session.Generation()->epoch;
    Check(session.SetRoutes({{"left","native:windows:input:1","native:windows:output:1",-6.020599913,false},
        {"right","native:windows:input:2","native:windows:output:2",0,false}},error),"route-only publication");
    Check(session.Generation()->epoch==epoch&&state->opens==1,"route publication reopened native owner");
    Await([&]{return state->heardExpected.load();},"route publication not audible in fake transport");
    session.Stop();Check(!state->badPacket.load()&&!state->wrongThread.load(),"fake transport ownership/packet invalid");
}
void TerminalSnapshot(){
    NativeAudioHealth health;health.state=NativeAudioState::Running;
    Check(NativeAudioLabSnapshotCompleted(false,1,true,health),"running successful snapshot rejected");
    health.state=NativeAudioState::Failed;health.error=NativeTransferCode::Removed;
    Check(!NativeAudioLabSnapshotCompleted(false,1,true,health),"failure between final poll and snapshot reported as success");
    health.state=NativeAudioState::Running;
    Check(!NativeAudioLabSnapshotCompleted(false,1,true,health),"error visible before state publication reported as success");
    health.error=NativeTransferCode::Ok;
    Check(!NativeAudioLabSnapshotCompleted(true,1,true,health)&&!NativeAudioLabSnapshotCompleted(false,0,true,health)&&
        !NativeAudioLabSnapshotCompleted(false,1,false,health),"snapshot bypassed observed failure/callback/fixture requirements");
}
}
int main(){try{TerminalSnapshot();PublicationAndFailure();UnavailableAndExplicitRoutes();std::puts("Native laboratory session tests passed (fake transport only)");return 0;}
catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
