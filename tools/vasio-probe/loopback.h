#pragma once

#include "nlohmann/json.hpp"
#include <cstdint>
#include <string>
#include <windows.h>

class AsioDriverList;

nlohmann::json runLoopback(AsioDriverList& drivers, const std::string& driverName,
    HWND hostWindow, const std::string& role, std::uint32_t durationSeconds,
    std::uint32_t channelIndex,
    const std::string& eventPath, const nlohmann::json& thresholds,
    std::uint64_t qpcFrequency);
