#include "receipt_file_policy.h"
namespace timox::lab {
bool SafeReceiptObject(const FileObservation& o, bool directory) noexcept {
    if (o.reparse || o.directory != directory || (!directory && o.links != 1) ||
        !o.protectedDacl || o.nullDacl || o.owner == Principal::Other || o.entries.size() != 2) return false;
    bool system = false, admin = false;
    for (const auto& ace : o.entries) {
        if (!ace.allow || ace.inherited || ace.mask != 0x001f01ff) return false;
        if (ace.principal == Principal::System) system = true;
        else if (ace.principal == Principal::Administrators) admin = true;
        else return false;
    }
    return system && admin;
}
}
