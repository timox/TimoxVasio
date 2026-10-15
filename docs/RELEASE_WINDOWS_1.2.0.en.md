# Windows release files for 1.2.0

The driver ZIP contains the ASIO DLL and its installer. The Setup and portable executables contain Control and the audio engine; they do not install the ASIO DLL.

Download all four files from the [1.2.0 release](https://github.com/timox/TimoxVasio/releases/tag/v1.2.0): `TimoxVasio.Driver.1.2.0.zip`, `Timox.VASIO.Control.Setup.1.2.0.exe`, `Timox.VASIO.Control.1.2.0.exe`, and `MANIFEST.json`.

Extract the ZIP and run `Installer TimoxVasio.bat` as administrator. It registers the DLL from `C:\Program Files\Steinberg\VirtualASIO\1.2.0`. Restart audio applications that had the previous DLL loaded. Then install or run Control.

The manifest records the source commit and SHA-256 hashes of each deliverable. See the [release notes](RELEASE_NOTES_v1.2.0.en.md) and [installation guide](INSTALL.en.md).
