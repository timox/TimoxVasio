#include "native_audio_transport_win32.h"
#include "native_audio_control_protocol.h"
#include "native_audio_packet_validation.h"
#include <array>
#include <atomic>
#include <cstring>
#include <stdexcept>

namespace {
constexpr DWORD MaxBytes=32832;
NativeTransferResult Error(DWORD error) noexcept {
    NativeTransferCode code=NativeTransferCode::Failed;
    switch(error) {
    case ERROR_FILE_NOT_FOUND:case ERROR_PATH_NOT_FOUND:code=NativeTransferCode::Unavailable;break;
    case ERROR_ACCESS_DENIED:code=NativeTransferCode::AccessDenied;break;
    case ERROR_SHARING_VIOLATION:code=NativeTransferCode::OwnedElsewhere;break;
    case ERROR_DEVICE_NOT_CONNECTED:case ERROR_DEV_NOT_EXIST:case ERROR_INVALID_HANDLE:
    case ERROR_DEVICE_REMOVED:case ERROR_NO_SUCH_DEVICE:
        code=NativeTransferCode::Removed;break;
    case ERROR_OPERATION_ABORTED:code=NativeTransferCode::Cancelled;break;
    default:break;
    }
    return {code,0,error};
}
NativeTransferResult Invalid() noexcept {return {NativeTransferCode::InvalidPacket,0,ERROR_INVALID_DATA};}
void Put(unsigned char* data,unsigned offset,std::uint64_t value,unsigned width) noexcept {
    for(unsigned i=0;i<width;++i)data[offset+i]=static_cast<unsigned char>(value>>(8*i));
}
class SystemApi final : public INativeAudioWin32 {
public:
    HANDLE OpenDevice(DWORD& error) noexcept override {
        auto h=CreateFileW(L"\\\\.\\Global\\TimoxWindowsAudioControl",GENERIC_READ|GENERIC_WRITE,
            FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
        error=h==INVALID_HANDLE_VALUE?GetLastError():ERROR_SUCCESS;return h;
    }
    HANDLE CreateManualEvent(DWORD& error) noexcept override {
        auto h=CreateEventW(nullptr,TRUE,FALSE,nullptr);error=h?ERROR_SUCCESS:GetLastError();return h;
    }
    bool Reset(HANDLE h,DWORD& error) noexcept override {
        const bool ok=ResetEvent(h)!=FALSE;error=ok?ERROR_SUCCESS:GetLastError();return ok;
    }
    void Signal(HANDLE h) noexcept override {SetEvent(h);}
    void Close(HANDLE h) noexcept override {CloseHandle(h);}
    bool Begin(HANDLE device,DWORD code,const void* input,DWORD inputBytes,void* output,DWORD outputBytes,
        OVERLAPPED& operation,DWORD& bytes,DWORD& error) noexcept override {
        const bool ok=DeviceIoControl(device,code,const_cast<void*>(input),inputBytes,
            output,outputBytes,&bytes,&operation)!=FALSE;
        error=ok?ERROR_SUCCESS:GetLastError();return ok;
    }
    NativeIoWait Wait(HANDLE stop,HANDLE completed,DWORD& error) noexcept override {
        const HANDLE events[]={stop,completed};
        const DWORD result=WaitForMultipleObjects(2,events,FALSE,INFINITE);
        error=ERROR_SUCCESS;
        if(result==WAIT_OBJECT_0)return NativeIoWait::Cancel;
        if(result==WAIT_OBJECT_0+1)return NativeIoWait::Complete;
        error=result==WAIT_FAILED?GetLastError():ERROR_INVALID_FUNCTION;
        return NativeIoWait::Failed;
    }
    bool Cancel(HANDLE device,OVERLAPPED& operation,DWORD& error) noexcept override {
        const bool ok=CancelIoEx(device,&operation)!=FALSE;error=ok?ERROR_SUCCESS:GetLastError();return ok;
    }
    bool Finish(HANDLE device,OVERLAPPED& operation,bool wait,DWORD& bytes,DWORD& error) noexcept override {
        const bool ok=GetOverlappedResult(device,&operation,&bytes,wait?TRUE:FALSE)!=FALSE;
        error=ok?ERROR_SUCCESS:GetLastError();return ok;
    }
};
class DirectTransport final : public INativeAudioTransport {
public:
    explicit DirectTransport(std::shared_ptr<INativeAudioWin32> api):api_(std::move(api)) {
        if(!api_)throw std::invalid_argument("Native transport requires an OS boundary");
        stop_=api_->CreateManualEvent(initializationError_);
        if(stop_)completed_=api_->CreateManualEvent(initializationError_);
        if((!stop_ || !completed_) && !initializationError_)initializationError_=ERROR_NOT_ENOUGH_MEMORY;
    }
    ~DirectTransport() override {
        Close();if(completed_)api_->Close(completed_);if(stop_)api_->Close(stop_);
    }
    NativeTransferResult Open(NativeSessionInfo& info) noexcept override {
        info={};
        if(cancelled_.load(std::memory_order_acquire))return Error(ERROR_OPERATION_ABORTED);
        if(initializationError_)return Error(initializationError_);
        if(device_!=INVALID_HANDLE_VALUE)return Error(ERROR_INVALID_STATE);
        DWORD error=0;device_=api_->OpenDevice(error);
        if(device_==INVALID_HANDLE_VALUE)return Error(error);
        auto result=Io(TIMOX_IOCTL_SESSION,nullptr,0,reply_.data(),8);
        if(result.code==NativeTransferCode::Ok) {
            if(result.bytes!=8)result=Invalid();
            else {
                generation_=timox_native::detail::ReadLittleEndian(reply_.data(),0,8);
                if(!generation_)result=Invalid();
            }
        }
        if(result.code!=NativeTransferCode::Ok){Close();return result;}
        info.wireGeneration=generation_;return {NativeTransferCode::Ok,0,0};
    }
    NativeTransferResult ReadRender(std::uint32_t maxFrames,void* packet,std::uint32_t capacity) noexcept override {
        // ACL/DRM denial and malformed replies supply no audio. Scratch never
        // escapes before validation; the bounded caller span is silenced first.
        if(packet && capacity<=MaxBytes)std::memset(packet,0,capacity);
        if(!packet || !maxFrames || maxFrames>4096 || capacity>MaxBytes || capacity<64+maxFrames*8)return Invalid();
        if(!generation_)return Error(ERROR_INVALID_HANDLE);
        std::array<unsigned char,16> request{};
        Put(request.data(),0,generation_,8);Put(request.data(),8,maxFrames,4);
        auto result=Io(TIMOX_IOCTL_READ_RENDER,request.data(),16,reply_.data(),64+maxFrames*8);
        if(result.code!=NativeTransferCode::Ok) {
            if(result.systemError==ERROR_NO_MORE_ITEMS)result.code=NativeTransferCode::Empty;
            return result;
        }
        const auto checked=timox_native::ValidatePacket(reply_.data(),result.bytes,
            {generation_,TIMOX_NATIVE_FROM_WINDOWS_RENDER,maxFrames});
        if(checked.error!=timox_native::PacketError::None ||
            (renderSeen_ && checked.metadata.sequence<=renderSequence_))return Invalid();
        renderSequence_=checked.metadata.sequence;renderSeen_=true;
        std::memcpy(packet,reply_.data(),result.bytes);return result;
    }
    NativeTransferResult WriteCapture(const void* packet,std::uint32_t bytes) noexcept override {
        if(!generation_)return Error(ERROR_INVALID_HANDLE);
        if(!packet || bytes>MaxBytes)return Invalid();
        const auto checked=timox_native::ValidatePacket(packet,bytes,
            {generation_,TIMOX_NATIVE_TO_WINDOWS_CAPTURE,4096});
        if(checked.error!=timox_native::PacketError::None ||
            (captureSeen_ && checked.metadata.sequence<=captureSequence_))return Invalid();
        std::memcpy(write_.data(),packet,bytes);
        auto result=Io(TIMOX_IOCTL_WRITE_CAPTURE,write_.data(),bytes,nullptr,0);
        if(result.code!=NativeTransferCode::Ok) {
            if(result.systemError==ERROR_BUSY)result.code=NativeTransferCode::Full;
            return result;
        }
        captureSeen_=true;captureSequence_=checked.metadata.sequence;return result;
    }
    NativeTransferResult ReadStatus(std::uint64_t (&counters)[3]) noexcept override {
        for(auto& value:counters)value=0;
        if(!generation_)return Error(ERROR_INVALID_HANDLE);
        std::array<unsigned char,8> request{};Put(request.data(),0,generation_,8);
        auto result=Io(TIMOX_IOCTL_STATUS,request.data(),8,reply_.data(),24);
        if(result.code!=NativeTransferCode::Ok)return result;
        if(result.bytes!=24)return Invalid();
        for(unsigned i=0;i<3;++i)counters[i]=timox_native::detail::ReadLittleEndian(reply_.data(),i*8,8);
        return result;
    }
    void RequestCancel() noexcept override {
        cancelled_.store(true,std::memory_order_release);
        if(stop_)api_->Signal(stop_); // Event lifetime fixed until destructor.
    }
    void Close() noexcept override {
        // Every Io call has observed completion before returning.
        if(device_!=INVALID_HANDLE_VALUE){api_->Close(device_);device_=INVALID_HANDLE_VALUE;}
        generation_=renderSequence_=captureSequence_=0;renderSeen_=captureSeen_=false;
    }
private:
    NativeTransferResult Io(DWORD code,const void* input,DWORD inputBytes,void* output,DWORD outputBytes) noexcept {
        if(cancelled_.load(std::memory_order_acquire))return Error(ERROR_OPERATION_ABORTED);
        DWORD error=0,bytes=0;if(!api_->Reset(completed_,error))return Error(error);
        OVERLAPPED operation{};operation.hEvent=completed_;
        bool ok=api_->Begin(device_,code,input,inputBytes,output,outputBytes,operation,bytes,error);
        if(!ok && error==ERROR_IO_PENDING) {
            DWORD waitError=0;const auto wait=api_->Wait(stop_,completed_,waitError);
            if(wait!=NativeIoWait::Complete) {
                DWORD cancelError=0;api_->Cancel(device_,operation,cancelError);
                // CancelIoEx is not a join. Even ERROR_NOT_FOUND can mean a
                // completion race: keep all memory until Finish has returned.
                api_->Finish(device_,operation,true,bytes,error);
                return wait==NativeIoWait::Cancel?Error(ERROR_OPERATION_ABORTED):
                    NativeTransferResult{NativeTransferCode::Failed,0,waitError};
            }
            ok=api_->Finish(device_,operation,true,bytes,error);
        }
        if(cancelled_.load(std::memory_order_acquire))return Error(ERROR_OPERATION_ABORTED);
        if(!ok)return Error(error);
        if(bytes>outputBytes)return Invalid();
        return {NativeTransferCode::Ok,bytes,0};
    }
    std::shared_ptr<INativeAudioWin32> api_;
    HANDLE stop_=nullptr,completed_=nullptr,device_=INVALID_HANDLE_VALUE;
    DWORD initializationError_=0;
    std::atomic<bool> cancelled_{false};
    std::uint64_t generation_=0,renderSequence_=0,captureSequence_=0;
    bool renderSeen_=false,captureSeen_=false;
    std::array<unsigned char,MaxBytes> reply_{},write_{};
};
}
std::unique_ptr<INativeAudioTransport> CreateDirectNativeAudioLabTransport(std::shared_ptr<INativeAudioWin32> api) {
    return std::make_unique<DirectTransport>(std::move(api));
}
std::unique_ptr<INativeAudioTransport> CreateDirectNativeAudioLabTransport() {
    return CreateDirectNativeAudioLabTransport(std::make_shared<SystemApi>());
}
