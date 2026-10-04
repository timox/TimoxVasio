// SPDX-License-Identifier: GPL-3.0-only
#include "vasio_compatibility_profile.h"

#include "application_profile_store.h"
#include "audio_transport.h"

#include <windows.h>

#include <iterator>

namespace VasioCompatibilityProfile {
ChannelCounts ChannelCountsForExecutablePath(std::wstring_view executablePath,
    const std::vector<ApplicationProfile>& profiles) {
    const auto profile = ApplicationProfileStore::EffectiveProfileForExecutable(executablePath, profiles);
    return {static_cast<long>(profile.inputChannels), static_cast<long>(profile.outputChannels)};
}

long ChannelCountForExecutablePath(std::wstring_view executablePath) {
    const auto counts = ChannelCountsForExecutablePath(executablePath,
        ApplicationProfileStore::DefaultProfiles());
    return counts.outputs;
}

ChannelCounts ChannelCountsForCurrentProcess() {
    wchar_t executablePath[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, executablePath,
        static_cast<DWORD>(std::size(executablePath)));
    if (!length || length >= std::size(executablePath))
        return {static_cast<long>(AudioClientMapping::kChannelCount),
                static_cast<long>(AudioClientMapping::kChannelCount)};
    std::vector<ApplicationProfile> profiles;
    std::string error;
    const ApplicationProfileStore store(ApplicationProfileStore::DefaultPath());
    if (!store.Load(profiles, error)) profiles = ApplicationProfileStore::DefaultProfiles();
    return ChannelCountsForExecutablePath(std::wstring_view(executablePath, length), profiles);
}
}
