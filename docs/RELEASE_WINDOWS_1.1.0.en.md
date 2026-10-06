# Windows release files for 1.1.0

[Français](RELEASE_WINDOWS_1.1.0.md) | **English**

| File | Contents | Use |
| --- | --- | --- |
| `Timox.VASIO.Control.Setup.1.1.0.exe` | Electron application with `TimoxVirtualAsioEngine.exe` bundled | Install Control and the engine |
| `Timox.VASIO.Control.1.1.0.exe` | Portable Control with the engine bundled | Run Control without installing it |
| `TimoxVasio.Driver.1.1.0.zip` | `TimoxVasio.dll`, installation and registration scripts, license | Install the Windows ASIO driver |
| `MANIFEST.json` | Commit, versions, and SHA-256 hashes | Verify downloads |

These are the GitHub release filenames. Locally built files may use spaces instead of dots. Setup and portable builds include the engine but not the ASIO DLL. The driver ZIP includes the DLL and installation files but not the engine or Control. See [Installation](../INSTALL.en.md).

Install the ASIO driver before selecting it in an application. Extract the driver archive and run `Installer TimoxVasio.bat` with administrator privileges. The DLL is copied to and registered from a `1.1.0` directory. An already open application must be restarted to load this version. Then install or run Timox VASIO Control; it starts the engine and its local API. Swagger is available in the API view.

To remove the driver, close audio applications and run `register_drivers.ps1 uninstall` from an administrator console.
