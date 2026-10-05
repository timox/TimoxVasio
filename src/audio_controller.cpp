#include "audio_controller.h"

#include "audio_routing_runtime.h"
#include "vasio_client_manager.h"

#include <windows.h>

#include <cmath>
#include <stdexcept>
#include <utility>

namespace {
constexpr wchar_t kHostWindowClass[] = L"VirtualASIO.AudioController.MessageWindow";

bool supportedBufferFrames(const PhysicalAsioCapabilities& capabilities, std::uint32_t frames) {
    if (!frames || capabilities.minBufferFrames < 0 || capabilities.maxBufferFrames < 0 ||
        frames < static_cast<std::uint32_t>(capabilities.minBufferFrames) ||
        frames > static_cast<std::uint32_t>(capabilities.maxBufferFrames)) return false;
    if (capabilities.bufferGranularity > 0)
        return (frames - static_cast<std::uint32_t>(capabilities.minBufferFrames)) %
            static_cast<std::uint32_t>(capabilities.bufferGranularity) == 0;
    if (capabilities.bufferGranularity == -1) return (frames & (frames - 1)) == 0;
    return capabilities.bufferGranularity == 0;
}

void configureVasioClients(VasioClientManager& clients, std::uint32_t bufferFrames,
                           std::uint32_t sampleRate) {
    clients.SetDriverConfiguration(bufferFrames, sampleRate);
}

LRESULT CALLBACK hostWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(window, message, wParam, lParam);
}

HWND createHostWindow() {
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = hostWindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kHostWindowClass;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
    return CreateWindowExW(0, kHostWindowClass, L"", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, instance, nullptr);
}
}

AudioController::AudioController(VasioClientManager& clients, std::wstring configurationPath,
                                 std::wstring applicationProfilesPath)
    : clients_(clients), configurationStore_(std::move(configurationPath)),
      applicationProfileStore_(std::move(applicationProfilesPath)) {}
AudioController::~AudioController() { Stop(); }

bool AudioController::Start() {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) return true;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        accepting_ = true;
        stopping_ = false;
    }
    queueEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    startupEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!queueEvent_ || !startupEvent_) {
        started_.store(false);
        std::lock_guard<std::mutex> lock(queueMutex_);
        accepting_ = false;
        if (queueEvent_) CloseHandle(queueEvent_);
        if (startupEvent_) CloseHandle(startupEvent_);
        queueEvent_ = nullptr;
        startupEvent_ = nullptr;
        return false;
    }
    try {
        worker_ = std::thread(&AudioController::Run, this);
    } catch (...) {
        started_.store(false);
        std::lock_guard<std::mutex> lock(queueMutex_);
        accepting_ = false;
        CloseHandle(queueEvent_);
        CloseHandle(startupEvent_);
        queueEvent_ = nullptr;
        startupEvent_ = nullptr;
        return false;
    }
    const DWORD startupResult = WaitForSingleObject(startupEvent_, INFINITE);
    return startupResult == WAIT_OBJECT_0 && started_.load();
}

void AudioController::Stop() noexcept {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (!worker_.joinable()) return;
        accepting_ = false;
        stopping_ = true;
    }
    if (queueEvent_) SetEvent(queueEvent_);
    if (worker_.get_id() != std::this_thread::get_id()) worker_.join();
    if (queueEvent_) {
        CloseHandle(queueEvent_);
        queueEvent_ = nullptr;
    }
    if (startupEvent_) {
        CloseHandle(startupEvent_);
        startupEvent_ = nullptr;
    }
}

std::vector<PhysicalAsioDriverInfo> AudioController::EnumeratePhysicalDrivers() {
    return Invoke([this] { return physicalHost_->enumerate(); });
}

ApplicationProfilesSnapshot AudioController::GetApplicationProfiles() {
    return Invoke([this] {
        return ApplicationProfilesSnapshot{applicationProfiles_, applicationProfilesError_};
    });
}

ApplicationProfilesUpdateResult AudioController::ReplaceApplicationProfiles(
    std::vector<ApplicationProfile> profiles) {
    try {
        return Invoke([this, profiles = std::move(profiles)]() mutable {
            ApplicationProfilesUpdateResult result;
            if (!ApplicationProfileStore::NormalizeAndValidate(profiles, result.error)) return result;

            const auto previous = applicationProfiles_;
            bool unchanged = previous.size() == profiles.size();
            for (std::size_t index = 0; unchanged && index < profiles.size(); ++index) {
                unchanged = previous[index].processName == profiles[index].processName &&
                    previous[index].inputChannels == profiles[index].inputChannels &&
                    previous[index].outputChannels == profiles[index].outputChannels;
            }
            if ((!unchanged || !applicationProfilesError_.empty()) &&
                !applicationProfileStore_.Save(profiles, result.error)) return result;

            for (const auto& client : clients_.GetClientSnapshots()) {
                const auto oldProfile = ApplicationProfileStore::EffectiveProfileForExecutable(
                    client.info.processName, previous);
                const auto newProfile = ApplicationProfileStore::EffectiveProfileForExecutable(
                    client.info.processName, profiles);
                if (oldProfile.inputChannels != newProfile.inputChannels ||
                    oldProfile.outputChannels != newProfile.outputChannels) {
                    result.restartRequiredClients.push_back({client.info.processId,
                        client.info.processName});
                }
            }
            applicationProfiles_ = std::move(profiles);
            applicationProfilesError_.clear();
            result.profiles = applicationProfiles_;
            result.success = true;
            return result;
        });
    } catch (const std::exception& exception) {
        return {false, exception.what(), {}, {}};
    }
}

AudioControllerSnapshot AudioController::Snapshot() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return state_;
}

std::vector<AudioControllerSnapshot> AudioController::EventsSince(std::uint64_t revision) const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    std::vector<AudioControllerSnapshot> result;
    for (const auto& event : events_) if (event.revision > revision) result.push_back(event);
    return result;
}

std::vector<AudioMeterSnapshot> AudioController::ReadMeters() {
    return Invoke([this] {
        return runtime_ ? runtime_->ReadMeters() : std::vector<AudioMeterSnapshot>{};
    });
}

AudioControllerResult AudioController::ApplyConfiguration(
    const AudioControllerConfiguration& configuration) {
    try {
        return Invoke([this, configuration] { return ApplyOnWorker(configuration); });
    } catch (const std::exception& exception) {
        return {false, exception.what()};
    }
}

AudioControllerResult AudioController::StartAudio() {
    try {
        return Invoke([this] {
            if (!audioStoppedByCommand_ && Snapshot().state == "running")
                return AudioControllerResult{true, {}};
            const auto result = ApplyConfigurationCore(desiredConfiguration_);
            if (result.success) audioStoppedByCommand_ = false;
            return result;
        });
    } catch (const std::exception& exception) {
        return {false, exception.what()};
    }
}

AudioControllerResult AudioController::StopAudio() {
    try {
        return Invoke([this] {
            if (audioStoppedByCommand_) return AudioControllerResult{true, {}};
            auto status = Snapshot();
            status.state = "reconfiguring";
            SetStatus(status);
            if (runtime_) runtime_->StopCorrelation();
            if (physicalHost_) physicalHost_->stop();
            runtime_.reset();
            if (physicalHost_) physicalHost_->close();
            status.state = "stopped";
            status.lastError.clear();
            SetStatus(status);
            audioStoppedByCommand_ = true;
            return AudioControllerResult{true, {}};
        });
    } catch (const std::exception& exception) {
        return {false, exception.what()};
    }
}

AudioControllerResult AudioController::StartCorrelation(const std::string& leftEndpointId,
                                                          const std::string& rightEndpointId) {
    try {
        return Invoke([this, leftEndpointId, rightEndpointId] {
            if (Snapshot().state != "running" || !runtime_)
                return AudioControllerResult{false, "Audio engine is not running"};
            if (!runtime_->SetCorrelationPair(leftEndpointId, rightEndpointId))
                return AudioControllerResult{false, "Stereo pair is not an active routed client output"};
            return AudioControllerResult{true, {}};
        });
    } catch (const std::exception& exception) {
        return {false, exception.what()};
    }
}

void AudioController::StopCorrelation() {
    try {
        Invoke([this] { if (runtime_) runtime_->StopCorrelation(); });
    } catch (...) {}
}

AudioCorrelationSnapshot AudioController::ReadCorrelation() {
    try {
        return Invoke([this] {
            return runtime_ ? runtime_->ReadCorrelation() : AudioCorrelationSnapshot{};
        });
    } catch (...) {
        return {};
    }
}

void AudioController::SetStatus(const AudioControllerSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    const auto revision = state_.revision + 1;
    state_ = snapshot;
    state_.revision = revision;
    events_.push_back(state_);
    if (events_.size() > 64) events_.pop_front();
}

void AudioController::Run() noexcept {
    try {
        hostWindow_ = createHostWindow();
        if (!hostWindow_) throw std::runtime_error("Unable to create ASIO message window");
        physicalHost_ = std::make_unique<PhysicalAsioHost>();
        RestoreApplicationProfilesOnWorker();
        RestoreConfigurationOnWorker();
        if (startupEvent_) SetEvent(startupEvent_);
        for (;;) {
            const DWORD waitResult = MsgWaitForMultipleObjectsEx(1, &queueEvent_, 20,
                QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (waitResult == WAIT_FAILED) throw std::runtime_error("Audio controller message wait failed");
            if (waitResult == WAIT_OBJECT_0) {
                for (;;) {
                    std::function<void()> task;
                    {
                        std::lock_guard<std::mutex> lock(queueMutex_);
                        if (queue_.empty()) break;
                        task = std::move(queue_.front());
                        queue_.pop_front();
                    }
                    task();
                }
            }
            if (waitResult == WAIT_OBJECT_0 + 1) {
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
            if (physicalHost_ && physicalHost_->consumeSampleRateChange()) {
                const auto previous = Snapshot();
                physicalHost_->stop();
                runtime_.reset();
                physicalHost_->close();
                AudioControllerSnapshot failed = previous;
                failed.state = "error";
                failed.physicalCapabilities = {};
                failed.lastError = "Physical ASIO driver reported a sample-rate change; reapply the configuration to resynchronize TimoxVasio";
                SetStatus(failed);
            }
            bool shouldStop = false;
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                shouldStop = stopping_ && queue_.empty();
            }
            if (shouldStop) break;
        }
        physicalHost_->stop();
        runtime_.reset();
        physicalHost_->close();
        physicalHost_.reset();
        if (hostWindow_) DestroyWindow(hostWindow_);
        hostWindow_ = nullptr;
    } catch (...) {
        if (physicalHost_) physicalHost_->stop();
        runtime_.reset();
        if (physicalHost_) physicalHost_->close();
        physicalHost_.reset();
        if (hostWindow_) DestroyWindow(hostWindow_);
        hostWindow_ = nullptr;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            accepting_ = false;
            stopping_ = true;
        }
        AudioControllerSnapshot failed;
        failed.state = "error";
        failed.lastError = "Audio controller worker failed";
        SetStatus(failed);
        if (startupEvent_) SetEvent(startupEvent_);
    }
    started_.store(false);
}

void AudioController::RestoreApplicationProfilesOnWorker() {
    std::vector<ApplicationProfile> storedProfiles;
    std::string error;
    if (!applicationProfileStore_.Load(storedProfiles, error)) {
        applicationProfiles_ = ApplicationProfileStore::DefaultProfiles();
        applicationProfilesError_ = std::move(error);
    } else {
        applicationProfiles_ = std::move(storedProfiles);
        applicationProfilesError_.clear();
    }
}

AudioControllerResult AudioController::ApplyOnWorker(
    const AudioControllerConfiguration& configuration, bool persist) {
    try {
        auto applied = ApplyConfigurationCore(configuration);
        if (!applied.success || !persist) return applied;
        std::string error;
        if (!configurationStore_.Save(configuration, error)) {
            physicalHost_->stop();
            runtime_.reset();
            physicalHost_->close();
            auto failed = Snapshot();
            failed.state = "error";
            failed.physicalCapabilities = {};
            failed.lastError = error;
            SetStatus(failed);
            return {false, failed.lastError};
        }
        desiredConfiguration_ = configuration;
        audioStoppedByCommand_ = false;
        return applied;
    } catch (const std::exception& exception) {
        if (physicalHost_) physicalHost_->stop();
        runtime_.reset();
        if (physicalHost_) physicalHost_->close();
        AudioControllerSnapshot failed;
        failed.state = "error";
        failed.lastError = exception.what();
        SetStatus(failed);
        return {false, failed.lastError};
    } catch (...) {
        if (physicalHost_) physicalHost_->stop();
        runtime_.reset();
        if (physicalHost_) physicalHost_->close();
        AudioControllerSnapshot failed;
        failed.state = "error";
        failed.lastError = "Unknown audio controller failure";
        SetStatus(failed);
        return {false, failed.lastError};
    }
}

void AudioController::RestoreConfigurationOnWorker() {
    AudioControllerConfiguration configuration;
    std::string error;
    const auto status = configurationStore_.Load(configuration, error);
    if (status == ConfigurationLoadStatus::Missing) return;
    if (status == ConfigurationLoadStatus::Error) {
        AudioControllerSnapshot failed;
        failed.state = "stopped";
        failed.lastError = std::move(error);
        SetStatus(failed);
        return;
    }
    desiredConfiguration_ = configuration;
    const auto restored = ApplyConfigurationCore(configuration);
    if (!restored.success) {
        physicalHost_->stop();
        runtime_.reset();
        physicalHost_->close();
        auto failed = Snapshot();
        failed.state = "stopped";
        failed.physicalCapabilities = {};
        failed.lastError = restored.error;
        SetStatus(failed);
    }
}

AudioControllerResult AudioController::ApplyConfigurationCore(
    const AudioControllerConfiguration& configuration) {
    AudioControllerSnapshot next;
    next.state = "reconfiguring";
    SetStatus(next);
    physicalHost_->stop();
    runtime_.reset();
    physicalHost_->close();
    if (!configuration.physicalDriverId) {
        if (!configuration.routes.empty() || configuration.sampleRate || configuration.bufferFrames) {
            next.state = "error";
            next.lastError = "A physical ASIO clock is required for sample-rate, buffer and routing configuration";
            SetStatus(next);
            return {false, next.lastError};
        }
        next.state = "stopped";
        SetStatus(next);
        return {true, {}};
    }

    const std::wstring driverId(configuration.physicalDriverId->begin(),
                                configuration.physicalDriverId->end());
    std::string error;
    PhysicalAsioCapabilities capabilities;
    if (!physicalHost_->open(driverId, hostWindow_, capabilities, error)) {
        next.state = "error";
        next.physicalDriverId = configuration.physicalDriverId;
        next.lastError = std::move(error);
        SetStatus(next);
        return {false, next.lastError};
    }

    const double requestedRate = configuration.sampleRate.value_or(
        static_cast<std::uint32_t>(capabilities.currentSampleRate));
    if (!std::isfinite(requestedRate) || requestedRate < 1.0 || std::floor(requestedRate) != requestedRate ||
        requestedRate > UINT32_MAX || !physicalHost_->setSampleRate(requestedRate, error) ||
        !physicalHost_->refreshCapabilities(capabilities, error) ||
        capabilities.currentSampleRate != requestedRate) {
        physicalHost_->close();
        next.state = "error";
        next.physicalDriverId = configuration.physicalDriverId;
        next.lastError = error.empty() ? "Physical driver did not confirm the requested sample rate" : std::move(error);
        SetStatus(next);
        return {false, next.lastError};
    }
    const auto sampleRate = static_cast<std::uint32_t>(requestedRate);
    const auto physicalBufferFrames = configuration.bufferFrames.value_or(
        capabilities.preferredBufferFrames > 0 ? static_cast<std::uint32_t>(capabilities.preferredBufferFrames) : 0u);
    if (!supportedBufferFrames(capabilities, physicalBufferFrames)) {
        physicalHost_->close();
        next.state = "error";
        next.physicalDriverId = configuration.physicalDriverId;
        next.lastError = "Requested buffer size is not supported by the selected physical driver at this rate";
        SetStatus(next);
        return {false, next.lastError};
    }

    configureVasioClients(clients_, physicalBufferFrames, sampleRate);
    AudioRoutingLayout layout;
    layout.physicalDriverId = *configuration.physicalDriverId;
    layout.sampleRate = sampleRate;
    layout.bufferFrames = physicalBufferFrames;
    layout.physicalInputChannels = capabilities.inputChannels;
    layout.physicalOutputChannels = capabilities.outputChannels;
    layout.clients = clients_.GetClientSnapshots();
    layout.routes = configuration.routes;
    runtime_ = AudioRoutingRuntime::Create(std::move(layout), error);
    if (!runtime_) {
        physicalHost_->close();
        next.state = "error";
        next.physicalDriverId = configuration.physicalDriverId;
        next.lastError = std::move(error);
        SetStatus(next);
        return {false, next.lastError};
    }

    // Keep the physical driver configured while the user discovers endpoints.
    // Starting a callback with no routed channels serves no audio purpose and
    // some ASIO drivers do not tolerate the hidden clock-only buffer path.
    if (configuration.routes.empty()) {
        next.state = "stopped";
        next.physicalDriverId = configuration.physicalDriverId;
        next.sampleRate = sampleRate;
        next.bufferFrames = physicalBufferFrames;
        next.routes = configuration.routes;
        next.physicalCapabilities = std::move(capabilities);
        SetStatus(next);
        return {true, {}};
    }

    if (!physicalHost_->start(physicalBufferFrames, runtime_->RoutedPhysicalInputChannels(),
                              runtime_->RoutedPhysicalOutputChannels(),
                              &AudioRoutingRuntime::PhysicalCallback, runtime_.get(), error)) {
        runtime_.reset();
        physicalHost_->close();
        next.state = "error";
        next.physicalDriverId = configuration.physicalDriverId;
        next.sampleRate = sampleRate;
        next.bufferFrames = physicalBufferFrames;
        next.lastError = std::move(error);
        SetStatus(next);
        return {false, next.lastError};
    }
    next.state = "running";
    next.physicalDriverId = configuration.physicalDriverId;
    next.sampleRate = sampleRate;
    next.bufferFrames = physicalBufferFrames;
    next.routes = configuration.routes;
    next.physicalCapabilities = std::move(capabilities);
    SetStatus(next);
    return {true, {}};
}
