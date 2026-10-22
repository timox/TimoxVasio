// Test only: no socket, service or certificate store; named user keys are opt-in.
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
constexpr std::size_t Budget=256*1024;
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
bool Matches(PCCERT_CONTEXT cert,const Pin& pin,const char* eku){
    if(!cert || Hash(cert)!=pin || CertVerifyTimeValidity(nullptr,cert->pCertInfo)!=0)return false;
    DWORD size=0;
    if(!CertGetEnhancedKeyUsage(cert,CERT_FIND_EXT_ONLY_ENHKEY_USAGE_FLAG,nullptr,&size) || size>4096)return false;
    Bytes storage(size);auto* usage=reinterpret_cast<PCERT_ENHKEY_USAGE>(storage.data());
    if(!CertGetEnhancedKeyUsage(cert,CERT_FIND_EXT_ONLY_ENHKEY_USAGE_FLAG,usage,&size))return false;
    for(DWORD i=0;i<usage->cUsageIdentifier;++i)if(std::strcmp(usage->rgpszUsageIdentifier[i],eku)==0)return true;
    return false;
}
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
struct Step {SECURITY_STATUS status=SEC_E_OK;std::size_t consumed=0;Bytes token;};
class Peer {
    bool server_,authorized_=false,complete_=false;
    CredHandle credential_{};CtxtHandle context_{};Pin pin_;unsigned long attributes_=0;
public:
    Peer(bool server,Identity* own,const Pin& pin):server_(server),pin_(pin){
        SecInvalidateHandle(&credential_);SecInvalidateHandle(&context_);
        TLS_PARAMETERS tls{};tls.grbitDisabledProtocols=SP_PROT_SSL2|SP_PROT_SSL3|SP_PROT_TLS1|SP_PROT_TLS1_1|SP_PROT_TLS1_3;
        SCH_CREDENTIALS credentials{};credentials.dwVersion=SCH_CREDENTIALS_VERSION;
        credentials.cCreds=own?1:0;credentials.paCred=own?&own->cert:nullptr;
        credentials.dwFlags=SCH_CRED_NO_DEFAULT_CREDS|SCH_CRED_MANUAL_CRED_VALIDATION|SCH_CRED_NO_SYSTEM_MAPPER;
        credentials.cTlsParameters=1;credentials.pTlsParameters=&tls;TimeStamp expiry{};
        Status(AcquireCredentialsHandleW(nullptr,const_cast<wchar_t*>(UNISP_NAME_W),server?SECPKG_CRED_INBOUND:SECPKG_CRED_OUTBOUND,
            nullptr,&credentials,nullptr,nullptr,&credential_,&expiry),server?"server Schannel credentials":"client Schannel credentials");
    }
    ~Peer(){if(SecIsValidHandle(&context_))DeleteSecurityContext(&context_);if(SecIsValidHandle(&credential_))FreeCredentialsHandle(&credential_);}
    Peer(const Peer&)=delete;
    bool Ready() const noexcept{return complete_ && authorized_;}
    Step Handshake(Bytes& input){
        Check(!complete_ && input.size()<=Budget,"handshake state/budget");
        SecBuffer in[2]={{static_cast<unsigned long>(input.size()),SECBUFFER_TOKEN,input.data()},{0,SECBUFFER_EMPTY,nullptr}};
        SecBufferDesc inDesc{SECBUFFER_VERSION,2,in};
        SecBuffer outputs[2]={{0,SECBUFFER_TOKEN,nullptr},{0,SECBUFFER_ALERT,nullptr}};
        auto& out=outputs[0];SecBufferDesc outDesc{SECBUFFER_VERSION,2,outputs};
        TimeStamp expiry{};Step r;const bool exists=SecIsValidHandle(&context_)!=FALSE;
        if(server_)r.status=AcceptSecurityContext(&credential_,exists?&context_:nullptr,&inDesc,
            ASC_REQ_STREAM|ASC_REQ_CONFIDENTIALITY|ASC_REQ_INTEGRITY|ASC_REQ_MUTUAL_AUTH|ASC_REQ_ALLOCATE_MEMORY,
            0,&context_,&outDesc,&attributes_,&expiry);
        else r.status=InitializeSecurityContextW(&credential_,exists?&context_:nullptr,const_cast<wchar_t*>(L"probe.timox.invalid"),
            ISC_REQ_STREAM|ISC_REQ_CONFIDENTIALITY|ISC_REQ_INTEGRITY|ISC_REQ_ALLOCATE_MEMORY|
            ISC_REQ_MANUAL_CRED_VALIDATION|ISC_REQ_USE_SUPPLIED_CREDS,0,0,
            input.empty()?nullptr:&inDesc,0,&context_,&outDesc,&attributes_,&expiry);
        if(outputs[1].pvBuffer)FreeContextBuffer(outputs[1].pvBuffer);
        if(out.pvBuffer){if(out.cbBuffer>Budget){FreeContextBuffer(out.pvBuffer);throw std::runtime_error("output budget");}
            try{const auto* p=static_cast<const unsigned char*>(out.pvBuffer);r.token.assign(p,p+out.cbBuffer);}catch(...){FreeContextBuffer(out.pvBuffer);throw;}
            FreeContextBuffer(out.pvBuffer);}
        if(r.status==SEC_E_INCOMPLETE_MESSAGE)return r;
        r.consumed=input.size();if(in[1].BufferType==SECBUFFER_EXTRA){Check(in[1].cbBuffer<=input.size(),"extra bound");r.consumed-=in[1].cbBuffer;}
        if(r.status==SEC_E_OK){complete_=true;authorized_=Verify();}return r;
    }
    bool Verify(){
        std::printf("%s final attributes=0x%08lx\n",server_?"server":"client",attributes_);
        // Schannel does not necessarily return RET_INTEGRITY for TLS records.
        // Require encryption and the AEAD suite below; both peers must present a pinned certificate.
        if(!(attributes_&(server_?ASC_RET_CONFIDENTIALITY:ISC_RET_CONFIDENTIALITY)))return false;
        PCCERT_CONTEXT remote=nullptr;const auto remoteStatus=QueryContextAttributesW(&context_,SECPKG_ATTR_REMOTE_CERT_CONTEXT,&remote);
        if(remoteStatus!=SEC_E_OK){std::printf("remote certificate status=0x%08lx\n",static_cast<unsigned long>(remoteStatus));return false;}
        bool matches=false;try{matches=Matches(remote,pin_,server_?szOID_PKIX_KP_CLIENT_AUTH:szOID_PKIX_KP_SERVER_AUTH);}
        catch(...){CertFreeCertificateContext(remote);throw;}CertFreeCertificateContext(remote);if(!matches)return false;
        SecPkgContext_CipherInfo cipher{};cipher.dwVersion=SECPKGCONTEXT_CIPHERINFO_V1;
        Status(QueryContextAttributesW(&context_,SECPKG_ATTR_CIPHER_INFO,&cipher),"cipher query");
        std::printf("negotiated protocol=0x%08lx cipher=0x%04lx\n",cipher.dwProtocol,cipher.dwCipherSuite);
        SecPkgContext_ConnectionInfo connection{};
        Status(QueryContextAttributesW(&context_,SECPKG_ATTR_CONNECTION_INFO,&connection),"connection query");
        return (connection.dwProtocol&(SP_PROT_TLS1_2_CLIENT|SP_PROT_TLS1_2_SERVER))!=0 &&
            (cipher.dwCipherSuite==0xc02b || cipher.dwCipherSuite==0xc02c); // ECDHE_ECDSA AES128/256 GCM
    }
    Bytes Encrypt(const Bytes& plain){
        Check(Ready(),"no data before authentication");SecPkgContext_StreamSizes sizes{};
        Status(QueryContextAttributesW(&context_,SECPKG_ATTR_STREAM_SIZES,&sizes),"stream sizes");
        Check(!plain.empty() && plain.size()<=sizes.cbMaximumMessage &&
            static_cast<std::size_t>(sizes.cbHeader)+plain.size()+sizes.cbTrailer<=Budget,"record bound");
        Bytes result(sizes.cbHeader+plain.size()+sizes.cbTrailer);std::memcpy(result.data()+sizes.cbHeader,plain.data(),plain.size());
        SecBuffer b[4]={{sizes.cbHeader,SECBUFFER_STREAM_HEADER,result.data()},
            {static_cast<unsigned long>(plain.size()),SECBUFFER_DATA,result.data()+sizes.cbHeader},
            {sizes.cbTrailer,SECBUFFER_STREAM_TRAILER,result.data()+sizes.cbHeader+plain.size()},{0,SECBUFFER_EMPTY,nullptr}};
        SecBufferDesc desc{SECBUFFER_VERSION,4,b};Status(EncryptMessage(&context_,0,&desc,0),"encrypt");
        result.resize(static_cast<std::size_t>(b[0].cbBuffer)+b[1].cbBuffer+b[2].cbBuffer);return result;
    }
    SECURITY_STATUS Decrypt(Bytes& encrypted,Bytes& plain,std::size_t& consumed){
        plain.clear();consumed=0;Check(Ready() && encrypted.size()<=Budget,"decrypt state/budget");
        SecBuffer b[4]={{static_cast<unsigned long>(encrypted.size()),SECBUFFER_DATA,encrypted.data()},
            {0,SECBUFFER_EMPTY,nullptr},{0,SECBUFFER_EMPTY,nullptr},{0,SECBUFFER_EMPTY,nullptr}};
        SecBufferDesc desc{SECBUFFER_VERSION,4,b};unsigned long qop=0;const auto status=DecryptMessage(&context_,&desc,0,&qop);
        if(status==SEC_E_INCOMPLETE_MESSAGE)return status;
        if(status!=SEC_E_OK){authorized_=false;return status;}
        Check(qop!=SECQOP_WRAP_NO_ENCRYPT,"cleartext record refused");consumed=encrypted.size();
        for(const auto& buffer:b){if(buffer.BufferType==SECBUFFER_EXTRA){Check(buffer.cbBuffer<=encrypted.size(),"extra record bound");consumed-=buffer.cbBuffer;}
            if(buffer.BufferType==SECBUFFER_DATA){const auto* p=static_cast<const unsigned char*>(buffer.pvBuffer);Check(buffer.cbBuffer<=Budget,"plain bound");plain.assign(p,p+buffer.cbBuffer);}}
        return status;
    }
};
bool Connect(Peer& client,Peer& server){
    Bytes toClient,toServer,empty;auto first=client.Handshake(empty);if(first.status!=SEC_I_CONTINUE_NEEDED){std::printf("initial handshake status 0x%08lx\n",static_cast<unsigned long>(first.status));return false;}toServer=std::move(first.token);
    for(unsigned step=0;step<32;++step){for(unsigned side=0;side<2;++side){
        auto& input=side?toClient:toServer;auto& output=side?toServer:toClient;auto& peer=side?client:server;
        if(input.empty() || peer.Ready())continue;auto result=peer.Handshake(input);
        if(result.status!=SEC_E_OK && result.status!=SEC_I_CONTINUE_NEEDED){std::printf("handshake status 0x%08lx\n",static_cast<unsigned long>(result.status));return false;}
        input.erase(input.begin(),input.begin()+result.consumed);Check(output.size()+result.token.size()<=Budget,"wire bound");
        output.insert(output.end(),result.token.begin(),result.token.end());if(result.status==SEC_E_OK && !peer.Ready())return false;}
        if(client.Ready() && server.Ready())return true;}return false;
}
void RoundTrip(Peer& sender,Peer& receiver,const Bytes& expected){
    auto encrypted=sender.Encrypt(expected);Check(encrypted!=expected,"ciphertext differs");
    auto shortRecord=Bytes(encrypted.begin(),encrypted.end()-1);Bytes plain;std::size_t consumed=0;
    Check(receiver.Decrypt(shortRecord,plain,consumed)==SEC_E_INCOMPLETE_MESSAGE && plain.empty() && !consumed,"truncated record no plaintext");
    Check(receiver.Decrypt(encrypted,plain,consumed)==SEC_E_OK && plain==expected && consumed==encrypted.size(),"duplex round trip");
}
void RunTests(){
    Identity serverId(L"CN=Timox memory server",szOID_PKIX_KP_SERVER_AUTH),clientId(L"CN=Timox memory client",szOID_PKIX_KP_CLIENT_AUTH);
    const auto serverPin=Hash(serverId.cert),clientPin=Hash(clientId.cert);
    Identity expired(L"CN=Expired test",szOID_PKIX_KP_CLIENT_AUTH,true);
    Check(!Matches(expired.cert,Hash(expired.cert),szOID_PKIX_KP_CLIENT_AUTH),"expired pinned certificate refused");
    Check(!Matches(clientId.cert,clientPin,szOID_PKIX_KP_SERVER_AUTH),"wrong EKU refused");
    Check(!Matches(serverId.cert,serverPin,szOID_PKIX_KP_CLIENT_AUTH),"server cannot be client");
    std::puts("PASS: CNG certificate/key association and pin/validity/EKU predicates (not a TLS handshake)");
    {Peer client(false,&clientId,serverPin),server(true,&serverId,clientPin);
        Check(Connect(client,server),"mutually authenticated TLS1.2 AES-GCM handshake");
        Bytes pcm(4096);for(std::size_t i=0;i<pcm.size();++i)pcm[i]=static_cast<unsigned char>(i*37);
        RoundTrip(client,server,pcm);RoundTrip(server,client,pcm);
        auto first=client.Encrypt(Bytes{1,2,3}),second=client.Encrypt(Bytes{4,5});const auto firstBytes=first.size();first.insert(first.end(),second.begin(),second.end());
        Bytes plain;std::size_t consumed=0;Check(server.Decrypt(first,plain,consumed)==SEC_E_OK && plain==Bytes({1,2,3}) && consumed==firstBytes,"coalesced first");
        first.erase(first.begin(),first.begin()+consumed);Check(server.Decrypt(first,plain,consumed)==SEC_E_OK && plain==Bytes({4,5}),"extra record preserved");
        auto tampered=client.Encrypt(pcm);tampered[tampered.size()/2]^=1;
        Check(server.Decrypt(tampered,plain,consumed)!=SEC_E_OK && plain.empty() && !server.Ready(),"tamper retires without PCM");}
    for(bool wrongServer:{false,true}){auto wrong=wrongServer?serverPin:clientPin;wrong[0]^=1;
        Peer client(false,&clientId,wrongServer?wrong:serverPin),server(true,&serverId,wrongServer?clientPin:wrong);
        Check(!Connect(client,server),"unlisted certificate refused");}
    {Peer client(false,nullptr,serverPin),server(true,&serverId,clientPin);Check(!Connect(client,server),"client certificate required");}
    std::puts("PASS: mutual TLS1.2 AES-GCM; duplex, truncation, coalescing, tamper, pins, client certificate; validity/EKU predicates");
}
}
int main(int argc,char** argv){
    if(argc==2 && std::strcmp(argv[1],"--temporary-user-key")==0)temporaryUserKeys=true;
    else if(argc!=1){std::fputs("Usage: NativeAudioSchannelProbe [--temporary-user-key]\n",stderr);return 1;}
    int result=0;
    try{RunTests();}
    catch(const Failure& e){
        const bool unavailable=!temporaryUserKeys && e.code==SEC_E_UNKNOWN_CREDENTIALS &&
            (std::strcmp(e.stage,"client Schannel credentials")==0 || std::strcmp(e.stage,"server Schannel credentials")==0);
        std::fprintf(stderr,"%s: %s\n",unavailable?"SKIP - TLS non qualifie avec cle ephemere":"FAIL",e.what());result=unavailable?77:1;
    }
    catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());result=1;}
    std::printf("Temporary user keys: created=%u deleted=%u absenceVerified=%u\n",keysCreated,keysDeleted,keysAbsent);
    if(cleanupFailure || keysCreated!=keysDeleted || keysCreated!=keysAbsent)return 1;
    return result;
}
