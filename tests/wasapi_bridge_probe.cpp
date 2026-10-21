// Opt-in real Windows render/loopback smoke probe. Emits a -60 dBFS tone;
// stores only aggregate measurements, never captured audio. Not a latency test.
#include "wasapi_bridge.h"
#include "software_clock_host.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace {
constexpr unsigned rate=48000, frames=256;
constexpr double pi=3.14159265358979323846, frequency=997, amplitude=0.001;
struct Signal {
    WasapiBridgeIo& capture;
    WasapiBridgeIo& render;
    unsigned channels;
    std::vector<float> send, receive;
    std::vector<const float*> outputs;
    std::vector<float*> inputs;
    std::uint64_t cursor=0, measured=0, accepted=0, received=0;
    double sine=0, cosine=0, energy=0;
    Signal(WasapiBridgeIo& c,WasapiBridgeIo& r,unsigned n)
        :capture(c),render(r),channels(n),send(n*frames),receive(n*frames),outputs(n),inputs(n) {
        for(unsigned i=0;i<n;++i){outputs[i]=send.data()+i*frames;inputs[i]=receive.data()+i*frames;}
    }
    void Generate() noexcept {
        for(unsigned i=0;i<frames;++i){
            const float value=static_cast<float>(amplitude*std::sin(2*pi*frequency*(cursor+i)/rate));
            for(unsigned ch=0;ch<channels;++ch)send[ch*frames+i]=value;
        }
        accepted+=render.WriteRenderBlock(outputs.data(),frames,channels);
        cursor+=frames;
    }
    static void Callback(const float* const*,float* const*,std::uint32_t n,void* context) noexcept {
        auto& s=*static_cast<Signal*>(context);
        if(n!=frames)return;
        s.Generate();
        s.received+=s.capture.ReadCaptureBlock(s.inputs.data(),frames,s.channels);
        if(s.cursor<rate)return; // Discard startup, priming and initial scheduling.
        for(unsigned i=0;i<frames;++i){
            const double phase=2*pi*frequency*s.measured/rate;
            const double value=s.receive[i];
            s.sine+=value*std::sin(phase);s.cosine+=value*std::cos(phase);s.energy+=value*value;++s.measured;
        }
    }
};
struct Com { HRESULT result=CoInitializeEx(nullptr,COINIT_MULTITHREADED); ~Com(){if(SUCCEEDED(result))CoUninitialize();} };
}

int main(int argc,char** argv){
    if(argc!=2 || std::strcmp(argv[1],"--run-default-render-loopback")!=0){
        std::puts("Opt-in: --run-default-render-loopback plays a -60 dBFS 997 Hz tone for about 4 seconds. No settings changed.");
        return 2;
    }
    try {
        Com apartment;
        if(FAILED(apartment.result))return 1;
        WasapiDeviceEnumerator enumerator;
        std::wstring id;std::string error;
        if(!enumerator.resolve(WasapiDataFlow::Render,L"",WasapiDefaultRole::Multimedia,id,error)){
            std::fprintf(stderr,"Resolve: %s\n",error.c_str());return 1;
        }
        std::vector<WasapiDeviceInfo> devices;
        if(!enumerator.enumerate(devices,error))return 1;
        unsigned channels=0;
        for(const auto& device:devices)if(device.id==id && device.mixFormat){
            channels=device.mixFormat->channels;
            std::wprintf(L"Endpoint: %ls (%ls), native rate=%u channels=%u\n",device.name.c_str(),id.c_str(),device.mixFormat->sampleRate,channels);
        }
        if(!channels || channels>32)return 1;
        WasapiBridgeIo capture({WasapiBridgeDirection::Capture,channels,rate,frames,1,4096});
        WasapiBridgeIo render({WasapiBridgeDirection::Render,channels,rate,frames,2,4096});
        WasapiBridgeConfig in,out;
        in.endpoint.kind=WasapiStreamKind::EndpointLoopback;in.endpoint.endpointId=id;
        out.endpoint.kind=WasapiStreamKind::Render;out.endpoint.endpointId=id;
        WasapiBridge captureBridge(in,capture),renderBridge(out,render);
        Signal signal(capture,render,channels);
        for(unsigned i=0;i<8;++i)signal.Generate(); // Half-FIFO before starting render.
        HRESULT hr=captureBridge.Start();
        if(SUCCEEDED(hr))hr=renderBridge.Start();
        if(FAILED(hr)){std::printf("Bridge start failed: 0x%08lx\n",static_cast<unsigned long>(hr));return 1;}
        SoftwareClockHost clock;AudioClockCapabilities caps;
        if(!clock.open(L"software",nullptr,caps,error) || !clock.setSampleRate(rate,error) ||
           !clock.start(frames,{}, {},Signal::Callback,&signal,error)){
            std::fprintf(stderr,"Clock: %s\n",error.c_str());return 1;
        }
        std::this_thread::sleep_for(std::chrono::seconds(4));
        clock.stop();clock.close();
        captureBridge.Stop();renderBridge.Stop();
        const auto c=captureBridge.Health(),r=renderBridge.Health();
        const double detected=signal.measured ? 2*std::hypot(signal.sine,signal.cosine)/signal.measured:0;
        const double rms=signal.measured ? std::sqrt(signal.energy/signal.measured):0;
        std::printf("tone_amplitude=%.9f rms=%.9f captured_native=%llu rendered_native=%llu graph_received=%llu render_accepted=%llu capture_dropped=%llu render_zero_input=%llu capture_error=0x%08lx render_error=0x%08lx\n",
            detected,rms,c.capturedFrames,r.renderedFrames,signal.received,signal.accepted,c.droppedCaptureFrames,
            r.renderSilencedInputFrames,static_cast<unsigned long>(c.error),static_cast<unsigned long>(r.error));
        if(FAILED(c.error)||FAILED(r.error)||!c.capturedFrames||!r.renderedFrames)return 1;
        if(detected<0.00005){std::puts("INCONCLUSIVE: stream ran, tone not detected (volume/mute/routing or processing may matter).");return 3;}
        std::puts("PASS: real shared render and loopback carried the probe tone. Not a latency, CPU or long-duration qualification.");
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
