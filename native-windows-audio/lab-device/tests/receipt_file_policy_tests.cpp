#include "receipt_file_policy.h"
#include <cstdio>
using namespace timox::lab;
int main() {
    unsigned failures = 0;
    const auto check = [&](bool ok, const char* text) { if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", text); } };
    FileObservation good; good.owner = Principal::Administrators; good.protectedDacl = true;
    good.entries = {{Principal::System, true, false, 0x001f01ff}, {Principal::Administrators, true, false, 0x001f01ff}};
    check(SafeReceiptObject(good, false), "explicit protected admin and system file accepted");
    auto directory = good; directory.directory = true; check(SafeReceiptObject(directory, true), "private directory accepted");
    for (unsigned i = 0; i != 10; ++i) {
        auto bad = good;
        if (i == 0) bad.reparse = true;
        if (i == 1) bad.links = 2;
        if (i == 2) bad.directory = true;
        if (i == 3) bad.nullDacl = true;
        if (i == 4) bad.protectedDacl = false;
        if (i == 5) bad.owner = Principal::Other;
        if (i == 6) bad.entries.push_back({Principal::Other, true, false, 1});
        if (i == 7) bad.entries[0].inherited = true;
        if (i == 8) bad.entries[0].allow = false;
        if (i == 9) bad.entries[0].mask = 1;
        check(!SafeReceiptObject(bad, false), "untrusted metadata refused");
    }
    auto missing = good; missing.entries.pop_back(); check(!SafeReceiptObject(missing, false), "both trustees required");
    std::printf("receipt policy failures=%u (metadata only)\n", failures); return failures ? 1 : 0;
}
