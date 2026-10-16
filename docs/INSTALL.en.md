# Installing TimoxVasio

[Français](INSTALL.md) | **English**

TimoxVasio ships as two separate downloads: the ASIO driver `TimoxVasio.dll` (`TimoxVasio.Driver.<version>.zip`) and the Timox VASIO Control application, which bundles the `TimoxVirtualAsioEngine.exe` engine (`Timox.VASIO.Control.Setup.<version>.exe` or the portable build). The Setup does not install the driver; the driver archive does not install the application. The DLL and the engine must share the same version: the 1.2.2 shared transport rejects an older DLL.

Requirements: Windows 10 or 11 x64, 64-bit ASIO applications, administrator rights to register the driver and, to hear anything, a physical ASIO driver.

## Install from a release

1. Download the files from the [latest release](https://github.com/timox/TimoxVasio/releases/latest).
2. Extract `TimoxVasio.Driver.<version>.zip`, then run `Installer TimoxVasio.bat` **as administrator** from the extracted folder. The script copies the DLL to `C:\Program Files\Steinberg\VirtualASIO\<version>\` and registers it under the ASIO name `TimoxVasio`. The folder is versioned: an older driver still loaded by an application is not overwritten.
3. Install `Timox.VASIO.Control.Setup.<version>.exe`, or run `Timox.VASIO.Control.<version>.exe` (portable) directly.
4. Restart any audio application that had loaded an older DLL.

To register the DLL by hand from the extracted folder, in an administrator PowerShell:

```powershell
$driverDir = 'C:\Program Files\Steinberg\VirtualASIO\1.2.2'
New-Item -ItemType Directory -Force -Path $driverDir | Out-Null
Copy-Item -LiteralPath '.\TimoxVasio.dll' -Destination (Join-Path $driverDir 'TimoxVasio.dll') -Force
pwsh -NoProfile -File '.\register_drivers.ps1' install -DllDirectory $driverDir
```

Registration removes only old TimoxVasio and VASIO1–VASIO4 entries; other installed ASIO drivers are left untouched.

## Verify and start

From the extracted folder (or `install\` in the repository):

```powershell
pwsh -NoProfile -File .\register_drivers.ps1 list
Get-ChildItem "C:\Program Files\Steinberg\VirtualASIO\1.2.2\TimoxVasio.dll"
```

The expected driver is `TimoxVasio`. It advertises 256 inputs and 256 outputs; each application only exposes the channels it actually allocates with `createBuffers`.

Start order: launch Timox VASIO Control **before** opening TimoxVasio in an audio application. In **Configuration**, choose the physical ASIO driver, then a sample rate and buffer size it accepts. The engine listens on `127.0.0.1`, port `52525` when free, otherwise a port chosen at startup and announced by the `api.ready` event; Control displays it. The engine must keep running even if the Control window is closed. A route saved for an absent application stays in `configuredRoutes` and moves to `routes` when its channels open.

In a clone of the repository, `pwsh -NoProfile -File .\tests\verify-installed-stack.ps1` checks the driver registration and the installed engine.

## Build and install from source

Additional requirements: Visual Studio 2026 with the C++ tools, CMake, and the ASIO SDK in `asiosdk`. The detailed commands are in [BUILD_DRIVERS.en.md](BUILD_DRIVERS.en.md).

From an administrator terminal at the repository root:

```powershell
.\install\build_and_install.bat
```

The script builds the `TimoxVasio` target in `build_driver_110`, copies `TimoxVasio.dll` to `C:\Program Files\Steinberg\VirtualASIO\1.2.2`, removes old VASIO1–VASIO4 entries and registers the driver. If an older DLL is still locked by an application, it says so; that DLL is no longer registered as a driver.

The engine and the application are built separately: see [BUILD_DRIVERS.en.md](BUILD_DRIVERS.en.md) and [gui/README.en.md](../gui/README.en.md).

## Uninstall

In an administrator PowerShell, from the extracted archive folder (or `install\` in the repository):

```powershell
pwsh -NoProfile -File .\register_drivers.ps1 uninstall
```

The command removes the ASIO and COM registration. The files under `C:\Program Files\Steinberg\VirtualASIO\` must be deleted by hand once the audio applications are closed. Timox VASIO Control is uninstalled from Windows settings.
