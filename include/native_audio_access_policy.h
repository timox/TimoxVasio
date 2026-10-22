#pragma once
#include <cstdint>
#include <optional>
#include <string>

// Trusted OS observations only: canonical SID strings extracted from verified
// tokens, never fields asserted by a pipe/HTTP client. This pure layer does not
// authenticate tokens, parse SID syntax, inspect Windows or prove isolation.
struct NativePeerIdentity {
    std::string userSid,logonSid;
    std::uint64_t authenticationId=0;
    std::uint32_t sessionId=0;
};
struct NativeSessionPolicySnapshot {
    std::string allowedUserSid;
    std::uint32_t consoleSessionId=0,interactiveLogonCount=0;
    bool unlocked=false,audioIsolationQualified=false;
    // Full current console logon prevents SessionId reuse admitting an old token.
    std::optional<NativePeerIdentity> optionalConsoleIdentity;
    std::optional<NativePeerIdentity> optionalActiveLeaseIdentity;
};
enum class NativeAdmission { Allowed, WrongUser, WrongLogon, UnsupportedSession,
    Locked, IsolationUnqualified, IncompleteIdentity, LeaseNotActive };
enum class NativeLeaseAction { Keep, Retire };
struct NativeLeaseDecision { NativeLeaseAction action; NativeAdmission reason; };

// Initial conservative console-only policy: exactly one interactive logon,
// unlocked, explicit allowed user and independently qualified endpoint isolation.
// Allowed is identity/policy eligibility only. The broker must separately enforce
// one connection owner: another connection with the SAME identity cannot steal a
// lease. These functions neither create nor mutate leases and never allocate.
NativeAdmission EvaluateNativeAdmission(const NativePeerIdentity&,
    const NativeSessionPolicySnapshot&) noexcept;
// Reevaluate the ORIGINAL granted identity on every observed policy transition.
// The caller must retire/quiesce endpoints and transport on Retire, before any
// new admission. Absence of an active lease is explicitly a retirement decision.
NativeLeaseDecision EvaluateNativeLeaseContinuation(const NativePeerIdentity& granted,
    const NativeSessionPolicySnapshot&) noexcept;
