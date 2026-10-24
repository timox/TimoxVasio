#include "socket_publication_win32.h"
#include "socket_discovery_win32.h"
#include <aclapi.h>
#include <bcrypt.h>
#include <sddl.h>
#include <winternl.h>
#include <array>
#include <cstring>
#include <new>

// The SDK exposes NtCreateFile but not this declaration. Native information
// class 10 and FILE_RENAME_INFORMATION layout are documented in the WDK.
extern "C" NTSYSAPI NTSTATUS NTAPI NtSetInformationFile(HANDLE,PIO_STATUS_BLOCK,PVOID,ULONG,FILE_INFORMATION_CLASS);

namespace timox_broker {
namespace {
struct NativeRename {BOOLEAN replace;HANDLE root;ULONG bytes;WCHAR name[1];};
struct Handle{HANDLE value=INVALID_HANDLE_VALUE;~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
struct Security{PSECURITY_DESCRIPTOR value=nullptr;~Security(){if(value)LocalFree(value);}};
struct Temporary {
    Handle file;
    bool published=false;
    ~Temporary(){
        if(!published&&file.value!=INVALID_HANDLE_VALUE&&file.value){
            FILE_DISPOSITION_INFO disposition{TRUE};
            // Best effort after an OS failure. A crash can also leave a protected
            // pub-*.tmp file; it is never read as discovery or reused by name.
            SetFileInformationByHandle(file.value,FileDispositionInfo,&disposition,sizeof(disposition));
        }
    }
};
DWORD CheckSecurity(HANDLE file,bool directory) noexcept {
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(file,&info))return GetLastError();
    if(GetFileType(file)!=FILE_TYPE_DISK || (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) ||
       (((info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0)!=directory) || (!directory&&info.nNumberOfLinks!=1))return ERROR_ACCESS_DENIED;
    Security sd;
    const auto error=GetSecurityInfo(file,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,
        nullptr,nullptr,nullptr,nullptr,&sd.value);
    if(error)return error;
    return directory?ValidateSocketDiscoveryDirectorySecurity(sd.value):ValidateSocketDiscoverySecurity(sd.value);
}
DWORD Replace(HANDLE root,const SocketDiscoveryBuffer& bytes,PSECURITY_DESCRIPTOR descriptor) noexcept {
    std::array<BYTE,16> random{};
    const auto generated=BCryptGenRandom(nullptr,random.data(),static_cast<ULONG>(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if(generated<0)return RtlNtStatusToDosError(generated);
    wchar_t name[41]=L"pub-";
    constexpr wchar_t hex[]=L"0123456789abcdef";
    for(std::size_t i=0;i<random.size();++i){name[4+2*i]=hex[random[i]>>4];name[5+2*i]=hex[random[i]&15];}
    std::memcpy(name+36,L".tmp",5*sizeof(wchar_t));
    UNICODE_STRING objectName{};objectName.Buffer=name;objectName.Length=40*sizeof(wchar_t);objectName.MaximumLength=sizeof(name);
    OBJECT_ATTRIBUTES attributes{};InitializeObjectAttributes(&attributes,&objectName,OBJ_CASE_INSENSITIVE,root,descriptor);
    IO_STATUS_BLOCK io{};Temporary temp;
    const auto created=NtCreateFile(&temp.file.value,FILE_WRITE_DATA|FILE_READ_ATTRIBUTES|READ_CONTROL|DELETE|SYNCHRONIZE,
        &attributes,&io,nullptr,FILE_ATTRIBUTE_NORMAL,0,FILE_CREATE,
        FILE_NON_DIRECTORY_FILE|FILE_OPEN_REPARSE_POINT|FILE_SYNCHRONOUS_IO_NONALERT,nullptr,0);
    if(created<0)return RtlNtStatusToDosError(created);
    if(descriptor){const auto checked=CheckSecurity(temp.file.value,false);if(checked)return checked;}
    DWORD written=0;
    if(!WriteFile(temp.file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr))return GetLastError();
    if(written!=bytes.size())return ERROR_WRITE_FAULT;
    if(!FlushFileBuffers(temp.file.value))return GetLastError();
    constexpr wchar_t destination[]=L"discovery.bin";
    alignas(NativeRename) std::array<BYTE,sizeof(NativeRename)+sizeof(destination)> storage{};
    auto* rename=new(storage.data())NativeRename{};
    rename->replace=TRUE;rename->root=root;rename->bytes=sizeof(destination)-sizeof(wchar_t);
    std::memcpy(rename->name,destination,sizeof(destination));
    // kernel32 FileRenameInfo rejected RootDirectory with ERROR_INVALID_PARAMETER
    // in the real test. Keep handle-relative resolution through the native call;
    // never fall back to reconstructing a pathname.
    const auto renamed=NtSetInformationFile(temp.file.value,&io,rename,static_cast<ULONG>(storage.size()),static_cast<FILE_INFORMATION_CLASS>(10));
    if(renamed<0)return RtlNtStatusToDosError(renamed);
    temp.published=true;return ERROR_SUCCESS;
}
}
DWORD CreateSocketPublicationSecurity(PSECURITY_DESCRIPTOR& output) noexcept {
    if(output)return ERROR_INVALID_PARAMETER;
    constexpr wchar_t sddl[]=L"O:S-1-5-80-954356743-986487362-260697779-2273189862-4263205101"
        L"D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;S-1-5-80-954356743-986487362-260697779-2273189862-4263205101)(A;;FR;;;AU)";
    PSECURITY_DESCRIPTOR candidate=nullptr;
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl,SDDL_REVISION_1,&candidate,nullptr))return GetLastError();
    output=candidate;return ERROR_SUCCESS;
}
DWORD PublishSocketDiscoveryDirectory(HANDLE original,const SocketDiscovery& discovery) noexcept {
    SocketDiscoveryBuffer bytes{};if(!EncodeSocketDiscovery(discovery,bytes))return ERROR_INVALID_DATA;
    if(!original||original==INVALID_HANDLE_VALUE)return ERROR_INVALID_HANDLE;
    Handle root{ReOpenFile(original,FILE_READ_ATTRIBUTES|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_WRITE,
        FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT)};
    if(root.value==INVALID_HANDLE_VALUE)return GetLastError();
    const auto checked=CheckSecurity(root.value,true);if(checked)return checked;
    SocketDiscovery previous;
    const auto existing=ReadSocketDiscoveryDirectory(root.value,previous);
    if(existing!=ERROR_SUCCESS&&existing!=ERROR_FILE_NOT_FOUND)return existing;
    Security descriptor;const auto generated=CreateSocketPublicationSecurity(descriptor.value);if(generated)return generated;
    return Replace(root.value,bytes,descriptor.value);
}
#ifdef TIMOX_NATIVE_DISCOVERY_TESTS
DWORD PublishSocketDiscoveryForTest(HANDLE root,const SocketDiscovery& discovery,PSECURITY_DESCRIPTOR descriptor) noexcept {
    SocketDiscoveryBuffer bytes{};if(!EncodeSocketDiscovery(discovery,bytes))return ERROR_INVALID_DATA;
    return Replace(root,bytes,descriptor);
}
#endif
}
