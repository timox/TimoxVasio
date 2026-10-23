// Real local pipes; no service, driver, machine key or certificate store writes.
#include "schannel_stream_win32.h"
#include "channel_frames.h"
#include <atomic>
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
void Tests(Identity& serverIdentity,Identity& clientIdentity,Identity& expired){
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
