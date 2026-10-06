# TimoxVasio quick start

[Français](QUICKSTART.md) | **English**

The project provides one virtual ASIO driver, `TimoxVasio.dll`, and a separate audio engine, `TimoxVirtualAsioEngine.exe`. The driver advertises up to 256 inputs and 256 outputs. The API reports only the channels actually allocated by each client.

## Build

From a Visual Studio 2026 x64 Developer Command Prompt at the repository root:

```powershell
cmake -S . -B build_drivers_vs2026_ninja -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DASIO_SDK_PATH="$PWD/asiosdk" `
  -DVASIO_BUILD_DRIVERS=ON
cmake --build build_drivers_vs2026_ninja --target TimoxVasio DriverProbe DriverAudioProbe

cmake -S . -B build_engine_vs2026_ninja -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build_engine_vs2026_ninja --target TimoxVirtualAsioEngine
```

The outputs are `build_drivers_vs2026_ninja/TimoxVasio.dll` and `build_engine_vs2026_ninja/TimoxVirtualAsioEngine.exe`.

## Install and inspect the driver

In an administrator terminal:

```powershell
.\build_and_install.bat
pwsh -NoProfile -File .\register_drivers.ps1 list
```

The registered ASIO name should be `TimoxVasio`. To probe COM without starting audio:

```powershell
.\build_drivers_vs2026_ninja\DriverProbe.exe `
  --dll .\build_drivers_vs2026_ninja\TimoxVasio.dll `
  --clsid "{A4D39126-78CB-4D89-9E0A-54494D4F5856}"
```

See [Installation](INSTALL.en.md) and [BUILD_DRIVERS.md](BUILD_DRIVERS.md).

## Start and route

Run the engine on its own to start the local service, or launch Timox VASIO Control, which starts it. In Control, select a physical ASIO driver, apply its accepted sample rate and buffer size, and create routes from endpoints returned by the API. Validate the full path with a real ASIO host and an observed signal on the hardware. See [TEST_VERIFICATION.md](TEST_VERIFICATION.md) and the [API quick start](docs/API_QUICKSTART.en.md).
