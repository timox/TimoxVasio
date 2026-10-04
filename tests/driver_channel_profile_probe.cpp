#include <windows.h>
#include <objbase.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "iasiodrv.h"

int wmain(int argc, wchar_t** argv) {
    if (argc != 7 || std::wstring(argv[1]) != L"--dll" ||
        std::wstring(argv[3]) != L"--expect-input" ||
        std::wstring(argv[5]) != L"--expect-output") {
        std::fwprintf(stderr, L"Usage: DriverChannelProfileProbe --dll <path> --expect-input <count> --expect-output <count>\n");
        return 2;
    }
    const long expectedInputs = std::wcstol(argv[4], nullptr, 10);
    const long expectedOutputs = std::wcstol(argv[6], nullptr, 10);
    if (expectedInputs < 1 || expectedInputs > 256 || expectedOutputs < 1 || expectedOutputs > 256) return 2;

    HMODULE module = LoadLibraryW(argv[2]);
    if (!module) {
        std::fwprintf(stderr, L"LoadLibraryW failed: %lu\n", GetLastError());
        return 3;
    }
    using GetClassObject = HRESULT (WINAPI*)(REFCLSID, REFIID, void**);
    const auto getClassObject = reinterpret_cast<GetClassObject>(
        GetProcAddress(module, "DllGetClassObject"));
    constexpr CLSID driverClass{0xa4d39126, 0x78cb, 0x4d89,
        {0x9e, 0x0a, 0x54, 0x49, 0x4d, 0x4f, 0x58, 0x56}};
    IClassFactory* factory = nullptr;
    IASIO* driver = nullptr;
    HRESULT result = getClassObject
        ? getClassObject(driverClass, IID_IClassFactory, reinterpret_cast<void**>(&factory))
        : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    if (SUCCEEDED(result))
        result = factory->CreateInstance(nullptr, driverClass, reinterpret_cast<void**>(&driver));

    bool passed = SUCCEEDED(result) && driver;
    long inputs = -1;
    long outputs = -1;
    if (passed) {
        char name[32]{};
        driver->getDriverName(name);
        passed = std::string(name) == "TimoxVasio" &&
            driver->getChannels(&inputs, &outputs) == ASE_OK &&
            inputs == expectedInputs && outputs == expectedOutputs;
    }
    if (passed) {
        ASIOChannelInfo channel{};
        channel.isInput = ASIOTrue;
        channel.channel = expectedInputs - 1;
        passed = driver->getChannelInfo(&channel) == ASE_OK;
        channel.channel = expectedInputs;
        passed = passed && driver->getChannelInfo(&channel) == ASE_InvalidParameter;
        channel.isInput = ASIOFalse;
        channel.channel = expectedOutputs - 1;
        passed = passed && driver->getChannelInfo(&channel) == ASE_OK;
        channel.channel = expectedOutputs;
        passed = passed && driver->getChannelInfo(&channel) == ASE_InvalidParameter;
    }

    if (driver) driver->Release();
    if (factory) factory->Release();
    FreeLibrary(module);
    if (!passed) {
        std::fwprintf(stderr, L"TimoxVasio profile mismatch: expected %ld/%ld channels, got %ld/%ld.\n",
                     expectedInputs, expectedOutputs, inputs, outputs);
        return 1;
    }
    std::printf("PASS: TimoxVasio advertises %ld/%ld channels for this process.\n",
                expectedInputs, expectedOutputs);
    return 0;
}
