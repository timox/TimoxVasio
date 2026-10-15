# Verifying TimoxVasio and the engine

[Français](TEST_VERIFICATION.md) | **English**

The maintainer confirmed that end-to-end ASIO host and hardware tests were performed in their environment. This document provides reproducible commands for verifying the code and API contracts; it does not present those tests as outstanding work.

## x64 build

In an x64 Visual Studio 2026 Developer Command Prompt:

```powershell
cmake --build build_driver_110 --config Release --target TimoxVasio DriverProbe DriverAudioProbe DriverCompatibilityProfileTests
cmake --build build_codex_110 --config Release --target TimoxVirtualAsioEngine AudioTransportProbe AudioRoutingRuntimeTests RoutingGraphTests ApplicationProfilesApiTests
```

## COM driver and transport

Verify the direct COM identity and 256/256 capability:

```powershell
.\build_driver_110\Release\DriverProbe.exe `
  --dll .\build_driver_110\Release\TimoxVasio.dll `
  --clsid "{A4D39126-78CB-4D89-9E0A-54494D4F5856}"
```

The engine probes cover interprocess transport and the graph, including high virtual channels and sparse physical indices:

```powershell
.\build_codex_110\Release\AudioTransportProbe.exe
.\build_codex_110\Release\AudioRoutingRuntimeTests.exe
.\build_codex_110\Release\RoutingGraphTests.exe
```

## ASIO registry

The registered-driver probe calls the Steinberg enumerator and checks the driver's identity and channels. It does not start an audio stream:

```powershell
.\build_driver_110\Release\DriverProbe.exe --registered TimoxVasio
```

`tests/driver-registration.Tests.ps1` checks idempotent migration in a temporary HKCU root. `tests/registered-driver-smoke.ps1` reads the existing HKLM installation and activates it without changing its registration.

## API contract and interface

```powershell
pwsh -NoProfile -File .\tests\api-contract.ps1
cd gui
npm run contract-test
$env:CI='true'
npm run react-test -- --watchAll=false --runInBand
node --check electron/main.js
```

## ASIO host and hardware workflow

The engine must start before the host: `TimoxVasio::init()` waits for the engine to attach. In Mixxx, open **Preferences > Sound Hardware**, choose **ASIO** as the *Sound API*, then select `TimoxVasio` as the output device. The ASIO API is separate from the device name. After selection/allocation, the engine API publishes the process and its active endpoints. The maintainer confirms having completed the workflow through the hardware in their environment.

For the external ASIO probe, start the new `TimoxVirtualAsioEngine.exe`, then run `VasioExternalHostProbe.exe --driver TimoxVasio`. The probe initializes the client and tests `createBuffers`, `start`, and stop/reopen cycles. Do not run it against an already active engine with an unknown identity or session.

To reproduce the workflow, select a physical driver in Timox VASIO Control, apply the confirmed sample rate and buffer size, create the routes, then observe the physical channels in use. Synthetic probes remain useful for isolating software contracts from hardware tests.
