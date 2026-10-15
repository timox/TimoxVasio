#include "../include/virtual_driver.h"
#include "../include/system_probe.h"
#include <cstring>
#include <sstream>

VirtualDriver::VirtualDriver(int idx, int numCh)
    : driverIndex(idx), numChannels(numCh), isRunning(false),
      currentSampleRate(44100), currentBufferSize(256) {

    std::ostringstream oss;
    oss << "VASIO" << (idx + 1);
    driverName = oss.str();

    InitializeChannels();
    InitializeCapabilities();
}

VirtualDriver::~VirtualDriver() {
    if (isRunning) {
        Stop();
    }
}

void VirtualDriver::InitializeChannels() {
    channels.clear();
    for (int i = 0; i < numChannels; ++i) {
        ChannelInfo ch;
        ch.channelNum = i + 1;
        ch.isActive = true;
        ch.type = ASIOSTFloat32LSB;

        std::ostringstream oss;
        oss << driverName << (i + 1);
        ch.name = oss.str();

        channels.push_back(ch);
    }
}

void VirtualDriver::InitializeCapabilities() {
    const SystemProbe::SystemInfo& sysInfo = SystemProbe::GetInstance().GetSystemInfo();

    capabilities.minBufferSize = sysInfo.minBufferSize;
    capabilities.maxBufferSize = sysInfo.maxBufferSize;
    capabilities.preferredBufferSize = sysInfo.preferredBufferSize;
    capabilities.granularity = 1;
    capabilities.sampleRate = currentSampleRate;
    capabilities.postOutput = true;
    capabilities.supportedSampleRates = sysInfo.supportedSampleRates;
    capabilities.supportedFormats = sysInfo.supportedFormats;
}

void VirtualDriver::SetBufferSize(long size) {
    if (size >= capabilities.minBufferSize && size <= capabilities.maxBufferSize) {
        currentBufferSize = size;
    }
}

void VirtualDriver::SetSampleRate(long rate) {
    bool supported = false;
    for (long sr : capabilities.supportedSampleRates) {
        if (sr == rate) {
            supported = true;
            break;
        }
    }
    if (supported) {
        currentSampleRate = rate;
        capabilities.sampleRate = rate;
    }
}

void VirtualDriver::SetActive(bool active) {
    for (auto& ch : channels) {
        ch.isActive = active;
    }
}

void VirtualDriver::ProbeSystemCapabilities() {
    InitializeCapabilities();
}

ASIOError VirtualDriver::Start() {
    if (!isRunning) {
        isRunning = true;
        return 0;
    }
    return 1;
}

ASIOError VirtualDriver::Stop() {
    if (isRunning) {
        isRunning = false;
        return 0;
    }
    return 1;
}

ASIOError VirtualDriver::ProcessBuffer(float* inputData, float* outputData, long numSamples) {
    if (!isRunning) {
        return 1;
    }

    if (inputData && outputData) {
        std::memcpy(outputData, inputData, numSamples * numChannels * sizeof(float));
    }

    return 0;
}
