#pragma once

#include "nlohmann/json.hpp"
#include <cstdint>
#include <string>
#include <windows.h>

class AsioDriverList;

nlohmann::json runCadence(AsioDriverList& drivers, const std::string& driverName,
    HWND hostWindow, std::uint32_t durationSeconds, const std::string& eventPath,
    const nlohmann::json& thresholds, std::uint64_t qpcFrequency);
