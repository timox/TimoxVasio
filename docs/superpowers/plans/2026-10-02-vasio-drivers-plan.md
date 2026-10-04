# VASIO Windows Drivers Implementation Plan

> **Archive historique — ne pas exécuter.** Ce plan décrit quatre pilotes à six canaux. Le contrat courant est l’unique pilote `TimoxVasio.dll` à 256 entrées/sorties; voir [le plan maître](2026-10-03-vasio-256-physical-master-plan.md) et [la spécification](../specs/2026-10-03-vasio-single-driver-256-channel-design.md).

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and register four discoverable Windows x64 ASIO COM drivers, each exposing six inputs and six outputs through the Steinberg ASIO contract.

**Architecture:** Follow the provided Steinberg Windows sample: `IASIO` plus `CUnknown`, one `CFactoryTemplate` per DLL, SDK COM entry points and a stable CLSID per driver. Each DLL is a thin client endpoint; shared audio transport is implemented in the engine plan, so an absent engine must return an explicit connection failure rather than synthetic audio.

**Tech Stack:** C++17, Steinberg ASIO SDK included in `asiosdk`, CMake, MSVC/Visual Studio 2026 x64, PowerShell registry installer.

**Spec:** `docs/superpowers/specs/2026-10-02-virtual-asio-routing-design.md`

## Global Constraints

- Target Windows x64 and Visual Studio 2026 (`Visual Studio 18 2026`).
- Expose exactly VASIO1 through VASIO4, with six input and six output channels per driver.
- Applications must select VASIO explicitly; do not capture Windows shared-mode audio.
- Every VASIO DLL has a unique stable CLSID and is discoverable through `HKLM\SOFTWARE\ASIO` plus COM `InprocServer32` registration.
- Never produce fake silence as evidence of a connected route; unavailable engine state is an ASIO error.
- No allocations, disk access or blocking locks in ASIO callbacks.

## Review Focus

- Driver enumerated from the ASIO registry but COM class cannot be instantiated: probe every CLSID via the SDK enumerator.
- Client opens a DLL before the engine: `init()` reports unavailable and does not start a local timer.
- Repeated install, upgrade or uninstall: only VASIO registry keys and CLSIDs change, and repeated operations converge to the same registry state.
- Host requests an invalid channel index or unsupported buffer size: return the specified ASIO error without memory access outside allocated buffers.
- DLL unload while COM objects remain active: SDK object count prevents unsafe unload; probe create/release cycles.

---

### Task 1: Replace the hand-written exports with the SDK COM class factory

**Files:**
- Modify: `CMakeLists_VASIO_Drivers.txt`
- Replace: `src/vasio_driver.cpp`
- Create: `src/vasio_com_driver.h`
- Create: `src/vasio_com_driver.cpp`
- Create: `src/vasio_driver_factory.cpp`
- Create: `src/vasio_driver.def`
- Create: `tests/driver_probe.cpp`

**Interfaces:**
- Consumes: Steinberg SDK declarations from `asiosdk/common/iasiodrv.h`, `combase.h`, `asio.h`.
- Produces: `VASIODriver final : IASIO, CUnknown`, `VASIODriver::CreateInstance(LPUNKNOWN,HRESULT*)`, and `VASIODriver::NonDelegatingQueryInterface(REFIID,void**)`.

- [x] **Step 1: Add a COM probe executable that checks the public ASIO methods**

Create two test modes. Support repeated `--dll <path> --clsid <guid>` pairs and repeated `--registered <name>` arguments. The direct mode loads each module with `LoadLibraryW`, calls its exported `DllGetClassObject`, asks the class factory for `IID_ASIO_DRIVER`, then asserts `getDriverName`, `getChannels`, and `getBufferSize`. The registered mode calls `CoInitializeEx`, uses `AsioDriverList`, opens the name and asserts the same methods. Return a nonzero exit code for a missing export/name, failed COM creation, wrong channel count, invalid buffer contract or failed `Release`/`CoUninitialize` path.

- [x] **Step 2: Run the probe before replacing the DLL implementation**

Run: `cmake --build build_2026_check --config Release --target DriverProbe; .\build_2026_check\Release\DriverProbe.exe --dll .\build_2026_check\Release\VASIO1.dll --clsid "{7A9F4D01-4D7D-4D54-9A61-564153494F31}"`

Expected: FAIL because the current registry entry is not discovered by the SDK `AsioDriverList` and the current DLL does not expose the sample COM factory.

- [x] **Step 3: Implement `VASIODriver` using the SDK sample COM pattern**

Implement every `IASIO` method declared in `asiosdk/common/iasiodrv.h`. Expose six inputs and six outputs, names `VASIO<n> In <channel>` / `VASIO<n> Out <channel>`, float32 little-endian, and the advertised buffer/rate values. Validate all pointer and channel arguments. For `init`, connect to the engine transport; return `ASIOFalse` with a retrievable error message when it is unavailable. Do not create a timer or synthesize an input signal in this task.

Use this class/factory shape and delegate every `IUnknown` request through the SDK base class:

```cpp
class VASIODriver final : public IASIO, public CUnknown {
public:
    VASIODriver(LPUNKNOWN outer, HRESULT* result, int driverId);
    static CUnknown* CreateInstance(LPUNKNOWN outer, HRESULT* result);
    HRESULT STDMETHODCALLTYPE NonDelegatingQueryInterface(REFIID iid, void** out) override;
    ASIOBool init(void* systemHandle) override;
    ASIOError getChannels(long* inputs, long* outputs) override;
    ASIOError createBuffers(ASIOBufferInfo* infos, long count,
                            long frames, ASIOCallbacks* callbacks) override;
    // Implement the remaining IASIO methods declared in iasiodrv.h.
};
```

The factory file defines one `CFactoryTemplate` entry for the selected driver CLSID. SDK `dllentry.cpp` owns `DllGetClassObject` and `DllCanUnloadNow`; do not define duplicate exports.

- [x] **Step 4: Build and run the probe for one driver**

Run: `cmake -S . -B build_driver_check -G "Visual Studio 18 2026" -A x64 -DVASIO_BUILD_DRIVERS=ON -DASIO_SDK_PATH="$PWD/asiosdk"; cmake --build build_driver_check --config Release --target VASIO1 DriverProbe; .\build_driver_check\Release\DriverProbe.exe --dll .\build_driver_check\Release\VASIO1.dll --clsid "{7A9F4D01-4D7D-4D54-9A61-564153494F31}"`

Expected: the DLL links without duplicate `ASIO*`, `DllGetClassObject`, or `DllCanUnloadNow` symbols; the direct COM probe reports six input/six output channels and the valid buffer contract.

---

### Task 2: Produce four independently identified DLLs

**Files:**
- Modify: `CMakeLists_VASIO_Drivers.txt`
- Modify: `src/vasio_driver_factory.cpp`
- Modify: `tests/driver_probe.cpp`

**Interfaces:**
- Consumes: `VASIODriver` COM class from Task 1.
- Produces: targets `VASIO1.dll` through `VASIO4.dll`; CLSIDs `{7A9F4D01-4D7D-4D54-9A61-564153494F31}` through `{7A9F4D04-4D7D-4D54-9A61-564153494F34}` as currently reserved in `register_drivers.ps1`.

- [x] **Step 1: Expand the probe to assert distinct driver identities**

For each expected name VASIO1–VASIO4, invoke the DLL directly by path and CLSID, check the matching `getDriverName`, six input and output channels, and release the COM object. Assert all four returned CLSIDs differ.

- [x] **Step 2: Run the four-driver probe against the current one-driver build**

Run: `cmake --build build_driver_check --config Release --target DriverProbe; .\build_driver_check\Release\DriverProbe.exe --dll .\build_driver_check\Release\VASIO1.dll --clsid "{7A9F4D01-4D7D-4D54-9A61-564153494F31}" --dll .\build_driver_check\Release\VASIO2.dll --clsid "{7A9F4D02-4D7D-4D54-9A61-564153494F32}" --dll .\build_driver_check\Release\VASIO3.dll --clsid "{7A9F4D03-4D7D-4D54-9A61-564153494F33}" --dll .\build_driver_check\Release\VASIO4.dll --clsid "{7A9F4D04-4D7D-4D54-9A61-564153494F34}"

Expected: FAIL for VASIO2–VASIO4, identifying the missing generated targets and registrations.

- [x] **Step 3: Generate each DLL with its fixed driver number and CLSID**

Update the CMake target function so each library compiles the same implementation with one compile-time `DRIVER_ID`, publishes the matching CLSID in its own `CFactoryTemplate`, has its own module name, and links the SDK COM support sources `combase.cpp`, `dllentry.cpp`, and `register.cpp`. Export only the SDK sample entry points through the module definition file. Do not compile the generic host-side `asiosdk/common/asio.cpp` into each DLL.

Keep the existing `create_vasio_driver(DRIVER_ID DRIVER_NUMBER)` signature; `DRIVER_ID` is the fixed 1–4 identifier and determines its CLSID, while the function names its target from `DRIVER_NUMBER`. The calls must produce this one-to-one mapping:

```cmake
create_vasio_driver(1 VASIO1)
create_vasio_driver(2 VASIO2)
create_vasio_driver(3 VASIO3)
create_vasio_driver(4 VASIO4)
```

- [x] **Step 4: Build all DLLs and run the probe**

Run: `cmake --build build_driver_check --config Release; .\build_driver_check\Release\DriverProbe.exe`

Expected: all four DLLs exist in `build_driver_check\Release`; the probe opens each with a unique identity and the same six-in/six-out contract.

---

### Task 3: Make ASIO registration conform to the SDK enumerator

**Files:**
- Modify: `register_drivers.ps1`
- Modify: `build_and_install.bat`
- Create: `tests/driver-registration.Tests.ps1`

**Interfaces:**
- Consumes: the four DLLs and stable CLSIDs from Task 2.
- Produces: idempotent `install`, `uninstall`, `list`, and `clean` operations limited to VASIO registry entries.

- [x] **Step 1: Write PowerShell checks for registry layout and idempotence**

Add a test-root parameter used only by self-test, defaulting in tests to `HKCU:\Software\VASIO-Registration-Test`. Assert each ASIO key has `CLSID` and `Description`; assert each CLSID has `Classes\CLSID\{...}\InprocServer32` with the absolute DLL path and `ThreadingModel=Apartment`. Call install twice and assert one stable result; uninstall twice and assert only VASIO registry keys are removed. Use a standalone PowerShell harness; Pester is not installed on this machine.

- [x] **Step 2: Run the registration checks before changing the script**

Run: `pwsh -NoProfile -File tests/driver-registration.Tests.ps1`

Expected: FAIL because the current script uses `SOFTWARE\Steinberg\ASIO` and omits the `description` field consumed by `AsioDriverList`.

- [x] **Step 3: Align the PowerShell script with `asiosdk/common/register.cpp`**

Write the driver name, stable CLSID, description, absolute DLL path and apartment threading model to the same keys the SDK sample uses. Keep the operation restricted to VASIO1–VASIO4. Preserve unrelated ASIO/COM entries during `uninstall` and `clean`. Verify all four DLLs before starting any registry mutation.

The registry values must be equivalent to:

```text
HKLM\SOFTWARE\ASIO\VASIO1\CLSID = {7A9F4D01-4D7D-4D54-9A61-564153494F31}
HKLM\SOFTWARE\ASIO\VASIO1\Description = VASIO1
HKLM\SOFTWARE\Classes\CLSID\{7A9F4D01-4D7D-4D54-9A61-564153494F31}\InprocServer32\(Default) = <absolute VASIO1.dll path>
HKLM\SOFTWARE\Classes\CLSID\{7A9F4D01-4D7D-4D54-9A61-564153494F31}\InprocServer32\ThreadingModel = Apartment
```

- [x] **Step 4: Run registration checks and the real SDK probe**

Run: `pwsh -NoProfile -File tests/driver-registration.Tests.ps1; .\build_driver_check\Release\DriverProbe.exe --registered VASIO1 --registered VASIO2 --registered VASIO3 --registered VASIO4`

Expected: the self-test passes; the SDK enumerator discovers all four registered drivers and instantiates each COM object.

---

### Task 4: Verify install and removal on the target machine

**Files:**
- Modify: `BUILD_DRIVERS.md`
- Modify: `INSTALL.md`
- Modify: `README.md`

**Interfaces:**
- Consumes: installer/uninstaller from Task 3.
- Produces: one documented Windows 2026 install and verification procedure.

- [x] **Step 1: Record the exact build, install, enumerate and uninstall commands**

Document Visual Studio 2026 CMake configuration, DLL locations, administrator requirement, the list operation, the SDK probe command, and uninstallation. State that this task verifies ASIO discovery and capabilities but does not yet prove cross-process audio transport.

- [ ] **Step 2: Perform a clean administrator install and verify the registry**

Run `build_and_install.bat`, then `register_drivers.ps1 list` and `DriverProbe.exe`. Confirm four unique CLSIDs, four existing DLL paths, four created COM classes and four successful COM activations.

- [ ] **Step 3: Uninstall and verify absence without altering other drivers**

Run `register_drivers.ps1 uninstall`; rerun `DriverProbe.exe` and `register_drivers.ps1 list`. Expected: no VASIO entries, other ASIO entries unchanged, and a documented nonzero probe result for absent drivers.
