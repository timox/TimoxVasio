#pragma once

#include "audio_meter.h"
#include "application_profile.h"
#include "application_profile_store.h"
#include "audio_configuration_store.h"
#include "physical_asio_host.h"
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

struct AudioControllerConfiguration {
    std::optional<std::string> physicalDriverId;
    std::optional<std::uint32_t> sampleRate;
    std::optional<std::uint32_t> bufferFrames;
    std::vector<AudioRoute> routes;
};

struct AudioControllerResult {
    bool success = false;
    std::string error;
};

struct AudioControllerSnapshot {
    std::uint64_t revision = 0;
    std::string state = "stopped";
    std::optional<std::string> physicalDriverId;
    std::optional<std::uint32_t> sampleRate;
    std::optional<std::uint32_t> bufferFrames;
    std::string lastError;
    std::vector<AudioRoute> routes;
    PhysicalAsioCapabilities physicalCapabilities;
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
    AudioControllerResult ApplyConfiguration(const AudioControllerConfiguration& configuration);
    AudioControllerResult StartAudio();
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
    AudioControllerResult ApplyConfigurationCore(const AudioControllerConfiguration& configuration);
};
