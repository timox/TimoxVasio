#include "channel_frames.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace timox_broker;
namespace {
using Code=NativeChannelCode;using Deadline=INativeAudioAuthenticatedChannel::Deadline;
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
auto Future(){return std::chrono::steady_clock::now()+std::chrono::seconds(2);}
struct Stream {
    std::vector<unsigned char> input,output;std::size_t offset=0,fragment=1,failAt=~std::size_t{0};
    bool closed=false;Code failure=Code::TimedOut;Deadline expected{};unsigned calls=0;
    bool zeroRead=false,oversizedRead=false,shortWrite=false;Code writeCode=Code::Ok;
    NativeChannelResult ReadSome(void* out,std::size_t capacity,Deadline deadline)noexcept{
        ++calls;if(expected!=Deadline{}&&deadline!=expected)return {Code::Failed};
        if(closed)return {Code::Removed};
        if(zeroRead)return {Code::Ok,0};
        if(oversizedRead)return {Code::Ok,static_cast<std::uint32_t>(capacity+1)};
        if(offset>=failAt)return {failure};
        if(offset==input.size())return {Code::Removed};
        const auto count=(std::min)({fragment,capacity,input.size()-offset,failAt-offset});
        std::memcpy(out,input.data()+offset,count);offset+=count;return {Code::Ok,static_cast<std::uint32_t>(count)};
    }
    NativeChannelResult WriteAll(const void* data,std::size_t size,Deadline deadline){
        ++calls;if(expected!=Deadline{}&&deadline!=expected)return {Code::Failed};
        const auto* p=static_cast<const unsigned char*>(data);output.assign(p,p+size);
        return {writeCode,static_cast<std::uint32_t>(shortWrite?size-1:size)};
    }
    void Close()noexcept{closed=true;}
};
std::vector<unsigned char> Audio(std::size_t payload){
    std::vector<unsigned char> b(HeaderBytes+payload);b[0]='T';b[1]='V';b[2]='A';b[3]='B';b[4]=1;b[6]=2;b[7]=0x80;b[8]=48;
    for(unsigned n=0;n<4;++n)b[12+n]=static_cast<unsigned char>(payload>>(8*n));
    for(std::size_t n=HeaderBytes;n<b.size();++n)b[n]=static_cast<unsigned char>(n*19);return b;
}
ChannelBinding Offer(){ChannelBinding b;b.type=BindingType::DataOffer;b.lease=11;b.generation=22;b.nonce[4]=42;return b;}
void FragmentedAndCoalesced(){
    for(auto fragment:{std::size_t{1},std::size_t{17},MaxMessageBytes*2}){
        Stream s;s.fragment=fragment;s.expected=Future();const auto first=Audio(MaxPayloadBytes),second=Audio(8);
        s.input=first;s.input.insert(s.input.end(),second.begin(),second.end());std::array<unsigned char,MaxMessageBytes> output{};
        const auto result=ReadAudioFrame(s,output.data(),output.size(),s.expected);
        Check(result.code==Code::Ok&&result.bytes==first.size()&&std::equal(first.begin(),first.end(),output.begin()),"fragmented audio frame differs");
        Check(s.offset==first.size()&&!s.closed,"reader consumed next message");
        const auto next=ReadAudioFrame(s,output.data(),output.size(),s.expected);
        Check(next.code==Code::Ok&&next.bytes==second.size()&&std::equal(second.begin(),second.end(),output.begin()),"coalesced second frame differs");
    }
    Stream s;BindingBuffer encoded{};std::size_t bytes=0;const auto offer=Offer();Check(EncodeBinding(offer,encoded,bytes),"offer fixture");
    s.input.assign(encoded.begin(),encoded.begin()+bytes);s.input.insert(s.input.end(),encoded.begin(),encoded.begin()+bytes);
    ChannelBinding out;
    Check(ReadBindingFrame(s,out,Future()).code==Code::Ok&&MatchesBinding(out,offer)&&s.offset==bytes,"fragmented binding differs");
    Check(ReadBindingFrame(s,out,Future()).code==Code::Ok&&MatchesBinding(out,offer),"next binding lost");
    Stream writer;const auto written=WriteBindingFrame(writer,offer,Future());
    Check(written.code==Code::Ok&&written.bytes==bytes&&std::equal(writer.output.begin(),writer.output.end(),encoded.begin()),"binding write differs");
}
void TerminalFailures(){
    for(auto at:{std::size_t{0},std::size_t{7},HeaderBytes,HeaderBytes+3})for(auto code:{Code::TimedOut,Code::Cancelled,Code::Removed}){
        Stream s;s.input=Audio(8);s.failAt=at;s.failure=code;std::array<unsigned char,MaxMessageBytes> output;output.fill(42);
        const auto result=ReadAudioFrame(s,output.data(),output.size(),Future());
        Check(result.code==code&&result.bytes==0&&s.closed,"failed partial frame not terminal");
        Check(std::all_of(output.begin(),output.end(),[](auto byte){return byte==0;}),"partial audio escaped on failure");
    }
    Stream s;s.input=Audio(8);std::array<unsigned char,MaxMessageBytes> output{};
    Check(ReadAudioFrame(s,output.data(),output.size(),std::chrono::steady_clock::now()).code==Code::TimedOut&&s.calls==0&&s.closed,"expired deadline touched stream");
    Stream bad;bad.input=Audio(8);bad.input[12]=0xff;bad.input[15]=0xff;
    Check(ReadAudioFrame(bad,output.data(),output.size(),Future()).code==Code::Failed&&bad.offset==HeaderBytes&&bad.closed,"oversize payload read");
    Stream small;small.input=Audio(8);
    Check(ReadAudioFrame(small,output.data(),HeaderBytes,Future()).code==Code::Failed&&small.offset==HeaderBytes&&small.closed,"small buffer overrun");
    Stream unknown;unknown.input=Audio(0);unknown.input[0]=0;
    Check(ReadAudioFrame(unknown,output.data(),output.size(),Future()).code==Code::Failed&&unknown.closed,"wrong frame magic accepted");
    BindingBuffer encoded{};std::size_t bytes=0;Check(EncodeBinding(Offer(),encoded,bytes),"offer fixture");
    Stream binding;binding.input.assign(encoded.begin(),encoded.begin()+bytes);binding.failAt=20;auto untouched=Offer();
    Check(ReadBindingFrame(binding,untouched,Future()).code==Code::TimedOut&&MatchesBinding(untouched,Offer())&&binding.closed,"failed binding mutated output");
    Stream wrongDomain;wrongDomain.input=Audio(0);
    Check(ReadBindingFrame(wrongDomain,untouched,Future()).code==Code::Failed&&wrongDomain.offset==16&&wrongDomain.closed,"audio accepted as binding");
    Stream invalidWrite;auto invalid=Offer();invalid.nonce.fill(0);
    Check(WriteBindingFrame(invalidWrite,invalid,Future()).code==Code::Failed&&invalidWrite.calls==0&&invalidWrite.closed,"invalid binding written");
    for(auto code:{Code::Ok,Code::TimedOut,Code::Cancelled,Code::Removed}){
        Stream partial;partial.shortWrite=true;partial.writeCode=code;
        const auto result=WriteBindingFrame(partial,Offer(),Future());
        Check(result.code==(code==Code::Ok?Code::Failed:code)&&result.bytes==0&&partial.closed,"partial write reported complete");
    }
    for(bool oversized:{false,true}){
        Stream broken;broken.zeroRead=!oversized;broken.oversizedRead=oversized;
        Check(ReadAudioFrame(broken,output.data(),output.size(),Future()).code==Code::Failed&&broken.closed,"invalid read completion accepted");
    }
}
}
int main(){try{FragmentedAndCoalesced();TerminalFailures();std::cout<<"Bounded channel frame tests passed\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
