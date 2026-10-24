#include "socket_discovery_win32.h"
#include <sddl.h>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace timox_broker;
constexpr auto Service=L"S-1-5-80-954356743-986487362-260697779-2273189862-4263205101";
void Check(bool value,const char* why) { if(!value) throw std::runtime_error(why); }
struct Descriptor {
    PSECURITY_DESCRIPTOR value=nullptr;
    explicit Descriptor(const std::wstring& sddl) {
        Check(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&value,nullptr)!=FALSE,"SDDL fixture");
    }
    ~Descriptor() { if(value) LocalFree(value); }
};
std::wstring Grants() { return std::wstring(L"(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;")+Service+L")(A;;FR;;;AU)"; }
void Security() {
    for(const auto* owner:{L"SY",L"BA",Service}) {
        Descriptor d(std::wstring(L"O:")+owner+L"D:P"+Grants());
        Check(ValidateSocketDiscoverySecurity(d.value)==ERROR_SUCCESS,"canonical descriptor refused");
    }
    for(const auto& sddl:{
        L"O:LSD:P"+Grants(), // LocalService is shared by unrelated services.
        L"O:S-1-5-21-1-2-3-1001D:P"+Grants(),
        L"O:SYD:"+Grants(), // inheritable from an unverified parent
        std::wstring(L"O:SYD:P"),
        L"O:SYD:P"+Grants()+L"(A;;FW;;;AU)",
        L"O:SYD:P"+Grants()+L"(A;;FA;;;WD)",
        std::wstring(L"O:SYD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FR;;;AU)"),
        std::wstring(L"O:SYD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;LS)(A;;FR;;;AU)"),
        std::wstring(L"O:SYD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;")+Service+L")(A;IO;FR;;;AU)",
        std::wstring(L"O:SYD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;")+Service+L")(A;;FW;;;AU)",
        std::wstring(L"O:SYD:P(A;;FA;;;SY)(A;;FA;;;SY)(A;;FA;;;")+Service+L")(A;;FR;;;AU)",
        std::wstring(L"O:SYD:P(D;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;")+Service+L")(A;;FR;;;AU)"}) {
        Descriptor d(sddl);
        Check(ValidateSocketDiscoverySecurity(d.value)==ERROR_ACCESS_DENIED,"unsafe/noncanonical descriptor accepted");
    }
    SECURITY_DESCRIPTOR empty{};
    Check(InitializeSecurityDescriptor(&empty,SECURITY_DESCRIPTOR_REVISION)!=FALSE,"initialize descriptor");
    Check(SetSecurityDescriptorDacl(&empty,TRUE,nullptr,FALSE)!=FALSE,"null DACL fixture");
    Check(ValidateSocketDiscoverySecurity(&empty)==ERROR_ACCESS_DENIED,"null DACL accepted");
    Check(ValidateSocketDiscoverySecurity(nullptr)==ERROR_ACCESS_DENIED,"null descriptor accepted");
}
struct File {
    HANDLE handle=INVALID_HANDLE_VALUE;
    ~File(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
};
void ReaderRefusals() {
    // Workspace-only user-owned fixture; no machine ACL/provisioning changes.
    wchar_t current[MAX_PATH]{};
    Check(GetCurrentDirectoryW(MAX_PATH,current)>0,"working directory");
    const auto path=std::wstring(current)+L"\\discovery-reader-"+std::to_wstring(GetCurrentProcessId())+L".tmp";
    struct Cleanup { std::wstring path; bool owned=false; ~Cleanup(){if(owned)DeleteFileW(path.c_str());} } cleanup{path};
    SocketDiscovery d;d.epoch=1;d.socketId[0]=1;d.serverPin[0]=1;
    SocketDiscoveryBuffer bytes{};Check(EncodeSocketDiscovery(d,bytes),"encode fixture");
    {
        File file;file.handle=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        Check(file.handle!=INVALID_HANDLE_VALUE,"create fixture");
        cleanup.owned=true;
        DWORD written=0;
        Check(WriteFile(file.handle,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size(),"write fixture");
        auto unchanged=d;
        Check(ReadSocketDiscoveryHandle(file.handle,unchanged)==ERROR_SHARING_VIOLATION,"open writer not excluded");
        Check(unchanged.epoch==d.epoch&&unchanged.socketId==d.socketId&&unchanged.serverPin==d.serverPin,"sharing failure modified output");
    }
    File file;file.handle=CreateFileW(path.c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    Check(file.handle!=INVALID_HANDLE_VALUE,"open fixture");
    auto output=d;output.epoch=123;
    Check(ReadSocketDiscoveryHandle(file.handle,output)==ERROR_ACCESS_DENIED,"user-owned discovery trusted");
    Check(output.epoch==123&&output.socketId==d.socketId&&output.serverPin==d.serverPin,"ACL failure modified output");
    Check(ReadSocketDiscoveryHandle(INVALID_HANDLE_VALUE,output)!=ERROR_SUCCESS&&output.epoch==123,"invalid handle mutated output");
}
}
int main() {
    try{Security();ReaderRefusals();std::cout<<"Discovery ACL fixtures and real file refusals: PASS\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
