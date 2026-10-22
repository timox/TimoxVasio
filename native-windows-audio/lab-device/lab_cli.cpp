#include "lab_cli.h"
namespace timox::lab {
ParsedCommand ParseCommand(const std::vector<std::wstring>& args) {
    ParsedCommand out;
    if (args.empty() || (args.size() == 1 && args[0] == L"--help")) { out.valid = true; out.help = true; return out; }
    if (args.size() > 2) return out;
    if (args[0] == L"inspect") out.request.action = Action::Inspect;
    else if (args[0] == L"create") out.request.action = Action::Create;
    else if (args[0] == L"remove") out.request.action = Action::Remove;
    else return out;
    if (args.size() == 2) {
        if (args[1] != L"--apply" || out.request.action == Action::Inspect) return out;
        out.request.apply = true;
    }
    out.valid = true; return out;
}
}
