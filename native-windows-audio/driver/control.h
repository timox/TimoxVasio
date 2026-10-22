#pragma once
#include <ntddk.h>
NTSTATUS TimoxControlCreate(PDRIVER_OBJECT driver,PDEVICE_OBJECT* device);
void TimoxControlStop(PDEVICE_OBJECT device);
void TimoxControlDestroy(PDEVICE_OBJECT device);
void TimoxControlRender(PDEVICE_OBJECT device,const unsigned char* pcm,ULONG frames);
void TimoxControlCapture(PDEVICE_OBJECT device,unsigned char* pcm,ULONG frames);
void TimoxControlBlockRender(PDEVICE_OBJECT device,bool blocked);
void TimoxControlGap(PDEVICE_OBJECT device,bool capture,ULONGLONG frames);
DRIVER_DISPATCH TimoxControlDispatch;
void TimoxControlInstallDispatch(PDRIVER_OBJECT driver);
