#pragma once
#include "broker_dispatcher.h"
#include "channel_binding.h"
#include "schannel_stream_win32.h"
#include "unix_socket_stream_win32.h"
#include <atomic>

namespace timox_broker {
// Trusted host input, never enrollment from the wire. Production creation must
// be backed by a qualified OS provisioner. Tests inject synthetic enrollments.
// Revoke is terminal. Host must ALSO RequestCancel every associated session to
// interrupt pending native/socket operations, before updating dispatcher policy.
class SocketEnrollment final {
public:
    const SchannelStream::Pin certificate;
    const ChannelIdentity identity;
    const std::uint64_t epoch;
    SocketEnrollment(const SchannelStream::Pin&,const ChannelIdentity&,std::uint64_t);
    bool Active() const noexcept {return !revoked_.load(std::memory_order_acquire);}
    void Revoke() noexcept {revoked_.store(true,std::memory_order_release);}
private:
    std::atomic<bool> revoked_{false};
};
// One accepted AF_UNIX connection, one TLS stream, one dispatcher registration.
// Run once on a dedicated I/O worker; only RequestCancel may run concurrently.
// Host bounds handlers to four and joins all calls before destruction. Dispatcher
// outlives sessions. No listener, provisioning, or physical peer-token assertion.
// Socket reads/handshake/confirmation use absolute deadlines; host cancellation
// is needed to interrupt a backend blocked inside the dispatcher.
class SocketSession final {
public:
    SocketSession(BrokerDispatcher&,std::unique_ptr<UnixSocketStream>,PCCERT_CONTEXT,
        std::shared_ptr<const SocketEnrollment>);
    ~SocketSession();
    NativeChannelResult Run() noexcept;
    void RequestCancel() noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
