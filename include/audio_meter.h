#pragma once

#include <cstdint>
#include <string>

struct AudioMeterSnapshot {
    std::string endpointId;
    float peakLinear = 0.0f;
    std::uint64_t underruns = 0;
    std::uint64_t overruns = 0;
};
