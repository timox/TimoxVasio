// Real local pipes; no service, driver, machine key or certificate store writes.
#include "schannel_stream_win32.h"
#include "pipe_stream_win32.h"
#include "channel_frames.h"
#include "pipe_identity_win32.h"
#include "native_audio_authenticated_channel_win32.h"
#include "broker_session.h"
#include "broker_dispatcher.h"
#include "unix_socket_stream_win32.h"
#include "socket_session_win32.h"
#include <filesystem>
#include <future>
#include <sddl.h>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <thread>
#include <windows.h>
#include <security.h>
#include <winternl.h>
#define SCHANNEL_USE_BLACKLISTS
#include <schannel.h>
#include <wincrypt.h>
#include <ncrypt.h>
#include <bcrypt.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
using Bytes=std::vector<unsigned char>;
using Pin=std::array<unsigned char,32>;
bool temporaryUserKeys=false,cleanupFailure=false;
unsigned keysCreated=0,keysDeleted=0,keysAbsent=0;
struct Failure:std::runtime_error {
    SECURITY_STATUS code;const char* stage;
    Failure(SECURITY_STATUS value,const char* operation,const char* message):std::runtime_error(message),code(value),stage(operation){}
};
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void Status(SECURITY_STATUS status,const char* operation){if(status!=0){char text[160]{};
    std::snprintf(text,sizeof(text),"%s: 0x%08lx",operation,static_cast<unsigned long>(status));throw Failure(status,operation,text);}}
Pin Hash(PCCERT_CONTEXT cert){Pin pin{};DWORD size=static_cast<DWORD>(pin.size());
    Check(CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM,0,nullptr,cert->pbCertEncoded,cert->cbCertEncoded,pin.data(),&size)!=FALSE && size==pin.size(),"SHA256");return pin;}
struct Identity {
    NCRYPT_PROV_HANDLE provider=0;NCRYPT_KEY_HANDLE key=0;PCCERT_CONTEXT cert=nullptr;
    std::wstring name;bool created=false;
    Identity(const wchar_t* subject,const char* eku,bool expired=false){try{
        Status(NCryptOpenStorageProvider(&provider,MS_KEY_STORAGE_PROVIDER,0),"open KSP");
        if(temporaryUserKeys){
            std::array<unsigned char,16> random{};
            Status(BCryptGenRandom(nullptr,random.data(),static_cast<ULONG>(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG),"key name randomness");
            name=L"TimoxVasioProbe-";constexpr wchar_t hex[]=L"0123456789abcdef";
            for(auto byte:random){name+=hex[byte>>4];name+=hex[byte&15];}
            std::printf("TEMPORARY USER KEY planned: %ls\n",name.c_str());std::fflush(stdout);
        }
        // Named user storage is explicit opt-in; no machine key or fallback.
        Status(NCryptCreatePersistedKey(provider,&key,NCRYPT_ECDSA_P256_ALGORITHM,name.empty()?nullptr:name.c_str(),0,0),"create CNG key");
        if(!name.empty()){created=true;++keysCreated;}
        DWORD exportPolicy=0;
        Status(NCryptSetProperty(key,NCRYPT_EXPORT_POLICY_PROPERTY,reinterpret_cast<PBYTE>(&exportPolicy),sizeof(exportPolicy),0),"nonexportable key policy");
        Status(NCryptFinalizeKey(key,0),"finalize ephemeral key");
        DWORD propertySize=0;exportPolicy=~DWORD{0};
        Status(NCryptGetProperty(key,NCRYPT_EXPORT_POLICY_PROPERTY,reinterpret_cast<PBYTE>(&exportPolicy),sizeof(exportPolicy),&propertySize,0),"verify export policy");
        Check(propertySize==sizeof(exportPolicy) && exportPolicy==0,"private key must remain nonexportable");
        DWORD size=0;Check(CertStrToNameW(X509_ASN_ENCODING,subject,CERT_X500_NAME_STR,nullptr,nullptr,&size,nullptr)!=FALSE,"DN size");
        Bytes encodedName(size);Check(CertStrToNameW(X509_ASN_ENCODING,subject,CERT_X500_NAME_STR,nullptr,encodedName.data(),&size,nullptr)!=FALSE,"DN encode");
        CERT_NAME_BLOB dn{size,encodedName.data()};auto* oid=const_cast<char*>(eku);CERT_ENHKEY_USAGE usage{1,&oid};
        DWORD encodedSize=0;Check(CryptEncodeObjectEx(X509_ASN_ENCODING,X509_ENHANCED_KEY_USAGE,&usage,0,nullptr,nullptr,&encodedSize)!=FALSE,"EKU size");
        Bytes encoded(encodedSize);Check(CryptEncodeObjectEx(X509_ASN_ENCODING,X509_ENHANCED_KEY_USAGE,&usage,0,nullptr,encoded.data(),&encodedSize)!=FALSE,"EKU encode");
        CERT_EXTENSION extension{const_cast<char*>(szOID_ENHANCED_KEY_USAGE),FALSE,{encodedSize,encoded.data()}};
        CERT_EXTENSIONS extensions{1,&extension};CRYPT_ALGORITHM_IDENTIFIER algorithm{const_cast<char*>(szOID_ECDSA_SHA256),{0,nullptr}};
        FILETIME now{};GetSystemTimeAsFileTime(&now);ULARGE_INTEGER ticks{};ticks.LowPart=now.dwLowDateTime;ticks.HighPart=now.dwHighDateTime;
        const auto date=[&](LONGLONG seconds){ULARGE_INTEGER value{};value.QuadPart=static_cast<ULONGLONG>(static_cast<LONGLONG>(ticks.QuadPart)+seconds*10000000);
            FILETIME ft{value.LowPart,value.HighPart};SYSTEMTIME st{};Check(FileTimeToSystemTime(&ft,&st)!=FALSE,"date");return st;};
        auto start=date(-7200),end=date(expired?-3600:3600);
        cert=CertCreateSelfSignCertificate(key,&dn,CERT_CREATE_SELFSIGN_NO_KEY_INFO,nullptr,&algorithm,&start,&end,&extensions);
        if(!cert)Status(static_cast<SECURITY_STATUS>(GetLastError()),"self-sign ephemeral certificate");
        Check(cert!=nullptr,"self-sign certificate");
        CERT_KEY_CONTEXT context{};context.cbSize=sizeof(context);context.hNCryptKey=key;context.dwKeySpec=CERT_NCRYPT_KEY_SPEC;
        Check(CertSetCertificateContextProperty(cert,CERT_KEY_CONTEXT_PROP_ID,CERT_STORE_NO_CRYPT_RELEASE_FLAG,&context)!=FALSE,"attach ephemeral private key");
        if(!name.empty()){
            CRYPT_KEY_PROV_INFO info{};info.pwszContainerName=name.data();info.pwszProvName=const_cast<wchar_t*>(MS_KEY_STORAGE_PROVIDER);
            // CNG provider info passes this as NCryptOpenKey dwLegacyKeySpec (zero).
            // CERT_KEY_CONTEXT above instead requires CERT_NCRYPT_KEY_SPEC.
            info.dwKeySpec=0;
            Check(CertSetCertificateContextProperty(cert,CERT_KEY_PROV_INFO_PROP_ID,0,&info)!=FALSE,"associate temporary key name");
        }
        HCRYPTPROV_OR_NCRYPT_KEY_HANDLE acquired=0;DWORD kind=0;BOOL release=FALSE;
        if(!CryptAcquireCertificatePrivateKey(cert,CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG|CRYPT_ACQUIRE_SILENT_FLAG|
            CRYPT_ACQUIRE_CACHE_FLAG|CRYPT_ACQUIRE_NO_HEALING,nullptr,&acquired,&kind,&release))
            Status(static_cast<SECURITY_STATUS>(GetLastError()),"retrieve attached ephemeral private key");
        Check(kind==CERT_NCRYPT_KEY_SPEC,"CNG private key kind");
        std::array<unsigned char,32> digest{};DWORD signatureSize=0;
        const auto signingStatus=NCryptSignHash(acquired,nullptr,digest.data(),static_cast<DWORD>(digest.size()),nullptr,0,&signatureSize,0);
        if(release)NCryptFreeObject(acquired);
        Status(signingStatus,"ephemeral key signing readiness");Check(signatureSize>0,"signature size");
    }catch(...){Release();throw;}}
    Identity(const Identity&)=delete;
    void Release() noexcept {
        if(cert)CertFreeCertificateContext(cert);cert=nullptr;
        if(key){
            if(created){const auto status=NCryptDeleteKey(key,0);
                if(status==ERROR_SUCCESS)++keysDeleted;
                else{cleanupFailure=true;std::fprintf(stderr,"DELETE FAILED %ls: 0x%08lx\n",name.c_str(),static_cast<unsigned long>(status));NCryptFreeObject(key);}}
            else NCryptFreeObject(key);
        }
        key=0;
        if(created && provider){NCRYPT_KEY_HANDLE remaining=0;const auto status=NCryptOpenKey(provider,&remaining,name.c_str(),0,NCRYPT_SILENT_FLAG);
            if(status==NTE_BAD_KEYSET){++keysAbsent;std::printf("ABSENT verified: %ls\n",name.c_str());}
            else{cleanupFailure=true;std::fprintf(stderr,"ABSENCE NOT PROVED %ls: 0x%08lx\n",name.c_str(),static_cast<unsigned long>(status));if(remaining)NCryptFreeObject(remaining);}}
        created=false;if(provider)NCryptFreeObject(provider);provider=0;
    }
    ~Identity(){Release();}
};

using timox_broker::PipeStream;
using timox_broker::SchannelStream;
using Code=NativeChannelCode;
using namespace std::chrono_literals;
void TimeoutCompletionAccounting(){
    // Controlled completion outcomes: these represent bytes already copied by
    // the OS, independently of which thread wins the cancellation race.
    std::size_t buffered=7;
    NativeChannelResult timeout{Code::TimedOut,13,WAIT_TIMEOUT};
    Check(timox_broker::detail::RetainCiphertextCompletion(timeout,32,buffered) &&
        buffered==20 && timeout.bytes==0 && timeout.code==Code::TimedOut,
        "timeout lost completed ciphertext or exposed it as plaintext");
    NativeChannelResult resumed{Code::Ok,12};
    Check(timox_broker::detail::RetainCiphertextCompletion(resumed,32,buffered) && buffered==32,
        "retry overwrote ciphertext consumed during timeout");
    NativeChannelResult idle{Code::TimedOut,0,WAIT_TIMEOUT};
    Check(timox_broker::detail::RetainCiphertextCompletion(idle,32,buffered) && buffered==32,
        "idle timeout changed retained ciphertext");
    NativeChannelResult invalid{Code::TimedOut,1,WAIT_TIMEOUT};
    Check(!timox_broker::detail::RetainCiphertextCompletion(invalid,32,buffered) && buffered==32,
        "completion accounting exceeded receive capacity");
}
auto Deadline(){return std::chrono::steady_clock::now()+5s;}
std::wstring PipeName(){
    std::array<unsigned char,16> nonce{};
    Status(BCryptGenRandom(nullptr,nonce.data(),static_cast<ULONG>(nonce.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG),"pipe nonce");
    std::wstring name=L"\\\\.\\pipe\\Timox.NativeAudio.tls-test.";
    for(auto c:nonce){name+=L"0123456789abcdef"[c>>4];name+=L"0123456789abcdef"[c&15];}return name;
}
struct Pair {
    std::unique_ptr<PipeStream> server=std::make_unique<PipeStream>(),client=std::make_unique<PipeStream>();
    Pair(){const auto name=PipeName();Check(server->CreateServer(name.c_str(),nullptr).code==Code::Ok,"pipe creation");
        NativeChannelResult accepted;std::thread thread([&]{accepted=server->Accept(Deadline());});
        const auto connected=client->Connect(name.c_str(),Deadline());thread.join();
        Check(accepted.code==Code::Ok && connected.code==Code::Ok,"pipe connection");}
};
// Ciphertext relay is test-only. Fragment actual pipe writes, never SSPI hooks.
struct Duplex {
    Pair a,b;
    std::atomic<bool> tamper{false};
    std::atomic<bool> stopping{false};
    std::thread forward,backward;
    std::unique_ptr<SchannelStream> client,server;
    Duplex(Identity& serverIdentity,Identity* clientIdentity,Pin serverPin,Pin clientPin){
        client=std::make_unique<SchannelStream>(std::move(a.client),false,clientIdentity?clientIdentity->cert:nullptr,serverPin);
        server=std::make_unique<SchannelStream>(std::move(b.server),true,serverIdentity.cert,clientPin);
        forward=std::thread([this]{Relay(*a.server,*b.client,true);});
        backward=std::thread([this]{Relay(*b.client,*a.server,false);});
    }
    DWORD Transfer(HANDLE pipe,void* data,DWORD size,bool write){
        if(stopping)return 0;
        OVERLAPPED operation{};operation.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!operation.hEvent)return 0;
        const BOOL immediate=write?WriteFile(pipe,data,size,nullptr,&operation):ReadFile(pipe,data,size,nullptr,&operation);
        const auto error=immediate?ERROR_SUCCESS:GetLastError();DWORD transferred=0;
        if(immediate || error==ERROR_IO_PENDING){
            while(!immediate && WaitForSingleObject(operation.hEvent,20)==WAIT_TIMEOUT){
                if(stopping){CancelIoEx(pipe,&operation);break;}}
            if(!GetOverlappedResult(pipe,&operation,&transferred,TRUE))transferred=0;
        }
        CloseHandle(operation.hEvent);return transferred;
    }
    void Relay(PipeStream& from,PipeStream& to,bool corruptible){
        std::array<unsigned char,17> data{};
        for(;;){const auto bytes=Transfer(from.BorrowHandle(),data.data(),static_cast<DWORD>(data.size()),false);if(!bytes)return;
            if(corruptible && tamper.exchange(false))data[bytes-1]^=0x80;
            if(Transfer(to.BorrowHandle(),data.data(),bytes,true)!=bytes)return;}
    }
    ~Duplex(){stopping=true;CancelIoEx(a.server->BorrowHandle(),nullptr);CancelIoEx(b.client->BorrowHandle(),nullptr);forward.join();backward.join();}
    bool Authenticate(){NativeChannelResult remote;std::thread thread([&]{remote=server->Handshake(Deadline());});
        const auto local=client->Handshake(Deadline());thread.join();
        return local.code==Code::Ok && remote.code==Code::Ok;}
};
void ConcreteCancellation(Identity& serverIdentity,Identity& clientIdentity){
    const auto name=PipeName();PipeStream peer;
    Check(peer.CreateServer(name.c_str(),nullptr).code==Code::Ok,"cancel fixture create");
    NativeAudioChannelCredentials credentials{clientIdentity.cert,Hash(serverIdentity.cert)};
    auto channel=CreateWindowsNativeAudioAuthenticatedChannelForTest(credentials,name.c_str());
    NativeChannelResult result;
    std::thread connecting([&]{result=channel->Connect(Deadline());});
    const auto accepted=peer.Accept(Deadline());std::array<unsigned char,4096> hello{};
    const auto read=accepted.code==Code::Ok?peer.ReadSome(hello.data(),hello.size(),Deadline()):NativeChannelResult{};
    // A received ClientHello proves Connect is active, not just scheduled.
    channel->RequestCancel();connecting.join();
    Check(accepted.code==Code::Ok&&read.code==Code::Ok&&read.bytes>0,"cancel fixture never reached TLS");
    Check(result.code==Code::Cancelled,"concrete handshake cancellation failed");
    channel->Close();
    Check(channel->Connect(Deadline()).code==Code::Cancelled,"close reset concrete cancellation");
    Check(channel->ConfirmLease(17,23,Deadline()).code==Code::Cancelled,"cancelled concrete lease confirmed");
    std::array<unsigned char,48> output;output.fill(42);
    Check(channel->Exchange(nullptr,0,output.data(),output.size(),Deadline()).code==Code::Cancelled&&
        std::all_of(output.begin(),output.end(),[](auto b){return b==0;}),"cancelled concrete exchange leaked output");
}
Bytes ConcretePacket(bool render){
    Bytes bytes(64+4096*8);
    const auto put=[&](std::size_t at,std::uint64_t value,unsigned width){for(unsigned n=0;n<width;++n)bytes[at+n]=static_cast<unsigned char>(value>>(8*n));};
    put(0,0x41575654,4);put(4,64,4);put(8,1,2);put(12,render?1:2,2);put(14,render?1:2,2);
    put(16,23,8);put(24,1,8);put(40,4096,4);put(44,48000,4);put(48,2,2);put(50,1,2);put(52,4096*8,4);
    for(std::size_t n=64;n<bytes.size();++n)bytes[n]=static_cast<unsigned char>(n*37+11);
    return bytes;
}
struct ConcreteAudioState {
    unsigned opens=0,closes=0,owners=0,maxOwners=0,writes=0;
    Bytes render=ConcretePacket(true);
    std::array<unsigned char,timox_broker::MaxPayloadBytes> captured{};
    std::uint32_t captureBytes=0;
};
class ConcreteAudio final:public INativeAudioTransport {
    std::shared_ptr<ConcreteAudioState> state_;bool owned_=false;
public:
    explicit ConcreteAudio(std::shared_ptr<ConcreteAudioState> state):state_(std::move(state)){}
    NativeTransferResult Open(NativeSessionInfo& info)noexcept override{
        ++state_->opens;++state_->owners;owned_=true;state_->maxOwners=(std::max)(state_->maxOwners,state_->owners);
        info.wireGeneration=23;return {NativeTransferCode::Ok};
    }
    NativeTransferResult ReadRender(std::uint32_t frames,void* out,std::uint32_t capacity)noexcept override{
        if(frames<4096||capacity<state_->render.size())return {NativeTransferCode::InvalidPacket};
        std::memcpy(out,state_->render.data(),state_->render.size());
        return {NativeTransferCode::Ok,static_cast<std::uint32_t>(state_->render.size())};
    }
    NativeTransferResult WriteCapture(const void* data,std::uint32_t bytes)noexcept override{
        ++state_->writes;if(state_->writes==1)return {NativeTransferCode::Full};
        if(bytes>state_->captured.size())return {NativeTransferCode::InvalidPacket};
        std::memcpy(state_->captured.data(),data,bytes);state_->captureBytes=bytes;return {NativeTransferCode::Ok};
    }
    NativeTransferResult ReadStatus(std::uint64_t (&values)[3])noexcept override{values[0]=12;values[1]=34;values[2]=56;return {NativeTransferCode::Ok};}
    void RequestCancel()noexcept override{}
    void Close()noexcept override{if(owned_){owned_=false;++state_->closes;--state_->owners;}}
};
std::string ObservedSidText(const std::array<std::uint8_t,68>& sid){
    LPSTR text=nullptr;Check(ConvertSidToStringSidA(const_cast<std::uint8_t*>(sid.data()),&text)!=FALSE,"observed SID conversion");
    try{std::string result(text);LocalFree(text);return result;}catch(...){LocalFree(text);throw;}
}
void ConcreteChannel(Identity& serverIdentity,Identity& clientIdentity,unsigned fault=0){
    using namespace timox_broker;
    const auto name=PipeName();auto bootstrapPipe=std::make_unique<PipeStream>();
    Check(bootstrapPipe->CreateServer(name.c_str(),nullptr).code==Code::Ok,"concrete bootstrap create");
    const auto bootstrapHandle=bootstrapPipe->BorrowHandle();
    auto dataPipe=std::make_unique<PipeStream>();
    ChannelBinding offer;offer.type=BindingType::DataOffer;offer.lease=17;offer.generation=23;offer.nonce[15]=42;
    auto dataName=name+L".data.";
    for(auto c:offer.nonce){dataName+=L"0123456789abcdef"[c>>4];dataName+=L"0123456789abcdef"[c&15];}
    Check(dataPipe->CreateServer(dataName.c_str(),nullptr).code==Code::Ok,"concrete data create");
    const auto dataHandle=dataPipe->BorrowHandle();
    NativeAudioChannelCredentials credentials{clientIdentity.cert,Hash(serverIdentity.cert)};
    auto transport=CreateBrokerNativeAudioTransport(CreateWindowsNativeAudioAuthenticatedChannelForTest(credentials,name.c_str()));
    const auto audio=std::make_shared<ConcreteAudioState>();
    // Complete potentially throwing allocations before starting a joinable
    // thread, so allocation failure can still unwind and clean up test keys.
    Bytes render(MaxPayloadBytes);const auto capture=ConcretePacket(false);
    std::exception_ptr failure;
    std::thread peer([&]{try{
        Check(bootstrapPipe->Accept(Deadline()).code==Code::Ok,"concrete bootstrap accept");
        SchannelStream bootstrap(std::move(bootstrapPipe),true,serverIdentity.cert,Hash(clientIdentity.cert));
        Check(bootstrap.Handshake(Deadline()).code==Code::Ok,"concrete bootstrap TLS");
        ChannelBinding binding;ChannelIdentity observed;
        Check(ReadBindingFrame(bootstrap,binding,Deadline()).code==Code::Ok,"concrete bootstrap binding read");
        Check(ObservePipeIdentity(bootstrapHandle,observed)==ERROR_SUCCESS&&SameChannelIdentity(binding.identity,observed),"concrete observed bootstrap identity");
        Check(binding.type==BindingType::BindRequest,"concrete bootstrap binding type");
        binding.type=BindingType::BindReply;
        if(fault==1)++binding.identity.authenticationId;
        Check(WriteBindingFrame(bootstrap,binding,Deadline()).code==Code::Ok,"concrete bootstrap binding reply");
        if(fault==1)return;
        NativePeerIdentity identity{ObservedSidText(observed.userSid),ObservedSidText(observed.logonSid),observed.authenticationId,observed.sessionId};
        // Explicit synthetic isolation policy: exercises transport/admission,
        // never qualifies Windows endpoints on the current machine.
        NativeSessionPolicySnapshot policy;policy.allowedUserSid=identity.userSid;policy.consoleSessionId=identity.sessionId;
        policy.interactiveLogonCount=1;policy.unlocked=true;policy.audioIsolationQualified=true;policy.optionalConsoleIdentity=identity;
        BrokerDispatcher broker([audio]{return std::make_unique<ConcreteAudio>(audio);},policy);
        const auto attached=broker.AttachAuthenticated(identity);Check(attached.id&&attached.status==Status::Ok,"real broker attach");
        struct Detach {BrokerDispatcher& broker;ConnectionId id;~Detach(){broker.Disconnect(id);}} detach{broker,attached.id};
        std::array<unsigned char,MaxMessageBytes> request{},response{};
        const auto exchange=[&](SchannelStream& stream,Opcode expected){
            const auto read=ReadAudioFrame(stream,request.data(),request.size(),Deadline());
            Check(read.code==Code::Ok&&request[6]==static_cast<unsigned>(expected),"concrete broker request");
            const auto dispatched=broker.Process(attached.id,request.data(),read.bytes,response.data(),response.size());
            Check(dispatched.disposition==Dispatch::Reply,"real broker dispatch refused");
            Check(stream.WriteAll(response.data(),dispatched.bytes,Deadline()).code==Code::Ok,"real broker reply");
            return dispatched;
        };
        Check(exchange(bootstrap,Opcode::Session).status==Status::Ok,"real broker SESSION");
        offer.lease=frames_detail::Read(response.data(),24,8);offer.generation=frames_detail::Read(response.data(),48,8);
        if(fault==7)return; // EOF after successful SESSION must retire its owner.
        if(fault==2)++offer.lease;
        Check(WriteBindingFrame(bootstrap,offer,Deadline()).code==Code::Ok,"concrete data offer");
        if(fault==2)return;
        Check(dataPipe->Accept(Deadline()).code==Code::Ok,"concrete data accept");
        SchannelStream data(std::move(dataPipe),true,serverIdentity.cert,Hash(clientIdentity.cert));
        Check(data.Handshake(Deadline()).code==Code::Ok,"concrete data TLS");
        ChannelBinding dataBinding;
        Check(ReadBindingFrame(data,dataBinding,Deadline()).code==Code::Ok,"concrete data binding read");
        ChannelBinding expected=offer;expected.type=BindingType::DataBindRequest;expected.identity=observed;
        Check(MatchesBinding(dataBinding,expected)&&ObservePipeIdentity(dataHandle,observed)==ERROR_SUCCESS&&
            SameChannelIdentity(dataBinding.identity,observed),"concrete data identity/lease differs");
        dataBinding.type=BindingType::DataBindReply;
        if(fault==3)++dataBinding.identity.authenticationId;
        if(fault==4)dataBinding.nonce[0]^=1;
        if(fault==5)++dataBinding.generation;
        if(fault==6)++dataBinding.lease;
        Check(WriteBindingFrame(data,dataBinding,Deadline()).code==Code::Ok,"concrete data binding reply");
        if(fault>=3)return;
        bootstrap.Close();
        for(auto op:{Opcode::ReadRender,Opcode::WriteCapture,Opcode::WriteCapture,Opcode::Status,Opcode::Close})exchange(data,op);
    }catch(...){failure=std::current_exception();}});
    NativeSessionInfo session;const auto opened=transport->Open(session);
    NativeTransferResult rendered,full,written;
    if(opened.code==NativeTransferCode::Ok){
        rendered=transport->ReadRender(4096,render.data(),static_cast<std::uint32_t>(render.size()));
        full=transport->WriteCapture(capture.data(),static_cast<std::uint32_t>(capture.size()));
        written=transport->WriteCapture(capture.data(),static_cast<std::uint32_t>(capture.size()));
    }
    std::uint64_t counters[3]{};const auto status=opened.code==NativeTransferCode::Ok?transport->ReadStatus(counters):NativeTransferResult{};
    transport->Close();peer.join();
    Check(audio->owners==0&&audio->opens==audio->closes&&audio->maxOwners<=1,"real broker owner leaked after connection exit");
    if(fault){
        if(failure)std::rethrow_exception(failure);
        Check(opened.code==(fault==7?NativeTransferCode::Removed:NativeTransferCode::AccessDenied)&&session.wireGeneration==0,"concrete channel accepted mismatched binding or SESSION EOF");
        return;
    }
    Check(opened.code==NativeTransferCode::Ok&&session.wireGeneration==23,"concrete authenticated channel open failed");
    Check(rendered.code==NativeTransferCode::Ok&&rendered.bytes==render.size()&&render==ConcretePacket(true),"PCM render differs through TLS client/broker");
    Check(full.code==NativeTransferCode::Full&&written.code==NativeTransferCode::Ok,"capture Full retry failed through TLS client/broker");
    Check(audio->writes==2&&audio->captureBytes==capture.size()&&std::equal(capture.begin(),capture.end(),audio->captured.begin()),"capture PCM changed through TLS client/broker");
    if(failure)std::rethrow_exception(failure);
    Check(status.code==NativeTransferCode::Ok&&counters[0]==12&&counters[1]==34&&counters[2]==56,"concrete status reply differs");
}
void UnixTls(Identity& serverIdentity,Identity& clientIdentity,bool wrongPin){
    using namespace timox_broker;
    struct Path {
        std::string value=std::filesystem::absolute("tls-unix-"+std::to_string(GetCurrentProcessId())+".sock").string();
        Path(){Check(!std::filesystem::exists(value),"TLS socket path exists");}
        ~Path(){std::error_code error;std::filesystem::remove(value,error);}
    } path;
    UnixSocketStream listener;auto clientPipe=std::make_unique<UnixSocketStream>();auto serverPipe=std::make_unique<UnixSocketStream>();
    Check(listener.Listen(path.value.c_str()).code==Code::Ok,"TLS socket listen");
    Check(clientPipe->Connect(path.value.c_str(),Deadline()).code==Code::Ok,"TLS socket connect");
    Check(listener.Accept(*serverPipe,Deadline()).code==Code::Ok,"TLS socket accept");
    auto serverPin=Hash(serverIdentity.cert);if(wrongPin)serverPin[0]^=1;
    SchannelStream server(std::move(serverPipe),true,serverIdentity.cert,Hash(clientIdentity.cert));
    SchannelStream client(std::move(clientPipe),false,clientIdentity.cert,serverPin);
    std::array<unsigned char,32768> sent{},received{},echo{};
    for(std::size_t n=0;n<sent.size();++n)sent[n]=static_cast<unsigned char>(n*31);
    NativeChannelResult peerResult;
    std::thread peer([&]{
        peerResult=server.Handshake(Deadline());if(peerResult.code!=Code::Ok)return;
        peerResult=frames_detail::Exact(server,received.data(),received.size(),Deadline());
        if(peerResult.code==Code::Ok)peerResult=server.WriteAll(received.data(),received.size(),Deadline());
    });
    const auto handshake=client.Handshake(Deadline());NativeChannelResult sentResult,echoResult;
    if(handshake.code==Code::Ok){
        sentResult=client.WriteAll(sent.data(),sent.size(),Deadline());
        if(sentResult.code==Code::Ok)echoResult=frames_detail::Exact(client,echo.data(),echo.size(),Deadline());
    }
    client.Close();peer.join();server.Close();
    if(wrongPin){Check(handshake.code==Code::AccessDenied,"AF_UNIX TLS accepted wrong pin");return;}
    Check(handshake.code==Code::Ok&&peerResult.code==Code::Ok&&sentResult.code==Code::Ok&&echoResult.code==Code::Ok,"AF_UNIX TLS duplex failed");
    Check(sent==received&&sent==echo,"AF_UNIX TLS payload changed");
}
void SocketBroker(Identity& serverIdentity,Identity& clientIdentity,unsigned fault){
    using namespace timox_broker;
    struct Path {
        std::string value=std::filesystem::absolute("broker-unix-"+std::to_string(GetCurrentProcessId())+".sock").string();
        Path(){Check(!std::filesystem::exists(value),"broker socket path exists");}
        ~Path(){std::error_code error;std::filesystem::remove(value,error);}
    } path;
    ChannelIdentity identity;Check(ObserveProcessIdentity(identity)==ERROR_SUCCESS,"socket fixture identity");
    NativePeerIdentity peer{ObservedSidText(identity.userSid),ObservedSidText(identity.logonSid),identity.authenticationId,identity.sessionId};
    NativeSessionPolicySnapshot policy;policy.allowedUserSid=peer.userSid;policy.consoleSessionId=peer.sessionId;
    policy.interactiveLogonCount=1;policy.unlocked=true;policy.audioIsolationQualified=true;policy.optionalConsoleIdentity=peer;
    const auto audio=std::make_shared<ConcreteAudioState>();
    BrokerDispatcher dispatcher([audio]{return std::make_unique<ConcreteAudio>(audio);},policy);
    // Synthetic trusted enrollment: this is not a production OS provisioner.
    auto enrollment=std::make_shared<SocketEnrollment>(Hash(clientIdentity.cert),identity,1);
    UnixSocketStream listener;auto local=std::make_unique<UnixSocketStream>(),remote=std::make_unique<UnixSocketStream>();
    Check(listener.Listen(path.value.c_str()).code==Code::Ok,"broker socket listen");
    Check(local->Connect(path.value.c_str(),Deadline()).code==Code::Ok&&listener.Accept(*remote,Deadline()).code==Code::Ok,"broker socket connect");
    SocketSession session(dispatcher,std::move(remote),serverIdentity.cert,enrollment);
    SchannelStream client(std::move(local),false,clientIdentity.cert,Hash(serverIdentity.cert));
    std::array<unsigned char,MaxMessageBytes> request{},response{};
    const auto capture=ConcretePacket(false),render=ConcretePacket(true);
    std::promise<void> completed;auto completion=completed.get_future();
    NativeChannelResult result;std::thread worker([&]{result=session.Run();completed.set_value();});
    struct Join {SocketSession& session;SchannelStream& client;std::thread& worker;
        ~Join(){session.RequestCancel();client.Close();if(worker.joinable())worker.join();}} join{session,client,worker};
    Check(client.Handshake(Deadline()).code==Code::Ok,"socket broker TLS");
    ChannelBinding binding;binding.identity=identity;if(fault==1)++binding.identity.authenticationId;
    Check(WriteBindingFrame(client,binding,Deadline()).code==Code::Ok,"socket broker bind send");
    const auto bound=ReadBindingFrame(client,binding,Deadline());
    std::uint64_t requestId=0,lease=0;
    const auto put=[&](std::size_t at,std::uint64_t value,unsigned count){for(unsigned n=0;n<count;++n)request[at+n]=static_cast<unsigned char>(value>>(8*n));};
    const auto exchange=[&](Opcode op,const void* payload,std::size_t bytes){
        request.fill(0);put(0,Magic,4);put(4,1,2);put(6,static_cast<unsigned>(op),2);put(8,48,4);put(12,bytes,4);put(16,++requestId,8);put(24,lease,8);
        if(bytes)std::memcpy(request.data()+48,payload,bytes);
        Check(client.WriteAll(request.data(),48+bytes,Deadline()).code==Code::Ok,"socket broker request");
        return ReadAudioFrame(client,response.data(),response.size(),Deadline());
    };
    if(fault==1)Check(bound.code!=Code::Ok,"wrong socket identity accepted");
    else {
        Check(bound.code==Code::Ok&&binding.type==BindingType::BindReply,"socket broker bind reply");
        if(fault==3)Check(exchange(Opcode::ReadRender,nullptr,0).code!=Code::Ok,"PCM before SESSION accepted");
        else {
            Check(exchange(Opcode::Session,nullptr,0).code==Code::Ok&&frames_detail::Read(response.data(),32,4)==0,"socket SESSION");
            lease=frames_detail::Read(response.data(),24,8);
            if(fault!=4){
                ChannelBinding offer;Check(ReadBindingFrame(client,offer,Deadline()).code==Code::Ok,"socket offer");
                if(fault==5){
                    enrollment->Revoke();session.RequestCancel();
                    // Keep the client open: EOF cannot explain completion.
                    Check(completion.wait_for(std::chrono::seconds(2))==std::future_status::ready,"revoked socket did not finish while client stayed open");
                    Check(result.code==Code::Cancelled,"revoked socket result not Cancelled");
                }
                else if(fault==6)Check(exchange(Opcode::ReadRender,nullptr,0).code!=Code::Ok,"PCM before confirmation accepted");
                else {
                    offer.type=BindingType::DataBindRequest;offer.identity=identity;if(fault==2)++offer.lease;
                    if(fault==7)offer.nonce[0]^=1;
                    if(fault==8)++offer.generation;
                    Check(WriteBindingFrame(client,offer,Deadline()).code==Code::Ok,"socket confirmation send");
                    const auto confirmed=ReadBindingFrame(client,binding,Deadline());
                    if(fault==2||fault==7||fault==8)Check(confirmed.code!=Code::Ok,"wrong socket lease/nonce/generation accepted");
                    else {
                        Check(confirmed.code==Code::Ok&&binding.type==BindingType::DataBindReply,"socket confirmation reply");
                        std::array<unsigned char,16> readPayload{};readPayload[0]=23;readPayload[9]=16;
                        Check(exchange(Opcode::ReadRender,readPayload.data(),readPayload.size()).bytes==48+render.size()&&
                            std::equal(render.begin(),render.end(),response.begin()+48),"socket render differs");
                        Check(exchange(Opcode::WriteCapture,capture.data(),capture.size()).code==Code::Ok&&frames_detail::Read(response.data(),32,4)==static_cast<unsigned>(Status::Full),"socket capture Full");
                        Check(exchange(Opcode::WriteCapture,capture.data(),capture.size()).code==Code::Ok&&frames_detail::Read(response.data(),32,4)==0,"socket capture retry");
                        Check(exchange(Opcode::Close,readPayload.data(),8).code==Code::Ok,"socket CLOSE");
                    }
                }
            }
        }
    }
    client.Close();worker.join();dispatcher.Stop();
    Check(audio->owners==0&&audio->opens==audio->closes,"socket broker owner leaked");
    if(fault)Check(result.code!=Code::Ok,"invalid socket session reported success");
    if(!fault)Check(result.code==Code::Ok&&audio->captureBytes==capture.size()&&std::equal(capture.begin(),capture.end(),audio->captured.begin()),"socket PCM/session failed");
    if(fault==1||fault==3)Check(audio->opens==0,"unbound socket opened native audio");
}
void ConcreteSocketClient(Identity& serverIdentity,Identity& clientIdentity){
    using namespace timox_broker;
    struct Path {
        std::string value=std::filesystem::absolute("client-unix-"+std::to_string(GetCurrentProcessId())+".sock").string();
        Path(){Check(!std::filesystem::exists(value),"client socket path exists");}
        ~Path(){std::error_code error;std::filesystem::remove(value,error);}
    } path;
    ChannelIdentity identity;Check(ObserveProcessIdentity(identity)==ERROR_SUCCESS,"client socket identity");
    NativePeerIdentity peer{ObservedSidText(identity.userSid),ObservedSidText(identity.logonSid),identity.authenticationId,identity.sessionId};
    NativeSessionPolicySnapshot policy;policy.allowedUserSid=peer.userSid;policy.consoleSessionId=peer.sessionId;
    policy.interactiveLogonCount=1;policy.unlocked=true;policy.audioIsolationQualified=true;policy.optionalConsoleIdentity=peer;
    const auto audio=std::make_shared<ConcreteAudioState>();
    BrokerDispatcher broker([audio]{return std::make_unique<ConcreteAudio>(audio);},policy);
    auto enrollment=std::make_shared<SocketEnrollment>(Hash(clientIdentity.cert),identity,1);
    UnixSocketStream listener;Check(listener.Listen(path.value.c_str()).code==Code::Ok,"client fixture listen");
    auto socket=std::make_unique<UnixSocketStream>();auto* accepted=socket.get();
    SocketSession session(broker,std::move(socket),serverIdentity.cert,enrollment);
    auto transport=CreateBrokerNativeAudioTransport(CreateWindowsNativeAudioSocketChannel(
        {clientIdentity.cert,Hash(serverIdentity.cert)},path.value.c_str()));
    Bytes render(MaxPayloadBytes);const auto capture=ConcretePacket(false);
    NativeChannelResult served;std::thread worker([&]{served=listener.Accept(*accepted,Deadline());if(served.code==Code::Ok)served=session.Run();});
    struct Join {UnixSocketStream& listener;SocketSession& session;INativeAudioTransport& transport;std::thread& worker;
        ~Join(){listener.RequestCancel();session.RequestCancel();transport.RequestCancel();transport.Close();if(worker.joinable())worker.join();}} join{listener,session,*transport,worker};
    NativeSessionInfo info;Check(transport->Open(info).code==NativeTransferCode::Ok&&info.wireGeneration==23,"concrete socket client Open");
    {
        auto otherSocket=std::make_unique<UnixSocketStream>();auto* otherAccepted=otherSocket.get();
        SocketSession otherSession(broker,std::move(otherSocket),serverIdentity.cert,enrollment);
        auto otherTransport=CreateBrokerNativeAudioTransport(CreateWindowsNativeAudioSocketChannel(
            {clientIdentity.cert,Hash(serverIdentity.cert)},path.value.c_str()));
        NativeChannelResult otherServed;
        std::thread otherWorker([&]{otherServed=listener.Accept(*otherAccepted,Deadline());if(otherServed.code==Code::Ok)otherServed=otherSession.Run();});
        Join otherJoin{listener,otherSession,*otherTransport,otherWorker};
        NativeSessionInfo otherInfo;
        Check(otherTransport->Open(otherInfo).code==NativeTransferCode::OwnedElsewhere&&otherInfo.wireGeneration==0,"second socket owner accepted");
        otherTransport->Close();otherWorker.join();
        Check(otherServed.code==Code::AccessDenied,"second owner session did not retire");
    }
    Check(transport->ReadRender(4096,render.data(),static_cast<std::uint32_t>(render.size())).code==NativeTransferCode::Ok&&render==ConcretePacket(true),"concrete socket render");
    Check(transport->WriteCapture(capture.data(),static_cast<std::uint32_t>(capture.size())).code==NativeTransferCode::Full,"concrete socket Full");
    Check(transport->WriteCapture(capture.data(),static_cast<std::uint32_t>(capture.size())).code==NativeTransferCode::Ok,"concrete socket capture retry");
    std::uint64_t counters[3]{};Check(transport->ReadStatus(counters).code==NativeTransferCode::Ok&&counters[0]==12&&counters[1]==34&&counters[2]==56,"concrete socket status");
    transport->Close();worker.join();broker.Stop();
    Check(served.code==Code::Ok&&audio->owners==0&&audio->opens==audio->closes,"concrete socket teardown");
    Check(audio->captureBytes==capture.size()&&std::equal(capture.begin(),capture.end(),audio->captured.begin()),"concrete socket capture changed");
}
void Tests(Identity& serverIdentity,Identity& clientIdentity,Identity& expired){
    ConcreteSocketClient(serverIdentity,clientIdentity);
    for(unsigned fault=0;fault<=8;++fault)SocketBroker(serverIdentity,clientIdentity,fault);
    UnixTls(serverIdentity,clientIdentity,false);
    UnixTls(serverIdentity,clientIdentity,true);
    ConcreteCancellation(serverIdentity,clientIdentity);
    for(unsigned fault=0;fault<=7;++fault)ConcreteChannel(serverIdentity,clientIdentity,fault);
    const auto serverPin=Hash(serverIdentity.cert),clientPin=Hash(clientIdentity.cert);
    {
        // Preserve the bootstrap connection (and buffered plaintext) while a
        // separately authenticated data connection is established. No scratch
        // sharing, implicit closing, or loss of bootstrap leftovers is allowed.
        Duplex bootstrap(serverIdentity,&clientIdentity,serverPin,clientPin);
        Check(bootstrap.Authenticate(),"bootstrap memory fixture handshake");
        const std::array<unsigned char,3> marker{{7,8,9}};std::array<unsigned char,3> received{};
        Check(bootstrap.server->WriteAll(marker.data(),marker.size(),Deadline()).code==Code::Ok,"bootstrap memory fixture write");
        Check(bootstrap.client->ReadSome(received.data(),1,Deadline()).bytes==1&&received[0]==7,"bootstrap initial byte");
        Duplex data(serverIdentity,&clientIdentity,serverPin,clientPin);
        Check(data.Authenticate(),"second retained stream handshake");
        Check(data.client->WriteAll(marker.data(),marker.size(),Deadline()).code==Code::Ok,"second retained stream write");
        Check(data.server->ReadSome(received.data(),received.size(),Deadline()).bytes==3&&received==marker,"second retained stream payload");
        received.fill(0);
        Check(bootstrap.client->ReadSome(received.data(),received.size(),Deadline()).bytes==2&&received[0]==8&&received[1]==9,"data handshake destroyed bootstrap leftovers");
    }
    {
        Duplex wire(serverIdentity,&clientIdentity,serverPin,clientPin);
        Check(wire.Authenticate(),"real pipe mutual Schannel handshake failed");
        {
            timox_broker::ChannelBinding offer;offer.type=timox_broker::BindingType::DataOffer;
            offer.lease=17;offer.generation=23;offer.nonce[15]=42;
            timox_broker::BindingBuffer encoded{};std::size_t size=0;
            Check(timox_broker::EncodeBinding(offer,encoded,size),"binding fixture encode");
            Bytes combined(encoded.begin(),encoded.begin()+size);
            combined.insert(combined.end(),encoded.begin(),encoded.begin()+size);
            Check(wire.server->WriteAll(combined.data(),combined.size(),Deadline()).code==Code::Ok,"coalesced binding write");
            timox_broker::ChannelBinding reply;
            for(unsigned n=0;n<2;++n)Check(timox_broker::ReadBindingFrame(*wire.client,reply,Deadline()).code==Code::Ok&&
                timox_broker::MatchesBinding(reply,offer),"TLS binding boundary lost");
            Check(timox_broker::WriteBindingFrame(*wire.client,offer,Deadline()).code==Code::Ok,"TLS binding frame write");
            Check(timox_broker::ReadBindingFrame(*wire.server,reply,Deadline()).code==Code::Ok&&
                timox_broker::MatchesBinding(reply,offer),"TLS reverse binding frame differs");
            Bytes frame(timox_broker::MaxMessageBytes,0);
            frame[0]='T';frame[1]='V';frame[2]='A';frame[3]='B';frame[4]=1;frame[6]=3;frame[8]=48;
            for(unsigned n=0;n<4;++n)frame[12+n]=static_cast<unsigned char>(timox_broker::MaxPayloadBytes>>(8*n));
            for(std::size_t n=48;n<frame.size();++n)frame[n]=static_cast<unsigned char>(n*13);
            Bytes output(frame.size());NativeChannelResult sentFrame;
            std::thread sender([&]{sentFrame=wire.client->WriteAll(frame.data(),frame.size(),Deadline());});
            const auto read=timox_broker::ReadAudioFrame(*wire.server,output.data(),output.size(),Deadline());
            if(read.code!=Code::Ok)wire.client->RequestCancel();sender.join();
            Check(read.code==Code::Ok&&read.bytes==frame.size()&&sentFrame.code==Code::Ok&&output==frame,"TLS multi-record audio framing differs");
        }
        Bytes expected(32880),received(expected.size());for(std::size_t i=0;i<expected.size();++i)expected[i]=static_cast<unsigned char>(i*19);
        NativeChannelResult sent;std::thread writer([&]{sent=wire.client->WriteAll(expected.data(),expected.size(),Deadline());});
        std::size_t offset=0;bool valid=true;
        while(offset<received.size()){const auto read=wire.server->ReadSome(received.data()+offset,received.size()-offset,Deadline());
            if(read.code!=Code::Ok || !read.bytes || read.bytes>received.size()-offset){valid=false;break;}offset+=read.bytes;}
        if(!valid)wire.client->RequestCancel();writer.join();
        Check(valid && sent.code==Code::Ok && sent.bytes==expected.size() && received==expected,"fragmented multi-record payload differs");
        const std::array<unsigned char,3> one{{0,0xff,0x80}};
        // Back-to-back TLS writes may be coalesced by the byte pipe; each read
        // may split or combine plaintext, and all bytes must be retained.
        Check(wire.server->WriteAll(one.data(),one.size(),Deadline()).code==Code::Ok,"reverse TLS write");
        Check(wire.server->WriteAll(one.data(),one.size(),Deadline()).code==Code::Ok,"second TLS write");
        std::array<unsigned char,6> out{};offset=0;
        while(offset<out.size()){const auto read=wire.client->ReadSome(out.data()+offset,out.size()-offset,Deadline());
            Check(read.code==Code::Ok && read.bytes && read.bytes<=out.size()-offset,"coalesced record read");offset+=read.bytes;}
        for(std::size_t i=0;i<out.size();++i)Check(out[i]==one[i%3],"coalesced plaintext differs");
        Check(wire.server->WriteAll(one.data(),one.size(),Deadline()).code==Code::Ok,"buffered deadline fixture write");
        Check(wire.client->ReadSome(out.data(),1,Deadline()).bytes==1,"buffered deadline fixture read");
        const auto beforeDeadline=out;
        Check(wire.client->ReadSome(out.data(),out.size(),std::chrono::steady_clock::now()-1ms).code==Code::TimedOut && out==beforeDeadline,"expired deadline returned buffered plaintext");
        Check(wire.client->ReadSome(out.data(),out.size(),Deadline()).bytes==2,"deadline consumed buffered plaintext");
        const auto timeout=wire.client->ReadSome(out.data(),out.size(),std::chrono::steady_clock::now()+40ms);
        Check(timeout.code==Code::TimedOut,"TLS idle timeout");
        Check(wire.server->WriteAll(one.data(),one.size(),Deadline()).code==Code::Ok,"buffered cancel fixture write");
        Check(wire.client->ReadSome(out.data(),1,Deadline()).bytes==1,"buffered cancel fixture read");
        wire.client->RequestCancel();const auto beforeCancel=out;
        Check(wire.client->ReadSome(out.data(),out.size(),Deadline()).code==Code::Cancelled && out==beforeCancel,"cancel returned buffered plaintext");
    }
    {auto wrong=serverPin;wrong[0]^=1;Duplex wire(serverIdentity,&clientIdentity,wrong,clientPin);Check(!wire.Authenticate(),"wrong server pin accepted");}
    {auto wrong=clientPin;wrong[0]^=1;Duplex wire(serverIdentity,&clientIdentity,serverPin,wrong);Check(!wire.Authenticate(),"wrong client pin accepted");}
    {Duplex wire(serverIdentity,nullptr,serverPin,clientPin);Check(!wire.Authenticate(),"missing client certificate accepted");}
    {Duplex wire(clientIdentity,&clientIdentity,clientPin,clientPin);Check(!wire.Authenticate(),"client EKU accepted as server");}
    {Duplex wire(expired,&clientIdentity,Hash(expired.cert),clientPin);Check(!wire.Authenticate(),"expired server certificate accepted");}
    {Duplex wire(serverIdentity,&clientIdentity,serverPin,clientPin);Check(wire.Authenticate(),"tamper fixture handshake");
        wire.tamper=true;const std::array<unsigned char,1> value{{42}};
        const auto sent=wire.client->WriteAll(value.data(),value.size(),Deadline());Check(sent.code==Code::Ok,"tamper fixture write");
        std::array<unsigned char,1> out{};Check(wire.server->ReadSome(out.data(),out.size(),Deadline()).code!=Code::Ok,"corrupt TLS record accepted");
        Check(wire.server->ReadSome(out.data(),out.size(),Deadline()).code==Code::Failed,"terminal TLS failure allowed reuse");}
    {Pair pair;SchannelStream client(std::move(pair.client),false,clientIdentity.cert,serverPin);
        SchannelStream server(std::move(pair.server),true,serverIdentity.cert,clientPin);
        NativeChannelResult accepted;std::thread thread([&]{accepted=server.Handshake(Deadline());});
        const auto connected=client.Handshake(Deadline());thread.join();
        Check(accepted.code==Code::Ok && connected.code==Code::Ok,"EOF fixture handshake");
        client.Close();std::array<unsigned char,1> out{};
        Check(server.ReadSome(out.data(),out.size(),Deadline()).code==Code::Removed,"TLS pipe EOF ignored");}
}
}
int main(int argc,char** argv){
    try{
        // Two retained streams, one active handshake, existing adapter request/
        // reply arrays and 16 KiB reserved for pipe objects/channel/frame/EKU
        // scratch. Final concrete-channel sizeof accounting remains mandatory.
        const auto bytes=2*SchannelStream::RetainedApplicationBytes()+SchannelStream::MaxHandshakeTokenBytes+
            2*timox_broker::MaxMessageBytes+16*1024;
        Check(bytes<=256*1024,"two-stream application memory envelope exceeds 256 KiB");
        std::printf("Two-stream application envelope: %zu bytes (includes 16 KiB integration reserve)\n",bytes);
        const auto concreteBytes=WindowsNativeAudioChannelRetainedBytesForTest()+SchannelStream::MaxHandshakeTokenBytes+
            2*timox_broker::MaxMessageBytes+1024+8192;
        // 1 KiB bounds adapter metadata beyond its two message arrays; 8 KiB
        // bounds application stack scratch (EKU, framing, identities, names).
        Check(concreteBytes<=256*1024,"concrete channel memory exceeds envelope");
        std::printf("Concrete channel envelope: %zu bytes (adapter metadata + stack reserve included)\n",concreteBytes);
        TimeoutCompletionAccounting();
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
    if(argc==2 && std::strcmp(argv[1],"--completion-accounting-only")==0){std::puts("Ciphertext completion accounting passed (no credentials created)");return 0;}
    if(argc==1){std::puts("SKIP: real TLS requires explicitly authorized --temporary-user-key; no keys created");return 77;}
    if(argc!=2 || std::strcmp(argv[1],"--temporary-user-key")!=0){std::fprintf(stderr,"Usage: NativeAudioSchannelPipeTests [--temporary-user-key|--completion-accounting-only]\n");return 2;}
    temporaryUserKeys=true;int result=0;
    try{Identity server(L"CN=Timox pipe test server",szOID_PKIX_KP_SERVER_AUTH);
        Identity client(L"CN=Timox pipe test client",szOID_PKIX_KP_CLIENT_AUTH);
        Identity expired(L"CN=Timox pipe test expired",szOID_PKIX_KP_SERVER_AUTH,true);
        Tests(server,client,expired);}
    catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());result=1;}
    std::printf("Temporary user keys created=%u deleted=%u absenceVerified=%u\n",keysCreated,keysDeleted,keysAbsent);
    if(cleanupFailure || keysCreated!=keysDeleted || keysCreated!=keysAbsent)return 1;
    if(!result)std::puts("NativeAudioSchannelPipeTests passed (current-user pipes, not production isolation)");
    return result;
}
