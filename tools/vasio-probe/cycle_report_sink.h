#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <ostream>
#include <utility>

class LifecycleCycleSink {
public:
    LifecycleCycleSink(nlohmann::json& report, std::ostream& output,
        std::size_t maxInlineCycles)
        : report_(report), output_(output), maxInlineCycles_(maxInlineCycles) {}

    bool append(nlohmann::json item) {
        output_ << item.dump() << '\n';
        if (!output_) return false;
        if (count_ < maxInlineCycles_) report_["cycles"].push_back(std::move(item));
        ++count_;
        report_["cyclesRecorded"] = count_;
        return true;
    }

    std::size_t count() const noexcept { return count_; }

private:
    nlohmann::json& report_;
    std::ostream& output_;
    std::size_t maxInlineCycles_;
    std::size_t count_ = 0;
};
