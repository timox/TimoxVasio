#include <windows.h>
#include <objbase.h>

#include "asiolist.h"
#include "iasiodrv.h"
#include "nlohmann/json.hpp"
#include "lifecycle.h"
#include "cadence.h"
#include "loopback.h"
#include "driver_registry.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {
using Json = nlohmann::json;

struct Options {
    std::string mode;
    std::string driver;
    std::string output;
    std::string thresholds;
    std::string role;
    std::uint32_t cycles = 1000;
    std::uint32_t seconds = 3600;
    std::uint32_t channel = 0;
    std::uint32_t requestedRate = 0;
    std::uint32_t requestedBuffer = 0;
    std::uint32_t liveRate = 0;
    bool reloadDll = false;
};

std::string boundedString(const char* data, std::size_t capacity) {
    return std::string(data, std::find(data, data + capacity, '\0'));
}

std::uint64_t qpc() {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return static_cast<std::uint64_t>(value.QuadPart);
}

void usage() {
    std::puts("Usage: vasio-probe list [--output FILE]\n"
              "       vasio-probe caps --driver NAME [--output FILE]\n"
              "       vasio-probe lifecycle --driver NAME --output FILE --thresholds FILE [--cycles N] [--reload] [--rate HZ] [--buffer FRAMES] [--live-rate HZ]\n"
              "       vasio-probe cadence --driver NAME --output FILE --thresholds FILE [--seconds N]\n"
              "       vasio-probe loopback --role send|receive|both --driver NAME --output FILE --thresholds FILE [--seconds N] [--channel ZERO_BASED]");
}

bool parse(int argc, char** argv, Options& options, std::string& error) {
    if (argc < 2) { error = "missing mode"; return false; }
    options.mode = argv[1];
    if (options.mode != "list" && options.mode != "caps" && options.mode != "lifecycle" &&
        options.mode != "cadence" && options.mode != "loopback") {
        error = "unknown mode";
        return false;
    }
    for (int index = 2; index < argc; ++index) {
        if (std::string(argv[index]) == "--reload" && !options.reloadDll) {
            options.reloadDll = true;
            continue;
        }
        if (index + 1 >= argc) { error = "missing option value"; return false; }
        const std::string option = argv[index++];
        if (option == "--driver" && options.driver.empty()) options.driver = argv[index];
        else if (option == "--output" && options.output.empty()) options.output = argv[index];
        else if (option == "--thresholds" && options.thresholds.empty()) options.thresholds = argv[index];
        else if (option == "--role" && options.role.empty()) options.role = argv[index];
        else if (option == "--cycles") {
            char* end = nullptr;
            const auto parsed = std::strtoul(argv[index], &end, 10);
            if (!end || *end || parsed == 0 || parsed > 1000000) {
                error = "cycles must be an integer from 1 to 1000000";
                return false;
            }
            options.cycles = static_cast<std::uint32_t>(parsed);
        }
        else if (option == "--seconds") {
            char* end = nullptr;
            const auto parsed = std::strtoul(argv[index], &end, 10);
            if (!end || *end || parsed == 0 || parsed > 86400) {
                error = "seconds must be an integer from 1 to 86400";
                return false;
            }
            options.seconds = static_cast<std::uint32_t>(parsed);
        }
        else if (option == "--channel") {
            char* end = nullptr;
            const auto parsed = std::strtoul(argv[index], &end, 10);
            if (!end || *end || parsed > 255) {
                error = "channel must be a zero-based integer from 0 to 255";
                return false;
            }
            options.channel = static_cast<std::uint32_t>(parsed);
        }
        else if (option == "--rate" || option == "--buffer" || option == "--live-rate") {
            char* end = nullptr;
            const auto parsed = std::strtoul(argv[index], &end, 10);
            if (!end || *end || parsed == 0 || parsed > 192000) {
                error = option + " must be a positive integer no greater than 192000";
                return false;
            }
            if (option == "--rate") options.requestedRate = static_cast<std::uint32_t>(parsed);
            else if (option == "--buffer") options.requestedBuffer = static_cast<std::uint32_t>(parsed);
            else options.liveRate = static_cast<std::uint32_t>(parsed);
        }
        else { error = "unknown or repeated option: " + option; return false; }
    }
    if (options.mode != "list" && options.driver.empty()) {
        error = "this mode requires --driver";
        return false;
    }
    if (options.mode == "list" && !options.driver.empty()) {
        error = "list does not accept --driver";
        return false;
    }
    if ((options.mode == "lifecycle" || options.mode == "cadence" ||
        options.mode == "loopback") &&
        (options.output.empty() || options.thresholds.empty())) {
        error = "this mode requires --output and --thresholds";
        return false;
    }
    if (options.mode == "loopback" && options.role != "send" && options.role != "receive" && options.role != "both") {
        error = "loopback requires --role send, receive or both";
        return false;
    }
    if (options.mode != "loopback" && !options.role.empty()) {
        error = "--role is only valid for loopback";
        return false;
    }
    if (options.reloadDll && options.mode != "lifecycle") {
        error = "--reload is only valid for lifecycle";
        return false;
    }
    if (options.mode != "lifecycle" && (options.requestedRate || options.requestedBuffer || options.liveRate)) {
        error = "--rate, --buffer and --live-rate are only valid for lifecycle";
        return false;
    }
    if (options.liveRate && (options.cycles != 1 || options.requestedRate)) {
        error = "--live-rate requires --cycles 1 and cannot be combined with --rate";
        return false;
    }
    return true;
}

bool emit(const Json& report, const std::string& output, std::string& error) {
    const auto serialized = report.dump(2) + "\n";
    if (output.empty()) {
        std::fwrite(serialized.data(), 1, serialized.size(), stdout);
        return true;
    }
    std::ofstream file(output, std::ios::binary | std::ios::trunc);
    if (!file) { error = "cannot open output file: " + output; return false; }
    file.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
    if (!file) { error = "cannot write output file: " + output; return false; }
    return true;
}

Json registryDrivers(AsioDriverList& drivers) {
    Json result = Json::array();
    for (int index = 0; index < drivers.asioGetNumDev(); ++index) {
        char name[MAXDRVNAMELEN]{};
        if (drivers.asioGetDriverName(index, name, sizeof(name)) != 0) continue;
        result.push_back({{"index", index}, {"name", boundedString(name, sizeof(name))}});
    }
    return result;
}

Json probeChannels(IASIO& driver, long count, ASIOBool input, Json& findings) {
    Json channels = Json::array();
    long firstType = -1;
    for (long index = 0; index < count; ++index) {
        ASIOChannelInfo info{};
        info.channel = index;
        info.isInput = input;
        const auto status = driver.getChannelInfo(&info);
        Json channel = {{"index", index}, {"status", status}};
        if (status == ASE_OK) {
            channel["name"] = boundedString(info.name, sizeof(info.name));
            channel["sampleType"] = static_cast<long>(info.type);
            channel["isActive"] = info.isActive == ASIOTrue;
            if (firstType < 0) firstType = static_cast<long>(info.type);
            else if (static_cast<long>(info.type) != firstType)
                findings.push_back({{"code", "mixed-sample-types"},
                    {"direction", input == ASIOTrue ? "input" : "output"}, {"channel", index}});
        } else {
            findings.push_back({{"code", "channel-info-failed"},
                {"direction", input == ASIOTrue ? "input" : "output"}, {"channel", index},
                {"status", status}});
        }
        channels.push_back(std::move(channel));
    }
    return channels;
}

Json caps(AsioDriverList& drivers, HWND hostWindow, const Options& options,
    const std::uint64_t frequency) {
    Json report = {{"mode", "caps"}, {"driver", options.driver}, {"qpcFrequency", frequency},
        {"startedQpc", qpc()}, {"findings", Json::array()}, {"verdict", "inconclusive"}};
    int matchingIndex = -1;
    int matches = 0;
    for (int index = 0; index < drivers.asioGetNumDev(); ++index) {
        char name[MAXDRVNAMELEN]{};
        if (drivers.asioGetDriverName(index, name, sizeof(name)) == 0 &&
            options.driver == boundedString(name, sizeof(name))) {
            matchingIndex = index;
            ++matches;
        }
    }
    if (matches != 1) {
        report["error"] = matches ? "driver name is ambiguous" : "driver is not registered";
        report["finishedQpc"] = qpc();
        return report;
    }

    CLSID clsid{};
    const auto clsidStatus = drivers.asioGetDriverCLSID(matchingIndex, &clsid);
    report["registeredClsid"] = {{"status", clsidStatus}};
    if (clsidStatus == 0) {
        const auto registeredPath = registeredAsioDllPath(clsid);
        report["registeredPath"] = utf8FromWide(registeredPath);
        wchar_t guid[40]{};
        if (StringFromGUID2(clsid, guid, static_cast<int>(std::size(guid)))) {
            char encoded[40]{};
            if (WideCharToMultiByte(CP_UTF8, 0, guid, -1, encoded,
                static_cast<int>(sizeof(encoded)), nullptr, nullptr))
                report["registeredClsid"]["value"] = encoded;
        }
    }

    IASIO* driver = nullptr;
    const auto openStatus = drivers.asioOpenDriver(matchingIndex, reinterpret_cast<void**>(&driver));
    report["openStatus"] = openStatus;
    if (openStatus != 0 || !driver) {
        report["error"] = "driver could not be opened";
        report["finishedQpc"] = qpc();
        return report;
    }
    const bool initialized = driver->init(hostWindow) == ASIOTrue;
    report["initialized"] = initialized;
    if (!initialized) {
        char message[128]{};
        driver->getErrorMessage(message);
        report["error"] = "driver init failed";
        report["driverError"] = boundedString(message, sizeof(message));
        drivers.asioCloseDriver(matchingIndex);
        report["finishedQpc"] = qpc();
        return report;
    }

    char driverName[128]{};
    driver->getDriverName(driverName);
    report["reportedName"] = boundedString(driverName, sizeof(driverName));
    report["driverVersion"] = driver->getDriverVersion();

    long inputs = 0, outputs = 0;
    const auto channelsStatus = driver->getChannels(&inputs, &outputs);
    report["channelsStatus"] = channelsStatus;
    report["inputCount"] = inputs;
    report["outputCount"] = outputs;
    if (channelsStatus != ASE_OK || inputs < 0 || outputs < 0 || inputs > 4096 || outputs > 4096) {
        report["findings"].push_back({{"code", "invalid-channel-counts"}});
    } else {
        report["inputs"] = probeChannels(*driver, inputs, ASIOTrue, report["findings"]);
        report["outputs"] = probeChannels(*driver, outputs, ASIOFalse, report["findings"]);
    }

    long minimum = 0, maximum = 0, preferred = 0, granularity = 0;
    const auto bufferStatus = driver->getBufferSize(&minimum, &maximum, &preferred, &granularity);
    report["buffer"] = {{"status", bufferStatus}, {"minimum", minimum}, {"maximum", maximum},
        {"preferred", preferred}, {"granularity", granularity}};
    if (bufferStatus != ASE_OK || minimum <= 0 || maximum < minimum ||
        preferred < minimum || preferred > maximum)
        report["findings"].push_back({{"code", "invalid-buffer-limits"}});

    constexpr std::array<long, 6> rates{44100, 48000, 88200, 96000, 176400, 192000};
    report["sampleRates"] = Json::array();
    for (const auto rate : rates) {
        const auto status = driver->canSampleRate(static_cast<ASIOSampleRate>(rate));
        report["sampleRates"].push_back({{"rate", rate}, {"status", status},
            {"accepted", status == ASE_OK}});
    }
    ASIOSampleRate currentRate = 0;
    const auto currentRateStatus = driver->getSampleRate(&currentRate);
    report["currentSampleRate"] = {{"status", currentRateStatus}, {"rate", currentRate}};

    long inputLatency = 0, outputLatency = 0;
    const auto latencyStatus = driver->getLatencies(&inputLatency, &outputLatency);
    report["latencies"] = {{"status", latencyStatus}, {"inputFrames", inputLatency},
        {"outputFrames", outputLatency}};

    std::array<ASIOClockSource, 32> clocks{};
    long clockCount = static_cast<long>(clocks.size());
    const auto clockStatus = driver->getClockSources(clocks.data(), &clockCount);
    report["clockSources"] = {{"status", clockStatus}, {"count", clockCount},
        {"sources", Json::array()}};
    if (clockStatus == ASE_OK && clockCount >= 0 && clockCount <= static_cast<long>(clocks.size())) {
        for (long index = 0; index < clockCount; ++index) {
            const auto& clock = clocks[static_cast<std::size_t>(index)];
            report["clockSources"]["sources"].push_back({{"index", clock.index},
                {"name", boundedString(clock.name, sizeof(clock.name))},
                {"current", clock.isCurrentSource == ASIOTrue},
                {"associatedChannel", clock.associatedChannel},
                {"associatedGroup", clock.associatedGroup}});
        }
    } else {
        report["findings"].push_back({{"code", "clock-query-failed"}, {"status", clockStatus}});
    }
    report["verdict"] = report["findings"].empty() ? "pass" : "fail";
    drivers.asioCloseDriver(matchingIndex);
    report["finishedQpc"] = qpc();
    return report;
}
}

int main(int argc, char** argv) {
    Options options;
    std::string error;
    if (!parse(argc, argv, options, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        usage();
        return 2;
    }
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return 3;
    // The SDK's AsioDriverList calls CoInitialize (STA) internally.
    const auto comStatus = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comStatus)) {
        std::fprintf(stderr, "COM initialization failed: 0x%08lX\n", static_cast<unsigned long>(comStatus));
        return 4;
    }
    const HWND hostWindow = CreateWindowExW(0, L"STATIC", L"vasio-probe",
        WS_OVERLAPPED, 0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hostWindow) {
        std::fprintf(stderr, "cannot create ASIO host window\n");
        CoUninitialize();
        return 4;
    }
    Json report;
    {
        AsioDriverList drivers;
        if (options.mode == "list") {
            report = {{"mode", "list"}, {"qpcFrequency", frequency.QuadPart},
                {"startedQpc", qpc()}, {"drivers", registryDrivers(drivers)}};
            report["finishedQpc"] = qpc();
        } else if (options.mode == "caps") {
            report = caps(drivers, hostWindow, options,
                static_cast<std::uint64_t>(frequency.QuadPart));
        } else {
            std::ifstream thresholdFile(options.thresholds, std::ios::binary);
            if (!thresholdFile) {
                error = "cannot read thresholds: " + options.thresholds;
            } else {
                try {
                    const auto thresholds = Json::parse(thresholdFile);
                    if (options.mode == "cadence")
                        report = runCadence(drivers, options.driver, hostWindow,
                            options.seconds, options.output + ".events.jsonl", thresholds,
                            static_cast<std::uint64_t>(frequency.QuadPart));
                    else if (options.mode == "loopback")
                        report = runLoopback(drivers, options.driver, hostWindow,
                            options.role, options.seconds, options.channel,
                            options.output + ".events.jsonl",
                            thresholds, static_cast<std::uint64_t>(frequency.QuadPart));
                    else
                        report = runLifecycle(drivers, options.driver, hostWindow,
                            options.cycles, options.reloadDll, options.requestedRate,
                            options.requestedBuffer, options.liveRate,
                            options.output + ".events.jsonl", thresholds,
                            static_cast<std::uint64_t>(frequency.QuadPart));
                } catch (const std::exception& exception) {
                    error = std::string("invalid thresholds: ") + exception.what();
                }
            }
        }
    }
    DestroyWindow(hostWindow);
    CoUninitialize();
    if (!error.empty()) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 5;
    }
    if (!emit(report, options.output, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 5;
    }
    const auto verdict = report.value("verdict", std::string("pass"));
    return verdict == "fail" ? 1 : verdict == "inconclusive" ? 6 : 0;
}
