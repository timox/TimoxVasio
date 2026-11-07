# Build the unified Windows preview

The preview is built from this repository only. A clean Git worktree is required; native outputs use a revision-specific directory under `build/unified-preview/`, and the refreshed artifact lock is generated in `gui/`.

From the repository root, run:

```powershell
pwsh -NoProfile -File .\scripts\build-unified-preview.ps1 `
  -MidiWinmdPath <path-to-pinned-Midi2.winmd> `
  -CppWinrtPath <path-to-pinned-cppwinrt.exe> `
  -CppWinrtHeadersPath <path-to-pinned-cppwinrt-headers> `
  -Package
```

The MIDI metadata and cppwinrt versions and SHA-256 values are pinned in `gui/artifacts.lock.json`; the script rejects mismatched inputs. It builds PWAR, the MIDI-enabled engine, and the ASIO driver, stages their receipts and hashes, runs `npm ci` with the committed lockfile, and creates the per-machine NSIS installer at `gui/dist/TimoxVasio-1.3.0-alpha.1-preview.exe`.

The installer is built and verified but not installed or launched. Run the script without `-Package` to build and stage the native components only; commit the refreshed artifact lock before packaging in a separate command.

`1.2.7` remains the last stable release. The installer is explicitly an unreleased `1.3.0` preview.
