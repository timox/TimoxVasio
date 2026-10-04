#include <windows.h>
#include <objbase.h>

#include "iasiodrv.h"
#include "asiolist.h"

#include <cstdio>
#include <string>

namespace {
void bufferSwitch(long, ASIOBool) {}
void sampleRateChanged(ASIOSampleRate) {}
long asioMessages(long, long, void*, double*) { return 0; }
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 3 || std::wstring(argv[1]) != L"--driver") {
        std::fwprintf(stderr, L"Usage: VasioExternalHostProbe --driver TimoxVasio\n");
        return 2;
    }

    char requestedNameBuffer[MAXDRVNAMELEN]{};
    if (!WideCharToMultiByte(CP_UTF8, 0, argv[2], -1, requestedNameBuffer,
        static_cast<int>(sizeof(requestedNameBuffer)), nullptr, nullptr)) return 2;
    const std::string requestedName(requestedNameBuffer);
    AsioDriverList drivers;
    int selected = -1;
    for (int index = 0; index < drivers.asioGetNumDev(); ++index) {
        char name[MAXDRVNAMELEN]{};
        if (drivers.asioGetDriverName(index, name, sizeof(name)) == 0 && name == requestedName) {
            selected = index;
            break;
        }
    }
    if (selected < 0) {
        std::fprintf(stderr, "Steinberg AsioDriverList did not discover %s.\n", requestedName.c_str());
        return 3;
    }

    IASIO* driver = nullptr;
    if (drivers.asioOpenDriver(selected, reinterpret_cast<void**>(&driver)) != 0 || !driver) {
        std::fprintf(stderr, "Steinberg AsioDriverList failed to open %s.\n", requestedName.c_str());
        return 4;
    }
    if (driver->init(nullptr) != ASIOTrue) {
        char message[128]{};
        driver->getErrorMessage(message);
        std::fprintf(stderr, "%s init failed: %s\n", requestedName.c_str(), message);
        drivers.asioCloseDriver(selected);
        return 5;
    }

    long minimum = 0, maximum = 0, preferred = 0, granularity = 0;
    if (driver->getBufferSize(&minimum, &maximum, &preferred, &granularity) != ASE_OK ||
        preferred < minimum || preferred > maximum || preferred <= 0 || (preferred & (preferred - 1)) != 0) {
        std::fprintf(stderr, "%s returned an invalid buffer contract.\n", requestedName.c_str());
        drivers.asioCloseDriver(selected);
        return 6;
    }

    ASIOBufferInfo buffers[2]{};
    buffers[0].isInput = ASIOFalse;
    buffers[0].channelNum = 0;
    buffers[1].isInput = ASIOFalse;
    buffers[1].channelNum = 1;
    ASIOCallbacks callbacks{};
    callbacks.bufferSwitch = bufferSwitch;
    callbacks.sampleRateDidChange = sampleRateChanged;
    callbacks.asioMessage = asioMessages;
    if (driver->createBuffers(buffers, 2, preferred, &callbacks) != ASE_OK) {
        std::fprintf(stderr, "%s createBuffers failed at %ld frames.\n", requestedName.c_str(), preferred);
        drivers.asioCloseDriver(selected);
        return 7;
    }
    if (driver->start() != ASE_OK) {
        char message[128]{};
        driver->getErrorMessage(message);
        std::fprintf(stderr, "%s start failed: %s\n", requestedName.c_str(), message);
        driver->disposeBuffers();
        drivers.asioCloseDriver(selected);
        return 8;
    }

    if (driver->stop() != ASE_OK || driver->disposeBuffers() != ASE_OK ||
        driver->init(nullptr) != ASIOTrue) {
        char message[128]{};
        driver->getErrorMessage(message);
        std::fprintf(stderr, "%s reset/re-init failed: %s\n", requestedName.c_str(), message);
        drivers.asioCloseDriver(selected);
        return 10;
    }
    buffers[0] = {};
    buffers[0].isInput = ASIOFalse;
    buffers[0].channelNum = 0;
    buffers[1] = {};
    buffers[1].isInput = ASIOFalse;
    buffers[1].channelNum = 1;
    if (driver->createBuffers(buffers, 2, preferred, &callbacks) != ASE_OK ||
        driver->start() != ASE_OK) {
        char message[128]{};
        driver->getErrorMessage(message);
        std::fprintf(stderr, "%s reset/restart failed: %s\n", requestedName.c_str(), message);
        driver->stop();
        driver->disposeBuffers();
        drivers.asioCloseDriver(selected);
        return 11;
    }

    ASIOSampleRate sampleRate = 0;
    driver->getSampleRate(&sampleRate);
    std::printf("PASS: %s via Steinberg AsioDriverList: init/createBuffers/start and reset/restart with the running external engine; rate=%.0f Hz, buffer=%ld frames.\n",
        requestedName.c_str(), sampleRate, preferred);
    std::fflush(stdout);
    Sleep(2000);

    const bool stopped = driver->stop() == ASE_OK;
    const bool disposed = driver->disposeBuffers() == ASE_OK;
    drivers.asioCloseDriver(selected);
    if (!stopped || !disposed) return 9;

    driver = nullptr;
    if (drivers.asioOpenDriver(selected, reinterpret_cast<void**>(&driver)) != 0 || !driver ||
        driver->init(nullptr) != ASIOTrue ||
        driver->createBuffers(buffers, 2, preferred, &callbacks) != ASE_OK ||
        driver->start() != ASE_OK) {
        char message[128]{};
        if (driver) driver->getErrorMessage(message);
        std::fprintf(stderr, "%s full reset/reopen failed: %s\n", requestedName.c_str(), message);
        if (driver) {
            driver->stop();
            driver->disposeBuffers();
            drivers.asioCloseDriver(selected);
        }
        return 12;
    }
    const bool restarted = driver->stop() == ASE_OK && driver->disposeBuffers() == ASE_OK;
    drivers.asioCloseDriver(selected);
    if (!restarted) return 13;
    std::puts("PASS: ASIO object can be released and reopened for the same process and VASIO driver.");
    return 0;
}
