#include "cadence_stats.h"
#include "clock_regression.h"
#include <cmath>
#include <vector>

int main() {
    const std::vector<CadenceSample> regular{{0, 0}, {1000, 48}, {2000, 96}, {3000, 144}};
    const auto ok = summarizeCadence(regular, 48000, 48, 1000000);
    if (ok.callbackCount != 4) return 1;
    if (ok.missingCallbacks != 0 || ok.duplicateCallbacks != 0) return 2;
    if (ok.positionStepErrors != 0) return 3;
    if (ok.intervalMeanMs != 1.0 || ok.intervalP99Ms != 1.0) return 4;
    if (ok.driftPpm != 0.0) return 5;

    const std::vector<CadenceSample> broken{{0, 0}, {1000, 48}, {2000, 48},
        {4000, 144}, {5000, 192}};
    const auto bad = summarizeCadence(broken, 48000, 48, 1000000);
    if (bad.duplicateCallbacks != 1) return 6;
    if (bad.missingCallbacks != 1) return 7;
    if (bad.positionStepErrors != 2) return 8;
    if (bad.intervalMaxMs != 2.0) return 9;

    // Quantized ASIO sample positions should still recover the underlying
    // rate over a long QPC window and bracket the known drift.
    std::vector<CadenceSample> quantized;
    constexpr double actualRate = 48004.8; // +100 ppm
    for (std::uint64_t tenth = 0; tenth <= 6000; ++tenth) {
        const double seconds = static_cast<double>(tenth) / 10.0;
        const auto frames = static_cast<std::uint64_t>(seconds * actualRate / 480.0) * 480;
        quantized.push_back({tenth * 100000, frames});
    }
    const auto regression = estimateClockRegression(quantized, 48000, 480, 1000000, 10);
    if (!regression.available || regression.blocks < 10) return 10;
    if (std::abs(regression.driftPpm - 100.0) > 20.0) return 11;
    if (regression.lowerPpm > 100.0 || regression.upperPpm < 100.0) return 12;
    if (regression.endpointQuantizationBoundPpm < 30.0) return 13;
    auto backwards = quantized;
    backwards[100].position = backwards[99].position - 1;
    if (estimateClockRegression(backwards, 48000, 480, 1000000, 10).available) return 14;
}
