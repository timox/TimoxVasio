#pragma once
#include "device_transaction.h"
#include <memory>
namespace timox::lab {
class WindowsDevicePlatform final : public DevicePlatform {
public:
    explicit WindowsDevicePlatform(bool apply);
    ~WindowsDevicePlatform() override;
    WindowsDevicePlatform(const WindowsDevicePlatform&) = delete;
    std::uint32_t Lock() override;
    void Unlock() noexcept override;
    std::uint32_t MachineIdentity(std::wstring&) override;
    std::uint32_t Inventory(std::vector<Device>&) override;
    std::uint32_t ReadReceipt(Receipt&, bool&) override;
    std::uint32_t WriteReceiptDurably(const Receipt&) override;
    std::uint32_t NewOperationId(std::wstring&) override;
    std::uint32_t CreateElement(const std::wstring&) override;
    std::uint32_t SetHardwareId() override;
    std::uint32_t RegisterElement() override;
    void ReleaseElement() noexcept override;
    std::uint32_t FindInstance(const std::wstring&, Device&, bool&) override;
    std::uint32_t RemoveInstance(const std::wstring&, bool&) override;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
