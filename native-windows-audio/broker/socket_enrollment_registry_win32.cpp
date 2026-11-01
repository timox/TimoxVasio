#include "socket_enrollment_registry_win32.h"
#include <Windows.h>
#include <sddl.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <memory>
#include <string>

namespace timox_broker {
namespace {
constexpr wchar_t RuntimeKey[]=L"SOFTWARE\\Timox\\NativeAudioBroker\\Runtime";

class RegistryKey final {
public:
    explicit RegistryKey(HKEY value=nullptr) noexcept:value_(value){}
    ~RegistryKey(){if(value_)RegCloseKey(value_);}
    RegistryKey(const RegistryKey&)=delete;
    RegistryKey& operator=(const RegistryKey&)=delete;
    HKEY get()const noexcept{return value_;}
private:HKEY value_;
};
class LocalString final {
public:
    explicit LocalString(wchar_t* value=nullptr)noexcept:value_(value){}
    ~LocalString(){if(value_)LocalFree(value_);}
    LocalString(const LocalString&)=delete;
    LocalString& operator=(const LocalString&)=delete;
    const wchar_t* get()const noexcept{return value_;}
private:wchar_t* value_;
};

bool ReadAllowedUserAndPin(std::wstring& allowedUser,SchannelStream::Pin& pin) {
    HKEY raw=nullptr;
    const auto opened=RegOpenKeyExW(HKEY_LOCAL_MACHINE,RuntimeKey,0,
        KEY_QUERY_VALUE|KEY_WOW64_64KEY,&raw);
    if(opened!=ERROR_SUCCESS)return false;
    RegistryKey key(raw);

    std::array<wchar_t,192> sid{};
    DWORD type=0,bytes=static_cast<DWORD>(sizeof(sid));
    if(RegQueryValueExW(key.get(),L"AllowedUserSid",nullptr,&type,
        reinterpret_cast<BYTE*>(sid.data()),&bytes)!=ERROR_SUCCESS || type!=REG_SZ ||
        bytes<2*sizeof(wchar_t) || bytes>sizeof(sid) || bytes%sizeof(wchar_t)!=0 ||
        sid[bytes/sizeof(wchar_t)-1]!=L'\0')return false;
    const auto length=bytes/sizeof(wchar_t)-1;
    if(!length||std::find(sid.begin(),sid.begin()+length,L'\0')!=sid.begin()+length)return false;
    allowedUser.assign(sid.data(),length);

    std::array<BYTE,32> hash{};
    type=0;bytes=static_cast<DWORD>(hash.size());
    if(RegQueryValueExW(key.get(),L"ClientCertificateSha256",nullptr,&type,hash.data(),&bytes)!=ERROR_SUCCESS ||
        type!=REG_BINARY || bytes!=hash.size() ||
        std::all_of(hash.begin(),hash.end(),[](BYTE value){return value==0;}))return false;
    std::copy(hash.begin(),hash.end(),pin.begin());
    return true;
}

struct EpochSequence {
    std::atomic<std::uint64_t> next{1};
    bool Allocate(std::uint64_t& output) noexcept {
        auto value=next.load(std::memory_order_relaxed);
        while(value&&value!=(std::numeric_limits<std::uint64_t>::max)()) {
            if(next.compare_exchange_weak(value,value+1,std::memory_order_relaxed)){
                output=value;return true;
            }
        }
        return false;
    }
};

class RegistryEnrollmentResolver final {
public:
    RegistryEnrollmentResolver():epochs_(std::make_shared<EpochSequence>()){}
    std::shared_ptr<const SocketEnrollment> Resolve(const ChannelIdentity& identity) const {
        std::wstring allowedUser;
        SchannelStream::Pin pin{};
        if(!ReadAllowedUserAndPin(allowedUser,pin))return {};
        LPWSTR rawText=nullptr;
        auto* rawSid=const_cast<std::uint8_t*>(identity.userSid.data());
        if(!SameChannelIdentity(identity,identity)||!ConvertSidToStringSidW(rawSid,&rawText))return {};
        LocalString sidText(rawText);
        if(allowedUser!=sidText.get())return {};
        std::uint64_t epoch=0;
        if(!epochs_->Allocate(epoch))return {};
        return std::make_shared<SocketEnrollment>(pin,identity,epoch);
    }
private:
    const std::shared_ptr<EpochSequence> epochs_;
};
}

SocketEnrollmentResolver CreateRegistrySocketEnrollmentResolver(){
    auto resolver=std::make_shared<RegistryEnrollmentResolver>();
    return [resolver=std::move(resolver)](const ChannelIdentity& identity){
        return resolver->Resolve(identity);
    };
}
}
