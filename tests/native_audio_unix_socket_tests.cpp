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
int PeerProbe(const char* path){
    UnixSocketStream stream;
    Check(stream.Connect(path,Deadline()).code==Code::Ok,"child connect");
    std::uint32_t parent=0;
    Check(stream.GetPeerProcessId(parent).code==Code::Ok&&parent!=GetCurrentProcessId(),"child peer PID");
    Check(stream.WriteAll(&parent,sizeof(parent),Deadline()).bytes==sizeof(parent),"child PID report");
    unsigned char ack=0;Check(stream.ReadSome(&ack,1,Deadline()).bytes==1&&ack==42,"child acknowledgement");return 0;
}
void SeparateProcess(UnixSocketStream& listener,const std::string& path){
    wchar_t executable[32768]{};const DWORD length=GetModuleFileNameW(nullptr,executable,32768);
    Check(length&&length<32768,"test executable path");
    std::wstring command=L"\""+std::wstring(executable)+L"\" --peer-probe \""+std::filesystem::path(path).wstring()+L"\"";
    struct Child {
        PROCESS_INFORMATION info{};
        ~Child(){
            if(info.hThread)CloseHandle(info.hThread);
            if(info.hProcess){
                if(WaitForSingleObject(info.hProcess,2500)==WAIT_TIMEOUT){TerminateProcess(info.hProcess,1);WaitForSingleObject(info.hProcess,2500);}
                CloseHandle(info.hProcess);
            }
        }
    } child;
    STARTUPINFOW startup{};startup.cb=sizeof(startup);
    Check(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child.info)!=FALSE,"launch child probe");
    UnixSocketStream peer;
    Check(listener.Accept(peer,Deadline()).code==Code::Ok,"child accept");
    std::uint32_t observed=0;
    Check(peer.GetPeerProcessId(observed).code==Code::Ok&&observed==child.info.dwProcessId&&observed!=GetCurrentProcessId(),"kernel peer PID differs from child");
    std::uint32_t reportedParent=0;std::size_t total=0;
    while(total<sizeof(reportedParent)){
        const auto r=peer.ReadSome(reinterpret_cast<unsigned char*>(&reportedParent)+total,sizeof(reportedParent)-total,Deadline());
        Check(r.code==Code::Ok&&r.bytes,"parent PID report");total+=r.bytes;
    }
    Check(reportedParent==GetCurrentProcessId(),"child observed wrong server PID");
    const unsigned char ack=42;Check(peer.WriteAll(&ack,1,Deadline()).bytes==1,"parent acknowledgement");
    Check(WaitForSingleObject(child.info.hProcess,2500)==WAIT_OBJECT_0,"child did not exit");
    DWORD code=1;Check(GetExitCodeProcess(child.info.hProcess,&code)&&code==0,"child probe failed");
}
int main(int argc,char** argv){try{
    if(argc==3&&std::string(argv[1])=="--peer-probe")return PeerProbe(argv[2]);
    Path path;UnixSocketStream listener,client,server;
    Check(listener.Listen(path.value.c_str()).code==Code::Ok,"AF_UNIX listen");
    UnixSocketStream duplicate;Check(duplicate.Listen(path.value.c_str()).code!=Code::Ok,"socket path taken over");
    Check(listener.Accept(server,Clock::now()+std::chrono::milliseconds(20)).code==Code::TimedOut,"empty accept deadline");
    Check(client.Connect(path.value.c_str(),Deadline()).code==Code::Ok,"AF_UNIX connect");
    Check(listener.Accept(server,Deadline()).code==Code::Ok,"AF_UNIX accept");
    std::uint32_t peerId=0;
    Check(server.GetPeerProcessId(peerId).code==Code::Ok&&peerId==GetCurrentProcessId(),"same-process peer PID");
    peerId=123;Check(listener.GetPeerProcessId(peerId).code!=Code::Ok&&peerId==123,"listener returned peer PID");
    SeparateProcess(listener,path.value);
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
    peerId=123;Check(server.GetPeerProcessId(peerId).code==Code::Cancelled&&peerId==123,"cancelled peer query changed output");
    server.Close();Check(server.Connect(path.value.c_str(),Deadline()).code==Code::Cancelled,"cancellation reset");
    Check(client.ReadSome(received.data(),1,Deadline()).code==Code::Removed,"peer EOF");
    UnixSocketStream invalid;
    Check(invalid.GetPeerProcessId(peerId).code!=Code::Ok&&peerId==123,"unused socket peer query changed output");
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
