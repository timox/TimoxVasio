#include <windows.h>
#include <initguid.h>
#include <usbiodef.h>
#include <usbioctl.h>
#include <setupapi.h>

#include "nlohmann/json.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
using Json = nlohmann::json;

struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}

bool ioctl(HANDLE handle, DWORD control, void* buffer, DWORD bytes, DWORD& returned) {
    return DeviceIoControl(handle, control, buffer, bytes, buffer, bytes, &returned, nullptr) != FALSE;
}

const char* syncName(unsigned value) {
    switch (value) {
    case 0: return "none";
    case 1: return "asynchronous";
    case 2: return "adaptive";
    case 3: return "synchronous";
    default: return "unknown";
    }
}

const char* usageName(unsigned value) {
    switch (value) {
    case 0: return "data";
    case 1: return "explicit-feedback";
    case 2: return "implicit-feedback-data";
    case 3: return "reserved";
    default: return "unknown";
    }
}

Json parseConfiguration(const unsigned char* bytes, std::size_t length) {
    Json result = {{"audioIsochronousEndpoints", Json::array()},
        {"explicitFeedbackPresent", false}, {"implicitFeedbackPresent", false}};
    if (length < 9 || bytes[1] != USB_CONFIGURATION_DESCRIPTOR_TYPE) {
        result["error"] = "invalid configuration descriptor";
        return result;
    }
    result["configurationValue"] = bytes[5];
    result["totalLength"] = static_cast<unsigned>(bytes[2] | (bytes[3] << 8));
    bool audioStreaming = false;
    unsigned interfaceNumber = 0, alternateSetting = 0;
    for (std::size_t offset = 0; offset + 2 <= length;) {
        const unsigned descriptorLength = bytes[offset];
        if (descriptorLength < 2 || offset + descriptorLength > length) {
            result["parseError"] = "truncated or zero-length descriptor";
            break;
        }
        const unsigned type = bytes[offset + 1];
        if (type == USB_INTERFACE_DESCRIPTOR_TYPE && descriptorLength >= 9) {
            interfaceNumber = bytes[offset + 2];
            alternateSetting = bytes[offset + 3];
            audioStreaming = bytes[offset + 5] == 1 && bytes[offset + 6] == 2;
        } else if (type == USB_ENDPOINT_DESCRIPTOR_TYPE && descriptorLength >= 7 &&
            audioStreaming && (bytes[offset + 3] & 0x03) == 1) {
            const unsigned attributes = bytes[offset + 3];
            const unsigned usage = (attributes >> 4) & 0x03;
            const unsigned address = bytes[offset + 2];
            result["audioIsochronousEndpoints"].push_back({
                {"interface", interfaceNumber}, {"alternateSetting", alternateSetting},
                {"address", address}, {"direction", (address & 0x80) ? "device-to-host" : "host-to-device"},
                {"synchronization", syncName((attributes >> 2) & 0x03)},
                {"usage", usageName(usage)},
                {"maxPacketSize", static_cast<unsigned>(bytes[offset + 4] | (bytes[offset + 5] << 8))}
            });
            if (usage == 1) result["explicitFeedbackPresent"] = true;
            if (usage == 2) result["implicitFeedbackPresent"] = true;
        }
        offset += descriptorLength;
    }
    return result;
}

Json readConfiguration(HANDLE hub, ULONG port, unsigned index) {
    constexpr std::size_t prefix = offsetof(USB_DESCRIPTOR_REQUEST, Data);
    auto request = [&](std::size_t descriptorBytes, std::vector<unsigned char>& buffer,
                       DWORD& returned) {
        buffer.assign(prefix + descriptorBytes, 0);
        auto* header = reinterpret_cast<USB_DESCRIPTOR_REQUEST*>(buffer.data());
        header->ConnectionIndex = port;
        header->SetupPacket.wValue = static_cast<USHORT>(
            (USB_CONFIGURATION_DESCRIPTOR_TYPE << 8) | (index & 0xff));
        header->SetupPacket.wLength = static_cast<USHORT>(descriptorBytes);
        return ioctl(hub, IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,
            buffer.data(), static_cast<DWORD>(buffer.size()), returned);
    };
    std::vector<unsigned char> buffer;
    DWORD returned = 0;
    if (!request(9, buffer, returned) || returned < prefix + 9) {
        return {{"index", index}, {"error", "configuration header request failed"},
            {"win32Error", GetLastError()}};
    }
    const auto length = static_cast<std::size_t>(buffer[prefix + 2] | (buffer[prefix + 3] << 8));
    if (length < 9 || length > 65535) return {{"index", index}, {"error", "invalid total length"}};
    if (!request(length, buffer, returned) || returned < prefix + 9) {
        return {{"index", index}, {"error", "full configuration request failed"},
            {"win32Error", GetLastError()}};
    }
    auto result = parseConfiguration(buffer.data() + prefix,
        (std::min)(length, static_cast<std::size_t>(returned - prefix)));
    result["index"] = index;
    return result;
}

Json scan(unsigned vid, unsigned pid) {
    Json report = {{"schemaVersion", 1}, {"vendorId", vid}, {"productId", pid},
        {"hubsEnumerated", 0}, {"hubsOpened", 0}, {"hubErrors", Json::array()},
        {"devices", Json::array()}};
    HDEVINFO set = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_USB_HUB, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) {
        report["error"] = "USB hub enumeration failed";
        report["win32Error"] = GetLastError();
        return report;
    }
    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA interfaceData{};
        interfaceData.cbSize = sizeof(interfaceData);
        if (!SetupDiEnumDeviceInterfaces(set, nullptr, &GUID_DEVINTERFACE_USB_HUB,
            index, &interfaceData)) break;
        report["hubsEnumerated"] = report["hubsEnumerated"].get<unsigned>() + 1;
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &interfaceData, nullptr, 0, &needed, nullptr);
        if (needed < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) continue;
        std::vector<unsigned char> detailBytes(needed);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBytes.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &interfaceData, detail, needed,
            nullptr, nullptr)) continue;
        const std::wstring path(detail->DevicePath);
        Handle hub{CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr)};
        if (hub.value == INVALID_HANDLE_VALUE) {
            report["hubErrors"].push_back({{"hub", utf8(path)}, {"stage", "open"},
                {"win32Error", GetLastError()}});
            continue;
        }
        report["hubsOpened"] = report["hubsOpened"].get<unsigned>() + 1;
        USB_NODE_INFORMATION node{};
        node.NodeType = UsbHub;
        DWORD returned = 0;
        if (!ioctl(hub.value, IOCTL_USB_GET_NODE_INFORMATION, &node,
            sizeof(node), returned)) {
            report["hubErrors"].push_back({{"hub", utf8(path)}, {"stage", "ports"},
                {"win32Error", GetLastError()}});
            continue;
        }
        const unsigned portCount = node.u.HubInformation.HubDescriptor.bNumberOfPorts;
        for (unsigned port = 1; port <= portCount; ++port) {
            std::vector<unsigned char> connectionBytes(
                sizeof(USB_NODE_CONNECTION_INFORMATION_EX) + 64 * sizeof(USB_PIPE_INFO), 0);
            auto* connection = reinterpret_cast<USB_NODE_CONNECTION_INFORMATION_EX*>(
                connectionBytes.data());
            connection->ConnectionIndex = port;
            if (!ioctl(hub.value, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,
                connection, static_cast<DWORD>(connectionBytes.size()), returned)) continue;
            const auto& device = connection->DeviceDescriptor;
            if (device.idVendor != vid || device.idProduct != pid) continue;
            Json entry = {{"hub", utf8(path)}, {"port", port},
                {"vendorId", device.idVendor}, {"productId", device.idProduct},
                {"usbVersionBcd", device.bcdUSB}, {"deviceVersionBcd", device.bcdDevice},
                {"speedCode", connection->Speed},
                {"currentConfigurationValue", connection->CurrentConfigurationValue},
                {"configurations", Json::array()}};
            for (unsigned configuration = 0; configuration < device.bNumConfigurations; ++configuration)
                entry["configurations"].push_back(readConfiguration(hub.value, port, configuration));
            report["devices"].push_back(std::move(entry));
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    report["available"] = !report["devices"].empty();
    return report;
}
}

int wmain(int argc, wchar_t** argv) {
    unsigned vid = 0, pid = 0;
    std::wstring output;
    for (int index = 1; index < argc; ++index) {
        const std::wstring arg(argv[index]);
        if ((arg == L"--vid" || arg == L"--pid" || arg == L"--output") && index + 1 < argc) {
            const std::wstring value(argv[++index]);
            if (arg == L"--vid") vid = static_cast<unsigned>(std::wcstoul(value.c_str(), nullptr, 16));
            else if (arg == L"--pid") pid = static_cast<unsigned>(std::wcstoul(value.c_str(), nullptr, 16));
            else output = value;
        } else {
            std::cerr << "usage: vasio-usb-descriptors --vid HEX --pid HEX [--output FILE]\n";
            return 2;
        }
    }
    if (!vid || !pid || vid > 0xffff || pid > 0xffff) return 2;
    const auto report = scan(vid, pid);
    const auto payload = report.dump(2);
    if (output.empty()) std::cout << payload << '\n';
    else {
        std::ofstream file(output, std::ios::binary | std::ios::trunc);
        if (!file) return 3;
        file << payload << '\n';
    }
    return report.value("available", false) ? 0 : 1;
}
