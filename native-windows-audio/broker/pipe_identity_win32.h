#pragma once
#include "channel_binding.h"
#include <Windows.h>
namespace timox_broker {
// Worker-only, returns ERROR_SUCCESS or a Windows error; output unchanged on failure.
// These observations do not grant admission or prove endpoint audio isolation.
DWORD ObserveProcessIdentity(ChannelIdentity&) noexcept;
// Call after reading the client's binding on this server pipe. The dedicated
// worker must not already impersonate. Reverts before querying the captured token;
// inability to revert terminates the process (never continue under client identity).
DWORD ObservePipeIdentity(HANDLE serverPipe,ChannelIdentity&) noexcept;
}
