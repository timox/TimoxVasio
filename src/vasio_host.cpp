#include "../include/vasio_host.h"
#include <iostream>

// Utilise PortAudio - projet existant depuis 2000
// Code éprouvé et maintained par la communauté

VASIOHost& VASIOHost::GetInstance() {
    static VASIOHost instance;
    return instance;
}

VASIOHost::VASIOHost() : isInitialized(false) {
}

VASIOHost::~VASIOHost() {
    if (isInitialized) {
        Shutdown();
    }
}

bool VASIOHost::Initialize() {
    if (isInitialized) return true;

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "Erreur Pa_Initialize: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    isInitialized = true;
    return true;
}

void VASIOHost::Shutdown() {
    if (!isInitialized) return;

    PaError err = Pa_Terminate();
    if (err != paNoError) {
        std::cerr << "Erreur Pa_Terminate: " << Pa_GetErrorText(err) << std::endl;
    }

    drivers.clear();
    isInitialized = false;
}

std::vector<DriverInfo> VASIOHost::EnumerateDevices() const {
    std::vector<DriverInfo> result;

    if (!isInitialized) return result;

    int deviceCount = Pa_GetDeviceCount();
    for (int i = 0; i < deviceCount; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info) continue;

        DriverInfo driver;
        driver.deviceIndex = i;
        driver.name = info->name;
        driver.inputChannels = info->maxInputChannels;
        driver.outputChannels = info->maxOutputChannels;
        driver.sampleRate = info->defaultSampleRate;

        result.push_back(driver);
    }

    return result;
}

const DriverInfo* VASIOHost::GetDriverInfo(const std::string& name) const {
    for (const auto& driver : drivers) {
        if (driver.name == name) {
            return &driver;
        }
    }
    return nullptr;
}

bool VASIOHost::CreateVirtualDriver(const std::string& name, int channels) {
    DriverInfo driver;
    driver.name = name;
    driver.inputChannels = channels;
    driver.outputChannels = channels;
    driver.sampleRate = 44100.0;
    driver.deviceIndex = -1;

    drivers.push_back(driver);
    return true;
}

bool VASIOHost::RemoveVirtualDriver(const std::string& name) {
    auto it = std::find_if(drivers.begin(), drivers.end(),
        [&name](const DriverInfo& d) { return d.name == name; });

    if (it != drivers.end()) {
        drivers.erase(it);
        return true;
    }
    return false;
}
