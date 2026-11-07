# Unified Repository Migration Status

Updated 2026-11-08 from the current local and remote Git state.

## Scope and version policy

`TimoxVasio` is the destination for the GUI, engine, ASIO driver, and PWAR sources. `grrzzzz` remains a separate, older project; only its obsolete TimoxVasio `asio/` tree is being removed. TimoxVasio `1.2.7` remains the latest stable release. `1.3.0-alpha.1` is an unreleased preview; no `1.3.0` stable tag or release has been created.

## Migration state

- Destination branch: `migration/unified-repositories`, HEAD `bad6107`; pushed to `origin/TimoxVasio`.
- The branch contains the imported histories under `gui/`, `engine/`, and `pwar/`, plus the existing root ASIO driver project. It is not yet merged into `main`; destination `main` still points at the stable `1.2.7` baseline.
- Imported source `main` revisions: TimoxVSSC `b314d6f`, TimoxVAudioEngine `8350d1d`, Timox-Pwar `f02f653`.
- Unique source references preserved and pushed under `archive/`: GUI `pr-midi-gui-1`, GUI extraction snapshot, GUI stash, engine extraction snapshot, and engine stash. Engine and PWAR side branches have no commits beyond their imported `main`; the three source repositories have no tags to transfer.
- GUI branch `pr-midi-gui-1` contains a distinct MIDI performance view. It is preserved for review and has not been merged into the preview GUI.
- Source clones `TimoxVSSC`, `TimoxVAudioEngine`, and `Timox-Pwar` are still present locally and on GitHub. They can be closed in an editor; do not delete the remote repositories before the migration branch is merged and audited.

## Preview build and installation

- Unified build script: `scripts/build-unified-preview.ps1`; CMake outputs are isolated by source revision. Obsolete pre-revision caches and superseded trial builds have been purged.
- `1.3.0-alpha.1` x64 per-machine NSIS installer built from GUI/engine/PWAR revision `126a8b1f303770d28a1b7d50268b651348e39a55` and passed artifact preflight plus packaged postflight.
- Installer: `gui/dist/TimoxVasio-1.3.0-alpha.1-preview.exe`, SHA-256 `c846f6caca289bdeaa711026a9296aef1c0baa62602db4d2b88cb78ba3386b2d`.
- Installed cleanly after silent uninstall; both operations exited 0. Installed GUI version is `1.3.0-alpha.1`; engine is `1.3.0-alpha.2`; driver hash matches `gui/artifacts.lock.json`; ASIO registration exists. User profile `%APPDATA%/timox-vsounddealer` remains present, stable `1.2.7` remains installed, and neither the GUI nor engine was launched.
- Release metadata states preview/unreleased. MIDI real-runtime, WASAPI graph interaction, and hardware qualification remain unverified.
- The focused release metadata test passed (1/1); the unified native build, React production build, artifact preflight, and packaged postflight passed. `npm ci` reported 55 dependency audit findings, including one critical.

## Legacy `grrzzzz` cleanup

- `grrzzzz/asio/` is absent locally; its 157 tracked paths were removed in local commit `3538e28` only. The commit was made on top of two unrelated, unpublished local commits, so it was not pushed. Other working changes in `grrzzzz` remain untouched. The old GitHub copy still needs a separately reviewed publication path if remote cleanup is required.

## Remaining before retiring source remotes

1. Review the full migration diff and merge `migration/unified-repositories` into `TimoxVasio/main` without tagging or releasing `1.3.0`.
2. Decide whether the preserved MIDI performance-view branch should be integrated into the preview.
3. Reconcile the remaining duplicate root engine files with `engine/` before removing any source copies; do not bulk-overlay the differing files.
4. Remove or archive the three source GitHub repositories only after verifying the merged destination and its reproducible build.
