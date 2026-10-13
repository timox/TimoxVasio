// SPDX-License-Identifier: GPL-3.0-only
#include "vasio_com_driver.h"

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

namespace {
constexpr long kChannelCount = static_cast<long>(AudioClientMapping::kChannelCount);
constexpr long kMinBufferFrames = 64;
constexpr long kMaxBufferFrames = 8192;
constexpr long kPreferredBufferFrames = 256;
constexpr DWORD kEngineAttachWaitMs = 2500;
constexpr DWORD kAudioAckWaitMs = 2500;
constexpr char kEngineUnavailable[] = "TimoxVasio audio transport is not connected";
thread_local const void* currentCallbackDriver = nullptr;

bool isSupportedRate(ASIOSampleRate rate) {
    return rate == 44100.0 || rate == 48000.0 || rate == 88200.0 ||
           rate == 96000.0 || rate == 176400.0 || rate == 192000.0;
}

std::uint64_t queryPerformanceNanoseconds() {
    LARGE_INTEGER counter{};
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceCounter(&counter) || !QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
        return 0;
    const auto ticks = static_cast<std::uint64_t>(counter.QuadPart);
    const auto hz = static_cast<std::uint64_t>(frequency.QuadPart);
    return (ticks / hz) * 1000000000ULL + ((ticks % hz) * 1000000000ULL) / hz;
}

void splitSamplePosition(std::uint64_t value, ASIOSamples* samples) {
#if NATIVE_INT64
    *samples = static_cast<ASIOSamples>(value);
#else
    samples->hi = static_cast<unsigned long>(value >> 32);
    samples->lo = static_cast<unsigned long>(value);
#endif
}

void splitTimestamp(std::uint64_t value, ASIOTimeStamp* timestamp) {
#if NATIVE_INT64
    *timestamp = static_cast<ASIOTimeStamp>(value);
#else
    timestamp->hi = static_cast<unsigned long>(value >> 32);
    timestamp->lo = static_cast<unsigned long>(value);
#endif
}
}

VASIODriver::VASIODriver(LPUNKNOWN outer, HRESULT* result, int driverId)
    : CUnknown(const_cast<TCHAR*>(TEXT("TimoxVasio")), outer, result),
      driverId_(driverId),
      sampleRate_(44100.0),
      bufferFrames_(kPreferredBufferFrames),
      callbacks_(nullptr),
      running_(false) {
    std::snprintf(name_, sizeof(name_), "TimoxVasio");
    error_[0] = '\0';
    const auto channelCounts = VasioCompatibilityProfile::ChannelCountsForCurrentProcess();
    announcedInputChannels_ = channelCounts.inputs;
    announcedOutputChannels_ = channelCounts.outputs;
}

VASIODriver::~VASIODriver() {
    stop();
    disposeBuffers();
    if (transport_) transport_->PublishClientState(AudioClientMapping::ClientState::Detached);
    transport_.reset();
}

CUnknown* VASIODriver::CreateInstance(LPUNKNOWN outer, HRESULT* result) {
    return new (std::nothrow) VASIODriver(outer, result, 1);
}

HRESULT STDMETHODCALLTYPE VASIODriver::NonDelegatingQueryInterface(REFIID iid, void** out) {
    if (!out) return E_POINTER;
    if (IsEqualIID(iid, IID_ASIO_DRIVER)) return GetInterface(static_cast<IASIO*>(this), out);
    return CUnknown::NonDelegatingQueryInterface(iid, out);
}

ASIOBool VASIODriver::init(void*) {
    if (transport_ && transport_->EngineAttached()) return ASIOTrue;
    // An old callback can still be leaving after it cleared running_.
    // Keep its transport mapped until an external thread has joined it.
    if (transport_ && stop() != ASE_OK)
        return ASIOFalse;
    transport_.reset();
    transport_ = AudioClientMapping::CreateClient(static_cast<std::uint32_t>(driverId_),
        GetCurrentProcessId(), static_cast<std::uint32_t>(sampleRate_),
        static_cast<std::uint32_t>(bufferFrames_));
    if (!transport_) {
        std::strncpy(error_, "Unable to create TimoxVasio shared audio transport", sizeof(error_) - 1);
        error_[sizeof(error_) - 1] = '\0';
        // Hosts such as PortAudio initialize every registered ASIO driver
        // while enumerating devices. Keep the driver discoverable when the
        // engine has not created its shared transport yet; createBuffers()
        // will reject audio use until the physical clock is connected.
        return ASIOTrue;
    }
    transport_->PublishClientState(AudioClientMapping::ClientState::Initialized);
    transport_->PublishClientRequestedFormat(static_cast<std::uint32_t>(sampleRate_),
        static_cast<std::uint32_t>(bufferFrames_));
    const ULONGLONG deadline = GetTickCount64() + kEngineAttachWaitMs;
    while (!transport_->EngineAttached() && GetTickCount64() < deadline) Sleep(10);
    if (!transport_->EngineAttached()) {
        std::strncpy(error_, "TimoxVasio engine did not attach this client", sizeof(error_) - 1);
        error_[sizeof(error_) - 1] = '\0';
        // ASIO hosts call init while building their device list. Keep the
        // driver enumerable while the engine is offline; createBuffers will
        // report the connection error if the host tries to start audio.
        return ASIOTrue;
    }
    if (!transport_->EngineProcessAlive()) {
        std::strncpy(error_, "TimoxVasio engine process is not reachable", sizeof(error_) - 1);
        error_[sizeof(error_) - 1] = '\0';
        return ASIOTrue;
    }
    sampleRate_ = transport_->SampleRate();
    error_[0] = '\0';
    return ASIOTrue;
}

void VASIODriver::getDriverName(char* name) {
    if (name) std::strncpy(name, name_, 32);
}

long VASIODriver::getDriverVersion() { return 100; }

void VASIODriver::getErrorMessage(char* message) {
    if (message) {
        const char* source = error_[0] ? error_ : kEngineUnavailable;
        std::strncpy(message, source, 128);
        message[127] = '\0';
    }
}

ASIOError VASIODriver::start() {
    const char* failure = nullptr;
    if (!transport_) failure = "TimoxVasio start called before init";
    else if (!transport_->EngineAttached()) failure = "TimoxVasio engine is not attached to this client";
    else if (!transport_->EngineProcessAlive()) failure = "TimoxVasio engine process is not reachable";
    else if (!callbacks_ || channelBuffers_.empty()) failure = "ASIO buffers and callbacks are not ready";
    if (failure) {
        std::strncpy(error_, failure, sizeof(error_) - 1);
        error_[sizeof(error_) - 1] = '\0';
        return ASE_NotPresent;
    }
    // The callback worker may have stopped itself after a format change.
    // Reap it before assigning a new std::thread, which would otherwise terminate.
    if (running_.load()) return ASE_InvalidMode;
    if (!joinCallbackWorker()) return ASE_InvalidMode;
    const auto audioEpoch = transport_->AudioEpoch();
    const ULONGLONG deadline = GetTickCount64() + kAudioAckWaitMs;
    while (audioEpoch != 0 && transport_->AudioEpoch() == audioEpoch &&
           !transport_->AudioGenerationAcknowledged() && GetTickCount64() < deadline)
        Sleep(10);
    if (audioEpoch == 0 || transport_->AudioEpoch() != audioEpoch ||
        !transport_->AudioGenerationAcknowledged() ||
        transport_->SampleRate() != static_cast<std::uint32_t>(sampleRate_) ||
        transport_->BlockFrames() != static_cast<std::uint32_t>(bufferFrames_)) {
        std::strncpy(error_, "TimoxVasio engine did not acknowledge the audio generation and format",
                     sizeof(error_) - 1);
        error_[sizeof(error_) - 1] = '\0';
        return ASE_NotPresent;
    }
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return ASE_InvalidMode;
    if (transport_) transport_->PublishClientState(AudioClientMapping::ClientState::Running);
    try {
        std::lock_guard<std::mutex> lock(callbackThreadMutex_);
        NonDelegatingAddRef();
        callbackWorkerReferenceHeld_ = true;
        callbackThread_ = std::thread(&VASIODriver::callbackLoop, this);
    } catch (...) {
        running_.store(false);
        if (transport_) transport_->PublishClientState(AudioClientMapping::ClientState::Stopped);
        std::strncpy(error_, "Unable to start TimoxVasio callback worker", sizeof(error_) - 1);
        error_[sizeof(error_) - 1] = '\0';
        if (callbackWorkerReferenceHeld_) {
            callbackWorkerReferenceHeld_ = false;
            NonDelegatingRelease();
        }
        return ASE_NoMemory;
    }
    error_[0] = '\0';
    return ASE_OK;
}

ASIOError VASIODriver::stop() {
    running_.store(false);
    if (transport_) transport_->SignalClient();
    if (!joinCallbackWorker()) return ASE_InvalidMode;
    if (transport_) transport_->PublishClientState(AudioClientMapping::ClientState::Stopped);
    return ASE_OK;
}

bool VASIODriver::joinCallbackWorker(bool onlyIfStopped) noexcept {
    if (currentCallbackDriver == this) return false;
    bool releaseWorkerReference = false;
    {
        std::lock_guard<std::mutex> lock(callbackThreadMutex_);
        if (onlyIfStopped && running_.load()) return true;
        if (callbackThread_.joinable()) {
            if (callbackThread_.get_id() == std::this_thread::get_id()) return false;
            try {
                callbackThread_.join();
            } catch (...) {
                return false;
            }
        }
        releaseWorkerReference = callbackWorkerReferenceHeld_;
        callbackWorkerReferenceHeld_ = false;
    }
    if (releaseWorkerReference) NonDelegatingRelease();
    return true;
}

ASIOError VASIODriver::getChannels(long* inputs, long* outputs) {
    if (!inputs || !outputs) return ASE_InvalidParameter;
    *inputs = announcedInputChannels_;
    *outputs = announcedOutputChannels_;
    return ASE_OK;
}

ASIOError VASIODriver::getLatencies(long* input, long* output) {
    if (!input || !output) return ASE_InvalidParameter;
    const auto confirmedFrames = transport_ && transport_->EngineAttached()
        ? transport_->BlockFrames() : static_cast<std::uint32_t>(bufferFrames_);
    *input = static_cast<long>(confirmedFrames);
    *output = static_cast<long>(confirmedFrames);
    return ASE_OK;
}

ASIOError VASIODriver::getBufferSize(long* minimum, long* maximum, long* preferred, long* granularity) {
    if (!minimum || !maximum || !preferred || !granularity) return ASE_InvalidParameter;
    if (transport_ && transport_->EngineAttached() && transport_->BlockFrames()) {
        const auto confirmed = static_cast<long>(transport_->BlockFrames());
        *minimum = *maximum = *preferred = confirmed;
        *granularity = 0;
        return ASE_OK;
    }
    *minimum = kMinBufferFrames;
    *maximum = kMaxBufferFrames;
    *preferred = transport_ ? static_cast<long>(transport_->PreferredBufferFrames()) : kPreferredBufferFrames;
    *granularity = -1; // ASIO powers-of-two buffer sizes.
    return ASE_OK;
}

ASIOError VASIODriver::canSampleRate(ASIOSampleRate rate) {
    if (transport_ && transport_->EngineAttached()) {
        return static_cast<std::uint32_t>(rate) == transport_->SampleRate()
            ? ASE_OK : ASE_NotPresent;
    }
    return isSupportedRate(rate) ? ASE_OK : ASE_NotPresent;
}

ASIOError VASIODriver::getSampleRate(ASIOSampleRate* rate) {
    if (!rate) return ASE_InvalidParameter;
    *rate = transport_ ? static_cast<ASIOSampleRate>(transport_->SampleRate()) : sampleRate_;
    return ASE_OK;
}

ASIOError VASIODriver::setSampleRate(ASIOSampleRate rate) {
    if (!isSupportedRate(rate)) return ASE_NotPresent;
    if (running_.load()) return ASE_InvalidMode;
    if (transport_ && transport_->EngineAttached()) {
        if (static_cast<std::uint32_t>(rate) != transport_->SampleRate()) return ASE_InvalidMode;
        sampleRate_ = rate;
        transport_->PublishClientRequestedFormat(static_cast<std::uint32_t>(rate), 0);
        return ASE_OK;
    }
    sampleRate_ = rate;
    if (transport_ && !transport_->SetSampleRate(static_cast<std::uint32_t>(rate))) return ASE_InvalidMode;
    if (transport_) transport_->PublishClientRequestedFormat(static_cast<std::uint32_t>(rate), 0);
    return ASE_OK;
}

ASIOError VASIODriver::getClockSources(ASIOClockSource* sources, long* count) {
    if (!count) return ASE_InvalidParameter;
    if (*count > 0 && !sources) return ASE_InvalidParameter;
    *count = 0;
    return ASE_OK;
}

ASIOError VASIODriver::setClockSource(long) { return ASE_NotPresent; }

ASIOError VASIODriver::getSamplePosition(ASIOSamples* position, ASIOTimeStamp* timestamp) {
    if (!position || !timestamp) return ASE_InvalidParameter;
    splitSamplePosition(samplePosition_.load(std::memory_order_relaxed), position);
    splitTimestamp(queryPerformanceNanoseconds(), timestamp);
    return ASE_OK;
}

ASIOError VASIODriver::getChannelInfo(ASIOChannelInfo* info) {
    if (!info || info->channel < 0 ||
        info->channel >= (info->isInput == ASIOTrue ? announcedInputChannels_ : announcedOutputChannels_))
        return ASE_InvalidParameter;
    info->isActive = ASIOFalse;
    info->channelGroup = 0;
    info->type = ASIOSTFloat32LSB;
    std::snprintf(info->name, sizeof(info->name), "TimoxVasio %s %ld",
                  info->isInput ? "In" : "Out", info->channel + 1);
    return ASE_OK;
}

ASIOError VASIODriver::createBuffers(ASIOBufferInfo* infos, long count, long frames, ASIOCallbacks* callbacks) {
    constexpr long kMaxBufferInfos = static_cast<long>(AudioClientMapping::kChannelCount * 2);
    const bool engineClockReady = transport_ && transport_->EngineAttached() &&
        transport_->EngineProcessAlive() && transport_->BlockFrames();
    if (!engineClockReady) {
        std::strncpy(error_, "TimoxVasio cannot create buffers until the physical ASIO clock is connected",
                     sizeof(error_) - 1);
        error_[sizeof(error_) - 1] = '\0';
        return ASE_NotPresent;
    }
    if (engineClockReady) sampleRate_ = static_cast<ASIOSampleRate>(transport_->SampleRate());
    if (!infos || !callbacks || count <= 0 || count > kMaxBufferInfos ||
        (!engineClockReady && (frames < kMinBufferFrames || frames > kMaxBufferFrames)))
        return ASE_InvalidParameter;
    if (!engineClockReady && (frames & (frames - 1))) return ASE_InvalidParameter;
    if (engineClockReady &&
        (static_cast<std::uint32_t>(frames) != transport_->BlockFrames() ||
         static_cast<std::uint32_t>(sampleRate_) != transport_->SampleRate())) return ASE_InvalidMode;
    if (running_.load() || !buffers_.empty()) return ASE_InvalidMode;
    if (!joinCallbackWorker()) return ASE_InvalidMode;

    std::vector<std::unique_ptr<float[]>> pending;
    std::vector<ChannelBuffer> pendingChannels;
    AudioClientMapping::ChannelMask activeInputs{};
    AudioClientMapping::ChannelMask activeOutputs{};
    pending.reserve(static_cast<size_t>(count * 2));
    pendingChannels.reserve(static_cast<size_t>(count));
    std::vector<std::pair<bool, long>> channels;
    channels.reserve(static_cast<size_t>(count));
    for (long i = 0; i < count; ++i) {
        if (infos[i].isInput != ASIOTrue && infos[i].isInput != ASIOFalse) return ASE_InvalidParameter;
        const long limit = infos[i].isInput == ASIOTrue
            ? announcedInputChannels_ : announcedOutputChannels_;
        if (infos[i].channelNum < 0 || infos[i].channelNum >= limit) return ASE_InvalidParameter;
        for (const auto& previous : channels) {
            if (previous.first == (infos[i].isInput != 0) && previous.second == infos[i].channelNum)
                return ASE_InvalidParameter;
        }
        channels.emplace_back(infos[i].isInput != 0, infos[i].channelNum);
        auto& mask = infos[i].isInput ? activeInputs : activeOutputs;
        mask[static_cast<std::size_t>(infos[i].channelNum) / 64] |=
            std::uint64_t{1} << (static_cast<std::uint32_t>(infos[i].channelNum) % 64);
        auto left = std::unique_ptr<float[]>(new (std::nothrow) float[frames]{});
        auto right = std::unique_ptr<float[]>(new (std::nothrow) float[frames]{});
        if (!left || !right) return ASE_NoMemory;
        infos[i].buffers[0] = left.get();
        infos[i].buffers[1] = right.get();
        pendingChannels.push_back({infos[i].isInput != 0, infos[i].channelNum, 0,
                                   {left.get(), right.get()}});
        pending.push_back(std::move(left));
        pending.push_back(std::move(right));
    }
    std::uint32_t inputStride = 0;
    std::uint32_t outputStride = 0;
    for (std::uint32_t channel = 0; channel < AudioClientMapping::kChannelCount; ++channel) {
        const auto word = channel / 64;
        const auto bit = std::uint64_t{1} << (channel % 64);
        if (activeInputs[word] & bit) ++inputStride;
        if (activeOutputs[word] & bit) ++outputStride;
    }
    std::vector<float> pendingTransportBlock;
    try {
        pendingTransportBlock.assign(static_cast<std::size_t>(frames) *
            (std::max)(inputStride, outputStride), 0.0f);
    } catch (...) {
        return ASE_NoMemory;
    }
    if (!transport_->SetActiveChannels(activeInputs, activeOutputs)) return ASE_NotPresent;
    for (auto& channel : pendingChannels) {
        const auto slot = channel.isInput
            ? transport_->InputSlot(static_cast<std::uint32_t>(channel.channel))
            : transport_->OutputSlot(static_cast<std::uint32_t>(channel.channel));
        if (slot < 0) return ASE_NotPresent;
        channel.slot = static_cast<std::uint32_t>(slot);
    }
    buffers_ = std::move(pending);
    channelBuffers_ = std::move(pendingChannels);
    transportBlock_ = std::move(pendingTransportBlock);
    inputStride_ = transport_->InputStride();
    outputStride_ = transport_->OutputStride();
    callbacks_ = callbacks;
    resetMessage_.store(callbacks->asioMessage, std::memory_order_release);
    bufferFrames_ = frames;
    if (transport_) {
        transport_->PublishClientRequestedFormat(static_cast<std::uint32_t>(sampleRate_),
            static_cast<std::uint32_t>(frames));
        transport_->PublishClientState(AudioClientMapping::ClientState::BuffersReady);
    }
    error_[0] = '\0';
    return ASE_OK;
}

ASIOError VASIODriver::disposeBuffers() {
    if (running_.load()) return ASE_InvalidMode;
    if (!joinCallbackWorker()) return ASE_InvalidMode;
    if (transport_ && !transport_->RetireAudioGeneration()) return ASE_NotPresent;
    buffers_.clear();
    channelBuffers_.clear();
    transportBlock_.clear();
    inputStride_ = 0;
    outputStride_ = 0;
    callbacks_ = nullptr;
    resetMessage_.store(nullptr, std::memory_order_release);
    if (transport_) transport_->PublishClientState(AudioClientMapping::ClientState::Initialized);
    return ASE_OK;
}

ASIOError VASIODriver::controlPanel() { return ASE_NotPresent; }
ASIOError VASIODriver::future(long, void*) { return ASE_NotPresent; }
ASIOError VASIODriver::outputReady() { return ASE_NotPresent; }

void VASIODriver::callbackLoop() noexcept {
    currentCallbackDriver = this;
    long bufferIndex = 0;
    std::uint32_t stageCallbackNumber = 0;
    bool formatChanged = false;
    while (running_.load()) {
        if (!transport_->EngineAttached()) {
            running_.store(false);
            break;
        }
        if (transport_->SampleRate() != static_cast<std::uint32_t>(sampleRate_) ||
            transport_->BlockFrames() != static_cast<std::uint32_t>(bufferFrames_)) {
            formatChanged = true;
            running_.store(false);
            break;
        }
        if (!transport_->WaitForClientSignal(250)) continue;
        while (running_.load() && transport_->ClientInputAvailable() >= static_cast<std::uint32_t>(bufferFrames_)) {
            if (transport_->SampleRate() != static_cast<std::uint32_t>(sampleRate_) ||
                transport_->BlockFrames() != static_cast<std::uint32_t>(bufferFrames_)) {
                formatChanged = true;
                running_.store(false);
                break;
            }
            transport_->ReadClientInput(transportBlock_.data(), static_cast<std::uint32_t>(bufferFrames_));
            for (const auto& channel : channelBuffers_) {
                if (!channel.isInput) continue;
                for (long frame = 0; frame < bufferFrames_; ++frame)
                    channel.buffers[bufferIndex][frame] = transportBlock_[static_cast<size_t>(frame) * inputStride_ + channel.slot];
            }

            const bool measureStage = transport_->StageMeasurementsEnabled() &&
                (++stageCallbackNumber % 16u == 0u);
            LARGE_INTEGER callbackStart{}, callbackEnd{}, copyStart{}, copyEnd{};
            if (measureStage) QueryPerformanceCounter(&callbackStart);
            if (callbacks_->bufferSwitchTimeInfo) {
                ASIOTime timeInfo{};
                const auto position = samplePosition_.load(std::memory_order_relaxed);
                splitSamplePosition(position, &timeInfo.timeInfo.samplePosition);
                splitTimestamp(queryPerformanceNanoseconds(), &timeInfo.timeInfo.systemTime);
                timeInfo.timeInfo.flags = kSystemTimeValid | kSamplePositionValid;
                callbacks_->bufferSwitchTimeInfo(&timeInfo, bufferIndex, ASIOFalse);
            } else if (callbacks_->bufferSwitch) {
                callbacks_->bufferSwitch(bufferIndex, ASIOFalse);
            }
            if (measureStage) {
                QueryPerformanceCounter(&callbackEnd);
                QueryPerformanceCounter(&copyStart);
            }

            std::fill(transportBlock_.begin(), transportBlock_.end(), 0.0f);
            for (const auto& channel : channelBuffers_) {
                if (channel.isInput) continue;
                for (long frame = 0; frame < bufferFrames_; ++frame)
                    transportBlock_[static_cast<size_t>(frame) * outputStride_ + channel.slot] = channel.buffers[bufferIndex][frame];
            }
            if (measureStage) QueryPerformanceCounter(&copyEnd);
            const auto frames = static_cast<std::uint32_t>(bufferFrames_);
            const bool exchanged = measureStage
                ? transport_->WriteClientOutputMeasured(transportBlock_.data(), frames,
                    static_cast<std::uint64_t>(callbackEnd.QuadPart - callbackStart.QuadPart),
                    static_cast<std::uint64_t>(copyEnd.QuadPart - copyStart.QuadPart), 0)
                : transport_->WriteClientOutput(transportBlock_.data(), frames);
            transport_->PublishClientExchange(exchanged ? static_cast<std::uint32_t>(bufferFrames_) : 0);
            samplePosition_.fetch_add(static_cast<std::uint64_t>(bufferFrames_), std::memory_order_relaxed);
            bufferIndex ^= 1;
        }
    }
    if (transport_) transport_->PublishClientState(AudioClientMapping::ClientState::Stopped);
    if (formatChanged && resetMessage_.load(std::memory_order_acquire)) {
        // A host may close/reopen this COM object synchronously from asioMessage.
        // Keep it alive and send the request off the audio callback worker.
        NonDelegatingAddRef();
        if (!QueueUserWorkItem(&VASIODriver::deliverResetRequest, this, WT_EXECUTEDEFAULT))
            NonDelegatingRelease();
    }
    // The callback thread cannot join itself. A separate worker joins it and
    // releases the reference held for its lifetime, even if the host is gone.
    NonDelegatingAddRef();
    if (!QueueUserWorkItem(&VASIODriver::reapCallbackWorker, this, WT_EXECUTEDEFAULT)) {
        const HANDLE fallback = CreateThread(nullptr, 0, &VASIODriver::reapCallbackWorker,
            this, 0, nullptr);
        if (fallback) CloseHandle(fallback);
        else NonDelegatingRelease();
    }
    currentCallbackDriver = nullptr;
}

DWORD WINAPI VASIODriver::reapCallbackWorker(void* context) noexcept {
    auto* driver = static_cast<VASIODriver*>(context);
    driver->joinCallbackWorker(true);
    driver->NonDelegatingRelease();
    return 0;
}

DWORD WINAPI VASIODriver::deliverResetRequest(void* context) noexcept {
    auto* driver = static_cast<VASIODriver*>(context);
    const auto message = driver->resetMessage_.load(std::memory_order_acquire);
    if (message) {
        if (driver->transport_) driver->transport_->PublishResetRequest();
        message(kAsioResetRequest, 0, nullptr, nullptr);
    }
    driver->NonDelegatingRelease();
    return 0;
}
