#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
#include <aclapi.h>
#include <sddl.h>
#include <shlobj.h>
#include <objbase.h>
#include "device_win32.h"
#include "receipt_codec.h"
#include "receipt_file_policy.h"
#include <array>
#include <utility>

namespace timox::lab {
namespace {
const GUID MediaGuid = {0x4d36e96c, 0xe325, 0x11ce, {0xbf,0xc1,0x08,0x00,0x2b,0xe1,0x03,0x18}};
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(Handle&& other) noexcept : value(std::exchange(other.value, INVALID_HANDLE_VALUE)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); value = std::exchange(other.value, INVALID_HANDLE_VALUE); } return *this;
    }
    Handle(const Handle&) = delete;
};
struct DevSet {
    HDEVINFO value = INVALID_HANDLE_VALUE;
    ~DevSet() { if (value != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(value); }
};
struct LocalMemory { HLOCAL value = nullptr; ~LocalMemory() { if (value) LocalFree(value); } };
Principal Identify(PSID sid) {
    if (sid && IsValidSid(sid)) {
        if (IsWellKnownSid(sid, WinLocalSystemSid)) return Principal::System;
        if (IsWellKnownSid(sid, WinBuiltinAdministratorsSid)) return Principal::Administrators;
    }
    return Principal::Other;
}
DWORD SafeFile(HANDLE file, bool directory, bool privateObject) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file, &info)) return GetLastError();
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory) return ERROR_ACCESS_DENIED;
    if (!privateObject) return ERROR_SUCCESS;
    PSID owner = nullptr; PACL acl = nullptr; PSECURITY_DESCRIPTOR descriptor = nullptr;
    const auto error = GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        &owner, nullptr, &acl, nullptr, &descriptor);
    LocalMemory memory{descriptor}; if (error) return error;
    SECURITY_DESCRIPTOR_CONTROL control = 0; DWORD revision = 0;
    if (!GetSecurityDescriptorControl(descriptor, &control, &revision)) return GetLastError();
    FileObservation observation; observation.directory = directory; observation.links = info.nNumberOfLinks;
    observation.owner = Identify(owner); observation.protectedDacl = (control & SE_DACL_PROTECTED) != 0;
    observation.nullDacl = acl == nullptr;
    if (acl) {
        if (!IsValidAcl(acl) || acl->AceCount != 2) return ERROR_ACCESS_DENIED;
        for (DWORD i = 0; i < acl->AceCount; ++i) {
            void* entry = nullptr; if (!GetAce(acl, i, &entry)) return GetLastError();
            const auto* header = static_cast<ACE_HEADER*>(entry);
            constexpr std::size_t sidOffset = offsetof(ACCESS_ALLOWED_ACE, SidStart);
            if (header->AceType != ACCESS_ALLOWED_ACE_TYPE || header->AceSize < sidOffset + 8) return ERROR_ACCESS_DENIED;
            const auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(entry);
            const auto* sidBytes = reinterpret_cast<const BYTE*>(&ace->SidStart);
            if (sidBytes[1] > SID_MAX_SUB_AUTHORITIES || sidOffset + 8 + 4 * static_cast<std::size_t>(sidBytes[1]) > header->AceSize)
                return ERROR_ACCESS_DENIED;
            // Inherit-only ACEs do not grant this object access; reject them too.
            if (header->AceFlags & (INHERITED_ACE | INHERIT_ONLY_ACE)) return ERROR_ACCESS_DENIED;
            observation.entries.push_back({Identify(const_cast<DWORD*>(&ace->SidStart)), true, false, ace->Mask});
        }
    }
    return SafeReceiptObject(observation, directory) ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
}
DWORD MakeSecurity(LocalMemory& descriptor, SECURITY_ATTRIBUTES& attributes) {
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)", SDDL_REVISION_1, &sd, nullptr)) return GetLastError();
    descriptor.value = sd; attributes = {sizeof(attributes), sd, FALSE}; return 0;
}
DWORD Elevated() {
    BYTE sid[SECURITY_MAX_SID_SIZE]{}; DWORD size = sizeof(sid); BOOL member = FALSE;
    if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, sid, &size)) return GetLastError();
    if (!CheckTokenMembership(nullptr, sid, &member)) return GetLastError();
    return member ? ERROR_SUCCESS : ERROR_ELEVATION_REQUIRED;
}
DWORD Describe(HDEVINFO set, SP_DEVINFO_DATA& data, Device& result) {
    std::array<wchar_t, 512> instance{}; DWORD required = 0;
    if (!SetupDiGetDeviceInstanceIdW(set, &data, instance.data(), static_cast<DWORD>(instance.size()), &required)) return GetLastError();
    wchar_t classText[40]{};
    if (!StringFromGUID2(data.ClassGuid, classText, 40)) return ERROR_INVALID_DATA;
    Device device; device.instanceId = instance.data(); device.classId = classText;
    // Devices without hardware IDs are unrelated, not a malformed matching device.
    DWORD type = 0; required = 0;
    if (!SetupDiGetDeviceRegistryPropertyW(set, &data, SPDRP_HARDWAREID, &type, nullptr, 0, &required)) {
        auto error = GetLastError();
        if (error == ERROR_INVALID_DATA) { result = std::move(device); return 0; }
        if (error != ERROR_INSUFFICIENT_BUFFER) return error;
    }
    if (required < 2 * sizeof(wchar_t) || required > 65536 || required % sizeof(wchar_t)) return ERROR_INVALID_DATA;
    std::vector<wchar_t> ids(required / sizeof(wchar_t)); DWORD actual = 0;
    if (!SetupDiGetDeviceRegistryPropertyW(set, &data, SPDRP_HARDWAREID, &type, reinterpret_cast<PBYTE>(ids.data()), required, &actual)) return GetLastError();
    if (type != REG_MULTI_SZ || actual != required || ids[ids.size()-1] || ids[ids.size()-2]) return ERROR_INVALID_DATA;
    std::size_t offset = 0;
    while (offset < ids.size()-1 && ids[offset]) {
        std::size_t end = offset; while (end < ids.size() && ids[end]) ++end;
        if (end == ids.size()) return ERROR_INVALID_DATA;
        device.hardwareIds.emplace_back(ids.data()+offset, end-offset); offset = end+1;
    }
    for (; offset < ids.size(); ++offset) if (ids[offset]) return ERROR_INVALID_DATA;
    result = std::move(device); return 0;
}
}
struct WindowsDevicePlatform::State {
    explicit State(bool enabled) : apply(enabled) {}
    bool apply, missing = false;
    std::wstring directory;
    std::vector<Handle> ancestors;
    Handle lock;
    HDEVINFO elementSet = INVALID_HANDLE_VALUE;
    SP_DEVINFO_DATA element{sizeof(SP_DEVINFO_DATA)};
};
WindowsDevicePlatform::WindowsDevicePlatform(bool apply) : state_(std::make_unique<State>(apply)) {}
WindowsDevicePlatform::~WindowsDevicePlatform() { ReleaseElement(); Unlock(); }
std::uint32_t WindowsDevicePlatform::Lock() {
    auto& s = *state_;
    if (s.apply) { auto e = Elevated(); if (e) return e; }
    PWSTR known = nullptr; auto hr = SHGetKnownFolderPath(FOLDERID_ProgramData, KF_FLAG_DONT_VERIFY, nullptr, &known);
    if (FAILED(hr)) return static_cast<DWORD>(hr);
    std::wstring path(known); CoTaskMemFree(known);
    if (path.size() < 3 || path[1] != L':' || path[2] != L'\\' || path.find(L"..") != std::wstring::npos) return ERROR_BAD_PATHNAME;
    s.directory = path + L"\\TimoxVasio\\NativeAudioLab";
    // Lock every ancestor against rename/removal before descending. Never follow a reparse point.
    Handle volumeRoot(CreateFileW(path.substr(0, 3).c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (volumeRoot.value == INVALID_HANDLE_VALUE) return GetLastError();
    auto rootError = SafeFile(volumeRoot.value, true, false); if (rootError) return rootError;
    s.ancestors.push_back(std::move(volumeRoot));
    for (std::size_t pos = 3;;) {
        const auto end = s.directory.find(L'\\', pos);
        const auto current = end == std::wstring::npos ? s.directory : s.directory.substr(0, end);
        const bool privateDirectory = current.size() > path.size();
        LocalMemory descriptor; SECURITY_ATTRIBUTES attributes{};
        auto e = MakeSecurity(descriptor, attributes); if (e) return e;
        Handle h(CreateFileW(current.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (h.value == INVALID_HANDLE_VALUE) {
            e = GetLastError();
            if ((e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND) || !privateDirectory) return e;
            if (!s.apply) { s.missing = true; return 0; }
            if (!CreateDirectoryW(current.c_str(), &attributes)) return GetLastError();
            h = Handle(CreateFileW(current.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            if (h.value == INVALID_HANDLE_VALUE) return GetLastError();
        }
        e = SafeFile(h.value, true, privateDirectory); if (e) return e;
        s.ancestors.push_back(std::move(h));
        if (end == std::wstring::npos) break;
        pos = end+1;
    }
    LocalMemory descriptor; SECURITY_ATTRIBUTES attributes{}; auto e = MakeSecurity(descriptor, attributes); if (e) return e;
    const auto lockPath = s.directory + L"\\transaction.lock";
    s.lock = Handle(CreateFileW(lockPath.c_str(), GENERIC_READ | (s.apply ? GENERIC_WRITE : 0) | READ_CONTROL, 0,
        &attributes, s.apply ? OPEN_ALWAYS : OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (s.lock.value == INVALID_HANDLE_VALUE) {
        e = GetLastError(); return !s.apply && e == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : e;
    }
    return SafeFile(s.lock.value, false, true);
}
void WindowsDevicePlatform::Unlock() noexcept { state_->lock = Handle{}; state_->ancestors.clear(); }
std::uint32_t WindowsDevicePlatform::MachineIdentity(std::wstring& result) {
    HKEY key = nullptr; auto error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key);
    if (error) return static_cast<DWORD>(error);
    wchar_t value[64]{}; DWORD bytes = sizeof(value), type = 0;
    error = RegGetValueW(key, nullptr, L"MachineGuid", RRF_RT_REG_SZ, &type, value, &bytes); RegCloseKey(key);
    if (error) return static_cast<DWORD>(error);
    if (bytes != 37 * sizeof(wchar_t) || value[36] != 0) return ERROR_INVALID_DATA;
    result.assign(value, 36); return 0;
}
std::uint32_t WindowsDevicePlatform::Inventory(std::vector<Device>& result) {
    result.clear(); DevSet set; set.value = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES);
    if (set.value == INVALID_HANDLE_VALUE) return GetLastError();
    for (DWORD i = 0; i < 65536; ++i) {
        SP_DEVINFO_DATA data{sizeof(data)};
        if (!SetupDiEnumDeviceInfo(set.value, i, &data)) { const auto e = GetLastError(); return e == ERROR_NO_MORE_ITEMS ? ERROR_SUCCESS : e; }
        Device device; auto e = Describe(set.value, data, device); if (e) return e;
        result.push_back(std::move(device));
    }
    return ERROR_MORE_DATA;
}
std::uint32_t WindowsDevicePlatform::ReadReceipt(Receipt& receipt, bool& found) {
    found = false; if (state_->missing) return 0;
    Handle file(CreateFileW((state_->directory+L"\\receipt.bin").c_str(), GENERIC_READ | READ_CONTROL, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (file.value == INVALID_HANDLE_VALUE) { const auto e = GetLastError(); return e == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : e; }
    auto error = SafeFile(file.value, false, true); if (error) return error;
    LARGE_INTEGER size{}; if (!GetFileSizeEx(file.value, &size)) return GetLastError();
    if (size.QuadPart != static_cast<LONGLONG>(ReceiptWireSize)) return ERROR_INVALID_DATA;
    std::array<std::uint8_t, ReceiptWireSize> bytes{}; DWORD actual = 0;
    if (!ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &actual, nullptr)) return GetLastError();
    if (actual != bytes.size() || !DecodeReceipt(bytes.data(), bytes.size(), receipt)) return ERROR_INVALID_DATA;
    found = true; return 0;
}
std::uint32_t WindowsDevicePlatform::WriteReceiptDurably(const Receipt& receipt) {
    auto& s = *state_; if (!s.apply || s.lock.value == INVALID_HANDLE_VALUE) return ERROR_ACCESS_DENIED;
    std::vector<std::uint8_t> bytes; if (!EncodeReceipt(receipt, bytes)) return ERROR_INVALID_DATA;
    // Validate existing destination before replacement, including hardlink count.
    Receipt previous; bool found = false; auto error = ReadReceipt(previous, found); if (error) return error;
    std::wstring nonce; error = NewOperationId(nonce); if (error) return error;
    const auto temp = s.directory + L"\\receipt-" + nonce + L".tmp";
    LocalMemory descriptor; SECURITY_ATTRIBUTES attributes{}; error = MakeSecurity(descriptor, attributes); if (error) return error;
    Handle file(CreateFileW(temp.c_str(), GENERIC_WRITE | READ_CONTROL, 0, &attributes, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_WRITE_THROUGH, nullptr));
    if (file.value == INVALID_HANDLE_VALUE) return GetLastError();
    error = SafeFile(file.value, false, true); DWORD actual = 0;
    if (!error && !WriteFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &actual, nullptr)) error = GetLastError();
    if (!error && actual != bytes.size()) error = ERROR_WRITE_FAULT;
    if (!error && !FlushFileBuffers(file.value)) error = GetLastError();
    file = Handle{};
    if (!error && !MoveFileExW(temp.c_str(), (s.directory+L"\\receipt.bin").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) error = GetLastError();
    if (error) DeleteFileW(temp.c_str()); // Exact file created by this operation; never wildcard cleanup.
    return error;
}
std::uint32_t WindowsDevicePlatform::NewOperationId(std::wstring& result) {
    GUID id{}; auto hr = CoCreateGuid(&id); if (FAILED(hr)) return static_cast<DWORD>(hr);
    wchar_t text[40]{}; if (!StringFromGUID2(id, text, 40)) return ERROR_INVALID_DATA;
    result.assign(text+1, 36); return 0;
}
std::uint32_t WindowsDevicePlatform::CreateElement(const std::wstring& instance) {
    if (!state_->apply || state_->lock.value == INVALID_HANDLE_VALUE) return ERROR_ACCESS_DENIED;
    ReleaseElement(); state_->elementSet = SetupDiCreateDeviceInfoList(&MediaGuid, nullptr);
    if (state_->elementSet == INVALID_HANDLE_VALUE) return GetLastError();
    if (!SetupDiCreateDeviceInfoW(state_->elementSet, instance.c_str(), &MediaGuid, L"Timox Windows Audio laboratory", nullptr, 0, &state_->element)) {
        const auto e = GetLastError(); ReleaseElement(); return e;
    }
    std::array<wchar_t, 512> actual{};
    if (!SetupDiGetDeviceInstanceIdW(state_->elementSet, &state_->element, actual.data(), static_cast<DWORD>(actual.size()), nullptr)) {
        const auto e = GetLastError(); ReleaseElement(); return e;
    }
    if (_wcsicmp(actual.data(), instance.c_str())) { ReleaseElement(); return ERROR_INVALID_DATA; }
    return 0;
}
std::uint32_t WindowsDevicePlatform::SetHardwareId() {
    if (!state_->apply || state_->elementSet == INVALID_HANDLE_VALUE) return ERROR_ACCESS_DENIED;
    constexpr wchar_t ids[] = L"Root\\TimoxWindowsAudio\0";
    return SetupDiSetDeviceRegistryPropertyW(state_->elementSet, &state_->element, SPDRP_HARDWAREID,
        reinterpret_cast<const BYTE*>(ids), sizeof(ids)) ? ERROR_SUCCESS : GetLastError();
}
std::uint32_t WindowsDevicePlatform::RegisterElement() {
    if (!state_->apply || state_->elementSet == INVALID_HANDLE_VALUE) return ERROR_ACCESS_DENIED;
    return SetupDiCallClassInstaller(DIF_REGISTERDEVICE, state_->elementSet, &state_->element) ? ERROR_SUCCESS : GetLastError();
}
void WindowsDevicePlatform::ReleaseElement() noexcept {
    if (state_->elementSet != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(state_->elementSet);
    state_->elementSet = INVALID_HANDLE_VALUE; state_->element = {}; state_->element.cbSize = sizeof(state_->element);
}
std::uint32_t WindowsDevicePlatform::FindInstance(const std::wstring& id, Device& result, bool& found) {
    found = false; DevSet set; set.value = SetupDiCreateDeviceInfoList(nullptr, nullptr);
    if (set.value == INVALID_HANDLE_VALUE) return GetLastError();
    SP_DEVINFO_DATA data{sizeof(data)};
    if (!SetupDiOpenDeviceInfoW(set.value, id.c_str(), nullptr, 0, &data)) {
        const auto e = GetLastError(); return e == ERROR_NO_SUCH_DEVINST ? ERROR_SUCCESS : e;
    }
    const auto error = Describe(set.value, data, result); if (!error) found = true; return error;
}
std::uint32_t WindowsDevicePlatform::RemoveInstance(const std::wstring& id, bool& reboot) {
    reboot = false; if (!state_->apply || state_->lock.value == INVALID_HANDLE_VALUE) return ERROR_ACCESS_DENIED;
    DevSet set; set.value = SetupDiCreateDeviceInfoList(nullptr, nullptr);
    if (set.value == INVALID_HANDLE_VALUE) return GetLastError();
    SP_DEVINFO_DATA data{sizeof(data)};
    if (!SetupDiOpenDeviceInfoW(set.value, id.c_str(), nullptr, 0, &data)) return GetLastError();
    // Recheck identity on the exact SP_DEVINFO_DATA used for DIF_REMOVE.
    Device device; auto error = Describe(set.value, data, device); if (error) return error;
    Receipt receipt; bool found = false; error = ReadReceipt(receipt, found); if (error) return error;
    if (!found || receipt.stage != Stage::RemovalPending || _wcsicmp(receipt.instanceId.c_str(), id.c_str()) ||
        _wcsicmp(device.classId.c_str(), MediaClass)) return ERROR_ACCESS_DENIED;
    bool hardware = false; for (const auto& hw : device.hardwareIds) if (!_wcsicmp(hw.c_str(), HardwareId)) hardware = true;
    if (!hardware) return ERROR_ACCESS_DENIED;
    SP_REMOVEDEVICE_PARAMS remove{}; remove.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    remove.ClassInstallHeader.InstallFunction = DIF_REMOVE; remove.Scope = DI_REMOVEDEVICE_GLOBAL;
    if (!SetupDiSetClassInstallParamsW(set.value, &data, &remove.ClassInstallHeader, sizeof(remove))) return GetLastError();
    if (!SetupDiCallClassInstaller(DIF_REMOVE, set.value, &data)) return GetLastError();
    SP_DEVINSTALL_PARAMS_W params{}; params.cbSize = sizeof(params);
    if (!SetupDiGetDeviceInstallParamsW(set.value, &data, &params)) return GetLastError();
    reboot = (params.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART)) != 0; return 0;
}
}
