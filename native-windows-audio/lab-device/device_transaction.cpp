#include "device_transaction.h"
namespace timox::lab {
namespace {
wchar_t Fold(wchar_t c) noexcept { return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + (L'a' - L'A')) : c; }
bool Equal(const std::wstring& a, const std::wstring& b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (Fold(a[i]) != Fold(b[i])) return false;
    return true;
}
bool Guid(const std::wstring& value) noexcept {
    if (value.size() != 36) return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (value[i] != L'-') return false; }
        else { const auto c = Fold(value[i]); if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) return false; }
    }
    return true;
}
bool HardwareMatches(const Device& device) {
    for (const auto& id : device.hardwareIds) if (Equal(id, HardwareId)) return true;
    return false;
}
bool Own(const Device& device, const Receipt& receipt) {
    return Equal(device.instanceId, receipt.instanceId) && Equal(device.classId, MediaClass) && HardwareMatches(device);
}
bool Valid(const Receipt& r, const std::wstring& machine) {
    return r.schema == 1 && Guid(r.operationId) && !machine.empty() && machine.size() <= 256 &&
        r.machineId == machine && Equal(r.hardwareId, HardwareId) && Equal(r.classId, MediaClass) &&
        Equal(r.instanceId, std::wstring(L"ROOT\\TimoxWindowsAudioLab\\") + r.operationId) &&
        (r.stage == Stage::Prepared || r.stage == Stage::Registered ||
         r.stage == Stage::RemovalPending || r.stage == Stage::Removed);
}
Result Outcome(Status status, const std::wstring& instance = {}, std::uint32_t error = 0, bool recovery = false) {
    return {status, error, instance, status == Status::RebootRequired, recovery};
}
struct LockGuard { DevicePlatform& p; ~LockGuard() { p.Unlock(); } };
struct ElementGuard { DevicePlatform& p; ~ElementGuard() { p.ReleaseElement(); } };
Result Remove(Receipt& receipt, DevicePlatform& p) {
    Device device; bool found = false;
    auto error = p.FindInstance(receipt.instanceId, device, found);
    if (error) return Outcome(Status::PlatformError, receipt.instanceId, error, true);
    if (found && receipt.stage == Stage::Removed) return Outcome(Status::Refused, receipt.instanceId);
    if (found && !Own(device, receipt)) return Outcome(Status::Refused, receipt.instanceId, 0, true);
    if (found) {
        receipt.stage = Stage::RemovalPending;
        error = p.WriteReceiptDurably(receipt);
        if (error) return Outcome(Status::PlatformError, receipt.instanceId, error, true);
        bool reboot = false; error = p.RemoveInstance(receipt.instanceId, reboot);
        if (error) return Outcome(Status::PlatformError, receipt.instanceId, error, true);
        if (reboot) return Outcome(Status::RebootRequired, receipt.instanceId, 0, true);
        error = p.FindInstance(receipt.instanceId, device, found);
        if (error) return Outcome(Status::PlatformError, receipt.instanceId, error, true);
        if (found) return Outcome(Status::Refused, receipt.instanceId, 0, true);
    }
    receipt.stage = Stage::Removed;
    error = p.WriteReceiptDurably(receipt);
    return error ? Outcome(Status::PlatformError, receipt.instanceId, error, true) : Outcome(Status::Success, receipt.instanceId);
}
}
Result Execute(const Request& request, DevicePlatform& p) {
    if (request.action != Action::Inspect && request.action != Action::Create && request.action != Action::Remove)
        return Outcome(Status::Refused);
    auto error = p.Lock();
    if (error) return Outcome(Status::PlatformError, {}, error);
    LockGuard lock{p};
    std::wstring machine;
    error = p.MachineIdentity(machine);
    if (error) return Outcome(Status::PlatformError, {}, error);
    if (machine.empty() || machine.size() > 256 || machine.find(L'\0') != std::wstring::npos) return Outcome(Status::Refused);
    Receipt receipt; bool hasReceipt = false;
    error = p.ReadReceipt(receipt, hasReceipt);
    if (error) return Outcome(Status::PlatformError, {}, error);
    if (hasReceipt && !Valid(receipt, machine)) return Outcome(Status::Refused);
    if (request.action == Action::Remove) {
        if (!hasReceipt) return Outcome(Status::Refused);
        if (!request.apply) {
            Device device; bool found = false; error = p.FindInstance(receipt.instanceId, device, found);
            if (error) return Outcome(Status::PlatformError, receipt.instanceId, error);
            if (found && receipt.stage == Stage::Removed) return Outcome(Status::Refused, receipt.instanceId);
            if (found && !Own(device, receipt)) return Outcome(Status::Refused, receipt.instanceId);
            return Outcome(Status::DryRun, receipt.instanceId);
        }
        return Remove(receipt, p);
    }
    std::vector<Device> devices;
    error = p.Inventory(devices);
    if (error) return Outcome(Status::PlatformError, {}, error);
    if (request.action == Action::Inspect) return Outcome(Status::Success, hasReceipt ? receipt.instanceId : std::wstring{});
    if (hasReceipt && receipt.stage != Stage::Removed) return Outcome(Status::Refused, receipt.instanceId, 0, true);
    for (const auto& device : devices) if (HardwareMatches(device)) return Outcome(Status::Refused);
    if (!request.apply) return Outcome(Status::DryRun);
    receipt = Receipt{}; receipt.machineId = machine;
    error = p.NewOperationId(receipt.operationId);
    if (error) return Outcome(Status::PlatformError, {}, error);
    if (!Guid(receipt.operationId)) return Outcome(Status::Refused);
    receipt.instanceId = std::wstring(L"ROOT\\TimoxWindowsAudioLab\\") + receipt.operationId;
    error = p.CreateElement(receipt.instanceId);
    if (error) return Outcome(Status::PlatformError, receipt.instanceId, error);
    ElementGuard element{p};
    error = p.SetHardwareId();
    if (error) return Outcome(Status::PlatformError, receipt.instanceId, error);
    error = p.WriteReceiptDurably(receipt);
    if (error) return Outcome(Status::PlatformError, receipt.instanceId, error);
    error = p.RegisterElement();
    if (error) {
        Device device; bool found = false;
        // Preserve primary error; durable intent remains even if this observation fails.
        (void)p.FindInstance(receipt.instanceId, device, found);
        return Outcome(Status::PlatformError, receipt.instanceId, error, true);
    }
    Device device; bool found = false;
    error = p.FindInstance(receipt.instanceId, device, found);
    if (error) return Outcome(Status::PlatformError, receipt.instanceId, error, true);
    if (!found || !Own(device, receipt)) return Outcome(Status::Refused, receipt.instanceId, 0, true);
    devices.clear(); error = p.Inventory(devices);
    if (error) return Outcome(Status::PlatformError, receipt.instanceId, error, true);
    for (const auto& other : devices) {
        if (HardwareMatches(other) && !Equal(other.instanceId, receipt.instanceId)) {
            auto rollback = Remove(receipt, p);
            return rollback.status == Status::Success ? Outcome(Status::Refused, receipt.instanceId) : rollback;
        }
    }
    receipt.stage = Stage::Registered; error = p.WriteReceiptDurably(receipt);
    return error ? Outcome(Status::PlatformError, receipt.instanceId, error, true) : Outcome(Status::Success, receipt.instanceId);
}
}
