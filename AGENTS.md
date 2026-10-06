# TimoxVasio repository guide

## Active repository
- The root is the directory returned by `git rev-parse --show-toplevel` in this clone.
- Keep changes within this public repository.
- Before any build or installation, confirm the root with `git rev-parse --show-toplevel` and read `git status`.

## Canonical binaries and paths
| Component | Role | Build | Distribution |
|---|---|---|---|
| `TimoxVasio.dll` | ASIO driver loaded by applications | `build_driver_110\Release\TimoxVasio.dll` | `gui\dist\TimoxVasio Driver 1.1.1.zip` |
| `TimoxVirtualAsioEngine.exe` | Audio engine and API server | `build_codex_110\Release\TimoxVirtualAsioEngine.exe` | Embedded under `resources\backend\` in both Electron executables |
| `Timox VASIO Control` | Electron interface | Sources in `gui\` | `gui\dist\Timox VASIO Control Setup 1.1.1.exe` and `gui\dist\Timox VASIO Control 1.1.1.exe` |

Electron uses the engine in `build_codex_110\Release` during development and packaging. The packaged application launches it from `process.resourcesPath\backend`. Electron Setup installs the interface and engine. The ASIO DLL is distributed separately in the ZIP and must be installed and registered separately.

The 1.1.1 driver installs to `C:\Program Files\Steinberg\VirtualASIO\1.1.1\TimoxVasio.dll`. This versioned path allows registration while another application still holds an older DLL open. Restart that application to load the new version.

## Provenance
- Never select a binary by name alone. Check its path, timestamp, and SHA-256.
- Compare the packaged engine with the built engine and the driver in the ZIP with the built driver.
- If a file is missing or its hash differs, stop packaging and rebuild the relevant component.
- `gui\dist\win-unpacked\resources\backend\TimoxVirtualAsioEngine.exe` is a control copy, not a separate deliverable.
