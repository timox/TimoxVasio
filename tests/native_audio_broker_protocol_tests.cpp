#include "native_audio_broker_protocol.h"
#include <vector>
#include <iostream>
#include <stdexcept>
using namespace timox_broker;
void Check(bool ok,const char* why) { if(!ok)throw std::runtime_error(why); }
void Put(std::vector<unsigned char>& b,std::size_t at,std::uint64_t v,unsigned n) {
    for(unsigned i=0;i<n;++i)b[at+i]=static_cast<unsigned char>(v>>(8*i));
}
std::vector<unsigned char> Message(Opcode op,bool response=false,Status status=Status::Ok,
    std::size_t payload=0) {
    std::vector<unsigned char> b(48+payload);
    Put(b,0,0x42415654,4);Put(b,4,1,2);Put(b,6,static_cast<unsigned>(op)|(response?0x8000:0),2);
    Put(b,8,48,4);Put(b,12,payload,4);Put(b,16,10,8);Put(b,24,op==Opcode::Session&&!response?0:77,8);
    Put(b,32,static_cast<unsigned>(status),4);return b;
}
Context C(bool response=false,Opcode op=Opcode::Status) { return {response,op,10,9,77,123,4096}; }
bool Valid(const std::vector<unsigned char>& b,const Context& c) {return ValidateMessage(b.data(),b.size(),c).error==Error::None;}
void HeaderAndCorrelation() {
    auto b=Message(Opcode::Status);b.resize(56);Put(b,12,8,4);Put(b,48,123,8);
    Check(Valid(b,C()),"valid status request");
    for(std::size_t n=0;n<b.size();++n)Check(ValidateMessage(b.data(),n,C()).error!=Error::None,"truncation");
    for(auto offset:{0u,4u,6u,8u,36u,40u,47u}){auto bad=b;bad[offset]^=0x40;Check(!Valid(bad,C()),"header mutation");}
    auto bad=b;Put(bad,12,0xffffffff,4);Check(!Valid(bad,C()),"payload overflow");
    bad=b;bad.push_back(0);Check(!Valid(bad,C()),"trailing bytes");
    bad=b;Put(bad,16,9,8);Check(!Valid(bad,C()),"replayed id");
    bad=b;Put(bad,16,0,8);Check(!Valid(bad,C()),"zero id");
    bad=b;Put(bad,24,78,8);Check(!Valid(bad,C()),"wrong lease");
    bad=b;Put(bad,48,124,8);Check(!Valid(bad,C()),"wrong generation");
    auto c=C();c.lastRequestId=~std::uint64_t{0};Check(!Valid(b,c),"request exhaustion");
    bad=b;Put(bad,16,~std::uint64_t{0},8);Check(Valid(bad,C()),"MAX request accepted");
    Put(bad,16,0,8);Check(!Valid(bad,c),"wrap rejected");
    auto unaligned=b;unaligned.insert(unaligned.begin(),0xee);
    Check(ValidateMessage(unaligned.data()+1,b.size(),C()).error==Error::None,"unaligned header");
    const auto view=ValidateMessage(b.data(),b.size(),C());
    Check(view.message.payload==b.data()+48 && view.message.payloadBytes==8 &&
        view.message.leaseId==77 && view.message.requestId==10,"borrowed decoded metadata");
    Check(ValidateMessage(nullptr,b.size(),C()).error==Error::Truncated,"null input");
    auto reply=Message(Opcode::Status,true,Status::Ok,24);
    Check(Valid(reply,C(true)),"status response");Put(reply,16,11,8);Check(!Valid(reply,C(true)),"response correlation");
}
void PayloadContracts() {
    Check(Valid(Message(Opcode::Session),C(false,Opcode::Session)),"session request");
    auto b=Message(Opcode::Session,true,Status::Ok,8);Put(b,48,123,8);
    Check(Valid(b,C(true,Opcode::Session)),"session response");Put(b,48,0,8);Check(!Valid(b,C(true,Opcode::Session)),"zero generation");
    b=Message(Opcode::Session,true,Status::Unavailable,4);Put(b,24,0,8);
    Check(Valid(b,C(true,Opcode::Session)),"session error without lease");Put(b,24,77,8);
    Check(!Valid(b,C(true,Opcode::Session)),"session error cannot allocate lease");
    b=Message(Opcode::Status,true,Status::Removed,4);Check(Valid(b,C(true)),"removed echoes lease");
    Put(b,24,0,8);Check(!Valid(b,C(true)),"removed cannot drop correlation lease");
    b=Message(Opcode::ReadRender,false,Status::Ok,16);Put(b,48,123,8);Put(b,56,4096,4);
    Check(Valid(b,C(false,Opcode::ReadRender)),"read request");Put(b,56,4097,4);Check(!Valid(b,C(false,Opcode::ReadRender)),"read bound");
    Put(b,56,0,4);Check(!Valid(b,C(false,Opcode::ReadRender)),"zero frames");Put(b,56,1,4);b[60]=1;Check(!Valid(b,C(false,Opcode::ReadRender)),"read reserved");
    for(auto op:{Opcode::WriteCapture,Opcode::Close})Check(Valid(Message(op,true),C(true,op)),"empty success");
    b=Message(Opcode::Close,false,Status::Ok,8);Put(b,48,123,8);Check(Valid(b,C(false,Opcode::Close)),"close request");
    for(unsigned s=1;s<=static_cast<unsigned>(Status::Failed);++s){
        b=Message(Opcode::Status,true,static_cast<Status>(s),4);Put(b,48,0xffffffff,4);
        Check(Valid(b,C(true)),"bounded diagnostic");b.push_back(0);Check(!Valid(b,C(true)),"extra error data");
    }
    b=Message(Opcode::Status,false,Status::Failed,4);Check(!Valid(b,C()),"request status");
    b=Message(Opcode::Status,true,static_cast<Status>(999),4);Check(!Valid(b,C(true)),"unknown status");
}
void N0Packets() {
    for(bool render:{false,true}) {
        const auto op=render?Opcode::ReadRender:Opcode::WriteCapture;
        auto b=Message(op,render,Status::Ok,32832);
        Put(b,48,0x41575654,4);Put(b,52,64,4);Put(b,56,1,2);Put(b,60,render?1:2,2);Put(b,62,render?1:2,2);
        Put(b,64,123,8);Put(b,72,~std::uint64_t{0},8);Put(b,88,4096,4);Put(b,92,48000,4);Put(b,96,2,2);Put(b,98,1,2);Put(b,100,32768,4);
        Check(Valid(b,C(render,op)),"maximum N0 packet");
        auto bad=b;Put(bad,62,render?2:1,2);Check(!Valid(bad,C(render,op)),"N0 direction");
        bad=b;Put(bad,64,124,8);Check(!Valid(bad,C(render,op)),"N0 generation");
        bad=b;bad[108]=1;Check(!Valid(bad,C(render,op)),"N0 reserved");
        bad=b;bad.push_back(0);Put(bad,12,32833,4);Check(!Valid(bad,C(render,op)),"payload cap");
        auto c=C(render,op);c.maxFrames=4095;Check(!Valid(b,c),"requested capacity");
    }
}
int main(){try{HeaderAndCorrelation();PayloadContracts();N0Packets();std::cout<<"NativeAudioBrokerProtocolTests passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
