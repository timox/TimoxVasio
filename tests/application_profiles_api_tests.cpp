#include <httplib.h>
#include <nlohmann/json.hpp>

#include "audio_controller.h"
#include "control_api_server.h"
#include "vasio_client_manager.h"

#include <windows.h>

#include <cstdio>
#include <string>

namespace {
bool require(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

std::wstring tempPath(const wchar_t* suffix) {
    wchar_t directory[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, directory)) return {};
    return std::wstring(directory) + L"TimoxVasio-profile-api-test-" +
        std::to_wstring(GetCurrentProcessId()) + suffix;
}
}

int wmain() {
    const auto configPath = tempPath(L"-configuration.json");
    const auto profilesPath = tempPath(L"-profiles.json");
    if (configPath.empty() || profilesPath.empty()) return 2;
    DeleteFileW(configPath.c_str());
    DeleteFileW(profilesPath.c_str());

    VasioClientManager clients;
    AudioController controller(clients, configPath, profilesPath);
    if (!controller.Start()) return 3;

    ControlApiServer api(clients, controller);
    std::uint16_t port = 0;
    std::string error;
    if (!api.Start(0, port, error)) {
        std::fprintf(stderr, "API server failed to start: %s\n", error.c_str());
        controller.Stop();
        return 4;
    }

    httplib::Client client("127.0.0.1", port);
    bool passed = true;
    const auto initial = client.Get("/api/v1/application-profiles");
    passed = require(initial && initial->status == 200,
        "GET application profiles returns the configured profile list") && passed;
    if (initial && initial->status == 200) {
        const auto body = nlohmann::json::parse(initial->body);
        passed = require(body["profiles"].size() == 1 &&
            body["profiles"][0]["processName"] == "mixxx.exe" &&
            body["profiles"][0]["inputChannels"] == 255 &&
            body["profiles"][0]["outputChannels"] == 255,
            "GET exposes the default Mixxx compatibility profile") && passed;
    }

    const auto update = client.Put("/api/v1/application-profiles", R"({"profiles":[{"processName":"Mixxx.exe","inputChannels":256,"outputChannels":192}]})",
        "application/json");
    passed = require(update && update->status == 200,
        "PUT replaces application profiles") && passed;
    if (update && update->status == 200) {
        const auto body = nlohmann::json::parse(update->body);
        passed = require(body["profiles"].size() == 1 &&
            body["profiles"][0]["processName"] == "mixxx.exe" &&
            body["profiles"][0]["inputChannels"] == 256 &&
            body["profiles"][0]["outputChannels"] == 192 &&
            body["restartRequiredClients"].empty(),
            "PUT returns normalized profiles and clients requiring restart") && passed;
    }

    const auto invalid = client.Put("/api/v1/application-profiles", R"({"profiles":[{"processName":"mixxx.exe","inputChannels":257,"outputChannels":255}]})",
        "application/json");
    passed = require(invalid && invalid->status == 400,
        "PUT rejects channel counts above 256") && passed;

    const auto reset = client.Put("/api/v1/application-profiles", R"({"profiles":[]})",
        "application/json");
    passed = require(reset && reset->status == 200,
        "PUT with an empty list resets all application overrides") && passed;
    const auto afterReset = client.Get("/api/v1/application-profiles");
    if (!afterReset || afterReset->status != 200) passed = false;
    else {
        const auto body = nlohmann::json::parse(afterReset->body);
        passed = require(body["profiles"].empty(),
            "an explicitly empty profile list survives API readback") && passed;
    }

    api.Stop();
    controller.Stop();
    DeleteFileW(configPath.c_str());
    DeleteFileW(profilesPath.c_str());
    if (!passed) return 1;
    std::puts("PASS: application profile API is reachable over local HTTP.");
    return 0;
}
