#include <httplib.h>
#include <nlohmann/json.hpp>

#include "control_api_server.h"
#include "audio_controller.h"
#include "routing_graph.h"
#include "vasio_client_manager.h"
#include "engine_diagnostics.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <future>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {
using Json = nlohmann::json;
constexpr std::uint32_t kVasioId = 1;
constexpr char kSubprotocol[] = "vasio.api.v1";

std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string result(static_cast<std::size_t>(bytes), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), bytes, nullptr, nullptr) != bytes) return {};
    return result;
}

bool wide(const std::string& value, std::wstring& result) {
    if (value.empty()) { result.clear(); return true; }
    const int characters = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (characters <= 0) return false;
    result.resize(static_cast<std::size_t>(characters));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), characters) == characters;
}

bool validOrigin(const std::string& origin) {
    return origin.empty() || origin == "null" || origin == "file://" ||
           origin == "http://localhost:3000" || origin == "http://127.0.0.1:3000";
}

bool offersSubprotocol(const httplib::Request& request) {
    const auto value = request.get_header_value("Sec-WebSocket-Protocol");
    std::size_t begin = 0;
    while (begin < value.size()) {
        const auto comma = value.find(',', begin);
        const auto end = comma == std::string::npos ? value.size() : comma;
        const auto first = value.find_first_not_of(" \t", begin);
        if (first != std::string::npos && first < end) {
            const auto last = value.find_last_not_of(" \t", end - 1);
            if (value.substr(first, last - first + 1) == kSubprotocol) return true;
        }
        if (comma == std::string::npos) break;
        begin = comma + 1;
    }
    return false;
}

void setCors(const httplib::Request& request, httplib::Response& response) {
    const auto origin = request.get_header_value("Origin");
    if (!origin.empty()) {
        response.set_header("Access-Control-Allow-Origin", origin);
        response.set_header("Vary", "Origin");
    }
}

Json errorEnvelope(const std::string& id, const char* code, const std::string& message,
                   const std::string& operation = {}) {
    Json error{{"code", code}, {"message", message}};
    if (!operation.empty()) error["operation"] = operation;
    return Json{{"id", id}, {"success", false}, {"error", std::move(error)}};
}

Json statusEvent(const AudioControllerSnapshot& snapshot) {
    Json physicalId = snapshot.physicalDriverId ? Json(*snapshot.physicalDriverId) : Json(nullptr);
    Json sampleRate = snapshot.sampleRate ? Json(*snapshot.sampleRate) : Json(nullptr);
    Json bufferFrames = snapshot.bufferFrames ? Json(*snapshot.bufferFrames) : Json(nullptr);
    Json lastError = snapshot.lastError.empty() ? Json(nullptr) :
        Json{{"code", "AUDIO_CONFIGURATION_FAILED"}, {"message", snapshot.lastError}, {"operation", "apply"}};
    return Json{{"event", "engine.status"},
        {"payload", {{"state", snapshot.state}, {"physicalDriverId", std::move(physicalId)},
                      {"sampleRate", std::move(sampleRate)}, {"bufferFrames", std::move(bufferFrames)},
                      {"lastError", std::move(lastError)}}}};
}

Json routeJson(const AudioRoute& route) {
    return Json{{"id", route.id}, {"sourceEndpointId", route.sourceEndpointId},
        {"destinationEndpointId", route.destinationEndpointId}, {"gainDb", route.gainDb},
        {"mute", route.mute}};
}

Json applicationProfileJson(const ApplicationProfile& profile) {
    return Json{{"processName", utf8(profile.processName)},
        {"inputChannels", profile.inputChannels}, {"outputChannels", profile.outputChannels}};
}

Json applicationProfilesJson(const std::vector<ApplicationProfile>& profiles) {
    Json result = Json::array();
    for (const auto& profile : profiles) result.push_back(applicationProfileJson(profile));
    return Json{{"profiles", std::move(result)}};
}

Json endpoint(const std::string& id, const std::string& name,
              const char* direction, long channel) {
    return Json{{"id", id}, {"name", name}, {"direction", direction}, {"channel", channel}};
}

bool matchesSsl12ChannelLayout(const PhysicalAsioCapabilities& capabilities) {
    if (capabilities.inputChannels != 16 || capabilities.outputChannels != 8 ||
        capabilities.inputChannelNames.size() != 16 || capabilities.outputChannelNames.size() != 8)
        return false;

    for (std::size_t channel = 0; channel < 4; ++channel) {
        if (capabilities.inputChannelNames[channel] != L"Analogue " + std::to_wstring(channel + 1))
            return false;
    }
    if (capabilities.inputChannelNames[4] != L"Talkback" ||
        capabilities.inputChannelNames[5] != L"Talkback" ||
        capabilities.inputChannelNames[6] != L"Loopback L" ||
        capabilities.inputChannelNames[7] != L"Loopback R")
        return false;
    for (std::size_t channel = 8; channel < 16; ++channel) {
        if (capabilities.inputChannelNames[channel] != L"ADAT " + std::to_wstring(channel - 7))
            return false;
    }
    if (capabilities.outputChannelNames[0] != L"Mon L" ||
        capabilities.outputChannelNames[1] != L"Mon R")
        return false;
    for (std::size_t channel = 2; channel < 8; ++channel) {
        if (capabilities.outputChannelNames[channel] != L"Out " + std::to_wstring(channel + 1))
            return false;
    }
    return true;
}

std::string physicalOutputChannelName(const PhysicalAsioDriverInfo& driver,
                                      const PhysicalAsioCapabilities& capabilities,
                                      long channel, const std::string& asioName) {
    static constexpr std::array<const char*, 8> ssl12Names{
        "Monitor L", "Monitor R", "Line 3", "Line 4",
        "Headphone A L", "Headphone A R", "Headphone B L", "Headphone B R"
    };
    const bool sslAsioSlot = driver.name.rfind("SSL ASIO Driver ", 0) == 0;
    if (sslAsioSlot && matchesSsl12ChannelLayout(capabilities) && channel >= 1 && channel <= 8)
        return ssl12Names[static_cast<std::size_t>(channel - 1)];
    return asioName;
}

Json meterEvent(const AudioMeterSnapshot& meter) {
    const double peakDbfs = meter.peakLinear > 0.0f
        ? std::max(-120.0, 20.0 * std::log10(static_cast<double>(meter.peakLinear)))
        : -120.0;
    return Json{{"event", "audio.meter"},
        {"payload", {{"endpointId", meter.endpointId}, {"peakDbfs", peakDbfs},
            {"underruns", meter.underruns}, {"overruns", meter.overruns}}}};
}
}

struct ControlApiServer::Impl {
    VasioClientManager& clients;
    AudioController& controller;
    EngineDiagnostics& diagnostics;
    std::function<void()> stopRequest;
    std::vector<PhysicalAsioDriverInfo> physicalInventory;
    httplib::Server server;
    std::atomic<bool> running{false};
    bool configured = false;
    std::thread listenerThread;
    std::uint16_t port = 0;

    Impl(VasioClientManager& manager, AudioController& audioController, EngineDiagnostics& engineDiagnostics,
         std::function<void()> requestStop)
        : clients(manager), controller(audioController), diagnostics(engineDiagnostics), stopRequest(std::move(requestStop)) {}
    ~Impl() { Stop(); }

    Json physicalDriversJson() const {
        Json result = Json::array();
        const auto state = controller.Snapshot();
        for (const auto& driver : physicalInventory) {
            const auto id = utf8(driver.id);
            const bool selected = state.state != "error" && state.physicalDriverId &&
                *state.physicalDriverId == id && state.sampleRate && state.bufferFrames;
            Json inputs = Json::array();
            Json outputs = Json::array();
            Json rates = Json::array();
            Json bufferSizes = nullptr;
            if (selected) {
                for (long channel = 1; channel <= state.physicalCapabilities.inputChannels; ++channel) {
                    const auto index = static_cast<std::size_t>(channel - 1);
                    const auto channelName = index < state.physicalCapabilities.inputChannelNames.size()
                        ? utf8(state.physicalCapabilities.inputChannelNames[index]) : std::string{};
                    inputs.push_back(endpoint("physical:" + id + ":input:" + std::to_string(channel),
                        channelName.empty() ? driver.name + " In " + std::to_string(channel)
                            : driver.name + " · " + channelName, "input", channel));
                }
                for (long channel = 1; channel <= state.physicalCapabilities.outputChannels; ++channel) {
                    const auto index = static_cast<std::size_t>(channel - 1);
                    const auto channelName = index < state.physicalCapabilities.outputChannelNames.size()
                        ? utf8(state.physicalCapabilities.outputChannelNames[index]) : std::string{};
                    const auto resolvedName = physicalOutputChannelName(driver,
                        state.physicalCapabilities, channel, channelName);
                    outputs.push_back(endpoint("physical:" + id + ":output:" + std::to_string(channel),
                        resolvedName.empty() ? driver.name + " Out " + std::to_string(channel)
                            : driver.name + " · " + resolvedName, "output", channel));
                }
                for (const auto rate : state.physicalCapabilities.supportedSampleRates)
                    rates.push_back(static_cast<std::uint32_t>(rate));
                bufferSizes = {{"minimumFrames", state.physicalCapabilities.minBufferFrames},
                    {"maximumFrames", state.physicalCapabilities.maxBufferFrames},
                    {"preferredFrames", state.physicalCapabilities.preferredBufferFrames},
                    {"granularity", state.physicalCapabilities.bufferGranularity}};
            }
            result.push_back({{"id", id}, {"name", driver.name},
                {"inputEndpoints", std::move(inputs)}, {"outputEndpoints", std::move(outputs)},
                {"sampleRates", std::move(rates)}, {"bufferSizes", std::move(bufferSizes)},
                {"capabilitiesKnown", selected}});
        }
        return result;
    }

    Json virtualDriversJson() const {
        const auto attachedClients = clients.GetClientSnapshots();
        const auto state = controller.Snapshot();
        Json clientArray = Json::array();
        Json inputs = Json::array();
        Json outputs = Json::array();
        for (const auto& client : attachedClients) {
            if (client.info.driverId != kVasioId || !client.mapping) continue;
            AudioClientMapping::ChannelMask activeInputs{};
            AudioClientMapping::ChannelMask activeOutputs{};
            if (!client.mapping->GetActiveChannels(activeInputs, activeOutputs)) continue;
            Json inputChannels = Json::array();
            Json outputChannels = Json::array();
            const auto prefix = "virtual:TimoxVasio:" + std::to_string(client.info.processId) + ":";
            for (std::uint32_t channel = 0; channel < AudioClientMapping::kChannelCount; ++channel) {
                const auto apiChannel = static_cast<long>(channel + 1);
                const auto mask = std::uint64_t{1} << (channel % 64);
                if (activeInputs[channel / 64] & mask) {
                    inputChannels.push_back(apiChannel);
                    inputs.push_back(endpoint(prefix + "input:" + std::to_string(apiChannel),
                        "TimoxVasio " + utf8(client.info.processName) + " In " + std::to_string(apiChannel),
                        "input", apiChannel));
                }
                if (activeOutputs[channel / 64] & mask) {
                    outputChannels.push_back(apiChannel);
                    outputs.push_back(endpoint(prefix + "output:" + std::to_string(apiChannel),
                        "TimoxVasio " + utf8(client.info.processName) + " Out " + std::to_string(apiChannel),
                        "output", apiChannel));
                }
            }
            clientArray.push_back({{"pid", client.info.processId},
                {"processName", utf8(client.info.processName)},
                {"inputChannels", std::move(inputChannels)},
                {"outputChannels", std::move(outputChannels)}});
        }
        Json rate = state.sampleRate ? Json(*state.sampleRate) : Json(nullptr);
        Json bufferFrames = state.bufferFrames ? Json(*state.bufferFrames) : Json(nullptr);
        return Json::array({{{"id", "TimoxVasio"}, {"channelsPerDirection", AudioClientMapping::kChannelCount},
            {"sampleRate", std::move(rate)}, {"bufferFrames", std::move(bufferFrames)},
            {"clients", std::move(clientArray)}, {"inputEndpoints", std::move(inputs)},
            {"outputEndpoints", std::move(outputs)}}});
    }

    Json driversJson() const {
        return Json{{"physicalDrivers", physicalDriversJson()}, {"virtualDrivers", virtualDriversJson()}};
    }

    Json stateJson() const {
        const auto state = controller.Snapshot();
        Json physicalId = state.physicalDriverId ? Json(*state.physicalDriverId) : Json(nullptr);
        Json sampleRate = state.sampleRate ? Json(*state.sampleRate) : Json(nullptr);
        Json bufferFrames = state.bufferFrames ? Json(*state.bufferFrames) : Json(nullptr);
        Json lastError = state.lastError.empty() ? Json(nullptr) :
            Json{{"code", "AUDIO_CONFIGURATION_FAILED"}, {"message", state.lastError}, {"operation", "apply"}};
        Json routes = Json::array();
        for (const auto& route : state.routes) routes.push_back(routeJson(route));
        return Json{{"apiVersion", "1.0"},
            {"engine", {{"state", state.state}, {"physicalDriverId", std::move(physicalId)},
                         {"sampleRate", std::move(sampleRate)}, {"bufferFrames", std::move(bufferFrames)},
                         {"lastError", std::move(lastError)}}},
            {"physicalDrivers", physicalDriversJson()}, {"virtualDrivers", virtualDriversJson()},
            {"routes", std::move(routes)}};
    }

    Json applyCommand(const Json& request) {
        std::string id;
        if (request.is_object() && request.contains("id") && request["id"].is_string())
            id = request["id"].get<std::string>();
        if (id.empty() || id.size() > 128 || !request.is_object() ||
            !request.contains("command") || !request["command"].is_string())
            return errorEnvelope(id, "INVALID_CONFIGURATION", "Invalid request envelope", "validate");
        diagnostics.Write(EngineDiagnostics::Level::Debug, "api", "Received command " + request["command"].get<std::string>());
        if (request["command"] == "engine.stop") {
            if (request.size() != 2)
                return errorEnvelope(id, "INVALID_ENGINE_COMMAND", "engine.stop accepts only an id and command", "validate");
            if (!clients.TryReserveShutdown())
            {
                diagnostics.Write(EngineDiagnostics::Level::Warning, "engine", "Stop refused because VASIO clients are still attached");
                return errorEnvelope(id, "ENGINE_CLIENTS_CONNECTED", "Stop TimoxVasio clients before stopping the engine", "stop");
            }
            diagnostics.Write(EngineDiagnostics::Level::Info, "engine", "Ordered stop requested through the local API");
            return Json{{"id", id}, {"success", true}, {"result", {{"accepted", true}}}};
        }
        if (request["command"] != "configuration.apply")
            return errorEnvelope(id, "INVALID_CONFIGURATION", "Unsupported command", "validate");
        if (!request.contains("payload") || !request["payload"].is_object())
            return errorEnvelope(id, "INVALID_CONFIGURATION", "Payload must be an object", "validate");
        const auto& payload = request["payload"];
        if (!payload.contains("physicalDriverId") ||
            !(payload["physicalDriverId"].is_null() || payload["physicalDriverId"].is_string()) ||
            !payload.contains("sampleRate") || !(payload["sampleRate"].is_null() ||
                payload["sampleRate"].is_number_integer()) ||
            !payload.contains("bufferFrames") || !(payload["bufferFrames"].is_null() ||
                payload["bufferFrames"].is_number_integer()) ||
            !payload.contains("routes") || !payload["routes"].is_array())
            return errorEnvelope(id, "INVALID_CONFIGURATION",
                "Payload must contain the physical selection, sample rate, buffer size and routes", "validate");
        if (payload.size() != 4)
            return errorEnvelope(id, "INVALID_CONFIGURATION", "Unexpected configuration field", "validate");

        AudioControllerConfiguration configuration;
        if (payload["physicalDriverId"].is_string())
            configuration.physicalDriverId = payload["physicalDriverId"].get<std::string>();
        if (!payload["sampleRate"].is_null()) {
            const auto rate = payload["sampleRate"].get<std::int64_t>();
            if (rate <= 0 || rate > 768000)
                return errorEnvelope(id, "INVALID_CONFIGURATION", "Invalid physical sample rate", "validate");
            configuration.sampleRate = static_cast<std::uint32_t>(rate);
        }
        if (!payload["bufferFrames"].is_null()) {
            const auto frames = payload["bufferFrames"].get<std::int64_t>();
            if (frames <= 0 || frames > 65536)
                return errorEnvelope(id, "INVALID_CONFIGURATION", "Invalid physical buffer size", "validate");
            configuration.bufferFrames = static_cast<std::uint32_t>(frames);
        }

        configuration.routes.reserve(payload["routes"].size());
        for (const auto& item : payload["routes"]) {
            if (!item.is_object() || item.size() != 5 || !item.contains("id") || !item["id"].is_string() ||
                !item.contains("sourceEndpointId") || !item["sourceEndpointId"].is_string() ||
                !item.contains("destinationEndpointId") || !item["destinationEndpointId"].is_string() ||
                !item.contains("gainDb") || !item["gainDb"].is_number() ||
                !item.contains("mute") || !item["mute"].is_boolean())
                return errorEnvelope(id, "INVALID_CONFIGURATION", "Invalid route entry", "validate");
            configuration.routes.push_back({item["id"].get<std::string>(),
                item["sourceEndpointId"].get<std::string>(),
                item["destinationEndpointId"].get<std::string>(),
                item["gainDb"].get<double>(), item["mute"].get<bool>()});
        }

        std::unordered_map<std::string, RoutingEndpointType> endpointTypes;
        const auto inventory = driversJson();
        for (const auto& driver : inventory["physicalDrivers"]) {
            for (const auto& item : driver["inputEndpoints"])
                endpointTypes.emplace(item["id"].get<std::string>(), RoutingEndpointType::PhysicalInput);
            for (const auto& item : driver["outputEndpoints"])
                endpointTypes.emplace(item["id"].get<std::string>(), RoutingEndpointType::PhysicalOutput);
        }
        for (const auto& driver : inventory["virtualDrivers"]) {
            for (const auto& item : driver["inputEndpoints"])
                endpointTypes.emplace(item["id"].get<std::string>(), RoutingEndpointType::VirtualInput);
            for (const auto& item : driver["outputEndpoints"])
                endpointTypes.emplace(item["id"].get<std::string>(), RoutingEndpointType::VirtualOutput);
        }
        for (const auto& route : configuration.routes) {
            const auto source = endpointTypes.find(route.sourceEndpointId);
            const auto destination = endpointTypes.find(route.destinationEndpointId);
            if (source == endpointTypes.end() || destination == endpointTypes.end())
                return errorEnvelope(id, "UNKNOWN_ENDPOINT",
                    "A route endpoint is not present in the current API inventory", "validate");
            if (!RoutingGraph::SupportsLink(source->second, destination->second))
                return errorEnvelope(id, "INVALID_ROUTE_DIRECTION",
                    "The route direction is not one of the supported ASIO connections", "validate");
        }

        const auto applied = controller.ApplyConfiguration(configuration);
        if (!applied.success) {
            diagnostics.Write(EngineDiagnostics::Level::Error, "audio", applied.error);
            return errorEnvelope(id, "AUDIO_CONFIGURATION_FAILED", applied.error, "apply");
        }
        diagnostics.Write(EngineDiagnostics::Level::Info, "audio", "Audio configuration applied with " + std::to_string(configuration.routes.size()) + " routes");
        const auto confirmed = controller.Snapshot();
        if (confirmed.physicalDriverId && confirmed.sampleRate && confirmed.bufferFrames)
            diagnostics.Write(EngineDiagnostics::Level::Info, "audio",
                "Physical driver " + *confirmed.physicalDriverId + " configured at " +
                std::to_string(*confirmed.sampleRate) + " Hz, " +
                std::to_string(*confirmed.bufferFrames) + " frames");
        return Json{{"id", id}, {"success", true}, {"result", {{"accepted", true}}}};
    }

    void configureApplicationProfileRoutes() {
        server.Get("/api/v1/application-profiles", [this](const httplib::Request& request,
                                                           httplib::Response& response) {
            setCors(request, response);
            const auto snapshot = controller.GetApplicationProfiles();
            if (!snapshot.error.empty()) {
                response.status = 500;
                response.set_content(Json{{"code", "APPLICATION_PROFILE_STORE_FAILED"},
                    {"message", snapshot.error}}.dump(), "application/json; charset=utf-8");
                return;
            }
            response.set_content(applicationProfilesJson(snapshot.profiles).dump(),
                "application/json; charset=utf-8");
        });
        server.Put("/api/v1/application-profiles", [this](const httplib::Request& request,
                                                           httplib::Response& response) {
            setCors(request, response);
            try {
                const auto body = Json::parse(request.body);
                if (!body.is_object() || body.size() != 1 || !body.contains("profiles") ||
                    !body["profiles"].is_array() || body["profiles"].size() > 1024) {
                    response.status = 400;
                    response.set_content(Json{{"code", "INVALID_APPLICATION_PROFILES"},
                        {"message", "Expected an object containing at most 1024 application profiles"}}.dump(),
                        "application/json; charset=utf-8");
                    return;
                }
                std::vector<ApplicationProfile> profiles;
                profiles.reserve(body["profiles"].size());
                for (const auto& item : body["profiles"]) {
                    if (!item.is_object() || item.size() != 3 || !item.contains("processName") ||
                        !item["processName"].is_string() || !item.contains("inputChannels") ||
                        !item["inputChannels"].is_number_integer() || !item.contains("outputChannels") ||
                        !item["outputChannels"].is_number_integer()) {
                        response.status = 400;
                        response.set_content(Json{{"code", "INVALID_APPLICATION_PROFILES"},
                            {"message", "Each profile requires processName, inputChannels and outputChannels"}}.dump(),
                            "application/json; charset=utf-8");
                        return;
                    }
                    const auto inputChannels = item["inputChannels"].get<std::int64_t>();
                    const auto outputChannels = item["outputChannels"].get<std::int64_t>();
                    std::wstring processName;
                    if (inputChannels < 1 || inputChannels > 256 || outputChannels < 1 ||
                        outputChannels > 256 || !wide(item["processName"].get<std::string>(), processName)) {
                        response.status = 400;
                        response.set_content(Json{{"code", "INVALID_APPLICATION_PROFILES"},
                            {"message", "Profile names must be UTF-8 executable basenames and channel counts must be 1 to 256"}}.dump(),
                            "application/json; charset=utf-8");
                        return;
                    }
                    profiles.push_back({std::move(processName),
                        static_cast<std::uint32_t>(inputChannels),
                        static_cast<std::uint32_t>(outputChannels)});
                }
                std::string validationError;
                if (!ApplicationProfileStore::NormalizeAndValidate(profiles, validationError)) {
                    response.status = 400;
                    response.set_content(Json{{"code", "INVALID_APPLICATION_PROFILES"},
                        {"message", validationError}}.dump(), "application/json; charset=utf-8");
                    return;
                }
                const auto updated = controller.ReplaceApplicationProfiles(std::move(profiles));
                if (!updated.success) {
                    response.status = 500;
                    response.set_content(Json{{"code", "APPLICATION_PROFILE_STORE_FAILED"},
                        {"message", updated.error}}.dump(), "application/json; charset=utf-8");
                    return;
                }
                Json clientsRequiringRestart = Json::array();
                for (const auto& client : updated.restartRequiredClients) {
                    clientsRequiringRestart.push_back({{"pid", client.processId},
                        {"processName", utf8(client.processName)}});
                }
                Json result = applicationProfilesJson(updated.profiles);
                result["restartRequiredClients"] = std::move(clientsRequiringRestart);
                response.set_content(result.dump(), "application/json; charset=utf-8");
            } catch (const Json::exception&) {
                response.status = 400;
                response.set_content(Json{{"code", "INVALID_APPLICATION_PROFILES"},
                    {"message", "Request body is not valid JSON"}}.dump(), "application/json; charset=utf-8");
            }
        });
    }

    void configureRoutes() {
        if (configured) return;
        configured = true;
        server.set_payload_max_length(1024 * 1024);
        server.Get("/api/v1/state", [this](const httplib::Request& request, httplib::Response& response) {
            setCors(request, response);
            response.set_content(stateJson().dump(), "application/json; charset=utf-8");
        });
        server.Get("/api/v1/drivers", [this](const httplib::Request& request, httplib::Response& response) {
            setCors(request, response);
            response.set_content(driversJson().dump(), "application/json; charset=utf-8");
        });
        server.Get("/api/v1/diagnostics", [this](const httplib::Request& request, httplib::Response& response) {
            setCors(request, response);
            std::size_t limit = 200;
            if (request.has_param("limit")) {
                try {
                    const auto raw = request.get_param_value("limit");
                    std::size_t consumed = 0;
                    const auto parsed = std::stoul(raw, &consumed);
                    if (consumed != raw.size() || parsed < 1 || parsed > 500) throw std::invalid_argument("limit");
                    limit = parsed;
                } catch (...) {
                    response.status = 400;
                    response.set_content(Json{{"code", "INVALID_LIMIT"}, {"message", "limit must be between 1 and 500"}}.dump(), "application/json; charset=utf-8");
                    return;
                }
            }
            Json entries = Json::array();
            for (const auto& entry : diagnostics.ReadRecent(limit))
                entries.push_back({{"timestamp", entry.timestamp}, {"level", entry.level},
                    {"component", entry.component}, {"message", entry.message}});
            response.set_content(Json{{"level", EngineDiagnostics::LevelName(diagnostics.GetLevel())},
                {"entries", std::move(entries)}}.dump(), "application/json; charset=utf-8");
        });
        server.Put("/api/v1/diagnostics", [this](const httplib::Request& request, httplib::Response& response) {
            setCors(request, response);
            try {
                const auto body = Json::parse(request.body);
                EngineDiagnostics::Level level{};
                if (!body.is_object() || body.size() != 1 || !body.contains("level") ||
                    !body["level"].is_string() || !EngineDiagnostics::ParseLevel(body["level"].get<std::string>(), level)) {
                    response.status = 400;
                    response.set_content(Json{{"code", "INVALID_DIAGNOSTIC_LEVEL"}, {"message", "level must be info or debug"}}.dump(), "application/json; charset=utf-8");
                    return;
                }
                if (level != EngineDiagnostics::Level::Info && level != EngineDiagnostics::Level::Debug) {
                    response.status = 400;
                    response.set_content(Json{{"code", "INVALID_DIAGNOSTIC_LEVEL"}, {"message", "level must be info or debug"}}.dump(), "application/json; charset=utf-8");
                    return;
                }
                std::string error;
                if (!diagnostics.SetLevel(level, error)) {
                    response.status = 500;
                    response.set_content(Json{{"code", "DIAGNOSTICS_STORE_FAILED"}, {"message", error}}.dump(), "application/json; charset=utf-8");
                    return;
                }
                diagnostics.Write(EngineDiagnostics::Level::Info, "diagnostics", "Log level changed to " + std::string(EngineDiagnostics::LevelName(level)));
                response.set_content(Json{{"level", EngineDiagnostics::LevelName(level)}}.dump(), "application/json; charset=utf-8");
            } catch (const Json::exception&) {
                response.status = 400;
                response.set_content(Json{{"code", "INVALID_DIAGNOSTIC_LEVEL"}, {"message", "Request body is not valid JSON"}}.dump(), "application/json; charset=utf-8");
            }
        });
        configureApplicationProfileRoutes();
        const auto optionsHandler = [](const httplib::Request& request, httplib::Response& response) {
            setCors(request, response);
            response.set_header("Access-Control-Allow-Methods", "GET, PUT, OPTIONS");
            response.set_header("Access-Control-Allow-Headers", "Content-Type");
            response.status = 204;
        };
        server.Options("/api/v1/state", optionsHandler);
        server.Options("/api/v1/drivers", optionsHandler);
        server.Options("/api/v1/application-profiles", optionsHandler);
        server.Options("/api/v1/diagnostics", optionsHandler);
        server.set_pre_request_handler([](const httplib::Request& request, httplib::Response& response) {
            const auto origin = request.get_header_value("Origin");
            if (!validOrigin(origin)) {
                response.status = 403;
                return httplib::Server::HandlerResponse::Handled;
            }
            if (request.matched_route == "/api/v1/ws" && !offersSubprotocol(request)) {
                response.status = 400;
                response.set_content("Missing WebSocket subprotocol vasio.api.v1", "text/plain; charset=utf-8");
                return httplib::Server::HandlerResponse::Handled;
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });
        server.WebSocket("/api/v1/ws",
            [this](const httplib::Request&, httplib::ws::WebSocket& websocket) {
                websocket.set_read_timeout(std::chrono::milliseconds(500));
                auto initialState = controller.Snapshot();
                websocket.send(statusEvent(initialState).dump());
                auto lastRevision = initialState.revision;
                const auto sendAudioMeters = [&] {
                    for (const auto& meter : controller.ReadMeters())
                        if (!websocket.send(meterEvent(meter).dump())) return false;
                    return true;
                };
                const auto sendStatusChanges = [&] {
                    for (const auto& change : controller.EventsSince(lastRevision)) {
                        if (!websocket.send(statusEvent(change).dump())) return false;
                        lastRevision = change.revision;
                    }
                    return true;
                };
                auto devices = driversJson();
                auto lastDevices = devices.dump();
                websocket.send(Json{{"event", "devices.changed"}, {"payload", devices}}.dump());
                while (websocket.is_open()) {
                    std::string message;
                    const auto result = websocket.read(message);
                    if (result == httplib::ws::Timeout) {
                        if (!sendStatusChanges()) break;
                        if (!sendAudioMeters()) break;
                        devices = driversJson();
                        const auto currentDevices = devices.dump();
                        if (currentDevices != lastDevices) {
                            lastDevices = currentDevices;
                            if (!websocket.send(Json{{"event", "devices.changed"},
                                {"payload", devices}}.dump())) break;
                        }
                        continue;
                    }
                    if (result != httplib::ws::Text) break;
                    Json request;
                    Json reply;
                    try {
                        request = Json::parse(message);
                        auto pending = std::async(std::launch::async,
                            [this, request] { return applyCommand(request); });
                        while (pending.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready) {
                            if (!sendStatusChanges()) return;
                        }
                        reply = pending.get();
                    }
                    catch (const Json::exception&) {
                        reply = errorEnvelope("", "INVALID_CONFIGURATION", "Malformed JSON command", "parse");
                    }
                    if (!websocket.send(reply.dump())) break;
                    if (reply.is_object() && reply.value("success", false) && request.is_object() &&
                        request.value("command", std::string{}) == "engine.stop") {
                        if (stopRequest) stopRequest();
                        break;
                    }
                    if (!sendStatusChanges()) break;
                    if (reply.is_object() && reply.value("success", false)) {
                        Json routes = stateJson()["routes"];
                        if (!websocket.send(Json{{"event", "routes.changed"},
                            {"payload", {{"routes", std::move(routes)}}}}.dump())) break;
                    } else if (controller.Snapshot().state == "error") {
                        const auto state = controller.Snapshot();
                        if (!websocket.send(Json{{"event", "engine.error"},
                            {"payload", {{"code", "AUDIO_CONFIGURATION_FAILED"},
                                {"message", state.lastError}, {"operation", "apply"}}}}.dump())) break;
                    }
                }
            },
            [](const std::vector<std::string>& protocols) {
                return std::find(protocols.begin(), protocols.end(), kSubprotocol) == protocols.end()
                    ? std::string{} : std::string{kSubprotocol};
            });
    }

    bool Start(std::uint16_t requestedPort, std::uint16_t& actualPort, std::string& error) {
        if (running.load()) { actualPort = port; return true; }
        configureRoutes();
        try { physicalInventory = controller.EnumeratePhysicalDrivers(); }
        catch (const std::exception& exception) { error = exception.what(); return false; }
        const int selectedPort = requestedPort == 0
            ? server.bind_to_any_port("127.0.0.1")
            : (server.bind_to_port("127.0.0.1", requestedPort) ? requestedPort : -1);
        if (selectedPort < 0) {
            error = "cpp-httplib could not bind the local API port " + std::to_string(requestedPort);
            return false;
        }
        port = static_cast<std::uint16_t>(selectedPort);
        running.store(true);
        try {
            listenerThread = std::thread([this] {
                if (!server.listen_after_bind()) running.store(false);
            });
        } catch (...) {
            running.store(false);
            server.stop();
            error = "Unable to start the cpp-httplib listener thread";
            return false;
        }
        actualPort = port;
        return true;
    }

    void Stop() noexcept {
        if (!running.exchange(false) && !listenerThread.joinable()) return;
        server.stop();
        if (listenerThread.joinable() && listenerThread.get_id() != std::this_thread::get_id())
            listenerThread.join();
        port = 0;
    }
};

ControlApiServer::ControlApiServer(VasioClientManager& clients, AudioController& controller, EngineDiagnostics& diagnostics,
    std::function<void()> stopRequest)
    : impl_(std::make_unique<Impl>(clients, controller, diagnostics, std::move(stopRequest))) {}
ControlApiServer::~ControlApiServer() = default;

bool ControlApiServer::Start(std::uint16_t requestedPort, std::uint16_t& boundPort, std::string& error) {
    return impl_->Start(requestedPort, boundPort, error);
}

void ControlApiServer::Stop() noexcept {
    if (impl_) impl_->Stop();
}
