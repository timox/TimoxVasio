#pragma once

#include "virtual_driver.h"
#include "audio_engine.h"
#include <vector>
#include <memory>
#include <map>

class ASIOHost {
public:
    static ASIOHost& GetInstance();

    bool Initialize();
    void Shutdown();

    VirtualDriver* GetDriver(int index);
    VirtualDriver* GetDriver(const std::string& name);
    const std::vector<std::shared_ptr<VirtualDriver>>& GetAllDrivers() const { return drivers; }

    AudioEngine& GetAudioEngine() { return audioEngine; }

    int GetNumDrivers() const { return static_cast<int>(drivers.size()); }

    bool RegisterDrivers();
    bool UnregisterDrivers();

    void ProcessAudio(const std::string& driverName,
                     float* inputData, float* outputData, long numSamples);

private:
    ASIOHost();
    ~ASIOHost();

    std::vector<std::shared_ptr<VirtualDriver>> drivers;
    AudioEngine audioEngine;
    bool isInitialized;

    void CreateVirtualDrivers();
};
