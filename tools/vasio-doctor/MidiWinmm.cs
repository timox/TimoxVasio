using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;

public static class MidiWinmm
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct InCaps
    {
        public ushort ManufacturerId;
        public ushort ProductId;
        public uint DriverVersion;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string Name;
        public uint Support;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct OutCaps
    {
        public ushort ManufacturerId;
        public ushort ProductId;
        public uint DriverVersion;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string Name;
        public ushort Technology;
        public ushort Voices;
        public ushort Notes;
        public ushort ChannelMask;
        public uint Support;
    }

    public sealed class Port
    {
        public int Id { get; set; }
        public string Flow { get; set; }
        public string Name { get; set; }
        public uint DriverVersion { get; set; }
    }

    public sealed class ClockReport
    {
        public uint OpenError { get; set; }
        public uint StartError { get; set; }
        public int ClockCount { get; set; }
        public int StartCount { get; set; }
        public int StopCount { get; set; }
        public double TempoBpm { get; set; }
        public double JitterMs { get; set; }
        public string State { get; set; }
    }

    public sealed class PortOpenCheck
    {
        public uint Status { get; set; }
        public string State { get; set; }
    }

    private delegate void MidiCallback(IntPtr handle, uint message, UIntPtr instance,
        UIntPtr param1, UIntPtr param2);

    [DllImport("winmm.dll")] private static extern uint midiInGetNumDevs();
    [DllImport("winmm.dll")] private static extern uint midiOutGetNumDevs();
    [DllImport("winmm.dll", CharSet = CharSet.Unicode, EntryPoint = "midiInGetDevCapsW")]
    private static extern uint midiInGetDevCaps(UIntPtr id, out InCaps caps, uint size);
    [DllImport("winmm.dll", CharSet = CharSet.Unicode, EntryPoint = "midiOutGetDevCapsW")]
    private static extern uint midiOutGetDevCaps(UIntPtr id, out OutCaps caps, uint size);
    [DllImport("winmm.dll")] private static extern uint midiInOpen(out IntPtr handle,
        uint deviceId, MidiCallback callback, UIntPtr instance, uint flags);
    [DllImport("winmm.dll")] private static extern uint midiInStart(IntPtr handle);
    [DllImport("winmm.dll")] private static extern uint midiInStop(IntPtr handle);
    [DllImport("winmm.dll")] private static extern uint midiInClose(IntPtr handle);
    [DllImport("winmm.dll")] private static extern uint midiOutOpen(out IntPtr handle,
        uint deviceId, IntPtr callback, UIntPtr instance, uint flags);
    [DllImport("winmm.dll")] private static extern uint midiOutClose(IntPtr handle);
    [DllImport("kernel32.dll")] private static extern bool QueryPerformanceCounter(out long value);
    [DllImport("kernel32.dll")] private static extern bool QueryPerformanceFrequency(out long value);

    public static Port[] Enumerate()
    {
        var ports = new List<Port>();
        for (uint id = 0; id < midiInGetNumDevs(); ++id)
        {
            InCaps caps;
            if (midiInGetDevCaps((UIntPtr)id, out caps, (uint)Marshal.SizeOf<InCaps>()) == 0)
                ports.Add(new Port { Id = (int)id, Flow = "input", Name = caps.Name,
                    DriverVersion = caps.DriverVersion });
        }
        for (uint id = 0; id < midiOutGetNumDevs(); ++id)
        {
            OutCaps caps;
            if (midiOutGetDevCaps((UIntPtr)id, out caps, (uint)Marshal.SizeOf<OutCaps>()) == 0)
                ports.Add(new Port { Id = (int)id, Flow = "output", Name = caps.Name,
                    DriverVersion = caps.DriverVersion });
        }
        return ports.ToArray();
    }

    public static PortOpenCheck CheckOpen(uint deviceId, string flow)
    {
        IntPtr handle = IntPtr.Zero;
        uint status = flow == "input"
            ? midiInOpen(out handle, deviceId, null, UIntPtr.Zero, 0)
            : midiOutOpen(out handle, deviceId, IntPtr.Zero, UIntPtr.Zero, 0);
        try
        {
            return new PortOpenCheck {
                Status = status,
                State = status == 0 ? "available" :
                    status == 4 ? "exclusive-occupied" : "open-failed"
            };
        }
        finally
        {
            if (status == 0 && handle != IntPtr.Zero)
            {
                if (flow == "input") midiInClose(handle);
                else midiOutClose(handle);
            }
        }
    }

    public static ClockReport Listen(uint deviceId, int seconds)
    {
        var report = new ClockReport { State = "unavailable" };
        var clocks = new List<long>();
        long frequency;
        if (!QueryPerformanceFrequency(out frequency) || frequency <= 0) return report;
        object gate = new object();
        MidiCallback callback = (handle, message, instance, param1, param2) =>
        {
            if (message != 0x3C3) return; // MIM_DATA
            byte status = (byte)(param1.ToUInt64() & 0xFF);
            long qpc;
            if (!QueryPerformanceCounter(out qpc)) return;
            lock (gate)
            {
                if (status == 0xF8) clocks.Add(qpc);
                else if (status == 0xFA) report.StartCount++;
                else if (status == 0xFC) report.StopCount++;
            }
        };
        IntPtr input;
        report.OpenError = midiInOpen(out input, deviceId, callback, UIntPtr.Zero, 0x30000);
        if (report.OpenError != 0) return report;
        try
        {
            report.StartError = midiInStart(input);
            if (report.StartError != 0) return report;
            Thread.Sleep(TimeSpan.FromSeconds(seconds));
            midiInStop(input);
            lock (gate)
            {
                report.ClockCount = clocks.Count;
                if (clocks.Count < 3) { report.State = "insufficient-clock"; return report; }
                double sum = 0;
                for (int i = 1; i < clocks.Count; ++i) sum += clocks[i] - clocks[i - 1];
                double mean = sum / (clocks.Count - 1);
                double variance = 0;
                for (int i = 1; i < clocks.Count; ++i)
                {
                    double delta = clocks[i] - clocks[i - 1] - mean;
                    variance += delta * delta;
                }
                variance /= clocks.Count - 1;
                report.TempoBpm = 60.0 * frequency / (24.0 * mean);
                report.JitterMs = 1000.0 * Math.Sqrt(variance) / frequency;
                report.State = "measured";
            }
            return report;
        }
        finally
        {
            midiInClose(input);
            GC.KeepAlive(callback);
        }
    }
}
