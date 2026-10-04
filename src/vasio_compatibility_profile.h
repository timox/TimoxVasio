// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "application_profile.h"

#include <string_view>
#include <vector>

namespace VasioCompatibilityProfile {
struct ChannelCounts {
    long inputs;
    long outputs;
};

long ChannelCountForExecutablePath(std::wstring_view executablePath);
ChannelCounts ChannelCountsForExecutablePath(std::wstring_view executablePath,
    const std::vector<ApplicationProfile>& profiles);
ChannelCounts ChannelCountsForCurrentProcess();
}
