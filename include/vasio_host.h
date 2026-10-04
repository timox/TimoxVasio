#pragma once

#include <vector>
#include <memory>
#include <string>

// PortAudio - projet existant
// https://github.com/PortAudio/portaudio
#include "portaudio.h"

struct DriverInfo {
    std::string name;
    int deviceIndex;
    int inputChannels;
    int outputChannels;
    double sampleRate;
};

class VASIOHost {
public:
    static VASIOHost& GetInstance();

    bool Initialize();
    void Shutdown();

    std::vector<DriverInfo> EnumerateDevices() const;
    const DriverInfo* GetDriverInfo(const std::string& name) const;

    bool CreateVirtualDriver(const std::string& name, int channels);
    bool RemoveVirtualDriver(const std::string& name);

private:
    VASIOHost();
    ~VASIOHost();

    std::vector<DriverInfo> drivers;
    bool isInitialized;
};
