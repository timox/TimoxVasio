# TimoxVSSC extraction — 2026-10-26

## Supervision follow-up — 2026-10-26

The extraction record below is historical. The current sources add engine-owned
observations, preserve channel levels across activity-only events, invalidate
unavailable source levels and distinguish engine-produced sink levels. PWAR
identification uses the current supervisor PID, not process names; unknown
status/counters remain unknown. WASAPI labels distinguish requested audio from
observed stream state. Swagger exposes the supervision models.

Current focused checks: 35 React tests across App meter topology, graph meters,
PWAR supervision and native audio; 35 Node/Electron/contract tests. Both sets
pass. Existing unrelated React failures from extraction remain unqualified.
The engine/schema inputs are pinned in artifacts.lock.json. The original
artifact and the follow-up candidate are distinct; no running engine is replaced
by these source changes. No new hardware or real PWAR-network qualification.

Status: source extraction, production React build, binary preflight and directory packaging verified; runtime and release validation remain pending. Source revision and per-file original/final hashes are recorded in `import-manifest.json`. Original files include uncommitted work: the commit alone does not reproduce the imported snapshot.

## Scope and decisions

Imported the complete GUI sources, Electron main/preload/API clients, tests, scripts, assets, lockfile, documentation, API schema snapshots and licensing notices. No installed dependencies, generated frontend, binaries, secrets or runtime preferences are source imports.

TimoxVSSC keeps preview version 0.1.0 independently of driver/engine version 1.2.7. Product/app identity is `TimoxVSSC` / `com.timox.vssc`. The renderer product name changes; API endpoints, driver identifiers, authentication, loopback discovery and engine supervision behavior do not. API 1.1 is now explicitly required without consulting product semver. Startup tests assert the requirement, and discovery tests reject API 1.0 while accepting 1.1.

Ruling: explicitly retain `%APPDATA%/timox-vsounddealer` before requesting Electron's single-instance lock. This directory was observed on this workstation. Preferences, browser storage and secrets remain in place; none are copied into this repository. A second launcher using this profile focuses the existing GUI. `%APPDATA%/vasio-control` and `%APPDATA%/timox-vsounddealer-preview5` remain untouched; their separate preferences are not automatically merged. Real application coexistence was not exercised.

The default and compatibility builder configurations produce a per-machine NSIS preview installer. The installer bundles the pinned ASIO driver, registers it under the 64-bit ASIO registry, and stops TimoxVSSC clients before install or uninstall. Historical portable/installer assets are retained as source provenance but not invoked. Updated README files are authoritative over imported historical feature guides.

`artifacts.lock.json` pins the included OpenAPI/schema snapshots. Engine and PWAR revisions/hashes now identify fresh sibling builds (see packaging follow-up below). Their intended locations are `artifacts/backend/TimoxVirtualAsioEngine.exe` and `artifacts/backend/PwarAsioClient.exe`. Preflight and the electron-builder beforePack hook fail closed for absent pins/files or mismatched hashes; postflight compares packaged copies, including schema files. The engine launch path uses these local artifacts in development and `process.resourcesPath/backend` when packaged. No runnable source references original build directories.

## Verification

Before build, ran `git rev-parse --show-toplevel` and `git status --short --branch`: the staging folder is within the authorized TimoxVasio working tree, branch `ccr-d306084d-ieaii9`, with pre-existing modifications. No source-repository edits, installs, network operations or commits were performed by this extraction.

Node v24.20.0; existing `gui/node_modules` dependencies were reused for verification only. Initial Node tests used relative `NODE_PATH=../../gui/node_modules`. For the production build, ignored per-package directory junctions under staging `node_modules` pointed at existing packages; the cache directory remained local to staging. These junctions are not exported. A fresh standalone checkout uses `npm ci` with the imported lockfile (not run here).

From staging root:

- `node scripts/embed-openapi.js`: passed; generates bundled Swagger from local snapshots without engine binaries.
- `node --test electron/*.test.js scripts/*.test.cjs src/api-contract.test.cjs`: **34 passed, 0 failed**. Includes existing API client, discovery, process lifetime, startup, embedded OpenAPI, contract tests and new artifact verification coverage. Log: ignored `verification/node-tests.log`.
- With `CI=true`, `node ../../gui/node_modules/react-scripts/scripts/build.js`: **compiled successfully**. Gzipped output: JS 171.84 kB, CSS 22.71 kB. Log: ignored `verification/react-build.log`.
- With `CI=true`, `node ../../gui/node_modules/react-scripts/scripts/test.js --watchAll=false --runInBand --runTestsByPath src/App.test.js src/RoutingPatchbay.test.js src/WasapiDeviceTree.test.js src/PwarDialog.test.js`: **35 passed, 38 failed across 4 suites**. Missing `DOMMatrixReadOnly` test environment support and stale assertions include PWAR default tab contents and WASAPI channel wording.
- Repeated those exact selected suites against an unmodified GUI copy under ignored `verification/baseline`, using `NODE_PATH=../../../../gui/node_modules` and `node ../../../../gui/node_modules/react-scripts/scripts/test.js ...`: **same 35 passed, 38 failed across 4 suites**. These failures predate extraction; no broad unrelated test/component changes were made. Log: ignored `verification/react-baseline.log`.
- `node scripts/verify-artifacts.cjs`: rejected the currently unpinned engine as expected. Test fixtures additionally prove missing/changed binaries, missing/empty schema pins, and mismatched packaged schemas are rejected.

## Next steps and limits

Fresh engine/PWAR/driver inputs and matching API snapshots have been verified. Parent integration owns freezing the engine manifest revision after its final metadata update and refreshing manifest hashes. `npm run dist` produces the per-machine NSIS installer; signing and hardware qualification remain separate steps.

No Electron UI/audio process, hardware test, visual/responsive inspection, installer, self-extracting portable executable, authentication migration or real multi-GUI coexistence test was launched. Existing React failures remain. The profile choice intentionally shares TVSD data/lock and does not merge the stable control application's separate profile. Engine/PWAR/API packaged copies match their supplied pins; runtime compatibility has not been exercised.

The manifest lists source imports and extraction-created source files. `sourceSha256` is the original on-disk content; `sha256`/`targetSha256` are final staged content after documented transformations. Generated directories and binaries are excluded. Copy the manifest itself alongside its listed files; its self-entry has no hash to avoid recursive hashing.

## Directory packaging follow-up

Copied fresh sibling build binaries only after checking parent-supplied SHA-256, then verified destination hashes and equality of both engine API schema snapshots. Artifact pins include working-tree manifest identity, source timestamp, size and SHA-256. The engine manifest identity may still need its final parent metadata refresh; binary hash is final.

After checking git root/status, ran:

- `node scripts/verify-artifacts.cjs`: passed.
- With `CSC_IDENTITY_AUTO_DISCOVERY=false`, `node ../../gui/node_modules/electron-builder/cli.js --dir --config.electronDist=../../gui/node_modules/electron/dist --config.npmRebuild=false`: passed using installed Electron 28.3.3 and electron-builder 24.13.3, with no install/network/escalation. Directory output: `dist/win-unpacked`; log: ignored `verification/package-dir.log`.
- Initial packaging exposed an incorrect beforePack hook context. Corrected it to `context.packager.info.appDir` using installed builder implementation; `node --test scripts/verify-artifacts.test.cjs` passed, including that hook shape.
- `node scripts/verify-artifacts.cjs --packaged`: passed for engine, PWAR and both schemas.
- Inspected app.asar without execution: package identity `timox-vssc`/`0.1.0`, entrypoint `electron/main.js`, preload, renderer HTML and ws dependency present.

Verified packaged SHA-256:

| File | SHA-256 |
|---|---|
| resources/backend/TimoxVirtualAsioEngine.exe | 6CDA07B551B1235264993281FFADF769FBDC04711B0F586751CCFE319045E29D |
| resources/backend/PwarAsioClient.exe | 29D5E3208E10AE5FEBBB7FD3914790A87EAF12D32BE6BEF923F20D7E33566FD3 |
| resources/app.asar | 2D36A2A8965143951E3BE29BDF8DF64F88442FC9CADE45EB7378203D24D05C73 |
| TimoxVSSC.exe | 468D240C7806C35FD59401394A4874FBF0C75B73680D221A458FE4BBF4E5548B |

This is unsigned directory packaging, not a published or runtime-qualified release. No application was started.

Git attributes disable automatic text normalization for this snapshot. This preserves the exact imported bytes and SHA-256 across clones; verify the committed blobs as well as the working tree before updating these pins.
