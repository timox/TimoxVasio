#pragma once
#include "socket_discovery.h"
#include <Windows.h>

namespace timox_broker {
// Canonical PUBLIC metadata file policy, not client key policy. Descriptor must
// refer to valid Windows-allocated memory. No access is granted/ACLs changed.
// Protected DACL: SYSTEM, Administrators, exact broker service SID full control;
// Authenticated Users read only. Owner must be one of those three writers.
DWORD ValidateSocketDiscoverySecurity(PSECURITY_DESCRIPTOR) noexcept;
// Worker-only: reopens the same file object with read-only sharing, validates
// its type, link count, size and security descriptor, then decodes exactly 72
// bytes. Never reopens by pathname; borrowed handle and output preserved on error.
// THIS IS NOT PATH DISCOVERY: host must establish a trusted local directory chain
// and publication provenance. A valid ACL does not prove freshness or distinguish
// an old trusted file substituted through an untrusted parent. No runtime wiring
// is authorized by this helper. Caller must keep the original handle alive.
DWORD ReadSocketDiscoveryHandle(HANDLE, SocketDiscovery&) noexcept;
}
