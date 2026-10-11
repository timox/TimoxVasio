#include "clock_regression.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using Json = nlohmann::json;

namespace {
Json readJson(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot open report: " + path);
    return Json::parse(stream);
}

std::vector<CadenceSample> readPolls(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot open position polls: " + path);
    std::vector<CadenceSample> samples;
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        const auto item = Json::parse(line);
        const CadenceSample sample{item.at("qpc").get<std::uint64_t>(),
            item.at("samplePosition").get<std::uint64_t>()};
        if (!samples.empty() && (sample.qpc <= samples.back().qpc ||
            sample.position < samples.back().position)) {
            throw std::runtime_error("Nonmonotonic position polls: " + path);
        }
        samples.push_back(sample);
    }
    return samples;
}

Json fitJson(const ClockRegression& fit) {
    return {{"available", fit.available}, {"samples", fit.samples},
        {"blocks", fit.blocks}, {"durationSeconds", fit.durationSeconds},
        {"observedRateHz", fit.observedRateHz}, {"driftPpm", fit.driftPpm},
        {"lowerPpm", fit.lowerPpm}, {"upperPpm", fit.upperPpm},
        {"uncertaintyHalfWidthPpm", fit.uncertaintyHalfWidthPpm}};
}

Json compare(const Json& primary, const Json& reference, const Json& thresholds) {
    Json result = {{"available", false}, {"reason", nullptr},
        {"primaryDriver", primary.value("driver", "")},
        {"referenceDriver", reference.value("driver", "")},
        {"overlapSeconds", 0.0}, {"overlapFraction", 0.0},
        {"relativeDriftPpm", nullptr}, {"lowerPpm", nullptr},
        {"upperPpm", nullptr}, {"timeToOnePrimaryBufferSeconds", nullptr},
        {"conclusion", "indéterminée"}};
    const auto qpcFrequency = primary.value("qpcFrequency", std::uint64_t{0});
    if (!qpcFrequency || qpcFrequency != reference.value("qpcFrequency", std::uint64_t{0})) {
        result["reason"] = "QPC frequencies are missing or different.";
        return result;
    }
    if (!primary.value("positionPollsSaved", false) ||
        !reference.value("positionPollsSaved", false)) {
        result["reason"] = "Raw ASIO sample-position polls are unavailable.";
        return result;
    }
    const auto primarySamples = readPolls(primary.at("positionPollsPath").get<std::string>());
    const auto referenceSamples = readPolls(reference.at("positionPollsPath").get<std::string>());
    if (primarySamples.size() < 3 || referenceSamples.size() < 3) {
        result["reason"] = "One or both drivers yielded fewer than three position polls.";
        return result;
    }
    const auto start = (std::max)(primarySamples.front().qpc, referenceSamples.front().qpc);
    const auto end = (std::min)(primarySamples.back().qpc, referenceSamples.back().qpc);
    const auto primaryDuration = static_cast<double>(primarySamples.back().qpc -
        primarySamples.front().qpc) / qpcFrequency;
    const auto referenceDuration = static_cast<double>(referenceSamples.back().qpc -
        referenceSamples.front().qpc) / qpcFrequency;
    const auto overlap = end > start ? static_cast<double>(end - start) / qpcFrequency : 0.0;
    const auto shorter = (std::min)(primaryDuration, referenceDuration);
    const auto overlapFraction = shorter > 0 ? overlap / shorter : 0.0;
    result["overlapSeconds"] = overlap;
    result["overlapFraction"] = overlapFraction;
    const auto minimumOverlap = thresholds.at("clockComparison").at("minOverlapFraction").get<double>();
    if (!(minimumOverlap > 0 && minimumOverlap <= 1))
        throw std::runtime_error("clockComparison.minOverlapFraction must be in (0, 1]");
    if (overlapFraction < minimumOverlap) {
        result["reason"] = "The QPC acquisition windows do not overlap enough.";
        return result;
    }
    const auto within = [start, end](const std::vector<CadenceSample>& all) {
        std::vector<CadenceSample> selected;
        selected.reserve(all.size());
        for (const auto& sample : all)
            if (sample.qpc >= start && sample.qpc <= end) selected.push_back(sample);
        return selected;
    };
    const auto primaryCommon = within(primarySamples);
    const auto referenceCommon = within(referenceSamples);
    const auto blockSeconds = thresholds.at("cadence").at("regressionBlockSeconds").get<double>();
    const auto primaryRate = primary.at("sampleRate").get<double>();
    const auto primaryFrames = primary.at("bufferFrames").get<std::uint32_t>();
    const auto referenceRate = reference.at("sampleRate").get<double>();
    const auto referenceFrames = reference.at("bufferFrames").get<std::uint32_t>();
    const auto a = estimateClockRegression(primaryCommon, primaryRate, primaryFrames,
        qpcFrequency, blockSeconds);
    const auto b = estimateClockRegression(referenceCommon, referenceRate, referenceFrames,
        qpcFrequency, blockSeconds);
    result["primaryRegression"] = fitJson(a);
    result["referenceRegression"] = fitJson(b);
    if (!a.available || !b.available) {
        result["reason"] = "The common QPC window has insufficient stable position data for both regressions.";
        return result;
    }
    if (1.0 + b.lowerPpm / 1e6 <= 0 || 1.0 + b.upperPpm / 1e6 <= 0) {
        result["reason"] = "Reference clock regression interval is invalid.";
        return result;
    }
    const auto relative = ((1.0 + a.driftPpm / 1e6) /
        (1.0 + b.driftPpm / 1e6) - 1.0) * 1e6;
    const auto lower = ((1.0 + a.lowerPpm / 1e6) /
        (1.0 + b.upperPpm / 1e6) - 1.0) * 1e6;
    const auto upper = ((1.0 + a.upperPpm / 1e6) /
        (1.0 + b.lowerPpm / 1e6) - 1.0) * 1e6;
    result["available"] = true;
    result["reason"] = "Both regressions use only sample-position polls within the shared QPC window; uncertainty is empirical.";
    result["relativeDriftPpm"] = relative;
    result["lowerPpm"] = lower;
    result["upperPpm"] = upper;
    if (std::abs(relative) > 0)
        result["timeToOnePrimaryBufferSeconds"] =
            primaryFrames / (primaryRate * std::abs(relative) / 1e6);
    result["conclusion"] = lower > 0 || upper < 0 ?
        "dérive relative détectée" : "dérive relative non résolue";
    return result;
}
}

int main(int argc, char** argv) {
    std::string primaryPath, referencePath, thresholdsPath, outputPath;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (index + 1 >= argc) return 2;
        if (arg == "--primary") primaryPath = argv[++index];
        else if (arg == "--reference") referencePath = argv[++index];
        else if (arg == "--thresholds") thresholdsPath = argv[++index];
        else if (arg == "--output") outputPath = argv[++index];
        else return 2;
    }
    if (primaryPath.empty() || referencePath.empty() || thresholdsPath.empty() || outputPath.empty()) {
        std::cerr << "Usage: vasio-clock-compare --primary REPORT --reference REPORT "
            "--thresholds FILE --output FILE\n";
        return 2;
    }
    try {
        const auto report = compare(readJson(primaryPath), readJson(referencePath),
            readJson(thresholdsPath));
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot open comparison report: " + outputPath);
        output << report.dump(2) << '\n';
        if (!output) throw std::runtime_error("Cannot write comparison report: " + outputPath);
        std::cout << report.dump() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 3;
    }
}
