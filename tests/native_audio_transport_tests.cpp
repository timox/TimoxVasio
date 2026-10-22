#include "native_audio_transport_win32.h"
#include "native_audio_control_protocol.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Put(unsigned char* p, unsigned offset, std::uint64_t value, unsigned width) {
    for (unsigned i=0;i<width;++i) p[offset+i]=static_cast<unsigned char>(value>>(i*8));
}
std::vector<unsigned char> Packet(bool capture, std::uint64_t generation=7, std::uint64_t sequence=1) {
    std::vector<unsigned char> p(80, 0);
    Put(p.data(),0,0x41575654,4);Put(p.data(),4,64,4);Put(p.data(),8,1,2);
    Put(p.data(),12,capture?2:1,2);Put(p.data(),14,capture?2:1,2);
    Put(p.data(),16,generation,8);Put(p.data(),24,sequence,8);
    Put(p.data(),40,2,4);Put(p.data(),44,48000,4);Put(p.data(),48,2,2);
    Put(p.data(),50,1,2);Put(p.data(),52,16,4);
    p[64]=0x81;p[79]=0x42;return p;
}
class Fake final : public INativeAudioWin32 {
public:
    DWORD openError=0,ioError=0,sessionBytes=8,statusBytes=24;
    std::uint64_t generation=7;
    unsigned opens=0,closes=0,begins=0,cancels=0,finishes=0;
    bool pending=false,finishSuccess=false,failWait=false,badLifetime=false,signalCompletion=false;
    DWORD finishError=ERROR_OPERATION_ABORTED;
    std::atomic<bool> issued{false},finishEntered{false},openEntered{false};
    HANDLE finishGate=nullptr,openGate=nullptr;
    DWORD pendingBytes=0,lastInputBytes=0,lastOutputBytes=0;
    std::array<unsigned char,16> lastInput{};
    bool outstanding=false;
    DWORD lastCode=0;
    std::vector<unsigned char> render=Packet(false),written;
    HANDLE OpenDevice(DWORD& error) noexcept override {
        ++opens;openEntered=true;
        if(openGate)WaitForSingleObject(openGate,5000);
        error=openError;return error?INVALID_HANDLE_VALUE:reinterpret_cast<HANDLE>(1);
    }
    HANDLE CreateManualEvent(DWORD& error) noexcept override {
        auto h=CreateEventW(nullptr,TRUE,FALSE,nullptr);error=h?0:GetLastError();return h;
    }
    bool Reset(HANDLE h,DWORD& error) noexcept override {error=0;return ResetEvent(h)!=0;}
    void Signal(HANDLE h) noexcept override {SetEvent(h);}
    void Close(HANDLE h) noexcept override {
        if(outstanding)badLifetime=true;
        if(h==reinterpret_cast<HANDLE>(1))++closes;else CloseHandle(h);
    }
    bool Begin(HANDLE,DWORD code,const void* in,DWORD n,void* out,DWORD capacity,
        OVERLAPPED& operation,DWORD& bytes,DWORD& error) noexcept override {
        ++begins;lastCode=code;bytes=0;error=ioError;
        lastInputBytes=n;lastOutputBytes=capacity;lastInput.fill(0);
        if(in)std::memcpy(lastInput.data(),in,(std::min)(static_cast<std::size_t>(n),lastInput.size()));
        if(outstanding)badLifetime=true;
        if(error)return false;
        if(code==TIMOX_IOCTL_SESSION) {
            if(out && capacity>=8)Put(static_cast<unsigned char*>(out),0,generation,8);
            bytes=sessionBytes;return true;
        }
        if(code==TIMOX_IOCTL_WRITE_CAPTURE) written.assign(static_cast<const unsigned char*>(in),static_cast<const unsigned char*>(in)+n);
        if(code==TIMOX_IOCTL_READ_RENDER) {
            bytes=static_cast<DWORD>(render.size());
            if(out)std::memcpy(out,render.data(),(std::min)(render.size(),static_cast<std::size_t>(capacity)));
        }
        if(code==TIMOX_IOCTL_STATUS) {bytes=statusBytes;for(unsigned i=0;i<3;++i)Put(static_cast<unsigned char*>(out),i*8,10+i,8);}
        if(pending){pendingBytes=bytes;outstanding=true;issued=true;error=ERROR_IO_PENDING;
            if(signalCompletion)SetEvent(operation.hEvent);return false;}
        return true;
    }
    NativeIoWait Wait(HANDLE stop,HANDLE complete,DWORD& error) noexcept override {
        if(failWait){error=ERROR_INVALID_HANDLE;return NativeIoWait::Failed;}
        HANDLE events[]={stop,complete};auto result=WaitForMultipleObjects(2,events,FALSE,5000);
        if(result==WAIT_OBJECT_0)return NativeIoWait::Cancel;
        if(result==WAIT_OBJECT_0+1)return NativeIoWait::Complete;
        error=ERROR_TIMEOUT;return NativeIoWait::Failed;
    }
    bool Cancel(HANDLE,OVERLAPPED&,DWORD& error) noexcept override {++cancels;error=ERROR_NOT_FOUND;return false;}
    bool Finish(HANDLE,OVERLAPPED&,bool wait,DWORD& bytes,DWORD& error) noexcept override {
        ++finishes;finishEntered=true;
        if(finishGate)WaitForSingleObject(finishGate,5000);
        if(!wait)badLifetime=true;outstanding=false;bytes=finishSuccess?pendingBytes:0;error=finishError;return finishSuccess;
    }
};
auto Make(const std::shared_ptr<Fake>& f) {return CreateDirectNativeAudioLabTransport(f);}
void SessionAndPackets() {
    auto f=std::make_shared<Fake>();auto t=Make(f);NativeSessionInfo info;
    Require(t->Open(info).code==NativeTransferCode::Ok && info.wireGeneration==7,"valid session must open");
    std::array<unsigned char,32832> out{};
    auto r=t->ReadRender(2,out.data(),static_cast<DWORD>(out.size()));
    Require(r.code==NativeTransferCode::Ok && r.bytes==80 && out[64]==0x81 && out[79]==0x42,"render PCM and exact byte count");
    std::array<unsigned char,16> request{};request[0]=7;request[8]=2;
    Require(f->lastInput==request && f->lastInputBytes==16 && f->lastOutputBytes==80,"read request wire layout and bounded output");
    Require(t->ReadRender(2,out.data(),static_cast<DWORD>(out.size())).code==NativeTransferCode::InvalidPacket,"duplicate render sequence");
    auto capture=Packet(true);
    Require(t->WriteCapture(capture.data(),static_cast<DWORD>(capture.size())).code==NativeTransferCode::Ok && f->written==capture,"capture wire preserved");
    auto calls=f->begins;
    Require(t->WriteCapture(capture.data(),static_cast<DWORD>(capture.size())).code==NativeTransferCode::InvalidPacket && f->begins==calls,"capture duplicate rejected before IO");
    std::uint64_t counts[3]{};
    Require(t->ReadStatus(counts).code==NativeTransferCode::Ok && counts[0]==10 && counts[2]==12,"status counters decoded");
    f->statusBytes=23;
    Require(t->ReadStatus(counts).code==NativeTransferCode::InvalidPacket && counts[0]==0 && counts[2]==0,"short status cannot expose partial counters");
    t->Close();f->generation=8;
    Require(t->Open(info).code==NativeTransferCode::Ok && info.wireGeneration==8,"reopen gets new wire generation");
    Require(t->WriteCapture(capture.data(),static_cast<DWORD>(capture.size())).code==NativeTransferCode::InvalidPacket,"old generation cannot enter new session");
    t->Close();Require(f->closes==2 && !f->badLifetime,"session handles released");
}
void InvalidResponsesAndBounds() {
    for(DWORD bytes:{0u,7u,9u}){
        auto f=std::make_shared<Fake>();f->sessionBytes=bytes;auto t=Make(f);NativeSessionInfo s{99};
        Require(t->Open(s).code==NativeTransferCode::InvalidPacket && !s.wireGeneration && f->closes==1,"bad handshake closes handle and clears output");
    }
    {auto f=std::make_shared<Fake>();f->generation=0;auto t=Make(f);NativeSessionInfo s;
    Require(t->Open(s).code==NativeTransferCode::InvalidPacket,"zero generation rejected");}
    for(unsigned scenario=0;scenario<6;++scenario){
        auto f=std::make_shared<Fake>();auto t=Make(f);NativeSessionInfo s;Require(t->Open(s).code==NativeTransferCode::Ok,"fixture open");
        if(scenario==0)f->render.resize(63);
        if(scenario==1)f->render[14]=2;
        if(scenario==2)f->render[16]=8;
        if(scenario==3)f->render[60]=1;
        if(scenario==4)f->render.push_back(0);
        if(scenario==5)Put(f->render.data(),40,4097,4);
        std::array<unsigned char,32832> out{};out.fill(0xcd);
        Require(t->ReadRender(4096,out.data(),static_cast<DWORD>(out.size())).code==NativeTransferCode::InvalidPacket && out[0]==0,"invalid reply not exposed as audio");
    }
    auto f=std::make_shared<Fake>();auto t=Make(f);NativeSessionInfo s;Require(t->Open(s).code==NativeTransferCode::Ok,"bounds fixture");
    unsigned char out[80]{};auto calls=f->begins;
    Require(t->ReadRender(0,out,80).code==NativeTransferCode::InvalidPacket &&
        t->ReadRender(4097,out,80).code==NativeTransferCode::InvalidPacket &&
        t->ReadRender(2,out,79).code==NativeTransferCode::InvalidPacket && f->begins==calls,"invalid requests do not reach driver");
}
void ErrorsAndRetry() {
    for(auto entry:{std::pair<DWORD,NativeTransferCode>{ERROR_FILE_NOT_FOUND,NativeTransferCode::Unavailable},
        {ERROR_ACCESS_DENIED,NativeTransferCode::AccessDenied},{ERROR_SHARING_VIOLATION,NativeTransferCode::OwnedElsewhere}}){
        auto f=std::make_shared<Fake>();f->openError=entry.first;auto t=Make(f);NativeSessionInfo s;
        Require(t->Open(s).code==entry.second && f->closes==0,"open error mapping");
    }
    auto f=std::make_shared<Fake>();auto t=Make(f);NativeSessionInfo s;Require(t->Open(s).code==NativeTransferCode::Ok,"error fixture");
    unsigned char out[80]{};f->ioError=ERROR_NO_MORE_ITEMS;
    Require(t->ReadRender(2,out,80).code==NativeTransferCode::Empty,"empty queue expected");
    auto capture=Packet(true);f->ioError=ERROR_BUSY;
    Require(t->WriteCapture(capture.data(),80).code==NativeTransferCode::Full,"full queue expected");
    f->ioError=0;Require(t->WriteCapture(capture.data(),80).code==NativeTransferCode::Ok,"unaccepted sequence can retry");
    f->ioError=ERROR_DEVICE_NOT_CONNECTED;
    Require(t->ReadRender(2,out,80).code==NativeTransferCode::Removed,"device removal distinct");
    for(DWORD removed:{ERROR_DEVICE_REMOVED,ERROR_NO_SUCH_DEVICE}) {
        f->ioError=removed;
        Require(t->ReadRender(2,out,80).code==NativeTransferCode::Removed,"PnP removed-device error distinct");
    }
    f->ioError=ERROR_ACCESS_DENIED;std::memset(out,0x7f,sizeof(out));
    auto denied=t->ReadRender(2,out,80);
    Require(denied.code==NativeTransferCode::AccessDenied && denied.bytes==0 && out[64]==0,
        "operation access denied (including protected audio) supplies no PCM");
}
void CancellationCompletion() {
    for(bool failedWait:{false,true})for(bool completedDuringCancel:{false,true}){
        auto f=std::make_shared<Fake>();auto t=Make(f);NativeSessionInfo s;Require(t->Open(s).code==NativeTransferCode::Ok,"cancel fixture");
        f->pending=true;f->failWait=failedWait;f->finishSuccess=completedDuringCancel;
        NativeTransferResult result;unsigned char out[80]{};
        std::thread worker([&]{result=t->ReadRender(2,out,80);});
        while(!f->issued.load())std::this_thread::yield();
        t->RequestCancel();worker.join();
        Require(result.code==(failedWait?NativeTransferCode::Failed:NativeTransferCode::Cancelled) &&
            f->cancels==1 && f->finishes==1 && !f->outstanding && !f->badLifetime,"cancel/wait failure observes final completion even if CancelIoEx not found");
        auto calls=f->begins;t->Close();
        Require(t->Open(s).code==NativeTransferCode::Cancelled && f->begins==calls,"cancellation sticky across reopen");
    }
    auto f=std::make_shared<Fake>();auto t=Make(f);t->RequestCancel();NativeSessionInfo s;
    Require(t->Open(s).code==NativeTransferCode::Cancelled && f->opens==0,"stop before open is not lost");
}
void PendingSuccessAndDelayedCancellation() {
    auto f=std::make_shared<Fake>();auto t=Make(f);NativeSessionInfo s;
    Require(t->Open(s).code==NativeTransferCode::Ok,"pending fixture open");
    f->pending=true;f->finishSuccess=true;f->signalCompletion=true;
    unsigned char out[80]{};
    Require(t->ReadRender(2,out,80).code==NativeTransferCode::Ok && out[64]==0x81 &&
        f->finishes==1 && f->cancels==0,"successful overlapped completion validates real reply");
    f->signalCompletion=false;f->finishSuccess=false;f->issued=false;f->finishEntered=false;
    f->finishGate=CreateEventW(nullptr,TRUE,FALSE,nullptr);Require(f->finishGate!=nullptr,"test completion gate");
    std::atomic<bool> returned{false};
    std::thread worker([&]{t->ReadRender(2,out,80);returned=true;});
    while(!f->issued.load())std::this_thread::yield();
    t->RequestCancel();
    while(!f->finishEntered.load())std::this_thread::yield();
    const bool premature=returned.load();
    SetEvent(f->finishGate);worker.join();CloseHandle(f->finishGate);f->finishGate=nullptr;
    Require(!premature && !f->badLifetime && !f->outstanding,"cancel returns only after completion releases borrowed memory");

    auto opening=std::make_shared<Fake>();auto ot=Make(opening);
    opening->openGate=CreateEventW(nullptr,TRUE,FALSE,nullptr);Require(opening->openGate!=nullptr,"test open gate");
    NativeTransferResult result;
    std::thread opener([&]{result=ot->Open(s);});
    while(!opening->openEntered.load())std::this_thread::yield();
    ot->RequestCancel();SetEvent(opening->openGate);opener.join();
    CloseHandle(opening->openGate);opening->openGate=nullptr;
    Require(result.code==NativeTransferCode::Cancelled && opening->closes==1 && opening->begins==0,
        "cancel racing synchronous CreateFile closes handle before handshake");
}
void SequenceExhaustionAndRepeatedGeneration() {
    auto f=std::make_shared<Fake>();auto t=Make(f);NativeSessionInfo s;
    Require(t->Open(s).code==NativeTransferCode::Ok,"sequence fixture");
    auto capture=Packet(true,7,~std::uint64_t{0});
    Require(t->WriteCapture(capture.data(),80).code==NativeTransferCode::Ok,"last capture sequence");
    capture=Packet(true,7,0);
    Require(t->WriteCapture(capture.data(),80).code==NativeTransferCode::InvalidPacket,"capture sequence wrap rejected");
    f->render=Packet(false,7,~std::uint64_t{0});unsigned char out[80]{};
    Require(t->ReadRender(2,out,80).code==NativeTransferCode::Ok,"last render sequence");
    f->render=Packet(false,7,0);
    Require(t->ReadRender(2,out,80).code==NativeTransferCode::InvalidPacket,"render sequence wrap rejected");
    t->Close();Require(t->Open(s).code==NativeTransferCode::Ok && s.wireGeneration==7,
        "driver replacement may repeat wire generation; local epoch belongs to backend");
    f->render=Packet(false);
    Require(t->ReadRender(2,out,80).code==NativeTransferCode::Ok,"new session has independent sequence state");
}
}
int main(){try{SessionAndPackets();InvalidResponsesAndBounds();ErrorsAndRetry();CancellationCompletion();
    PendingSuccessAndDelayedCancellation();SequenceExhaustionAndRepeatedGeneration();}
catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
std::puts("Native audio transport tests passed");}
