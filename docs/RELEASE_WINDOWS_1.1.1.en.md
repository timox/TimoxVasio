# Windows release files for 1.1.1

| File | Contents | Use |
| --- | --- | --- |
| `Timox.VASIO.Control.Setup.1.1.1.exe` | Electron interface and bundled `TimoxVirtualAsioEngine.exe` | Install Control and the engine |
| `Timox.VASIO.Control.1.1.1.exe` | Portable Electron interface and bundled engine | Run Control without installing it |
| `TimoxVasio.Driver.1.1.1.zip` | `TimoxVasio.dll`, installer, registration script, license | Install and register the ASIO driver |

Install the driver before selecting TimoxVasio in an ASIO application. Extract the ZIP and run `Installer TimoxVasio.bat` as administrator. The DLL is registered from `C:\Program Files\Steinberg\VirtualASIO\1.1.1`. Restart applications that still have an earlier DLL open.

Then install or run Control. It starts or connects to the engine and its local API. The Setup and portable executables do not install or register the ASIO DLL.

The release `MANIFEST.json` records the source commit, artifact paths, binary versions, and SHA-256 values. See the [release notes](RELEASE_NOTES_v1.1.1.en.md) and [installation guide](../INSTALL.en.md).
