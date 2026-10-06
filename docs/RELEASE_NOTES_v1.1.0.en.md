# TimoxVasio 1.1.0

[Français](RELEASE_NOTES_v1.1.0.md) | **English**

This release includes the virtual ASIO driver, audio engine, and Timox VASIO Control.

## Downloads

| File | Purpose |
| --- | --- |
| `TimoxVasio.Driver.1.1.0.zip` | `TimoxVasio.dll` and driver installer to run as administrator. |
| `Timox.VASIO.Control.Setup.1.1.0.exe` | Install Control and the audio engine. |
| `Timox.VASIO.Control.1.1.0.exe` | Portable Control and audio engine. |
| `MANIFEST.json` | Published files' SHA-256 hashes, versions, and commit. |

Install the driver from the ZIP, then install or launch Timox VASIO Control. Open Control before the ASIO application so the engine and its local API are available. See the [installation procedure](../INSTALL.en.md).

## Changes

- Routing between ASIO applications and a physical driver, with channel profiles per application.
- Persistent routes linked to executable names: they wait for an absent application's channels and activate when it reconnects, even with a new PID.
- Integrated API and Swagger; state distinguishes configured and active routes.
- Reorganized interface with meters for active channels, stereo L/R correlation, API console, and logs.
- Post-installation checks of the registered DLL, running engine, and API version.

The [1.1.0 feature guide](FONCTIONS_1.1.0.en.md) explains the five views with diagrams and provides HTTP and WebSocket examples for meters, correlation, and persistent routes.

Native builds and tests and interface tests pass. Artifact SHA-256 hashes are in `MANIFEST.json`. Buffer-size and missed-deadline diagnostics are tracked in the [roadmap](ROADMAP.en.md) for a later version.
