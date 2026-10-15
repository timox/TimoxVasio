# TimoxVasio 1.2.0

This release reduces audio callback work when an ASIO application opens few channels. The advertised capacity remains 256 inputs and 256 outputs per application.

## Changes

- Shared transport copies only channels created through `createBuffers`, ordered by channel number. Its v8 format coordinates the DLL and engine during reconfiguration and rejects older layouts.
- Ring transfers use contiguous spans. The physical callback avoids clearing outputs twice.
- Clients without routes continue to receive callbacks. Removing the last route leaves physical outputs silent.
- The documented API, buffer size, and phase settings remain unchanged.

## Measurements and validation

With one output channel, one muted route, SSL ASIO Driver 1 at 48 kHz and 1,024 frames, the median of the worst physical callback processing times across five-second windows fell from **626.9 µs to 43.8 µs**, about **14 times lower** between the previous DLL/engine pair and the 1.2.0 pair. No delayed or overrun physical callback was reported during these runs. The [measurement report](archive/MESURES_1_1_1_TEST_2026-10-11.md) records all four alternating runs.

Release probes cover cross-process transport, 0, 2, 8, and 256 channels, sparse channel selections, DLL lifecycle, reconfiguration, silence without routes, and client progress. The DLL and engine are built together. `MANIFEST.json` lists their hashes.

## Windows x64 downloads

| File | Contents |
| --- | --- |
| `TimoxVasio.Driver.1.2.0.zip` | ASIO driver, installer, and registration script. |
| `Timox.VASIO.Control.Setup.1.2.0.exe` | Control and engine installer. |
| `Timox.VASIO.Control.1.2.0.exe` | Portable Control and engine. |
| `MANIFEST.json` | Commit, versions, and SHA-256 hashes. |

Install the driver from the ZIP, then install or run Control. Restart audio applications that already had the previous DLL loaded. See the [installation guide](../INSTALL.en.md).
