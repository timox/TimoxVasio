# Unified Repository Migration Inventory

Read-only inventory for the migration plan. No source files or Git history have been moved.

## Repository state

## Version policy

TimoxVasio `1.2.7` remains the latest stable release. The unified work is the unreleased `1.3.0` preview line. Do not create a stable `1.3.0` tag or publish a release as part of this migration. Keep GUI/engine/driver/PWAR version resources intact as component provenance while giving the monorepo one source revision and build identity.

| Repository | HEAD | Working tree | Build/release entry points |
|---|---|---|---|
| TimoxVasio (target) | `ee22f2f` on `migration/unified-repositories`; `main` remains at the 1.2.7 baseline | GUI source consolidated under `gui/`; imported engine moving to canonical `engine/`; root engine/driver conflicts remain under review | Root `CMakeLists.txt`; `gui/package.json`, `gui/package-lock.json`; `scripts/build-unified-preview.ps1` |
| TimoxVSSC | `b314d6f` | Clean | Electron package/lock; `artifacts.lock.json`; packaging scripts and workflows |
| TimoxVAudioEngine | `8350d1d` | Clean | Root `CMakeLists.txt`; `.github/workflows/windows-engine.yml` |
| Timox-Pwar | `f02f653` | Clean | Root `CMakeLists.txt`; `.github/workflows/pwar-build.yml` |
| grrzzzz (legacy duplicate) | `c4ae7bd` | Many tracked modifications and untracked files under `asio/` and unrelated areas | Legacy `asio/CMakeLists.txt`, GUI, API/schema, tests, docs and numerous generated build trees |

### Import progress and engine delta

- Branch `migration/unified-repositories` isolates migration work from `main`.
- `pwar/` contains Timox-Pwar `main` and its imported history (subtree commit `7213185`).
- TimoxVSSC `main` and its imported history (subtree commit `82e629c`) have been copied into canonical `gui/`; all 117 tracked imported files match by SHA-256. The legacy `gui/dist/` remains open in a running process and was not overwritten.
- TimoxVAudioEngine `main` and its imported history (subtree commit `7631e7b`) have been moved to canonical `engine/`; the 34 path conflicts with older root engine/driver sources still need reconciliation before those older source copies are removed.
- TimoxVAudioEngine's import manifest has 184 entries. Comparing those paths with the preexisting TimoxVasio root found 114 byte-identical files, 34 differing files, and 36 paths missing at root. Missing files include the full MIDI patchbay/backend/tests, engine provenance/build metadata, and the pinned transport vendor snapshot. Differences include `CMakeLists.txt`, API schemas, API server/controller code, WASAPI and PWAR supervisor code, plus engine documentation. Do not bulk-overlay these differences; preserve the current driver's root build and merge deliberately.
- The engine extraction manifest says it originated at TimoxVasio commit `3bcc05f5fed34ea8404e478ef34bb230a2efe39c` plus local uncommitted changes. Its file-level hashes are the stronger provenance record for imported working-tree changes.

TimoxVSSC's `artifacts.lock.json` identifies the preview inputs: engine `8350d1d25c841cc7d5ee3239d345ca98b7bb8cf7`, PWAR `7179c868e7110ae90fbe5bfd24eb1f0f56f154e0`, and driver `d263c9470dd052af3bc0f0b43bf1b9611012171f`. The TimoxVasio checkout is at `275283b`; the exact driver source commit/tree used for the pinned DLL must be established from artifact provenance, not inferred from target HEAD.

## Destination map and collisions

| Component | Proposed destination | Collision check |
|---|---|---|
| Existing TimoxVasio engine/driver | Keep current root layout and CMake targets | Canonical engine/driver sources already live here |
| TimoxVSSC Electron/React GUI | `gui/` | Destination exists and contains the older TimoxVasio GUI; reconcile histories/content before replacing anything |
| Timox-Pwar client | `pwar/` | No top-level `pwar/` listed in target; check nested CMake/vendor and licenses before import |
| Shared API schemas/contracts | `api/` | Destination already exists; compare with TimoxVSSC `api/` and engine API definitions before choosing canonical files |
| Unified packaging and artifact lock | `packaging/` plus root lockfile | Existing `gui/electron-builder.tvsd.json`, release scripts, and build outputs must be inventoried before relocation |
| Legacy duplicate in grrzzzz | Remove `asio/` only after Task 6 comparison and preservation | `asio/` currently has extensive modified tracked files and untracked source/docs; it is not safe to delete wholesale |

Do not import `build*`, `dist`, `node_modules`, runtime logs, user data, generated objects, or machine-local artifacts as source. Rebuild deliverables from imported tracked source and locked dependencies.

## Required follow-up before obsolete source removal

- Remove the now-redundant `gui-next/` source copy after full hash comparison; keep the imported GUI history in Git. (Completed: all 125 files matched before removal.)
- Inspect CMake subdirectories, includes, vendor dependencies, resources, code signing, and install references in engine/PWAR/driver.
- Inspect GUI package scripts, Electron Builder configs, CI workflows, and TimoxVSSC artifact staging/verification scripts.
- Resolve driver provenance: preview pins `d263c947`, target checkout is `275283b`; find the exact source commit/tree used to produce the pinned DLL.
- Compare API schema ownership and prevent duplicate or divergent copies between GUI and engine.
- Review licenses and third-party notices for every imported subtree.
- For `grrzzzz/asio`, compare every modified/untracked item with imported source, preserve unique work, and distinguish generated build debris from useful project files before any removal.

## Ruling

Keep the root CMake project as ASIO driver build owner and use the imported engine CMake project for the 1.3 preview engine until the 34 source conflicts are reconciled. This preserves the stable driver build; cost if wrong: the build keeps two engine source layouts longer than intended.

## Follow-up state (2026-11-07)

- The preview engine API exposes its compiled version and exact monorepo revision. GUI discovery requires API 1.1 plus both values from the artifact lock; mismatched, foreign, or unidentified engines are rejected without starting a competing engine.
- Windows preview build completed from `1ea6c76`; artifact/schema verification passed. API contract tests passed 7/7 and Electron/release tests passed 31/31.
- `gui/` now contains the imported GUI source and preview artifacts; the duplicate source path `gui-next/` has been removed after a 125-file SHA-256 comparison. `gui/dist/` and `gui/node_modules/` are legacy generated data, with `dist/` still locked by the running engine.
- `engine/` is the canonical 1.3 preview engine source. The old root `src/` and `include/` engine files remain while the 34 differing paths are reconciled; `pwar/` is imported with history.
