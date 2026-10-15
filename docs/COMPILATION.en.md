# Building TimoxVasio

[Français](COMPILATION.md) | **English**

## Requirements

- Windows 10 or 11 x64;
- Visual Studio 2026 with the C++ workload;
- CMake;
- the ASIO SDK in `asiosdk`.

The Visual Studio GUI and `vasio.sln` are not required. Use a Developer Command Prompt for VS 2026 configured for x64.

## Configure and build

From the `TimoxVasio` repository root:

```powershell
cmake -S . -B build_driver_110 -G "Visual Studio 18 2026" -A x64 `
  -DASIO_SDK_PATH="$PWD/asiosdk" `
  -DVASIO_BUILD_DRIVERS=ON
cmake --build build_driver_110 --config Release --target TimoxVasio DriverProbe DriverAudioProbe
```

The output is `build_driver_110\Release\TimoxVasio.dll`. `VASIO_BUILD_DRIVERS` is the historical CMake option name; it enables the single `TimoxVasio` target.

The direct probe is documented in [BUILD_DRIVERS.en.md](BUILD_DRIVERS.en.md). To install the DLL in `Program Files` and register it, run `install\build_and_install.bat` from an administrator terminal.

## Build the engine

The normal configuration produces the engine executable:

```powershell
cmake -S . -B build_codex_110 -G "Visual Studio 18 2026" -A x64
cmake --build build_codex_110 --config Release --target TimoxVirtualAsioEngine
```

The CMake target and output file are named `TimoxVirtualAsioEngine`. Building the engine alone does not produce `TimoxVasio.dll`.
