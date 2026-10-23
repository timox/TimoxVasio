#pragma once
#include "native_audio_broker_transport.h"
#include "byte_stream.h"
#include <Windows.h>
#include <memory>

namespace timox_broker {
// Raw local byte stream, NOT authentication or an authorization boundary.
// Worker-only except RequestCancel. One operation outstanding. A returned call
// retains no caller buffer/OVERLAPPED; cancellation is sticky across Close.
// Destruction must not race any method. No FlushFileBuffers (peer-dependent wait).
// A deadline triggers cancellation; draining the OS completion can exceed it.
// This guarantees buffer lifetime, not a hard realtime completion bound.
class PipeStream final : public ByteStream {
public:
    using Deadline=INativeAudioAuthenticatedChannel::Deadline;
    PipeStream();
    ~PipeStream() override;
    PipeStream(const PipeStream&)=delete;
    PipeStream& operator=(const PipeStream&)=delete;
    // Caller supplies the production DACL. Null attributes are only suitable for
    // explicitly scoped current-user tests, not service admission.
    NativeChannelResult CreateServer(const wchar_t* localName,
        SECURITY_ATTRIBUTES* attributes,bool firstInstance=true) noexcept;
    NativeChannelResult Accept(Deadline) noexcept;
    NativeChannelResult Connect(const wchar_t* localName,Deadline) noexcept;
    NativeChannelResult ReadSome(void*,std::size_t capacity,Deadline) noexcept override;
    // On failure bytes reports the prefix transferred, never an implicit retry.
    NativeChannelResult WriteAll(const void*,std::size_t bytes,Deadline) noexcept override;
    void RequestCancel() noexcept override;
    void Close() noexcept override;
    // Borrowed worker-only handle for subsequent Windows identity observation.
    HANDLE BorrowHandle() const noexcept;
    static std::size_t RetainedApplicationBytes() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace timox_broker
