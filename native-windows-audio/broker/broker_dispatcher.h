#pragma once
#include "broker_session.h"

namespace timox_broker {
// One dispatcher per service, shared by <=4 connection I/O workers. Synchronous
// submissions retain caller buffers until completion; no socket I/O on this
// worker. Only authenticated OS identities may be attached. No audio callback use.
// One Process/Disconnect submission at a time per connection; RequestCancel and
// the single control caller may run concurrently. Response storage is exactly
// MaxMessageBytes and is distinct from the request, matching BrokerSession.
class BrokerDispatcher final {
public:
    BrokerDispatcher(BrokerSession::Factory,const NativeSessionPolicySnapshot& initialPolicy);
    ~BrokerDispatcher();
    BrokerDispatcher(const BrokerDispatcher&)=delete;
    BrokerDispatcher& operator=(const BrokerDispatcher&)=delete;
    AttachedConnection AttachAuthenticated(const NativePeerIdentity&) noexcept;
    DispatchResult Process(ConnectionId,const void*,std::size_t,void*,std::size_t) noexcept;
    void Disconnect(ConnectionId) noexcept;
    // Sticky for this ID; prevents returning queued/in-flight responses. The
    // service must also cancel that connection's pipe/TLS I/O: a response already
    // returned to its caller cannot be retracted by this dispatcher.
    void RequestCancel(ConnectionId) noexcept;
    // Conservative transition: revoke all old connections, then install policy.
    // Single control caller; cancel pipe I/O before calling. Reauthentication is
    // required even if a new policy would permit the old peer.
    void RevokeAllAndUpdatePolicy(const NativeSessionPolicySnapshot&) noexcept;
    // Single control caller. Drains worker and cancels native operations; callers
    // must return/join before destruction. Stop is terminal and idempotent.
    void Stop() noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
