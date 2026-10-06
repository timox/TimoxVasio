#include "audio_configuration_store.h"
#include "audio_controller.h"

#include <windows.h>

#include <cstdio>
#include <string>

namespace {
bool require(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

std::wstring testPath() {
    wchar_t directory[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, directory)) return {};
    return std::wstring(directory) + L"TimoxVasio-configuration-store-test-" +
        std::to_wstring(GetCurrentProcessId()) + L".json";
}
}

int wmain() {
    const auto path = testPath();
    if (path.empty()) return 2;
    DeleteFileW(path.c_str());

    AudioConfigurationStore store(path);
    AudioControllerConfiguration restored;
    std::string error;
    const auto missing = store.Load(restored, error);
    bool passed = require(missing == ConfigurationLoadStatus::Missing,
        "a missing configuration is reported as missing");

    AudioControllerConfiguration expected;
    expected.physicalDriverId = "{physical-driver}";
    expected.sampleRate = 48000;
    expected.bufferFrames = 512;
    expected.routes.push_back({"route-1", "virtual:TimoxVasio:123:output:1",
        "physical:driver:output:2", -3.5, false});
    passed = require(store.Save(expected, error), "a valid configuration is saved") && passed;
    const auto status = store.Load(restored, error);
    passed = require(status == ConfigurationLoadStatus::Loaded,
        "a saved configuration is loaded") && passed;
    if (status == ConfigurationLoadStatus::Loaded) {
        passed = require(restored.physicalDriverId == expected.physicalDriverId,
            "physical driver identity is preserved") && passed;
        passed = require(restored.sampleRate == expected.sampleRate &&
            restored.bufferFrames == expected.bufferFrames,
            "effective sample rate and buffer size are preserved") && passed;
        passed = require(restored.routes.size() == 1 &&
            restored.routes[0].id == expected.routes[0].id &&
            restored.routes[0].sourceEndpointId == expected.routes[0].sourceEndpointId &&
            restored.routes[0].destinationEndpointId == expected.routes[0].destinationEndpointId &&
            restored.routes[0].gainDb == expected.routes[0].gainDb &&
            restored.routes[0].mute == expected.routes[0].mute,
            "complete route data is preserved") && passed;
    }

    expected.routes[0].sourceEndpointId = "virtual:TimoxVasio:app:renoise.exe:output:1";
    passed = require(store.Save(expected, error), "an executable-bound route is saved") && passed;
    passed = require(store.Load(restored, error) == ConfigurationLoadStatus::Loaded &&
        restored.routes.size() == 1 &&
        restored.routes[0].sourceEndpointId == expected.routes[0].sourceEndpointId,
        "an executable-bound route survives a configuration reload") && passed;

    HANDLE malformed = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (malformed == INVALID_HANDLE_VALUE) passed = false;
    else {
        constexpr char invalid[] = "{";
        DWORD written = 0;
        passed = WriteFile(malformed, invalid, sizeof(invalid) - 1, &written, nullptr) &&
            written == sizeof(invalid) - 1 && passed;
        CloseHandle(malformed);
        passed = require(store.Load(restored, error) == ConfigurationLoadStatus::Error,
            "malformed configuration is reported as an error") && passed;
    }

    DeleteFileW(path.c_str());
    if (!passed) return 1;
    std::puts("PASS: configuration store distinguishes missing/invalid files and round-trips the complete configuration.");
    return 0;
}
