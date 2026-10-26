#include "socket_discovery.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace timox_broker;
void Check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
SocketDiscovery Fixture() {
    SocketDiscovery d; d.epoch = 0x0807060504030201ULL;
    for (unsigned i = 0; i < 16; ++i) d.socketId[i] = static_cast<std::uint8_t>(i);
    for (unsigned i = 0; i < 32; ++i) d.serverPin[i] = static_cast<std::uint8_t>(0x80 + i);
    return d;
}
void Codec() {
    const auto d = Fixture(); SocketDiscoveryBuffer encoded{};
    Check(EncodeSocketDiscovery(d, encoded), "encode valid discovery");
    SocketDiscoveryBuffer golden{};
    golden[0]='T'; golden[1]='N'; golden[2]='S'; golden[3]='D'; golden[4]=1; golden[8]=72;
    for (unsigned i=0;i<8;++i) golden[16+i]=static_cast<std::uint8_t>(i+1);
    for (unsigned i=0;i<16;++i) golden[24+i]=static_cast<std::uint8_t>(i);
    for (unsigned i=0;i<32;++i) golden[40+i]=static_cast<std::uint8_t>(0x80+i);
    Check(encoded==golden, "canonical portable wire layout");
    SocketDiscovery result{};
    Check(DecodeSocketDiscovery(golden.data(),golden.size(),result), "decode golden");
    Check(result.epoch==d.epoch && result.socketId==d.socketId && result.serverPin==d.serverPin, "exact fields");
    auto reject=[&](const void* bytes,std::size_t size) {
        auto output=d;
        Check(!DecodeSocketDiscovery(bytes,size,output), "accepted malformed descriptor");
        Check(output.epoch==d.epoch && output.socketId==d.socketId && output.serverPin==d.serverPin, "failure changed output");
    };
    reject(nullptr,golden.size());
    for (std::size_t size=0;size<golden.size();++size) reject(golden.data(),size);
    std::array<std::uint8_t,73> trailing{}; std::copy(golden.begin(),golden.end(),trailing.begin());
    reject(trailing.data(),trailing.size());
    // Every header byte is fixed, including flags/reserved/version/length.
    for (unsigned i=0;i<16;++i) { auto bad=golden; bad[i]^=0x80; reject(bad.data(),bad.size()); }
    for (unsigned field=0;field<3;++field) {
        auto invalid=d; auto bad=golden;
        if(field==0) { invalid.epoch=0; std::fill(bad.begin()+16,bad.begin()+24,std::uint8_t{0}); }
        if(field==1) { invalid.socketId.fill(0); std::fill(bad.begin()+24,bad.begin()+40,std::uint8_t{0}); }
        if(field==2) { invalid.serverPin.fill(0); std::fill(bad.begin()+40,bad.end(),std::uint8_t{0}); }
        reject(bad.data(),bad.size());
        auto sentinel=golden;
        Check(!EncodeSocketDiscovery(invalid,sentinel) && sentinel==golden, "invalid encode changed output");
    }
}
void Names() {
    auto d=Fixture(); SocketBasename name{};
    Check(SocketDiscoveryBasename(d,name), "basename rejected valid descriptor");
    Check(std::string(name.data())=="000102030405060708090a0b0c0d0e0f.sock", "basename not canonical");
    d.socketId.fill(0xff);
    Check(SocketDiscoveryBasename(d,name) && std::string(name.data())==std::string(32,'f')+".sock", "high bytes corrupt basename");
    auto original=name; d.socketId.fill(0);
    Check(!SocketDiscoveryBasename(d,name) && name==original, "invalid basename changed output");
}
}
int main() {
    try { Codec(); Names(); std::cout<<"socket discovery codec: PASS\n"; return 0; }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
