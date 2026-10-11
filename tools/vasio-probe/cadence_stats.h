#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

struct CadenceSample {
    std::uint64_t qpc;
    std::uint64_t position;
};

struct CadenceSummary {
    std::size_t callbackCount = 0;
    std::uint64_t missingCallbacks = 0;
    std::uint64_t duplicateCallbacks = 0;
    std::uint64_t backwardsPositions = 0;
    std::uint64_t positionStepErrors = 0;
    std::uint64_t nonMonotonicTimestamps = 0;
    double expectedIntervalMs = 0;
    double intervalMeanMs = 0;
    double intervalMinMs = 0;
    double intervalMaxMs = 0;
    double intervalP99Ms = 0;
    double jitterP99Ms = 0;
    double driftPpm = 0;
};

inline CadenceSummary summarizeCadence(const std::vector<CadenceSample>& samples,
    double sampleRate, std::uint32_t frames, std::uint64_t qpcFrequency) {
    CadenceSummary result{};
    result.callbackCount = samples.size();
    if (sampleRate <= 0 || frames == 0 || qpcFrequency == 0) return result;
    result.expectedIntervalMs = static_cast<double>(frames) * 1000.0 / sampleRate;
    if (samples.size() < 2) return result;

    std::vector<double> intervals;
    std::vector<double> jitter;
    intervals.reserve(samples.size() - 1);
    jitter.reserve(samples.size() - 1);
    double intervalSum = 0;
    for (std::size_t index = 1; index < samples.size(); ++index) {
        const auto& previous = samples[index - 1];
        const auto& current = samples[index];
        if (current.qpc <= previous.qpc) {
            ++result.nonMonotonicTimestamps;
        } else {
            const double interval = static_cast<double>(current.qpc - previous.qpc) *
                1000.0 / static_cast<double>(qpcFrequency);
            intervals.push_back(interval);
            jitter.push_back(std::abs(interval - result.expectedIntervalMs));
            intervalSum += interval;
        }
        if (current.position == previous.position) {
            ++result.duplicateCallbacks;
            ++result.positionStepErrors;
        } else if (current.position < previous.position) {
            ++result.backwardsPositions;
            ++result.positionStepErrors;
        } else {
            const auto delta = current.position - previous.position;
            if (delta != frames) ++result.positionStepErrors;
            if (delta > frames) result.missingCallbacks += delta / frames - 1;
        }
    }
    if (!intervals.empty()) {
        result.intervalMeanMs = intervalSum / static_cast<double>(intervals.size());
        std::sort(intervals.begin(), intervals.end());
        std::sort(jitter.begin(), jitter.end());
        result.intervalMinMs = intervals.front();
        result.intervalMaxMs = intervals.back();
        const auto percentileIndex = static_cast<std::size_t>(
            std::ceil(0.99 * static_cast<double>(intervals.size()))) - 1;
        result.intervalP99Ms = intervals[percentileIndex];
        result.jitterP99Ms = jitter[percentileIndex];
    }
    const auto elapsedTicks = samples.back().qpc - samples.front().qpc;
    if (samples.back().qpc > samples.front().qpc &&
        samples.back().position > samples.front().position) {
        const double realRate = static_cast<double>(samples.back().position -
            samples.front().position) * static_cast<double>(qpcFrequency) /
            static_cast<double>(elapsedTicks);
        result.driftPpm = (realRate / sampleRate - 1.0) * 1000000.0;
    }
    return result;
}
