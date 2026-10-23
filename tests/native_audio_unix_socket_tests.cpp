#include "unix_socket_stream_win32.h"
#include <Windows.h>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
using namespace timox_broker;
using Code=NativeChannelCode;
using Clock=std::chrono::steady_clock;
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
auto Deadline(){return Clock::now()+std::chrono::seconds(2);}
struct Path {
    std::string value=std::filesystem::absolute("unix-probe-"+std::to_string(GetCurrentProcessId())+".sock").string();
    Path(){Check(!std::filesystem::exists(value),"probe path exists");}
    ~Path(){std::error_code error;std::filesystem::remove(value,error);}
};
int main(){try{
    Path path;UnixSocketStream listener,client,server;
    Check(listener.Listen(path.value.c_str()).code==Code::Ok,"AF_UNIX listen");
    UnixSocketStream duplicate;Check(duplicate.Listen(path.value.c_str()).code!=Code::Ok,"socket path taken over");
    Check(listener.Accept(server,Clock::now()+std::chrono::milliseconds(20)).code==Code::TimedOut,"empty accept deadline");
    Check(client.Connect(path.value.c_str(),Deadline()).code==Code::Ok,"AF_UNIX connect");
    Check(listener.Accept(server,Deadline()).code==Code::Ok,"AF_UNIX accept");
    std::array<unsigned char,32768> sent{},received{};
    for(std::size_t i=0;i<sent.size();++i)sent[i]=static_cast<unsigned char>(i*17);
    Check(client.WriteAll(sent.data(),sent.size(),Deadline()).bytes==sent.size(),"socket write");
    std::size_t total=0;
    while(total<received.size()){
        auto r=server.ReadSome(received.data()+total,received.size()-total,Deadline());
        Check(r.code==Code::Ok&&r.bytes>0,"socket read");total+=r.bytes;
    }
    Check(received==sent,"socket bytes changed");
    Check(server.WriteAll(sent.data(),3,Deadline()).bytes==3,"reverse write");
    total=0;
    while(total<3){
        const auto r=client.ReadSome(received.data()+total,3-total,Deadline());
        Check(r.code==Code::Ok&&r.bytes>0,"reverse read");total+=r.bytes;
    }
    Check(std::equal(sent.begin(),sent.begin()+3,received.begin()),"reverse bytes changed");
    Check(server.ReadSome(received.data(),1,Clock::now()+std::chrono::milliseconds(20)).code==Code::TimedOut,"empty read deadline");
    // Exercises concurrent cancellation, including cancellation before entry;
    // does not claim deterministic interruption inside select.
    NativeChannelResult cancelled;std::thread worker([&]{cancelled=server.ReadSome(received.data(),1,Deadline());});
    server.RequestCancel();worker.join();
    Check(cancelled.code==Code::Cancelled,"concurrent cancellation");
    server.Close();Check(server.Connect(path.value.c_str(),Deadline()).code==Code::Cancelled,"cancellation reset");
    Check(client.ReadSome(received.data(),1,Deadline()).code==Code::Removed,"peer EOF");
    UnixSocketStream invalid;
    const std::string longPath(108,'x');Check(invalid.Connect(longPath.c_str(),Deadline()).code==Code::Failed,"truncated socket path");
    UnixSocketStream sender,blocked;
    Check(sender.Connect(path.value.c_str(),Deadline()).code==Code::Ok&&listener.Accept(blocked,Deadline()).code==Code::Ok,"backpressure connect");
    std::array<unsigned char,UnixSocketStream::MaxTransferBytes> bulk{};
    bool timedOut=false;
    for(unsigned n=0;n<64&&!timedOut;++n){
        const auto r=sender.WriteAll(bulk.data(),bulk.size(),Clock::now()+std::chrono::milliseconds(20));
        Check(r.bytes<=bulk.size(),"invalid sent prefix");
        Check(r.code==Code::Ok||r.code==Code::TimedOut,"backpressure unexpected error");
        timedOut=r.code==Code::TimedOut;
    }
    Check(timedOut,"unread receiver never applied backpressure");
    listener.RequestCancel();UnixSocketStream unused;
    Check(listener.Accept(unused,Deadline()).code==Code::Cancelled,"cancel accept");
    std::cout<<"AF_UNIX stream tests passed: duplex, deadlines, cancellation, EOF, collision, path bound\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
