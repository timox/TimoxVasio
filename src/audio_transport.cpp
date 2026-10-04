#include "audio_transport.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>

namespace {
constexpr std::uint32_t kMagic = 0x56534154; // VSAT
constexpr std::uint32_t kVersion = 6;
constexpr std::uint64_t kCapacity = AudioClientMapping::kRingCapacityFrames;
constexpr std::size_t kSamples = kCapacity * AudioClientMapping::kChannelCount;

std::wstring objectName(const wchar_t* kind, std::uint32_t pid, std::uint32_t driver) {
    return L"Local\\VASIO-" + std::wstring(kind) + L"-" + std::to_wstring(pid) + L"-" + std::to_wstring(driver);
}
}

struct alignas(64) AudioClientMapping::Shared {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t driverId;
    std::uint32_t processId;
    volatile LONG sampleRate;
    std::uint32_t channelCount;
    std::uint32_t capacityFrames;
    volatile LONG blockFrames;
    volatile LONG preferredBufferFrames;
    alignas(8) volatile LONG64 generation;
    alignas(8) volatile LONG64 activeChannelsGeneration;
    volatile LONG64 activeInputs[AudioClientMapping::kChannelMaskWords];
    volatile LONG64 activeOutputs[AudioClientMapping::kChannelMaskWords];
    volatile LONG64 clientWriteFrame;
    volatile LONG64 engineReadFrame;
    volatile LONG64 engineWriteFrame;
    volatile LONG64 clientReadFrame;
    volatile LONG64 inputUnderruns;
    volatile LONG64 outputOverruns;
    volatile LONG64 inputOverruns;
    volatile LONG64 outputUnderruns;
    volatile LONG64 engineAttached;
    volatile LONG engineProcessId;
    alignas(64) float clientToEngine[kSamples];
    alignas(64) float engineToClient[kSamples];
};

AudioClientMapping::AudioClientMapping(HANDLE mapping, HANDLE eventHandle, Shared* shared, bool engineOwner) noexcept
    : mapping_(mapping), event_(eventHandle), shared_(shared), engineOwner_(engineOwner) {}

AudioClientMapping::~AudioClientMapping() {
    if (ownsEngineAttachment_ && EngineAttached()) {
        SetEngineAttached(false);
        SignalClient();
    }
    if (shared_) UnmapViewOfFile(shared_);
    if (event_) CloseHandle(event_);
    if (mapping_) CloseHandle(mapping_);
}

std::unique_ptr<AudioClientMapping> AudioClientMapping::CreateClient(std::uint32_t driver,
    std::uint32_t pid, std::uint32_t rate, std::uint32_t block) {
    return Open(true, driver, pid, rate, block);
}

std::unique_ptr<AudioClientMapping> AudioClientMapping::OpenEngine(std::uint32_t driver,
    std::uint32_t pid, std::uint32_t rate) {
    return Open(false, driver, pid, rate, 0);
}

std::unique_ptr<AudioClientMapping> AudioClientMapping::Open(bool create, std::uint32_t driver,
    std::uint32_t pid, std::uint32_t rate, std::uint32_t block) {
    if (!pid || (create && (!rate || !block || block > kRingCapacityFrames))) return {};
    const auto mapName = objectName(L"Map", pid, driver);
    const auto eventName = objectName(L"Event", pid, driver);
    HANDLE map = create
        ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared), mapName.c_str())
        : OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, mapName.c_str());
    if (!map) return {};
    const DWORD mapError = GetLastError();
    const bool reusingClientMapping = create && mapError == ERROR_ALREADY_EXISTS;
    HANDLE eventHandle = create ? CreateEventW(nullptr, FALSE, FALSE, eventName.c_str())
                                : OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, eventName.c_str());
    if (!eventHandle) { CloseHandle(map); return {}; }
    void* view = MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared));
    if (!view) { CloseHandle(eventHandle); CloseHandle(map); return {}; }
    auto* shared = static_cast<Shared*>(view);
    if (create && !reusingClientMapping) {
        std::memset(shared, 0, sizeof(Shared));
        shared->magic = kMagic;
        shared->version = kVersion;
        shared->driverId = driver;
        shared->processId = pid;
        InterlockedExchange(&shared->sampleRate, static_cast<LONG>(rate));
        shared->channelCount = kChannelCount;
        shared->capacityFrames = kRingCapacityFrames;
        InterlockedExchange(&shared->blockFrames, static_cast<LONG>(block));
        InterlockedExchange(&shared->preferredBufferFrames, static_cast<LONG>(block));
        InterlockedExchange64(&shared->generation, 1);
    } else if (shared->magic != kMagic || shared->version != kVersion ||
        shared->driverId != driver || shared->processId != pid ||
        (rate && !reusingClientMapping &&
            static_cast<std::uint32_t>(InterlockedCompareExchange(&shared->sampleRate, 0, 0)) != rate) ||
        shared->channelCount != kChannelCount || shared->capacityFrames != kRingCapacityFrames) {
        UnmapViewOfFile(view); CloseHandle(eventHandle); CloseHandle(map); return {};
    }
    return std::unique_ptr<AudioClientMapping>(new AudioClientMapping(map, eventHandle, shared, !create));
}

bool AudioClientMapping::SetEngineAttached(bool attached) noexcept {
    ownsEngineAttachment_ = attached;
    const LONG64 state = attached ? 1 : 0;
    InterlockedExchange(&shared_->engineProcessId, attached ? static_cast<LONG>(GetCurrentProcessId()) : 0);
    if (InterlockedExchange64(&shared_->engineAttached, state) != state)
        InterlockedIncrement64(&shared_->generation);
    return true;
}

bool AudioClientMapping::EngineAttached() const noexcept {
    return InterlockedCompareExchange64(const_cast<volatile LONG64*>(&shared_->engineAttached), 0, 0) != 0;
}

bool AudioClientMapping::EngineProcessAlive() const noexcept {
    if (!EngineAttached()) return false;
    const DWORD pid = static_cast<DWORD>(InterlockedCompareExchange(
        const_cast<volatile LONG*>(&shared_->engineProcessId), 0, 0));
    if (!pid) return false;
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process) return false;
    const bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return alive;
}

std::uint32_t AudioClientMapping::SampleRate() const noexcept {
    return static_cast<std::uint32_t>(InterlockedCompareExchange(
        const_cast<volatile LONG*>(&shared_->sampleRate), 0, 0));
}

std::uint32_t AudioClientMapping::BlockFrames() const noexcept {
    return static_cast<std::uint32_t>(InterlockedCompareExchange(
        const_cast<volatile LONG*>(&shared_->blockFrames), 0, 0));
}

std::uint32_t AudioClientMapping::PreferredBufferFrames() const noexcept {
    return static_cast<std::uint32_t>(InterlockedCompareExchange(
        const_cast<volatile LONG*>(&shared_->preferredBufferFrames), 0, 0));
}

bool AudioClientMapping::SetSampleRate(std::uint32_t rate) noexcept {
    if (!engineOwner_ || !rate) return false;
    if (InterlockedExchange(&shared_->sampleRate, static_cast<LONG>(rate)) != static_cast<LONG>(rate))
        InterlockedIncrement64(&shared_->generation);
    return true;
}

bool AudioClientMapping::SetBlockFrames(std::uint32_t frames) noexcept {
    if (!engineOwner_ || !frames || frames > kRingCapacityFrames) return false;
    if (InterlockedExchange(&shared_->blockFrames, static_cast<LONG>(frames)) != static_cast<LONG>(frames))
        InterlockedIncrement64(&shared_->generation);
    return true;
}

bool AudioClientMapping::SetPreferredBufferFrames(std::uint32_t frames) noexcept {
    if (!engineOwner_ || frames < 64 || frames > 8192 || (frames & (frames - 1)) != 0)
        return false;
    if (InterlockedExchange(&shared_->preferredBufferFrames, static_cast<LONG>(frames)) != static_cast<LONG>(frames))
        InterlockedIncrement64(&shared_->generation);
    return true;
}

bool AudioClientMapping::SetActiveChannels(const ChannelMask& inputs, const ChannelMask& outputs) noexcept {
    InterlockedIncrement64(&shared_->activeChannelsGeneration);
    for (std::size_t word = 0; word < kChannelMaskWords; ++word) {
        InterlockedExchange64(&shared_->activeInputs[word], static_cast<LONG64>(inputs[word]));
        InterlockedExchange64(&shared_->activeOutputs[word], static_cast<LONG64>(outputs[word]));
    }
    InterlockedIncrement64(&shared_->activeChannelsGeneration);
    InterlockedIncrement64(&shared_->generation);
    return true;
}

bool AudioClientMapping::GetActiveChannels(ChannelMask& inputs, ChannelMask& outputs) const noexcept {
    for (int attempt = 0; attempt < 16; ++attempt) {
        const auto before = InterlockedCompareExchange64(
            const_cast<volatile LONG64*>(&shared_->activeChannelsGeneration), 0, 0);
        if (before & 1) continue;
        ChannelMask inputSnapshot{};
        ChannelMask outputSnapshot{};
        for (std::size_t word = 0; word < kChannelMaskWords; ++word) {
            inputSnapshot[word] = static_cast<std::uint64_t>(InterlockedCompareExchange64(
                const_cast<volatile LONG64*>(&shared_->activeInputs[word]), 0, 0));
            outputSnapshot[word] = static_cast<std::uint64_t>(InterlockedCompareExchange64(
                const_cast<volatile LONG64*>(&shared_->activeOutputs[word]), 0, 0));
        }
        const auto after = InterlockedCompareExchange64(
            const_cast<volatile LONG64*>(&shared_->activeChannelsGeneration), 0, 0);
        if (before == after && !(after & 1)) {
            inputs = inputSnapshot;
            outputs = outputSnapshot;
            return true;
        }
    }
    return false;
}

bool AudioClientMapping::WaitForClientSignal(DWORD timeoutMs) noexcept {
    return WaitForSingleObject(event_, timeoutMs) == WAIT_OBJECT_0;
}

std::uint32_t AudioClientMapping::Available(volatile LONG64* write, volatile LONG64* read) const noexcept {
    const auto w = static_cast<std::uint64_t>(InterlockedCompareExchange64(write, 0, 0));
    const auto r = static_cast<std::uint64_t>(InterlockedCompareExchange64(read, 0, 0));
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(w - r, kCapacity));
}

std::uint32_t AudioClientMapping::ClientOutputAvailable() const noexcept {
    return Available(&shared_->clientWriteFrame, &shared_->engineReadFrame);
}
std::uint32_t AudioClientMapping::ClientInputAvailable() const noexcept {
    return Available(&shared_->engineWriteFrame, &shared_->clientReadFrame);
}

bool AudioClientMapping::Write(const float* src, std::uint32_t frames, volatile LONG64* write,
    volatile LONG64* read, float* ring, volatile LONG64* overruns) noexcept {
    if (!src || frames > kCapacity) return false;
    const auto w = static_cast<std::uint64_t>(InterlockedCompareExchange64(write, 0, 0));
    const auto r = static_cast<std::uint64_t>(InterlockedCompareExchange64(read, 0, 0));
    if (w - r + frames > kCapacity) { InterlockedIncrement64(overruns); return false; }
    for (std::uint32_t f = 0; f < frames; ++f) {
        const auto slot = ((w + f) % kCapacity) * kChannelCount;
        std::memcpy(ring + slot, src + static_cast<std::size_t>(f) * kChannelCount, kChannelCount * sizeof(float));
    }
    InterlockedExchange64(write, static_cast<LONG64>(w + frames));
    return true;
}

std::uint32_t AudioClientMapping::Read(float* dst, std::uint32_t frames, volatile LONG64* read,
    volatile LONG64* write, const float* ring, volatile LONG64* underruns) noexcept {
    if (!dst) return 0;
    const auto r = static_cast<std::uint64_t>(InterlockedCompareExchange64(read, 0, 0));
    const auto w = static_cast<std::uint64_t>(InterlockedCompareExchange64(write, 0, 0));
    const auto count = static_cast<std::uint32_t>(std::min<std::uint64_t>(frames, w - r));
    for (std::uint32_t f = 0; f < count; ++f) {
        const auto slot = ((r + f) % kCapacity) * kChannelCount;
        std::memcpy(dst + static_cast<std::size_t>(f) * kChannelCount, ring + slot, kChannelCount * sizeof(float));
    }
    if (count < frames) {
        std::fill(dst + static_cast<std::size_t>(count) * kChannelCount,
                  dst + static_cast<std::size_t>(frames) * kChannelCount, 0.0f);
        if (underruns) InterlockedIncrement64(underruns);
    }
    InterlockedExchange64(read, static_cast<LONG64>(r + count));
    return count;
}

bool AudioClientMapping::WriteClientOutput(const float* data, std::uint32_t frames) noexcept {
    return Write(data, frames, &shared_->clientWriteFrame, &shared_->engineReadFrame,
        shared_->clientToEngine, &shared_->outputOverruns);
}
std::uint32_t AudioClientMapping::ReadClientOutput(float* data, std::uint32_t frames) noexcept {
    return Read(data, frames, &shared_->engineReadFrame, &shared_->clientWriteFrame,
        shared_->clientToEngine, &shared_->outputUnderruns);
}
bool AudioClientMapping::WriteEngineInput(const float* data, std::uint32_t frames) noexcept {
    return Write(data, frames, &shared_->engineWriteFrame, &shared_->clientReadFrame,
        shared_->engineToClient, &shared_->inputOverruns);
}
std::uint32_t AudioClientMapping::ReadClientInput(float* data, std::uint32_t frames) noexcept {
    return Read(data, frames, &shared_->clientReadFrame, &shared_->engineWriteFrame,
        shared_->engineToClient, &shared_->inputUnderruns);
}
bool AudioClientMapping::SignalClient() noexcept { return SetEvent(event_) != FALSE; }
std::uint64_t AudioClientMapping::ClientOutputOverruns() const noexcept {
    return static_cast<std::uint64_t>(InterlockedCompareExchange64(&shared_->outputOverruns, 0, 0));
}
std::uint64_t AudioClientMapping::ClientInputUnderruns() const noexcept {
    return static_cast<std::uint64_t>(InterlockedCompareExchange64(&shared_->inputUnderruns, 0, 0));
}
std::uint64_t AudioClientMapping::ClientInputOverruns() const noexcept {
    return static_cast<std::uint64_t>(InterlockedCompareExchange64(&shared_->inputOverruns, 0, 0));
}
std::uint64_t AudioClientMapping::ClientOutputUnderruns() const noexcept {
    return static_cast<std::uint64_t>(InterlockedCompareExchange64(&shared_->outputUnderruns, 0, 0));
}
