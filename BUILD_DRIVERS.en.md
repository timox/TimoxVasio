# Building and verifying TimoxVasio

[Français](BUILD_DRIVERS.md) | **English**

## Verified environment

- Visual Studio 2026 from its Developer Command Prompt;
- MSVC x64 19.51.36260.0;
- Windows SDK 10.0.26100.0;
- CMake 4.4.3;
- ASIO SDK present in `asiosdk`.

Run the commands from a Developer Command Prompt for VS 2026 configured for x64.

## Build the DLL

From the `TimoxVasio` repository root:

```powershell
cmake -S . -B build_driver_110 `
  -G "Visual Studio 18 2026" -A x64 `
  -DASIO_SDK_PATH="$PWD/asiosdk" `
  -DVASIO_BUILD_DRIVERS=ON
cmake --build build_driver_110 --config Release --target TimoxVasio DriverProbe DriverAudioProbe
```

The resulting DLL is `build_driver_110/Release/TimoxVasio.dll`. `VASIO_BUILD_DRIVERS` remains the historical CMake build switch; the produced and registered driver is called `TimoxVasio`.

## COM verification

The probe explicitly calls `ASIOInit` and verifies COM activation, 256 inputs and outputs, and that `canSampleRate` advertises only the attached physical clock's sample rate:

```powershell
.\build_driver_110\Release\DriverProbe.exe `
  --dll .\build_driver_110\Release\TimoxVasio.dll `
  --clsid "{A4D39126-78CB-4D89-9E0A-54494D4F5856}"
```

The audio probe also checks low, high, and sparse channel indices. Results of the audio test completed end to end on this machine are recorded under “Validation status” below.

## Verify Steinberg registration

`AsioDriverList` reads registered ASIO drivers and activates the COM classes. The migration test in `tests/driver-registration.Tests.ps1` uses a temporary HKCU key and checks that unrelated ASIO entries are preserved. `tests/registered-driver-smoke.ps1` reads the HKLM system registration without changing it and verifies the versioned path and COM activation.

For installation on this machine, see [INSTALL.en.md](INSTALL.en.md). To list or uninstall the product entry:

```powershell
pwsh -NoProfile -File .\register_drivers.ps1 list
pwsh -NoProfile -File .\register_drivers.ps1 uninstall
```

## Validation status

The build, direct COM activation, transport/routing probes, and API/UI contracts were verified. After reinstalling version 1.0.0, the user confirmed the end-to-end audio test on October 5, 2026: Renoise, eight routes, SSL ASIO Driver 1, 48 kHz/1024 frames; the API published 60 `audio.meter` events in 3.5 seconds with peaks from `-101.65` to `-26.43 dBFS`. Diagnostics and `engine.log` confirmed startup and configuration.
