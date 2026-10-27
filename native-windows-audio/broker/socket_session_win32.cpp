#include "socket_session_win32.h"
#include "channel_frames.h"
#include "pipe_identity_win32.h"
#include "session_observation_win32.h"
#include <sddl.h>
#include <bcrypt.h>
#include <algorithm>
#include <mutex>
#include <stdexcept>

namespace timox_broker {
namespace {
using Code=NativeChannelCode;
using Clock=std::chrono::steady_clock;
auto Deadline(){return Clock::now()+std::chrono::seconds(5);}
std::string SidText(const std::array<std::uint8_t,68>& sid){
    LPSTR text=nullptr;
    if(!ConvertSidToStringSidA(const_cast<std::uint8_t*>(sid.data()),&text))throw std::runtime_error("enrollment SID");
    struct Release {LPSTR value;~Release(){LocalFree(value);}} release{text};return text;
}
bool IsCurrentConsolePeer(const ChannelIdentity& peer) noexcept {
    InteractiveSessionObservation session;
    if(ObserveInteractiveSessions(session)!=ERROR_SUCCESS||session.interactiveLogonCount!=1||
        !session.consoleUnlocked||session.consoleSessionId!=peer.sessionId)return false;
    try{return session.consoleUserSid==SidText(peer.userSid);}catch(...){return false;}
}
}
SocketEnrollment::SocketEnrollment(const SchannelStream::Pin& pin,const ChannelIdentity& peer,std::uint64_t value)
    :certificate(pin),identity(peer),epoch(value){
    if(!epoch||!SameChannelIdentity(identity,identity)||
        std::all_of(certificate.begin(),certificate.end(),[](auto b){return b==0;}))throw std::invalid_argument("invalid socket enrollment");
}
struct SocketSession::Impl {
    BrokerDispatcher& broker;
    std::shared_ptr<const SocketEnrollment> enrollment;
    SocketEnrollmentResolver resolveEnrollment;
    PCCERT_CONTEXT serverCertificate=nullptr;
    std::unique_ptr<UnixSocketStream> ownedSocket;
    UnixSocketStream* peerSocket=nullptr;
    HANDLE peerProcess=nullptr;
    std::atomic<bool> peerIdentityChecked{false};
    std::unique_ptr<SchannelStream> stream;
    std::mutex socketMutex;
    std::mutex mutex;ConnectionId id=0;
    bool nativePending=false;SchannelStream::Deadline nativeDeadline{};
    Clock::time_point lastConsoleCheck{};
    std::atomic<bool> cancelled{false},started{false};
    std::array<unsigned char,MaxMessageBytes> request{},response{};
    Impl(BrokerDispatcher& b,std::unique_ptr<UnixSocketStream> socket,PCCERT_CONTEXT cert,SocketEnrollmentResolver resolver)
        :broker(b),resolveEnrollment(std::move(resolver)),serverCertificate(cert),ownedSocket(std::move(socket)),peerSocket(ownedSocket.get()){
        if(!cert||!peerSocket||!resolveEnrollment)throw std::invalid_argument("missing server certificate, socket or enrollment resolver");
    }
    ~Impl(){if(peerProcess)CloseHandle(peerProcess);}
    bool Active() const noexcept {return !cancelled.load(std::memory_order_acquire)&&(!enrollment||enrollment->Active());}
    bool PeerProcessAlive() const noexcept {
        return peerProcess&&WaitForSingleObject(peerProcess,0)==WAIT_TIMEOUT;
    }
    NativeChannelResult BindPeerProcess() {
        std::uint32_t pid=0;
        auto result=peerSocket->GetPeerProcessId(pid);
        if(result.code!=Code::Ok)return {Code::AccessDenied,0,result.systemError};
        peerProcess=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);
        if(!peerProcess)return {Code::AccessDenied,0,GetLastError()};
        ChannelIdentity observed;
        const auto error=ObserveProcessHandleIdentity(peerProcess,observed);
        if(error||!IsCurrentConsolePeer(observed))
            return {Code::AccessDenied,0,error?error:ERROR_ACCESS_DENIED};
        auto candidate=resolveEnrollment(observed);
        if(!Active())return {Code::Cancelled};
        if(!candidate||!candidate->Active()||!SameChannelIdentity(observed,candidate->identity))return {Code::AccessDenied};
        enrollment=std::move(candidate);
        {
            std::lock_guard<std::mutex> lock(socketMutex);
            auto* acceptedSocket=peerSocket;
            try {
                stream=std::make_unique<SchannelStream>(std::unique_ptr<ByteStream>(ownedSocket.release()),true,
                    serverCertificate,enrollment->certificate);
            } catch(...) {
                // Construction may consume/destroy the socket before throwing.
                peerSocket=nullptr;
                throw;
            }
            peerSocket=acceptedSocket;
        }
        peerIdentityChecked.store(true,std::memory_order_release);
        return {Code::Ok};
    }
    void CancelSocket() noexcept {
        std::lock_guard<std::mutex> lock(socketMutex);
        if(peerSocket)peerSocket->RequestCancel();
    }
    void Cancel() noexcept {
        cancelled.store(true,std::memory_order_release);
        CancelSocket();
        std::lock_guard<std::mutex> lock(mutex);if(id)broker.RequestCancel(id);
    }
    void Retire() noexcept {
        ConnectionId retiring=0;
        {std::lock_guard<std::mutex> lock(mutex);retiring=id;id=0;nativePending=false;}
        if(retiring){broker.RequestCancel(retiring);broker.Disconnect(retiring);}
        if(stream)stream->Close();else if(ownedSocket)ownedSocket->Close();request.fill(0);response.fill(0);
    }
    NativeChannelResult WriteBinding(const ChannelBinding& binding,SchannelStream::Deadline deadline) noexcept {
        if(!Active()||!stream)return {Code::Cancelled};return WriteBindingFrame(*stream,binding,deadline);
    }
    void CheckDeadline() noexcept {
        if(peerIdentityChecked.load(std::memory_order_acquire)&&!PeerProcessAlive()) {
            cancelled.store(true,std::memory_order_release);CancelSocket();
            std::lock_guard<std::mutex> lock(mutex);if(id)broker.RequestCancel(id);return;
        }
        if(peerIdentityChecked.load(std::memory_order_acquire)) {
            const auto now=Clock::now();
            if(lastConsoleCheck==Clock::time_point{}||now-lastConsoleCheck>=std::chrono::milliseconds(200)) {
                lastConsoleCheck=now;
                InteractiveSessionObservation session;
                const auto error=ObserveInteractiveSessions(session,false);
                if(error||session.interactiveLogonCount!=1||!session.consoleUnlocked||
                    !enrollment||session.consoleSessionId!=enrollment->identity.sessionId) {
                    cancelled.store(true,std::memory_order_release);CancelSocket();
                    std::lock_guard<std::mutex> lock(mutex);if(id)broker.RequestCancel(id);return;
                }
            }
        }
        std::lock_guard<std::mutex> lock(mutex);
        if(!nativePending||Clock::now()<nativeDeadline)return;
        // Publication, completion and cancellation use the same mutex, so a
        // stale timeout cannot cancel a later operation after it replaced one.
        cancelled.store(true,std::memory_order_release);CancelSocket();
        if(id)broker.RequestCancel(id);
    }
    DispatchResult Process(ConnectionId connection,std::size_t bytes,SchannelStream::Deadline deadline) noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if(!Active()||!PeerProcessAlive()||Clock::now()>=deadline)return {Dispatch::CloseConnection,0,Status::Cancelled};
            nativeDeadline=deadline;nativePending=true;
        }
        const auto result=broker.Process(connection,request.data(),bytes,response.data(),response.size());
        {std::lock_guard<std::mutex> lock(mutex);nativePending=false;}
        return result;
    }
    NativeChannelResult Serve(){
        if(!Active())return {Code::Cancelled};
        auto result=BindPeerProcess();if(result.code!=Code::Ok)return result;
        const auto setupDeadline=Deadline();
        result=stream->Handshake(setupDeadline);if(result.code!=Code::Ok)return result;
        ChannelBinding binding;
        result=ReadBindingFrame(*stream,binding,setupDeadline);if(result.code!=Code::Ok)return result;
        if(binding.type!=BindingType::BindRequest||!SameChannelIdentity(binding.identity,enrollment->identity))return {Code::AccessDenied};
        ChannelIdentity observed;
        if(!Active()||!PeerProcessAlive()||ObserveProcessHandleIdentity(peerProcess,observed)!=ERROR_SUCCESS||
            !SameChannelIdentity(observed,enrollment->identity)||!IsCurrentConsolePeer(observed))return {Code::AccessDenied};
        const auto& identity=enrollment->identity;
        NativePeerIdentity peer{SidText(identity.userSid),SidText(identity.logonSid),identity.authenticationId,identity.sessionId};
        const auto attached=broker.AttachAuthenticated(peer);if(!attached.id)return {Code::AccessDenied};
        {std::lock_guard<std::mutex> lock(mutex);id=attached.id;if(!Active()){broker.RequestCancel(id);return {Code::Cancelled};}}
        binding.type=BindingType::BindReply;
        result=WriteBinding(binding,setupDeadline);if(result.code!=Code::Ok)return result;
        result=ReadAudioFrame(*stream,request.data(),request.size(),setupDeadline);if(result.code!=Code::Ok)return result;
        if(frames_detail::Read(request.data(),6,2)!=static_cast<unsigned>(Opcode::Session))return {Code::AccessDenied};
        if(!Active())return {Code::Cancelled};
        auto reply=Process(attached.id,result.bytes,setupDeadline);
        if(reply.disposition!=Dispatch::Reply||!Active())return {Code::Cancelled};
        result=stream->WriteAll(response.data(),reply.bytes,setupDeadline);if(result.code!=Code::Ok)return result;
        if(reply.status!=Status::Ok)return {Code::AccessDenied};
        ChannelBinding offer;offer.type=BindingType::DataOffer;
        offer.lease=frames_detail::Read(response.data(),24,8);offer.generation=frames_detail::Read(response.data(),48,8);
        response.fill(0);
        if(BCryptGenRandom(nullptr,offer.nonce.data(),static_cast<ULONG>(offer.nonce.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)return {Code::Failed};
        result=WriteBinding(offer,setupDeadline);if(result.code!=Code::Ok)return result;
        result=ReadBindingFrame(*stream,binding,setupDeadline);if(result.code!=Code::Ok)return result;
        ChannelBinding expected=offer;expected.type=BindingType::DataBindRequest;expected.identity=identity;
        if(!MatchesBinding(binding,expected))return {Code::AccessDenied};
        binding.type=BindingType::DataBindReply;
        result=WriteBinding(binding,setupDeadline);if(result.code!=Code::Ok)return result;
        for(;;){
            if(!Active())return {Code::Cancelled};
            const auto deadline=Deadline();
            result=ReadAudioFrame(*stream,request.data(),request.size(),deadline);if(result.code!=Code::Ok)return result;
            const auto opcode=frames_detail::Read(request.data(),6,2);
            if(opcode==static_cast<unsigned>(Opcode::Session))return {Code::AccessDenied};
            if(!Active())return {Code::Cancelled};
            reply=Process(attached.id,result.bytes,deadline);
            if(reply.disposition!=Dispatch::Reply||!Active())return {Code::Cancelled};
            result=stream->WriteAll(response.data(),reply.bytes,deadline);response.fill(0);
            if(result.code!=Code::Ok)return result;
            if(opcode==static_cast<unsigned>(Opcode::Close))return {Code::Ok};
        }
    }
};
SocketSession::SocketSession(BrokerDispatcher& broker,std::unique_ptr<UnixSocketStream> socket,PCCERT_CONTEXT cert,
    std::shared_ptr<const SocketEnrollment> enrollment):SocketSession(broker,std::move(socket),cert,
        [enrollment=std::move(enrollment)](const ChannelIdentity& observed){
            return enrollment&&SameChannelIdentity(observed,enrollment->identity)?enrollment:std::shared_ptr<const SocketEnrollment>{};
        }){}
SocketSession::SocketSession(BrokerDispatcher& broker,std::unique_ptr<UnixSocketStream> socket,PCCERT_CONTEXT cert,
    SocketEnrollmentResolver resolver):impl_(std::make_unique<Impl>(broker,std::move(socket),cert,std::move(resolver))){}
SocketSession::~SocketSession()=default;
std::size_t SocketSession::FixedStorageBytes() noexcept{
    return sizeof(SocketSession)+sizeof(Impl)+SchannelStream::RetainedApplicationBytes()-sizeof(SchannelStream)+
        UnixSocketStream::FixedStorageBytes();
}
NativeChannelResult SocketSession::Run() noexcept {
    if(impl_->started.exchange(true))return {Code::Failed};
    NativeChannelResult result;
    try{result=impl_->Serve();}catch(...){result={Code::Failed};}
    impl_->Retire();return result;
}
void SocketSession::RequestCancel() noexcept {impl_->Cancel();}
void SocketSession::CheckDeadline() noexcept {impl_->CheckDeadline();}
}
