#include "receipt_codec.h"
#include <cstdio>
#include <limits>
using namespace timox::lab;
int main() {
    unsigned failed = 0;
    const auto check = [&](bool ok, const char* name) { if (!ok) { ++failed; std::fprintf(stderr, "FAIL: %s\n", name); } };
    Receipt original; original.operationId = L"00112233-4455-4677-8899-aabbccddeeff";
    original.machineId = L"11223344-5566-4788-99aa-bbccddeeff00";
    original.instanceId = L"ROOT\\TimoxWindowsAudioLab\\" + original.operationId;
    std::vector<std::uint8_t> encoded;
    for (auto stage : {Stage::Prepared, Stage::Registered, Stage::RemovalPending, Stage::Removed}) {
        original.stage = stage; Receipt decoded;
        const auto ok = EncodeReceipt(original, encoded);
        check(ok && encoded.size() == ReceiptWireSize, "exact bounded receipt encoding");
        if (!ok) continue;
        check(DecodeReceipt(encoded.data(), encoded.size(), decoded) && decoded.stage == stage &&
            decoded.operationId == original.operationId && decoded.machineId == original.machineId &&
            decoded.instanceId == original.instanceId && decoded.hardwareId == HardwareId && decoded.classId == MediaClass,
            "all receipt stages round trip with fixed device identity");
        for (std::size_t size = 0; size < encoded.size(); ++size)
            check(!DecodeReceipt(encoded.data(), size, decoded), "every truncation refused");
        auto extra = encoded; extra.push_back(0); check(!DecodeReceipt(extra.data(), extra.size(), decoded), "trailing bytes refused");
        for (auto offset : {0u, 4u, 8u, 12u, 48u}) {
            auto bad = encoded; bad[offset] = 0xff;
            check(!DecodeReceipt(bad.data(), bad.size(), decoded), "bad magic version stage or identifier refused");
        }
        check(!DecodeReceipt(nullptr, ReceiptWireSize, decoded), "null bytes refused");
        check(!DecodeReceipt(encoded.data(), std::numeric_limits<std::size_t>::max(), decoded), "oversized buffer refused before dereference");
    }
    for (unsigned which = 0; which != 5; ++which) {
        auto bad = original;
        if (which == 0) bad.machineId = L"not-guid";
        if (which == 1) bad.instanceId = L"ROOT\\Other\\0";
        if (which == 2) bad.hardwareId = L"Other";
        if (which == 3) bad.classId = L"Other";
        if (which == 4) bad.stage = static_cast<Stage>(99);
        encoded = {1, 2}; check(!EncodeReceipt(bad, encoded) && encoded.empty(), "invalid receipt cannot produce partial output");
    }
    std::printf("receipt codec failures=%u (memory only)\n", failed); return failed ? 1 : 0;
}
