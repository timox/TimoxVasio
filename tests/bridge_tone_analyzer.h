#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// Short, non-coherent projections prove tone presence across clock domains.
// This is deliberately not an SNR, latency, or bit-accuracy measurement.
class BridgeToneAnalyzer {
public:
    void Prepare(double rate) {
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
        if (!invalid_ && a1 > 1e-7 && a2 > 1e-7 &&
            p1 >= 0.15 * power && p2 >= 0.15 * power &&
            p1 + p2 >= 0.70 * power && a1 >= 0.5 * a2 && a1 <= 2 * a2) ++matched_;
        power1_ += p1; power2_ += p2;
        position_ = 0; s1_ = c1_ = s2_ = c2_ = energy_ = 0; invalid_ = false;
    }
    bool Passed() const noexcept { return windows_ >= 20 && matched_ * 10 >= windows_ * 9; }
    std::uint64_t Windows() const noexcept { return windows_; }
    std::uint64_t Matched() const noexcept { return matched_; }
    double Amplitude1() const noexcept { return windows_ ? std::sqrt(2 * power1_ / windows_) : 0; }
    double Amplitude2() const noexcept { return windows_ ? std::sqrt(2 * power2_ / windows_) : 0; }
private:
    struct Basis { double s1, c1, s2, c2; };
    std::vector<Basis> basis_;
    std::size_t position_ = 0;
    std::uint64_t windows_ = 0, matched_ = 0;
    double s1_ = 0, c1_ = 0, s2_ = 0, c2_ = 0, energy_ = 0, power1_ = 0, power2_ = 0;
    bool invalid_ = false;
};
