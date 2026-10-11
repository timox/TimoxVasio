#include <winsock2.h>

#include "audio_transport.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

using Json = nlohmann::json;

namespace {
struct Options {
    bool json = false;
    bool once = false;
    int seconds = 0;
    int intervalMs = 1000;
    int apiPort = 52525;
    std::string history;
};

struct Client {
    std::uint32_t pid = 0;
    std::string process;
    AudioClientMapping::StatusSnapshot status;
};

std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
        result.data(), length, nullptr, nullptr);
    return result;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool parse(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--json") options.json = true;
        else if (arg == "--once") options.once = true;
        else if (arg == "--history" && i + 1 < argc) options.history = argv[++i];
        else if (arg == "--seconds" && i + 1 < argc) options.seconds = std::stoi(argv[++i]);
        else if (arg == "--interval-ms" && i + 1 < argc) options.intervalMs = std::stoi(argv[++i]);
        else if (arg == "--api-port" && i + 1 < argc) options.apiPort = std::stoi(argv[++i]);
        else return false;
    }
    return options.seconds >= 0 && options.intervalMs >= 100 && options.apiPort > 0 &&
        options.apiPort <= 65535;
}

std::vector<Client> clients(std::set<std::uint32_t>& alivePids,
    bool& processSnapshotAvailable) {
    std::vector<Client> result;
    HANDLE list = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (list == INVALID_HANDLE_VALUE) return result;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(list, &entry)) {
        processSnapshotAvailable = true;
        do {
            alivePids.insert(entry.th32ProcessID);
            AudioClientMapping::StatusSnapshot status;
            if (AudioClientMapping::ReadStatusReadOnly(1, entry.th32ProcessID, status))
                result.push_back({entry.th32ProcessID, utf8(entry.szExeFile), std::move(status)});
        } while (Process32NextW(list, &entry));
    }
    CloseHandle(list);
    return result;
}

Json engineState(int port) {
    httplib::Client api("127.0.0.1", port);
    api.set_connection_timeout(0, 300000);
    api.set_read_timeout(0, 500000);
    auto response = api.Get("/api/v1/state");
    if (!response || response->status != 200) return nullptr;
    return Json::parse(response->body, nullptr, false);
}

std::vector<int> channelNumbers(const AudioClientMapping::ChannelMask& mask) {
    std::vector<int> result;
    for (std::size_t word = 0; word < mask.size(); ++word)
        for (int bit = 0; bit < 64; ++bit)
            if (mask[word] & (std::uint64_t{1} << bit))
                result.push_back(static_cast<int>(word * 64 + bit + 1));
    return result;
}

std::string stateName(AudioClientMapping::ClientState state) {
    switch (state) {
    case AudioClientMapping::ClientState::Initialized: return "initialized";
    case AudioClientMapping::ClientState::BuffersReady: return "buffers-ready";
    case AudioClientMapping::ClientState::Running: return "running";
    case AudioClientMapping::ClientState::Stopped: return "stopped";
    case AudioClientMapping::ClientState::Detached: return "detached";
    }
    return "unknown";
}

std::uint64_t errorTotal(const AudioClientMapping::StatusSnapshot& s) {
    return s.inputUnderruns + s.outputOverruns + s.inputOverruns + s.outputUnderruns;
}

void alert(Json& alerts, const std::string& code, const std::string& text,
    std::uint32_t pid = 0) {
    alerts.push_back({{"severity", "red"}, {"code", code}, {"message", text}, {"pid", pid}});
}

Json snapshot(const std::vector<Client>& active, const Json& engine,
    const std::map<std::uint32_t, Client>& previous,
    const std::set<std::uint32_t>& alivePids, bool processSnapshotAvailable) {
    LARGE_INTEGER now{};
    LARGE_INTEGER frequency{};
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    Json rows = Json::array();
    Json alerts = Json::array();
    std::set<std::uint32_t> seen;
    std::set<std::uint32_t> rates;
    std::set<std::uint32_t> blocks;
    const bool apiAvailable = engine.is_object();
    Json routes = Json::array();
    if (apiAvailable && engine.contains("routes") && engine["routes"].is_array())
        routes = engine["routes"];
    for (const auto& client : active) {
        const auto& s = client.status;
        seen.insert(client.pid);
        if (s.confirmedSampleRate) rates.insert(s.confirmedSampleRate);
        if (s.confirmedBufferFrames) blocks.insert(s.confirmedBufferFrames);
        const auto inputs = channelNumbers(s.activeInputs);
        const auto outputs = channelNumbers(s.activeOutputs);
        Json unwritten = Json::array();
        if (apiAvailable) {
            for (int channel : inputs) {
                const std::string destination = lower("virtual:TimoxVasio:" +
                    std::to_string(client.pid) + ":input:" + std::to_string(channel));
                bool writer = false;
                for (const auto& route : routes) {
                    if (route.is_object() && route.contains("destinationEndpointId") &&
                        route["destinationEndpointId"].is_string() &&
                        lower(route["destinationEndpointId"].get<std::string>()) == destination) {
                        writer = true;
                        break;
                    }
                }
                if (!writer) unwritten.push_back(channel);
            }
            if (!unwritten.empty())
                alert(alerts, "UNWRITTEN_INPUT", client.process + " reads inputs without a routed writer",
                    client.pid);
        }
        if (s.available && s.requestedSampleRate && s.confirmedSampleRate &&
            s.requestedSampleRate != s.confirmedSampleRate)
            alert(alerts, "RATE_DIVERGENCE", client.process + " requested a different sample rate",
                client.pid);
        if (s.available && s.requestedBufferFrames && s.confirmedBufferFrames &&
            s.requestedBufferFrames != s.confirmedBufferFrames)
            alert(alerts, "BUFFER_DIVERGENCE", client.process + " requested a different buffer size",
                client.pid);
        const auto prior = previous.find(client.pid);
        if (prior != previous.end() && errorTotal(s) > errorTotal(prior->second.status))
            alert(alerts, "ERRORS_INCREASING", client.process + " transport errors increased", client.pid);
        if (!s.stableExchange)
            alert(alerts, "UNSTABLE_SNAPSHOT", client.process + " status changed during every bounded read attempt", client.pid);
        if (!s.stableChannels)
            alert(alerts, "UNSTABLE_CHANNELS", client.process + " channel mask changed during every bounded read attempt", client.pid);
        Json expectedMs = nullptr;
        Json measuredMs = nullptr;
        Json driftPpm = nullptr;
        if (s.confirmedSampleRate && s.confirmedBufferFrames)
            expectedMs = 1000.0 * s.confirmedBufferFrames / s.confirmedSampleRate;
        if (s.available && s.callbackCount > 1 && s.lastCallbackQpc > s.firstCallbackQpc &&
            s.qpcFrequency) {
            measuredMs = 1000.0 * (s.lastCallbackQpc - s.firstCallbackQpc) /
                (static_cast<double>(s.callbackCount - 1) * s.qpcFrequency);
            if (expectedMs.is_number() && expectedMs.get<double>() > 0 &&
                measuredMs.get<double>() > 0)
                driftPpm = (expectedMs.get<double>() / measuredMs.get<double>() - 1.0) * 1000000.0;
        }
        Json exchangeAgeMs = nullptr;
        if (s.qpcFrequency && s.lastSuccessfulExchangeQpc &&
            static_cast<std::uint64_t>(now.QuadPart) >= s.lastSuccessfulExchangeQpc)
            exchangeAgeMs = 1000.0 * (now.QuadPart - s.lastSuccessfulExchangeQpc) /
                s.qpcFrequency;
        rows.push_back({
            {"pid", client.pid}, {"process", client.process}, {"layoutVersion", s.layoutVersion},
            {"statusAvailable", s.available}, {"state", s.available ? stateName(s.state) : "legacy"},
            {"dllVersion", s.available ? std::to_string(s.dllVersion[0]) + "." +
                std::to_string(s.dllVersion[1]) + "." + std::to_string(s.dllVersion[2]) + "." +
                std::to_string(s.dllVersion[3]) : "unknown"},
            {"dllPath", utf8(s.dllPath)}, {"inputChannels", inputs}, {"outputChannels", outputs},
            {"inputsWithoutWriter", unwritten},
            {"requestedSampleRate", s.requestedSampleRate},
            {"requestedBufferFrames", s.requestedBufferFrames},
            {"confirmedSampleRate", s.confirmedSampleRate},
            {"confirmedBufferFrames", s.confirmedBufferFrames},
            {"callbackCount", s.callbackCount},
            {"successfulExchangeCount", s.successfulExchangeCount},
            {"exchangedFrames", s.exchangedFrames},
            {"stableExchange", s.stableExchange},
            {"stableChannels", s.stableChannels},
            {"lastCallbackQpc", s.lastCallbackQpc},
            {"lastExchangeQpc", s.lastSuccessfulExchangeQpc}, {"qpcFrequency", s.qpcFrequency},
            {"lastExchangeAgeMs", exchangeAgeMs},
            {"expectedPeriodMs", expectedMs}, {"measuredPeriodMs", measuredMs},
            {"driftPpm", driftPpm},
            {"errors", {{"missedBufferOperations", errorTotal(s)},
                {"inputUnderruns", s.inputUnderruns},
                {"outputOverruns", s.outputOverruns}, {"inputOverruns", s.inputOverruns},
                {"outputUnderruns", s.outputUnderruns}, {"resetRequests", s.resetRequests},
                {"resetRequestsSupported", s.available}}}
        });
    }
    std::set<std::uint32_t> ghostReported;
    for (const auto& old : previous)
        if (!seen.count(old.first) && old.second.status.available &&
            old.second.status.state == AudioClientMapping::ClientState::Running) {
            alert(alerts, "GHOST_CLIENT", old.second.process +
                " disappeared without publishing a stop", old.first);
            ghostReported.insert(old.first);
        }
    if (processSnapshotAvailable && apiAvailable && engine.contains("virtualDrivers") &&
        engine["virtualDrivers"].is_array()) {
        for (const auto& driver : engine["virtualDrivers"]) {
            if (!driver.is_object() || !driver.contains("clients") ||
                !driver["clients"].is_array()) continue;
            for (const auto& client : driver["clients"]) {
                if (!client.is_object() || !client.contains("pid") ||
                    !client["pid"].is_number_integer()) continue;
                const auto rawPid = client["pid"].get<std::int64_t>();
                if (rawPid <= 0 || rawPid > UINT32_MAX) continue;
                const auto pid = static_cast<std::uint32_t>(rawPid);
                if (alivePids.count(pid) || ghostReported.count(pid)) continue;
                const auto name = client.value("processName", std::string("unknown"));
                alert(alerts, "GHOST_CLIENT", name +
                    " remains in the engine after its process exited", pid);
                ghostReported.insert(pid);
            }
        }
    }
    if (rates.size() > 1) alert(alerts, "MULTIPLE_RATES", "clients use different confirmed sample rates");
    if (blocks.size() > 1) alert(alerts, "MULTIPLE_BUFFERS", "clients use different confirmed buffer sizes");
    return {{"schemaVersion", 1}, {"qpc", now.QuadPart}, {"qpcFrequency", frequency.QuadPart},
        {"engineApiAvailable", apiAvailable}, {"clients", std::move(rows)},
        {"alerts", std::move(alerts)}};
}

void printHuman(const Json& report) {
    const HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info{};
    const bool color = console != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(console, &info);
    std::cout << "TimoxVasio clients: " << report["clients"].size() <<
        " | engine API: " << (report["engineApiAvailable"] == true ? "online" : "offline") << '\n';
    for (const auto& row : report["clients"])
        std::cout << "  " << row["process"].get<std::string>() << " PID " << row["pid"] <<
            "  " << row["state"].get<std::string>() << "  " << row["confirmedSampleRate"] <<
            " Hz / " << row["confirmedBufferFrames"] << " frames  callbacks " <<
            row["callbackCount"] << "  drift " <<
            (row["driftPpm"].is_number() ? row["driftPpm"].dump() : "n/a") << " ppm\n";
    for (const auto& item : report["alerts"]) {
        if (color) SetConsoleTextAttribute(console, FOREGROUND_RED | FOREGROUND_INTENSITY);
        std::cout << "  RED " << item["code"].get<std::string>() << ": " <<
            item["message"].get<std::string>() << '\n';
        if (color) SetConsoleTextAttribute(console, info.wAttributes);
    }
    std::cout.flush();
}
}

int main(int argc, char** argv) {
    Options options;
    try {
        if (!parse(argc, argv, options)) {
            std::cerr << "Usage: vasio-status [--json] [--once] [--seconds N] "
                "[--interval-ms N] [--api-port N] [--history PATH]\n";
            return 2;
        }
    } catch (...) {
        std::cerr << "Invalid numeric option\n";
        return 2;
    }
    std::ofstream history;
    if (!options.history.empty()) {
        history.open(options.history, std::ios::app);
        if (!history) { std::cerr << "Cannot open history file\n"; return 3; }
    }
    std::map<std::uint32_t, Client> previous;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(options.seconds);
    do {
        const auto engine = engineState(options.apiPort);
        std::set<std::uint32_t> alivePids;
        bool processSnapshotAvailable = false;
        const auto current = clients(alivePids, processSnapshotAvailable);
        const auto report = snapshot(current, engine, previous, alivePids,
            processSnapshotAvailable);
        const auto serialized = report.dump();
        if (options.json) std::cout << serialized << '\n';
        else printHuman(report);
        if (!options.history.empty()) {
            history << serialized << '\n';
            history.flush();
            if (!history) {
                std::cerr << "Cannot write status history\n";
                return 4;
            }
        }
        previous.clear();
        for (const auto& client : current) previous.emplace(client.pid, client);
        if (options.once || (options.json && options.seconds == 0)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(options.intervalMs));
    } while (options.seconds == 0 || std::chrono::steady_clock::now() < deadline);
    return 0;
}
