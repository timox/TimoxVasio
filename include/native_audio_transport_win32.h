#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "native_audio_transport.h"

enum class NativeIoWait { Complete, Cancel, Failed };
// Injectable OS boundary. Implementations preserve Win32 error/byte semantics;
// only Signal may run concurrently. Owned by the transport until destruction.
class INativeAudioWin32 {
public:
    virtual ~INativeAudioWin32() = default;
    virtual HANDLE OpenDevice(DWORD& error) noexcept = 0;
    virtual HANDLE CreateManualEvent(DWORD& error) noexcept = 0;
    virtual bool Reset(HANDLE event, DWORD& error) noexcept = 0;
    virtual void Signal(HANDLE event) noexcept = 0;
    virtual void Close(HANDLE handle) noexcept = 0;
    virtual bool Begin(HANDLE device, DWORD code, const void* input, DWORD inputBytes,
        void* output, DWORD outputBytes, OVERLAPPED& operation, DWORD& bytes, DWORD& error) noexcept = 0;
    virtual NativeIoWait Wait(HANDLE stop, HANDLE completed, DWORD& error) noexcept = 0;
    virtual bool Cancel(HANDLE device, OVERLAPPED& operation, DWORD& error) noexcept = 0;
    // wait=true must observe completion (including failure/cancellation) before
    // returning. It cannot manufacture a timeout while kernel memory is borrowed.
    virtual bool Finish(HANDLE device, OVERLAPPED& operation, bool wait,
        DWORD& bytes, DWORD& error) noexcept = 0;
};
std::unique_ptr<INativeAudioTransport> CreateDirectNativeAudioLabTransport(std::shared_ptr<INativeAudioWin32> api);
