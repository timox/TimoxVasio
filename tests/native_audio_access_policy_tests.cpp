#include "native_audio_access_policy.h"
#include <iostream>
#include <stdexcept>
#include <limits>

namespace {
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
NativePeerIdentity Alice(){return {"S-1-5-21-100-200-300-1001","S-1-5-5-12-34",0x12345678,2};}
NativeSessionPolicySnapshot QualifiedFixture(){
    NativeSessionPolicySnapshot p;p.allowedUserSid=Alice().userSid;p.consoleSessionId=2;
    p.interactiveLogonCount=1;p.unlocked=true;p.audioIsolationQualified=true;
    p.optionalConsoleIdentity=Alice();return p;
}
void AdmissionRefusals(){
    using A=NativeAdmission;const auto alice=Alice();auto p=QualifiedFixture();
    Check(EvaluateNativeAdmission(alice,p)==A::Allowed,"qualified single console identity refused");
    auto peer=alice;peer.userSid="S-1-5-21-100-200-300-1002";
    Check(EvaluateNativeAdmission(peer,p)==A::WrongUser,"another account admitted");
    peer=alice;peer.logonSid="S-1-5-5-12-35";
    Check(EvaluateNativeAdmission(peer,p)==A::WrongLogon,"same user different logon admitted without active lease");
    peer=alice;++peer.authenticationId;
    Check(EvaluateNativeAdmission(peer,p)==A::WrongLogon,"same session reused with old authentication identity");
    peer=alice;peer.authenticationId+=std::uint64_t{1}<<32;
    Check(EvaluateNativeAdmission(peer,p)==A::WrongLogon,"authentication high bits were truncated");
    peer=alice;peer.sessionId=3;
    Check(EvaluateNativeAdmission(peer,p)==A::UnsupportedSession,"remote/nonconsole session admitted");
    peer.sessionId=0;Check(EvaluateNativeAdmission(peer,p)==A::UnsupportedSession,"session zero admitted");
    peer.sessionId=(std::numeric_limits<std::uint32_t>::max)();
    Check(EvaluateNativeAdmission(peer,p)==A::UnsupportedSession,"invalid session sentinel admitted");
    for(unsigned field=0;field<3;++field){peer=alice;if(field==0)peer.userSid.clear();if(field==1)peer.logonSid.clear();if(field==2)peer.authenticationId=0;
        Check(EvaluateNativeAdmission(peer,p)==A::IncompleteIdentity,"incomplete peer admitted");}
    p=QualifiedFixture();p.allowedUserSid.clear();Check(EvaluateNativeAdmission(alice,p)==A::IncompleteIdentity,"missing provisioned identity admitted");
    p=QualifiedFixture();p.optionalConsoleIdentity.reset();Check(EvaluateNativeAdmission(alice,p)==A::IncompleteIdentity,"unobserved console identity admitted");
    p=QualifiedFixture();p.optionalConsoleIdentity->logonSid.clear();Check(EvaluateNativeAdmission(alice,p)==A::IncompleteIdentity,"incomplete console observation admitted");
    p=QualifiedFixture();p.optionalConsoleIdentity->sessionId=3;Check(EvaluateNativeAdmission(alice,p)==A::UnsupportedSession,"incoherent console snapshot admitted");
    p=QualifiedFixture();p.optionalConsoleIdentity->userSid="S-1-5-21-100-200-300-1002";Check(EvaluateNativeAdmission(alice,p)==A::WrongLogon,"different current console account admitted");
    for(auto count:{0u,2u,0xffffffffu}){p=QualifiedFixture();p.interactiveLogonCount=count;
        Check(EvaluateNativeAdmission(alice,p)==A::UnsupportedSession,"not exactly one interactive logon");}
    p=QualifiedFixture();p.unlocked=false;Check(EvaluateNativeAdmission(alice,p)==A::Locked,"locked console admitted");
    p=QualifiedFixture();p.audioIsolationQualified=false;Check(EvaluateNativeAdmission(alice,p)==A::IsolationUnqualified,"console exclusivity treated as isolation proof");
    p=QualifiedFixture();p.optionalActiveLeaseIdentity=alice;
    Check(EvaluateNativeAdmission(alice,p)==A::Allowed,"same identity eligibility unexpectedly denied");
    ++p.optionalActiveLeaseIdentity->authenticationId;
    Check(EvaluateNativeAdmission(alice,p)==A::WrongLogon,"different active lease admitted");
    p.optionalActiveLeaseIdentity->userSid.clear();Check(EvaluateNativeAdmission(alice,p)==A::IncompleteIdentity,"malformed lease observation admitted");
}
void TransitionsRetireOriginalLease(){
    using A=NativeAdmission;const auto original=Alice();auto p=QualifiedFixture();p.optionalActiveLeaseIdentity=original;
    auto check=[&](NativeAdmission reason){const auto d=EvaluateNativeLeaseContinuation(original,p);
        Check(d.action==NativeLeaseAction::Retire && d.reason==reason,"invalid lease transition was kept or misclassified");};
    auto d=EvaluateNativeLeaseContinuation(original,p);Check(d.action==NativeLeaseAction::Keep&&d.reason==A::Allowed,"unchanged lease retired");
    p.unlocked=false;check(A::Locked);p.unlocked=true;
    p.audioIsolationQualified=false;check(A::IsolationUnqualified);p.audioIsolationQualified=true;
    p.interactiveLogonCount=2;check(A::UnsupportedSession);p.interactiveLogonCount=1;
    p.consoleSessionId=3;check(A::UnsupportedSession);p.consoleSessionId=2;
    p.allowedUserSid="S-1-5-21-100-200-300-1002";check(A::WrongUser);p.allowedUserSid=original.userSid;
    // Same numeric SessionId and same user after logoff/login must not keep the
    // old owner: authentication LUID and logon SID belong to a different logon.
    ++p.optionalConsoleIdentity->authenticationId;check(A::WrongLogon);
    p.optionalConsoleIdentity=original;p.optionalConsoleIdentity->logonSid="S-1-5-5-12-35";check(A::WrongLogon);
    p.optionalConsoleIdentity.reset();check(A::IncompleteIdentity);p.optionalConsoleIdentity=original;
    p.optionalActiveLeaseIdentity.reset();check(A::LeaseNotActive);
    p.optionalActiveLeaseIdentity=original;++p.optionalActiveLeaseIdentity->authenticationId;check(A::WrongLogon);
    // Decisions do not mutate or replace the granted identity/snapshot.
    Check(original.authenticationId==0x12345678 && p.optionalActiveLeaseIdentity->authenticationId==0x12345679,"policy mutated ownership");
}
}
int main(){try{AdmissionRefusals();TransitionsRetireOriginalLease();std::cout<<"Native access policy tests passed (synthetic identities; no isolation qualification)\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
