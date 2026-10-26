#include "service_identity_win32.h"
#include <array>
#include <cstddef>
namespace timox_broker {
namespace {
struct Handle{HANDLE value=nullptr;~Handle(){if(value)CloseHandle(value);}};
struct ServiceSid {
    BYTE revision=SID_REVISION,count=6;SID_IDENTIFIER_AUTHORITY authority=SECURITY_NT_AUTHORITY;
    DWORD sub[6]={80,954356743,986487362,260697779,2273189862,4263205101};
};
static_assert(offsetof(ServiceSid,sub)==8,"Windows SID layout");
}
bool IsBrokerServiceIdentity(const BrokerServiceIdentity& identity) noexcept {
    constexpr DWORD needed=SE_GROUP_ENABLED|SE_GROUP_OWNER;
    return identity.localService&&identity.sessionId==0&&
        (identity.serviceGroupAttributes&needed)==needed&&
        !(identity.serviceGroupAttributes&SE_GROUP_USE_FOR_DENY_ONLY);
}
DWORD ObserveBrokerServiceIdentity(BrokerServiceIdentity& output) noexcept {
    Handle impersonation;
    if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&impersonation.value))return ERROR_BAD_IMPERSONATION_LEVEL;
    const auto threadError=GetLastError();if(threadError!=ERROR_NO_TOKEN)return threadError;
    Handle token;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.value))return GetLastError();
    alignas(TOKEN_USER) std::array<BYTE,256> user{};DWORD size=0;
    if(!GetTokenInformation(token.value,TokenUser,user.data(),static_cast<DWORD>(user.size()),&size))return GetLastError();
    BrokerServiceIdentity identity;
    identity.localService=IsWellKnownSid(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid,WinLocalServiceSid)!=FALSE;
    if(!GetTokenInformation(token.value,TokenSessionId,&identity.sessionId,sizeof(identity.sessionId),&size))return GetLastError();
    alignas(TOKEN_GROUPS) std::array<BYTE,65536> storage{};
    if(!GetTokenInformation(token.value,TokenGroups,storage.data(),static_cast<DWORD>(storage.size()),&size))return GetLastError();
    const auto* groups=reinterpret_cast<const TOKEN_GROUPS*>(storage.data());
    constexpr auto headerBytes=offsetof(TOKEN_GROUPS,Groups);
    if(size<headerBytes||groups->GroupCount>(size-headerBytes)/sizeof(SID_AND_ATTRIBUTES))return ERROR_INVALID_DATA;
    ServiceSid service;bool found=false;
    for(DWORD i=0;i<groups->GroupCount;++i)if(EqualSid(groups->Groups[i].Sid,&service)) {
        if(found)return ERROR_INVALID_DATA;
        identity.serviceGroupAttributes=groups->Groups[i].Attributes;found=true;
    }
    output=identity;return ERROR_SUCCESS;
}
}
