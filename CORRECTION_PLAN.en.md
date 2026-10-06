# Correction plan — proper ASIO implementation

[Français](CORRECTION_PLAN.md) | **English**

> Historical plan superseded by the single-driver TimoxVasio implementation.
> Do not use its VASIO1–VASIO4 commands as installation instructions.
> The current plan is [here](docs/superpowers/plans/2026-10-03-vasio-256-physical-master-plan.en.md).

## Problem summary

The implementation at the time used PortAudio (an abstraction) instead of implementing the Steinberg SDK's `IASIO` interface directly. **Result:** Windows/DAWs would not recognize the drivers.

## Missing pieces

### 1. Complete IASIO interface (CRITICAL)

```cpp
// At the time: VirtualDriver (custom class)
// Needed: inherit IASIO and implement 40+ methods
```

### 2. bufferSwitch callbacks (CRITICAL)

```cpp
// At the time: no callbacks
// Needed: real-time callbacks synchronized with the audio kernel
void bufferSwitchCallback(long index, ASIOBool processNow) {
    // Process audio here
}
```

### 3. Double buffering (CRITICAL)

```cpp
// At the time: no ping-pong buffers
// Needed: alternate buffers[0] and buffers[1]
```

### 4. DLL and COM registration (CRITICAL)

```text
// At the time: only an executable
// Needed: DLL registered in the Windows registry with a CLSID
HKEY_LOCAL_MACHINE\SOFTWARE\Steinberg\ASIO\VASIO1
  ├─ CLSID = {UNIQUE-GUID}
  └─ InprocServer32 = C:\path\to\VASIO1.dll
```

## Proposed solution

### Phase 1: Check the state at the time (1 hour)

```text
# Check whether VASIO1–4 appear in a DAW
1. Build the code at the time
2. Open Reaper/Mixxx
3. Look for VASIO1–4 in audio settings
4. If absent, the implementation is incomplete
```

### Phase 2: Implement IASIO properly (4–6 hours)

**File:** `src/vasio_driver_correct.cpp`

Contains a complete IASIO interface, `bufferSwitch` callbacks, double buffering, DLL exports, and COM registration.

**Build as DLL:**

```bash
cl.exe /LD vasio_driver_correct.cpp /I asiosdk/common /I asiosdk/host
# Produces: vasio_driver_correct.dll
```

### Phase 3: Registration (1 hour)

**Create four DLLs:** VASIO1.dll, VASIO2.dll, VASIO3.dll, and VASIO4.dll.

**Register (administrator PowerShell):**

```powershell
# Copy DLLs
Copy-Item VASIO1.dll "C:\Program Files\Common Files\Steinberg\ASIO\"

# Register COM (through the registration script)
regsvr32 "C:\Program Files\Common Files\Steinberg\ASIO\VASIO1.dll"

# Verify
Get-ItemProperty "HKLM:\SOFTWARE\Steinberg\ASIO" | Select-Object PS*
```

### Phase 4: Testing (2 hours)

- [ ] VASIO1–4 appear in Reaper/Mixxx.
- [ ] Selecting VASIO1 produces no error.
- [ ] Audio passes (silence is acceptable initially).
- [ ] No crashes.
- [ ] Acceptable latency (< 50 ms).

## Implementation resources

### Steinberg documentation

```text
asiosdk/ASIO_SDK.txt          - Full specification
asiosdk/host/asio.h           - IASIO interface
asiosdk/host/asiodrivers.h    - Helper functions
```

### Reference examples

```text
asiosdk/host/getsamplerate.cpp      - Reading sample rate
asiosdk/host/createbuffers.cpp      - Buffer management
asiosdk/host/hostsample.cpp         - Complete host

External:
https://github.com/sadko4u/lsp-plugins/asio/
  → Actual ASIO implementation for Linux/Wine
```

### Key patterns

**1. Very fast callbacks**

```cpp
void bufferSwitch(long index, ASIOBool processNow) {
    // MUST take < 1 ms!!!
    // No memory allocation, stdio, or locks
    for (int i = 0; i < numChannels; i++) {
        processChannel(buffers[i][index], bufferSize);
    }
    ASIOOutputReady();
}
```

**2. Kernel synchronization**

```cpp
// The audio kernel calls bufferSwitch.
// Our code must return immediately; otherwise clicks or crashes may occur.
```

**3. Double buffering**

```cpp
// Index alternates: 0 → 1 → 0 → 1 ...
// While we fill buffer[1], the kernel reads buffer[0].
// This prevents overwrite clicks.
```

## PortAudio versus direct IASIO (historical comparison)

| Aspect | PortAudio | Direct IASIO |
|---|---|---|
| Complexity | Low (3–5 files) | High (40+ methods) |
| Latency | 10–50 ms | 1–5 ms |
| Control | Limited | Complete |
| Features | Basic | Advanced |
| Windows recognition | ❌ Not guaranteed | ✓ 100% |
| Required DLLs | 1 | 4 (VASIO1–4) |
| DAW support | Moderate | Excellent |

## Recommended decision at the time

**For production:** implement IASIO properly. Reason: guaranteed compatibility. Estimated cost: 6–8 development hours. Benefit: professional drivers.

**For a quick test:** use PortAudio. Reason: works quickly. Estimated cost: 2–3 development hours. Limitation: not recognized by every DAW.

## Concrete steps from the historical plan

### 1. Build the corrected code

```bash
cd asio

# With Visual Studio
cl.exe /LD src/vasio_driver_correct.cpp ^
  /I asiosdk/common ^
  /I asiosdk/host ^
  /Fe:VASIO1.dll

# Or with CMake
cmake -B build -G "Visual Studio 17 2022" -DASIO_SDK_PATH=asiosdk/
cmake --build build --config Release
```

### 2. Create four DLLs

Change this for each driver:

```cpp
#define DRIVER_ID 1  // or 2, 3, 4
// Build four times → VASIO1.dll, VASIO2.dll, etc.
```

### 3. Register in Windows

```powershell
# Administrator PowerShell
$dllPath = "C:\...\VASIO1.dll"
[System.Runtime.InteropServices.RuntimeEnvironment]::SystemVersion

# Register
regsvr32 $dllPath

# Verify
reg query HKLM\SOFTWARE\Steinberg\ASIO
```

### 4. Test in a DAW

1. Launch Reaper.
2. Open Options → Preferences → Audio Device.
3. Look for VASIO1–4.
4. Select VASIO1.
5. Audio input/output should work.

### 5. Troubleshoot

**“Cannot find VASIO1”:** DLL not registered; check the registry and restart the DAW.

**“VASIO1 not initialized”:** `Init()` was not called; check `ASIOInit()` in callbacks.

**“Audio crackles”:** incorrect buffer size or callbacks too slow; reduce processing.

## Timeline estimated at the time

```text
Phase 1 (test):       1 hour      ← State at the time
Phase 2 (IASIO):      4–6 hours
Phase 3 (register):   1 hour
Phase 4 (DAW test):   2 hours
─────────────────────
Total:                8–10 hours
```

## Next actions recorded at the time

1. ✅ Audit completed (`ASIO_AUDIT.md`).
2. ✅ Correct code written (`vasio_driver_correct.cpp`).
3. ⏭️ Build and test (outstanding then).
4. ⏭️ Fix issues if needed.
5. ⏭️ Integrate with the Electron GUI.

## Support

If blocked: consult `asiosdk/ASIO_SDK.txt`, check the Windows registry, test with Reaper (strong ASIO support), and read `asiosdk/host/hostsample.cpp` as a reference.
