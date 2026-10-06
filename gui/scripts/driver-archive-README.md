# TimoxVasio 1.1.1 driver

This archive contains the x64 ASIO driver, its installer, and the license. It does not contain the Electron interface or the audio engine.

1. Extract all files into the same folder.
2. Run `Installer TimoxVasio.bat` as administrator.
3. Close and reopen audio applications that were already using TimoxVasio. An open application keeps the old DLL in memory until it restarts.
4. Install or start `Timox VASIO Control` separately.

The 1.1.1 driver is installed at `C:\Program Files\Steinberg\VirtualASIO\1.1.1\TimoxVasio.dll`. An older DLL may remain on disk while it is open; the ASIO registration points to version 1.1.1.

To verify registration, run `register_drivers.ps1 list` in PowerShell. To remove it, run `register_drivers.ps1 uninstall` as administrator.
