#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

#include <cstdio>

int main() {
    winrt::init_apartment();
    const auto value = winrt::Windows::Data::Json::JsonValue::Parse(
        LR"({"id":"probe","command":"configuration.apply","payload":{"routes":[]}})");
    const auto object = value.GetObject();
    if (object.GetNamedString(L"id") != L"probe" ||
        object.GetNamedString(L"command") != L"configuration.apply" ||
        object.GetNamedObject(L"payload").GetNamedArray(L"routes").Size() != 0) {
        std::fprintf(stderr, "WinRT JSON parse returned unexpected values\n");
        return 1;
    }
    std::puts("PASS: Windows JSON runtime parses the control API envelope and route arrays.");
    return 0;
}
