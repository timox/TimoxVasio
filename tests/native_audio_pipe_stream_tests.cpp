#include "pipe_stream_win32.h"
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using timox_broker::PipeStream;
using Code=NativeChannelCode;
using Clock=std::chrono::steady_clock;
using namespace std::chrono_literals;
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
auto Deadline(){return Clock::now()+2s;}
std::wstring Name(){
    std::array<unsigned char,16> nonce{};
    Check(BCryptGenRandom(nullptr,nonce.data(),static_cast<ULONG>(nonce.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0,"nonce creation failed");
    std::wstring name=L"\\\\.\\pipe\\Timox.NativeAudio.test.";
    for(auto c:nonce){name+=L"0123456789abcdef"[c>>4];name+=L"0123456789abcdef"[c&15];}
    return name;
}
struct Pair {
    PipeStream server,client;
    const std::wstring name=Name();
    Pair(){
        const auto creation=server.CreateServer(name.c_str(),nullptr);
        if(creation.code!=Code::Ok)throw std::runtime_error("real pipe server creation failed: channel="+
            std::to_string(static_cast<int>(creation.code))+" systemError="+std::to_string(creation.systemError));
        NativeChannelResult accepted;
        std::thread accept([&]{accepted=server.Accept(Deadline());});
        const auto connected=client.Connect(name.c_str(),Deadline());
        accept.join();
        Check(connected.code==Code::Ok && accepted.code==Code::Ok,"real pipe connection failed");
    }
};
void DuplexAndFragmentation(){
    Pair pair;
    std::vector<unsigned char> expected(65537),received(expected.size());
    for(std::size_t i=0;i<expected.size();++i)expected[i]=static_cast<unsigned char>(i*37u);
    NativeChannelResult write;
    std::thread sender([&]{write=pair.client.WriteAll(expected.data(),expected.size(),Deadline());});
    std::size_t offset=0;bool good=true;
    while(offset<received.size()){
        const auto count=(std::min)(std::size_t{31},received.size()-offset);
        const auto read=pair.server.ReadSome(received.data()+offset,count,Deadline());
        if(read.code!=Code::Ok || !read.bytes || read.bytes>count){good=false;break;}
        offset+=read.bytes;
    }
    if(!good)pair.client.RequestCancel();
    sender.join();
    Check(good && offset==expected.size() && write.code==Code::Ok && write.bytes==expected.size() && received==expected,"fragmented byte stream differs");
    const std::array<unsigned char,4> reply{{0,0xff,0x80,1}};
    Check(pair.server.WriteAll(reply.data(),reply.size(),Deadline()).code==Code::Ok,"reverse write failed");
    std::array<unsigned char,4> out{};
    const auto read=pair.client.ReadSome(out.data(),out.size(),Deadline());
    Check(read.code==Code::Ok && read.bytes==out.size() && out==reply,"reverse stream differs");
}
void TimeoutEofAndBounds(){
    Pair pair;std::array<unsigned char,8> bytes{};
    const auto start=Clock::now();
    const auto read=pair.client.ReadSome(bytes.data(),bytes.size(),start+40ms);
    Check(read.code==Code::TimedOut && Clock::now()-start<1s,"pending read did not time out and drain");
    const auto snapshot=bytes;std::this_thread::sleep_for(10ms);
    Check(bytes==snapshot,"timed out read retained caller buffer");
    // Timeout ends the operation, not the connection; cancellation is distinct.
    Check(pair.server.WriteAll(bytes.data(),1,Deadline()).code==Code::Ok,"post-timeout write failed");
    Check(pair.client.ReadSome(bytes.data(),1,Deadline()).code==Code::Ok,"post-timeout read failed");
    Check(pair.client.ReadSome(nullptr,1,Deadline()).code==Code::Failed,"null output accepted");
    Check(pair.client.WriteAll(bytes.data(),PipeStream::MaxTransferBytes+1,Deadline()).code==Code::Failed,"oversized write accepted");
    pair.server.Close();
    Check(pair.client.ReadSome(bytes.data(),bytes.size(),Deadline()).code==Code::Removed,"EOF not reported");
}
void CancelAndBackpressure(){
    Pair pair;std::array<unsigned char,8> bytes{};NativeChannelResult read;
    std::thread reader([&]{read=pair.client.ReadSome(bytes.data(),bytes.size(),Deadline());});
    std::this_thread::sleep_for(20ms);
    const auto start=Clock::now();pair.client.RequestCancel();reader.join();
    Check(read.code==Code::Cancelled && Clock::now()-start<1s,"cancel read did not drain");
    const auto snapshot=bytes;std::this_thread::sleep_for(10ms);
    Check(bytes==snapshot,"cancelled read retained caller buffer");
    pair.client.Close();
    Check(pair.client.Connect(Name().c_str(),Deadline()).code==Code::Cancelled,"close reset sticky cancellation");
    Pair blocked;std::vector<unsigned char> large(PipeStream::MaxTransferBytes,0x5a);NativeChannelResult write;
    std::thread writer([&]{write=blocked.client.WriteAll(large.data(),large.size(),Deadline());});
    std::this_thread::sleep_for(20ms);blocked.client.RequestCancel();writer.join();
    Check(write.code==Code::Cancelled && write.bytes<=large.size(),"backpressured write not cancelled");
}
void AcceptAndConnectCancellation(){
    PipeStream server;const auto name=Name();
    Check(server.CreateServer(name.c_str(),nullptr).code==Code::Ok,"accept fixture creation failed");
    NativeChannelResult accepted;
    std::thread waiter([&]{accepted=server.Accept(Deadline());});
    std::this_thread::sleep_for(20ms);server.RequestCancel();waiter.join();
    Check(accepted.code==Code::Cancelled,"pending accept not cancelled");
    PipeStream missing;missing.RequestCancel();
    Check(missing.Connect(Name().c_str(),Deadline()).code==Code::Cancelled,"pre-cancelled connect accepted");
    PipeStream remote;
    Check(remote.Connect(L"\\\\remote\\pipe\\test",Deadline()).code==Code::Failed,"nonlocal pipe accepted");
    Pair occupied;PipeStream contender;NativeChannelResult connection;
    std::thread connector([&]{connection=contender.Connect(occupied.name.c_str(),Deadline());});
    std::this_thread::sleep_for(20ms);contender.RequestCancel();connector.join();
    Check(connection.code==Code::Cancelled,"busy connect not cancelled");
    PipeStream timed;
    Check(timed.Connect(occupied.name.c_str(),Clock::now()+40ms).code==Code::TimedOut,"busy connect deadline ignored");
}
void CompletionCancellationRace(){
    for(unsigned iteration=0;iteration<24;++iteration){
        Pair pair;std::array<unsigned char,1> byte{{0x37}},out{};NativeChannelResult result;
        std::thread reader([&]{result=pair.client.ReadSome(out.data(),out.size(),Deadline());});
        const auto write=pair.server.WriteAll(byte.data(),byte.size(),Deadline());
        pair.client.RequestCancel();reader.join();
        Check(write.code==Code::Ok && (result.code==Code::Ok || result.code==Code::Cancelled),"completion/cancel race failed");
        if(result.code==Code::Ok)Check(result.bytes==1 && out==byte,"completed racing read differs");
        Check(pair.client.ReadSome(out.data(),out.size(),Deadline()).code==Code::Cancelled,"race lost sticky cancel");
    }
}
}
int main(){
    try{DuplexAndFragmentation();TimeoutEofAndBounds();CancelAndBackpressure();AcceptAndConnectCancellation();CompletionCancellationRace();std::cout<<"NativeAudioPipeStreamTests passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
