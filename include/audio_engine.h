#pragma once

#include <map>
#include <vector>
#include <memory>
#include <string>

struct RouteMapping {
    std::string name;
    std::string sourceDriver;
    int sourceChannel;
    std::string destDriver;
    int destChannel;
};

struct AudioBuffer {
    float* data;
    int size;
    int sampleCount;
    int channelCount;
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    void AddRoute(const RouteMapping& route);
    void RemoveRoute(const std::string& routeName);
    void ClearRoutes();

    const std::vector<RouteMapping>& GetRoutes() const { return routes; }
    const std::map<std::string, std::vector<float>>& GetBuffers() const { return buffers; }

    void ProcessRouting(const std::string& driverName, int channelIndex,
                       const float* inputData, float* outputData, int numSamples);

    void SetMasterVolume(float volume);
    void SetChannelVolume(const std::string& driver, int channel, float volume);

    bool Initialize();
    void Shutdown();

private:
    std::vector<RouteMapping> routes;
    std::map<std::string, std::vector<float>> buffers;
    std::map<std::string, std::map<int, float>> channelVolumes;
    float masterVolume;

    void MixAudio(float* dest, const float* src, int numSamples, float volume);
};
