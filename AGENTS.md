# TimoxVasio repository guide

## Active repository
- The root is the directory returned by `git rev-parse --show-toplevel` in this clone.
- Keep changes within this public repository.
- Before any build or installation, confirm the root with `git rev-parse --show-toplevel` and read `git status`.

## Canonical binaries and paths
| Component | Role | Build | Distribution |
|---|---|---|---|
| `TimoxVasio.dll` | ASIO driver loaded by applications | `build_driver_110\Release\TimoxVasio.dll` | Embedded in Setup and available in `gui\dist\TimoxVasio Driver 1.2.7.zip` |
| `TimoxVirtualAsioEngine.exe` | Audio engine and API server | `build_codex_110\Release\TimoxVirtualAsioEngine.exe` | Embedded under `resources\backend\` in both Electron executables |
| `Timox VASIO Control` | Electron interface | Sources in `gui\` | `gui\dist\Timox VASIO Control Setup 1.2.7.exe` and `gui\dist\Timox VASIO Control 1.2.7.exe` |

Electron uses the engine in `build_codex_110\Release` during development and packaging. The packaged application launches it from `process.resourcesPath\backend`. Electron Setup installs the interface, engine, and ASIO DLL, and registers the driver with administrator rights. The portable executable does not install the driver. The ZIP remains available for manual driver installation.

The 1.2.7 driver installs to `C:\Program Files\Steinberg\VirtualASIO\1.2.7\TimoxVasio.dll`. This versioned path allows registration while another application still holds an older DLL open. Restart that application to load the new version.

## Provenance
- Never select a binary by name alone. Check its path, timestamp, and SHA-256.
- Compare the packaged engine with the built engine and the driver in both Setup and ZIP with the built driver.
- If a file is missing or its hash differs, stop packaging and rebuild the relevant component.
- `gui\dist\win-unpacked\resources\backend\TimoxVirtualAsioEngine.exe` is a control copy, not a separate deliverable.

## Mandatory interface rule
- The left navigation contains sections and global state/actions only. Never place contextual process, channel, connection, or selection details there.
- Routing object actions originate in the context menu of the background, process, or cable. Their detail forms open in modal dialogs.
- Never render a permanent connection-properties panel beside the graph, on either side. Selecting a cable alone must not open a detail panel or dialog.
- Keep this rule across screen sizes and future changes unless the user explicitly changes it.
