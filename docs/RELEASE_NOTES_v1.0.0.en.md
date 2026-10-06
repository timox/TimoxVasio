# TimoxVasio 1.0.0 — release notes

[Français](RELEASE_NOTES_v1.0.0.md) | **English**

The project's first public release, organized around three components: the `TimoxVasio` ASIO driver, the `TimoxVirtualAsioEngine` engine, and the `Timox VASIO Control` Electron interface.

## Included

- x64 virtual ASIO driver advertising 256 inputs and 256 outputs.
- Audio engine with transport shared among applications and routing to a physical ASIO driver.
- Configuration interface using the documented API, with per-application channel profiles, routing matrix, Swagger, and `info`/`debug` logs.
- Portable Electron package and Windows x64 installer.
- GPL-3.0-only license, project license notice, and ASIO SDK license included in the Electron package's legal resources.
- UX/UI review based on five configuration and routing screenshots, published in [UX_UI_REVIEW.en.md](UX_UI_REVIEW.en.md).
- Getting-started guide with a complete architecture diagram and API examples: [quick start](https://github.com/timox/TimoxVasio/blob/main/docs/API_QUICKSTART.en.md).
- Development support through [GitHub Sponsors](https://github.com/sponsors/timox), described in [Support the project](https://github.com/timox/TimoxVasio/blob/main/docs/FUNDING.en.md).

## Validation performed on this version

- The user confirmed an end-to-end audio test after reinstalling version 1.0.0: Renoise was connected with 64 inputs and 64 outputs; the engine used SSL ASIO Driver 1 at 48 kHz and 1024 frames, with eight active routes. The user confirmed audible sound.
- The `audio.meter` stream supplied 60 readings in 3.5 seconds on routed physical outputs and source virtual outputs. Observed peaks ranged from `-101.65` to `-26.43 dBFS`.
- Logs and the diagnostics API were verified after reinstalling: engine startup, physical driver configuration, and application of the eight routes were recorded in `%LOCALAPPDATA%/TimoxVasio/logs/engine.log`.
- The user verified Swagger in the installed Electron interface.

Channel profiles take effect the next time each affected audio application starts.

## Windows x64 release files

| GitHub asset | Purpose |
|---|---|
| `Timox.VASIO.Control.Setup.1.0.0.exe` | Per-user installer for **Timox VASIO Control**. Installs the Electron interface and its packaged engine. It neither installs nor registers the `TimoxVasio.dll` ASIO driver. |
| `Timox.VASIO.Control.1.0.0.exe` | Portable application: Electron interface and packaged engine, without installing the interface. The ASIO driver must be installed separately. |
| `TimoxVasio.dll` | Virtual ASIO driver loaded by audio hosts. This file alone does not install itself: also download the release source code and follow “Install the downloaded DLL” in [INSTALL.en.md](https://github.com/timox/TimoxVasio/blob/main/INSTALL.en.md). |
| `TimoxVirtualAsioEngine.exe` | Audio engine executable used by the interface. The packaged version is included in both applications above; this separate asset is for manual deployment or diagnosis. |
| `SHA256SUMS.txt` | SHA-256 sums of the four binaries above, for integrity checking after download. |

For a normal installation, first install **Timox VASIO Control** with the Setup, then install and register `TimoxVasio.dll` separately following [INSTALL.en.md](https://github.com/timox/TimoxVasio/blob/main/INSTALL.en.md). ASIO hosts load the DLL; the interface configures the engine launched with it.

This release is published in the public [timox/TimoxVasio](https://github.com/timox/TimoxVasio) repository.
