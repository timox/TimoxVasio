#include "socket_server_win32.h"
#include <array>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
namespace timox_broker {
namespace {
using Code=NativeChannelCode;
struct Certificate {
    PCCERT_CONTEXT value;
    explicit Certificate(PCCERT_CONTEXT c):value(c?CertDuplicateCertificateContext(c):nullptr){
        if(!value)throw std::invalid_argument("server certificate");
    }
    ~Certificate(){CertFreeCertificateContext(value);}
};
}
struct SocketServer::Impl {
    struct Slot {std::unique_ptr<SocketSession> session;std::thread worker;bool done=true;};
    BrokerDispatcher& broker;
    Certificate certificate;
    SocketEnrollmentResolver resolveEnrollment;
    std::shared_ptr<const SocketEnrollment> fixedEnrollment;
    UnixSocketStream listener;
    mutable std::mutex mutex;std::condition_variable changed;
    std::array<Slot,4> slots{};
    std::atomic<bool> cancelled{false},started{false};bool listening=false;
    Impl(BrokerDispatcher& b,PCCERT_CONTEXT c,SocketEnrollmentResolver resolver)
        :broker(b),certificate(c),resolveEnrollment(std::move(resolver)){
        if(!resolveEnrollment)throw std::invalid_argument("server enrollment resolver");
    }
    Impl(BrokerDispatcher& b,PCCERT_CONTEXT c,std::shared_ptr<const SocketEnrollment> e)
        :Impl(b,c,[e](const ChannelIdentity& observed){
            return e&&SameChannelIdentity(observed,e->identity)?e:std::shared_ptr<const SocketEnrollment>{};
        }){
        if(!e)throw std::invalid_argument("server enrollment");
        fixedEnrollment=std::move(e);
    }
    bool Active()const noexcept{return !cancelled.load(std::memory_order_acquire)&&(!fixedEnrollment||fixedEnrollment->Active());}
    void Cancel() noexcept {
        cancelled.store(true,std::memory_order_release);listener.RequestCancel();
        std::lock_guard<std::mutex> lock(mutex);
        for(auto& slot:slots)if(slot.session)slot.session->RequestCancel();
        changed.notify_all();
    }
    void Drain() noexcept {
        Cancel();
        for(auto& slot:slots){
            if(slot.worker.joinable())slot.worker.join();
            std::lock_guard<std::mutex> lock(mutex);slot.session.reset();slot.done=true;
        }
        listener.Close();
    }
    void CheckDeadlines() noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        for(auto& slot:slots)if(slot.session&&!slot.done)slot.session->CheckDeadline();
    }
    NativeChannelResult Serve(){
        if(!listening)return {Code::Failed};
        while(Active()){
            CheckDeadlines();
            Slot* available=nullptr;
            {
                std::unique_lock<std::mutex> lock(mutex);
                for(auto& slot:slots)if(slot.done){available=&slot;break;}
                if(!available){changed.wait_for(lock,std::chrono::milliseconds(200));continue;}
            }
            // done is published by the worker; join before destroying its session.
            if(available->worker.joinable())available->worker.join();
            {std::lock_guard<std::mutex> lock(mutex);available->session.reset();}
            auto socket=std::make_unique<UnixSocketStream>();
            NativeChannelResult accepted;
            do {
                CheckDeadlines();
                accepted=listener.Accept(*socket,std::chrono::steady_clock::now()+std::chrono::milliseconds(200));
            }while(accepted.code==Code::TimedOut&&Active());
            if(!Active())return {Code::Cancelled};
            if(accepted.code!=Code::Ok)return accepted;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if(!Active())return {Code::Cancelled};
                available->session=std::make_unique<SocketSession>(broker,std::move(socket),certificate.value,resolveEnrollment);
                available->done=false;
                available->worker=std::thread([this,available]{
                    available->session->Run();
                    std::lock_guard<std::mutex> completion(mutex);available->done=true;changed.notify_all();
                });
            }
        }
        return {Code::Cancelled};
    }
};
SocketServer::SocketServer(BrokerDispatcher& broker,PCCERT_CONTEXT certificate,std::shared_ptr<const SocketEnrollment> enrollment)
    :impl_(std::make_unique<Impl>(broker,certificate,std::move(enrollment))){}
SocketServer::SocketServer(BrokerDispatcher& broker,PCCERT_CONTEXT certificate,SocketEnrollmentResolver resolver)
    :impl_(std::make_unique<Impl>(broker,certificate,std::move(resolver))){}
SocketServer::~SocketServer(){impl_->Drain();}
std::size_t SocketServer::FixedStorageBytes() noexcept{
    return sizeof(SocketServer)+sizeof(Impl)+2*UnixSocketStream::FixedStorageBytes()-sizeof(UnixSocketStream)+
        4*(SocketSession::FixedStorageBytes()+sizeof(SocketEnrollment));
}
NativeChannelResult SocketServer::Listen(const char* path) noexcept {
    if(impl_->started.load()||impl_->listening)return {Code::Failed};
    if(!impl_->Active())return {Code::Cancelled};
    auto result=impl_->listener.Listen(path);impl_->listening=result.code==Code::Ok;return result;
}
NativeChannelResult SocketServer::Run() noexcept {
    if(impl_->started.exchange(true))return {Code::Failed};
    NativeChannelResult result;
    try{result=impl_->Serve();}catch(...){result={Code::Failed};}
    impl_->Drain();return result;
}
void SocketServer::RequestCancel() noexcept {impl_->Cancel();}
unsigned SocketServer::ActiveConnections() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex);unsigned count=0;
    for(const auto& slot:impl_->slots)if(slot.session&&!slot.done)++count;return count;
}
}
