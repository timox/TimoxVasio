// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <windows.h>
#include <ole2.h>
#include <memory>
#include <vector>
#include <utility>
#include <atomic>
#include <thread>
#include "audio_transport.h"
#include "vasio_compatibility_profile.h"
#include "combase.h"
#include "iasiodrv.h"

extern const CLSID IID_ASIO_DRIVER;

class VASIODriver final : public IASIO, public CUnknown {
public:
    VASIODriver(LPUNKNOWN outer, HRESULT* result, int driverId);
    ~VASIODriver() override;

    DECLARE_IUNKNOWN
    static CUnknown* CreateInstance(LPUNKNOWN outer, HRESULT* result);
    HRESULT STDMETHODCALLTYPE NonDelegatingQueryInterface(REFIID iid, void** out) override;

    ASIOBool init(void* systemHandle) override;
    void getDriverName(char* name) override;
    long getDriverVersion() override;
    void getErrorMessage(char* message) override;
    ASIOError start() override;
    ASIOError stop() override;
    ASIOError getChannels(long* inputs, long* outputs) override;
    ASIOError getLatencies(long* input, long* output) override;
    ASIOError getBufferSize(long* minimum, long* maximum, long* preferred, long* granularity) override;
    ASIOError canSampleRate(ASIOSampleRate rate) override;
    ASIOError getSampleRate(ASIOSampleRate* rate) override;
    ASIOError setSampleRate(ASIOSampleRate rate) override;
    ASIOError getClockSources(ASIOClockSource* sources, long* count) override;
    ASIOError setClockSource(long reference) override;
    ASIOError getSamplePosition(ASIOSamples* position, ASIOTimeStamp* timestamp) override;
    ASIOError getChannelInfo(ASIOChannelInfo* info) override;
    ASIOError createBuffers(ASIOBufferInfo* infos, long count, long frames, ASIOCallbacks* callbacks) override;
    ASIOError disposeBuffers() override;
    ASIOError controlPanel() override;
    ASIOError future(long selector, void* value) override;
    ASIOError outputReady() override;

private:
    struct ChannelBuffer {
        bool isInput;
        long channel;
        float* buffers[2];
    };

    void callbackLoop() noexcept;

    int driverId_;
    long announcedInputChannels_ = static_cast<long>(AudioClientMapping::kChannelCount);
    long announcedOutputChannels_ = static_cast<long>(AudioClientMapping::kChannelCount);
    char name_[32]{};
    char error_[128]{};
    ASIOSampleRate sampleRate_;
    long bufferFrames_;
    ASIOCallbacks* callbacks_;
    std::atomic<bool> running_;
    std::vector<std::unique_ptr<float[]>> buffers_;
    std::vector<ChannelBuffer> channelBuffers_;
    std::vector<float> transportBlock_;
    std::unique_ptr<AudioClientMapping> transport_;
    std::thread callbackThread_;
    std::atomic<std::uint64_t> samplePosition_{0};
};
