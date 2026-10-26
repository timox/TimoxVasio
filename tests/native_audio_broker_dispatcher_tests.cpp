#include "broker_dispatcher.h"
#include <array>
#include <condition_variable>
#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
using namespace timox_broker;
namespace {
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
NativePeerIdentity Peer(){return {"S-1-5-21-100-200-300-1001","S-1-5-5-12-34",1234,2};}
NativeSessionPolicySnapshot Policy(){NativeSessionPolicySnapshot p;p.allowedUserSid=Peer().userSid;p.consoleSessionId=2;p.interactiveLogonCount=1;p.unlocked=true;p.audioIsolationQualified=true;p.optionalConsoleIdentity=Peer();return p;}
struct Audio {
    std::mutex mutex;std::condition_variable changed;bool blockOpen=false,entered=false,cancelled=false,wrongThread=false;
    unsigned owners=0,maxOwners=0,opens=0,closes=0;std::thread::id worker;
    bool Wait(){std::unique_lock<std::mutex> lock(mutex);return changed.wait_for(lock,std::chrono::seconds(2),[&]{return entered;});}
    void Observe(){if(worker==std::thread::id{})worker=std::this_thread::get_id();else if(worker!=std::this_thread::get_id())wrongThread=true;}
};
class Backend final:public INativeAudioTransport {
    std::shared_ptr<Audio> audio;bool owned=false;
public:
    explicit Backend(std::shared_ptr<Audio> a):audio(std::move(a)){}
    NativeTransferResult Open(NativeSessionInfo& info)noexcept override{
        std::unique_lock<std::mutex> lock(audio->mutex);audio->Observe();++audio->opens;audio->entered=true;audio->changed.notify_all();
        if(audio->blockOpen)audio->changed.wait(lock,[&]{return audio->cancelled;});
        if(audio->cancelled)return {NativeTransferCode::Cancelled};
        owned=true;++audio->owners;audio->maxOwners=(std::max)(audio->maxOwners,audio->owners);info.wireGeneration=23;return {NativeTransferCode::Ok};
    }
    NativeTransferResult ReadRender(std::uint32_t,void*,std::uint32_t)noexcept override{return {NativeTransferCode::Empty};}
    NativeTransferResult WriteCapture(const void*,std::uint32_t)noexcept override{return {NativeTransferCode::Ok};}
    NativeTransferResult ReadStatus(std::uint64_t (&v)[3])noexcept override{v[0]=v[1]=v[2]=0;return {NativeTransferCode::Ok};}
    void RequestCancel()noexcept override{std::lock_guard<std::mutex> lock(audio->mutex);audio->cancelled=true;audio->changed.notify_all();}
    void Close()noexcept override{std::lock_guard<std::mutex> lock(audio->mutex);audio->Observe();++audio->closes;if(owned){--audio->owners;owned=false;}}
};
struct Reply {DispatchResult result;std::array<unsigned char,MaxMessageBytes> bytes{};};
struct Callers {
    BrokerDispatcher& dispatcher;std::array<std::thread,4> threads{};
    ~Callers(){dispatcher.Stop();Join();}
    void Join(){for(auto& thread:threads)if(thread.joinable())thread.join();}
};
Reply Open(BrokerDispatcher& d,ConnectionId id){
    std::array<unsigned char,48> request{};request[0]='T';request[1]='V';request[2]='A';request[3]='B';request[4]=1;request[6]=1;request[8]=48;request[16]=1;
    Reply reply;reply.bytes.fill(42);reply.result=d.Process(id,request.data(),request.size(),reply.bytes.data(),reply.bytes.size());return reply;
}
void ParallelOwnersAndBound(){
    auto audio=std::make_shared<Audio>();BrokerDispatcher d([audio]{return std::make_unique<Backend>(audio);},Policy());
    std::array<ConnectionId,4> ids{};for(auto& id:ids){id=d.AttachAuthenticated(Peer()).id;Check(id!=0,"dispatcher attach failed");}
    Check(!d.AttachAuthenticated(Peer()).id,"fifth connection accepted");
    Reply a,b;Callers callers{d};
    callers.threads[0]=std::thread([&]{a=Open(d,ids[0]);});callers.threads[1]=std::thread([&]{b=Open(d,ids[1]);});callers.Join();
    Check(a.result.disposition==Dispatch::Reply&&b.result.disposition==Dispatch::Reply,"parallel dispatch failed");
    Check((a.result.status==Status::Ok&&b.result.status==Status::OwnedElsewhere)||(b.result.status==Status::Ok&&a.result.status==Status::OwnedElsewhere),"parallel owners not arbitrated");
    for(auto id:ids)d.Disconnect(id);d.Stop();
    Check(audio->owners==0&&audio->maxOwners==1&&!audio->wrongThread&&audio->worker!=std::this_thread::get_id(),"native work not serialized on one worker");
}
void CancelBlockedOpen(unsigned mode){
    auto audio=std::make_shared<Audio>();audio->blockOpen=true;
    BrokerDispatcher d([audio]{return std::make_unique<Backend>(audio);},Policy());
    const auto id=d.AttachAuthenticated(Peer()).id;Check(id!=0,"cancel fixture attach");
    Reply reply;Callers callers{d};callers.threads[0]=std::thread([&]{reply=Open(d,id);});const bool entered=audio->Wait();
    if(mode==1)d.Stop();
    else if(mode==2){auto policy=Policy();policy.unlocked=false;d.RevokeAllAndUpdatePolicy(policy);}
    else d.RequestCancel(id);
    callers.Join();
    Check(entered&&reply.result.disposition==Dispatch::CloseConnection&&reply.result.bytes==0,"cancel returned in-flight reply");
    for(auto byte:reply.bytes)Check(byte==0,"cancel leaked response bytes");
    d.Disconnect(id);d.Stop();
    Check(audio->owners==0&&!audio->wrongThread,"cancel cleanup thread/owner wrong");
    Check(!d.AttachAuthenticated(Peer()).id,"stopped dispatcher reopened");
}
void StopDrainsWaitingCallers(){
    auto audio=std::make_shared<Audio>();audio->blockOpen=true;
    BrokerDispatcher d([audio]{return std::make_unique<Backend>(audio);},Policy());
    std::array<ConnectionId,4> ids{};for(auto& id:ids){id=d.AttachAuthenticated(Peer()).id;Check(id!=0,"drain fixture attach");}
    std::array<Reply,4> replies;
    Callers callers{d};callers.threads[0]=std::thread([&]{replies[0]=Open(d,ids[0]);});const bool entered=audio->Wait();
    // The worker is held in native Open. Other connections either queue before
    // Stop or observe terminal state on entry; every caller must complete safely.
    callers.threads[1]=std::thread([&]{replies[1]=Open(d,ids[1]);});
    callers.threads[2]=std::thread([&]{replies[2]=Open(d,ids[2]);});
    callers.threads[3]=std::thread([&]{replies[3]=Open(d,ids[3]);});
    d.Stop();callers.Join();
    Check(entered,"drain never reached blocked native Open");
    for(const auto& reply:replies){
        Check(reply.result.disposition==Dispatch::CloseConnection&&reply.result.bytes==0,"Stop returned a queued response");
        for(auto byte:reply.bytes)Check(byte==0,"Stop retained queued response bytes");
    }
    Check(audio->owners==0&&!audio->wrongThread,"Stop failed worker cleanup");
}
void PolicyRevokes(){
    auto audio=std::make_shared<Audio>();BrokerDispatcher d([audio]{return std::make_unique<Backend>(audio);},Policy());
    const auto id=d.AttachAuthenticated(Peer()).id;Check(id!=0,"policy fixture attach");Check(Open(d,id).result.status==Status::Ok,"policy fixture open");
    auto p=Policy();p.unlocked=false;d.RevokeAllAndUpdatePolicy(p);
    Check(Open(d,id).result.disposition==Dispatch::CloseConnection,"old policy connection survived");d.Disconnect(id);
    const auto next=d.AttachAuthenticated(Peer()).id;Check(next&&next!=id,"connection id reused");
    Check(Open(d,next).result.status==Status::AccessDenied,"locked policy ignored");d.Disconnect(next);d.Stop();
    Check(audio->owners==0&&!audio->wrongThread,"policy retained native owner");
}
void PolicyReleasesSlots(){
    auto audio=std::make_shared<Audio>();BrokerDispatcher d([audio]{return std::make_unique<Backend>(audio);},Policy());
    std::array<ConnectionId,4> old{};for(auto& id:old){id=d.AttachAuthenticated(Peer()).id;Check(id!=0,"policy slot fixture");}
    d.RevokeAllAndUpdatePolicy(Policy());
    // Old I/O threads may still be draining cancelled writes. Their late
    // Disconnect must neither occupy slots forever nor detach a new identity.
    const auto replacement=d.AttachAuthenticated(Peer()).id;Check(replacement!=0,"revoked slots retained until old socket exits");
    for(auto id:old)d.Disconnect(id);
    Check(Open(d,replacement).result.status==Status::Ok,"late disconnect removed replacement");d.Disconnect(replacement);d.Stop();
}
}
int main(){try{ParallelOwnersAndBound();CancelBlockedOpen(0);CancelBlockedOpen(1);CancelBlockedOpen(2);StopDrainsWaitingCallers();PolicyRevokes();PolicyReleasesSlots();std::cout<<"Bounded broker dispatcher tests passed (synthetic policy, no pipes)\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
