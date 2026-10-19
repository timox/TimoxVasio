#pragma once

#include "audio_meter.h"
#include "asio_buffer_health.h"
#include "application_profile.h"
#include "application_profile_store.h"
#include "audio_configuration_store.h"
#include "physical_asio_host.h"
#include "realtime_callback_slot.h"
#include "routing_graph.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <future>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

class AudioRoutingRuntime;
class VasioClientManager;
struct VasioClientSnapshot;

struct AudioControllerConfiguration {
    std::optional<std::string> physicalDriverId;
    std::optional<std::uint32_t> sampleRate;
    std::optional<std::uint32_t> bufferFrames;
    std::vector<AudioRoute> routes;
};

struct AudioControllerResult {
    bool success = false;
    std::string error;
    bool conflict = false;
};

struct AudioControllerSnapshot {
    std::uint64_t revision = 0;
    std::uint64_t configurationRevision = 0;
    std::string state = "stopped";
    std::optional<std::string> physicalDriverId;
    std::optional<std::uint32_t> sampleRate;
    std::optional<std::uint32_t> bufferFrames;
    std::string lastError;
    std::vector<AudioRoute> routes;
    std::vector<AudioRoute> configuredRoutes;
    PhysicalAsioCapabilities physicalCapabilities;
};

struct AudioBufferHealthSnapshot {
    bool available = false;
    AsioBufferWindow window{};
    AsioBufferAssessment assessment{};
};

struct PerformanceDuration {
    std::uint64_t samples = 0;
    std::uint64_t meanNs = 0;
    std::uint64_t worstNs = 0;
};

struct PerformanceClientWindow {
    std::uint32_t processId = 0;
    std::string driverId;
    std::optional<PerformanceDuration> hostCallback;
    std::optional<PerformanceDuration> ringWrite;
    std::optional<PerformanceDuration> ringWait;
    std::uint64_t ringWaitDiscardedBlocks = 0;
};

struct PerformancePhysicalWindow {
    std::optional<PerformanceDuration> clientRead;
    std::optional<PerformanceDuration> routing;
    std::optional<PerformanceDuration> outputCopy;
};

struct PerformanceMeasurementsWindow {
    std::uint32_t durationMs = 5000;
    std::vector<PerformanceClientWindow> clients;
    PerformancePhysicalWindow physical;
};

struct PerformanceMeasurementsSnapshot {
    bool enabled = false;
    std::optional<PerformanceMeasurementsWindow> window;
};

class AudioController final {
public:
    explicit AudioController(VasioClientManager& clients,
        std::wstring configurationPath = AudioConfigurationStore::DefaultPath(),
        std::wstring applicationProfilesPath = ApplicationProfileStore::DefaultPath());
    ~AudioController();
    AudioController(const AudioController&) = delete;
    AudioController& operator=(const AudioController&) = delete;

    bool Start();
    void Stop() noexcept;
    std::vector<PhysicalAsioDriverInfo> EnumeratePhysicalDrivers();
    AudioControllerResult ApplyConfiguration(const AudioControllerConfiguration& configuration,
        std::optional<std::uint64_t> expectedConfigurationRevision = std::nullopt);
    AudioControllerResult StartAudio();
    AudioControllerResult ResetAudio();
    AudioControllerResult StopAudio();
    AudioControllerResult StartCorrelation(const std::string& leftEndpointId,
                                           const std::string& rightEndpointId);
    void StopCorrelation();
    AudioCorrelationSnapshot ReadCorrelation();
    ApplicationProfilesSnapshot GetApplicationProfiles();
    ApplicationProfilesUpdateResult ReplaceApplicationProfiles(
        std::vector<ApplicationProfile> profiles);
    AudioControllerSnapshot Snapshot() const;
    std::vector<AudioControllerSnapshot> EventsSince(std::uint64_t revision) const;
    std::vector<AudioMeterSnapshot> ReadMeters();
    AudioBufferHealthSnapshot ReadBufferHealth() const;
    AudioControllerResult SetPerformanceMeasurementsEnabled(bool enabled);
    PerformanceMeasurementsSnapshot ReadPerformanceMeasurements() const;

private:
    template<class Function>
    auto Invoke(Function&& function) -> decltype(function()) {
        using Result = decltype(function());
        auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<Function>(function));
        auto future = task->get_future();
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (!accepting_) throw std::runtime_error("Audio controller is not running");
            queue_.emplace_back([task] { (*task)(); });
        }
        SetEvent(queueEvent_);
        return future.get();
    }

    void Run() noexcept;
    AudioControllerResult ApplyOnWorker(const AudioControllerConfiguration& configuration,
                                        bool persist = true);
    void RestoreConfigurationOnWorker();
    void RestoreApplicationProfilesOnWorker();
    void SetStatus(const AudioControllerSnapshot& snapshot);

    VasioClientManager& clients_;
    AudioConfigurationStore configurationStore_;
    ApplicationProfileStore applicationProfileStore_;
    AudioControllerConfiguration desiredConfiguration_;
    std::uint64_t configurationRevision_ = 0; // Controller worker only.
    bool audioStoppedByCommand_ = false;
    std::vector<ApplicationProfile> applicationProfiles_ = ApplicationProfileStore::DefaultProfiles();
    std::string applicationProfilesError_;
    std::atomic<bool> started_{false};
    std::thread worker_;
    mutable std::mutex queueMutex_;
    std::deque<std::function<void()>> queue_;
    bool accepting_ = false;
    bool stopping_ = false;
    HANDLE queueEvent_ = nullptr;
    HANDLE startupEvent_ = nullptr;
    HWND hostWindow_ = nullptr;
    std::unique_ptr<PhysicalAsioHost> physicalHost_;
    std::unique_ptr<AudioRoutingRuntime> runtime_;
    mutable std::mutex stateMutex_;
    AudioControllerSnapshot state_;
    std::deque<AudioControllerSnapshot> events_;
    mutable std::mutex bufferHealthMutex_;
    AudioBufferHealthSnapshot bufferHealth_;
    AsioBufferHealthPolicy bufferHealthPolicy_;
    std::uint64_t bufferHealthGeneration_ = 0; // Controller worker only.
    mutable std::mutex performanceMeasurementsMutex_;
    PerformanceMeasurementsSnapshot performanceMeasurements_;
    std::uint64_t performanceMeasurementsGeneration_ = 0; // Controller worker only.
    void ResetPerformanceMeasurementsWindow() noexcept;
    AudioControllerResult ApplyConfigurationCore(const AudioControllerConfiguration& configuration);
    bool TryHotSwapRoutes(const std::vector<AudioRoute>& routes,
        const std::vector<VasioClientSnapshot>& clients);
    void StopPhysicalRuntime() noexcept;
    RealtimeCallbackSlot callbackSlot_;
    std::vector<std::uint32_t> hostPhysicalInputs_;
    std::vector<std::uint32_t> hostPhysicalOutputs_;
};
