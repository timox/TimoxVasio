#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>

namespace timox_broker {
// Read-only observations from the local WTS server. The account SID comes from
// the session's WTS name and a local account lookup; it is a cross-check against
// a process token, not a substitute for observing that token.
struct InteractiveSessionObservation {
    DWORD consoleSessionId=MAXDWORD;
    std::uint32_t interactiveLogonCount=0;
    bool consoleUnlocked=false;
    std::string consoleUserSid;
};

// Counts active, connecting, connected, shadowed, disconnected and transitional
// non-service sessions conservatively. Any incomplete WTS observation fails and
// preserves output. `consoleUnlocked` is true only for an explicit WTS unlock
// state on an OS version whose SessionFlags semantics are known. The WTS name to
// SID lookup may consult a domain controller, so this is control-plane work only.
DWORD ObserveInteractiveSessions(InteractiveSessionObservation& output,
    bool resolveConsoleUserSid=true) noexcept;
}
