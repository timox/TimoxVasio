# TimoxVSSC

Next development candidate: **0.2.0-alpha.5**, paired with engine
**1.3.0-alpha.1** (API **1.1**, MIDI backend enabled). The external ASIO driver
remains **1.2.7**. See [RELEASE.md](RELEASE.md) and `artifacts.lock.json` for the
assembly, source revisions and outstanding binary/runtime validation.
The bundled PWAR candidate is independently versioned **0.1.0-alpha.1**;
its control protocol remains **v1**.

Control preview extracted from TimoxVasio. The NSIS installer bundles the `1.2.7-test` ASIO driver and stops/removes only the GUI-owned engine, PWAR client and preview registration; installing the stable driver afterward remains supported.

With the versions in `package-lock.json` installed, run `npm run react-build`, `npm run contract-test`, and `npm run electron-test`. `npm start` launches the UI and may launch its pinned engine; do not use it just to verify a build.

For packaging, place the separately built engine and PWAR client under `artifacts/backend/`, keep the pinned preview driver under `artifacts/driver/`, pin revisions and SHA-256 values in `artifacts.lock.json`, then run `npm run dist`. Missing or changed inputs fail packaging.

The application deliberately retains `%APPDATA%/timox-vsounddealer` for preferences, browser storage and the existing single-instance lock. Existing `%APPDATA%/vasio-control` and preview-specific profiles are not merged or deleted. API authentication and engine discovery remain unchanged.
