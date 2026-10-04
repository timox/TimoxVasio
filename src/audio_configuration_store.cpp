// SPDX-License-Identifier: GPL-3.0-only
#include "audio_configuration_store.h"

#include "audio_controller.h"

#include <windows.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace {
using Json = nlohmann::json;
constexpr std::uint32_t kStoreVersion = 1;
constexpr DWORD kMaximumConfigurationBytes = 1024 * 1024;

bool readOptionalUint32(const Json& value, std::optional<std::uint32_t>& result) {
    if (value.is_null()) {
        result.reset();
        return true;
    }
    if (!value.is_number_unsigned() && !value.is_number_integer()) return false;
    const auto number = value.get<std::int64_t>();
    if (number <= 0 || static_cast<std::uint64_t>(number) >
            (std::numeric_limits<std::uint32_t>::max)()) return false;
    result = static_cast<std::uint32_t>(number);
    return true;
}

bool isDirectory(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool ensureParentDirectory(const std::wstring& path, std::string& error) {
    const auto separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos) return true;
    const auto directory = path.substr(0, separator);
    if (CreateDirectoryW(directory.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (isDirectory(directory)) return true;
    }
    error = "Unable to create the TimoxVasio configuration directory";
    return false;
}

Json encodeConfiguration(const AudioControllerConfiguration& configuration) {
    Json routes = Json::array();
    for (const auto& route : configuration.routes) {
        routes.push_back({
            {"id", route.id},
            {"sourceEndpointId", route.sourceEndpointId},
            {"destinationEndpointId", route.destinationEndpointId},
            {"gainDb", route.gainDb},
            {"mute", route.mute}
        });
    }
    return Json{
        {"version", kStoreVersion},
        {"physicalDriverId", configuration.physicalDriverId
            ? Json(*configuration.physicalDriverId) : Json(nullptr)},
        {"sampleRate", configuration.sampleRate ? Json(*configuration.sampleRate) : Json(nullptr)},
        {"bufferFrames", configuration.bufferFrames ? Json(*configuration.bufferFrames) : Json(nullptr)},
        {"routes", std::move(routes)}
    };
}

bool decodeConfiguration(const Json& value, AudioControllerConfiguration& configuration) {
    if (!value.is_object() || value.size() != 5 ||
        !value.contains("version") || !value["version"].is_number_unsigned() ||
        value["version"].get<std::uint32_t>() != kStoreVersion ||
        !value.contains("physicalDriverId") ||
        !(value["physicalDriverId"].is_null() || value["physicalDriverId"].is_string()) ||
        !value.contains("sampleRate") || !value.contains("bufferFrames") ||
        !value.contains("routes") || !value["routes"].is_array()) return false;

    AudioControllerConfiguration decoded;
    if (value["physicalDriverId"].is_string()) {
        decoded.physicalDriverId = value["physicalDriverId"].get<std::string>();
        if (decoded.physicalDriverId->empty()) return false;
    }
    if (!readOptionalUint32(value["sampleRate"], decoded.sampleRate) ||
        !readOptionalUint32(value["bufferFrames"], decoded.bufferFrames)) return false;

    decoded.routes.reserve(value["routes"].size());
    for (const auto& item : value["routes"]) {
        if (!item.is_object() || item.size() != 5 || !item.contains("id") ||
            !item["id"].is_string() || !item.contains("sourceEndpointId") ||
            !item["sourceEndpointId"].is_string() || !item.contains("destinationEndpointId") ||
            !item["destinationEndpointId"].is_string() || !item.contains("gainDb") ||
            !item["gainDb"].is_number() || !item.contains("mute") || !item["mute"].is_boolean())
            return false;
        const double gainDb = item["gainDb"].get<double>();
        if (!std::isfinite(gainDb)) return false;
        decoded.routes.push_back({item["id"].get<std::string>(),
            item["sourceEndpointId"].get<std::string>(),
            item["destinationEndpointId"].get<std::string>(), gainDb,
            item["mute"].get<bool>()});
    }
    configuration = std::move(decoded);
    return true;
}
}

AudioConfigurationStore::AudioConfigurationStore(std::wstring path) : path_(std::move(path)) {}

std::wstring AudioConfigurationStore::DefaultPath() {
    const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (!required) return {};
    std::wstring localAppData(required, L'\0');
    const DWORD written = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.data(), required);
    if (!written || written >= required) return {};
    localAppData.resize(written);
    return localAppData + L"\\TimoxVasio\\configuration.json";
}

ConfigurationLoadStatus AudioConfigurationStore::Load(
    AudioControllerConfiguration& configuration, std::string& error) const {
    error.clear();
    if (path_.empty()) {
        error = "LOCALAPPDATA is unavailable; cannot locate the TimoxVasio configuration";
        return ConfigurationLoadStatus::Error;
    }
    HANDLE file = CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
            return ConfigurationLoadStatus::Missing;
        error = "Unable to open the TimoxVasio configuration file";
        return ConfigurationLoadStatus::Error;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        size.QuadPart > kMaximumConfigurationBytes) {
        CloseHandle(file);
        error = "The TimoxVasio configuration file has an invalid size";
        return ConfigurationLoadStatus::Error;
    }
    std::string contents(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD bytesRead = 0;
    const BOOL read = contents.empty() || ReadFile(file, contents.data(),
        static_cast<DWORD>(contents.size()), &bytesRead, nullptr);
    CloseHandle(file);
    if (!read || bytesRead != contents.size()) {
        error = "Unable to read the complete TimoxVasio configuration file";
        return ConfigurationLoadStatus::Error;
    }

    try {
        if (!decodeConfiguration(Json::parse(contents), configuration)) {
            error = "The TimoxVasio configuration file does not match the supported schema";
            return ConfigurationLoadStatus::Error;
        }
    } catch (const Json::exception&) {
        error = "The TimoxVasio configuration file is malformed";
        return ConfigurationLoadStatus::Error;
    }
    return ConfigurationLoadStatus::Loaded;
}

bool AudioConfigurationStore::Save(
    const AudioControllerConfiguration& configuration, std::string& error) const {
    error.clear();
    if (path_.empty()) {
        error = "LOCALAPPDATA is unavailable; cannot save the TimoxVasio configuration";
        return false;
    }
    if (!ensureParentDirectory(path_, error)) return false;

    std::string contents;
    try {
        contents = encodeConfiguration(configuration).dump();
    } catch (const Json::exception&) {
        error = "The TimoxVasio configuration cannot be serialized";
        return false;
    }
    if (contents.size() > kMaximumConfigurationBytes) {
        error = "The TimoxVasio configuration exceeds the supported size";
        return false;
    }

    const std::wstring temporaryPath = path_ + L".tmp";
    HANDLE file = CreateFileW(temporaryPath.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "Unable to create a temporary TimoxVasio configuration file";
        return false;
    }
    DWORD written = 0;
    const BOOL write = WriteFile(file, contents.data(), static_cast<DWORD>(contents.size()),
        &written, nullptr);
    const BOOL flushed = write && written == contents.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!flushed) {
        DeleteFileW(temporaryPath.c_str());
        error = "Unable to write the complete TimoxVasio configuration file";
        return false;
    }
    if (!MoveFileExW(temporaryPath.c_str(), path_.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporaryPath.c_str());
        error = "Unable to atomically replace the TimoxVasio configuration file";
        return false;
    }
    return true;
}
