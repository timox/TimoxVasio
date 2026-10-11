#pragma once

#include "cadence_stats.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

struct ClockRegression {
    bool available = false;
    std::size_t samples = 0;
    std::size_t blocks = 0;
    double durationSeconds = 0;
    double observedRateHz = 0;
    double driftPpm = 0;
    double uncertaintyHalfWidthPpm = 0;
    double lowerPpm = 0;
    double upperPpm = 0;
    double blockStandardErrorPpm = 0;
    double endpointQuantizationBoundPpm = 0;
};

inline double fitClockSlope(const std::vector<CadenceSample>& samples,
    std::size_t begin, std::size_t end, std::uint64_t qpcFrequency) {
    if (end - begin < 2 || qpcFrequency == 0) return 0;
    const auto originQpc = samples[begin].qpc;
    const auto originPosition = samples[begin].position;
    double meanX = 0, meanY = 0, sumXX = 0, sumXY = 0;
    std::size_t count = 0;
    for (std::size_t index = begin; index < end; ++index) {
        const double x = static_cast<double>(samples[index].qpc - originQpc) /
            static_cast<double>(qpcFrequency);
        const double y = static_cast<double>(samples[index].position - originPosition);
        ++count;
        const double deltaX = x - meanX;
        meanX += deltaX / static_cast<double>(count);
        const double deltaY = y - meanY;
        meanY += deltaY / static_cast<double>(count);
        sumXX += deltaX * (x - meanX);
        sumXY += deltaX * (y - meanY);
    }
    return sumXX > 0 ? sumXY / sumXX : 0;
}

inline ClockRegression estimateClockRegression(const std::vector<CadenceSample>& samples,
    double nominalRate, std::uint32_t bufferFrames, std::uint64_t qpcFrequency,
    double blockSeconds) {
    ClockRegression result{};
    result.samples = samples.size();
    if (samples.size() < 3 || nominalRate <= 0 || bufferFrames == 0 ||
        qpcFrequency == 0 || blockSeconds <= 0 ||
        samples.back().qpc <= samples.front().qpc ||
        samples.back().position <= samples.front().position) return result;
    for (std::size_t index = 1; index < samples.size(); ++index) {
        if (samples[index].qpc <= samples[index - 1].qpc ||
            samples[index].position < samples[index - 1].position) return result;
    }
    result.durationSeconds = static_cast<double>(samples.back().qpc - samples.front().qpc) /
        static_cast<double>(qpcFrequency);
    result.observedRateHz = fitClockSlope(samples, 0, samples.size(), qpcFrequency);
    if (result.observedRateHz <= 0) return result;
    result.driftPpm = (result.observedRateHz / nominalRate - 1.0) * 1e6;

    std::vector<double> blockPpm;
    std::size_t begin = 0;
    while (begin + 1 < samples.size()) {
        const auto blockEndQpc = samples[begin].qpc +
            static_cast<std::uint64_t>(blockSeconds * static_cast<double>(qpcFrequency));
        auto end = begin + 1;
        while (end < samples.size() && samples[end].qpc < blockEndQpc) ++end;
        if (end - begin >= 3 &&
            samples[end - 1].qpc - samples[begin].qpc >=
                static_cast<std::uint64_t>(blockSeconds * 0.8 * qpcFrequency)) {
            const double slope = fitClockSlope(samples, begin, end, qpcFrequency);
            if (slope > 0) blockPpm.push_back((slope / nominalRate - 1.0) * 1e6);
        }
        begin = end;
    }
    result.blocks = blockPpm.size();
    // A short run has no defensible empirical interval. Keep the rate estimate,
    // but do not claim confidence from one or two correlated blocks.
    if (blockPpm.size() < 3) return result;
    double mean = 0;
    for (const double value : blockPpm) mean += value;
    mean /= static_cast<double>(blockPpm.size());
    double squares = 0;
    for (const double value : blockPpm) squares += (value - mean) * (value - mean);
    const double standardDeviation = std::sqrt(squares / (blockPpm.size() - 1));
    result.blockStandardErrorPpm = standardDeviation / std::sqrt(static_cast<double>(blockPpm.size()));
    result.endpointQuantizationBoundPpm = 2.0 * bufferFrames * 1e6 /
        (nominalRate * result.durationSeconds);
    // Use a conservative normal multiplier. This is an empirical uncertainty
    // band, not a calibrated confidence interval for correlated USB clocks.
    result.uncertaintyHalfWidthPpm = (std::max)(
        2.0 * result.blockStandardErrorPpm, result.endpointQuantizationBoundPpm);
    result.lowerPpm = result.driftPpm - result.uncertaintyHalfWidthPpm;
    result.upperPpm = result.driftPpm + result.uncertaintyHalfWidthPpm;
    result.available = true;
    return result;
}
