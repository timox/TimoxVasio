#pragma once

#include <vector>
#include <string>
#include <map>
#include <memory>
#include <atomic>
#include <thread>

typedef int ASIOError;
typedef int ASIOBool;

enum ASIOSampleType {
    ASIOSTInt16MSB = 0,
    ASIOSTInt24MSB = 1,
    ASIOSTInt32MSB = 2,
    ASIOSTFloat32MSB = 3,
    ASIOSTFloat64MSB = 4,
    ASIOSTInt32MSB16 = 5,
    ASIOSTInt32MSB18 = 6,
    ASIOSTInt32MSB20 = 7,
    ASIOSTInt32MSB24 = 8,
    ASIOSTInt16LSB = 16,
    ASIOSTInt24LSB = 17,
    ASIOSTInt32LSB = 18,
    ASIOSTFloat32LSB = 19,
    ASIOSTFloat64LSB = 20,
    ASIOSTInt32LSB16 = 21,
    ASIOSTInt32LSB18 = 22,
    ASIOSTInt32LSB20 = 23,
    ASIOSTInt32LSB24 = 24
};

struct ChannelInfo {
    int channelNum;
    ASIOBool isActive;
    ASIOSampleType type;
    std::string name;
};

struct DriverCapabilities {
    long minBufferSize;
    long maxBufferSize;
    long preferredBufferSize;
    long granularity;
    long sampleRate;
    ASIOBool postOutput;
    std::vector<long> supportedSampleRates;
    std::vector<ASIOSampleType> supportedFormats;
};

class VirtualDriver {
public:
    VirtualDriver(int driverIndex, int numChannels);
    ~VirtualDriver();

    const std::string& GetName() const { return driverName; }
    int GetDriverIndex() const { return driverIndex; }
    int GetNumChannels() const { return numChannels; }
    const DriverCapabilities& GetCapabilities() const { return capabilities; }
    const std::vector<ChannelInfo>& GetChannels() const { return channels; }

    void SetBufferSize(long size);
    void SetSampleRate(long rate);
    void SetActive(bool active);

    ASIOError Start();
    ASIOError Stop();
    ASIOError ProcessBuffer(float* inputData, float* outputData, long numSamples);

    void ProbeSystemCapabilities();

private:
    int driverIndex;
    int numChannels;
    std::string driverName;
    DriverCapabilities capabilities;
    std::vector<ChannelInfo> channels;
    bool isRunning;
    long currentSampleRate;
    long currentBufferSize;

    void InitializeChannels();
    void InitializeCapabilities();
};
