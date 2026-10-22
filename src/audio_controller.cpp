#include "audio_controller.h"

#include "audio_routing_runtime.h"
#include "physical_asio_host.h"
#include "software_clock_host.h"
#include "persistent_routes.h"
#include "vasio_client_manager.h"

#include <windows.h>

#include <cmath>
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace {
constexpr wchar_t kHostWindowClass[] = L"VirtualASIO.AudioController.MessageWindow";

template<class Counters>
std::optional<PerformanceDuration> durationFromTicks(const Counters& value,
    std::uint64_t frequency) noexcept {
    if (!value.samples || !frequency) return std::nullopt;
    const auto nanos = [frequency](std::uint64_t ticks) {
        return static_cast<std::uint64_t>(
            static_cast<long double>(ticks) * 1000000000.0L / frequency);
    };
    return PerformanceDuration{value.samples, static_cast<std::uint64_t>(
        static_cast<long double>(value.sumTicks) * 1000000000.0L /
        frequency / value.samples),
        nanos(value.worstTicks)};
}

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

bool sameRoutes(const std::vector<AudioRoute>& left, const std::vector<AudioRoute>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].id != right[index].id ||
            left[index].sourceEndpointId != right[index].sourceEndpointId ||
            left[index].destinationEndpointId != right[index].destinationEndpointId ||
            left[index].gainDb != right[index].gainDb || left[index].mute != right[index].mute)
            return false;
    }
    return true;
}

bool hasAttachedClient(const std::vector<VasioClientSnapshot>& clients) noexcept {
    return std::any_of(clients.begin(), clients.end(), [](const VasioClientSnapshot& client) {
        return client.info.driverId == 1 && client.mapping && client.mapping->EngineAttached();
    });
}

void acknowledgePublishedGenerations(const std::vector<VasioClientSnapshot>& clients,
    std::uint32_t sampleRate, std::uint32_t bufferFrames) noexcept {
    for (const auto& client : clients) {
        if (client.mapping && client.mapping->AudioEpoch())
            client.mapping->AckAudioGeneration(sampleRate, bufferFrames);
    }
}
}

AudioController::AudioController(VasioClientManager& clients, std::wstring configurationPath,
                                 std::wstring applicationProfilesPath,
                                 ClockHostFactory clockHostFactory,
                                 WasapiBridge::BackendFactory wasapiBackendFactory,
                                 WasapiBridgeManager::EndpointResolver wasapiEndpointResolver)
    : clients_(clients), configurationStore_(std::move(configurationPath)),
      applicationProfileStore_(std::move(applicationProfilesPath)),
      clockHostFactory_(std::move(clockHostFactory)),
      callbackSlot_(&AudioRoutingRuntime::PhysicalCallback),
      wasapiBackendFactory_(std::move(wasapiBackendFactory)),
      wasapiEndpointResolver_(std::move(wasapiEndpointResolver)) {}
AudioController::~AudioController() { Stop(); }

void AudioController::StopPhysicalRuntime() noexcept {
    ResetPerformanceMeasurementsWindow();
    callbackSlot_.Publish(nullptr);
    if (physicalHost_) physicalHost_->stop();
    callbackSlot_.WaitForQuiescence();
    runtime_.reset();
    wasapiManager_.reset(); // Graph retired first; session destruction joins workers off RT.
    hostPhysicalInputs_.clear();
    hostPhysicalOutputs_.clear();
    if (physicalHost_) physicalHost_->close();
}

bool AudioController::TryHotSwapRoutes(const std::vector<AudioRoute>& routes,
    const std::vector<VasioClientSnapshot>& clients,
    const std::shared_ptr<WasapiBridgeManager>& requestedManager) {
    const auto current = Snapshot();
    const auto manager=requestedManager?requestedManager:wasapiManager_;
    const auto bindings=manager?manager->Bindings():std::vector<WasapiRoutingBinding>{};
    const auto activeRoutes = PersistentRoutes::ResolveForClock(routes, clients, current.physicalDriverId,bindings);
    if (current.state != "running" || !runtime_ || !physicalHost_ ||
        !current.sampleRate || !current.bufferFrames ||
        current.clockSource != desiredConfiguration_.clockSource ||
        current.physicalDriverId != desiredConfiguration_.physicalDriverId ||
        (desiredConfiguration_.sampleRate && current.sampleRate != desiredConfiguration_.sampleRate) ||
        (desiredConfiguration_.bufferFrames && current.bufferFrames != desiredConfiguration_.bufferFrames))
        return false;

    AudioRoutingLayout layout;
    layout.physicalDriverId = current.physicalDriverId.value_or("");
    layout.sampleRate = *current.sampleRate;
    layout.bufferFrames = *current.bufferFrames;
    layout.physicalInputChannels = current.physicalCapabilities.inputChannels;
    layout.physicalOutputChannels = current.physicalCapabilities.outputChannels;
    layout.clients = clients;
    layout.routes = activeRoutes;
    layout.wasapiBindings=bindings;
    std::string error;
    auto replacement = AudioRoutingRuntime::Create(std::move(layout), error);
    if (!replacement ||
        !std::includes(hostPhysicalInputs_.begin(), hostPhysicalInputs_.end(),
            replacement->RoutedPhysicalInputChannels().begin(),
            replacement->RoutedPhysicalInputChannels().end()) ||
        !std::includes(hostPhysicalOutputs_.begin(), hostPhysicalOutputs_.end(),
            replacement->RoutedPhysicalOutputChannels().begin(),
            replacement->RoutedPhysicalOutputChannels().end()))
        return false;

    ResetPerformanceMeasurementsWindow();
    const bool measurementsEnabled = ReadPerformanceMeasurements().enabled;
    replacement->SetStageMeasurementsEnabled(measurementsEnabled);
    for (const auto& client : clients)
        if (client.mapping) client.mapping->SetStageMeasurementsEnabled(measurementsEnabled);

    auto retired = std::move(runtime_);
    runtime_ = std::move(replacement);
    callbackSlot_.Publish(runtime_.get());
    callbackSlot_.WaitForQuiescence();
    retired.reset();
    wasapiManager_=manager;
    acknowledgePublishedGenerations(clients, *current.sampleRate, *current.bufferFrames);
    auto updated = current;
    updated.routes = activeRoutes;
    updated.configuredRoutes = desiredConfiguration_.routes;
    if(updated.lastError.find("WASAPI reconnection rejected")==0)updated.lastError.clear();
    SetStatus(updated);
    return true;
}

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
    return Invoke([this] {
        if (hostClockSource_ == AudioClockSource::Asio) return physicalHost_->enumerate();
        auto catalog = clockHostFactory_ ? clockHostFactory_() : std::make_unique<PhysicalAsioHost>();
        return catalog ? catalog->enumerate() : std::vector<PhysicalAsioDriverInfo>{};
    });
}

std::vector<WasapiDeviceInfo> AudioController::EnumerateWasapiDevices() {
    return Invoke([this] {
        const auto apartment=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        wasapiDevices_.clear();wasapiInventoryError_.clear();
        if(SUCCEEDED(apartment) || apartment==RPC_E_CHANGED_MODE) {
            try { WasapiDeviceEnumerator{}.enumerate(wasapiDevices_,wasapiInventoryError_); }
            catch(...) {
                if(SUCCEEDED(apartment))CoUninitialize();
                throw;
            }
            if(SUCCEEDED(apartment))CoUninitialize();
        } else wasapiInventoryError_="Cannot initialize COM for WASAPI inventory";
        SetStatus(Snapshot());
        return wasapiDevices_;
    });
}

void AudioController::ReconcileWasapiOnWorker() {
    {std::lock_guard<std::mutex> lock(queueMutex_);if(stopping_)return;}
    if(!wasapiManager_ || Snapshot().state!="running")return;
    try {
        auto candidate=wasapiManager_->Reconcile(WasapiBridgeManager::Clock::now());
        if(candidate && !TryHotSwapRoutes(desiredConfiguration_.routes,clients_.GetClientSnapshots(),candidate)) {
            wasapiManager_->DeferPublication(WasapiBridgeManager::Clock::now());
            auto status=Snapshot();
            status.lastError="WASAPI reconnection rejected by graph constraints; retry deferred";
            SetStatus(status);
        }
    } catch(...) {
        // A slave preparation/allocation failure must not terminate the master.
        // The existing manager retains the failed session for the next retry.
    }
}

std::optional<AudioControllerResult> AudioController::ApplyPreparedLive(
    const AudioControllerConfiguration& configuration,const AudioControllerConfiguration* durable) {
    const auto current=Snapshot();
    if(current.state!="running" || !runtime_ || !physicalHost_ || !current.sampleRate || !current.bufferFrames ||
       current.clockSource!=configuration.clockSource || current.physicalDriverId!=configuration.physicalDriverId ||
       (configuration.sampleRate && configuration.sampleRate!=current.sampleRate) ||
       (configuration.bufferFrames && configuration.bufferFrames!=current.bufferFrames))return std::nullopt;
    std::string error;
    auto candidate=WasapiBridgeManager::Prepare(configuration.wasapiBridges,*current.sampleRate,
        *current.bufferFrames,wasapiManager_,wasapiBackendFactory_,error,false,wasapiEndpointResolver_);
    if(!candidate)return AudioControllerResult{false,error};
    auto clients=clients_.GetClientSnapshots();
    auto bindings=candidate->Bindings();
    const auto routes=PersistentRoutes::ResolveForClock(configuration.routes,clients,current.physicalDriverId,bindings);
    AudioRoutingLayout layout;
    layout.physicalDriverId=current.physicalDriverId.value_or("");
    layout.sampleRate=*current.sampleRate;layout.bufferFrames=*current.bufferFrames;
    layout.physicalInputChannels=current.physicalCapabilities.inputChannels;
    layout.physicalOutputChannels=current.physicalCapabilities.outputChannels;
    layout.clients=clients;layout.routes=routes;layout.wasapiBindings=std::move(bindings);
    auto replacement=AudioRoutingRuntime::Create(std::move(layout),error);
    if(!replacement)return AudioControllerResult{false,error};
    if(!std::includes(hostPhysicalInputs_.begin(),hostPhysicalInputs_.end(),
          replacement->RoutedPhysicalInputChannels().begin(),replacement->RoutedPhysicalInputChannels().end()) ||
       !std::includes(hostPhysicalOutputs_.begin(),hostPhysicalOutputs_.end(),
          replacement->RoutedPhysicalOutputChannels().begin(),replacement->RoutedPhysicalOutputChannels().end()))
        return std::nullopt;
    // All fallible preparation precedes persistence and callback publication.
    auto updated=current;updated.routes=routes;updated.configuredRoutes=configuration.routes;
    updated.configuredWasapiBridges=configuration.wasapiBridges;
    updated.configuredNativeAudio=configuration.nativeAudio;
    replacement->SetStageMeasurementsEnabled(ReadPerformanceMeasurements().enabled);
    if(durable && !configurationStore_.Save(*durable,error))return AudioControllerResult{false,error};
    ResetPerformanceMeasurementsWindow();
    auto retired=std::move(runtime_);runtime_=std::move(replacement);
    callbackSlot_.Publish(runtime_.get());callbackSlot_.WaitForQuiescence();retired.reset();
    wasapiManager_=std::move(candidate);
    acknowledgePublishedGenerations(clients,*current.sampleRate,*current.bufferFrames);
    SetStatus(updated);
    return AudioControllerResult{true,{}};
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

AudioBufferHealthSnapshot AudioController::ReadBufferHealth() const {
    std::lock_guard<std::mutex> lock(bufferHealthMutex_);
    return bufferHealth_;
}

PerformanceMeasurementsSnapshot AudioController::ReadPerformanceMeasurements() const {
    std::lock_guard<std::mutex> lock(performanceMeasurementsMutex_);
    return performanceMeasurements_;
}

void AudioController::ResetPerformanceMeasurementsWindow() noexcept {
    ++performanceMeasurementsGeneration_;
    std::lock_guard<std::mutex> lock(performanceMeasurementsMutex_);
    performanceMeasurements_.window.reset();
}

AudioControllerResult AudioController::SetPerformanceMeasurementsEnabled(bool enabled) {
    try {
        return Invoke([this, enabled] {
            {
                std::lock_guard<std::mutex> lock(performanceMeasurementsMutex_);
                if (performanceMeasurements_.enabled == enabled) return AudioControllerResult{true, {}};
                performanceMeasurements_.enabled = enabled;
                performanceMeasurements_.window.reset();
            }
            ++performanceMeasurementsGeneration_;
            if (runtime_) runtime_->SetStageMeasurementsEnabled(enabled);
            if (physicalHost_) physicalHost_->setStageMeasurementsEnabled(enabled);
            for (const auto& client : clients_.GetClientSnapshots())
                if (client.mapping) client.mapping->SetStageMeasurementsEnabled(enabled);
            return AudioControllerResult{true, {}};
        });
    } catch (const std::exception& exception) {
        return {false, exception.what()};
    }
}

AudioControllerResult AudioController::ApplyConfiguration(
    const AudioControllerConfiguration& configuration,
    std::optional<std::uint64_t> expectedConfigurationRevision, bool preserveClockSource,bool preserveWasapiBridges,
    bool preserveNativeAudio) {
    try {
        return Invoke([this, configuration, expectedConfigurationRevision, preserveClockSource,preserveWasapiBridges,preserveNativeAudio] {
            if (expectedConfigurationRevision &&
                *expectedConfigurationRevision != configurationRevision_)
                return AudioControllerResult{false,
                    "Saved configuration changed in another application; refresh and retry", true};
            auto effective = configuration;
            if (preserveClockSource) effective.clockSource = desiredConfiguration_.clockSource;
            if (preserveWasapiBridges) effective.wasapiBridges=desiredConfiguration_.wasapiBridges;
            if (preserveNativeAudio) effective.nativeAudio=desiredConfiguration_.nativeAudio;
            return ApplyOnWorker(effective);
        });
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

AudioControllerResult AudioController::ResetAudio() {
    try {
        return Invoke([this] {
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
            StopPhysicalRuntime();
            ++bufferHealthGeneration_;
            bufferHealthPolicy_.Reset();
            { std::lock_guard<std::mutex> lock(bufferHealthMutex_); bufferHealth_ = {}; }
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
    state_.wasapiBridges=wasapiManager_?wasapiManager_->Snapshots():std::vector<WasapiBridgeSnapshot>{};
    if(!wasapiManager_)for(const auto& config:state_.configuredWasapiBridges) {
        WasapiBridgeSnapshot bridge;bridge.id=config.id;bridge.state=config.enabled?"stopped":"disabled";
        bridge.health.state=WasapiBridgeState::Stopped;state_.wasapiBridges.push_back(std::move(bridge));
    }
    state_.wasapiDevices=wasapiDevices_;
    state_.wasapiInventoryError=wasapiInventoryError_;
    state_.revision = revision;
    state_.configurationRevision = configurationRevision_;
    events_.push_back(state_);
    if (events_.size() > 64) events_.pop_front();
}

void AudioController::Run() noexcept {
    try {
        hostWindow_ = createHostWindow();
        if (!hostWindow_) throw std::runtime_error("Unable to create ASIO message window");
        physicalHost_ = clockHostFactory_ ? clockHostFactory_() : std::make_unique<PhysicalAsioHost>();
        hostClockSource_ = AudioClockSource::Asio;
        if (!physicalHost_) throw std::runtime_error("Audio clock host factory returned null");
        RestoreApplicationProfilesOnWorker();
        RestoreConfigurationOnWorker();
        if (startupEvent_) SetEvent(startupEvent_);
        auto nextRouteReconcile = std::chrono::steady_clock::time_point{};
        auto nextWasapiHealth=std::chrono::steady_clock::time_point{};
        auto bufferWindowStart = std::chrono::steady_clock::time_point{};
        auto observedBufferHealthGeneration = bufferHealthGeneration_;
        auto observedPerformanceGeneration = performanceMeasurementsGeneration_;
        auto performanceWindowStart = std::chrono::steady_clock::time_point{};
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
            const auto clockError = physicalHost_ ? physicalHost_->consumeErrorCode() : 0;
            if (clockError) {
                auto failed = Snapshot();
                StopPhysicalRuntime();
                audioStoppedByCommand_ = true;
                failed.state = "error";
                failed.clockCapabilities = {};
                failed.physicalCapabilities = {};
                failed.lastError = "Audio clock backend failed (Windows error " +
                    std::to_string(clockError) + "); explicitly start or reset audio to retry";
                SetStatus(failed);
            }
            if (!clockError && physicalHost_ && physicalHost_->consumeSampleRateChange()) {
                const auto previous = Snapshot();
                StopPhysicalRuntime();
                AudioControllerSnapshot failed = previous;
                failed.state = "error";
                failed.physicalCapabilities = {};
                failed.lastError = "Physical ASIO driver reported a sample-rate change; reapply the configuration to resynchronize TimoxVasio";
                SetStatus(failed);
            }
            if (!audioStoppedByCommand_ && (desiredConfiguration_.physicalDriverId ||
                    desiredConfiguration_.clockSource == AudioClockSource::Software)) {
                ReconcileWasapiOnWorker();
                if(wasapiManager_ && std::chrono::steady_clock::now()>=nextWasapiHealth) {
                    SetStatus(Snapshot());nextWasapiHealth=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);
                }
                const auto clients = clients_.GetClientSnapshots();
                const auto resolved = PersistentRoutes::ResolveForClock(desiredConfiguration_.routes,
                    clients, desiredConfiguration_.physicalDriverId,
                    wasapiManager_?wasapiManager_->Bindings():std::vector<WasapiRoutingBinding>{});
                const auto current = Snapshot();
                const bool attachedClient = hasAttachedClient(clients);
                const bool routesChanged = !sameRoutes(resolved, current.routes);
                const bool generationChanged = runtime_ &&
                    !runtime_->MatchesClientGenerations(clients);
                const bool callbackNeeded = attachedClient && current.state != "running";
                if (routesChanged || generationChanged || callbackNeeded) {
                    const auto now = std::chrono::steady_clock::now();
                    if (now >= nextRouteReconcile) {
                        const bool swapped = TryHotSwapRoutes(resolved, clients);
                        const auto applied = swapped ? AudioControllerResult{true, {}} :
                            ApplyConfigurationCore(desiredConfiguration_);
                        nextRouteReconcile = now + (applied.success
                            ? std::chrono::milliseconds(250) : std::chrono::seconds(2));
                    }
                } else {
                    nextRouteReconcile = std::chrono::steady_clock::time_point{};
                }
                if (resolved.size() == desiredConfiguration_.routes.size()) {
                    std::string migrationError;
                    auto durable = PersistentRoutes::Encode(desiredConfiguration_.routes,
                        clients, migrationError);
                    if (migrationError.empty() &&
                        !sameRoutes(durable, desiredConfiguration_.routes)) {
                        auto updated = desiredConfiguration_;
                        updated.routes = std::move(durable);
                        if (configurationStore_.Save(updated, migrationError)) {
                            desiredConfiguration_ = std::move(updated);
                            auto status = Snapshot();
                            status.configuredRoutes = desiredConfiguration_.routes;
                            SetStatus(status);
                        }
                    }
                }
            }
            if (observedBufferHealthGeneration != bufferHealthGeneration_) {
                observedBufferHealthGeneration = bufferHealthGeneration_;
                bufferWindowStart = {};
            }
            const auto currentStatus = Snapshot();
            if (currentStatus.state == "running" && currentStatus.sampleRate &&
                currentStatus.bufferFrames && physicalHost_) {
                const auto now = std::chrono::steady_clock::now();
                if (bufferWindowStart == std::chrono::steady_clock::time_point{})
                    bufferWindowStart = now;
                const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - bufferWindowStart).count();
                if (durationMs >= 5000) {
                    const auto counters = physicalHost_->consumeTimingCounters();
                    AsioBufferWindow window{*currentStatus.sampleRate, *currentStatus.bufferFrames,
                        static_cast<std::uint64_t>(durationMs), counters.callbacks,
                        counters.delayedStarts, counters.processingOverruns,
                        counters.worstProcessingNs};
                    const auto assessment = bufferHealthPolicy_.Assess(window);
                    { std::lock_guard<std::mutex> lock(bufferHealthMutex_);
                      bufferHealth_ = {true, window, assessment}; }
                    bufferWindowStart = now;
                }
            } else {
                bufferWindowStart = {};
                bufferHealthPolicy_.Reset();
                { std::lock_guard<std::mutex> lock(bufferHealthMutex_); bufferHealth_ = {}; }
            }
            if (observedPerformanceGeneration != performanceMeasurementsGeneration_) {
                observedPerformanceGeneration = performanceMeasurementsGeneration_;
                performanceWindowStart = {};
                if (runtime_) runtime_->ConsumeStageCounters();
                if (physicalHost_) physicalHost_->consumeOutputCopyStageCounters();
                for (const auto& client : clients_.GetClientSnapshots())
                    if (client.mapping) client.mapping->ConsumeClientStageCounters();
            }
            const bool measuring = ReadPerformanceMeasurements().enabled;
            if (measuring && currentStatus.state == "running" && runtime_ && physicalHost_) {
                const auto now = std::chrono::steady_clock::now();
                if (performanceWindowStart == std::chrono::steady_clock::time_point{})
                    performanceWindowStart = now;
                if (now - performanceWindowStart >= std::chrono::seconds(5)) {
                    LARGE_INTEGER frequencyValue{};
                    QueryPerformanceFrequency(&frequencyValue);
                    const auto frequency = frequencyValue.QuadPart > 0
                        ? static_cast<std::uint64_t>(frequencyValue.QuadPart) : 0;
                    PerformanceMeasurementsWindow window;
                    const auto runtimeCounters = runtime_->ConsumeStageCounters();
                    window.physical.clientRead = durationFromTicks(runtimeCounters.clientRead, frequency);
                    window.physical.routing = durationFromTicks(runtimeCounters.routing, frequency);
                    window.physical.outputCopy = durationFromTicks(
                        physicalHost_->consumeOutputCopyStageCounters(), frequency);
                    for (const auto& client : clients_.GetClientSnapshots()) {
                        PerformanceClientWindow item;
                        item.processId = client.info.processId;
                        item.driverId = client.info.driverId == 1
                            ? "TimoxVasio" : std::to_string(client.info.driverId);
                        if (client.mapping) {
                            const auto counters = client.mapping->ConsumeClientStageCounters();
                            if (client.mapping->EngineAttached() && counters.epoch &&
                                counters.epoch == client.mapping->AudioEpoch() &&
                                counters.epoch == client.mapping->PublishedAudioEpoch()) {
                                item.hostCallback = durationFromTicks(counters.hostCallback, frequency);
                                item.ringWrite = durationFromTicks(counters.ringWrite, frequency);
                                item.ringWait = durationFromTicks(counters.ringWait, frequency);
                                item.ringWaitDiscardedBlocks = counters.ringWaitDiscardedBlocks;
                            }
                        }
                        window.clients.push_back(std::move(item));
                    }
                    { std::lock_guard<std::mutex> lock(performanceMeasurementsMutex_);
                      performanceMeasurements_.window = std::move(window); }
                    performanceWindowStart = now;
                }
            } else {
                performanceWindowStart = {};
            }
            bool shouldStop = false;
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                shouldStop = stopping_ && queue_.empty();
            }
            if (shouldStop) break;
        }
        StopPhysicalRuntime();
        physicalHost_.reset();
        if (hostWindow_) DestroyWindow(hostWindow_);
        hostWindow_ = nullptr;
    } catch (...) {
        StopPhysicalRuntime();
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
        started_.store(false);
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
    const auto previous=Snapshot();
    const auto oldConfiguration=desiredConfiguration_;
    const auto restore=[&] {
        if(previous.state=="running" && Snapshot().revision!=previous.revision) {
            const auto result=ApplyConfigurationCore(oldConfiguration,true);
            if(!result.success) {
                auto failed=Snapshot();failed.lastError="Configuration rollback failed: "+result.error;
                SetStatus(failed);
            }
        }
    };
    try {
        std::string routeError;
        AudioControllerConfiguration durable = configuration;
        durable.routes = PersistentRoutes::Encode(configuration.routes,
            clients_.GetClientSnapshots(), routeError);
        if (!routeError.empty()) return {false, routeError};
        const auto live=ApplyPreparedLive(configuration,persist?&durable:nullptr);
        auto applied = live?*live:ApplyConfigurationCore(configuration);
        if (!applied.success) {if(!live)restore();return applied;}
        if(!persist)return applied;
        std::string error;
        if (!live && !configurationStore_.Save(durable, error)) {
            if(previous.state=="running")restore();
            else {StopPhysicalRuntime();auto failed=previous;failed.lastError=error;SetStatus(failed);}
            return {false, error};
        }
        desiredConfiguration_ = std::move(durable);
        ++configurationRevision_;
        auto status = Snapshot();
        status.configuredRoutes = desiredConfiguration_.routes;
        status.configuredWasapiBridges=desiredConfiguration_.wasapiBridges;
        status.configuredNativeAudio=desiredConfiguration_.nativeAudio;
        SetStatus(status);
        audioStoppedByCommand_ = false;
        return applied;
    } catch (const std::exception& exception) {
        restore();return {false, exception.what()};
    } catch (...) {
        restore();return {false,"Unknown audio controller failure"};
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
    const auto restored = ApplyConfigurationCore(configuration,true);
    if (restored.success) {
        ++configurationRevision_;
        SetStatus(Snapshot());
    }
    if (!restored.success) {
        StopPhysicalRuntime();
        auto failed = Snapshot();
        failed.state = "stopped";
        failed.physicalCapabilities = {};
        failed.lastError = restored.error;
        SetStatus(failed);
    }
}

AudioControllerResult AudioController::ApplyConfigurationCore(
    const AudioControllerConfiguration& configuration,bool allowUnavailable) {
    const bool software = configuration.clockSource == AudioClockSource::Software;
    if (configuration.clockSource != AudioClockSource::Asio && !software)
        return {false, "Unsupported audio clock source"};
    if (software && (configuration.physicalDriverId || !configuration.sampleRate ||
            *configuration.sampleRate < 8000 || *configuration.sampleRate > 384000 ||
            !configuration.bufferFrames || *configuration.bufferFrames < 16 ||
            *configuration.bufferFrames > 8192))
        return {false, "Software clock requires no physical driver, a sample rate from 8000 to 384000 and a buffer size from 16 to 8192"};
    if (!software && !configuration.physicalDriverId &&
            (!configuration.routes.empty() || configuration.sampleRate || configuration.bufferFrames))
        return {false, "A physical ASIO clock is required for sample-rate, buffer and routing configuration"};
    std::string preparationError;
    if(!ValidateWasapiBridgeConfigurations(configuration.wasapiBridges,preparationError))return {false,preparationError};
    std::shared_ptr<WasapiBridgeManager> candidate;
    if(configuration.sampleRate && configuration.bufferFrames) {
        candidate=WasapiBridgeManager::Prepare(configuration.wasapiBridges,*configuration.sampleRate,
            *configuration.bufferFrames,{},wasapiBackendFactory_,preparationError,allowUnavailable,wasapiEndpointResolver_);
        if(!candidate)return {false,preparationError};
    }
    ++bufferHealthGeneration_;
    bufferHealthPolicy_.Reset();
    { std::lock_guard<std::mutex> lock(bufferHealthMutex_); bufferHealth_ = {}; }
    AudioControllerSnapshot next;
    next.clockSource = configuration.clockSource;
    next.state = "reconfiguring";
    next.configuredRoutes = configuration.routes;
    next.configuredWasapiBridges=configuration.wasapiBridges;
    next.configuredNativeAudio=configuration.nativeAudio;
    SetStatus(next);
    StopPhysicalRuntime();
    if (hostClockSource_ != configuration.clockSource) {
        std::unique_ptr<AudioClockHost> replacement;
        if (software) replacement = std::make_unique<SoftwareClockHost>();
        else replacement = clockHostFactory_ ? clockHostFactory_() : std::make_unique<PhysicalAsioHost>();
        if (!replacement) throw std::runtime_error("Audio clock host factory returned null");
        physicalHost_ = std::move(replacement);
        hostClockSource_ = configuration.clockSource;
    }
    if (!software && !configuration.physicalDriverId) {
        next.state = "stopped";
        SetStatus(next);
        return {true, {}};
    }

    const auto selectedId = configuration.physicalDriverId.value_or("software");
    const std::wstring driverId(selectedId.begin(), selectedId.end());
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

    if(!candidate)candidate=WasapiBridgeManager::Prepare(configuration.wasapiBridges,sampleRate,
        physicalBufferFrames,{},wasapiBackendFactory_,error,allowUnavailable,wasapiEndpointResolver_);
    if(!candidate) {
        physicalHost_->close();next.state="error";next.lastError=error;SetStatus(next);return {false,error};
    }
    const auto bindings=candidate->Bindings();
    const auto activeRoutes=PersistentRoutes::ResolveForClock(configuration.routes,
        clients_.GetClientSnapshots(),configuration.physicalDriverId,bindings);
    configureVasioClients(clients_, physicalBufferFrames, sampleRate);
    AudioRoutingLayout layout;
    layout.physicalDriverId = configuration.physicalDriverId.value_or("");
    layout.sampleRate = sampleRate;
    layout.bufferFrames = physicalBufferFrames;
    layout.physicalInputChannels = capabilities.inputChannels;
    layout.physicalOutputChannels = capabilities.outputChannels;
    layout.clients = clients_.GetClientSnapshots();
    const auto generationClients = layout.clients;
    layout.routes = activeRoutes;
    layout.wasapiBindings=bindings;
    runtime_ = AudioRoutingRuntime::Create(std::move(layout), error);
    if (!runtime_) {
        physicalHost_->close();
        next.state = "error";
        next.physicalDriverId = configuration.physicalDriverId;
        next.lastError = std::move(error);
        SetStatus(next);
        return {false, next.lastError};
    }
    wasapiManager_=std::move(candidate);
    runtime_->SetStageMeasurementsEnabled(ReadPerformanceMeasurements().enabled);
    for (const auto& client : generationClients)
        if (client.mapping) client.mapping->SetStageMeasurementsEnabled(
            ReadPerformanceMeasurements().enabled);

    // Without an attached client the physical driver remains configured for
    // endpoint discovery. An attached client needs the clock-only callback to
    // exchange compact frames and receive its signal even without routes.
    // An enabled slave needs a live selected master even while its routes are
    // dormant, so endpoint recovery can resume WASAPI-to-WASAPI flow. Explicit
    // StopAudio destroys this manager and prevents retries until StartAudio.
    const bool enabledBridge=std::any_of(configuration.wasapiBridges.begin(),configuration.wasapiBridges.end(),
        [](const WasapiBridgeConfiguration& bridge){return bridge.enabled;});
    if (!software && activeRoutes.empty() && !hasAttachedClient(generationClients) && !enabledBridge) {
        next.state = "stopped";
        next.physicalDriverId = configuration.physicalDriverId;
        next.sampleRate = sampleRate;
        next.bufferFrames = physicalBufferFrames;
        next.routes = activeRoutes;
        next.clockCapabilities = capabilities;
        next.physicalCapabilities = std::move(capabilities);
        SetStatus(next);
        return {true, {}};
    }

    callbackSlot_.Publish(runtime_.get());
    if (!physicalHost_->start(physicalBufferFrames, runtime_->RoutedPhysicalInputChannels(),
                               runtime_->RoutedPhysicalOutputChannels(),
                               &RealtimeCallbackSlot::Dispatch, &callbackSlot_, error)) {
        StopPhysicalRuntime();
        next.state = "error";
        next.physicalDriverId = configuration.physicalDriverId;
        next.sampleRate = sampleRate;
        next.bufferFrames = physicalBufferFrames;
        next.lastError = std::move(error);
        SetStatus(next);
        return {false, next.lastError};
    }
    hostPhysicalInputs_ = runtime_->RoutedPhysicalInputChannels();
    hostPhysicalOutputs_ = runtime_->RoutedPhysicalOutputChannels();
    physicalHost_->setStageMeasurementsEnabled(ReadPerformanceMeasurements().enabled);
    acknowledgePublishedGenerations(generationClients, sampleRate, physicalBufferFrames);
    next.state = "running";
    next.physicalDriverId = configuration.physicalDriverId;
    next.sampleRate = sampleRate;
    next.bufferFrames = physicalBufferFrames;
    next.routes = activeRoutes;
    next.clockCapabilities = capabilities;
    if (!software) next.physicalCapabilities = std::move(capabilities);
    SetStatus(next);
    return {true, {}};
}
