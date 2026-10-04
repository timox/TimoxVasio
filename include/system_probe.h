#pragma once

#include "virtual_driver.h"
#include <vector>
#include <string>

class SystemProbe {
public:
    static SystemProbe& GetInstance();

    struct SystemInfo {
        std::vector<long> supportedSampleRates;
        std::vector<ASIOSampleType> supportedFormats;
        long minBufferSize;
        long maxBufferSize;
        long preferredBufferSize;
        int numInputChannels;
        int numOutputChannels;
    };

    bool ProbeAudioDevices();
    const SystemInfo& GetSystemInfo() const { return systemInfo; }

    std::vector<std::string> EnumerateAudioDevices() const;
    bool CheckFormat(ASIOSampleType format) const;
    bool CheckSampleRate(long sampleRate) const;

private:
    SystemProbe();
    ~SystemProbe();

    SystemInfo systemInfo;

    void DetectSampleRates();
    void DetectFormats();
    void DetectBufferSizes();
    void DetectChannelCounts();
};
