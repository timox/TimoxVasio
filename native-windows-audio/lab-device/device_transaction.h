#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace timox::lab {
inline constexpr wchar_t HardwareId[] = L"Root\\TimoxWindowsAudio";
inline constexpr wchar_t MediaClass[] = L"{4d36e96c-e325-11ce-bfc1-08002be10318}";
enum class Action { Inspect, Create, Remove };
enum class Stage { Prepared, Registered, RemovalPending, Removed };
enum class Status { Success, DryRun, Refused, PlatformError, RebootRequired };
struct Request { Action action = Action::Inspect; bool apply = false; };
struct Receipt {
    unsigned schema = 1;
    std::wstring operationId, machineId, instanceId;
    std::wstring hardwareId = HardwareId, classId = MediaClass;
    Stage stage = Stage::Prepared;
};
struct Device {
    std::wstring instanceId, classId;
    std::vector<std::wstring> hardwareIds;
    bool present = true;
};
struct Result {
    Status status = Status::Success;
    std::uint32_t win32Error = 0;
    std::wstring instanceId;
    bool rebootRequired = false, recoveryRequired = false;
};
// Methods return original Win32 error (zero = success). Missing receipt/device
// is represented by found=false, never inferred from arbitrary error codes.
// Real storage must authenticate location/ACL/reparse safety before ReadReceipt.
// Inventory includes phantom devices and rejects malformed/oversized properties.
class DevicePlatform {
public:
    virtual ~DevicePlatform() = default;
    virtual std::uint32_t Lock() = 0;
    virtual void Unlock() noexcept = 0;
    virtual std::uint32_t MachineIdentity(std::wstring&) = 0;
    virtual std::uint32_t Inventory(std::vector<Device>&) = 0;
    virtual std::uint32_t ReadReceipt(Receipt&, bool& found) = 0;
    virtual std::uint32_t WriteReceiptDurably(const Receipt&) = 0;
    virtual std::uint32_t NewOperationId(std::wstring&) = 0;
    virtual std::uint32_t CreateElement(const std::wstring& instanceId) = 0;
    virtual std::uint32_t SetHardwareId() = 0;
    virtual std::uint32_t RegisterElement() = 0;
    virtual void ReleaseElement() noexcept = 0;
    virtual std::uint32_t FindInstance(const std::wstring&, Device&, bool& found) = 0;
    virtual std::uint32_t RemoveInstance(const std::wstring&, bool& rebootRequired) = 0;
};
Result Execute(const Request&, DevicePlatform&);
}
