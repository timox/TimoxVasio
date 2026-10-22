#include "broker_session.h"
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace timox_broker;
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void Put(void* buffer,std::size_t at,std::uint64_t value,unsigned bytes){auto* p=static_cast<unsigned char*>(buffer);for(unsigned i=0;i<bytes;++i)p[at+i]=static_cast<unsigned char>(value>>(8*i));}
std::uint64_t Read(const void* buffer,std::size_t at,unsigned bytes){return timox_native::detail::ReadLittleEndian(static_cast<const unsigned char*>(buffer),at,bytes);}
NativePeerIdentity Peer(){return {"S-1-5-21-100-200-300-1001","S-1-5-5-12-34",1234,2};}
NativeSessionPolicySnapshot Qualified(){NativeSessionPolicySnapshot p;p.allowedUserSid=Peer().userSid;p.consoleSessionId=2;p.interactiveLogonCount=1;p.unlocked=true;p.audioIsolationQualified=true;p.optionalConsoleIdentity=Peer();return p;}
std::vector<unsigned char> Packet(bool render,std::uint64_t sequence=0){
    std::vector<unsigned char> p(80);Put(p.data(),0,0x41575654,4);Put(p.data(),4,64,4);Put(p.data(),8,1,2);
    Put(p.data(),12,render?1:2,2);Put(p.data(),14,render?1:2,2);Put(p.data(),16,7,8);Put(p.data(),24,sequence,8);
    Put(p.data(),40,2,4);Put(p.data(),44,48000,4);Put(p.data(),48,2,2);Put(p.data(),50,1,2);Put(p.data(),52,16,4);
    Put(p.data(),64,0x12345678,4);Put(p.data(),68,0x87654321,4);return p;
}
std::vector<unsigned char> Request(Opcode op,std::uint64_t id,std::uint64_t lease=0,std::vector<unsigned char> payload={}){
    std::vector<unsigned char> b(48+payload.size());Put(b.data(),0,Magic,4);Put(b.data(),4,1,2);Put(b.data(),6,static_cast<unsigned>(op),2);
    Put(b.data(),8,48,4);Put(b.data(),12,payload.size(),4);Put(b.data(),16,id,8);Put(b.data(),24,lease,8);
    if(!payload.empty())std::memcpy(b.data()+48,payload.data(),payload.size());return b;
}
std::vector<unsigned char> GenerationPayload(bool read=false){std::vector<unsigned char> p(read?16:8);Put(p.data(),0,7,8);if(read)Put(p.data(),8,4096,4);return p;}
struct State {
    unsigned opens=0,closes=0,owners=0,maxOwners=0,writes=0,reads=0,cancels=0;
    NativeTransferCode openCode=NativeTransferCode::Ok,writeCode=NativeTransferCode::Ok,readCode=NativeTransferCode::Empty;
    std::vector<unsigned char> render=Packet(true),lastWrite;
    std::mutex mutex;std::condition_variable changed;bool blockOpen=false,blockRead=false,blockWrite=false,entered=false,cancelled=false;
};
class Transport final:public INativeAudioTransport {
    std::shared_ptr<State> s;bool owned=false;
public:
    explicit Transport(std::shared_ptr<State> value):s(std::move(value)){}
    NativeTransferResult Open(NativeSessionInfo& out)noexcept override{
        std::unique_lock<std::mutex> lock(s->mutex);++s->opens;s->entered=true;s->changed.notify_all();
        if(s->blockOpen)s->changed.wait(lock,[&]{return s->cancelled;});
        if(s->cancelled)return {NativeTransferCode::Cancelled};
        if(s->openCode!=NativeTransferCode::Ok)return {s->openCode,0,5};
        owned=true;++s->owners;if(s->owners>s->maxOwners)s->maxOwners=s->owners;out.wireGeneration=7;return {NativeTransferCode::Ok};
    }
    NativeTransferResult ReadRender(std::uint32_t,void* out,std::uint32_t cap)noexcept override{
        {std::unique_lock<std::mutex> lock(s->mutex);if(s->blockRead){s->entered=true;s->changed.notify_all();s->changed.wait(lock,[&]{return s->cancelled;});return {NativeTransferCode::Cancelled};}}
        ++s->reads;if(s->readCode!=NativeTransferCode::Ok)return {s->readCode,0,s->readCode==NativeTransferCode::AccessDenied?5u:0u};
        if(s->render.size()>cap)return {NativeTransferCode::Failed};std::memcpy(out,s->render.data(),s->render.size());return {NativeTransferCode::Ok,static_cast<std::uint32_t>(s->render.size())};
    }
    NativeTransferResult WriteCapture(const void* p,std::uint32_t n)noexcept override{
        {std::unique_lock<std::mutex> lock(s->mutex);if(s->blockWrite){s->entered=true;s->changed.notify_all();s->changed.wait(lock,[&]{return s->cancelled;});return {NativeTransferCode::Cancelled};}}
        ++s->writes;try{s->lastWrite.assign(static_cast<const unsigned char*>(p),static_cast<const unsigned char*>(p)+n);}catch(...){return {NativeTransferCode::Failed};}return {s->writeCode};
    }
    NativeTransferResult ReadStatus(std::uint64_t (&c)[3])noexcept override{c[0]=11;c[1]=22;c[2]=33;return {NativeTransferCode::Ok};}
    void RequestCancel()noexcept override{std::lock_guard<std::mutex> lock(s->mutex);++s->cancels;s->cancelled=true;s->changed.notify_all();}
    void Close()noexcept override{++s->closes;if(owned){--s->owners;owned=false;}}
};
struct Reply {DispatchResult result;std::array<unsigned char,MaxMessageBytes> bytes{};};
Reply Send(BrokerSession& broker,ConnectionId id,const std::vector<unsigned char>& request){Reply r;r.result=broker.Process(id,request.data(),request.size(),r.bytes.data(),r.bytes.size());return r;}
void ValidReply(const Reply& reply,Opcode op,std::uint64_t id,std::uint64_t lease,Status status){
    Check(reply.result.disposition==Dispatch::Reply && reply.result.status==status,"wrong dispatch/status");
    const auto result=ValidateMessage(reply.bytes.data(),reply.result.bytes,{true,op,id,0,lease,7,4096});
    Check(result.error==Error::None && result.message.status==status,"broker response violates wire contract");
}
ConnectionId Attach(BrokerSession& broker){const auto c=broker.AttachAuthenticated(Peer());Check(c.id && c.status==Status::Ok,"authenticated connection attach failed");return c.id;}
std::uint64_t Open(BrokerSession& broker,ConnectionId c,std::uint64_t request=1){auto r=Send(broker,c,Request(Opcode::Session,request));ValidReply(r,Opcode::Session,request,0,Status::Ok);Check(Read(r.bytes.data(),48,8)==7,"wrong wire generation");return Read(r.bytes.data(),24,8);}
void AdmissionAndExclusiveOwnership(){
    auto s=std::make_shared<State>();BrokerSession b([s]{return std::make_unique<Transport>(s);});
    const auto a=Attach(b);auto r=Send(b,a,Request(Opcode::Session,1));ValidReply(r,Opcode::Session,1,0,Status::AccessDenied);
    Check(s->opens==0,"synthetic authentication bypassed unqualified policy");
    b.UpdatePolicy(Qualified());const auto lease=Open(b,a,2);Check(lease!=0&&s->owners==1,"owner not acquired");
    const auto other=Attach(b);r=Send(b,other,Request(Opcode::Session,1));ValidReply(r,Opcode::Session,1,0,Status::OwnedElsewhere);
    Check(s->opens==1,"same identity stole global ownership");
    b.Disconnect(a);Check(s->owners==0&&s->closes==1,"disconnect did not retire owner");
    s->cancelled=false;const auto replacement=Open(b,other,2);Check(replacement>lease&&s->maxOwners==1,"replacement lease reused or overlapped");
    auto policy=Qualified();policy.unlocked=false;b.UpdatePolicy(policy);Check(s->owners==0,"lock transition retained owner");
    b.Disconnect(other);
}
void RequestsPacketsAndRetry(){
    auto s=std::make_shared<State>();BrokerSession b([s]{return std::make_unique<Transport>(s);});b.UpdatePolicy(Qualified());const auto c=Attach(b);const auto lease=Open(b,c);
    auto packet=Packet(false,42);s->writeCode=NativeTransferCode::Full;
    auto r=Send(b,c,Request(Opcode::WriteCapture,2,lease,packet));ValidReply(r,Opcode::WriteCapture,2,lease,Status::Full);
    s->writeCode=NativeTransferCode::Ok;r=Send(b,c,Request(Opcode::WriteCapture,3,lease,packet));ValidReply(r,Opcode::WriteCapture,3,lease,Status::Ok);
    Check(s->writes==2&&s->lastWrite==packet,"Full retry changed PCM or did not use new request");
    r=Send(b,c,Request(Opcode::Status,4,lease,GenerationPayload()));ValidReply(r,Opcode::Status,4,lease,Status::Ok);
    Check(Read(r.bytes.data(),48,8)==11&&Read(r.bytes.data(),56,8)==22&&Read(r.bytes.data(),64,8)==33,"status counters altered");
    s->readCode=NativeTransferCode::Ok;r=Send(b,c,Request(Opcode::ReadRender,5,lease,GenerationPayload(true)));ValidReply(r,Opcode::ReadRender,5,lease,Status::Ok);
    Check(std::memcmp(r.bytes.data()+48,s->render.data(),s->render.size())==0,"render PCM altered");
    r=Send(b,c,Request(Opcode::Status,5,lease,GenerationPayload()));Check(r.result.disposition==Dispatch::CloseConnection&&s->owners==0,"replayed request did not fail closed");
}
void FailureCases(){
    for(unsigned mode=0;mode<6;++mode){auto s=std::make_shared<State>();BrokerSession b([s]{return std::make_unique<Transport>(s);});b.UpdatePolicy(Qualified());const auto c=Attach(b);const auto lease=Open(b,c);Reply r;
        if(mode==0){auto request=Request(Opcode::Status,2,lease,GenerationPayload());request[36]=1;r=Send(b,c,request);}
        if(mode==1)r=Send(b,c,Request(Opcode::Status,2,lease+1,GenerationPayload()));
        if(mode==2)r=Send(b,c,Request(Opcode::WriteCapture,2,lease,Packet(true)));
        if(mode==3){s->writeCode=NativeTransferCode::Full;Send(b,c,Request(Opcode::WriteCapture,2,lease,Packet(false)));auto altered=Packet(false);altered[64]^=1;r=Send(b,c,Request(Opcode::WriteCapture,3,lease,altered));Check(s->writes==1,"altered Full retry reached transport");}
        if(mode==4){s->readCode=NativeTransferCode::Ok;s->render[14]=2;r=Send(b,c,Request(Opcode::ReadRender,2,lease,GenerationPayload(true)));}
        if(mode==5){Send(b,c,Request(Opcode::WriteCapture,2,lease,Packet(false,~std::uint64_t{0})));r=Send(b,c,Request(Opcode::WriteCapture,3,lease,Packet(false,0)));}
        Check(r.result.disposition==Dispatch::CloseConnection&&s->owners==0,"invalid packet/lease/retry did not fail closed");
    }
    auto s=std::make_shared<State>();s->openCode=NativeTransferCode::Unavailable;BrokerSession b([s]{return std::make_unique<Transport>(s);});b.UpdatePolicy(Qualified());auto c=Attach(b);
    auto r=Send(b,c,Request(Opcode::Session,1));ValidReply(r,Opcode::Session,1,0,Status::Unavailable);Check(s->owners==0,"failed Open retained lease owner");
    s->openCode=NativeTransferCode::Ok;s->cancelled=false;const auto lease=Open(b,c,2);
    r=Send(b,c,Request(Opcode::Close,3,lease,GenerationPayload()));ValidReply(r,Opcode::Close,3,lease,Status::Ok);
    Check(s->owners==0,"CLOSE retained owner");
    b.Disconnect(c);
}
void BoundsAndExhaustion(){
    auto s=std::make_shared<State>();auto ids=std::make_shared<BrokerIdAllocators>();
    BrokerSession b([s]{return std::make_unique<Transport>(s);},ids);b.UpdatePolicy(Qualified());
    const auto first=Attach(b);Attach(b);Attach(b);Attach(b);
    Check(b.AttachAuthenticated(Peer()).id==0,"unbounded connection slots");
    b.Disconnect(first);const auto next=Attach(b);Check(next>first,"connection id reused");
    std::array<unsigned char,MaxMessageBytes> output{};const auto request=Request(Opcode::Session,1);
    for(const std::size_t capacity:{std::size_t{47},std::size_t{48},MaxMessageBytes-1})
        Check(b.Process(next,request.data(),request.size(),output.data(),capacity).disposition==Dispatch::InvalidBuffer&&s->opens==0,
            "short response buffer mutated transport");
    ids->leases=~std::uint64_t{0};auto r=Send(b,next,request);ValidReply(r,Opcode::Session,1,0,Status::Failed);
    Check(s->opens==0&&ids->leases==~std::uint64_t{0},"lease allocator wrapped or opened at exhaustion");
    b.Disconnect(next);ids->connections=~std::uint64_t{0};Check(!b.AttachAuthenticated(Peer()).id&&ids->connections==~std::uint64_t{0},"connection allocator wrapped");
    auto state=std::make_shared<State>();BrokerSession maxRequest([state]{return std::make_unique<Transport>(state);});maxRequest.UpdatePolicy(Qualified());const auto c=Attach(maxRequest);
    const auto lease=Open(maxRequest,c,~std::uint64_t{0});
    r=Send(maxRequest,c,Request(Opcode::Status,0,lease,GenerationPayload()));
    Check(r.result.disposition==Dispatch::CloseConnection&&state->owners==0,"request id wrapped after MAX");
}
void CancelBlockedOpen(){
    auto s=std::make_shared<State>();s->blockOpen=true;BrokerSession b([s]{return std::make_unique<Transport>(s);});b.UpdatePolicy(Qualified());const auto c=Attach(b);
    Reply reply;std::thread worker([&]{reply=Send(b,c,Request(Opcode::Session,1));});
    bool entered=false;{std::unique_lock<std::mutex> lock(s->mutex);entered=s->changed.wait_for(lock,std::chrono::seconds(3),[&]{return s->entered;});}
    // Watchdog releases fake Open even if a broken implementation never forwards
    // cancel, so this regression cannot leave the test process hung.
    bool watchdogFired=false;
    std::thread watchdog([&]{std::unique_lock<std::mutex> lock(s->mutex);if(!s->changed.wait_for(lock,std::chrono::seconds(2),[&]{return s->cancelled;})){watchdogFired=true;s->cancelled=true;s->changed.notify_all();}});
    b.RequestCancel(c);worker.join();watchdog.join();
    Check(entered&&!watchdogFired&&s->cancels>0&&s->owners==0,"cancel did not reach published transport during Open");
    Check(reply.result.status==Status::Cancelled,"cancelled Open returned success");b.Disconnect(c);
}
void NonOwnerCannotTouchLease(){
    for(const auto op:{Opcode::ReadRender,Opcode::WriteCapture,Opcode::Close}){
        auto s=std::make_shared<State>();BrokerSession b([s]{return std::make_unique<Transport>(s);});b.UpdatePolicy(Qualified());
        const auto owner=Attach(b);const auto lease=Open(b,owner);const auto cancelledOther=Attach(b);
        b.RequestCancel(cancelledOther);Check(s->cancels==0&&s->owners==1,"nonowner cancellation reached owner");
        const auto other=Attach(b);
        const auto payload=op==Opcode::WriteCapture?Packet(false):GenerationPayload(op==Opcode::ReadRender);
        auto r=Send(b,other,Request(op,1,lease,payload));
        Check(r.result.disposition==Dispatch::CloseConnection&&s->owners==1&&s->reads==0&&s->writes==0,
            "nonowner operation touched known lease");
        r=Send(b,owner,Request(Opcode::Status,2,lease,GenerationPayload()));ValidReply(r,Opcode::Status,2,lease,Status::Ok);
    }
}
void DenialsKeepDiagnostic(){
    auto s=std::make_shared<State>();s->openCode=NativeTransferCode::AccessDenied;
    BrokerSession b([s]{return std::make_unique<Transport>(s);});b.UpdatePolicy(Qualified());const auto c=Attach(b);
    auto r=Send(b,c,Request(Opcode::Session,1));ValidReply(r,Opcode::Session,1,0,Status::AccessDenied);
    Check(Read(r.bytes.data(),48,4)==5&&s->owners==0,"Open denial lost systemError");
    s->cancelled=false;s->openCode=NativeTransferCode::Ok;const auto lease=Open(b,c,2);
    s->readCode=NativeTransferCode::AccessDenied;r=Send(b,c,Request(Opcode::ReadRender,3,lease,GenerationPayload(true)));
    ValidReply(r,Opcode::ReadRender,3,lease,Status::AccessDenied);
    Check(Read(r.bytes.data(),48,4)==5&&s->owners==0,"read denial became Protected or lost systemError");
}
void CancelBlockedAudioThenRetirePolicy(){
    for(bool read:{true,false}){
        auto s=std::make_shared<State>();unsigned constructions=0;
        BrokerSession b([&]{++constructions;return std::make_unique<Transport>(s);});b.UpdatePolicy(Qualified());
        const auto c=Attach(b);const auto lease=Open(b,c);
        {std::lock_guard<std::mutex> lock(s->mutex);s->entered=false;s->blockRead=read;s->blockWrite=!read;}
        const auto op=read?Opcode::ReadRender:Opcode::WriteCapture;
        const auto payload=read?GenerationPayload(true):Packet(false);
        Reply reply;std::thread worker([&]{reply=Send(b,c,Request(op,2,lease,payload));});
        bool entered=false;{std::unique_lock<std::mutex> lock(s->mutex);entered=s->changed.wait_for(lock,std::chrono::seconds(3),[&]{return s->entered;});}
        bool watchdogFired=false;
        std::thread watchdog([&]{std::unique_lock<std::mutex> lock(s->mutex);if(!s->changed.wait_for(lock,std::chrono::seconds(2),[&]{return s->cancelled;})){watchdogFired=true;s->cancelled=true;s->changed.notify_all();}});
        b.RequestCancel(c);worker.join();watchdog.join();
        Check(entered&&!watchdogFired&&reply.result.status==Status::Cancelled&&s->owners==0,"blocked audio cancellation failed");
        auto policy=Qualified();policy.unlocked=false;b.UpdatePolicy(policy);Check(s->owners==0,"policy update retained cancelled owner");
        b.UpdatePolicy(Qualified());auto denied=Send(b,c,Request(Opcode::Session,3));
        Check(denied.result.status==Status::Cancelled&&s->opens==1,"cancelled connection reopened transport");
        s->blockRead=s->blockWrite=s->cancelled=false;const auto fresh=Attach(b);const auto newLease=Open(b,fresh);
        Check(newLease>lease&&constructions==2&&s->maxOwners==1,"replacement did not use fresh transport instance");
    }
}
}
int main(){try{AdmissionAndExclusiveOwnership();RequestsPacketsAndRetry();FailureCases();BoundsAndExhaustion();NonOwnerCannotTouchLease();DenialsKeepDiagnostic();CancelBlockedOpen();CancelBlockedAudioThenRetirePolicy();std::cout<<"Broker tests passed (fake authenticated identities/transport only)\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
