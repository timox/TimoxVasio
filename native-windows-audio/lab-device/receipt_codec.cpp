#include "receipt_codec.h"
namespace timox::lab {
namespace {
bool Guid(const std::wstring& s) {
    if (s.size() != 36) return false;
    for (std::size_t i = 0; i != s.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (s[i] != L'-') return false; }
        else if (!((s[i] >= L'0' && s[i] <= L'9') || (s[i] >= L'a' && s[i] <= L'f') || (s[i] >= L'A' && s[i] <= L'F'))) return false;
    }
    return true;
}
bool Valid(const Receipt& r) {
    return r.schema == 1 && Guid(r.operationId) && Guid(r.machineId) &&
        r.instanceId == L"ROOT\\TimoxWindowsAudioLab\\" + r.operationId &&
        r.hardwareId == HardwareId && r.classId == MediaClass &&
        (r.stage == Stage::Prepared || r.stage == Stage::Registered || r.stage == Stage::RemovalPending || r.stage == Stage::Removed);
}
}
bool EncodeReceipt(const Receipt& r, std::vector<std::uint8_t>& bytes) {
    bytes.clear(); if (!Valid(r)) return false;
    bytes.assign(ReceiptWireSize, 0); bytes[0] = 'T'; bytes[1] = 'L'; bytes[2] = 'R'; bytes[3] = '1'; bytes[4] = 1;
    bytes[8] = static_cast<std::uint8_t>(r.stage);
    for (std::size_t i = 0; i != 36; ++i) { bytes[12+i] = static_cast<std::uint8_t>(r.operationId[i]); bytes[48+i] = static_cast<std::uint8_t>(r.machineId[i]); }
    return true;
}
bool DecodeReceipt(const std::uint8_t* bytes, std::size_t size, Receipt& result) {
    if (!bytes || size != ReceiptWireSize || bytes[0] != 'T' || bytes[1] != 'L' || bytes[2] != 'R' || bytes[3] != '1' ||
        bytes[4] != 1 || bytes[5] || bytes[6] || bytes[7] || bytes[8] > 3 || bytes[9] || bytes[10] || bytes[11]) return false;
    Receipt r; r.stage = static_cast<Stage>(bytes[8]);
    for (std::size_t i = 0; i != 36; ++i) { r.operationId += static_cast<wchar_t>(bytes[12+i]); r.machineId += static_cast<wchar_t>(bytes[48+i]); }
    r.instanceId = L"ROOT\\TimoxWindowsAudioLab\\" + r.operationId;
    if (!Valid(r)) return false;
    result = std::move(r); return true;
}
}
