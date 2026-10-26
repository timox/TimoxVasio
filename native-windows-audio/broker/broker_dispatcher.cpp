#include "broker_dispatcher.h"
#include <array>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
namespace timox_broker {
struct BrokerDispatcher::Impl {
    enum class Kind {Attach,Process,Disconnect,Policy};
    struct Job {
        Kind kind;ConnectionId id=0;const NativePeerIdentity* peer=nullptr;
        const NativeSessionPolicySnapshot* policy=nullptr;
        const void* request=nullptr;std::size_t requestBytes=0;void* response=nullptr;std::size_t capacity=0;
        AttachedConnection attached{};DispatchResult result{};bool done=false;
    };
    struct Registration {ConnectionId id=0;bool revoked=false;};
    BrokerSession broker;
    NativeSessionPolicySnapshot initial;
    std::mutex mutex;std::condition_variable changed;
    std::array<Job*,4> queue{};std::size_t head=0,count=0;
    std::array<Registration,4> registrations{};
    bool stopping=false,policyPending=false;
    // Last member: all state must exist before this thread can enter Run.
    std::thread worker;
    Impl(BrokerSession::Factory factory,const NativeSessionPolicySnapshot& policy)
        :broker(std::move(factory)),initial(policy),worker([this]{Run();}){}
    Registration* Find(ConnectionId id) noexcept {
        if(id)for(auto& entry:registrations)if(entry.id==id)return &entry;return nullptr;
    }
    bool Suppressed(ConnectionId id) noexcept {
        const auto* entry=Find(id);return stopping||policyPending||!entry||entry->revoked;
    }
    static void Discard(Job& job) noexcept {
        job.result={Dispatch::CloseConnection,0,Status::Cancelled};
        if(job.response&&job.capacity<=MaxMessageBytes)std::memset(job.response,0,job.capacity);
    }
    bool Submit(Job& job) noexcept {
        std::unique_lock<std::mutex> lock(mutex);
        const auto rejected=[&]{return stopping||(policyPending&&(job.kind==Kind::Attach||job.kind==Kind::Process));};
        changed.wait(lock,[&]{return rejected()||count<queue.size();});
        if(rejected()){if(job.kind==Kind::Process)Discard(job);return false;}
        queue[(head+count)%queue.size()]=&job;++count;changed.notify_all();
        changed.wait(lock,[&]{return job.done;});
        // The cancellation decision is checked while the consumer owns the
        // mutex too, not only when the worker publishes its completion.
        if(job.kind==Kind::Process&&Suppressed(job.id))Discard(job);
        if(job.kind==Kind::Attach&&job.attached.id&&Suppressed(job.attached.id))job.attached={};
        return true;
    }
    void Attach(Job& job) noexcept {
        Registration* slot=nullptr;
        {std::lock_guard<std::mutex> lock(mutex);
            if(stopping||policyPending)return;
            for(auto& entry:registrations)if(!entry.id){slot=&entry;break;}}
        if(!slot){job.attached={0,Status::Full};return;}
        auto attached=broker.AttachAuthenticated(*job.peer);
        bool discard=false;
        {std::lock_guard<std::mutex> lock(mutex);
            discard=stopping||policyPending;
            if(!discard&&attached.id){*slot={attached.id,false};job.attached=attached;}
            else if(!discard)job.attached=attached;}
        if(discard&&attached.id)broker.Disconnect(attached.id);
    }
    void Process(Job& job) noexcept {
        {std::lock_guard<std::mutex> lock(mutex);if(Suppressed(job.id)){Discard(job);return;}}
        job.result=broker.Process(job.id,job.request,job.requestBytes,job.response,job.capacity);
        bool retire=false;
        {std::lock_guard<std::mutex> lock(mutex);
            if(Suppressed(job.id)){Discard(job);retire=true;}
            if(job.result.disposition==Dispatch::CloseConnection){
                if(auto* entry=Find(job.id))entry->revoked=true;retire=true;
            }}
        if(retire)broker.Disconnect(job.id);
    }
    void Disconnect(Job& job) noexcept {
        broker.Disconnect(job.id);
        std::lock_guard<std::mutex> lock(mutex);if(auto* entry=Find(job.id))*entry={};
    }
    void Policy(Job& job) noexcept {
        std::array<ConnectionId,4> ids{};
        {std::lock_guard<std::mutex> lock(mutex);for(std::size_t n=0;n<ids.size();++n)ids[n]=registrations[n].id;}
        for(auto id:ids)if(id)broker.Disconnect(id);
        broker.UpdatePolicy(*job.policy);
        std::lock_guard<std::mutex> lock(mutex);registrations={};policyPending=false;
    }
    void Run() noexcept {
        broker.UpdatePolicy(initial);
        for(;;){
            Job* job=nullptr;bool skip=false;
            {std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock,[&]{return stopping||count;});
                if(!count&&stopping)break;
                job=queue[head];queue[head]=nullptr;head=(head+1)%queue.size();--count;
                skip=stopping;changed.notify_all();}
            if(skip){if(job->kind==Kind::Process)Discard(*job);}
            else switch(job->kind){case Kind::Attach:Attach(*job);break;case Kind::Process:Process(*job);break;
                case Kind::Disconnect:Disconnect(*job);break;case Kind::Policy:Policy(*job);break;}
            {std::lock_guard<std::mutex> lock(mutex);
                // Cancellation may arrive after Process checked the result but
                // before the waiting caller can regain this mutex.
                if(job->kind==Kind::Process&&Suppressed(job->id))Discard(*job);
                job->done=true;changed.notify_all();}
        }
        // No submissions can now enter. Retirement stays on the broker worker.
        for(const auto& entry:registrations)if(entry.id)broker.Disconnect(entry.id);
    }
    void Cancel(ConnectionId id) noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        if(auto* entry=Find(id)){entry->revoked=true;broker.RequestCancel(id);}
        changed.notify_all();
    }
    void Update(const NativeSessionPolicySnapshot& policy) noexcept {
        {std::lock_guard<std::mutex> lock(mutex);
            if(stopping)return;policyPending=true;
            for(auto& entry:registrations)if(entry.id){entry.revoked=true;broker.RequestCancel(entry.id);}
            changed.notify_all();}
        Job job{Kind::Policy};job.policy=&policy;Submit(job);
    }
    void Stop() noexcept {
        {std::lock_guard<std::mutex> lock(mutex);stopping=true;
            for(auto& entry:registrations)if(entry.id){entry.revoked=true;broker.RequestCancel(entry.id);}
            changed.notify_all();}
        if(worker.joinable())worker.join();
    }
};
BrokerDispatcher::BrokerDispatcher(BrokerSession::Factory factory,const NativeSessionPolicySnapshot& policy):impl_(std::make_unique<Impl>(std::move(factory),policy)){}
BrokerDispatcher::~BrokerDispatcher(){impl_->Stop();}
AttachedConnection BrokerDispatcher::AttachAuthenticated(const NativePeerIdentity& peer)noexcept{
    Impl::Job job{Impl::Kind::Attach};job.peer=&peer;impl_->Submit(job);return job.attached;
}
DispatchResult BrokerDispatcher::Process(ConnectionId id,const void* request,std::size_t bytes,void* response,std::size_t capacity)noexcept{
    if(!request||bytes<HeaderBytes||bytes>MaxMessageBytes||!response||capacity!=MaxMessageBytes){
        if(response&&capacity<=MaxMessageBytes)std::memset(response,0,capacity);
        return {Dispatch::InvalidBuffer,0,Status::InvalidPacket};
    }
    Impl::Job job{Impl::Kind::Process};job.id=id;job.request=request;job.requestBytes=bytes;job.response=response;job.capacity=capacity;
    impl_->Submit(job);return job.result;
}
void BrokerDispatcher::Disconnect(ConnectionId id)noexcept{Impl::Job job{Impl::Kind::Disconnect};job.id=id;impl_->Submit(job);}
void BrokerDispatcher::RequestCancel(ConnectionId id)noexcept{impl_->Cancel(id);}
void BrokerDispatcher::RevokeAllAndUpdatePolicy(const NativeSessionPolicySnapshot& policy)noexcept{impl_->Update(policy);}
void BrokerDispatcher::Stop()noexcept{impl_->Stop();}
}
