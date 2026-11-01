#include "control.h"
#include <portcls.h>
#include <wdmsec.h>
#include <new.h>
#include "../include/native_audio_control_protocol.h"
#include "../include/native_audio_packet_queue.h"

namespace {
// Initialized once in DriverEntry, before any device is published; immutable
// thereafter. Includes the sample PnP handler and every PortCls default handler.
PDRIVER_DISPATCH AudioDispatch[IRP_MJ_MAXIMUM_FUNCTION+1]{};
// This GUID is a private security class, never the MEDIA or KS interface class.
const GUID ControlClass={0x20ec34c0,0x1f92,0x41b0,{0xa5,0x9e,0xfc,0x89,0x8f,0x74,0x2f,0x64}};
struct Control {
    KSPIN_LOCK lock;
    EX_RUNDOWN_REF rundown;
    volatile LONG stopped=0;
    timox_u64 generation=0;
    timox_native::PacketQueues packets;
};
Control* Context(PDEVICE_OBJECT device){return static_cast<Control*>(device->DeviceExtension);}
timox_u64 Owner(PFILE_OBJECT file){return static_cast<timox_u64>(reinterpret_cast<ULONG_PTR>(file));}
void Put(unsigned char* data,unsigned offset,timox_u64 value){
    for(unsigned i=0;i<8;++i)data[offset+i]=static_cast<unsigned char>(value>>(8*i));
}
NTSTATUS Complete(PIRP irp,NTSTATUS status,ULONG_PTR bytes=0){
    irp->IoStatus.Status=status;irp->IoStatus.Information=bytes;
    IoCompleteRequest(irp,IO_NO_INCREMENT);return status;
}
NTSTATUS Translate(timox_native::Transfer result){
    using timox_native::Transfer;
    switch(result){
    case Transfer::Ok:return STATUS_SUCCESS;
    case Transfer::Owner:return STATUS_ACCESS_DENIED;
    case Transfer::Protected:return STATUS_ACCESS_DENIED;
    case Transfer::Full:return STATUS_DEVICE_BUSY;
    case Transfer::Empty:return STATUS_NO_MORE_ENTRIES;
    case Transfer::Buffer:return STATUS_BUFFER_TOO_SMALL;
    default:return STATUS_INVALID_PARAMETER;
    }
}
}

void TimoxControlInstallDispatch(PDRIVER_OBJECT driver){
    for(unsigned i=0;i<=IRP_MJ_MAXIMUM_FUNCTION;++i){
        AudioDispatch[i]=driver->MajorFunction[i];
        driver->MajorFunction[i]=TimoxControlDispatch;
    }
}

NTSTATUS TimoxControlCreate(PDRIVER_OBJECT driver,PDEVICE_OBJECT* device){
    PAGED_CODE();
    *device=nullptr;
    UNICODE_STRING name=RTL_CONSTANT_STRING(L"\\Device\\TimoxWindowsAudioControl");
    UNICODE_STRING link=RTL_CONSTANT_STRING(L"\\DosDevices\\Global\\TimoxWindowsAudioControl");
    // The broker runs as LocalService with this exact service SID enabled.
    // Keep the private IOCTL device closed to ordinary users and other services.
    UNICODE_STRING acl=RTL_CONSTANT_STRING(
        L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;S-1-5-80-954356743-986487362-260697779-2273189862-4263205101)");
    PDEVICE_OBJECT created=nullptr;
    NTSTATUS status=IoCreateDeviceSecure(driver,sizeof(Control),&name,TIMOX_CONTROL_DEVICE_TYPE,
        FILE_DEVICE_SECURE_OPEN,FALSE,&acl,&ControlClass,&created);
    if(!NT_SUCCESS(status))return status;
    auto context=new(created->DeviceExtension) Control;
    KeInitializeSpinLock(&context->lock);ExInitializeRundownProtection(&context->rundown);
    status=IoCreateSymbolicLink(&link,&name);
    if(!NT_SUCCESS(status)){IoDeleteDevice(created);return status;}
    created->Flags&=~DO_DEVICE_INITIALIZING;
    *device=created;
    return STATUS_SUCCESS;
}
void TimoxControlStop(PDEVICE_OBJECT device){
    PAGED_CODE();
    if(!device)return;
    auto context=Context(device);
    if(InterlockedExchange(&context->stopped,1)!=0)return;
    UNICODE_STRING link=RTL_CONSTANT_STRING(L"\\DosDevices\\Global\\TimoxWindowsAudioControl");
    IoDeleteSymbolicLink(&link);
    // No lock held while waiting. Call only outside audio callbacks at PASSIVE.
    ExWaitForRundownProtectionRelease(&context->rundown);
    KIRQL old;KeAcquireSpinLock(&context->lock,&old);
    context->packets.Remove();
    KeReleaseSpinLock(&context->lock,old);
}
void TimoxControlDestroy(PDEVICE_OBJECT device){
    PAGED_CODE();
    if(!device)return;
    TimoxControlStop(device);
    // Extension remains alive for outstanding FILE_OBJECT/IRP references.
    // It contains no separately allocated buffer or nontrivial destructor.
    IoDeleteDevice(device);
}
NTSTATUS TimoxControlDispatch(PDEVICE_OBJECT device,PIRP irp){
    const auto stack=IoGetCurrentIrpStackLocation(irp);
    if(device->DeviceType!=TIMOX_CONTROL_DEVICE_TYPE){
        const auto major=stack->MajorFunction;
        if(major>IRP_MJ_MAXIMUM_FUNCTION || !AudioDispatch[major])
            return Complete(irp,STATUS_INVALID_DEVICE_REQUEST);
        return AudioDispatch[major](device,irp);
    }
    if(stack->MajorFunction==IRP_MJ_CLOSE)return Complete(irp,STATUS_SUCCESS);
    if(stack->MajorFunction!=IRP_MJ_CREATE && stack->MajorFunction!=IRP_MJ_CLEANUP &&
       stack->MajorFunction!=IRP_MJ_DEVICE_CONTROL)
        return Complete(irp,STATUS_INVALID_DEVICE_REQUEST);
    // A kernel caller can issue some majors at DISPATCH_LEVEL. Reject before
    // the <= APC_LEVEL critical-region operation; ordinary user IOCTLs are PASSIVE.
    if(KeGetCurrentIrql()>APC_LEVEL)return Complete(irp,STATUS_INVALID_DEVICE_STATE);
    auto context=Context(device);
    KeEnterCriticalRegion();
    if(!ExAcquireRundownProtection(&context->rundown)){
        KeLeaveCriticalRegion();
        return Complete(irp,stack->MajorFunction==IRP_MJ_CLEANUP ? STATUS_SUCCESS:STATUS_DEVICE_REMOVED);
    }
    KIRQL old;KeAcquireSpinLock(&context->lock,&old);
    NTSTATUS status=STATUS_INVALID_DEVICE_REQUEST;
    ULONG_PTR returned=0;
    const auto owner=Owner(stack->FileObject);
    if(stack->MajorFunction==IRP_MJ_CREATE){
        if(stack->FileObject->FileName.Length!=0)status=STATUS_OBJECT_NAME_NOT_FOUND;
        else status=context->packets.Open(owner,context->generation) ? STATUS_SUCCESS:STATUS_SHARING_VIOLATION;
    } else if(stack->MajorFunction==IRP_MJ_CLEANUP){
        context->packets.Close(owner);status=STATUS_SUCCESS;
    } else if(stack->MajorFunction==IRP_MJ_DEVICE_CONTROL){
        auto data=static_cast<unsigned char*>(irp->AssociatedIrp.SystemBuffer);
        const ULONG input=stack->Parameters.DeviceIoControl.InputBufferLength;
        const ULONG output=stack->Parameters.DeviceIoControl.OutputBufferLength;
        using timox_native::detail::ReadLittleEndian;
        if(input>32832 || output>32832)status=STATUS_INVALID_PARAMETER;
        else switch(stack->Parameters.DeviceIoControl.IoControlCode){
        case TIMOX_IOCTL_SESSION:
            if(input!=0 || output<8 || !data){status=STATUS_INVALID_PARAMETER;break;}
            // Ownership is verified without consuming audio by the wrapper.
            status=context->packets.Allows(owner,context->generation) ? STATUS_SUCCESS:STATUS_ACCESS_DENIED;
            if(NT_SUCCESS(status)){Put(data,0,context->generation);returned=8;status=STATUS_SUCCESS;}
            break;
        case TIMOX_IOCTL_WRITE_CAPTURE:
            if(input>32832 || output!=0){status=STATUS_INVALID_PARAMETER;break;}
            status=Translate(context->packets.WriteCapture(owner,context->generation,data,input));break;
        case TIMOX_IOCTL_READ_RENDER:
            if(input!=16 || !data || ReadLittleEndian(data,12,4)!=0){status=STATUS_INVALID_PARAMETER;break;}
            {
                const auto generation=ReadLittleEndian(data,0,8);
                const auto frames=static_cast<timox_u32>(ReadLittleEndian(data,8,4));
                auto result=context->packets.ReadRender(owner,generation,data,output,frames);
                status=Translate(result.status);returned=result.bytes;
            }break;
        case TIMOX_IOCTL_STATUS:
            if(input!=8 || output<24 || !data){status=STATUS_INVALID_PARAMETER;break;}
            status=context->packets.Allows(owner,ReadLittleEndian(data,0,8)) ? STATUS_SUCCESS:STATUS_ACCESS_DENIED;
            if(NT_SUCCESS(status)){
                Put(data,0,context->packets.RenderDropped());Put(data,8,context->packets.CaptureDropped());
                Put(data,16,context->packets.CaptureSilence());returned=24;status=STATUS_SUCCESS;
            }break;
        default:break;
        }
    }
    KeReleaseSpinLock(&context->lock,old);
    ExReleaseRundownProtection(&context->rundown);
    KeLeaveCriticalRegion();
    return Complete(irp,status,returned);
}
void TimoxControlRender(PDEVICE_OBJECT device,const unsigned char* pcm,ULONG frames){
    if(!device)return;
    auto context=Context(device);
    if(!ExAcquireRundownProtection(&context->rundown))return;
    KIRQL old;KeAcquireSpinLock(&context->lock,&old);
    context->packets.PushRender(pcm,frames);
    KeReleaseSpinLock(&context->lock,old);ExReleaseRundownProtection(&context->rundown);
}
void TimoxControlCapture(PDEVICE_OBJECT device,unsigned char* pcm,ULONG frames){
    if(!pcm || frames>4096)return;
    if(!device){RtlZeroMemory(pcm,frames*8);return;}
    auto context=Context(device);
    if(!ExAcquireRundownProtection(&context->rundown)){RtlZeroMemory(pcm,frames*8);return;}
    KIRQL old;KeAcquireSpinLock(&context->lock,&old);
    context->packets.ReadCapture(pcm,frames);
    KeReleaseSpinLock(&context->lock,old);ExReleaseRundownProtection(&context->rundown);
}
void TimoxControlBlockRender(PDEVICE_OBJECT device,bool blocked){
    if(!device)return;
    auto context=Context(device);
    // Called by PASSIVE content-rights path; protect rundown from suspension.
    KeEnterCriticalRegion();
    if(ExAcquireRundownProtection(&context->rundown)){
        KIRQL old;KeAcquireSpinLock(&context->lock,&old);
        context->packets.SetRenderBlocked(blocked);
        KeReleaseSpinLock(&context->lock,old);ExReleaseRundownProtection(&context->rundown);
    }
    KeLeaveCriticalRegion();
}
void TimoxControlGap(PDEVICE_OBJECT device,bool capture,ULONGLONG frames){
    if(!device || !frames)return;
    auto context=Context(device);
    if(!ExAcquireRundownProtection(&context->rundown))return;
    KIRQL old;KeAcquireSpinLock(&context->lock,&old);
    context->packets.AccountGap(capture,frames);
    KeReleaseSpinLock(&context->lock,old);ExReleaseRundownProtection(&context->rundown);
}
