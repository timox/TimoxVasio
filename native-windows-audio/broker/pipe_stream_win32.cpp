#include "pipe_stream_win32.h"
#include <algorithm>
#include <chrono>
#include <cwchar>
#include <limits>

namespace timox_broker {
namespace {
using Code=NativeChannelCode;
NativeChannelResult Error(DWORD error) noexcept {
    Code code=Code::Failed;
    switch(error){
    case ERROR_FILE_NOT_FOUND:case ERROR_PATH_NOT_FOUND:case ERROR_PIPE_BUSY:code=Code::Unavailable;break;
    case ERROR_ACCESS_DENIED:code=Code::AccessDenied;break;
    case ERROR_BROKEN_PIPE:case ERROR_PIPE_NOT_CONNECTED:case ERROR_NO_DATA:code=Code::Removed;break;
    case ERROR_OPERATION_ABORTED:code=Code::Cancelled;break;
    default:break;
    }
    return {code,0,error};
}
bool LocalName(const wchar_t* name) noexcept {
    if(!name)return false;
    constexpr wchar_t prefix[]=L"\\\\.\\pipe\\";
    constexpr std::size_t prefixLength=sizeof(prefix)/sizeof(prefix[0])-1;
    for(std::size_t i=0;i<prefixLength;++i)if(name[i]!=prefix[i])return false;
    std::size_t length=prefixLength;
    for(;length<240 && name[length];++length){
        const auto c=name[length];
        if(!((c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||(c>=L'0'&&c<=L'9')||c==L'.'||c==L'_'||c==L'-'))return false;
    }
    return length>prefixLength && length<240;
}
DWORD Remaining(PipeStream::Deadline deadline) noexcept {
    const auto now=std::chrono::steady_clock::now();
    if(now>=deadline)return 0;
    const auto ms=std::chrono::ceil<std::chrono::milliseconds>(deadline-now).count();
    return static_cast<DWORD>((std::min)(ms,static_cast<decltype(ms)>(INFINITE-1)));
}
}
struct PipeStream::Impl {
    HANDLE pipe=INVALID_HANDLE_VALUE;
    HANDLE cancelled=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HANDLE completed=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    bool server=false,connected=false;
    ~Impl(){Close();if(completed)CloseHandle(completed);if(cancelled)CloseHandle(cancelled);}
    void Close() noexcept {if(pipe!=INVALID_HANDLE_VALUE){CloseHandle(pipe);pipe=INVALID_HANDLE_VALUE;}server=false;connected=false;}
    NativeChannelResult Ready(Deadline deadline) const noexcept {
        if(!cancelled || !completed)return {Code::Failed,0,ERROR_NOT_ENOUGH_MEMORY};
        if(WaitForSingleObject(cancelled,0)==WAIT_OBJECT_0)return {Code::Cancelled,0,ERROR_OPERATION_ABORTED};
        if(!Remaining(deadline))return {Code::TimedOut,0,WAIT_TIMEOUT};
        return {Code::Ok};
    }
    NativeChannelResult Finish(OVERLAPPED& operation,BOOL immediate,DWORD error,Deadline deadline) noexcept {
        if(!immediate && error!=ERROR_IO_PENDING)return Error(error);
        Code interrupted=Code::Ok;DWORD diagnostic=0;
        if(!immediate){
            const HANDLE events[]={cancelled,completed};
            const auto wait=WaitForMultipleObjects(2,events,FALSE,Remaining(deadline));
            if(wait==WAIT_OBJECT_0){interrupted=Code::Cancelled;diagnostic=ERROR_OPERATION_ABORTED;}
            else if(wait==WAIT_TIMEOUT){interrupted=Code::TimedOut;diagnostic=WAIT_TIMEOUT;}
            else if(wait!=WAIT_OBJECT_0+1){interrupted=Code::Failed;diagnostic=GetLastError();}
            if(interrupted!=Code::Ok)CancelIoEx(pipe,&operation);
        }
        // Even a cancellation that loses its race must acknowledge completion.
        DWORD transferred=0;
        const BOOL ok=GetOverlappedResult(pipe,&operation,&transferred,TRUE);
        const DWORD completionError=ok?ERROR_SUCCESS:GetLastError();
        if(WaitForSingleObject(cancelled,0)==WAIT_OBJECT_0){interrupted=Code::Cancelled;diagnostic=ERROR_OPERATION_ABORTED;}
        if(interrupted!=Code::Ok)return {interrupted,transferred,diagnostic};
        if(!ok){auto result=Error(completionError);result.bytes=transferred;return result;}
        return {Code::Ok,transferred};
    }
};
PipeStream::PipeStream():impl_(std::make_unique<Impl>()){}
PipeStream::~PipeStream()=default;
NativeChannelResult PipeStream::CreateServer(const wchar_t* name,SECURITY_ATTRIBUTES* attributes,bool firstInstance) noexcept {
    const auto ready=impl_->Ready(Deadline::max());if(ready.code!=Code::Ok)return ready;
    if(!LocalName(name) || impl_->pipe!=INVALID_HANDLE_VALUE)return {Code::Failed,0,ERROR_INVALID_PARAMETER};
    impl_->pipe=CreateNamedPipeW(name,PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|(firstInstance?FILE_FLAG_FIRST_PIPE_INSTANCE:0),
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,4,4096,4096,0,attributes);
    if(impl_->pipe==INVALID_HANDLE_VALUE)return Error(GetLastError());
    impl_->server=true;return {Code::Ok};
}
NativeChannelResult PipeStream::Accept(Deadline deadline) noexcept {
    const auto ready=impl_->Ready(deadline);if(ready.code!=Code::Ok)return ready;
    if(!impl_->server || impl_->connected)return {Code::Failed,0,ERROR_INVALID_PARAMETER};
    ResetEvent(impl_->completed);OVERLAPPED operation{};operation.hEvent=impl_->completed;
    const BOOL immediate=ConnectNamedPipe(impl_->pipe,&operation);const auto error=immediate?ERROR_SUCCESS:GetLastError();
    NativeChannelResult result;
    if(!immediate && error==ERROR_PIPE_CONNECTED)result=impl_->Ready(deadline);
    else result=impl_->Finish(operation,immediate,error,deadline);
    if(result.code==Code::Ok)impl_->connected=true;
    return result;
}
NativeChannelResult PipeStream::Connect(const wchar_t* name,Deadline deadline) noexcept {
    auto ready=impl_->Ready(deadline);if(ready.code!=Code::Ok)return ready;
    if(!LocalName(name) || impl_->pipe!=INVALID_HANDLE_VALUE)return {Code::Failed,0,ERROR_INVALID_PARAMETER};
    for(;;){
        ready=impl_->Ready(deadline);if(ready.code!=Code::Ok)return ready;
        // Explicit rights omit FILE_CREATE_PIPE_INSTANCE (aliased APPEND_DATA).
        impl_->pipe=CreateFileW(name,FILE_READ_DATA|FILE_WRITE_DATA|FILE_READ_ATTRIBUTES|FILE_WRITE_ATTRIBUTES|SYNCHRONIZE,
            0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr);
        if(impl_->pipe!=INVALID_HANDLE_VALUE){
            ready=impl_->Ready(deadline);
            if(ready.code!=Code::Ok){impl_->Close();return ready;}
            impl_->connected=true;return {Code::Ok};
        }
        const auto error=GetLastError();if(error!=ERROR_PIPE_BUSY)return Error(error);
        const auto wait=WaitForSingleObject(impl_->cancelled,(std::min)(DWORD{10},Remaining(deadline)));
        if(wait==WAIT_FAILED)return Error(GetLastError());
    }
}
NativeChannelResult PipeStream::ReadSome(void* buffer,std::size_t capacity,Deadline deadline) noexcept {
    const auto ready=impl_->Ready(deadline);if(ready.code!=Code::Ok)return ready;
    if(!buffer || !capacity || capacity>MaxTransferBytes || !impl_->connected)return {Code::Failed,0,ERROR_INVALID_PARAMETER};
    ResetEvent(impl_->completed);OVERLAPPED operation{};operation.hEvent=impl_->completed;
    const BOOL immediate=ReadFile(impl_->pipe,buffer,static_cast<DWORD>(capacity),nullptr,&operation);
    auto result=impl_->Finish(operation,immediate,immediate?ERROR_SUCCESS:GetLastError(),deadline);
    if(result.code==Code::Ok && !result.bytes)return {Code::Removed,0,ERROR_BROKEN_PIPE};
    return result;
}
NativeChannelResult PipeStream::WriteAll(const void* buffer,std::size_t bytes,Deadline deadline) noexcept {
    const auto ready=impl_->Ready(deadline);if(ready.code!=Code::Ok)return ready;
    if(!buffer || !bytes || bytes>MaxTransferBytes || !impl_->connected)return {Code::Failed,0,ERROR_INVALID_PARAMETER};
    DWORD total=0;
    while(total<bytes){
        auto next=impl_->Ready(deadline);if(next.code!=Code::Ok){next.bytes=total;return next;}
        ResetEvent(impl_->completed);OVERLAPPED operation{};operation.hEvent=impl_->completed;
        const BOOL immediate=WriteFile(impl_->pipe,static_cast<const unsigned char*>(buffer)+total,static_cast<DWORD>(bytes-total),nullptr,&operation);
        auto result=impl_->Finish(operation,immediate,immediate?ERROR_SUCCESS:GetLastError(),deadline);
        if(result.bytes>bytes-total)return {Code::Failed,total,ERROR_INVALID_DATA};
        total+=result.bytes;
        if(result.code!=Code::Ok){result.bytes=total;return result;}
        if(!result.bytes)return {Code::Removed,total,ERROR_BROKEN_PIPE};
    }
    return {Code::Ok,total};
}
void PipeStream::RequestCancel() noexcept {if(impl_->cancelled)SetEvent(impl_->cancelled);}
void PipeStream::Close() noexcept {impl_->Close();}
HANDLE PipeStream::BorrowHandle() const noexcept{return impl_->pipe;}
}
