# TimoxVSSC

Per-machine NSIS control preview extracted from TimoxVasio. The installer deploys and registers the pinned ASIO driver, and its uninstaller removes that registration and driver when owned by this installation. The technical engine API and driver identity remain unchanged. See [MIGRATION.md](MIGRATION.md) for provenance and validation.

With the versions in `package-lock.json` installed, run `npm run react-build`, `npm run contract-test`, and `npm run electron-test`. `npm start` launches the UI and may launch its pinned engine; do not use it just to verify a build.

For packaging, place the separately built engine and PWAR client under `artifacts/backend/`, keep the pinned driver under `artifacts/driver/`, pin revisions and SHA-256 values in `artifacts.lock.json`, then run `npm run dist`. Missing or changed inputs fail packaging. The NSIS installer includes the pinned driver and fails closed when its artifact or registration inputs are missing.

The application deliberately retains `%APPDATA%/timox-vsounddealer` for preferences, browser storage and the existing single-instance lock. Existing `%APPDATA%/vasio-control` and preview-specific profiles are not merged or deleted. API authentication and engine discovery remain unchanged.
