#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <array>

// Short, non-coherent projections prove tone presence across clock domains.
// This is deliberately not an SNR, latency, or bit-accuracy measurement.
class BridgeToneAnalyzer {
public:
    enum RejectReason : unsigned {
        Nonfinite = 1, LowAmplitude997 = 2, LowAmplitude1709 = 4,
        LowEnergy997 = 8, LowEnergy1709 = 16, LowCombinedEnergy = 32, UnbalancedTones = 64
    };
    struct WindowDiagnostic {
        std::uint64_t index = 0;
        unsigned reasons = 0;
        double startSeconds = 0, rms = 0, peak = 0, amplitude997 = 0, amplitude1709 = 0;
        double energyFraction997 = 0, energyFraction1709 = 0;
        std::uint64_t zeroSamples = 0, nearFullScaleSamples = 0;
    };
    struct Diagnostics {
        std::uint64_t rejectedWindows = 0, longestRejectedRun = 0, longestRunStart = 0;
        std::array<std::uint64_t, 7> reasonCounts{};
        std::array<WindowDiagnostic, 16> firstRejected{};
        std::size_t storedRejected = 0;
        WindowDiagnostic lastRejected{};
    };
    void Prepare(double rate) {
        rate_ = rate;
        basis_.resize(static_cast<std::size_t>(std::llround(rate * 0.025)));
        for (std::size_t i = 0; i < basis_.size(); ++i) {
            const double angle = 6.2831853071795864769 * i / rate;
            basis_[i] = {std::sin(997 * angle), std::cos(997 * angle),
                std::sin(1709 * angle), std::cos(1709 * angle)};
        }
    }
    void Process(double sample) noexcept {
        const auto& b = basis_[position_];
        if (!std::isfinite(sample)) invalid_ = true;
        else {
            peak_ = std::fmax(peak_, std::abs(sample));
            if (sample == 0) ++zeros_;
            if (std::abs(sample) >= 0.999) ++nearFullScale_;
            energy_ += sample * sample;
            s1_ += sample * b.s1; c1_ += sample * b.c1;
            s2_ += sample * b.s2; c2_ += sample * b.c2;
        }
        if (++position_ != basis_.size()) return;
        const double count = static_cast<double>(position_);
        const double a1 = 2 * std::hypot(s1_, c1_) / count;
        const double a2 = 2 * std::hypot(s2_, c2_) / count;
        const double power = energy_ / count;
        const double p1 = a1 * a1 / 2, p2 = a2 * a2 / 2;
        ++windows_;
        unsigned reasons = 0;
        if (invalid_) reasons |= Nonfinite;
        if (!(a1 > 1e-7)) reasons |= LowAmplitude997;
        if (!(a2 > 1e-7)) reasons |= LowAmplitude1709;
        if (!(p1 >= 0.15 * power)) reasons |= LowEnergy997;
        if (!(p2 >= 0.15 * power)) reasons |= LowEnergy1709;
        if (!(p1 + p2 >= 0.70 * power)) reasons |= LowCombinedEnergy;
        if (!(a1 >= 0.5 * a2 && a1 <= 2 * a2)) reasons |= UnbalancedTones;
        if (!reasons) { ++matched_; currentRejectedRun_ = 0; }
        else {
            ++diagnostics_.rejectedWindows;
            if (++currentRejectedRun_ > diagnostics_.longestRejectedRun) {
                diagnostics_.longestRejectedRun = currentRejectedRun_;
                diagnostics_.longestRunStart = windows_ - currentRejectedRun_;
            }
            for (unsigned bit = 0; bit < diagnostics_.reasonCounts.size(); ++bit)
                if (reasons & (1u << bit)) ++diagnostics_.reasonCounts[bit];
            const WindowDiagnostic record{windows_ - 1, reasons, (windows_ - 1) * count / rate_,
                std::sqrt(power), peak_, a1, a2, power > 0 ? p1 / power : 0,
                power > 0 ? p2 / power : 0, zeros_, nearFullScale_};
            if (diagnostics_.storedRejected < diagnostics_.firstRejected.size())
                diagnostics_.firstRejected[diagnostics_.storedRejected++] = record;
            diagnostics_.lastRejected = record;
        }
        power1_ += p1; power2_ += p2;
        position_ = 0; s1_ = c1_ = s2_ = c2_ = energy_ = peak_ = 0;
        zeros_ = nearFullScale_ = 0; invalid_ = false;
    }
    bool Passed() const noexcept { return windows_ >= 20 && matched_ * 10 >= windows_ * 9; }
    std::uint64_t Windows() const noexcept { return windows_; }
    std::uint64_t Matched() const noexcept { return matched_; }
    double Amplitude1() const noexcept { return windows_ ? std::sqrt(2 * power1_ / windows_) : 0; }
    double Amplitude2() const noexcept { return windows_ ? std::sqrt(2 * power2_ / windows_) : 0; }
    const Diagnostics& DiagnosticSummary() const noexcept { return diagnostics_; }
private:
    struct Basis { double s1, c1, s2, c2; };
    std::vector<Basis> basis_;
    std::size_t position_ = 0;
    std::uint64_t windows_ = 0, matched_ = 0;
    double s1_ = 0, c1_ = 0, s2_ = 0, c2_ = 0, energy_ = 0, power1_ = 0, power2_ = 0;
    bool invalid_ = false;
    double rate_ = 0, peak_ = 0;
    std::uint64_t zeros_ = 0, nearFullScale_ = 0, currentRejectedRun_ = 0;
    Diagnostics diagnostics_;
};
