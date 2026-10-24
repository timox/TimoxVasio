#pragma once
#include <Windows.h>

namespace timox_broker {
// Ancestor policy: trusted Windows owner, nonnull DACL, no effective grant to
// another principal permitting DELETE, DELETE_CHILD, WRITE_DAC or WRITE_OWNER.
// Creation alone is permitted; every existing child is separately validated.
DWORD ValidateSocketDiscoveryAncestorSecurity(PSECURITY_DESCRIPTOR) noexcept;
// Read-only acquisition from the Windows known-folder path, component by
// component using held handles. Global DOS volume mapping, local NTFS only;
// rejects reparse points, unsupported paths and unsafe ancestor descriptors.
// This returns an ancestor, NOT a publication directory or service credential.
// Output must be INVALID_HANDLE_VALUE; caller owns successful handle.
DWORD OpenSocketDiscoveryProgramData(HANDLE& output) noexcept;
// Fixed ProgramData\TimoxVasio\NativeAudio. Both private directories must meet
// the canonical publication-directory policy. Never creates/repairs anything.
// Returned directory is held against rename and can be supplied to
// ReadSocketDiscoveryDirectory. Does not prove epoch freshness or enrollment.
DWORD OpenInstalledSocketDiscoveryRoot(HANDLE& output) noexcept;
// Full read-only path acquisition + directory/file validation + bounded decode.
// Output preserved on every failure; no fallback to a user-provided location.
DWORD ReadInstalledSocketDiscovery(struct SocketDiscovery& output) noexcept;
}
