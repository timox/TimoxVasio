#include "persistent_routes.h"
#include "audio_transport.h"

#include <cstdio>

int main() {
    auto mapping = AudioClientMapping::CreateClient(1, 123, 48000, 256);
    if (!mapping) return 2;
    mapping->SetEngineAttached(true);
    VasioClientSnapshot client{{1, 123, L"Renoise.exe"}, std::shared_ptr<AudioClientMapping>(std::move(mapping))};
    const AudioRoute route{"route-1", "virtual:TimoxVasio:123:output:1",
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
    auto replacement = AudioClientMapping::CreateClient(1, 124, 48000, 256);
    if (!replacement) return 2;
    replacement->SetEngineAttached(true);
    replacement->SetActiveChannels(inputs, outputs);
    VasioClientSnapshot restarted{{1, 124, L"Renoise.exe"},
        std::shared_ptr<AudioClientMapping>(std::move(replacement))};
    const auto reconnected = PersistentRoutes::Resolve(saved, {restarted});
    if (reconnected.size() != 1 || reconnected[0].sourceEndpointId !=
        "virtual:TimoxVasio:124:output:1") {
        std::fputs("FAIL: route did not follow restarted application\n", stderr);
        return 1;
    }
    if (!PersistentRoutes::Resolve(saved, {client, restarted}).empty()) {
        std::fputs("FAIL: route bound ambiguously to two instances\n", stderr);
        return 1;
    }
    return 0;
}
