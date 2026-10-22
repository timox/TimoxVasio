#pragma once
#include "native_audio_geometry.h"

namespace timox_native {
// Pure lifecycle policy. Caller serializes EVERY operation with transfers and
// queue purge. Owner is a trusted internal handle-context identity, not a user
// credential. This class supplies neither ACLs nor object rundown protection.
class Session final {
public:
    // Allows restoring a trusted monotone counter; never accept it from a packet.
    explicit Session(timox_u64 lastGeneration=0) noexcept : generation_(lastGeneration) {}
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;

    bool Open(timox_u64 owner, timox_u64& generation) noexcept {
        if (!owner || owner_ || removed_ || generation_==~timox_u64{0}) return false;
        ++generation_;
        owner_=owner;
        generation=generation_;
        return true;
    }
    bool Allows(timox_u64 owner, timox_u64 generation) const noexcept {
        return !removed_ && owner!=0 && owner==owner_ && generation==generation_;
    }
    bool Close(timox_u64 owner) noexcept {
        if (!owner || owner!=owner_) return false;
        owner_=0;
        return true;
    }
    void Remove() noexcept {
        removed_=true;
        owner_=0;
    }
private:
    timox_u64 generation_=0;
    timox_u64 owner_=0;
    bool removed_=false;
};
}
