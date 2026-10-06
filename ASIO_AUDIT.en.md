# ASIO audit — historical working notes

[Français](ASIO_AUDIT.md) | **English**

> **Warning: this file contains findings from different dates and states.** Older sections below do not all describe the current driver. In particular, claims that no `IASIO` interface, COM DLL, or callbacks exist are obsolete. For the current summary, see [README.en.md](README.en.md); for the active code assessment and evidence, see [ASIO_CONFORMITE.en.md](ASIO_CONFORMITE.en.md). Do not treat old recommendations to replace the driver with PortAudio as a description of the current contract.

## Current summary

The active driver is `TimoxVasio.dll`. It implements `IASIO`, advertises 256 channels in each direction, and processes channels actually allocated by the host. Local probes verify capacity, sparse allocations, and callback transport. After reinstalling version 1.0.0, the user confirmed an end-to-end test on October 5, 2026. Renoise had 64 inputs and 64 outputs, eight routes were active, and the engine used SSL ASIO Driver 1 at 48 kHz/1024 frames. A 3.5-second observation received 60 `audio.meter` events on routed physical outputs and source virtual outputs, with peaks between `-101.65` and `-26.43 dBFS`; the user confirmed audible sound. Observed compliance is documented in [ASIO_CONFORMITE.en.md](ASIO_CONFORMITE.en.md).

The `/api/v1/diagnostics` API and `%LOCALAPPDATA%/TimoxVasio/logs/engine.log` confirmed engine startup, physical configuration at 48 kHz/1024 frames, and application of the eight routes. The user verified Swagger in the installed Electron application.

---

## Audit notes and history

The following findings are retained for context and may predate the current driver and engine. They are not current status.

### State on October 4, 2026 — historical observation superseded by the October 5 test

## State at that time

The driver and engine had progressed beyond the earlier audit preserved below.

- `TimoxVasio.dll` was an x64 ASIO driver registered at `HKLM\SOFTWARE\ASIO\TimoxVasio`. After its October 4 replacement, the registry pointed to `C:\Program Files\Steinberg\VirtualASIO\TimoxVasio.dll`; its SHA-256 matched `build_drivers_vs2026_ninja\TimoxVasio.dll` (`7FA67EE0DF55ADA8F28A68B81159A44B2003BAA77AA7A1CBD36894313D5307EE`). The installed Mixxx was x64 version `2.6.0-beta` (commit `2.6-beta-402-ge1c1e5b72b`).
- After installing that DLL and restarting Mixxx, an Audio Preferences screenshot showed `TimoxVasio` available for main, headphone, booth, bus, and deck outputs, with channel pairs 1–20. The engine API confirmed `mixxx.exe` with 20 open outputs and no inputs. Discovery and opening of the driver were therefore verified in the installed Mixxx.
- A probe loading exactly `C:\Program Files\Mixxx\portaudio.dll` displayed the PortAudio version, then stayed in `Pa_Initialize()` without reaching `Pa_GetDeviceCount()`. That result cannot establish that Mixxx hides the driver.
- The API observation confirmed Renoise with 64 open inputs/outputs and Mixxx with 0 inputs and 20 open outputs. The profile actually served by the API was `mixxx.exe` 255/255; a 12/12 setting was not present in current state. This is an advertised ceiling, distinct from the 20 outputs Mixxx actually allocated.
- At that date, the active engine reported `stopped`, 48 kHz and 1024 frames, with no configured route. This preceded the successful end-to-end test confirmed on October 5 and does not describe its outcome.
- The physical ASIO driver callback now signaled rate changes to the engine worker. The worker checked the signal within its 20 ms wait, stopped the engine, and invalidated physical capabilities until configuration was reapplied. `TimoxVirtualAsioEngine` built in `build_engine_names_check`; the active process was not restarted.
- The React GUI built for production (`npm run react-build`). Changes locked settings and routes during `reconfiguring`; this build did not validate runtime behavior.
- The active engine API reported the SSL 12 with 16 inputs and 8 outputs: inputs had explicit names, but outputs 3–8 were still `Out 3` through `Out 8`. Resolution of those six generic labels was added for the SSL 12 signature and exact name `SSL ASIO Driver 1`, preserving non-generic names from the driver. Published names would be `Line 3`, `Line 4`, `Headphone A L/R`, and `Headphone B L/R`, roles documented in the [SSL 12 guide](https://support.solidstatelogic.com/hc/en-gb/articles/5568765809309-SSL-12-User-Guide). The updated build passed in `build_engine_names_check`; the active `build_engine_vs2026_ninja` engine was not restarted, so its API still published old labels.
- An older WebSocket reading at `-120 dBFS` preceded the successful October 5 test. It is retained only in history and is neither a current result nor an outstanding criterion.

### Mixxx compatibility (October 4, 2026)

- The code diagnosis remains valid: in that revision, `ChannelCount::value_t` is `uint8_t`, so 256 cannot be represented. For the installed stable Mixxx, TimoxVasio's 255/255 application profile works around this limit without reducing general driver capacity, which retains 256 slots and defaults to 256 channels for unprofiled applications.
- PortAudio code included in this repository loads ASIO drivers during initialization and gathers their capabilities; the generic probe sees TimoxVasio at 256/256. The exact Mixxx DLL probe hung in `Pa_Initialize()` and did not independently verify that installation.
- An earlier test with an obsolete installed DLL could not validate the profile. The built DLL was subsequently installed and the API confirmed Mixxx opening 20 outputs; no patch was applied to Mixxx source.

### Additional coverage

The requested end-to-end test succeeded. Further measurements, hosts, and transitions can extend those results; they do not qualify the completed test.

## Audit archive predating the current driver

> Historical audit note predating `TimoxVasio.dll`. Implementation examples and references to four old drivers are not the current contract. See [BUILD_DRIVERS.en.md](BUILD_DRIVERS.en.md) and the [current specification](docs/superpowers/specs/2026-10-03-vasio-single-driver-256-channel-design.en.md).

## Steinberg ASIO SDK documentation

Official source: https://github.com/steinbergmedia/asiosdk

### Minimum required structure

```cpp
// asio.h - Main interface
struct IASIO {
    // Initialization
    ASIOBool Init(void* sysHandle);
    void getDriverName(char* name);
    long getDriverVersion();

    // Capabilities
    ASIOError getChannels(long* numInputChannels, long* numOutputChannels);
    ASIOError getBufferSize(long* minSize, long* maxSize,
                           long* preferredSize, long* granularity);
    ASIOError canSampleRate(ASIOSampleRate sampleRate);
    ASIOError getSampleRate(ASIOSampleRate* sampleRate);
    ASIOError setSampleRate(ASIOSampleRate sampleRate);

    // Configuration
    ASIOError createBuffers(ASIOBufferInfo* bufferInfos, long numChannels,
                            long bufferSize, ASIOCallbacks* callbacks);
    ASIOError disposeBuffers();

    // Control
    ASIOError start();
    ASIOError stop();
    ASIOError getLatencies(long* inputLatency, long* outputLatency);

    // I/O
    ASIOError outputReady();

    // Advanced control
    ASIOError controlPanel();
    ASIOError future(long selector, void* opt);
    ASIOError dispose();
};
```

## Problems identified in the implementation at that time

### 1. ❌ No IASIO interface implemented

**Problem:**

```cpp
// At the time: simplified PortAudio wrapper
class VirtualDriver {
    // Does not inherit IASIO
    // Lacks required ASIO methods
};
```

**Proposed correction (ASIO SDK):**

```cpp
class VirtualASIODriver : public IASIO {
    ASIOError Init(void* sysHandle) override;
    ASIOError getChannels(long* in, long* out) override;
    ASIOError createBuffers(...) override;
    // ~40 required methods
};
```

### 2. ❌ No COM/DLL registration

**Problem:** The audit stated that the ASIO SDK required a COM-registered DLL for Windows to recognize the driver.

**Proposed correction:**

```cpp
// VASIO1.cpp - DLL export
extern "C" EXPORT ASIODriver* (*asioCreateDriver)(void);

ASIODriver* asioCreateDriver(void) {
    return new VirtualASIODriver("VASIO1");
}

// Registry registration (CLSID)
HKEY_LOCAL_MACHINE\SOFTWARE\Steinberg\ASIO\VASIO1
  → InprocServer32 = VASIO1.dll
  → CLSID = {UNIQUE-GUID}
```

### 3. ❌ No real-time audio callbacks

**Problem:**

```cpp
// No callbacks at the time
ASIOError ProcessBuffer(float* input, float* output, long numSamples) {
    std::memcpy(output, input, ...);  // Described as too slow
}
```

**Proposed correction:**

```cpp
// Callback called by the audio kernel
void bufferSwitch(long doubleBufferIndex, ASIOBool directProcess) {
    // Process audio buffers; must be fast (< 1 ms)
    processAudio(doubleBufferIndex);
    // Signal that output is ready
    ASIOOutputReady();
}
```

### 4. ❌ No circular-buffer handling

**Problem:** No double (ping-pong) buffering to prevent clicks.

**Proposed correction:**

```cpp
struct BufferInfo {
    void* buffers[2];    // Double buffer
    long bufferSize;
    long currentIndex;   // 0 or 1
};
```

### 5. ❌ Registry registration too simplified

**Problem:**

```cpp
// At the time
RegSetValueExA(hKey, "VASIO1", 0, REG_SZ, "VirtualASIODriver", 18);
```

**Proposed correction:**

```text
HKEY_LOCAL_MACHINE\SOFTWARE\Steinberg\ASIO\VASIO1
  ├─ CLSID = {12345678-1234-1234-1234-123456789ABC}
  └─ [CLSID]
      └─ InprocServer32 = C:\path\to\VASIO1.dll
```

### 6. ❌ No audio format handling

**Problem:** Hard-coded `ASIOSTFloat32LSB`.

**Proposed correction:**

```cpp
ASIOError getChannelInfo(ASIOChannelInfo* info) {
    info->type = ASIOSTFloat32LSB;  // Or Int24LSB, etc.
    info->isActive = ASIOTrue;
    strcpy(info->name, "VASIO1:1");
    return ASE_OK;
}
```

---

## Comparison with Steinberg examples

### Example 1: ASIO SDK host example

Source: `asiosdk/host/getsamplerate.cpp`

```cpp
// Pattern shown in the audit:
ASIOError ret = ASIOInit(&asioVersion);
if (ret == ASE_NotPresent) {
    // ASIO not installed
}

// Enumerate drivers
for (int i = 0; i < asioDrivers->getNumberOfDrivers(); i++) {
    char driverName[32];
    asioDrivers->getDriverName(i, driverName);
}

// Create instance
ASIODriver* driver = asioDrivers->getCurrentDriver();
```

**Implementation at the time:** ❌ Did not use `ASIOInit()` or `asioDrivers`.

### Example 2: ASIO SDK tutorial

Pattern shown in the audit:

```cpp
// 1. Initialize
ASIOCallbacks callbacks = {
    .bufferSwitch = bufferSwitchCallback,
    .sampleRateDidChange = sampleRateCallback,
    .asioMessage = messageCallback,
    .bufferSwitchTimeInfo = bufferSwitchTimeInfoCallback
};

// 2. Create buffers
ASIOBufferInfo bufferInfos[numChannels];
for (int i = 0; i < numChannels; i++) {
    bufferInfos[i].isInput = ASIOFalse;
    bufferInfos[i].channelNum = i;
    bufferInfos[i].buffers[0] = malloc(bufferSize);
    bufferInfos[i].buffers[1] = malloc(bufferSize);
}

driver->createBuffers(bufferInfos, numChannels, bufferSize, &callbacks);

// 3. Start
driver->start();

// 4. Real-time callback
void bufferSwitchCallback(long index, ASIOBool processNow) {
    // ⚠️ Must return in < 1 ms
    for (int i = 0; i < numChannels; i++) {
        float* buffer = (float*)bufferInfos[i].buffers[index];
        processAudio(buffer, bufferSize);
    }
    driver->outputReady();
}
```

**Implementation at the time:** ❌ No callbacks or `bufferSwitch`.

---

## Gap summary from the historical audit

| Item | Steinberg | Code at the time | Status at the time |
|---|---|---|---|
| IASIO interface | ✓ Required | ❌ Missing | **CRITICAL** |
| `bufferSwitch` callbacks | ✓ Required | ❌ Missing | **CRITICAL** |
| Double buffering | ✓ Required | ❌ Absent | **CRITICAL** |
| COM/DLL registration | ✓ Required | ❌ Simplified | **CRITICAL** |
| ASIO methods (~40) | ✓ Required | ❌ ~15 only | **CRITICAL** |
| Kernel synchronization | ✓ Required | ❌ Absent | **SERIOUS** |
| Message callbacks | ✓ Recommended | ❌ Absent | **Moderate** |
| Control panel | ✓ Recommended | ❌ Absent | **Moderate** |

---

## Approaches considered at the time

### Option A: Use the ASIO SDK properly

```cpp
// VASIO1Driver.cpp - Complete IASIO implementation
#include "asio.h"
#include "asiodrivers.h"

class VASIO1Driver : public IASIO {
    // ~40 methods to implement
    // Circular buffers
    // Real-time callbacks
    // COM registration
};

// Export from DLL
extern "C" {
    EXPORT ASIODriver* (*asioCreateDriver)(void) = createVASIO1Driver;
}
```

**Advantages:** real ASIO driver and full features. **Drawbacks:** complexity (~2,000 lines) and low-level handling.

### Option B: Continue with PortAudio (simplified)

```cpp
// Keep PortAudio as backend
// Use PortAudio's official ASIO wrapper
// Register through an ASIO helper
```

**Advantages:** simple and quick to get working. **Drawbacks:** not a real ASIO driver and higher latency.

### Option C: Use VirtualAudio (recommended in the old audit)

Use a dedicated virtual-driver library or product: VirtualAudio.exe (VB-Audio), LoopMIDI (MIDI, similar pattern), or VoiceMeeter (partial source available).

**Advantages:** proven and professional. **Drawback:** less opportunity to learn.

---

## Decision recommended in the old audit

### For a “proper” implementation

The old audit recommended a hybrid approach:

1. **C++ backend:** PortAudio (simple and functional).
2. **Electron/React frontend:** already completed with a good design.
3. **ASIO driver:** PortAudio's official ASIO wrapper.

The stated reasons were that PortAudio already had an ASIO backend, avoided 40 IASIO methods and DLL/COM complexity, and remained ASIO compatible. These statements belong to the historical audit, not the current product contract.

---

## Checks proposed at the time

### 1. Is the ASIO SDK necessary?

The old answer was “yes, but PortAudio already integrates it”: PortAudio → ASIO SDK internally; project code → PortAudio wrapper.

### 2. Will virtual drivers appear in DAWs?

The old answer was “problematic.” Its checklist called for proper COM registration, a unique CLSID per driver, a signed DLL (or disabling SmartScreen), and restarting Windows.

### 3. Is latency acceptable?

The old estimates were PortAudio: 10–20 ms (acceptable); native ASIO: 1–5 ms (better). These were estimates in the historical audit, not measurements of the current product.

---

## Historical correction plan

### Phase 1: Quick verification

- [ ] Check whether VASIO1–4 appear in Reaper.
- [ ] Check the Windows registry.
- [ ] Test with simple audio.

### Phase 2: Use the ASIO SDK properly

- [ ] Read all of `asiosdk/host/asiodrivers.cpp`.
- [ ] Implement the 40 IASIO methods.
- [ ] Handle `bufferSwitch` callbacks.
- [ ] Implement proper double buffering.

### Phase 3: Packaging

- [ ] Sign DLL (optional).
- [ ] Create installer script.
- [ ] Test multiple DAWs.

---

## Official resources

**ASIO documentation:**

- https://github.com/steinbergmedia/asiosdk
- `asiosdk/ASIO_SDK.txt` — full specification.
- `asiosdk/host/asio.h` — main interface.
- `asiosdk/host/asiodrivers.cpp` — example implementation.

**Examples:**

- `asiosdk/host/getsamplerate.cpp` — reading sample rate.
- `asiosdk/host/createbuffers.cpp` — buffer management.
- `asiosdk/host/hostsample.cpp` — complete host.

**Community:** ASIO SDK issues at https://github.com/steinbergmedia/asiosdk/issues, Reaper ASIO testing, and the Mixxx ASIO implementation.

---

## Historical conclusion

The audit then described a good Electron/React interface and healthy C++ communication, but said the ASIO SDK contract was not met and Windows would not recognize the drivers. It recommended using PortAudio officially with its ASIO backend instead of implementing IASIO from scratch. Its proposed next step was checking whether drivers appeared in a real DAW and, if not, implementing ASIO callbacks correctly. These findings have been superseded by the current summary above.
