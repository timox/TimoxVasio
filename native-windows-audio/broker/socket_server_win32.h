#pragma once
#include "socket_session_win32.h"
namespace timox_broker {
// One listener, at most four session workers, one shared dispatcher. Host owns
// protected path discovery/removal and trusted enrollment. No SCM installation.
// Listen once before Run; Run once on the acceptor thread. RequestCancel and
// ActiveConnections are concurrent. Join Run before destruction/policy update;
// dispatcher outlives server. Cancellation is terminal, including before Run.
class SocketServer final {
public:
    SocketServer(BrokerDispatcher&,PCCERT_CONTEXT,std::shared_ptr<const SocketEnrollment>);
    ~SocketServer();
    NativeChannelResult Listen(const char* trustedPath) noexcept;
    NativeChannelResult Run() noexcept;
    void RequestCancel() noexcept;
    unsigned ActiveConnections() const noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
