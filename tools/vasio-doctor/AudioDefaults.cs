using System;
using System.Runtime.InteropServices;

public sealed class AudioDefaultEndpoint
{
    public string Flow { get; set; }
    public string Role { get; set; }
    public string Id { get; set; }
    public string Error { get; set; }
}

public static class AudioDefaults
{
    [ComImport]
    [Guid("A95664D2-9614-4F35-A746-DE8DB63617E6")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IMMDeviceEnumerator
    {
        [PreserveSig] int EnumAudioEndpoints(int flow, uint stateMask, out IntPtr collection);
        [PreserveSig] int GetDefaultAudioEndpoint(int flow, int role, out IMMDevice device);
        [PreserveSig] int GetDevice([MarshalAs(UnmanagedType.LPWStr)] string id, out IMMDevice device);
        [PreserveSig] int RegisterEndpointNotificationCallback(IntPtr client);
        [PreserveSig] int UnregisterEndpointNotificationCallback(IntPtr client);
    }

    [ComImport]
    [Guid("D666063F-1587-4E43-81F1-B948E807363F")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IMMDevice
    {
        [PreserveSig] int Activate(ref Guid iid, uint context, IntPtr activationParams, out IntPtr instance);
        [PreserveSig] int OpenPropertyStore(uint access, out IntPtr store);
        [PreserveSig] int GetId(out IntPtr id);
        [PreserveSig] int GetState(out uint state);
    }

    public static AudioDefaultEndpoint[] Read()
    {
        var type = Type.GetTypeFromCLSID(new Guid("BCDE0395-E52F-467C-8E3D-C4579291692E"), true);
        var enumerator = (IMMDeviceEnumerator)Activator.CreateInstance(type);
        var result = new AudioDefaultEndpoint[6];
        try
        {
            int index = 0;
            for (int flow = 0; flow <= 1; ++flow)
            {
                for (int role = 0; role <= 2; ++role)
                {
                    var entry = new AudioDefaultEndpoint {
                        Flow = flow == 0 ? "render" : "capture",
                        Role = role == 0 ? "console" : role == 1 ? "multimedia" : "communications"
                    };
                    IMMDevice device = null;
                    try
                    {
                        int status = enumerator.GetDefaultAudioEndpoint(flow, role, out device);
                        if (status == 0 && device != null)
                        {
                            IntPtr id = IntPtr.Zero;
                            try
                            {
                                status = device.GetId(out id);
                                if (status == 0 && id != IntPtr.Zero)
                                    entry.Id = Marshal.PtrToStringUni(id);
                                else entry.Error = "IMMDevice.GetId HRESULT 0x" + status.ToString("X8");
                            }
                            finally { if (id != IntPtr.Zero) Marshal.FreeCoTaskMem(id); }
                        }
                        else entry.Error = "GetDefaultAudioEndpoint HRESULT 0x" + status.ToString("X8");
                    }
                    catch (Exception exception) { entry.Error = exception.Message; }
                    finally { if (device != null) Marshal.ReleaseComObject(device); }
                    result[index++] = entry;
                }
            }
            return result;
        }
        finally { Marshal.ReleaseComObject(enumerator); }
    }
}
