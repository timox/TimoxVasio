#pragma once
#include <cstdint>
#include <vector>
namespace timox::lab {
enum class Principal { System, Administrators, Other };
struct AccessEntry { Principal principal; bool allow; bool inherited; std::uint32_t mask; };
struct FileObservation {
    bool reparse = false, directory = false, protectedDacl = false, nullDacl = false;
    std::uint32_t links = 1;
    Principal owner = Principal::Other;
    std::vector<AccessEntry> entries;
};
// Only private transaction directory/files use this exact policy. Ancestors
// are instead held open without delete sharing and checked for reparse points.
bool SafeReceiptObject(const FileObservation&, bool expectDirectory) noexcept;
}
