#include "vasio_compatibility_profile.h"

#include "audio_transport.h"

#include <cstdio>

int main() {
    const auto mixxx = VasioCompatibilityProfile::ChannelCountForExecutablePath(
        L"C:\\Program Files\\Mixxx\\mixxx.exe");
    if (mixxx != 255) {
        std::fprintf(stderr, "Mixxx compatibility profile expected 255 channels, got %ld\n", mixxx);
        return 1;
    }

    const auto otherHost = VasioCompatibilityProfile::ChannelCountForExecutablePath(
        L"C:\\Audio\\OtherHost.exe");
    if (otherHost != static_cast<long>(AudioClientMapping::kChannelCount)) {
        std::fprintf(stderr, "other ASIO host should retain 256 channels, got %ld\n", otherHost);
        return 1;
    }

    const auto caseInsensitiveMixxx = VasioCompatibilityProfile::ChannelCountForExecutablePath(
        L"D:\\Portable\\MIXXX.EXE");
    if (caseInsensitiveMixxx != 255) {
        std::fprintf(stderr, "Mixxx executable matching should ignore case, got %ld\n",
                     caseInsensitiveMixxx);
        return 1;
    }

    const auto misleadingName = VasioCompatibilityProfile::ChannelCountForExecutablePath(
        L"C:\\Audio\\notmixxx.exe");
    if (misleadingName != static_cast<long>(AudioClientMapping::kChannelCount)) {
        std::fprintf(stderr, "profile matching must use the exact executable name, got %ld\n",
                     misleadingName);
        return 1;
    }

    std::puts("PASS: only Mixxx receives the 255-channel compatibility profile.");
    return 0;
}
