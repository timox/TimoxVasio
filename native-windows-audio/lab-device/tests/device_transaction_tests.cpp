#include "device_transaction.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
using namespace timox::lab;
namespace {
constexpr wchar_t Id[] = L"00112233-4455-4677-8899-aabbccddeeff";
constexpr wchar_t Instance[] = L"ROOT\\TimoxWindowsAudioLab\\00112233-4455-4677-8899-aabbccddeeff";
unsigned failures = 0;
void Check(bool condition, const char* name) {
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); }
}
Device Own() { return {Instance, MediaClass, {HardwareId}, true}; }
Receipt Saved(Stage stage = Stage::Registered) {
    Receipt r; r.operationId = Id; r.machineId = L"vm-a"; r.instanceId = Instance; r.stage = stage; return r;
}
struct Fake final : DevicePlatform {
    std::vector<std::string> calls;
    std::vector<Device> devices;
    Receipt receipt;
    bool hasReceipt = false, held = false, reboot = false, registeredDespiteError = false;
    std::string failAt;
    unsigned inventoryCount = 0;
    bool raceDuplicate = false;
    std::uint32_t Call(const char* name) { calls.emplace_back(name); return failAt == name ? 1234u : 0u; }
    std::uint32_t Lock() override { auto e = Call("lock"); if (!e) held = true; return e; }
    void Unlock() noexcept override { held = false; }
    std::uint32_t MachineIdentity(std::wstring& v) override { v = L"vm-a"; return Call("machine"); }
    std::uint32_t Inventory(std::vector<Device>& v) override {
        ++inventoryCount; auto e = Call("inventory");
        if (raceDuplicate && inventoryCount == 2) { auto other = Own(); other.instanceId = L"ROOT\\other\\0"; devices.push_back(other); }
        v = devices;
        return e;
    }
    std::uint32_t ReadReceipt(Receipt& r, bool& found) override { r = receipt; found = hasReceipt; return Call("read"); }
    std::uint32_t WriteReceiptDurably(const Receipt& r) override {
        const char* label = r.stage == Stage::Prepared ? "write-prepared" :
            r.stage == Stage::Registered ? "write-registered" :
            r.stage == Stage::RemovalPending ? "write-removing" : "write-removed";
        auto e = Call(label); if (!e) { receipt = r; hasReceipt = true; } return e;
    }
    std::uint32_t NewOperationId(std::wstring& v) override { v = Id; return Call("new-id"); }
    std::uint32_t CreateElement(const std::wstring& v) override { Check(v == Instance, "fixed instance namespace"); return Call("element"); }
    std::uint32_t SetHardwareId() override { return Call("hardware"); }
    std::uint32_t RegisterElement() override {
        auto e = Call("register"); if (!e || registeredDespiteError) devices.push_back(Own()); return e;
    }
    void ReleaseElement() noexcept override {}
    std::uint32_t FindInstance(const std::wstring& id, Device& d, bool& found) override {
        auto e = Call("find"); found = false;
        for (const auto& item : devices) if (item.instanceId == id) { found = true; d = item; break; }
        return e;
    }
    std::uint32_t RemoveInstance(const std::wstring& id, bool& needsReboot) override {
        auto e = Call("remove"); needsReboot = reboot;
        if (!e && !reboot) devices.erase(std::remove_if(devices.begin(), devices.end(),
            [&](const Device& d) { return d.instanceId == id; }), devices.end());
        return e;
    }
    bool Called(const char* name) const { return std::find(calls.begin(), calls.end(), name) != calls.end(); }
};
void DryAndDuplicates() {
    Fake f; auto r = Execute({Action::Create, false}, f);
    Check(r.status == Status::DryRun && !f.Called("element") && !f.Called("write-prepared"), "create defaults to dry run");
    f = Fake{}; auto d = Own(); d.present = false; d.hardwareIds = {L"Other", L"ROOT\\TIMOXWINDOWSAUDIO"}; f.devices.push_back(d);
    r = Execute({Action::Create, true}, f);
    Check(r.status == Status::Refused && !f.Called("element"), "phantom exact case insensitive hardware duplicate refused");
    f = Fake{}; d = Own(); d.instanceId = L"ROOT\\OtherDevice\\0"; d.hardwareIds = {L"Root\\TimoxWindowsAudioExtra"}; f.devices.push_back(d);
    r = Execute({Action::Create, true}, f); Check(r.status == Status::Success && f.Called("register"), "similar prefix is not a duplicate");
    f = Fake{}; f.failAt = "inventory"; r = Execute({Action::Create, true}, f);
    Check(r.status == Status::PlatformError && r.win32Error == 1234 && !f.Called("element"), "inventory failure cannot become empty inventory");
}
void CreateTransactions() {
    Fake f; auto r = Execute({Action::Create, true}, f);
    Check(r.status == Status::Success && f.hasReceipt && f.receipt.stage == Stage::Registered && !f.held, "successful durable registered receipt");
    auto prepared = std::find(f.calls.begin(), f.calls.end(), "write-prepared");
    auto reg = std::find(f.calls.begin(), f.calls.end(), "register");
    Check(prepared != f.calls.end() && reg != f.calls.end() && prepared < reg, "intent persisted before registration");
    for (const char* step : {"lock", "machine", "read", "new-id", "element", "hardware", "write-prepared"}) {
        f = Fake{}; f.failAt = step; r = Execute({Action::Create, true}, f);
        Check(r.status == Status::PlatformError && r.win32Error == 1234 && !f.Called("register") && !f.held, "pre-registration failure preserves original error and unlocks");
    }
    f = Fake{}; f.failAt = "write-registered"; r = Execute({Action::Create, true}, f);
    Check(r.recoveryRequired && f.hasReceipt && f.receipt.stage == Stage::Prepared, "post-register storage failure retains recovery intent");
    f = Fake{}; f.failAt = "register"; f.registeredDespiteError = true; r = Execute({Action::Create, true}, f);
    Check(r.status == Status::PlatformError && r.recoveryRequired && f.Called("find"), "ambiguous registration failure probes exact instance");
    f = Fake{}; f.failAt = "register"; r = Execute({Action::Create, true}, f);
    Check(r.status == Status::PlatformError && r.recoveryRequired && f.hasReceipt && f.receipt.stage == Stage::Prepared,
        "failed registration with absent instance still requires durable intent recovery");
    f = Fake{}; f.raceDuplicate = true; r = Execute({Action::Create, true}, f);
    Check(r.status != Status::Success && f.Called("remove"), "post-registration duplicate rolls back only own instance");
    Check(f.devices.size() == 1 && f.devices[0].instanceId == L"ROOT\\other\\0", "rollback preserves actual concurrent foreign instance");
}
void RemovalTransactions() {
    Fake f; f.hasReceipt = true; f.receipt = Saved(); f.devices.push_back(Own());
    auto r = Execute({Action::Remove, false}, f);
    Check(r.status == Status::DryRun && !f.Called("remove"), "remove defaults to dry run");
    r = Execute({Action::Remove, true}, f);
    Check(r.status == Status::Success && f.devices.empty() && f.receipt.stage == Stage::Removed, "remove exact saved instance");
    r = Execute({Action::Remove, true}, f); Check(r.status == Status::Success, "already removed is idempotent");
    f = Fake{}; f.hasReceipt = true; f.receipt = Saved(Stage::Removed); f.devices.push_back(Own());
    r = Execute({Action::Remove, true}, f);
    Check(r.status == Status::Refused && !f.Called("remove") && f.receipt.stage == Stage::Removed,
        "terminal receipt cannot authorize deletion of reappeared instance");
    r = Execute({Action::Remove, false}, f);
    Check(r.status == Status::Refused && !f.Called("remove"), "terminal receipt reappearance also refused by dry run");
    for (unsigned bad = 0; bad != 5; ++bad) {
        f = Fake{}; f.hasReceipt = true; f.receipt = Saved(); f.devices.push_back(Own());
        if (bad == 0) f.receipt.machineId = L"other-vm";
        if (bad == 1) f.receipt.instanceId = L"ROOT\\Other\\0";
        if (bad == 2) f.receipt.operationId = L"not-a-guid";
        if (bad == 3) f.receipt.schema = 99;
        if (bad == 4) f.devices[0].hardwareIds = {L"Root\\Other"};
        r = Execute({Action::Remove, true}, f);
        Check(r.status == Status::Refused && !f.Called("remove"), "foreign or damaged receipt/device refuses removal");
    }
    f = Fake{}; f.hasReceipt = true; f.receipt = Saved(Stage::Prepared); f.devices.push_back(Own());
    r = Execute({Action::Remove, true}, f); Check(r.status == Status::Success && f.devices.empty(), "recover interrupted registration from durable intent");
    f = Fake{}; f.hasReceipt = true; f.receipt = Saved(); f.devices.push_back(Own()); f.reboot = true;
    r = Execute({Action::Remove, true}, f);
    Check(r.status == Status::RebootRequired && r.rebootRequired && f.receipt.stage == Stage::RemovalPending, "pending reboot is not verified disappearance");
    f = Fake{}; f.hasReceipt = true; f.receipt = Saved(); f.devices.push_back(Own()); f.failAt = "remove";
    r = Execute({Action::Remove, true}, f);
    Check(r.status == Status::PlatformError && r.recoveryRequired && f.hasReceipt, "remove failure retains receipt");
    f = Fake{}; f.hasReceipt = true; f.receipt = Saved(); f.devices.push_back(Own()); f.failAt = "write-removing";
    r = Execute({Action::Remove, true}, f);
    Check(r.status == Status::PlatformError && r.win32Error == 1234 && !f.Called("remove") && !f.held,
        "remove intent write failure prevents destructive action");
    f = Fake{}; f.hasReceipt = true; f.receipt = Saved(); f.devices.push_back(Own()); f.failAt = "write-removed";
    r = Execute({Action::Remove, true}, f);
    Check(r.status == Status::PlatformError && r.recoveryRequired && f.devices.empty() && f.receipt.stage == Stage::RemovalPending,
        "final receipt failure retains recovery after actual removal");
    f = Fake{}; f.hasReceipt = true; f.receipt = Saved(); f.devices.push_back(Own()); f.failAt = "find";
    r = Execute({Action::Remove, true}, f);
    Check(r.status == Status::PlatformError && r.win32Error == 1234 && !f.Called("remove"), "find failure is not absence");
}
}
int main() {
    DryAndDuplicates(); CreateTransactions(); RemovalTransactions();
    std::printf("transaction failures=%u (fake platform only)\n", failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
