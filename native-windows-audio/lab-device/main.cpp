#include "device_win32.h"
#include "lab_cli.h"
#include <cstdio>
#include <exception>
int wmain(int argc, wchar_t** argv) {
    using namespace timox::lab;
    try {
        std::vector<std::wstring> args;
        for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
        const auto command = ParseCommand(args);
        if (!command.valid) { std::fputs("Invalid arguments. Use --help.\n", stderr); return 2; }
        if (command.help) {
            std::puts("TimoxNativeLabDevice inspect | create [--apply] | remove [--apply]\n"
                "No arguments: help. create/remove default to dry-run.\n"
                "--apply: dedicated authorized test VM only; no automatic elevation.\n"
                "Creates/removes only the fixed Timox ROOT device. Does not install INF, sign, or change Windows security.");
            return 0;
        }
        WindowsDevicePlatform platform(command.request.apply);
        const auto r = Execute(command.request, platform);
        const char* status = r.status == Status::Success ? "success" : r.status == Status::DryRun ? "dry-run" :
            r.status == Status::Refused ? "refused" : r.status == Status::RebootRequired ? "reboot-required" : "platform-error";
        // Instance IDs in Result originate only from the validated fixed namespace.
        std::printf("{\"status\":\"%s\",\"win32Error\":%lu,\"rebootRequired\":%s,\"recoveryRequired\":%s}\n",
            status, static_cast<unsigned long>(r.win32Error), r.rebootRequired ? "true" : "false", r.recoveryRequired ? "true" : "false");
        if (!r.instanceId.empty()) std::printf("Instance: %ls\n", r.instanceId.c_str());
        return r.status == Status::RebootRequired ? 3010 : r.status == Status::PlatformError ? 3 : r.status == Status::Refused ? 2 : 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Failed: %s. Inspect the durable receipt from the VM console before retrying.\n", e.what()); return 3;
    }
}
