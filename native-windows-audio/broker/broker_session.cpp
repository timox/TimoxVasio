#include "broker_session.h"
#include <array>
#include <cstring>
#include <limits>
#include <mutex>

namespace timox_broker {
namespace {
const auto processIds=std::make_shared<BrokerIdAllocators>();
std::uint64_t Allocate(std::atomic<std::uint64_t>& counter) noexcept {
    auto next=counter.load(std::memory_order_relaxed);
    while(next && next!=(std::numeric_limits<std::uint64_t>::max)())
        if(counter.compare_exchange_weak(next,next+1,std::memory_order_relaxed))return next;
    return 0;
}
bool Bounded(const NativePeerIdentity& peer) noexcept {
    return peer.userSid.size()<=184 && peer.logonSid.size()<=184;
}
void Put(void* buffer,std::size_t at,std::uint64_t value,unsigned bytes) noexcept {
    auto* p=static_cast<unsigned char*>(buffer);
    for(unsigned i=0;i<bytes;++i)p[at+i]=static_cast<unsigned char>(value>>(8*i));
}
Status Map(NativeTransferCode code) noexcept {
    switch(code){
    case NativeTransferCode::Ok:return Status::Ok;
    case NativeTransferCode::Empty:return Status::Empty;
    case NativeTransferCode::Full:return Status::Full;
    case NativeTransferCode::Unavailable:return Status::Unavailable;
    case NativeTransferCode::AccessDenied:return Status::AccessDenied;
    case NativeTransferCode::OwnedElsewhere:return Status::OwnedElsewhere;
    case NativeTransferCode::Removed:return Status::Removed;
    case NativeTransferCode::Cancelled:return Status::Cancelled;
    case NativeTransferCode::InvalidPacket:return Status::InvalidPacket;
    case NativeTransferCode::Failed:return Status::Failed;
    }
    return Status::Failed;
}
}
struct BrokerSession::Impl {
    struct Connection {ConnectionId id=0;bool cancelled=false;NativePeerIdentity peer;std::uint64_t lastRequest=0;};
    Factory factory;std::shared_ptr<BrokerIdAllocators> ids;
    std::array<Connection,4> connections;
    NativeSessionPolicySnapshot policy;
    // cancelMutex protects only id/cancelled and published cancellation target.
    // Identity/request state and unique_ptr ownership stay on the worker.
    std::mutex cancelMutex;INativeAudioTransport* cancelTarget=nullptr;ConnectionId cancelOwner=0;
    std::unique_ptr<INativeAudioTransport> transport;
    ConnectionId owner=0;std::uint64_t lease=0,generation=0,lastCapture=0,lastRender=0;
    bool haveCapture=false,haveRender=false;
    std::array<unsigned char,MaxPayloadBytes> pending{};std::uint32_t pendingBytes=0;

    Connection* Find(ConnectionId id) noexcept {
        if(!id)return nullptr;
        for(auto& c:connections)if(c.id==id)return &c;
        return nullptr;
    }
    bool Cancelled(Connection& c) noexcept {std::lock_guard<std::mutex> lock(cancelMutex);return c.cancelled;}
    void Retire() noexcept {
        // A concurrent RequestCancel finishes before the target is unpublished.
        // Close/destruction are outside the mutex and after synchronous IO ends.
        {std::lock_guard<std::mutex> lock(cancelMutex);cancelTarget=nullptr;cancelOwner=0;}
        if(transport){transport->RequestCancel();transport->Close();transport.reset();}
        owner=0;lease=generation=0;haveCapture=haveRender=false;pendingBytes=0;
        pending.fill(0);policy.optionalActiveLeaseIdentity.reset();
    }
    void Disconnect(ConnectionId id) noexcept {
        auto* c=Find(id);if(!c)return;
        if(owner==id || cancelOwner==id)Retire();
        std::lock_guard<std::mutex> lock(cancelMutex);*c={};
    }
    DispatchResult Reject(ConnectionId id,Status status=Status::InvalidPacket) noexcept {
        Disconnect(id);return {Dispatch::CloseConnection,0,status};
    }
    DispatchResult Reply(void* output,Opcode op,std::uint64_t request,std::uint64_t replyLease,
        Status status,std::uint32_t payloadBytes=0,std::uint32_t systemError=0) noexcept {
        std::memset(output,0,HeaderBytes);
        Put(output,0,Magic,4);Put(output,4,1,2);Put(output,6,static_cast<unsigned>(op)|0x8000u,2);
        Put(output,8,HeaderBytes,4);Put(output,16,request,8);Put(output,24,replyLease,8);Put(output,32,static_cast<unsigned>(status),4);
        if(status!=Status::Ok){payloadBytes=4;Put(output,HeaderBytes,systemError,4);}
        Put(output,12,payloadBytes,4);return {Dispatch::Reply,static_cast<std::uint32_t>(HeaderBytes)+payloadBytes,status};
    }
};
BrokerSession::BrokerSession(Factory factory,std::shared_ptr<BrokerIdAllocators> ids):impl_(std::make_unique<Impl>()) {
    impl_->factory=std::move(factory);impl_->ids=ids?std::move(ids):processIds;
}
BrokerSession::~BrokerSession(){impl_->Retire();}
AttachedConnection BrokerSession::AttachAuthenticated(const NativePeerIdentity& peer) noexcept {
    auto& p=*impl_;if(!Bounded(peer))return {0,Status::AccessDenied};
    try {
        // No concurrent worker mutation is allowed. Only RequestCancel shares
        // slot IDs, so publish an initialized slot under the short mutex.
        for(auto& c:p.connections)if(!c.id){
            auto identity=peer;const auto id=Allocate(p.ids->connections);if(!id)return {0,Status::Failed};
            std::lock_guard<std::mutex> lock(p.cancelMutex);
            c.peer=std::move(identity);c.lastRequest=0;c.cancelled=false;c.id=id;return {id,Status::Ok};
        }
    }catch(...){return {0,Status::Failed};}
    return {0,Status::Full};
}
void BrokerSession::UpdatePolicy(const NativeSessionPolicySnapshot& policy) noexcept {
    auto& p=*impl_;
    try {
        if(policy.allowedUserSid.size()>184 || (policy.optionalConsoleIdentity && !Bounded(*policy.optionalConsoleIdentity))) {
            p.policy={};p.Retire();return;
        }
        NativeSessionPolicySnapshot next;
        next.allowedUserSid=policy.allowedUserSid;next.consoleSessionId=policy.consoleSessionId;
        next.interactiveLogonCount=policy.interactiveLogonCount;next.unlocked=policy.unlocked;
        next.audioIsolationQualified=policy.audioIsolationQualified;
        next.optionalConsoleIdentity=policy.optionalConsoleIdentity;
        if(const auto* c=p.Find(p.owner))next.optionalActiveLeaseIdentity=c->peer;
        p.policy=std::move(next);
        if(const auto* c=p.Find(p.owner))
            if(EvaluateNativeLeaseContinuation(c->peer,p.policy).action==NativeLeaseAction::Retire)p.Retire();
    }catch(...){p.policy={};p.Retire();}
}
void BrokerSession::Disconnect(ConnectionId id) noexcept {impl_->Disconnect(id);}
void BrokerSession::RequestCancel(ConnectionId id) noexcept {
    auto& p=*impl_;std::lock_guard<std::mutex> lock(p.cancelMutex);
    if(auto* c=p.Find(id)){
        c->cancelled=true;
        if(p.cancelOwner==id && p.cancelTarget)p.cancelTarget->RequestCancel();
    }
}
DispatchResult BrokerSession::Process(ConnectionId id,const void* input,std::size_t bytes,
    void* output,std::size_t capacity) noexcept {
    auto& p=*impl_;
    if(!output || capacity<MaxMessageBytes)return {Dispatch::InvalidBuffer,0,Status::Failed};
    auto* c=p.Find(id);if(!c)return {Dispatch::CloseConnection,0,Status::AccessDenied};
    if(!input || bytes<HeaderBytes || bytes>MaxMessageBytes)return p.Reject(id);
    const auto raw=timox_native::detail::ReadLittleEndian(static_cast<const unsigned char*>(input),6,2);
    if(raw<1 || raw>5)return p.Reject(id,Status::ProtocolMismatch);
    const auto op=static_cast<Opcode>(raw);
    // A second connection cannot touch another owner's transport even if it
    // learns its lease number. Reject only the offending connection.
    if(op!=Opcode::Session && p.owner!=id)return p.Reject(id,Status::AccessDenied);
    const auto validated=ValidateMessage(input,bytes,{false,op,0,c->lastRequest,p.lease,p.generation,4096});
    if(validated.error!=Error::None)return p.Reject(id);
    const auto message=validated.message;c->lastRequest=message.requestId;
    const auto replyLease=op==Opcode::Session?0:message.leaseId;
    if(p.Cancelled(*c)){
        p.Disconnect(id);return p.Reply(output,op,message.requestId,replyLease,Status::Cancelled);
    }
    try {
        if(op==Opcode::Session){
            if(EvaluateNativeAdmission(c->peer,p.policy)!=NativeAdmission::Allowed)
                return p.Reply(output,op,message.requestId,0,Status::AccessDenied);
            if(p.owner)return p.Reply(output,op,message.requestId,0,Status::OwnedElsewhere);
            const auto lease=Allocate(p.ids->leases);
            if(!lease)return p.Reply(output,op,message.requestId,0,Status::Failed);
            if(!p.factory)return p.Reply(output,op,message.requestId,0,Status::Unavailable);
            auto transport=p.factory();if(!transport)return p.Reply(output,op,message.requestId,0,Status::Unavailable);
            p.transport=std::move(transport);
            {std::lock_guard<std::mutex> lock(p.cancelMutex);
                p.cancelOwner=id;p.cancelTarget=p.transport.get();
                if(c->cancelled)p.cancelTarget->RequestCancel();}
            NativeSessionInfo session;auto result=p.transport->Open(session);
            if(p.Cancelled(*c))result={NativeTransferCode::Cancelled};
            if(result.code==NativeTransferCode::Ok && session.wireGeneration){
                p.policy.optionalActiveLeaseIdentity=c->peer;
                p.owner=id;p.lease=lease;p.generation=session.wireGeneration;
                Put(output,HeaderBytes,p.generation,8);return p.Reply(output,op,message.requestId,lease,Status::Ok,8);
            }
            const auto status=result.code==NativeTransferCode::Ok?Status::InvalidPacket:Map(result.code);
            p.Retire();
            if(result.code==NativeTransferCode::Empty || result.code==NativeTransferCode::Full)return p.Reject(id,Status::Failed);
            return p.Reply(output,op,message.requestId,0,status,0,result.systemError);
        }
        if(op==Opcode::Close){p.Retire();return p.Reply(output,op,message.requestId,replyLease,Status::Ok);}
        NativeTransferResult result;std::uint32_t payloadBytes=0;
        if(op==Opcode::ReadRender){
            const auto maxFrames=static_cast<std::uint32_t>(timox_native::detail::ReadLittleEndian(message.payload,8,4));
            auto* packet=static_cast<unsigned char*>(output)+HeaderBytes;
            result=p.transport->ReadRender(maxFrames,packet,static_cast<std::uint32_t>(MaxPayloadBytes));
            if(result.code==NativeTransferCode::Ok){
                if(result.bytes>MaxPayloadBytes)return p.Reject(id);
                const auto valid=timox_native::ValidatePacket(packet,result.bytes,{p.generation,1,maxFrames});
                if(valid.error!=timox_native::PacketError::None || (p.haveRender && valid.metadata.sequence<=p.lastRender))return p.Reject(id);
                p.haveRender=true;p.lastRender=valid.metadata.sequence;payloadBytes=result.bytes;
            }
        }else if(op==Opcode::WriteCapture){
            const auto sequence=timox_native::detail::ReadLittleEndian(message.payload,24,8);
            if((p.haveCapture && sequence<=p.lastCapture) ||
               (p.pendingBytes && (message.payloadBytes!=p.pendingBytes || std::memcmp(message.payload,p.pending.data(),p.pendingBytes))))return p.Reject(id);
            result=p.transport->WriteCapture(message.payload,message.payloadBytes);
            if(result.code==NativeTransferCode::Full){
                if(!p.pendingBytes){p.pendingBytes=message.payloadBytes;std::memcpy(p.pending.data(),message.payload,p.pendingBytes);}
            }else if(result.code==NativeTransferCode::Ok){p.haveCapture=true;p.lastCapture=sequence;p.pendingBytes=0;p.pending.fill(0);}
        }else {
            std::uint64_t counters[3]{};result=p.transport->ReadStatus(counters);
            if(result.code==NativeTransferCode::Ok){for(unsigned i=0;i<3;++i)Put(output,HeaderBytes+i*8,counters[i],8);payloadBytes=24;}
        }
        if(p.Cancelled(*c))result={NativeTransferCode::Cancelled};
        if(result.code==NativeTransferCode::Ok)return p.Reply(output,op,message.requestId,replyLease,Status::Ok,payloadBytes);
        if(result.code==NativeTransferCode::Empty && op==Opcode::ReadRender)return p.Reply(output,op,message.requestId,replyLease,Status::Empty,0,result.systemError);
        if(result.code==NativeTransferCode::Full && op==Opcode::WriteCapture)return p.Reply(output,op,message.requestId,replyLease,Status::Full,0,result.systemError);
        if(result.code==NativeTransferCode::Empty || result.code==NativeTransferCode::Full)return p.Reject(id,Status::Failed);
        p.Retire();return p.Reply(output,op,message.requestId,replyLease,Map(result.code),0,result.systemError);
    }catch(...){p.Retire();return p.Reply(output,op,message.requestId,replyLease,Status::Failed);}
}
}
