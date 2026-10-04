#pragma once

#include <memory>
#include <string>
#include <cstdint>
#include <vector>

struct PhysicalAsioDriverInfo {
    std::wstring id; // Stable COM CLSID.
    std::string name;
};

struct PhysicalAsioCapabilities {
    long inputChannels = 0;
    long outputChannels = 0;
    std::vector<std::wstring> inputChannelNames;
    std::vector<std::wstring> outputChannelNames;
    long minBufferFrames = 0;
    long maxBufferFrames = 0;
    long preferredBufferFrames = 0;
    long bufferGranularity = 0;
    double currentSampleRate = 0.0;
    std::vector<double> supportedSampleRates;
};

using PhysicalAsioProcessCallback = void (*)(const float* const* inputs,
    float* const* outputs, std::uint32_t frames, void* context) noexcept;

// Enumerates the registered ASIO drivers without initializing or opening them.
class PhysicalAsioHost final {
public:
    PhysicalAsioHost();
    ~PhysicalAsioHost();
    PhysicalAsioHost(const PhysicalAsioHost&) = delete;
    PhysicalAsioHost& operator=(const PhysicalAsioHost&) = delete;

    std::vector<PhysicalAsioDriverInfo> enumerate() const;
    bool open(const std::wstring& driverId, void* systemHandle,
              PhysicalAsioCapabilities& capabilities, std::string& error);
    bool setSampleRate(double sampleRate, std::string& error);
    bool refreshCapabilities(PhysicalAsioCapabilities& capabilities, std::string& error);
    bool consumeSampleRateChange() noexcept;
    bool start(std::uint32_t bufferFrames, const std::vector<std::uint32_t>& inputChannels,
               const std::vector<std::uint32_t>& outputChannels,
               PhysicalAsioProcessCallback callback, void* context, std::string& error);
    void stop() noexcept;
    void close() noexcept;

private:
    struct Session;
    std::unique_ptr<Session> session_;
};
