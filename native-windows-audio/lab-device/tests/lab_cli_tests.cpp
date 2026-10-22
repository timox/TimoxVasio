#include "lab_cli.h"
#include <cstdio>
using namespace timox::lab;
int main() {
    unsigned failed = 0;
    const auto check = [&](bool ok, const char* name) { if (!ok) { ++failed; std::fprintf(stderr, "FAIL: %s\n", name); } };
    for (const auto& args : std::vector<std::vector<std::wstring>>{{}, {L"--help"}}) {
        const auto p = ParseCommand(args); check(p.valid && p.help && !p.request.apply, "help never requests mutation");
    }
    for (const auto& pair : std::vector<std::pair<std::wstring, Action>>{{L"inspect", Action::Inspect}, {L"create", Action::Create}, {L"remove", Action::Remove}}) {
        const auto p = ParseCommand({pair.first}); check(p.valid && !p.help && !p.request.apply && p.request.action == pair.second, "bare action is read-only");
    }
    for (const auto& pair : std::vector<std::pair<std::wstring, Action>>{{L"create", Action::Create}, {L"remove", Action::Remove}}) {
        const auto p = ParseCommand({pair.first, L"--apply"}); check(p.valid && p.request.apply && p.request.action == pair.second, "explicit apply action accepted");
    }
    for (const auto& args : std::vector<std::vector<std::wstring>>{
        {L"inspect", L"--apply"}, {L"--apply"}, {L"--help", L"--apply"}, {L"create", L"--force"},
        {L"create", L"--apply", L"--apply"}, {L"remove", L"ROOT\\Other\\0"}, {L"create", L"--receipt", L"evil.json"},
        {L"create", L"remove"}, {L"CREATE"}, {L"create", std::wstring(L"--apply\0extra", 13)}}) {
        check(!ParseCommand(args).valid, "unknown contradictory and injected arguments refused");
    }
    std::printf("CLI failures=%u (parser only)\n", failed); return failed ? 1 : 0;
}
