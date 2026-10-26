#include "channel_binding.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace timox_broker;
namespace {
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
ChannelIdentity Identity(){
    ChannelIdentity i;
    // S-1-5-21-100-200-300-1001 and S-1-5-5-12-34.
    const std::uint8_t user[]={1,5,0,0,0,0,0,5,21,0,0,0,100,0,0,0,200,0,0,0,44,1,0,0,233,3,0,0};
    const std::uint8_t logon[]={1,3,0,0,0,0,0,5,5,0,0,0,12,0,0,0,34,0,0,0};
    std::copy(std::begin(user),std::end(user),i.userSid.begin());i.userSidBytes=sizeof(user);
    std::copy(std::begin(logon),std::end(logon),i.logonSid.begin());i.logonSidBytes=sizeof(logon);
    i.authenticationId=0x123456789abcdef0ull;i.sessionId=2;return i;
}
ChannelBinding Message(BindingType type){
    ChannelBinding b;b.type=type;
    if(type!=BindingType::DataOffer)b.identity=Identity();
    if(type==BindingType::DataOffer||type==BindingType::DataBindRequest||type==BindingType::DataBindReply){
        b.lease=0xfedcba9876543210ull;b.generation=0x123456789abcdef0ull;b.nonce[15]=42;
    }return b;
}
void RoundtripAndWire(){
    for(auto type:{BindingType::BindRequest,BindingType::BindReply,BindingType::DataOffer,
                  BindingType::DataBindRequest,BindingType::DataBindReply}){
        const auto input=Message(type);BindingBuffer wire{};std::size_t size=0;
        Check(EncodeBinding(input,wire,size),"valid binding encode failed");
        Check(wire[0]=='T'&&wire[1]=='N'&&wire[2]=='A'&&wire[3]=='C'&&wire[4]==1&&wire[5]==0,"header endian/magic wrong");
        Check(wire[6]==static_cast<unsigned>(type)&&wire[7]==0,"type endian wrong");
        Check(size==(type==BindingType::DataOffer?48u:112u),"unexpected wire length");
        Check(wire[8]==size-16&&wire[9]==0&&wire[10]==0&&wire[11]==0,"payload length wrong");
        if(type==BindingType::DataOffer)Check(wire[16]==0x10&&wire[23]==0xfe&&wire[47]==42,"offer payload endian wrong");
        else Check(wire[32]==0xf0&&wire[39]==0x12&&wire[40]==2&&wire[44]==28&&wire[46]==20,"identity payload layout wrong");
        ChannelBinding output;Check(DecodeBinding(wire.data(),size,output),"valid binding decode failed");
        Check(MatchesBinding(output,input),"roundtrip changed binding");
        // Every truncated prefix and every extra suffix must fail without output mutation.
        for(std::size_t n=0;n<size;++n){output=input;Check(!DecodeBinding(wire.data(),n,output),"truncated binding accepted");Check(MatchesBinding(output,input),"failed decode mutated output");}
        Check(!DecodeBinding(wire.data(),size+1,output),"trailing byte accepted");
    }
}
void RejectMalformed(){
    const auto valid=Message(BindingType::DataBindRequest);BindingBuffer wire{};std::size_t size=0;
    Check(EncodeBinding(valid,wire,size),"fixture encode failed");
    const auto reject=[&](std::size_t offset,std::uint8_t value){auto bad=wire;bad[offset]=value;ChannelBinding out;
        Check(!DecodeBinding(bad.data(),size,out),"malformed wire accepted");};
    reject(0,0);reject(4,2);reject(6,0);reject(6,6);reject(7,1);reject(8,0);reject(11,1);
    for(std::size_t n=12;n<16;++n)reject(n,1);
    reject(44,69);reject(44,27);reject(45,1);reject(46,0);reject(48,2);reject(49,16);
    reject(76,2);reject(77,2);reject(83,6);reject(84,6); // logon must be S-1-5-5-X-Y.
    ChannelBinding out;Check(!DecodeBinding(nullptr,size,out),"null pointer accepted");
    Check(!DecodeBinding(wire.data(),MaxBindingBytes+1,out),"oversize accepted");
    const auto invalid=[&](ChannelBinding b){BindingBuffer target{};target.fill(0x55);const auto before=target;std::size_t n=123;
        Check(!EncodeBinding(b,target,n),"invalid binding encoded");Check(n==123&&target==before,"failed encode mutated output");};
    auto b=valid;b.type=static_cast<BindingType>(6);invalid(b);
    b=valid;b.lease=0;invalid(b);b=valid;b.generation=0;invalid(b);b=valid;b.nonce.fill(0);invalid(b);
    b=valid;b.identity.authenticationId=0;invalid(b);b=valid;b.identity.sessionId=0;invalid(b);
    b=valid;b.identity.sessionId=0xffffffffu;invalid(b);b=valid;b.identity.userSidBytes=69;invalid(b);
    b=Message(BindingType::BindRequest);b.lease=1;invalid(b);
    b=Message(BindingType::BindReply);b.generation=1;invalid(b);
    b=Message(BindingType::BindReply);b.nonce[0]=1;invalid(b);
    b=Message(BindingType::DataOffer);b.identity=Identity();invalid(b);
}
void Matching(){
    const auto expected=Message(BindingType::DataBindReply);
    Check(MatchesBinding(expected,expected),"same binding rejected");
    auto b=expected;b.type=BindingType::DataBindRequest;Check(!MatchesBinding(b,expected),"wrong type accepted");
    b=expected;++b.lease;Check(!MatchesBinding(b,expected),"wrong lease accepted");
    b=expected;++b.generation;Check(!MatchesBinding(b,expected),"wrong generation accepted");
    b=expected;b.nonce[0]=1;Check(!MatchesBinding(b,expected),"wrong data pipe accepted");
    b=expected;b.identity.authenticationId+=(std::uint64_t{1}<<32);Check(!MatchesBinding(b,expected),"reused SessionId accepted");
    b=expected;++b.identity.sessionId;Check(!MatchesBinding(b,expected),"wrong session accepted");
    b=expected;++b.identity.userSid[24];Check(!MatchesBinding(b,expected),"wrong user accepted");
    b=expected;++b.identity.logonSid[16];Check(!MatchesBinding(b,expected),"wrong logon accepted");
    ChannelIdentity empty;Check(!SameChannelIdentity(empty,empty),"empty identity matches");
}
void MaximumSidAndCorruption(){
    auto b=Message(BindingType::DataBindRequest);
    b.identity.userSid[1]=15;b.identity.userSidBytes=68;
    for(std::size_t i=28;i<68;++i)b.identity.userSid[i]=static_cast<std::uint8_t>(i);
    BindingBuffer wire{};std::size_t size=0;
    Check(EncodeBinding(b,wire,size)&&size==152,"maximum SID did not fit");
    ChannelBinding out;Check(DecodeBinding(wire.data(),size,out)&&MatchesBinding(out,b),"maximum SID changed");
    // Every single-byte corruption either is rejected as malformed or cannot
    // match the authenticated expected binding. Reserved/header bytes included.
    for(std::size_t n=0;n<size;++n){auto altered=wire;altered[n]^=0x80;
        Check(!DecodeBinding(altered.data(),size,out)||!MatchesBinding(out,b),"corrupted binding still matched");}
    // Fixed caller storage may contain data after the declared SID; it is never
    // serialized and cannot change equality of otherwise identical identities.
    b=Message(BindingType::BindReply);auto padded=b;padded.identity.userSid[67]=42;
    Check(EncodeBinding(b,wire,size),"base encode failed");BindingBuffer other{};std::size_t otherSize=0;
    Check(EncodeBinding(padded,other,otherSize)&&wire==other&&size==otherSize,"unused SID storage leaked to wire");
}
}
int main(){try{RoundtripAndWire();RejectMalformed();Matching();MaximumSidAndCorruption();std::cout<<"Channel binding codec tests passed (no OS identity or audio qualification)\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
