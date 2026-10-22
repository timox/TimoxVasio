#include "native_audio_lab_session.h"
#include "native_audio_lab_fake.h"
#include <charconv>
#include <iostream>
#include <string_view>
#include <thread>

namespace {
std::atomic<bool> interrupted{false};
BOOL WINAPI ConsoleStop(DWORD code){if(code==CTRL_C_EVENT||code==CTRL_BREAK_EVENT){interrupted=true;return TRUE;}return FALSE;}
const char* Name(NativeTransferCode code){
    switch(code){case NativeTransferCode::Ok:return "ok";case NativeTransferCode::Empty:return "empty";
    case NativeTransferCode::Full:return "full";case NativeTransferCode::Unavailable:return "unavailable";
    case NativeTransferCode::AccessDenied:return "access-denied";case NativeTransferCode::OwnedElsewhere:return "owned-elsewhere";
    case NativeTransferCode::Removed:return "removed";case NativeTransferCode::Cancelled:return "cancelled";
    case NativeTransferCode::InvalidPacket:return "invalid-packet";default:return "failed";}
}
void Help(){
    std::cout<<"NativeAudioLabProbe --help\n"
        "  --fake [--seconds 1..600]  In-process PCM source -> real worker/SRC/graph -> fake sink.\n"
        "  --native [--loopback-audio] [--seconds 1..600]  Explicit dedicated-target laboratory session.\n"
        "No arguments/help opens no device and starts no audio clock. Default duration: 2 seconds.\n"
        "Graph: software master 48000 Hz, quantum 256. Native mode has no routes unless --loopback-audio is given.\n"
        "The explicit loopback routes left at -6.0206 dB and right at unity from Windows playback to recording.\n"
        "Native mode needs the separately authorized N2 driver and permitted caller on a dedicated target.\n"
        "This executable never elevates, installs, changes default endpoints or invokes the permanent engine.\n"
        "Fake success is not loaded-driver, audible fidelity, latency or physical-hardware qualification.\n";
}
}
int main(int argc,char** argv){
    bool fake=false,native=false,loopback=false;unsigned seconds=2;
    if(argc==1){Help();return 0;}
    for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--help"){Help();return 0;}
    for(int i=1;i<argc;++i){const std::string_view argument=argv[i];
        if(argument=="--fake")fake=true;else if(argument=="--native")native=true;
        else if(argument=="--loopback-audio")loopback=true;
        else if(argument=="--seconds"&&i+1<argc){const std::string_view value=argv[++i];
            const auto result=std::from_chars(value.data(),value.data()+value.size(),seconds);
            if(result.ec!=std::errc{}||result.ptr!=value.data()+value.size()||seconds<1||seconds>600){std::cerr<<"Invalid duration\n";return 2;}
        }else{std::cerr<<"Unknown or incomplete option\n";return 2;}
    }
    if(fake==native || (fake&&loopback)){std::cerr<<"Choose exactly one mode; --loopback-audio belongs to --native\n";return 2;}
    try{
        NativeAudioLabSession session;std::string error;
        if(!session.StartClock(48000,256,error)){std::cerr<<error<<'\n';return 1;}
        if(fake||loopback){
            if(!session.SetRoutes({{"left","native:windows:input:1","native:windows:output:1",-6.020599913,false},
                {"right","native:windows:input:2","native:windows:output:2",0,false}},error)){std::cerr<<error<<'\n';return 1;}
        }
        std::shared_ptr<NativeAudioLabFakeState> state;
        std::unique_ptr<INativeAudioTransport> transport;
        if(fake){state=std::make_shared<NativeAudioLabFakeState>();transport=std::make_unique<NativeAudioLabFakeTransport>(state);}
        else transport=CreateDirectNativeAudioLabTransport();
        const auto opened=session.Attach(std::make_unique<NativeAudioBackend>(std::move(transport)),error);
        if(opened!=NativeTransferCode::Ok){std::cerr<<"Native session "<<Name(opened)<<", systemError="<<session.Health().systemError<<"; master remained independent\n";return 2;}
        const auto generation=session.Generation();
        SetConsoleCtrlHandler(ConsoleStop,TRUE);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);
        bool failed=false;
        while(!interrupted&&std::chrono::steady_clock::now()<deadline){
            if(session.PollFailure()){failed=true;break;}
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        const auto health=session.Health();const auto callbacks=session.Callbacks();session.Stop();
        SetConsoleCtrlHandler(ConsoleStop,FALSE);
        const bool expected=fake&&state->heardExpected&&!state->badPacket&&!state->wrongThread;
        const bool success=NativeAudioLabSnapshotCompleted(failed,callbacks,!fake||expected,health);
        std::cout<<"{\"mode\":\""<<(fake?"fake":"native-lab")<<"\",\"sessionCompleted\":"<<(success?"true":"false")
            <<",\"physicalQualified\":false,\"expectedFakePcm\":"<<(expected?"true":"false")
            <<",\"interrupted\":"<<(interrupted?"true":"false")<<",\"epoch\":\""<<generation->epoch
            <<"\",\"wireGeneration\":\""<<generation->wireGeneration<<"\",\"callbacks\":"<<callbacks
            <<",\"renderNativeFrames\":"<<health.renderNativeFrames<<",\"captureNativeFrames\":"<<health.captureNativeFrames
            <<",\"captureSilencedGraphFrames\":"<<health.captureSilencedGraphFrames
            <<",\"discardedPacingFrames\":"<<health.discardedPacingFrames<<",\"controlBacklogUpdates\":"<<health.controlBacklogUpdates
            <<",\"error\":\""<<Name(health.error)<<"\"}\n";
        return success?0:1;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
