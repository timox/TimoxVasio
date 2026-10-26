#include "service_identity_win32.h"
#include <iostream>
#include <stdexcept>
using namespace timox_broker;
void Check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){try{
    BrokerServiceIdentity valid;valid.localService=true;valid.sessionId=0;
    valid.serviceGroupAttributes=SE_GROUP_ENABLED|SE_GROUP_OWNER|SE_GROUP_ENABLED_BY_DEFAULT;
    Check(IsBrokerServiceIdentity(valid),"valid service identity refused");
    for(unsigned fault=0;fault<5;++fault){auto bad=valid;
        if(fault==0)bad.localService=false;
        if(fault==1)bad.sessionId=1;
        if(fault==2)bad.serviceGroupAttributes&=~SE_GROUP_ENABLED;
        if(fault==3)bad.serviceGroupAttributes&=~SE_GROUP_OWNER;
        if(fault==4)bad.serviceGroupAttributes|=SE_GROUP_USE_FOR_DENY_ONLY;
        Check(!IsBrokerServiceIdentity(bad),"ineligible service token accepted");
    }
    BrokerServiceIdentity observed;
    Check(ObserveBrokerServiceIdentity(observed)==ERROR_SUCCESS,"process token observation failed");
    auto unchanged=observed;
    Check(ImpersonateSelf(SecurityIdentification)!=FALSE,"test impersonation failed");
    const auto nested=ObserveBrokerServiceIdentity(unchanged);
    const auto reverted=RevertToSelf();
    Check(reverted!=FALSE,"test revert failed");
    Check(nested==ERROR_BAD_IMPERSONATION_LEVEL&&unchanged.localService==observed.localService&&
        unchanged.sessionId==observed.sessionId&&unchanged.serviceGroupAttributes==observed.serviceGroupAttributes,
        "impersonating observer changed identity");
    std::cout<<"Service identity observed: eligible="<<IsBrokerServiceIdentity(observed)
        <<"; session="<<observed.sessionId<<"; attributes="<<observed.serviceGroupAttributes<<'\n';
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
