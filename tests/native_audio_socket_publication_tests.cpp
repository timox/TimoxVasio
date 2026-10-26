#include "socket_publication_win32.h"
#include "socket_discovery_win32.h"
#include <sddl.h>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace timox_broker;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Handle{HANDLE value=INVALID_HANDLE_VALUE;~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
struct Security{PSECURITY_DESCRIPTOR value=nullptr;~Security(){if(value)LocalFree(value);}};
SocketDiscovery Record(std::uint64_t epoch){SocketDiscovery d;d.epoch=epoch;d.socketId[0]=1;d.serverPin[0]=2;return d;}
SocketDiscovery Read(const std::wstring& path){
    Handle file;file.value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    Check(file.value!=INVALID_HANDLE_VALUE,"open publication");
    SocketDiscoveryBuffer bytes{};DWORD read=0;
    Check(ReadFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr)&&read==bytes.size(),"read publication");
    SocketDiscovery d;Check(DecodeSocketDiscovery(bytes.data(),bytes.size(),d),"decode publication");return d;
}
void Test(){
    Security production;
    Check(CreateSocketPublicationSecurity(production.value)==ERROR_SUCCESS,"create service descriptor");
    Check(ValidateSocketDiscoverySecurity(production.value)==ERROR_SUCCESS,"generated descriptor violates reader policy");
    PSID owner=nullptr;BOOL defaulted=FALSE;PSID service=nullptr;
    Check(GetSecurityDescriptorOwner(production.value,&owner,&defaulted)!=FALSE,"read generated owner");
    Check(ConvertStringSidToSidW(L"S-1-5-80-954356743-986487362-260697779-2273189862-4263205101",&service)!=FALSE,"service SID fixture");
    const bool correctOwner=EqualSid(owner,service)!=FALSE;LocalFree(service);
    Check(correctOwner,"publication owner is not the broker service SID");
    auto* original=production.value;
    Check(CreateSocketPublicationSecurity(production.value)==ERROR_INVALID_PARAMETER&&production.value==original,"descriptor overwritten");
    wchar_t current[MAX_PATH]{};Check(GetCurrentDirectoryW(MAX_PATH,current)>0,"working directory");
    const auto directory=std::wstring(current)+L"\\publication-test-"+std::to_wstring(GetCurrentProcessId());
    const auto destination=directory+L"\\discovery.bin";
    struct Cleanup{std::wstring directory;bool owned=false;~Cleanup(){if(owned){DeleteFileW((directory+L"\\discovery.bin").c_str());RemoveDirectoryW(directory.c_str());}}} cleanup{directory};
    Check(CreateDirectoryW(directory.c_str(),nullptr)!=FALSE,"create test directory");cleanup.owned=true;
    Handle root;root.value=CreateFileW(directory.c_str(),FILE_READ_ATTRIBUTES|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_WRITE,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    Check(root.value!=INVALID_HANDLE_VALUE,"open test directory");
    // Test-only IO seam uses the caller's ordinary owner. No service/machine ACL
    // is installed. The production entry rejects this same user-owned root.
    const auto first=PublishSocketDiscoveryForTest(root.value,Record(1),nullptr);
    if(first!=ERROR_SUCCESS)throw std::runtime_error("first publication error "+std::to_string(first));
    Check(Read(destination).epoch==1,"first record missing");
    Check(PublishSocketDiscoveryForTest(root.value,Record(2),nullptr)==ERROR_SUCCESS,"replacement publication");
    Check(Read(destination).epoch==2,"replacement record missing");
    {
        Handle reader;reader.value=CreateFileW(destination.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        Check(reader.value!=INVALID_HANDLE_VALUE,"hold existing reader");
        const auto held=PublishSocketDiscoveryForTest(root.value,Record(3),nullptr);
        Check(held==ERROR_SHARING_VIOLATION||held==ERROR_ACCESS_DENIED,"replaced reader-held record");
        Check(Read(destination).epoch==2,"failed replacement damaged old record");
    }
    Check(PublishSocketDiscoveryForTest(root.value,Record(3),nullptr)==ERROR_SUCCESS,"retry after reader close failed");
    Check(Read(destination).epoch==3,"retry did not publish new record");
    Check(PublishSocketDiscoveryDirectory(root.value,Record(4))==ERROR_ACCESS_DENIED,"production trusts test-owned directory");
    Check(Read(destination).epoch==3,"refusal changed publication");
    Check(PublishSocketDiscoveryDirectory(root.value,SocketDiscovery{})==ERROR_INVALID_DATA,"invalid discovery accepted");
    WIN32_FIND_DATAW item{};const auto pattern=directory+L"\\pub-*.tmp";
    HANDLE found=FindFirstFileW(pattern.c_str(),&item);
    if(found!=INVALID_HANDLE_VALUE)FindClose(found);
    Check(found==INVALID_HANDLE_VALUE&&GetLastError()==ERROR_FILE_NOT_FOUND,"temporary publication leaked");
}
}
int main(){try{Test();std::cout<<"Publication descriptor, atomic replace, held reader and refusal: PASS\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
