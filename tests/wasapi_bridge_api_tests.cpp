#include <httplib.h>
#include <nlohmann/json.hpp>
#include "audio_controller.h"
#include "control_api_server.h"
#include "engine_diagnostics.h"
#include "vasio_client_manager.h"
#include <windows.h>
#include <cstdio>

// Disabled bridges exercise the actual HTTP/WebSocket/controller transaction
// without opening a Windows audio endpoint or depending on installed hardware.
int wmain() {
    using Json = nlohmann::json;
    wchar_t directory[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, directory)) return 2;
    const auto base = std::wstring(directory) + L"TimoxVasio-wasapi-api-" + std::to_wstring(GetCurrentProcessId());
    const auto config = base + L"-config.json", profiles = base + L"-profiles.json";
    DeleteFileW(config.c_str()); DeleteFileW(profiles.c_str());
    VasioClientManager clients;
    AudioController controller(clients, config, profiles);
    EngineDiagnostics diagnostics;
    std::string error;
    if (!controller.Start() || !diagnostics.Initialize(base + L"-logs", error)) return 3;
    ControlApiServer api(clients, controller, diagnostics, [] {});
    std::uint16_t port = 0;
    if (!api.Start(0, port, error)) return 4;
    httplib::Client http("127.0.0.1", port);
    bool passed = true;
    auto check = [&](bool condition, const char* message) {
        if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); passed = false; }
    };
    auto state = [&]() {
        const auto response = http.Get("/api/v1/state");
        return response && response->status == 200 ? Json::parse(response->body) : Json::object();
    };
    auto initial = state();
    check(initial.value("apiVersion", "") == "1.1" && initial.contains("wasapiInventoryError") &&
        initial.value("configuredWasapiBridges", Json{}) == Json::array(), "API 1.1 exposes empty bridge configuration and inventory status");
    httplib::Headers headers{{"Sec-WebSocket-Protocol", "vasio.api.v1"}};
    httplib::ws::WebSocketClient ws("ws://127.0.0.1:" + std::to_string(port) + "/api/v1/ws", headers);
    ws.set_read_timeout(2);
    const bool connected = static_cast<bool>(ws.connect());
    check(connected, "API 1.1 retains v1 WebSocket subprotocol");
    unsigned serial = 0;
    auto apply = [&](const Json& payload) {
        const auto id = "bridge-test-" + std::to_string(++serial);
        if (!ws.send(Json{{"id", id}, {"command", "configuration.apply"}, {"payload", payload}}.dump())) return Json::object();
        std::string message;
        for (int i = 0; i < 40; ++i) {
            if (ws.read(message) != httplib::ws::Text) break;
            auto reply = Json::parse(message);
            if (reply.value("id", "") == id) return reply;
        }
        return Json::object();
    };
    if (connected) {
        Json payload{{"physicalDriverId", nullptr}, {"sampleRate", 48000}, {"bufferFrames", 256},
            {"clockSource", {{"kind", "software"}}}, {"routes", Json::array()},
            {"wasapiBridges", Json::array({{{"id", "capture_a"}, {"enabled", false}, {"kind", "capture"}, {"defaultRole", "communications"}},
                {{"id", "loopback_b"}, {"enabled", false}, {"kind", "endpointLoopback"}, {"endpointId", "test-render-endpoint"}, {"bufferDuration100ns", 100000}}})}};
        check(apply(payload).value("success", false), "API accepts disabled default and explicit loopback configurations");
        const auto configured = state();
        const auto bridges = configured.value("configuredWasapiBridges", Json{});
        check(bridges.is_array() && bridges.size() == 2, "state preserves both bridge selectors");
        const auto effective = configured.value("wasapiBridges", Json::array());
        check(effective.size() == 2 && effective[0]["state"] == "disabled" && effective[0]["generationId"].is_string() &&
            effective[0]["effectiveInfo"].is_null(), "disabled bridge has status without invented device format");
        payload.erase("wasapiBridges"); payload.erase("clockSource");
        payload["expectedConfigurationRevision"] = configured.at("configurationRevision");
        check(apply(payload).value("success", false), "omitted bridge field is accepted");
        check(state().value("configuredWasapiBridges", Json{}) == bridges, "omitted bridge field preserves configuration transactionally");
        payload["wasapiBridges"] = Json::array();
        check(!apply(payload).value("success", true), "stale revision cannot remove bridges");
        check(state().value("configuredWasapiBridges", Json{}) == bridges, "stale rejection leaves bridge configuration intact");
        payload.erase("expectedConfigurationRevision");
        payload["wasapiBridges"] = Json::array({bridges[0], bridges[0]});
        check(!apply(payload).value("success", true), "duplicate bridge ids rejected by real API");
        payload["wasapiBridges"] = Json::array({{{"id", "invalid"}, {"enabled", false}, {"kind", "capture"}, {"endpointId", ""}}});
        check(!apply(payload).value("success", true), "empty explicit endpoint rejected by real API");
        check(state().value("configuredWasapiBridges", Json{}) == bridges, "malformed bridge updates leave configuration intact");
        payload["wasapiBridges"] = Json::array();
        check(apply(payload).value("success", false), "explicit empty bridges removes all bridges");
        const auto cleared = state();
        check(cleared.value("configuredWasapiBridges", Json{}) == Json::array() && cleared.value("wasapiBridges", Json{}) == Json::array(),
            "removal clears desired and effective state");
        ws.close();
    }
    const auto inventory = http.Get("/api/v1/wasapi-devices");
    check(inventory && inventory->status == 200 && Json::parse(inventory->body).contains("wasapiInventoryError"),
        "inventory refresh returns explicit success or error state");
    api.Stop(); controller.Stop();
    DeleteFileW(config.c_str()); DeleteFileW(profiles.c_str());
    return passed ? 0 : 1;
}
