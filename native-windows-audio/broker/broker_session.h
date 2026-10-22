#pragma once
#include "native_audio_access_policy.h"
#include "native_audio_broker_protocol.h"
#include "native_audio_transport.h"
#include <atomic>
#include <functional>
#include <memory>

namespace timox_broker {
using ConnectionId=std::uint64_t;
struct BrokerIdAllocators {
    // Next unused values. Zero/MAX mean exhausted; neither allocator wraps.
    std::atomic<std::uint64_t> connections{1},leases{1};
};
struct AttachedConnection { ConnectionId id=0;Status status=Status::Failed; };
enum class Dispatch { Reply, CloseConnection, InvalidBuffer };
struct DispatchResult { Dispatch disposition=Dispatch::CloseConnection;std::uint32_t bytes=0;Status status=Status::Failed; };

// One instance arbitrates the sole native owner across <=4 authenticated
// connections. All methods except RequestCancel belong to one service worker;
// none belongs to the graph callback. Authentication and OS identity binding
// MUST already be verified before AttachAuthenticated. This class provides no
// TLS/pipe authentication and does not qualify endpoint isolation.
// One instance must be shared by all service connections; constructing several
// arbiters does not create global exclusivity across them. Allocate this object
// off RT. Its retained audio storage is one MaxPayloadBytes capture retry packet.
class BrokerSession final {
public:
    using Factory=std::function<std::unique_ptr<INativeAudioTransport>()>;
    explicit BrokerSession(Factory factory,
        std::shared_ptr<BrokerIdAllocators> ids={});
    ~BrokerSession();
    BrokerSession(const BrokerSession&)=delete;
    BrokerSession& operator=(const BrokerSession&)=delete;
    AttachedConnection AttachAuthenticated(const NativePeerIdentity&) noexcept;
    // The broker owns the active-lease identity; the snapshot's optional active
    // identity is replaced by that owner before policy evaluation.
    void UpdatePolicy(const NativeSessionPolicySnapshot&) noexcept;
    // Input is one complete stable message, separate from response storage.
    // Output capacity must be >=MaxMessageBytes before any state/transport work.
    // Caller retains at most this one response until delivered; no pipelining.
    // A syntax/sequence/ownership violation disconnects that connection only.
    // Channel owner must discard any unsent reply when its connection is revoked;
    // a completed Process call cannot retract bytes already given to the channel.
    DispatchResult Process(ConnectionId,const void* request,std::size_t requestBytes,
        void* response,std::size_t responseCapacity) noexcept;
    void Disconnect(ConnectionId) noexcept;
    // Concurrent/sticky for this connection, including Open: transport pointer
    // is published before Open. No cancellation mutex spans a blocking Open,
    // Read/Write/Close. Caller keeps BrokerSession alive through this call.
    // To retire after a policy change during a blocked operation, the future
    // service first requests cancellation, waits for worker completion, then
    // invokes UpdatePolicy/Disconnect. UpdatePolicy itself is worker-only.
    // Transport RequestCancel must be bounded/nonblocking. Destructor and all
    // worker methods must not race another worker method or destruction.
    void RequestCancel(ConnectionId) noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace timox_broker
