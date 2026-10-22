#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include "native_audio_control_protocol.h"
static unsigned long long generation(HANDLE handle){
    unsigned char bytes[8]{};DWORD actual=0;
    if(!DeviceIoControl(handle,TIMOX_IOCTL_SESSION,nullptr,0,bytes,8,&actual,nullptr)||actual!=8)return 0;
    unsigned long long value=0;
    for(unsigned i=0;i<8;++i)value|=static_cast<unsigned long long>(bytes[i])<<(i*8);
    return value;
}
static HANDLE open(){return CreateFileW(L"\\\\.\\Global\\TimoxWindowsAudioControl",GENERIC_READ|GENERIC_WRITE,
    FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);}
int main(int argc,char** argv){
    if(argc!=2 || std::strcmp(argv[1],"--exercise-control")!=0){
        std::puts("NativeAudioDriverProbe --exercise-control\nRequires a previously installed test driver; never installs or loads one.\nChecks private control only, not WASAPI audio or kernel lifetime qualification.");
        return 0;
    }
    HANDLE first=open();
    if(first==INVALID_HANDLE_VALUE){std::printf("Open failed: %lu\n",GetLastError());return 2;}
    unsigned failed=0;
    const auto initial=generation(first);
    if(initial==0)++failed;
    HANDLE second=open();
    if(second!=INVALID_HANDLE_VALUE){++failed;CloseHandle(second);}
    unsigned char malformed[8]{};DWORD actual=99;
    if(DeviceIoControl(first,TIMOX_IOCTL_WRITE_CAPTURE,malformed,8,nullptr,0,&actual,nullptr))++failed;
    actual=99;
    if(ReadFile(first,malformed,8,&actual,nullptr))++failed; // must never reach PortCls
    CloseHandle(first);
    first=open();
    if(first==INVALID_HANDLE_VALUE)++failed;
    else {if(generation(first)<=initial)++failed;CloseHandle(first);}
    std::printf("control failures=%u (audio/lifetime tests not run)\n",failed);
    return failed?1:0;
}
