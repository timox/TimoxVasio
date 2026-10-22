#include "native_audio_manager.h"
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>
#include <limits>

namespace {
void Check(bool ok,const char* why){if(!ok){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
struct Gate {std::mutex mutex;std::condition_variable changed;bool stopped=false;std::atomic<std::uint64_t> now{0};};
class Scheduler final:public INativeAudioScheduler {
    std::shared_ptr<Gate> gate;
public:
    explicit Scheduler(std::shared_ptr<Gate> g=std::make_shared<Gate>()):gate(std::move(g)){}
    std::uint64_t NowNanoseconds()noexcept override{return gate->now;}
    bool WaitUntil(std::uint64_t deadline)noexcept override{std::unique_lock<std::mutex> lock(gate->mutex);gate->changed.wait(lock,[&]{return gate->stopped||gate->now>=deadline;});return !gate->stopped;}
    void RequestStop()noexcept override{std::lock_guard<std::mutex> lock(gate->mutex);gate->stopped=true;gate->changed.notify_all();}
};
struct State {std::atomic<unsigned> opens{0},closes{0},owners{0},maxOwners{0};NativeTransferCode code=NativeTransferCode::Ok;std::atomic<NativeTransferCode> readCode{NativeTransferCode::Empty};};
class Transport final:public INativeAudioTransport {
    std::shared_ptr<State> s;bool owned=false;
public:
    explicit Transport(std::shared_ptr<State> state):s(std::move(state)){}
    NativeTransferResult Open(NativeSessionInfo& session)noexcept override{
        ++s->opens;if(s->code!=NativeTransferCode::Ok)return {s->code,0,5};
        owned=true;const auto owners=++s->owners;if(owners>s->maxOwners)s->maxOwners=owners;session.wireGeneration=7;return {NativeTransferCode::Ok};
    }
    NativeTransferResult ReadRender(std::uint32_t,void*,std::uint32_t)noexcept override{return {s->readCode.load(),0,1617};}
    NativeTransferResult WriteCapture(const void*,std::uint32_t)noexcept override{return {NativeTransferCode::Full};}
    NativeTransferResult ReadStatus(std::uint64_t (&c)[3])noexcept override{c[0]=c[1]=c[2]=0;return {NativeTransferCode::Ok};}
    void RequestCancel()noexcept override{}
    void Close()noexcept override{if(owned){--s->owners;owned=false;}++s->closes;}
};
void ExclusiveReplacement(){
    auto state=std::make_shared<State>();auto epochs=std::make_shared<std::atomic<std::uint64_t>>(1);
    NativeAudioManager manager([state]{return std::make_unique<Transport>(state);},[]{return std::make_unique<Scheduler>();},epochs);
    const auto now=NativeAudioManager::Clock::time_point{};
    manager.SetDesired(true,true,48000,256);
    Check(manager.Inspect(now)==NativeAudioAction::AttemptOpen,"enabled master requests initial native open");
    Check(manager.AttemptOpen(now).code==NativeTransferCode::Ok&&manager.MarkAttached(),"one native generation attached");
    auto first=manager.Generation();Check(first&&first->epoch==1&&first->wireGeneration==7,"independent local epoch");
    manager.SetDesired(true,true,48000,256);Check(manager.Inspect(now)==NativeAudioAction::None,"same format reuses owner");
    manager.SetDesired(true,true,44100,128);Check(manager.Inspect(now)==NativeAudioAction::Detach,"format change requires explicit detach");
    Check(manager.AttemptOpen(now).code!=NativeTransferCode::Ok&&state->opens==1,"cannot open before old owner stops");
    manager.StopAfterQuiescence(now);Check(state->owners==0,"old owner released before replacement");
    Check(manager.Inspect(now)==NativeAudioAction::AttemptOpen&&manager.AttemptOpen(now).code==NativeTransferCode::Ok,"replacement opens after retirement");
    Check(manager.Generation()->epoch==2&&manager.Generation()->wireGeneration==7&&state->maxOwners==1,"wire reuse cannot reuse epoch or overlap owner");
    manager.SetDesired(false,true,44100,128);Check(manager.Inspect(now)==NativeAudioAction::Detach,"disable detaches active backend");manager.StopAfterQuiescence(now);
    Check(manager.Inspect(now)==NativeAudioAction::None,"disabled cannot retry");
}
void RetryAndEpochBounds(){
    using Action=NativeAudioAction;using Code=NativeTransferCode;
    auto now=NativeAudioManager::Clock::time_point{};
    NativeAudioManager missing;missing.SetDesired(true,true,48000,256);
    Check(missing.AttemptOpen(now).code==Code::Unavailable,"missing factory is unavailable, never direct native");
    Check(missing.Inspect(now)==Action::None&&missing.Snapshot(now).retryInMs==1000,"first unavailable backoff1s");
    auto state=std::make_shared<State>();state->code=Code::Unavailable;
    auto epochs=std::make_shared<std::atomic<std::uint64_t>>(10);
    NativeAudioManager manager([state]{return std::make_unique<Transport>(state);},[]{return std::make_unique<Scheduler>();},epochs);
    manager.SetDesired(true,true,48000,256);
    for(const unsigned delay:{1000u,2000u,4000u,8000u,30000u,30000u}){
        Check(manager.AttemptOpen(now).code==Code::Unavailable,"scripted unavailable result retained");
        Check(manager.Inspect(now)==Action::Detach,"failed prepared backend must retire even before attachment");
        const auto opens=state->opens.load();manager.AttemptOpen(now);
        Check(state->opens==opens,"failed object prevents overlapping retry");manager.StopAfterQuiescence(now);
        Check(manager.Snapshot(now).retryInMs==delay,"capped exponential native retry delay");
        manager.SetDesired(true,true,48000,256);
        Check(manager.Inspect(now+std::chrono::milliseconds(delay-1))==Action::None,"same desired config cannot bypass backoff");
        now+=std::chrono::milliseconds(delay);Check(manager.Inspect(now)==Action::AttemptOpen,"retry allowed at deadline");
    }
    manager.SetDesired(false,true,48000,256);Check(manager.Inspect(now)==Action::None,"disable cancels expired retry");
    for(const auto failure:{Code::AccessDenied,Code::OwnedElsewhere,Code::InvalidPacket,Code::Failed,Code::Cancelled}){
        auto denied=std::make_shared<State>();denied->code=failure;
        NativeAudioManager m([denied]{return std::make_unique<Transport>(denied);},[]{return std::make_unique<Scheduler>();});
        m.SetDesired(true,true,48000,256);Check(m.AttemptOpen(now).code==failure,"typed terminal failure retained");m.StopAfterQuiescence(now);
        Check(m.Inspect(now+std::chrono::hours(24))==Action::None&&m.Snapshot(now).lastResult.systemError==5,"denied/protocol/ownership/cancel never auto-retry");
    }
    state->code=Code::Ok;
    std::uint64_t first=0;
    for(unsigned i=0;i<2;++i){
        NativeAudioManager m([state]{return std::make_unique<Transport>(state);},[]{return std::make_unique<Scheduler>();},epochs);
        m.SetDesired(true,true,48000,256);Check(m.AttemptOpen(now).code==Code::Ok,"replacement manager opens");
        const auto snapshot=m.Snapshot(now);Check(!snapshot.attached&&snapshot.health.state==NativeAudioState::Running,"prepared worker health distinct from graph attachment");
        if(i)Check(snapshot.epoch>first&&snapshot.wireGeneration==7,"epoch allocator survives manager replacement");else first=snapshot.epoch;
        m.StopAfterQuiescence(now);
    }
    epochs->store((std::numeric_limits<std::uint64_t>::max)());
    NativeAudioManager exhausted([state]{return std::make_unique<Transport>(state);},[]{return std::make_unique<Scheduler>();},epochs);
    exhausted.SetDesired(true,true,48000,256);const auto opens=state->opens.load();
    Check(exhausted.AttemptOpen(now).code==Code::Failed&&state->opens==opens&&epochs->load()==(std::numeric_limits<std::uint64_t>::max)(),"epoch exhaustion rejected without wrap or transport open");
    NativeAudioManager invalid;invalid.SetDesired(true,true,384000,256);
    Check(invalid.Inspect(now)==Action::None&&invalid.Snapshot(now).lastResult.code==Code::InvalidPacket,"unsupported reserve rejected before open");
}
void RunningFailureAndPublicationRejection(){
    using Action=NativeAudioAction;using Code=NativeTransferCode;
    const auto now=NativeAudioManager::Clock::time_point{};
    auto state=std::make_shared<State>();std::shared_ptr<Gate> gate;
    NativeAudioManager manager([state]{return std::make_unique<Transport>(state);},[&]{gate=std::make_shared<Gate>();return std::make_unique<Scheduler>(gate);});
    manager.SetDesired(true,true,48000,256);Check(manager.AttemptOpen(now).code==Code::Ok&&manager.MarkAttached(),"running failure fixture attaches");
    auto borrowed=manager.Generation();std::weak_ptr<NativeAudioGeneration> lifetime=borrowed;const auto epoch=borrowed->epoch;
    state->readCode=Code::Removed;
    // The worker may not have reached its first wait yet: advance until it
    // observes removal, without relying on scheduling order after Start.
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(manager.Snapshot(now).health.state!=NativeAudioState::Failed&&std::chrono::steady_clock::now()<deadline){
        {std::lock_guard<std::mutex> lock(gate->mutex);gate->now+=5000000;gate->changed.notify_all();}
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(manager.Inspect(now)==Action::Detach&&manager.Snapshot(now).attached&&!lifetime.expired(),"worker removal retains borrowed generation until owner detach");
    const auto opens=state->opens.load();manager.AttemptOpen(now);Check(state->opens==opens,"failed active object cannot be replaced before retirement");
    manager.StopAfterQuiescence(now);Check(!manager.Snapshot(now).attached&&!manager.Generation()&&!lifetime.expired(),"retirement cannot destroy separately retained graph generation");
    borrowed.reset();Check(lifetime.expired(),"generation released after final graph owner");
    Check(manager.Snapshot(now).retryInMs==1000&&manager.Snapshot(now).lastResult.systemError==1617,"Removed schedules typed1s retry");
    state->readCode=Code::Empty;Check(manager.AttemptOpen(now+std::chrono::seconds(1)).code==Code::Ok&&manager.Generation()->epoch>epoch,"removed backend gets fresh generation");
    manager.MarkAttached();manager.SetDesired(true,false,48000,256);Check(manager.Inspect(now)==Action::Detach,"master stop requires detach");manager.StopAfterQuiescence(now);
    Check(manager.Inspect(now+std::chrono::hours(1))==Action::None&&manager.Snapshot(now).state==NativeAudioManagerState::WaitingForMaster,"no reconnect without running master");
    manager.SetDesired(true,true,48000,256);Check(manager.AttemptOpen(now).code==Code::Ok,"master resumption permits fresh open");
    manager.StopAfterQuiescence(now,{Code::InvalidPacket,0,87});
    Check(manager.Inspect(now+std::chrono::hours(1))==Action::None&&manager.Snapshot(now).lastResult.systemError==87,"graph publication rejection cannot spin reconnect");
}
}
int main(){ExclusiveReplacement();RetryAndEpochBounds();RunningFailureAndPublicationRejection();std::cout<<"Native manager tests passed\n";}
