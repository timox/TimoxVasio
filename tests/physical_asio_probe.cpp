#include "physical_asio_host.h"

#include <cstdio>
#include <set>

int main() {
    PhysicalAsioHost host;
    const auto drivers = host.enumerate();
    std::set<std::wstring> ids;
    for (const auto& driver : drivers) {
        if (driver.id.empty() || driver.name.empty() || !ids.insert(driver.id).second) {
            std::fprintf(stderr, "physical ASIO inventory has an empty or duplicate ID/name\n");
            return 1;
        }
        if (driver.name.rfind("VASIO", 0) == 0) {
            std::fprintf(stderr, "virtual driver leaked into physical inventory: %s\n", driver.name.c_str());
            return 2;
        }
        std::printf("%ls\t%s\n", driver.id.c_str(), driver.name.c_str());
    }
    std::printf("PASS: %zu external ASIO registry entries discovered without opening a device.\n", drivers.size());
    return 0;
}
