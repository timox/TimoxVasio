#include "../include/asio_host.h"
#include "../include/system_probe.h"
#include <windows.h>

ASIOHost& ASIOHost::GetInstance() {
    static ASIOHost instance;
    return instance;
}

ASIOHost::ASIOHost() : isInitialized(false) {
}

ASIOHost::~ASIOHost() {
    Shutdown();
}

bool ASIOHost::Initialize() {
    if (isInitialized) {
        return true;
    }

    SystemProbe::GetInstance().ProbeAudioDevices();
    audioEngine.Initialize();

    CreateVirtualDrivers();
    isInitialized = true;

    return isInitialized;
}

void ASIOHost::CreateVirtualDrivers() {
    drivers.clear();

    const int NUM_DRIVERS = 4;
    const int CHANNELS_PER_DRIVER = 6;

    for (int i = 0; i < NUM_DRIVERS; ++i) {
        auto driver = std::make_shared<VirtualDriver>(i, CHANNELS_PER_DRIVER);
        driver->ProbeSystemCapabilities();
        drivers.push_back(driver);
    }
}

void ASIOHost::Shutdown() {
    if (!isInitialized) {
        return;
    }

    audioEngine.Shutdown();
    drivers.clear();
    isInitialized = false;
}

VirtualDriver* ASIOHost::GetDriver(int index) {
    if (index >= 0 && index < (int)drivers.size()) {
        return drivers[index].get();
    }
    return nullptr;
}

VirtualDriver* ASIOHost::GetDriver(const std::string& name) {
    for (auto& driver : drivers) {
        if (driver->GetName() == name) {
            return driver.get();
        }
    }
    return nullptr;
}

bool ASIOHost::RegisterDrivers() {
    HKEY hKey;
    LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Steinberg\\ASIO", 0, KEY_WRITE, &hKey);

    if (result != ERROR_SUCCESS) {
        result = RegCreateKeyExA(HKEY_LOCAL_MACHINE,
            "SOFTWARE\\Steinberg\\ASIO", 0, NULL, REG_OPTION_NON_VOLATILE,
            KEY_WRITE, NULL, &hKey, NULL);
    }

    if (result != ERROR_SUCCESS) {
        return false;
    }

    for (const auto& driver : drivers) {
        RegSetValueExA(hKey, driver->GetName().c_str(), 0, REG_SZ,
            (const BYTE*)"VirtualASIODriver", 18);
    }

    RegCloseKey(hKey);
    return true;
}

bool ASIOHost::UnregisterDrivers() {
    HKEY hKey;
    LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Steinberg\\ASIO", 0, KEY_WRITE, &hKey);

    if (result != ERROR_SUCCESS) {
        return false;
    }

    for (const auto& driver : drivers) {
        RegDeleteValueA(hKey, driver->GetName().c_str());
    }

    RegCloseKey(hKey);
    return true;
}

void ASIOHost::ProcessAudio(const std::string& driverName,
                           float* inputData, float* outputData, long numSamples) {
    VirtualDriver* driver = GetDriver(driverName);
    if (driver) {
        driver->ProcessBuffer(inputData, outputData, numSamples);
    }
}
