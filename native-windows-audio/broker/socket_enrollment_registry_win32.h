#pragma once
#include "socket_session_win32.h"

namespace timox_broker {
// Reads the machine-provisioned user SID and client certificate SHA-256 pin
// from HKLM Runtime. The returned resolver compares them with the OS identity
// observed on each accepted socket; it never accepts enrollment from the wire.
SocketEnrollmentResolver CreateRegistrySocketEnrollmentResolver();
}
