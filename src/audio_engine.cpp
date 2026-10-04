#include "../include/audio_engine.h"
#include <algorithm>
#include <cmath>

AudioEngine::AudioEngine() : masterVolume(1.0f) {
}

AudioEngine::~AudioEngine() {
    Shutdown();
}

void AudioEngine::AddRoute(const RouteMapping& route) {
    auto it = std::find_if(routes.begin(), routes.end(),
        [&route](const RouteMapping& r) { return r.name == route.name; });

    if (it == routes.end()) {
        routes.push_back(route);
    }
}

void AudioEngine::RemoveRoute(const std::string& routeName) {
    auto it = std::remove_if(routes.begin(), routes.end(),
        [&routeName](const RouteMapping& r) { return r.name == routeName; });

    routes.erase(it, routes.end());
}

void AudioEngine::ClearRoutes() {
    routes.clear();
}

void AudioEngine::ProcessRouting(const std::string& driverName, int channelIndex,
                                 const float* inputData, float* outputData, int numSamples) {
    if (!inputData || !outputData) {
        return;
    }

    std::fill(outputData, outputData + numSamples, 0.0f);

    for (const auto& route : routes) {
        if (route.sourceDriver == driverName && route.sourceChannel == channelIndex) {
            float channelVolume = 1.0f;
            auto driverVolumes = channelVolumes.find(driverName);
            if (driverVolumes != channelVolumes.end()) {
                auto configuredVolume = driverVolumes->second.find(channelIndex);
                if (configuredVolume != driverVolumes->second.end()) channelVolume = configuredVolume->second;
            }
            MixAudio(outputData, inputData, numSamples, masterVolume * channelVolume);
        }
    }
}

void AudioEngine::SetMasterVolume(float volume) {
    masterVolume = std::clamp(volume, 0.0f, 2.0f);
}

void AudioEngine::SetChannelVolume(const std::string& driver, int channel, float volume) {
    channelVolumes[driver][channel] = std::clamp(volume, 0.0f, 2.0f);
}

void AudioEngine::MixAudio(float* dest, const float* src, int numSamples, float volume) {
    for (int i = 0; i < numSamples; ++i) {
        dest[i] += src[i] * volume;
        dest[i] = std::clamp(dest[i], -1.0f, 1.0f);
    }
}

bool AudioEngine::Initialize() {
    buffers.clear();
    channelVolumes.clear();
    return true;
}

void AudioEngine::Shutdown() {
    buffers.clear();
    routes.clear();
}
