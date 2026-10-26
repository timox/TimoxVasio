#include "pipe_identity_win32.h"
#include "pipe_stream_win32.h"
#include <bcrypt.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
using namespace timox_broker;
namespace {
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
auto Deadline(){return std::chrono::steady_clock::now()+std::chrono::seconds(3);}
void Test(){
    ChannelIdentity process;
    const auto result=ObserveProcessIdentity(process);
    if(result!=ERROR_SUCCESS)throw std::runtime_error("process identity observation failed: "+std::to_string(result));
    Check(SameChannelIdentity(process,process),"process identity incomplete");
    ChannelIdentity invalid=process;
    Check(ObservePipeIdentity(INVALID_HANDLE_VALUE,invalid)!=ERROR_SUCCESS,"invalid pipe accepted");
    Check(SameChannelIdentity(invalid,process),"failed observation mutated output");
    std::array<unsigned char,16> random{};
    Check(BCryptGenRandom(nullptr,random.data(),static_cast<ULONG>(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0,"random name failed");
    std::wstring name=L"\\\\.\\pipe\\Timox.NativeAudio.identity.test.";
    for(auto c:random){name+=L"0123456789abcdef"[c>>4];name+=L"0123456789abcdef"[c&15];}
    PipeStream server,client;
    Check(server.CreateServer(name.c_str(),nullptr).code==NativeChannelCode::Ok,"create test pipe failed");
    NativeChannelResult accepted;
    std::thread accept([&]{accepted=server.Accept(Deadline());});
    const auto connected=client.Connect(name.c_str(),Deadline());accept.join();
    Check(connected.code==NativeChannelCode::Ok&&accepted.code==NativeChannelCode::Ok,"connect failed");
    const unsigned char marker=42;unsigned char received=0;
    Check(client.WriteAll(&marker,1,Deadline()).code==NativeChannelCode::Ok,"write failed");
    Check(server.ReadSome(&received,1,Deadline()).code==NativeChannelCode::Ok&&received==marker,"read failed");
    ChannelIdentity observed;
    const auto pipeResult=ObservePipeIdentity(server.BorrowHandle(),observed);
    if(pipeResult!=ERROR_SUCCESS)throw std::runtime_error("pipe identity observation failed: "+std::to_string(pipeResult));
    Check(SameChannelIdentity(observed,process),"pipe identity differs from process token");
    HANDLE threadToken=nullptr;
    Check(!OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&threadToken)&&GetLastError()==ERROR_NO_TOKEN,"impersonation leaked");
    // A caller already impersonating must be rejected without replacing its token.
    Check(ImpersonateSelf(SecurityIdentification)!=FALSE,"test impersonation failed");
    ChannelIdentity unchanged=process;
    const auto nested=ObservePipeIdentity(server.BorrowHandle(),unchanged);
    const BOOL stillImpersonating=OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&threadToken);
    const BOOL reverted=RevertToSelf();
    if(threadToken)CloseHandle(threadToken);
    Check(reverted!=FALSE,"test revert failed");
    Check(nested!=ERROR_SUCCESS&&stillImpersonating&&SameChannelIdentity(unchanged,process),"nested observation changed caller identity");
}
}
int main(){try{Test();std::cout<<"Real pipe identity matches current process; impersonation restored (single logon only)\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
