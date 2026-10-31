#pragma once

#include "application_profile_store.h"
#include "wasapi_bridge_configuration.h"
#include <nlohmann/json.hpp>
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <set>

// Catalogue entries are reusable intentions, never process-instance identities.
class ProcessCopyTemplateStore {
public:
    using Json = nlohmann::json;
    explicit ProcessCopyTemplateStore(std::wstring path = {})
        : path_(path.empty() ? DefaultPath() : std::move(path)) {}

    static std::wstring DefaultPath() {
        const auto profiles = ApplicationProfileStore::DefaultPath();
        return profiles.empty() ? std::wstring{} :
            (std::filesystem::path(profiles).parent_path() / L"process-copy-templates.json").wstring();
    }

    static bool Normalize(Json& document, std::string& error) {
        error = "Expected templates with only id, name, processName and bufferDuration100ns";
        if (!document.is_object() || document.size() != 1 || !document.contains("templates") ||
            !document["templates"].is_array() || document["templates"].size() > 1024) return false;
        std::set<std::string> ids;
        for (auto& item : document["templates"]) {
            if (!item.is_object() || item.size() != 4 || !item.contains("id") || !item["id"].is_string() ||
                !item.contains("name") || !item["name"].is_string() || !item.contains("processName") ||
                !item["processName"].is_string() || !item.contains("bufferDuration100ns") ||
                !item["bufferDuration100ns"].is_number_integer()) return false;
            const auto id = item["id"].get<std::string>();
            if (id.empty() || id.size() > 128 || !std::all_of(id.begin(), id.end(), [](unsigned char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-';
            }) || !ids.insert(id).second) return false;
            std::wstring name, process;
            if (!WasapiEndpointFromUtf8(item["name"].get<std::string>(), name) || name.size() > 128 ||
                std::any_of(name.begin(), name.end(), [](wchar_t c) { return c < 32; }) ||
                !WasapiEndpointFromUtf8(item["processName"].get<std::string>(), process)) return false;
            std::vector<ApplicationProfile> profile{{process, 1, 1}};
            if (!ApplicationProfileStore::NormalizeAndValidate(profile, error)) return false;
            error = "Invalid process-copy template buffer duration";
            const auto& duration = item["bufferDuration100ns"];
            if ((duration.is_number_unsigned() && duration.get<std::uint64_t>() > 10000000) ||
                duration.get<std::int64_t>() < 0 || duration.get<std::int64_t>() > 10000000) return false;
            std::string normalized;
            if (!WasapiEndpointToUtf8(profile.front().processName, normalized)) return false;
            item["processName"] = normalized;
        }
        error.clear();
        return true;
    }

    bool Load(Json& document, std::string& error) {
        std::lock_guard<std::mutex> lock(mutex_);
        error.clear();
        if (path_.empty()) { error = "The engine user profile is unavailable"; return false; }
        const HANDLE file = CreateFileW(path_.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND) {
                document = Json{{"templates", Json::array()}}; return true;
            }
            error = "Cannot read process-copy catalogue"; return false;
        }
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > MaximumBytes) {
            CloseHandle(file); error = "Process-copy catalogue is unreadable or too large"; return false;
        }
        std::string content(static_cast<size_t>(size.QuadPart), '\0'); DWORD read = 0;
        const bool complete = ReadFile(file, content.data(), static_cast<DWORD>(content.size()), &read, nullptr) && read == content.size();
        CloseHandle(file);
        if (!complete) { error = "Cannot read complete process-copy catalogue"; return false; }
        auto parsed = Json::parse(content, nullptr, false);
        if (!Normalize(parsed, error)) return false;
        document = std::move(parsed); return true;
    }

    bool Save(Json document, std::string& error) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!Normalize(document, error)) return false;
        if (path_.empty()) { error = "The engine user profile is unavailable"; return false; }
        const auto content = document.dump();
        if (content.size() > MaximumBytes) { error = "Process-copy catalogue is too large"; return false; }
        std::error_code filesystemError;
        const auto parent = std::filesystem::path(path_).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent, filesystemError);
        if (filesystemError) { error = "Cannot create process-copy catalogue directory"; return false; }
        static std::atomic<unsigned> sequence{0};
        const auto temporary = path_ + L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++sequence);
        const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) { error = "Cannot create process-copy catalogue"; return false; }
        DWORD written = 0;
        const bool complete = WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr) &&
            written == content.size() && FlushFileBuffers(file);
        CloseHandle(file);
        if (!complete) { DeleteFileW(temporary.c_str()); error = "Cannot write complete process-copy catalogue"; return false; }
        if (!MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temporary.c_str()); error = "Cannot replace process-copy catalogue"; return false;
        }
        return true;
    }
private:
    static constexpr DWORD MaximumBytes = 1024 * 1024;
    std::wstring path_;
    std::mutex mutex_;
};
