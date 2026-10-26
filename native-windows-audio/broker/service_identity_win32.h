#pragma once
#include <Windows.h>
namespace timox_broker {
struct BrokerServiceIdentity {
    bool localService=false;
    DWORD sessionId=0xffffffffu;
    DWORD serviceGroupAttributes=0;
};
// Read process token only; no impersonation/token mutation. Refuses a caller
// already impersonating. Failure preserves output. Group is the exact fixed
// TimoxNativeAudioBroker SID; user supplied observations cannot authorize work.
DWORD ObserveBrokerServiceIdentity(BrokerServiceIdentity&) noexcept;
bool IsBrokerServiceIdentity(const BrokerServiceIdentity&) noexcept;
}
