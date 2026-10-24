#include "socket_discovery_win32.h"
#include <aclapi.h>

namespace timox_broker {
namespace {
// sc.exe showsid TimoxNativeAudioBroker; tied to the production service name,
// not supplied by a descriptor, environment variable or a connected client.
struct BrokerSid {
    BYTE revision=SID_REVISION;
    BYTE count=6;
    SID_IDENTIFIER_AUTHORITY authority=SECURITY_NT_AUTHORITY;
    DWORD sub[6]={80,954356743,986487362,260697779,2273189862,4263205101};
};
static_assert(offsetof(BrokerSid,sub)==8,"Windows SID layout");
unsigned Principal(PSID sid) noexcept {
    if(!sid || !IsValidSid(sid)) return 0;
    if(IsWellKnownSid(sid,WinLocalSystemSid)) return 1;
    if(IsWellKnownSid(sid,WinBuiltinAdministratorsSid)) return 2;
    BrokerSid broker;
    if(EqualSid(sid,&broker)) return 4;
    if(IsWellKnownSid(sid,WinAuthenticatedUserSid)) return 8;
    return 0;
}
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
struct Security {
    PSECURITY_DESCRIPTOR value=nullptr;
    ~Security(){if(value)LocalFree(value);}
};
}
DWORD ValidateSocketDiscoverySecurity(PSECURITY_DESCRIPTOR descriptor) noexcept {
    if(!descriptor || !IsValidSecurityDescriptor(descriptor)) return ERROR_ACCESS_DENIED;
    PSID owner=nullptr; BOOL defaulted=FALSE,present=FALSE;
    SECURITY_DESCRIPTOR_CONTROL control=0; DWORD revision=0; PACL acl=nullptr;
    if(!GetSecurityDescriptorOwner(descriptor,&owner,&defaulted) ||
       !GetSecurityDescriptorControl(descriptor,&control,&revision) ||
       !GetSecurityDescriptorDacl(descriptor,&present,&acl,&defaulted)) return ERROR_ACCESS_DENIED;
    const unsigned ownerKind=Principal(owner);
    if((ownerKind!=1 && ownerKind!=2 && ownerKind!=4) ||
       !(control&SE_DACL_PROTECTED) || !present || !acl || !IsValidAcl(acl) || acl->AceCount!=4)
        return ERROR_ACCESS_DENIED;
    unsigned seen=0;
    for(DWORD i=0;i<acl->AceCount;++i) {
        void* raw=nullptr;
        if(!GetAce(acl,i,&raw)) return ERROR_ACCESS_DENIED;
        const auto* header=static_cast<ACE_HEADER*>(raw);
        constexpr auto offset=offsetof(ACCESS_ALLOWED_ACE,SidStart);
        if(header->AceType!=ACCESS_ALLOWED_ACE_TYPE || header->AceFlags!=0 || header->AceSize<offset+8)
            return ERROR_ACCESS_DENIED;
        const auto* ace=static_cast<ACCESS_ALLOWED_ACE*>(raw);
        auto* sid=reinterpret_cast<BYTE*>(const_cast<DWORD*>(&ace->SidStart));
        if(sid[1]>SID_MAX_SUB_AUTHORITIES || offset+8+4*static_cast<std::size_t>(sid[1])!=header->AceSize)
            return ERROR_ACCESS_DENIED;
        const auto kind=Principal(sid);
        const DWORD expected=kind==8 ? FILE_GENERIC_READ : FILE_ALL_ACCESS;
        if(!kind || (seen&kind) || ace->Mask!=expected)
            return ERROR_ACCESS_DENIED;
        seen|=kind;
    }
    return seen==15 ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
}
DWORD ReadSocketDiscoveryHandle(HANDLE original, SocketDiscovery& output) noexcept {
    if(!original || original==INVALID_HANDLE_VALUE) return ERROR_INVALID_HANDLE;
    Handle file{ReOpenFile(original,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,FILE_FLAG_OPEN_REPARSE_POINT)};
    if(file.value==INVALID_HANDLE_VALUE) return GetLastError();
    if(GetFileType(file.value)!=FILE_TYPE_DISK) return ERROR_ACCESS_DENIED;
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(file.value,&info)) return GetLastError();
    if((info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) || info.nNumberOfLinks!=1)
        return ERROR_ACCESS_DENIED;
    if(info.nFileSizeHigh || info.nFileSizeLow!=SocketDiscoveryBytes) return ERROR_INVALID_DATA;
    Security security;
    const auto error=GetSecurityInfo(file.value,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,
        nullptr,nullptr,nullptr,nullptr,&security.value);
    if(error!=ERROR_SUCCESS) return error;
    const auto policy=ValidateSocketDiscoverySecurity(security.value);
    if(policy!=ERROR_SUCCESS) return policy;
    SocketDiscoveryBuffer bytes{}; DWORD read=0;
    if(!ReadFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr)) return GetLastError();
    if(read!=bytes.size()) return ERROR_INVALID_DATA;
    return DecodeSocketDiscovery(bytes.data(),bytes.size(),output) ? ERROR_SUCCESS : ERROR_INVALID_DATA;
}
}
