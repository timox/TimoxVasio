#pragma once

#include <windows.h>
#include <objbase.h>

#include <string>
#include <utility>
#include <vector>

inline std::wstring registeredAsioDllPath(const CLSID& clsid) {
    wchar_t guid[40]{};
    if (!StringFromGUID2(clsid, guid, static_cast<int>(sizeof(guid) / sizeof(guid[0])))) return {};
    const std::wstring keyPath = std::wstring(L"CLSID\\") + guid + L"\\InprocServer32";
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, keyPath.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
        return {};
    DWORD type = 0, bytes = 0;
    const auto sizeStatus = RegQueryValueExW(key, nullptr, nullptr, &type, nullptr, &bytes);
    if (sizeStatus != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) ||
        bytes < sizeof(wchar_t) || bytes > 32768) {
        RegCloseKey(key);
        return {};
    }
    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
    const auto valueStatus = RegQueryValueExW(key, nullptr, nullptr, &type,
        reinterpret_cast<BYTE*>(buffer.data()), &bytes);
    RegCloseKey(key);
    if (valueStatus != ERROR_SUCCESS) return {};
    std::wstring path(buffer.data());
    if (type == REG_EXPAND_SZ) {
        const DWORD needed = ExpandEnvironmentStringsW(path.c_str(), nullptr, 0);
        if (!needed || needed > 32768) return {};
        std::wstring expanded(needed, L'\0');
        if (ExpandEnvironmentStringsW(path.c_str(), expanded.data(), needed) != needed)
            return {};
        expanded.resize(needed - 1);
        path = std::move(expanded);
    }
    return path;
}

inline std::string utf8FromWide(const std::wstring& value) {
    if (value.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string result(static_cast<std::size_t>(needed), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), needed, nullptr, nullptr) != needed) return {};
    return result;
}
