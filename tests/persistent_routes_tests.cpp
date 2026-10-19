#include "persistent_routes.h"
#include "audio_transport.h"

#include <cstdio>

int main() {
    const auto pid = GetCurrentProcessId();
    auto mapping = AudioClientMapping::CreateClient(1, pid, 48000, 256);
    if (!mapping) return 2;
    mapping->SetEngineAttached(true);
    VasioClientSnapshot client{{1, pid, L"Renoise.exe"}, std::shared_ptr<AudioClientMapping>(std::move(mapping))};
    const AudioRoute route{"route-1", "virtual:TimoxVasio:" + std::to_string(pid) + ":output:1",
        "physical:{driver}:output:1", 0.0, false};
    std::string error;
    const auto saved = PersistentRoutes::Encode({route}, {client}, error);
    if (saved.size() != 1 || saved[0].sourceEndpointId !=
        "virtual:TimoxVasio:app:renoise.exe:output:1") {
        std::fprintf(stderr, "FAIL: route was not stored by executable name: %s\n", error.c_str());
        return 1;
    }
    const auto beforeBuffers = PersistentRoutes::Resolve(saved, {client});
    if (!beforeBuffers.empty()) {
        std::fputs("FAIL: unopened channel was activated\n", stderr);
        return 1;
    }
    AudioClientMapping::ChannelMask inputs{};
    AudioClientMapping::ChannelMask outputs{};
    outputs[0] = 1;
    if (!client.mapping->SetActiveChannels(inputs, outputs)) return 2;
    const auto active = PersistentRoutes::Resolve(saved, {client});
    if (active.size() != 1 || active[0].sourceEndpointId != route.sourceEndpointId) {
        std::fputs("FAIL: active channel was not restored\n", stderr);
        return 1;
    }
    auto replacement = AudioClientMapping::CreateClient(1, pid, 48000, 256);
    if (!replacement) return 2;
    replacement->SetEngineAttached(true);
    replacement->SetActiveChannels(inputs, outputs);
    VasioClientSnapshot restarted{{1, pid + 1, L"Renoise.exe"},
        std::shared_ptr<AudioClientMapping>(std::move(replacement))};
    const auto reconnected = PersistentRoutes::Resolve(saved, {restarted});
    if (reconnected.size() != 1 || reconnected[0].sourceEndpointId !=
        "virtual:TimoxVasio:" + std::to_string(pid + 1) + ":output:1") {
        std::fputs("FAIL: route did not follow restarted application\n", stderr);
        return 1;
    }
    if (!PersistentRoutes::Resolve(saved, {client, restarted}).empty()) {
        std::fputs("FAIL: route bound ambiguously to two instances\n", stderr);
        return 1;
    }
    const std::vector<ApplicationProfile> profiles{{L"Renoise.exe", 8, 8}};
    if (PersistentRoutes::ProfiledEndpointType(saved[0].sourceEndpointId, profiles) !=
            RoutingEndpointType::VirtualOutput ||
        PersistentRoutes::ProfiledEndpointType(
            "virtual:TimoxVasio:app:renoise.exe:output:9", profiles) ||
        PersistentRoutes::ProfiledEndpointType(
            "virtual:TimoxVasio:app:unknown.exe:output:1", profiles)) {
        std::fputs("FAIL: profiled pending route validation is incorrect\n", stderr);
        return 1;
    }
    if (PersistentRoutes::KnownApplicationEndpointType(
            "virtual:TimoxVasio:app:renoise.exe:output:9", {}, {client}, {}) !=
            RoutingEndpointType::VirtualOutput ||
        PersistentRoutes::KnownApplicationEndpointType(
            "virtual:TimoxVasio:app:renoise.exe:output:9", {}, {}, saved) !=
            RoutingEndpointType::VirtualOutput ||
        PersistentRoutes::KnownApplicationEndpointType(
            "virtual:TimoxVasio:app:unknown.exe:output:9", {}, {client}, saved) ||
        PersistentRoutes::KnownApplicationEndpointType(
            "virtual:TimoxVasio:app:renoise.exe:output:257", {}, {client}, saved) ||
        PersistentRoutes::KnownApplicationEndpointType(
            "virtual:TimoxVasio:app:renoise.exe:output:9", profiles, {client}, saved)) {
        std::fputs("FAIL: known application pending endpoint validation is incorrect\n", stderr);
        return 1;
    }
    return 0;
}
