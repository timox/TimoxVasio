#include <windows.h>
#include <objbase.h>
#include <cstdio>
#include <string>
#include <vector>

#include "iasiodrv.h"
#include "asiolist.h"
#include "audio_transport.h"
#include "vasio_compatibility_profile.h"

struct DirectDriver {
    std::wstring path;
    CLSID clsid{};
};

static bool checkDriver(IASIO* driver, const char* expectedName) {
    if (!driver) return false;

    char name[32]{};
    driver->getDriverName(name);
    long inputs = -1;
    long outputs = -1;
    long minimum = 0;
    long maximum = 0;
    long preferred = 0;
    long granularity = 0;

    const auto expectedChannels = VasioCompatibilityProfile::ChannelCountsForCurrentProcess();
    if (driver->getChannels(&inputs, &outputs) != ASE_OK ||
        inputs != expectedChannels.inputs || outputs != expectedChannels.outputs) {
        std::fprintf(stderr, "channel contract failed for %s: inputs=%ld outputs=%ld\n", name, inputs, outputs);
        return false;
    }
    for (const auto direction : {ASIOTrue, ASIOFalse}) {
        ASIOChannelInfo highChannel{};
        highChannel.isInput = direction;
        const auto limit = direction == ASIOTrue ? expectedChannels.inputs : expectedChannels.outputs;
        highChannel.channel = limit - 1;
        if (driver->getChannelInfo(&highChannel) != ASE_OK) {
            std::fprintf(stderr, "channel %ld unavailable for %s (%s)\n", highChannel.channel, name,
                         direction == ASIOTrue ? "input" : "output");
            return false;
        }
        highChannel.channel = limit;
        if (driver->getChannelInfo(&highChannel) != ASE_InvalidParameter) {
            std::fprintf(stderr, "out-of-range channel %ld accepted for %s\n", highChannel.channel, name);
            return false;
        }
    }
    if (driver->getBufferSize(&minimum, &maximum, &preferred, &granularity) != ASE_OK ||
        minimum <= 0 || maximum < minimum || preferred < minimum || preferred > maximum || granularity == 0) {
        std::fprintf(stderr, "buffer contract failed for %s: min=%ld max=%ld preferred=%ld granularity=%ld\n",
                     name, minimum, maximum, preferred, granularity);
        return false;
    }
    if (expectedName && std::string(name) != expectedName) {
        std::fprintf(stderr, "driver identity mismatch: expected %s, got %s\n", expectedName, name);
        return false;
    }
    std::printf("%s: inputs=%ld outputs=%ld buffers=%ld..%ld preferred=%ld granularity=%ld\n",
                name, inputs, outputs, minimum, maximum, preferred, granularity);
    return true;
}

static bool checkPhysicalClockCapabilities(IASIO* driver) {
    constexpr std::uint32_t physicalRate = 48000;
    constexpr std::uint32_t physicalBlock = 512;
    auto engine = AudioClientMapping::OpenEngine(1, GetCurrentProcessId(), 0);
    if (!engine || !engine->SetSampleRate(physicalRate) ||
        !engine->SetBlockFrames(physicalBlock) || !engine->SetEngineAttached(true)) {
        std::fprintf(stderr, "could not expose the simulated physical ASIO clock\n");
        return false;
    }
    const auto matchingRate = driver->canSampleRate(physicalRate);
    const auto mismatchingRate = driver->canSampleRate(44100);
    if (matchingRate != ASE_OK || mismatchingRate != ASE_NotPresent) {
        std::fprintf(stderr,
            "sample-rate capability disagrees with attached physical clock: 48000=%ld 44100=%ld\n",
            matchingRate, mismatchingRate);
        return false;
    }
    return true;
}

static bool probeDirect(const DirectDriver& target) {
    HMODULE module = LoadLibraryW(target.path.c_str());
    if (!module) {
        std::fprintf(stderr, "LoadLibraryW failed (%lu): %ls\n", GetLastError(), target.path.c_str());
        return false;
    }

    using GetClassObject = HRESULT (WINAPI*)(REFCLSID, REFIID, void**);
    auto getClassObject = reinterpret_cast<GetClassObject>(GetProcAddress(module, "DllGetClassObject"));
    if (!getClassObject) {
        std::fprintf(stderr, "DllGetClassObject export missing: %ls\n", target.path.c_str());
        FreeLibrary(module);
        return false;
    }

    IClassFactory* factory = nullptr;
    HRESULT result = getClassObject(target.clsid, IID_IClassFactory, reinterpret_cast<void**>(&factory));
    IASIO* driver = nullptr;
    if (SUCCEEDED(result)) {
        result = factory->CreateInstance(nullptr, target.clsid, reinterpret_cast<void**>(&driver));
        factory->Release();
    }
    bool passed = SUCCEEDED(result) && driver;
    if (passed && driver->init(nullptr) != ASIOTrue) {
        char message[128]{};
        driver->getErrorMessage(message);
        std::fprintf(stderr, "direct ASIOInit failed: %s\n", message);
        passed = false;
    }
    if (passed) passed = checkDriver(driver, nullptr) && checkPhysicalClockCapabilities(driver);
    if (driver) driver->Release();
    FreeLibrary(module);
    if (!passed) std::fprintf(stderr, "Direct COM activation failed (0x%08lx): %ls\n",
                              static_cast<unsigned long>(result), target.path.c_str());
    return passed;
}

static bool probeRegistered(const char* expectedName) {
    AsioDriverList drivers;
    int selected = -1;
    for (int index = 0; index < drivers.asioGetNumDev(); ++index) {
        char name[MAXDRVNAMELEN]{};
        if (drivers.asioGetDriverName(index, name, sizeof(name)) == 0 && std::string(name) == expectedName) {
            selected = index;
            break;
        }
    }
    if (selected < 0) {
        std::fprintf(stderr, "SDK AsioDriverList did not discover %s\n", expectedName);
        return false;
    }

    LPVOID instance = nullptr;
    if (drivers.asioOpenDriver(selected, &instance) != 0 || !instance) {
        std::fprintf(stderr, "SDK AsioDriverList could not instantiate %s\n", expectedName);
        return false;
    }
    auto* driver = static_cast<IASIO*>(instance);
    bool passed = true;
    if (driver->init(nullptr) != ASIOTrue) {
        char message[128]{};
        driver->getErrorMessage(message);
        std::fprintf(stderr, "%s ASIOInit failed: %s\n", expectedName, message);
        passed = false;
    } else {
        passed = checkDriver(driver, expectedName);
    }
    if (passed && expectedName && std::string(expectedName) == "TimoxVasio")
        passed = checkPhysicalClockCapabilities(driver);
    drivers.asioCloseDriver(selected);
    return passed;
}

int wmain(int argc, wchar_t** argv) {
    std::vector<DirectDriver> direct;
    std::vector<std::string> registered;
    for (int i = 1; i < argc;) {
        if (std::wcscmp(argv[i], L"--dll") == 0 && i + 3 < argc && std::wcscmp(argv[i + 2], L"--clsid") == 0) {
            DirectDriver target;
            target.path = argv[i + 1];
            if (FAILED(CLSIDFromString(argv[i + 3], &target.clsid))) {
                std::fprintf(stderr, "Invalid CLSID: %ls\n", argv[i + 3]);
                return 2;
            }
            direct.push_back(std::move(target));
            i += 4;
        } else if (std::wcscmp(argv[i], L"--registered") == 0 && i + 1 < argc) {
            char name[MAXDRVNAMELEN]{};
            if (WideCharToMultiByte(CP_UTF8, 0, argv[i + 1], -1, name, sizeof(name), nullptr, nullptr) == 0) return 2;
            registered.emplace_back(name);
            i += 2;
        } else {
            std::fprintf(stderr, "Usage: DriverProbe --dll <path> --clsid <guid> [...] | --registered <name> [...]\n");
            return 2;
        }
    }
    if (direct.empty() && registered.empty()) return 2;

    bool passed = true;
    for (const auto& target : direct) passed = probeDirect(target) && passed;
    for (const auto& name : registered) passed = probeRegistered(name.c_str()) && passed;
    return passed ? 0 : 1;
}
