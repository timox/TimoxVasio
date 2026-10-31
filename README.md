# TimoxVSSC

Portable control preview extracted from TimoxVasio. The technical engine API and driver identity remain unchanged. See [MIGRATION.md](MIGRATION.md) for provenance and validation.

With the versions in `package-lock.json` installed, run `npm run react-build`, `npm run contract-test`, and `npm run electron-test`. `npm start` launches the UI and may launch its pinned engine; do not use it just to verify a build.

For packaging, place the separately built engine and PWAR client under `artifacts/backend/`, pin their source revision and SHA-256 in `artifacts.lock.json`, then run `npm run dist`. Missing or changed inputs fail packaging. API schema snapshots are included so rendering and unit tests do not require engine binaries. There is no driver installer in this preview.

The application deliberately retains `%APPDATA%/timox-vsounddealer` for preferences, browser storage and the existing single-instance lock. Existing `%APPDATA%/vasio-control` and preview-specific profiles are not merged or deleted. API authentication is preserved.

Every startup validates the packaged engine, PWAR client and API schemas against `artifacts.lock.json`, then checks the actual API listener's executable hash, API version, required capabilities and supervision schema. A matching healthy engine is reused. An obsolete engine recorded in `engine-ownership.json` is replaced automatically after checking its PID, creation time, path and hash; saved routing is retained. An unknown process is never terminated and no competing engine is launched. Failed audio-process refreshes clear the selectable inventory instead of retaining stale processes.
