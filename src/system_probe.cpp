#include "../include/system_probe.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

SystemProbe::SystemProbe() {
    DetectSampleRates();
    DetectFormats();
    DetectBufferSizes();
    DetectChannelCounts();
}

SystemProbe::~SystemProbe() {
}

SystemProbe& SystemProbe::GetInstance() {
    static SystemProbe instance;
    return instance;
}

void SystemProbe::DetectSampleRates() {
    systemInfo.supportedSampleRates = {
        8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000,
        88200, 96000, 176400, 192000
    };
}

void SystemProbe::DetectFormats() {
    systemInfo.supportedFormats = {
        ASIOSTInt16LSB,
        ASIOSTInt32LSB,
        ASIOSTFloat32LSB,
        ASIOSTInt24LSB
    };
}

void SystemProbe::DetectBufferSizes() {
    systemInfo.minBufferSize = 64;
    systemInfo.preferredBufferSize = 256;
    systemInfo.maxBufferSize = 8192;
}

void SystemProbe::DetectChannelCounts() {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        systemInfo.numInputChannels = 2;
        systemInfo.numOutputChannels = 2;
        return;
    }

    IMMDeviceEnumerator* pEnumerator = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                         __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);

    if (SUCCEEDED(hr)) {
        IMMDevice* pDevice = nullptr;
        hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);

        if (SUCCEEDED(hr)) {
            IAudioEndpointVolume* pVolume = nullptr;
            hr = pDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL,
                                 (void**)&pVolume);

            if (SUCCEEDED(hr)) {
                systemInfo.numOutputChannels = 2;
                pVolume->Release();
            }
            pDevice->Release();
        }
        pEnumerator->Release();
    }

    systemInfo.numInputChannels = 2;
    if (systemInfo.numOutputChannels == 0) {
        systemInfo.numOutputChannels = 2;
    }

    CoUninitialize();
}

bool SystemProbe::ProbeAudioDevices() {
    DetectSampleRates();
    DetectFormats();
    DetectBufferSizes();
    DetectChannelCounts();
    return true;
}

std::vector<std::string> SystemProbe::EnumerateAudioDevices() const {
    std::vector<std::string> devices;
    devices.push_back("Default Output");
    devices.push_back("Default Input");
    return devices;
}

bool SystemProbe::CheckFormat(ASIOSampleType format) const {
    for (const auto& fmt : systemInfo.supportedFormats) {
        if (fmt == format) return true;
    }
    return false;
}

bool SystemProbe::CheckSampleRate(long sampleRate) const {
    for (const auto& rate : systemInfo.supportedSampleRates) {
        if (rate == sampleRate) return true;
    }
    return false;
}
