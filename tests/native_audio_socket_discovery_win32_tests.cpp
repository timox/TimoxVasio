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
    Descriptor directory(std::wstring(L"O:SYD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;")+Service+L")(A;;FRFX;;;AU)");
    Check(ValidateSocketDiscoveryDirectorySecurity(directory.value)==ERROR_SUCCESS,"directory traverse/read policy refused");
    Check(ValidateSocketDiscoverySecurity(directory.value)==ERROR_ACCESS_DENIED,"directory policy used for file");
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
void RelativeOpen() {
    wchar_t current[MAX_PATH]{};
    Check(GetCurrentDirectoryW(MAX_PATH,current)>0,"working directory");
    File parent;parent.handle=CreateFileW(current,FILE_READ_ATTRIBUTES|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_WRITE,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    Check(parent.handle!=INVALID_HANDLE_VALUE,"open workspace directory");
    const auto basename=L"relative-discovery-"+std::to_wstring(GetCurrentProcessId())+L".tmp";
    const auto path=std::wstring(current)+L"\\"+basename;
    struct Cleanup {std::wstring path;bool owned=false;~Cleanup(){if(owned)DeleteFileW(path.c_str());}} cleanup{path};
    {
        File created;created.handle=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        Check(created.handle!=INVALID_HANDLE_VALUE,"create relative fixture");cleanup.owned=true;
        const BYTE marker=0x6a;DWORD written=0;
        Check(WriteFile(created.handle,&marker,1,&written,nullptr)&&written==1,"write relative fixture");
    }
    File child;
    Check(OpenSocketDiscoveryChild(parent.handle,basename.c_str(),child.handle)==ERROR_SUCCESS,"relative child open");
    BYTE marker=0;DWORD read=0;
    Check(ReadFile(child.handle,&marker,1,&read,nullptr)&&read==1&&marker==0x6a,"wrong relative object");
    // Returned file excludes future write/delete opens until closed.
    File writer;writer.handle=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
    Check(writer.handle==INVALID_HANDLE_VALUE&&GetLastError()==ERROR_SHARING_VIOLATION,"child allows competing writer");
    for(const auto* name:{L"",L".",L"..",L"../x",L"a\\b",L"a/b",L"C:x",L"x:stream",L"x.",L"x ",L"\\x"}) {
        HANDLE result=INVALID_HANDLE_VALUE;
        Check(OpenSocketDiscoveryChild(parent.handle,name,result)==ERROR_INVALID_NAME&&result==INVALID_HANDLE_VALUE,"unsafe component accepted");
    }
    HANDLE occupied=child.handle;
    Check(OpenSocketDiscoveryChild(parent.handle,basename.c_str(),occupied)==ERROR_INVALID_PARAMETER&&occupied==child.handle,"live output handle overwritten");
    SocketDiscovery output;output.epoch=123;
    // A writable user directory cannot be used as a trusted publication root.
    Check(ReadSocketDiscoveryDirectory(parent.handle,output)==ERROR_ACCESS_DENIED&&output.epoch==123,"user directory trusted");
}
void ParentReplacement() {
    wchar_t current[MAX_PATH]{};
    Check(GetCurrentDirectoryW(MAX_PATH,current)>0,"working directory");
    const auto root=std::wstring(current)+L"\\relative-parent-"+std::to_wstring(GetCurrentProcessId());
    const auto moved=root+L"-moved";
    struct Cleanup {
        std::wstring root,moved;bool rootOwned=false,movedOwned=false;
        ~Cleanup(){
            if(rootOwned){DeleteFileW((root+L"\\discovery.bin").c_str());RemoveDirectoryW(root.c_str());}
            if(movedOwned){DeleteFileW((moved+L"\\discovery.bin").c_str());RemoveDirectoryW(moved.c_str());}
        }
    } cleanup{root,moved};
    Check(CreateDirectoryW(root.c_str(),nullptr)!=FALSE,"create parent");cleanup.rootOwned=true;
    auto write=[](const std::wstring& path,BYTE marker){
        File file;file.handle=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        Check(file.handle!=INVALID_HANDLE_VALUE,"create marker");DWORD written=0;
        Check(WriteFile(file.handle,&marker,1,&written,nullptr)&&written==1,"write marker");
    };
    write(root+L"\\discovery.bin",0x35);
    File parent;parent.handle=CreateFileW(root.c_str(),FILE_READ_ATTRIBUTES|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    Check(parent.handle!=INVALID_HANDLE_VALUE,"open renameable parent");
    Check(MoveFileW(root.c_str(),moved.c_str())!=FALSE,"move held parent");
    cleanup.rootOwned=false;cleanup.movedOwned=true;
    Check(CreateDirectoryW(root.c_str(),nullptr)!=FALSE,"replace original pathname");cleanup.rootOwned=true;
    write(root+L"\\discovery.bin",0x79);
    File child;
    Check(OpenSocketDiscoveryChild(parent.handle,L"discovery.bin",child.handle)==ERROR_SUCCESS,"open moved parent's child");
    BYTE marker=0;DWORD read=0;
    Check(ReadFile(child.handle,&marker,1,&read,nullptr)&&read==1&&marker==0x35,"followed replacement parent pathname");
}
}
int main() {
    try{Security();ReaderRefusals();RelativeOpen();ParentReplacement();std::cout<<"Discovery ACL, relative open, parent replacement and real file refusals: PASS\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
