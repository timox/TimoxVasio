# Windows release files for 1.2.4

Download all four files from [release 1.2.4](https://github.com/timox/TimoxVasio/releases/tag/v1.2.4): `TimoxVasio.Driver.1.2.4.zip`, `Timox.VASIO.Control.Setup.1.2.4.exe`, `Timox.VASIO.Control.1.2.4.exe`, and `MANIFEST.json`.

Extract the ZIP and run `Installer TimoxVasio.bat` as administrator. It registers the DLL under `C:\Program Files\Steinberg\VirtualASIO\1.2.4`. Restart audio applications still holding an older DLL. Then install or run Control; its interface and engine are packaged together.

The manifest records the source commit and SHA-256 hashes. See the [release notes](RELEASE_NOTES_v1.2.4.en.md) and [installation guide](INSTALL.en.md).
