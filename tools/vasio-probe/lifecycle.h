#pragma once

#include "nlohmann/json.hpp"

#include <cstdint>
#include <string>
#include <windows.h>

class AsioDriverList;

nlohmann::json runLifecycle(AsioDriverList& drivers, const std::string& driverName,
    HWND hostWindow, std::uint32_t cycles, bool reloadDll,
    std::uint32_t requestedRate, std::uint32_t requestedBuffer,
    std::uint32_t liveRate,
    const std::string& eventPath,
    const nlohmann::json& thresholds, std::uint64_t qpcFrequency);
