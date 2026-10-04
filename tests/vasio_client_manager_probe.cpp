#include "vasio_client_manager.h"
#include "iasiodrv.h"

#include <windows.h>

#include <cstdio>
#include <string>

int wmain(int argc, wchar_t** argv) {
    if (argc == 5 && std::wstring(argv[1]) == L"--client" && std::wstring(argv[3]) == L"--driver") {
        const auto driverId = static_cast<std::uint32_t>(_wtoi(argv[4]));
        if (driverId != 1) return 2;
        HMODULE module = LoadLibraryW(argv[2]);
        if (!module) return 10;
        CLSID classId{0xa4d39126, 0x78cb, 0x4d89,
            {0x9e, 0x0a, 0x54, 0x49, 0x4d, 0x4f, 0x58, 0x56}};
        using GetClassObject = HRESULT (WINAPI*)(REFCLSID, REFIID, void**);
        auto getClassObject = reinterpret_cast<GetClassObject>(GetProcAddress(module, "DllGetClassObject"));
        IClassFactory* factory = nullptr;
        IASIO* driver = nullptr;
        const auto initialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        const bool created = getClassObject &&
            SUCCEEDED(getClassObject(classId, IID_IClassFactory, reinterpret_cast<void**>(&factory))) &&
            SUCCEEDED(factory->CreateInstance(nullptr, classId, reinterpret_cast<void**>(&driver)));
        const bool attached = created && driver->init(nullptr) == ASIOTrue;
        long minimum = 0, maximum = 0, preferred = 0, granularity = 0;
        ASIOSampleRate sampleRate = 0;
        const bool configured = attached &&
            driver->getBufferSize(&minimum, &maximum, &preferred, &granularity) == ASE_OK &&
            preferred == 512 && driver->getSampleRate(&sampleRate) == ASE_OK && sampleRate == 96000.0;
        if (configured) Sleep(4000);
        if (driver) driver->Release();
        if (factory) factory->Release();
        if (initialized) CoUninitialize();
        FreeLibrary(module);
        if (!attached) return 12;
        if (!configured) return 13;
        std::puts("PASS: ASIO initialization received the configured sample rate and preferred buffer size.");
        return 0;
    }

    if (argc != 3 || std::wstring(argv[1]) != L"--dll" || std::wstring(argv[2]).empty()) {
        std::fwprintf(stderr, L"Usage: VasioClientManagerProbe --dll <VASIO DLL path>\n");
        return 2;
    }
    VasioClientManager manager;
    manager.SetDriverConfiguration(512, 96000);
    if (!manager.Start()) {
        std::fprintf(stderr, "VasioClientManager failed to start\n");
        return 1;
    }
    wchar_t executable[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, executable, MAX_PATH)) { manager.Stop(); return 3; }
    wchar_t commandLine[3 * MAX_PATH]{};
    swprintf_s(commandLine, L"\"%s\" --client \"%s\" --driver 1", executable, argv[2]);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, commandLine, nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                        &startup, &process)) {
        manager.Stop();
        std::fwprintf(stderr, L"CreateProcessW failed: %lu\n", GetLastError());
        return 4;
    }
    bool snapshotPinned = false;
    const ULONGLONG snapshotDeadline = GetTickCount64() + 4000;
    while (GetTickCount64() < snapshotDeadline && !snapshotPinned) {
        for (const auto& snapshot : manager.GetClientSnapshots()) {
            if (snapshot.info.driverId == 1 && snapshot.info.processId == process.dwProcessId &&
                snapshot.mapping && snapshot.mapping->EngineAttached()) {
                snapshotPinned = true;
                break;
            }
        }
        if (!snapshotPinned) Sleep(10);
    }
    const bool attachedInitially = snapshotPinned;
    if (snapshotPinned) {
        Sleep(800);
        snapshotPinned = false;
        for (const auto& snapshot : manager.GetClientSnapshots()) {
            if (snapshot.info.driverId != 1 || snapshot.info.processId != process.dwProcessId) continue;
            snapshotPinned = snapshot.mapping && snapshot.mapping->EngineAttached();
            break;
        }
    }
    const DWORD wait = WaitForSingleObject(process.hProcess, 6000);
    if (wait != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 5);
    DWORD exitCode = 5;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    manager.Stop();
    if (wait != WAIT_OBJECT_0 || exitCode != 0 || !snapshotPinned) {
        std::fprintf(stderr, "VASIO client attachment/snapshot failed (exit=%lu, initiallyPinned=%s, pinnedAfterRescans=%s).\n",
            exitCode, attachedInitially ? "true" : "false", snapshotPinned ? "true" : "false");
        return 5;
    }
    std::puts("PASS: VASIO client discovery and attachment across processes.");
    return 0;
}
