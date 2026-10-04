#include "audio_controller.h"
#include "vasio_client_manager.h"

#include <windows.h>

#include <cstdio>
#include <string>

namespace {
bool require(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

std::wstring configurationPath() {
    wchar_t directory[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, directory)) return {};
    return std::wstring(directory) + L"TimoxVasio-controller-persistence-" +
        std::to_wstring(GetCurrentProcessId()) + L".json";
}

bool writeMalformedConfiguration(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    constexpr char malformed[] = "{";
    DWORD written = 0;
    const bool success = WriteFile(file, malformed, sizeof(malformed) - 1, &written, nullptr) &&
        written == sizeof(malformed) - 1;
    CloseHandle(file);
    return success;
}
}

int wmain() {
    const auto path = configurationPath();
    if (path.empty()) return 2;
    DeleteFileW(path.c_str());
    bool passed = true;

    {
        VasioClientManager clients;
        AudioController controller(clients, path);
        passed = require(controller.Start(), "controller starts with no saved configuration") && passed;
        const auto initial = controller.Snapshot();
        passed = require(initial.state == "stopped" && initial.lastError.empty() &&
            initial.revision == 0, "missing configuration leaves a clean stopped state") && passed;

        const AudioControllerConfiguration emptyConfiguration;
        const auto applied = controller.ApplyConfiguration(emptyConfiguration);
        passed = require(applied.success, "an empty stopped configuration applies") && passed;
        passed = require(GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES,
            "successful configuration.apply writes the persistence file") && passed;
        controller.Stop();
    }

    {
        VasioClientManager clients;
        AudioController controller(clients, path);
        passed = require(controller.Start(), "controller starts after restart with a saved configuration") && passed;
        const auto restored = controller.Snapshot();
        passed = require(restored.state == "stopped" && restored.lastError.empty() &&
            restored.revision >= 2,
            "startup replays the saved configuration before Start returns") && passed;
        controller.Stop();
    }

    passed = require(writeMalformedConfiguration(path), "malformed startup fixture is written") && passed;
    {
        VasioClientManager clients;
        AudioController controller(clients, path);
        passed = require(controller.Start(), "controller remains available after a restore error") && passed;
        const auto failedRestore = controller.Snapshot();
        passed = require(failedRestore.state == "stopped" && !failedRestore.lastError.empty(),
            "invalid saved configuration leaves the engine stopped with an API-visible error") && passed;
        controller.Stop();
    }

    DeleteFileW(path.c_str());
    if (!passed) return 1;
    std::puts("PASS: successful configuration.apply survives controller restart; restore errors stay stopped and visible.");
    return 0;
}
