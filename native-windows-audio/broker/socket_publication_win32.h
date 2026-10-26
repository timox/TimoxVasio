#pragma once
#include "socket_discovery.h"
#include <Windows.h>

namespace timox_broker {
// Allocates a canonical file descriptor, owned by exact broker service SID.
// Output must be null; caller LocalFree's it. Does not modify token or any ACL.
DWORD CreateSocketPublicationSecurity(PSECURITY_DESCRIPTOR& output) noexcept;
// Service worker only. Caller serializes publishers and has already acquired the
// installation root. Revalidates/pins root, refuses unsafe existing publication,
// creates a fresh protected temp file, writes+flushes, then replaces discovery.bin
// by handle-relative rename. No directory provisioning or ownership repair.
// Publish only after listener is ready, with a trusted certificate/enrollment.
// Success is namespace publication, not crash durability, freshness or enrollment
// proof. A reader-held target can return sharing violation OR access denied
// (observed on Windows); host bounds retries, never repairs ACLs to force a write.
DWORD PublishSocketDiscoveryDirectory(HANDLE root,const SocketDiscovery&) noexcept;
#ifdef TIMOX_NATIVE_DISCOVERY_TESTS
// Raw filesystem transaction only: bypasses trust policy for workspace fixtures.
// Not compiled into the production libraries. Null SD uses the test user's ACL.
DWORD PublishSocketDiscoveryForTest(HANDLE,const SocketDiscovery&,PSECURITY_DESCRIPTOR) noexcept;
#endif
}
