# Installing TimoxVasio

[Français](INSTALL.md) | **English**

This guide installs the `TimoxVasio.dll` virtual ASIO driver. The Electron setup installs Control and the engine, but **does not install the driver DLL**.

## Requirements

- Windows 10 or 11 x64.
- For a source build: Visual Studio 2026 C++ tools, CMake 3.16 or later, and the ASIO SDK in `asiosdk`.
- An administrator terminal to copy and register the DLL.

See [BUILD_DRIVERS.en.md](BUILD_DRIVERS.en.md) for source build instructions.

## Build and install from source

In an administrator terminal at the repository root:

```powershell
.\build_and_install.bat
```

The script builds `TimoxVasio` in `build_driver_110`, copies the DLL to `C:\Program Files\Steinberg\VirtualASIO\1.1.1`, and registers it. It removes known old VASIO1–VASIO4 registrations and attempts to delete their DLLs. A locked old DLL may remain on disk, but is no longer registered.

## Install the release download

Download and extract `TimoxVasio.Driver.1.1.1.zip` from the [1.1.1 release](https://github.com/timox/TimoxVasio/releases/tag/v1.1.1). Run `Installer TimoxVasio.bat` as administrator from the extracted directory. The installer uses a versioned directory so it can register the new driver even if an application still holds an older DLL open.

To perform the same operation manually, open an administrator PowerShell in the extracted directory:

```powershell
$driverDir = 'C:\Program Files\Steinberg\VirtualASIO\1.1.1'
New-Item -ItemType Directory -Force -Path $driverDir | Out-Null
Copy-Item -LiteralPath '.\TimoxVasio.dll' -Destination (Join-Path $driverDir 'TimoxVasio.dll') -Force
pwsh -NoProfile -File '.\register_drivers.ps1' install -DllDirectory $driverDir
```

This registers the ASIO name `TimoxVasio` and removes only known TimoxVasio and historical VASIO entries. Other installed ASIO drivers are unchanged.

## Verify and start

```powershell
pwsh -NoProfile -File .\register_drivers.ps1 list
Get-ChildItem 'C:\Program Files\Steinberg\VirtualASIO\1.1.1\TimoxVasio.dll'
pwsh -NoProfile -File .\tests\verify-installed-stack.ps1
```

Close and reopen audio applications after registering the driver. `TimoxVasio` advertises 256 inputs and 256 outputs; each application exposes only channels it allocated with `createBuffers`.

Start `TimoxVirtualAsioEngine.exe` or Timox VASIO Control. Choose a physical ASIO driver, apply a sample rate and buffer size that it accepts, then build routes in Control. Before opening Renoise or another ASIO host, ensure Control is connected and `http://127.0.0.1:52525/api/v1/state` responds. The engine must keep running even if Control is closed. Saved routes for absent applications remain in `configuredRoutes` and become active in `routes` when their channels open. Validate the complete signal path with your own hardware.

## Uninstall

In an administrator terminal at the repository root:

```powershell
pwsh -NoProfile -File .\register_drivers.ps1 uninstall
```
