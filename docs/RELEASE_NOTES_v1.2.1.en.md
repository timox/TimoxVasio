# TimoxVasio 1.2.1

This release adds actual stage timing to the Performance view. Additional collection is off at engine startup and can be enabled in that view.

## Changes

- Performance keeps the physical callback budget at the top and shows observed time beside each audio path stage: ASIO application call, virtual ring write, complete block wait, client read, routing, and physical driver buffer copy.
- Each duration shows a mean, worst value, and sample count over a completed five-second window. Collection samples at most one callback in sixteen. An unobserved stage remains blank.
- The documented WebSocket command `performance.measurements.set` controls collection; `GET /api/v1/state` publishes its state and latest window. The command is idempotent.
- Shared transport v9 pairs timestamps with blocks and audio epochs. The 1.2.1 DLL and engine must be installed together. Restart audio applications already holding the previous DLL.
- No automatic buffer, phase, or routing adjustment was added.

## What the numbers mean

The ASIO call includes all work the application performs during that call. Ring wait spans publication to reading the same complete block. Physical copy ends at the driver buffer handoff; these numbers do not measure hardware or acoustic latency. Worst values from separate stages are not summed into a total latency.

## Windows x64 downloads

| File | Contents |
| --- | --- |
| `TimoxVasio.Driver.1.2.1.zip` | ASIO driver, installer, and registration script. |
| `Timox.VASIO.Control.Setup.1.2.1.exe` | Control and engine installer. |
| `Timox.VASIO.Control.1.2.1.exe` | Portable Control and engine. |
| `MANIFEST.json` | Source commit, versions, and SHA-256 hashes. |

Install the driver from the ZIP, then Control. See the [installation guide](INSTALL.en.md).
