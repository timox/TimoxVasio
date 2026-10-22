#include "native_audio_access_policy.h"
#include <limits>

namespace {
bool Complete(const NativePeerIdentity& identity) noexcept {
    return !identity.userSid.empty() && !identity.logonSid.empty() && identity.authenticationId!=0;
}
bool SameLogon(const NativePeerIdentity& a,const NativePeerIdentity& b) noexcept {
    return a.userSid==b.userSid && a.logonSid==b.logonSid &&
        a.authenticationId==b.authenticationId && a.sessionId==b.sessionId;
}
bool InteractiveSession(std::uint32_t id) noexcept {
    return id!=0 && id!=(std::numeric_limits<std::uint32_t>::max)();
}
}
NativeAdmission EvaluateNativeAdmission(const NativePeerIdentity& peer,
    const NativeSessionPolicySnapshot& policy) noexcept {
    // Refusal precedence is deterministic. All observations are one coherent
    // control-owner snapshot; this function cannot close a notification race.
    if(!Complete(peer) || policy.allowedUserSid.empty() || !policy.optionalConsoleIdentity ||
       !Complete(*policy.optionalConsoleIdentity) ||
       (policy.optionalActiveLeaseIdentity && !Complete(*policy.optionalActiveLeaseIdentity)))
        return NativeAdmission::IncompleteIdentity;
    if(peer.userSid!=policy.allowedUserSid)return NativeAdmission::WrongUser;
    if(!InteractiveSession(peer.sessionId) || !InteractiveSession(policy.consoleSessionId) ||
       peer.sessionId!=policy.consoleSessionId || policy.interactiveLogonCount!=1 ||
       policy.optionalConsoleIdentity->sessionId!=policy.consoleSessionId)
        return NativeAdmission::UnsupportedSession;
    if(!policy.unlocked)return NativeAdmission::Locked;
    if(!policy.audioIsolationQualified)return NativeAdmission::IsolationUnqualified;
    if(!SameLogon(peer,*policy.optionalConsoleIdentity) ||
       (policy.optionalActiveLeaseIdentity && !SameLogon(peer,*policy.optionalActiveLeaseIdentity)))
        return NativeAdmission::WrongLogon;
    return NativeAdmission::Allowed;
}
NativeLeaseDecision EvaluateNativeLeaseContinuation(const NativePeerIdentity& granted,
    const NativeSessionPolicySnapshot& policy) noexcept {
    if(!policy.optionalActiveLeaseIdentity)
        return {NativeLeaseAction::Retire,NativeAdmission::LeaseNotActive};
    const auto admission=EvaluateNativeAdmission(granted,policy);
    return {admission==NativeAdmission::Allowed?NativeLeaseAction::Keep:NativeLeaseAction::Retire,admission};
}
