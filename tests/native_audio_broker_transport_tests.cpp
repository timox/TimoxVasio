#include "native_audio_broker_transport.h"
#include "broker_session.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace timox_broker;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void Put(void* memory,std::size_t offset,std::uint64_t value,unsigned bytes){auto* p=static_cast<unsigned char*>(memory);for(unsigned i=0;i<bytes;++i)p[offset+i]=static_cast<unsigned char>(value>>(8*i));}
std::uint64_t Read(const void* p,std::size_t offset,unsigned bytes){return timox_native::detail::ReadLittleEndian(static_cast<const unsigned char*>(p),offset,bytes);}
std::vector<unsigned char> Packet(bool render,std::uint64_t seq=0){
    std::vector<unsigned char> p(80);Put(p.data(),0,0x41575654,4);Put(p.data(),4,64,4);Put(p.data(),8,1,2);Put(p.data(),12,render?1:2,2);Put(p.data(),14,render?1:2,2);
    Put(p.data(),16,7,8);Put(p.data(),24,seq,8);Put(p.data(),40,2,4);Put(p.data(),44,48000,4);Put(p.data(),48,2,2);Put(p.data(),50,1,2);Put(p.data(),52,16,4);
    Put(p.data(),64,0x12345678,4);Put(p.data(),68,0x87654321,4);return p;
}
NativePeerIdentity Peer(){return {"S-1-5-21-100-200-300-1001","S-1-5-5-1-2",123,2};}
struct NativeState {unsigned opens=0,closes=0,owners=0,writes=0,reads=0;NativeTransferCode openCode=NativeTransferCode::Ok,writeCode=NativeTransferCode::Ok,readCode=NativeTransferCode::Ok;std::vector<unsigned char> lastWrite;std::uint64_t sequence=0;};
class Native final:public INativeAudioTransport {
    std::shared_ptr<NativeState> s;bool owned=false;
public:
    explicit Native(std::shared_ptr<NativeState> state):s(std::move(state)){}
    NativeTransferResult Open(NativeSessionInfo& out)noexcept override{++s->opens;if(s->openCode!=NativeTransferCode::Ok)return {s->openCode,0,5};owned=true;++s->owners;out.wireGeneration=7;return {NativeTransferCode::Ok};}
    NativeTransferResult ReadRender(std::uint32_t,void* out,std::uint32_t capacity)noexcept override{
        ++s->reads;if(s->readCode!=NativeTransferCode::Ok)return {s->readCode,0,5};
        if(capacity<80)return {NativeTransferCode::Failed};
        try{const auto p=Packet(true,s->sequence++);std::memcpy(out,p.data(),p.size());return {NativeTransferCode::Ok,80};}catch(...){return {NativeTransferCode::Failed};}
    }
    NativeTransferResult WriteCapture(const void* p,std::uint32_t bytes)noexcept override{++s->writes;try{s->lastWrite.assign(static_cast<const unsigned char*>(p),static_cast<const unsigned char*>(p)+bytes);}catch(...){return {NativeTransferCode::Failed};}return {s->writeCode};}
    NativeTransferResult ReadStatus(std::uint64_t (&c)[3])noexcept override{c[0]=11;c[1]=22;c[2]=33;return {NativeTransferCode::Ok};}
    void RequestCancel()noexcept override{}
    void Close()noexcept override{++s->closes;if(owned){owned=false;--s->owners;}}
};
struct Hub {
    std::shared_ptr<NativeState> native=std::make_shared<NativeState>();
    BrokerSession broker{[this]{return std::make_unique<Native>(native);}};
    Hub(){NativeSessionPolicySnapshot p;p.allowedUserSid=Peer().userSid;p.consoleSessionId=2;p.interactiveLogonCount=1;p.unlocked=true;p.audioIsolationQualified=true;p.optionalConsoleIdentity=Peer();broker.UpdatePolicy(p);}
};
enum class Corruption { None, RequestId, Lease, Opcode, Oversized, Truncated, Generation, Reserved, ErrorStatus,
    Protected,ProtocolMismatch,UnexpectedFull,SequenceMax,SequenceZero };
struct ChannelState {
    std::mutex mutex;std::condition_variable changed;bool cancelled=false,entered=false;
    unsigned pause=0,connects=0,confirms=0,exchanges=0,closes=0,cancels=0;
    NativeChannelCode connectCode=NativeChannelCode::Ok,confirmCode=NativeChannelCode::Ok;
    Corruption corruption=Corruption::None;bool confirmed=false;std::uint64_t lease=0,generation=0;
    std::vector<std::uint64_t> requestIds;bool invalidDeadline=false;
};
class Channel final:public INativeAudioAuthenticatedChannel {
    std::shared_ptr<Hub> hub;std::shared_ptr<ChannelState> s;std::atomic<ConnectionId> connection{0};
    bool Pause(unsigned operation,Deadline deadline)noexcept {
        std::unique_lock<std::mutex> lock(s->mutex);
        if(deadline<=std::chrono::steady_clock::now())s->invalidDeadline=true;
        if(s->pause==operation){s->entered=true;s->changed.notify_all();s->changed.wait_until(lock,deadline,[&]{return s->cancelled;});}
        return s->cancelled || std::chrono::steady_clock::now()>=deadline;
    }
public:
    Channel(std::shared_ptr<Hub> h,std::shared_ptr<ChannelState> state):hub(std::move(h)),s(std::move(state)){}
    NativeChannelResult Connect(Deadline deadline)noexcept override{
        ++s->connects;if(Pause(1,deadline))return {NativeChannelCode::Cancelled};
        if(s->connectCode!=NativeChannelCode::Ok)return {s->connectCode,0,123};
        const auto attached=hub->broker.AttachAuthenticated(Peer());connection=attached.id;return {attached.id?NativeChannelCode::Ok:NativeChannelCode::Failed};
    }
    NativeChannelResult ConfirmLease(std::uint64_t lease,std::uint64_t generation,Deadline deadline)noexcept override{
        ++s->confirms;if(Pause(2,deadline))return {NativeChannelCode::Cancelled};
        if(s->confirmCode!=NativeChannelCode::Ok)return {s->confirmCode,0,456};
        if(lease!=s->lease||generation!=s->generation)return {NativeChannelCode::AccessDenied};s->confirmed=true;return {NativeChannelCode::Ok};
    }
    NativeChannelResult Exchange(const void* request,std::size_t bytes,void* output,std::size_t capacity,Deadline deadline)noexcept override{
        ++s->exchanges;if(Pause(3,deadline))return {NativeChannelCode::Cancelled};
        const auto op=Read(request,6,2);if(op!=1&&op!=5&&!s->confirmed)return {NativeChannelCode::AccessDenied};
        try{s->requestIds.push_back(Read(request,16,8));}catch(...){return {NativeChannelCode::Failed};}
        const auto r=hub->broker.Process(connection,request,bytes,output,capacity);
        if(r.disposition!=Dispatch::Reply)return {NativeChannelCode::Removed};
        auto result=NativeChannelResult{NativeChannelCode::Ok,r.bytes};
        if(op==1&&r.status==Status::Ok){s->lease=Read(output,24,8);s->generation=Read(output,48,8);}
        switch(s->corruption){
        case Corruption::RequestId:Put(output,16,Read(output,16,8)+1,8);break;
        case Corruption::Lease:Put(output,24,Read(output,24,8)+1,8);break;
        case Corruption::Opcode:Put(output,6,0x8005,2);break;
        case Corruption::Oversized:result.bytes=static_cast<std::uint32_t>(MaxMessageBytes+1);break;
        case Corruption::Truncated:result.bytes=47;break;
        case Corruption::Generation:if(op==2)Put(output,64,8,8);break;
        case Corruption::Reserved:Put(output,36,1,4);break;
        case Corruption::ErrorStatus:Put(output,32,999,4);break;
        case Corruption::Protected:case Corruption::ProtocolMismatch:case Corruption::UnexpectedFull:
            Put(output,32,static_cast<unsigned>(s->corruption==Corruption::Protected?Status::Protected:
                s->corruption==Corruption::ProtocolMismatch?Status::ProtocolMismatch:Status::Full),4);
            Put(output,12,4,4);Put(output,48,5,4);result.bytes=52;break;
        case Corruption::SequenceMax:Put(output,72,~std::uint64_t{0},8);break;
        case Corruption::SequenceZero:Put(output,72,0,8);break;
        default:break;
        }
        return result;
    }
    void RequestCancel()noexcept override{{std::lock_guard<std::mutex> lock(s->mutex);++s->cancels;s->cancelled=true;s->changed.notify_all();}hub->broker.RequestCancel(connection);}
    void Close()noexcept override{++s->closes;hub->broker.Disconnect(connection.exchange(0));s->confirmed=false;}
};
std::unique_ptr<INativeAudioTransport> Client(const std::shared_ptr<Hub>& hub,const std::shared_ptr<ChannelState>& state){return CreateBrokerNativeAudioTransport(std::make_unique<Channel>(hub,state));}
void DuplexThroughRealBroker(){
    auto hub=std::make_shared<Hub>();auto state=std::make_shared<ChannelState>();auto client=Client(hub,state);NativeSessionInfo session;
    Check(client->Open(session).code==NativeTransferCode::Ok&&session.wireGeneration==7&&state->confirmed,"client failed authenticated broker SESSION/lease confirmation");
    std::array<unsigned char,32832> packet{};auto r=client->ReadRender(2,packet.data(),80);
    Check(r.code==NativeTransferCode::Ok&&r.bytes==80&&std::memcmp(packet.data(),Packet(true).data(),80)==0,"real broker render PCM mismatch");
    const auto capture=Packet(false,9);hub->native->writeCode=NativeTransferCode::Full;
    Check(client->WriteCapture(capture.data(),80).code==NativeTransferCode::Full,"Full not mapped");hub->native->writeCode=NativeTransferCode::Ok;
    Check(client->WriteCapture(capture.data(),80).code==NativeTransferCode::Ok&&hub->native->lastWrite==capture,"Full retry changed packet");
    Check(state->requestIds.size()==4&&state->requestIds[3]>state->requestIds[2],"Full retried old request id");
    std::uint64_t counters[3]{};Check(client->ReadStatus(counters).code==NativeTransferCode::Ok&&counters[0]==11&&counters[1]==22&&counters[2]==33,"status counters mismatch");
    auto second=Client(hub,std::make_shared<ChannelState>());Check(second->Open(session).code==NativeTransferCode::OwnedElsewhere&&hub->native->owners==1,"second client stole broker ownership");
    const auto lease=state->lease;client->Close();Check(hub->native->owners==0,"graceful close retained owner");
    Check(client->Open(session).code==NativeTransferCode::Ok&&state->lease>lease,"noncancelled reopen reused old lease");
    client.reset();Check(hub->native->owners==0&&state->cancels>0&&!state->invalidDeadline,"destructor did not cancel-first/release owner");
}
void CorruptResponsesNeverEscape(){
    for(auto corruption:{Corruption::RequestId,Corruption::Lease,Corruption::Opcode,Corruption::Oversized,Corruption::Truncated,Corruption::Generation,Corruption::Reserved,Corruption::ErrorStatus}){
        auto hub=std::make_shared<Hub>();auto state=std::make_shared<ChannelState>();auto client=Client(hub,state);NativeSessionInfo info;
        Check(client->Open(info).code==NativeTransferCode::Ok,"corruption setup");state->corruption=corruption;
        std::array<unsigned char,80> packet;packet.fill(0xff);
        const auto r=client->ReadRender(2,packet.data(),80);
        Check(r.code==NativeTransferCode::InvalidPacket&&r.bytes==0&&hub->native->owners==0,"bad authenticated response was accepted or retained lease");
        for(auto value:packet)Check(value==0,"unvalidated PCM escaped scratch");
    }
}
void DenialAndDefault(){
    auto absent=CreateBrokerNativeAudioTransport();NativeSessionInfo info{99};
    Check(absent->Open(info).code==NativeTransferCode::Unavailable&&info.wireGeneration==0,"missing channel selected fallback");
    auto hub=std::make_shared<Hub>();auto state=std::make_shared<ChannelState>();state->connectCode=NativeChannelCode::AccessDenied;auto client=Client(hub,state);
    auto r=client->Open(info);Check(r.code==NativeTransferCode::AccessDenied&&r.systemError==123&&hub->native->opens==0,"Connect denial lost diagnostic or opened broker");
    state->connectCode=NativeChannelCode::Ok;state->confirmCode=NativeChannelCode::AccessDenied;
    r=client->Open(info);Check(r.code==NativeTransferCode::AccessDenied&&r.systemError==456&&hub->native->owners==0,"lease binding failure retained owner");
    state->confirmCode=NativeChannelCode::Ok;hub->native->openCode=NativeTransferCode::AccessDenied;
    r=client->Open(info);Check(r.code==NativeTransferCode::AccessDenied&&r.systemError==5,"broker Open denial lost systemError");
}
void CancelEveryPhase(){
    for(unsigned phase:{1u,2u,3u}){
        auto hub=std::make_shared<Hub>();auto state=std::make_shared<ChannelState>();auto client=Client(hub,state);NativeSessionInfo info;
        if(phase==3)Check(client->Open(info).code==NativeTransferCode::Ok,"cancel Exchange setup");state->pause=phase;
        NativeTransferResult result;std::thread worker([&]{if(phase==3){std::array<unsigned char,80> p{};result=client->ReadRender(2,p.data(),80);}else result=client->Open(info);});
        bool entered=false;{std::unique_lock<std::mutex> lock(state->mutex);entered=state->changed.wait_for(lock,std::chrono::seconds(2),[&]{return state->entered;});}
        client->RequestCancel();worker.join();Check(entered&&result.code==NativeTransferCode::Cancelled&&hub->native->owners==0,"cancellation did not complete/retire owner");
        const auto connects=state->connects;Check(client->Open(info).code==NativeTransferCode::Cancelled&&state->connects==connects,"sticky cancellation reopened connection");
    }
}
void BoundsSequencesAndDomain(){
    auto hub=std::make_shared<Hub>();auto state=std::make_shared<ChannelState>();auto client=Client(hub,state);NativeSessionInfo info;
    Check(client->Open(info).code==NativeTransferCode::Ok,"bounds setup");
    std::array<unsigned char,32832> packet{};const auto exchanges=state->exchanges;
    for(const unsigned frames:{0u,4097u,0xffffffffu})Check(client->ReadRender(frames,packet.data(),80).code==NativeTransferCode::InvalidPacket,"invalid frame bound reached channel");
    Check(client->ReadRender(2,packet.data(),79).code==NativeTransferCode::InvalidPacket&&state->exchanges==exchanges,"short native output reached channel");
    const auto wrongDirection=Packet(true);Check(client->WriteCapture(wrongDirection.data(),80).code==NativeTransferCode::InvalidPacket&&state->exchanges==exchanges,"wrong N0 direction reached channel");
    hub->native->readCode=NativeTransferCode::Empty;Check(client->ReadRender(2,packet.data(),80).code==NativeTransferCode::Empty&&hub->native->owners==1,"Empty closed valid lease");hub->native->readCode=NativeTransferCode::Ok;
    state->corruption=Corruption::SequenceMax;Check(client->ReadRender(2,packet.data(),80).code==NativeTransferCode::Ok,"MAX render sequence refused");
    state->corruption=Corruption::SequenceZero;Check(client->ReadRender(2,packet.data(),80).code==NativeTransferCode::InvalidPacket&&hub->native->owners==0,"render sequence wrapped");
    for(const auto corruption:{Corruption::Protected,Corruption::ProtocolMismatch,Corruption::UnexpectedFull}){
        auto h=std::make_shared<Hub>();auto s=std::make_shared<ChannelState>();auto c=Client(h,s);Check(c->Open(info).code==NativeTransferCode::Ok,"domain setup");s->corruption=corruption;
        auto result=c->ReadRender(2,packet.data(),80);
        Check(result.code==(corruption==Corruption::Protected?NativeTransferCode::AccessDenied:NativeTransferCode::InvalidPacket)&&h->native->owners==0,"broker status domain incorrectly mapped");
        if(corruption!=Corruption::UnexpectedFull)Check(result.systemError==5,"explicit domain error lost diagnostic");
    }
}
}
int main(){try{DuplexThroughRealBroker();CorruptResponsesNeverEscape();DenialAndDefault();CancelEveryPhase();BoundsSequencesAndDomain();std::cout<<"Broker client tests passed (in-process channel, real BrokerSession, fake native transport)\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
