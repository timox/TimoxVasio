#include "wasapi_device_enumerator.h"
#include <Windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <cstring>
#include <limits>
#include <sstream>

namespace {
using Microsoft::WRL::ComPtr;
template<class T> struct TaskMemory {
    T* value = nullptr;
    ~TaskMemory() { CoTaskMemFree(value); }
    TaskMemory() = default;
    TaskMemory(const TaskMemory&) = delete;
    TaskMemory& operator=(const TaskMemory&) = delete;
};
struct PropertyValue {
    PROPVARIANT value{};
    ~PropertyValue() { PropVariantClear(&value); }
};
bool Fail(std::string& error, const char* message) {
    error = message;
    return false;
}
bool Check(HRESULT hr, const char* operation, std::string& error) {
    if (SUCCEEDED(hr)) return true;
    std::ostringstream text;
    text << operation << " failed (HRESULT 0x" << std::hex << static_cast<unsigned long>(hr) << ')';
    error = text.str();
    return false;
}
bool NativeFlow(WasapiDataFlow flow, EDataFlow& native, std::string& error) {
    switch (flow) {
    case WasapiDataFlow::Capture: native = eCapture; return true;
    case WasapiDataFlow::Render: native = eRender; return true;
    }
    return Fail(error, "Invalid WASAPI data flow");
}
bool CreateEnumerator(ComPtr<IMMDeviceEnumerator>& enumerator, std::string& error) {
    return Check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        IID_PPV_ARGS(enumerator.GetAddressOf())), "Create MMDeviceEnumerator (caller must initialize COM)", error);
}
bool DeviceId(IMMDevice* device, std::wstring& id, std::string& error) {
    TaskMemory<wchar_t> text;
    if (!Check(device->GetId(&text.value), "Get endpoint ID", error)) return false;
    if (!text.value || !*text.value) return Fail(error, "Endpoint returned an empty ID");
    id = text.value;
    return true;
}
void ReadMixFormat(IMMDevice* device, WasapiDeviceInfo& info) {
    if (!(info.state & DEVICE_STATE_ACTIVE)) {
        info.formatError = "Endpoint is not active";
        return;
    }
    ComPtr<IAudioClient> client;
    if (!Check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
        reinterpret_cast<void**>(client.GetAddressOf())), "Activate audio client", info.formatError)) return;
    TaskMemory<WAVEFORMATEX> format;
    if (!Check(client->GetMixFormat(&format.value), "Get shared mix format", info.formatError)) return;
    if (!format.value) { info.formatError = "Endpoint returned no mix format"; return; }
    WasapiMixFormat parsed;
    // The OS owns the allocation contract: cbSize bytes follow WAVEFORMATEX.
    if (ParseWasapiMixFormat(format.value, sizeof(WAVEFORMATEX) + format.value->cbSize, parsed, info.formatError))
        info.mixFormat = parsed;
}
}

bool ParseWasapiMixFormat(const void* bytes, std::size_t size,
    WasapiMixFormat& result, std::string& error) {
    result = {};
    error.clear();
    if (!bytes || size < sizeof(WAVEFORMATEX)) return Fail(error, "Truncated WAVEFORMATEX");
    WAVEFORMATEX base{};
    std::memcpy(&base, bytes, sizeof(base));
    if (base.cbSize > size - sizeof(base)) return Fail(error, "Truncated wave format extension");
    WasapiMixFormat candidate;
    WORD tag = base.wFormatTag;
    candidate.validBits = base.wBitsPerSample;
    if (tag == WAVE_FORMAT_EXTENSIBLE) {
        if (base.cbSize < 22 || size < sizeof(WAVEFORMATEXTENSIBLE))
            return Fail(error, "Truncated WAVEFORMATEXTENSIBLE");
        WAVEFORMATEXTENSIBLE ext{};
        std::memcpy(&ext, bytes, sizeof(ext));
        const GUID pcm = {WAVE_FORMAT_PCM, 0, 0x0010, {0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71}};
        const GUID floating = {WAVE_FORMAT_IEEE_FLOAT, 0, 0x0010, {0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71}};
        if (IsEqualGUID(ext.SubFormat, pcm)) tag = WAVE_FORMAT_PCM;
        else if (IsEqualGUID(ext.SubFormat, floating)) tag = WAVE_FORMAT_IEEE_FLOAT;
        else return Fail(error, "Unsupported extensible sample encoding");
        candidate.validBits = ext.Samples.wValidBitsPerSample;
        candidate.channelMask = ext.dwChannelMask;
        unsigned positions = 0;
        for (auto mask = ext.dwChannelMask; mask; mask &= mask - 1) ++positions;
        if (positions && positions != base.nChannels) return Fail(error, "Channel mask does not match channel count");
    }
    if (tag != WAVE_FORMAT_PCM && tag != WAVE_FORMAT_IEEE_FLOAT)
        return Fail(error, "Unsupported sample encoding");
    if (!base.nChannels || !base.nSamplesPerSec) return Fail(error, "Empty channel count or sample rate");
    const bool floating = tag == WAVE_FORMAT_IEEE_FLOAT;
    if (floating ? base.wBitsPerSample != 32 && base.wBitsPerSample != 64
        : base.wBitsPerSample != 8 && base.wBitsPerSample != 16 && base.wBitsPerSample != 24 && base.wBitsPerSample != 32)
        return Fail(error, "Unsupported sample container size");
    if (!candidate.validBits || candidate.validBits > base.wBitsPerSample ||
        (floating && candidate.validBits != base.wBitsPerSample)) return Fail(error, "Invalid valid-bit count");
    const std::uint32_t alignment = static_cast<std::uint32_t>(base.nChannels) * (base.wBitsPerSample / 8);
    const std::uint64_t byteRate = static_cast<std::uint64_t>(base.nSamplesPerSec) * alignment;
    if (alignment != base.nBlockAlign || byteRate > (std::numeric_limits<DWORD>::max)() ||
        byteRate != base.nAvgBytesPerSec) return Fail(error, "Inconsistent block alignment or byte rate");
    candidate.sampleRate = base.nSamplesPerSec;
    candidate.channels = base.nChannels;
    candidate.containerBits = base.wBitsPerSample;
    candidate.blockAlign = base.nBlockAlign;
    candidate.encoding = floating ? WasapiSampleEncoding::IeeeFloat : WasapiSampleEncoding::PcmInteger;
    result = candidate;
    return true;
}

bool WasapiDeviceEnumerator::enumerate(std::vector<WasapiDeviceInfo>& devices, std::string& error) const {
    devices.clear();
    error.clear();
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (!CreateEnumerator(enumerator, error)) return false;
    std::vector<WasapiDeviceInfo> inventory;
    for (const auto flow : {WasapiDataFlow::Capture, WasapiDataFlow::Render}) {
        EDataFlow native = eAll;
        if (!NativeFlow(flow, native, error)) return false;
        ComPtr<IMMDeviceCollection> collection;
        if (!Check(enumerator->EnumAudioEndpoints(native, DEVICE_STATEMASK_ALL, collection.GetAddressOf()),
            "Enumerate audio endpoints", error)) return false;
        UINT count = 0;
        if (!Check(collection->GetCount(&count), "Count audio endpoints", error)) return false;
        for (UINT index = 0; index < count; ++index) {
            ComPtr<IMMDevice> device;
            if (!Check(collection->Item(index, device.GetAddressOf()), "Read audio endpoint", error)) return false;
            WasapiDeviceInfo info;
            info.flow = flow;
            if (!DeviceId(device.Get(), info.id, error)) return false;
            DWORD state = 0;
            if (!Check(device->GetState(&state), "Read endpoint state", error)) return false;
            info.state = state;
            info.name = info.id; // Stable, nonempty fallback when the display property is unavailable.
            ComPtr<IPropertyStore> properties;
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, properties.GetAddressOf()))) {
                PropertyValue property;
                if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &property.value)) &&
                    property.value.vt == VT_LPWSTR && property.value.pwszVal && *property.value.pwszVal)
                    info.name = property.value.pwszVal;
            }
            ReadMixFormat(device.Get(), info);
            inventory.push_back(std::move(info));
        }
    }
    devices.swap(inventory);
    return true;
}

bool WasapiDeviceEnumerator::resolve(WasapiDataFlow flow, const std::wstring& explicitId,
    std::optional<WasapiDefaultRole> defaultRole, std::wstring& resolvedId, std::string& error) const {
    resolvedId.clear();
    error.clear();
    if (explicitId.empty() == !defaultRole.has_value())
        return Fail(error, "Select exactly one endpoint ID or explicit default role");
    EDataFlow native = eAll;
    if (!NativeFlow(flow, native, error)) return false;
    ERole role = eConsole;
    if (defaultRole) {
        switch (*defaultRole) {
        case WasapiDefaultRole::Console: role = eConsole; break;
        case WasapiDefaultRole::Multimedia: role = eMultimedia; break;
        case WasapiDefaultRole::Communications: role = eCommunications; break;
        default: return Fail(error, "Invalid WASAPI default role");
        }
    }
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (!CreateEnumerator(enumerator, error)) return false;
    ComPtr<IMMDevice> device;
    const HRESULT hr = defaultRole
        ? enumerator->GetDefaultAudioEndpoint(native, role, device.GetAddressOf())
        : enumerator->GetDevice(explicitId.c_str(), device.GetAddressOf());
    if (!Check(hr, "Resolve audio endpoint", error)) return false;
    ComPtr<IMMEndpoint> endpoint;
    if (!Check(device.As(&endpoint), "Read endpoint flow interface", error)) return false;
    EDataFlow actual = eAll;
    if (!Check(endpoint->GetDataFlow(&actual), "Read endpoint flow", error)) return false;
    if (actual != native) return Fail(error, "Endpoint direction does not match requested flow");
    DWORD state = 0;
    if (!Check(device->GetState(&state), "Read endpoint state", error)) return false;
    if (!(state & DEVICE_STATE_ACTIVE)) return Fail(error, "Endpoint is not active");
    return DeviceId(device.Get(), resolvedId, error);
}
