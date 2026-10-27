#include "session_observation_win32.h"
#include <WtsApi32.h>
#include <winternl.h>
#include <sddl.h>
#include <array>
#include <cwchar>
#include <string>
#include <limits>
#include <utility>

extern "C" NTSYSAPI NTSTATUS NTAPI RtlGetVersion(PRTL_OSVERSIONINFOW);

namespace timox_broker {
namespace {
struct WtsBuffer {
    void* value=nullptr;
    ~WtsBuffer(){if(value)WTSFreeMemory(value);}
};
bool PossibleInteractive(WTS_CONNECTSTATE_CLASS state) noexcept {
    switch(state) {
    case WTSActive:
    case WTSConnected:
    case WTSConnectQuery:
    case WTSShadow:
    case WTSDisconnected:
    case WTSReset:
    case WTSDown:
    case WTSInit:
        return true;
    case WTSIdle:
    case WTSListen:
        return false;
    default:
        return true; // Unknown states block admission until the observation is refreshed.
    }
}
DWORD Query(DWORD session,WTS_INFO_CLASS type,WtsBuffer& buffer,DWORD& bytes) noexcept {
    LPWSTR raw=nullptr;bytes=0;
    if(!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,session,type,&raw,&bytes))return GetLastError();
    if(!raw||!bytes){if(raw)WTSFreeMemory(raw);return ERROR_INVALID_DATA;}
    buffer.value=raw;return ERROR_SUCCESS;
}
template<std::size_t N>
bool CopyWtsText(const WCHAR (&source)[N],std::wstring& output,bool required=true) {
    const auto length=wcsnlen_s(source,N);
    if(length==N||(required&&!length))return false;
    output.assign(source,length);return true;
}
DWORD ResolveUserSid(const WTSINFOEX_LEVEL1_W& session,std::string& output) noexcept {
    try {
        std::wstring user,domain;
        if(!CopyWtsText(session.UserName,user)||!CopyWtsText(session.DomainName,domain,false))return ERROR_NO_SUCH_LOGON_SESSION;
        std::wstring account;
        if(!domain.empty()){account=domain;account.push_back(L'\\');}
        account+=user;
        DWORD sidBytes=0,domainBytes=0;SID_NAME_USE use{};
        LookupAccountNameW(nullptr,account.c_str(),nullptr,&sidBytes,nullptr,&domainBytes,&use);
        const DWORD firstError=GetLastError();
        if(firstError!=ERROR_INSUFFICIENT_BUFFER)return firstError?firstError:ERROR_NONE_MAPPED;
        if(!sidBytes||sidBytes>SECURITY_MAX_SID_SIZE||domainBytes>256)return ERROR_BUFFER_OVERFLOW;
        std::array<BYTE,SECURITY_MAX_SID_SIZE> sid{};
        std::array<WCHAR,256> resolvedDomain{};
        DWORD actualSidBytes=static_cast<DWORD>(sid.size()),actualDomainBytes=static_cast<DWORD>(resolvedDomain.size());
        if(!LookupAccountNameW(nullptr,account.c_str(),sid.data(),&actualSidBytes,resolvedDomain.data(),&actualDomainBytes,&use))
            return GetLastError();
        if(actualSidBytes>sid.size()||!IsValidSid(sid.data()))return ERROR_INVALID_SID;
        LPSTR text=nullptr;
        if(!ConvertSidToStringSidA(sid.data(),&text))return GetLastError();
        struct Release {LPSTR value;~Release(){LocalFree(value);}} release{text};
        output=text;return ERROR_SUCCESS;
    } catch(...) {return ERROR_NOT_ENOUGH_MEMORY;}
}
DWORD SessionFlagSemantics() noexcept {
    RTL_OSVERSIONINFOW version{};version.dwOSVersionInfoSize=sizeof(version);
    const auto status=RtlGetVersion(&version);
    if(status<0)return RtlNtStatusToDosError(status);
    // Windows 7 / Server 2008 R2 report WTS_SESSIONSTATE_LOCK and UNLOCK in
    // reverse. Refuse to infer the lock state there rather than admit a locked
    // desktop due to the documented platform defect.
    if(version.dwMajorVersion==6&&version.dwMinorVersion==1)return ERROR_NOT_SUPPORTED;
    return ERROR_SUCCESS;
}
}
DWORD ObserveInteractiveSessions(InteractiveSessionObservation& output,bool resolveConsoleUserSid) noexcept {
    const auto flagSupport=SessionFlagSemantics();if(flagSupport)return flagSupport;
    PWTS_SESSION_INFOW sessions=nullptr;DWORD count=0;
    if(!WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE,0,1,&sessions,&count))return GetLastError();
    WtsBuffer owner;owner.value=sessions;
    InteractiveSessionObservation observed;
    const DWORD console=WTSGetActiveConsoleSessionId();
    if(console==MAXDWORD||console==0)return ERROR_NO_SUCH_LOGON_SESSION;
    observed.consoleSessionId=console;
    bool consoleFound=false;
    for(DWORD i=0;i<count;++i) {
        const auto& session=sessions[i];
        if(session.SessionId==0||!PossibleInteractive(session.State))continue;
        if(observed.interactiveLogonCount==std::numeric_limits<std::uint32_t>::max())return ERROR_ARITHMETIC_OVERFLOW;
        ++observed.interactiveLogonCount;
        if(session.SessionId!=console)continue;
        if(session.State!=WTSActive&&session.State!=WTSConnected)return ERROR_RETRY;
        WtsBuffer protocol;DWORD protocolBytes=0;
        auto error=Query(console,WTSClientProtocolType,protocol,protocolBytes);if(error)return error;
        if(protocolBytes<sizeof(USHORT)||*static_cast<const USHORT*>(protocol.value)!=0)return ERROR_NOT_SUPPORTED;
        WtsBuffer extended;DWORD extendedBytes=0;
        error=Query(console,WTSSessionInfoEx,extended,extendedBytes);if(error)return error;
        if(extendedBytes<sizeof(WTSINFOEXW))return ERROR_INVALID_DATA;
        const auto* info=static_cast<const WTSINFOEXW*>(extended.value);
        if(info->Level!=1||info->Data.WTSInfoExLevel1.SessionId!=console)return ERROR_INVALID_DATA;
        const auto flags=info->Data.WTSInfoExLevel1.SessionFlags;
        if(flags!=WTS_SESSIONSTATE_LOCK&&flags!=WTS_SESSIONSTATE_UNLOCK)return ERROR_INVALID_DATA;
        observed.consoleUnlocked=flags==WTS_SESSIONSTATE_UNLOCK;
        if(resolveConsoleUserSid){
            error=ResolveUserSid(info->Data.WTSInfoExLevel1,observed.consoleUserSid);if(error)return error;
        }
        consoleFound=true;
    }
    if(!consoleFound)return ERROR_NO_SUCH_LOGON_SESSION;
    output=std::move(observed);return ERROR_SUCCESS;
}
}
