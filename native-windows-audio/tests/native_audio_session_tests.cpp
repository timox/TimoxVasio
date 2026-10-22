#include "native_audio_session.h"
#include <cstdio>
int main() {
    unsigned failed=0;
    auto check=[&](bool v){if(!v) ++failed;};
    timox_native::Session session;
    timox_u64 generation=99;
    check(!session.Open(0,generation) && generation==99);
    check(session.Open(7,generation) && generation==1);
    check(!session.Open(8,generation) && generation==1);
    check(session.Allows(7,1));
    check(!session.Allows(8,1));
    check(!session.Allows(7,0));
    check(!session.Close(8) && session.Allows(7,1));
    check(session.Close(7) && !session.Allows(7,1));
    check(!session.Close(7));
    check(session.Open(8,generation) && generation==2);
    check(!session.Allows(8,1));
    session.Remove();
    check(!session.Allows(8,2));
    check(!session.Open(9,generation) && generation==2);
    session.Remove();
    timox_native::Session exhausted(~timox_u64{0}-1);
    check(exhausted.Open(1,generation) && generation==~timox_u64{0});
    check(exhausted.Close(1));
    check(!exhausted.Open(1,generation) && generation==~timox_u64{0});
    if(failed) std::fprintf(stderr,"%u session failures\n",failed);
    return failed ? 1 : 0;
}
