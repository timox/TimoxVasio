# TimoxVasio quick start

[Français](QUICKSTART.md) | **English**

The project provides one virtual ASIO driver, `TimoxVasio.dll`, and a separate audio engine, `TimoxVirtualAsioEngine.exe`. The driver advertises up to 256 inputs and 256 outputs. The API reports only the channels actually allocated by each client.

## Build

From a Visual Studio 2026 x64 Developer Command Prompt at the repository root:

```powershell
cmake -S . -B build_driver_110 -G "Visual Studio 18 2026" -A x64 `
  -DASIO_SDK_PATH="$PWD/asiosdk" `
  -DVASIO_BUILD_DRIVERS=ON
cmake --build build_driver_110 --config Release --target TimoxVasio DriverProbe DriverAudioProbe

cmake -S . -B build_codex_110 -G "Visual Studio 18 2026" -A x64
cmake --build build_codex_110 --config Release --target TimoxVirtualAsioEngine
```

The outputs are `build_driver_110/Release/TimoxVasio.dll` and `build_codex_110/Release/TimoxVirtualAsioEngine.exe`.

## Install and inspect the driver

In an administrator terminal:

```powershell
.\install\build_and_install.bat
pwsh -NoProfile -File .\install\register_drivers.ps1 list
```

The registered ASIO name should be `TimoxVasio`. To probe COM without starting audio:

```powershell
.\build_driver_110\Release\DriverProbe.exe `
  --dll .\build_driver_110\Release\TimoxVasio.dll `
  --clsid "{A4D39126-78CB-4D89-9E0A-54494D4F5856}"
```

See [Installation](INSTALL.en.md) and [BUILD_DRIVERS.en.md](BUILD_DRIVERS.en.md).

## Start and route

Run the engine on its own to start the local service, or launch Timox VASIO Control, which starts it. In Control, select a physical ASIO driver, apply its accepted sample rate and buffer size, and create routes from endpoints returned by the API. Validate the full path with a real ASIO host and an observed signal on the hardware. See [TEST_VERIFICATION.en.md](TEST_VERIFICATION.en.md) and the [API quick start](API_QUICKSTART.en.md).
