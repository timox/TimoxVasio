#include "service_identity_win32.h"
#include "socket_discovery_root_win32.h"
#include "socket_discovery_win32.h"
#include "socket_publication_win32.h"
#include <aclapi.h>
#include <bcrypt.h>
#include <atomic>
#include <cwchar>
#include <iostream>

namespace {
using namespace timox_broker;
constexpr wchar_t ServiceName[]=L"TimoxNativeAudioBroker";
std::atomic_bool cancelled{false};
std::atomic<DWORD> resultCode{0};
SERVICE_STATUS_HANDLE statusHandle=nullptr;
struct Handle{HANDLE value=INVALID_HANDLE_VALUE;~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
struct Security{PSECURITY_DESCRIPTOR value=nullptr;~Security(){if(value)LocalFree(value);}};
enum Stage:DWORD {Passed=0,TokenRead=1,WrongIdentity=2,Root=3,Publish=4,Read=5,Owner=6,Compare=7,Cancelled=8,Random=9,StatusReport=10};
void Event(DWORD stage,DWORD error,const BrokerServiceIdentity* identity=nullptr,DWORD tokenError=ERROR_SUCCESS) noexcept {
    const auto log=RegisterEventSourceW(nullptr,L"TimoxNativeAudioServiceProbe");
    if(!log)return;
    const auto attributes=identity?identity->serviceGroupAttributes:0u;
    const auto eligible=identity&&IsBrokerServiceIdentity(*identity);
    wchar_t text[192]{};
    swprintf_s(text,L"Publication qualification: stage=%lu WindowsError=%lu tokenError=%lu serviceGroupAttributes=0x%08lX eligible=%u",
        stage,error,tokenError,attributes,eligible?1u:0u);
    const wchar_t* strings[]={text};
    ReportEventW(log,stage?EVENTLOG_ERROR_TYPE:EVENTLOG_INFORMATION_TYPE,0,1,nullptr,1,0,strings,nullptr);
    DeregisterEventSource(log);
}
DWORD Status(DWORD state,DWORD stage=0) noexcept {
    SERVICE_STATUS status{};status.dwServiceType=SERVICE_WIN32_OWN_PROCESS;status.dwCurrentState=state;
    status.dwControlsAccepted=state==SERVICE_RUNNING?SERVICE_ACCEPT_STOP:0;
    status.dwWin32ExitCode=stage?ERROR_SERVICE_SPECIFIC_ERROR:ERROR_SUCCESS;status.dwServiceSpecificExitCode=stage;
    if(state==SERVICE_START_PENDING){status.dwWaitHint=10000;status.dwCheckPoint=1;}
    return SetServiceStatus(statusHandle,&status)?ERROR_SUCCESS:GetLastError();
}
DWORD WINAPI Control(DWORD command,DWORD,void*,void*) noexcept {
    if(command==SERVICE_CONTROL_STOP){cancelled.store(true);return NO_ERROR;}
    return command==SERVICE_CONTROL_INTERROGATE?NO_ERROR:ERROR_CALL_NOT_IMPLEMENTED;
}
DWORD ExactOwner(HANDLE root) noexcept {
    Handle file;auto error=OpenSocketDiscoveryChild(root,L"discovery.bin",file.value);if(error)return error;
    Security actual,expected;PSID owner=nullptr,wanted=nullptr;BOOL defaulted=FALSE;
    error=GetSecurityInfo(file.value,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION,&owner,nullptr,nullptr,nullptr,&actual.value);
    if(error)return error;
    error=CreateSocketPublicationSecurity(expected.value);if(error)return error;
    if(!GetSecurityDescriptorOwner(expected.value,&wanted,&defaulted))return GetLastError();
    return EqualSid(owner,wanted)?ERROR_SUCCESS:ERROR_INVALID_OWNER;
}
void WINAPI ServiceMain(DWORD,wchar_t**) noexcept {
    statusHandle=RegisterServiceCtrlHandlerExW(ServiceName,Control,nullptr);
    if(!statusHandle){resultCode.store(TokenRead);return;}
    const auto pending=Status(SERVICE_START_PENDING);
    if(pending){Event(StatusReport,pending);resultCode.store(StatusReport);return;}
    DWORD stage=Passed,error=ERROR_SUCCESS;
    BrokerServiceIdentity identity{};
    DWORD tokenError=ERROR_SUCCESS;
    {
        tokenError=ObserveBrokerServiceIdentity(identity);
        error=tokenError;
        if(error)stage=TokenRead;
        else if(!IsBrokerServiceIdentity(identity)){stage=WrongIdentity;error=ERROR_ACCESS_DENIED;}
        Handle root;
        if(!stage){error=OpenSocketDiscoveryQualificationRoot(root.value);if(error)stage=Root;}
        if(!stage){
            error=Status(SERVICE_RUNNING);if(error)stage=StatusReport;
            SocketDiscovery record;
            FILETIME now{};GetSystemTimeAsFileTime(&now);record.epoch=(static_cast<std::uint64_t>(now.dwHighDateTime)<<32)|now.dwLowDateTime;
            // Synthetic metadata ONLY in the qualification sibling. No listener,
            // certificate/key generation or audio activation is performed here.
            if(!stage&&(BCryptGenRandom(nullptr,record.socketId.data(),static_cast<ULONG>(record.socketId.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0||
               BCryptGenRandom(nullptr,record.serverPin.data(),static_cast<ULONG>(record.serverPin.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)){stage=Random;error=ERROR_GEN_FAILURE;}
            if(!stage&&cancelled.load()){stage=Cancelled;error=ERROR_CANCELLED;}
            if(!stage){error=PublishSocketDiscoveryDirectory(root.value,record);if(error)stage=Publish;}
            SocketDiscovery observed;
            if(!stage){error=ReadSocketDiscoveryDirectory(root.value,observed);if(error)stage=Read;}
            if(!stage){error=ExactOwner(root.value);if(error)stage=Owner;}
            if(!stage&&(observed.epoch!=record.epoch||observed.socketId!=record.socketId||observed.serverPin!=record.serverPin)){stage=Compare;error=ERROR_INVALID_DATA;}
            if(!stage&&cancelled.load()){stage=Cancelled;error=ERROR_CANCELLED;}
        }
    }
    Event(stage,error,&identity,tokenError);resultCode.store(stage);
    const auto stopped=Status(SERVICE_STOPPED,stage);
    if(stopped){Event(StatusReport,stopped,&identity,tokenError);resultCode.store(StatusReport);}
}
int Inspect() {
    BrokerServiceIdentity identity;const auto token=ObserveBrokerServiceIdentity(identity);
    Handle root;const auto directory=OpenSocketDiscoveryQualificationRoot(root.value);
    SocketDiscovery publication;
    const auto read=directory?directory:ReadSocketDiscoveryDirectory(root.value,publication);
    const auto owner=read?read:ExactOwner(root.value);
    std::cout<<"{\"tokenError\":"<<token<<",\"localService\":"<<(identity.localService?"true":"false")
        <<",\"sessionId\":"<<identity.sessionId<<",\"serviceGroupAttributes\":"<<identity.serviceGroupAttributes
        <<",\"eligible\":"<<(!token&&IsBrokerServiceIdentity(identity)?"true":"false")<<",\"qualificationRootError\":"<<directory
        <<",\"publicationError\":"<<read<<",\"ownerError\":"<<owner<<",\"publicationEpoch\":\""<<publication.epoch<<"\"}\n";
    return token?1:0;
}
int Prepare(bool apply) {
    QualificationRootPreparation plan;
    const auto error=PrepareSocketDiscoveryQualificationRoot(apply,plan);
    std::cout<<"{\"apply\":"<<(apply?"true":"false")<<",\"error\":"<<error
        <<",\"productMissingBefore\":"<<(error?"null":plan.productMissing?"true":"false")
        <<",\"qualificationMissingBefore\":"<<(error?"null":plan.qualificationMissing?"true":"false")<<"}\n";
    return error?1:0;
}
}
int wmain(int argc,wchar_t** argv) {
    if(argc==1||(argc==2&&std::wcscmp(argv[1],L"--help")==0)) {
        std::cout<<"TimoxNativeAudioServiceProbe (VM qualification, not an audio broker)\n"
            <<"  --inspect                    Read token/root readiness; no writes\n"
            <<"  --prepare-qualification-root [--apply]  Inspect missing directories; explicit admin apply creates them\n"
            <<"  --service-publication-probe  SCM-only publication test in the Qualification sibling\n"
            <<"Requires service name TimoxNativeAudioBroker, LocalService, unrestricted service SID.\n"
            <<"No service install, key enrollment or audio activation. Directory creation requires --apply.\n";return 0;
    }
    if(argc==2&&std::wcscmp(argv[1],L"--inspect")==0)return Inspect();
    if(argc==2&&std::wcscmp(argv[1],L"--prepare-qualification-root")==0)return Prepare(false);
    if(argc==3&&std::wcscmp(argv[1],L"--prepare-qualification-root")==0&&std::wcscmp(argv[2],L"--apply")==0)return Prepare(true);
    if(argc!=2||std::wcscmp(argv[1],L"--service-publication-probe")!=0){std::cerr<<"Invalid arguments; use --help\n";return 2;}
    SERVICE_TABLE_ENTRYW table[]={{const_cast<wchar_t*>(ServiceName),ServiceMain},{nullptr,nullptr}};
    if(!StartServiceCtrlDispatcherW(table)){std::cerr<<"SCM dispatch failed: "<<GetLastError()<<'\n';return 1;}
    return static_cast<int>(resultCode.load());
}
