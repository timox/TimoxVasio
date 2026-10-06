# Timox VASIO Control: configuration, Swagger, and diagnostics implementation

[Français](2026-10-04-electron-control-diagnostics-swagger.md) | **English**

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task by task. Checkbox (`- [ ]`) steps track progress.

**Goal:** Deliver an Electron application that configures and controls the engine, displays Swagger locally, and reads persistent logs with a debug mode.

**Architecture:** The C++ engine owns the log and exposes diagnostics and orderly shutdown in the documented v1 API. Electron manages process startup and attachment; preload exposes the required operations; React presents Configuration, API, and Diagnostics. The build bundles OpenAPI and Swagger UI without external resources.

**Tech stack:** C++20/Win32, cpp-httplib, nlohmann/json, HTTP/WebSocket API v1, Electron 28, React 18, Jest, Node test runner, Electron Builder.

**Specification:** `docs/superpowers/specs/2026-10-04-electron-engine-control-diagnostics-swagger-design.en.md`.

## Global constraints

- The documented API is the required contract for business state, diagnostics, and engine commands.
- The renderer reads no local business file, registry, or audio mapping.
- The API remains bound to `127.0.0.1`.
- Real-time callbacks do not allocate, access disk, log, or block.
- Audio samples and buffer contents are never written to logs.
- Audio configuration is applied as a complete `configuration.apply` document.
- Engine shutdown is refused while an ASIO client is attached.
- Changes and assets are limited to TimoxVasio; publication goes to public `timox/TimoxVasio`.
- Tagging and publication wait for build, API, UI, Swagger, and Mixxx discovery checks.

## Review focus

- The callback must never call the logger, even in debug mode. Add a review and an isolation test for the logger API outside the real-time path in task 1.
- Rotation during log reading must not expose a partial file; test reading during rotation in task 1.
- Two simultaneous start commands must not create two engines; cover the startup lock in manager tests in task 3.
- A new client connection between check and shutdown must prevent audio shutdown; recheck inventory in the native checkpoint in task 2.
- Packaged OpenAPI must match the shipped server version and load offline; compare the bundled document with source in the task 6 build test.

---

### Task 1: Persistent native log and diagnostics preferences

**Files:** Create `include/engine_diagnostics.h`, `src/engine_diagnostics.cpp`, `tests/engine_diagnostics_tests.cpp`; modify `CMakeLists.txt`, `src/main.cpp`.

**Interfaces:** `EngineDiagnostics::Initialize(std::wstring directory)` opens `%LOCALAPPDATA%\TimoxVasio\logs\engine.log` and loads `diagnostics.json`. `Write(Level, component, message)` writes a timestamped entry at the active level. `SetLevel(Level)` persists `info` or `debug` and updates the active level. `ReadRecent(limit)` returns at most 500 complete entries, oldest to newest within the requested window.

- [ ] **Step 1: Write failing logger tests.** In an isolated temporary directory, cover `info` omitting `debug`, `debug` retaining it, timestamp/level/component/message lines, invalid-level rejection, rotation at 5 MiB with four archives, 1–500 bounded reads, coherent reads during rotation, and level preference restored after logger recreation.
- [ ] **Step 2: Run logger tests and verify expected failure.** Run `cmake --build build_api --target EngineDiagnosticsTests --config Release` and `build_api\EngineDiagnosticsTests.exe`. Expect target or assertions to fail because the types/logger do not yet exist.
- [ ] **Step 3: Implement the logger outside audio callbacks.** Create the directory with Win32 APIs, serialize each line as UTF-8 JSON, guard writing with an internal lock, rotate at 5 MiB, retain four archives, and read a coherent line window. No logger object is called from `AudioRoutingRuntime`, `VASIODriver::bufferSwitch`, or the physical driver callback.
- [ ] **Step 4: Initialize and feed lifecycle events.** Initialize in `wmain` before clients/controller start. Write startup, startup failure, `api.ready`, API open/close, controller error, and orderly shutdown events. Preserve the existing initial stdout `api.ready` event.
- [ ] **Step 5: Run logger tests and check limits.** Run the same build and executable; expect all cases to pass and at most the current file plus four archives in the temporary directory.

### Task 2: Diagnostics API routes and orderly shutdown

**Files:** Modify `include/control_api_server.h`, `src/control_api_server.cpp`, `include/audio_controller.h`, `src/audio_controller.cpp`, `schemas/api-v1.json`, `openapi-v1.json`, `CMakeLists.txt`; create `tests/engine_diagnostics_api_tests.cpp`.

**Interfaces:** `GET /api/v1/diagnostics?limit=N` returns `{level, entries}`; `N` defaults to 200 and is valid from 1 to 500. `PUT /api/v1/diagnostics` receives exactly `{level: "info"|"debug"}` and returns the confirmed level. WebSocket `{id, command:"engine.stop", payload:{}}` returns `ENGINE_CLIENTS_CONNECTED` if the snapshot contains a client; otherwise it acknowledges and signals the main thread to stop.

- [ ] **Step 1: Write failing diagnostics/shutdown route tests.** With a controller and logger in a temporary directory, cover default read, bounds 1 and 500, rejection of 0/501, `info`/`debug`, unknown field, invalid level, stop refusal with a client, stop acceptance without a client, rejection of a new attach after stop reservation, and final `engine.status` event.
- [ ] **Step 2: Run API tests and confirm functional failure.** Run `cmake --build build_api --target EngineDiagnosticsApiTests --config Release` and `build_api\EngineDiagnosticsApiTests.exe`; expect compilation or assertions to fail because the routes/command do not exist.
- [ ] **Step 3: Add diagnostics schemas and HTTP API.** Validate `limit` before reading; serialize timestamps and levels; reject unknown PUT properties; return structured `INVALID_DIAGNOSTICS_CONFIGURATION` for invalid bodies. Never return audio content.
- [ ] **Step 4: Add protected WebSocket stop.** Atomically reserve shutdown with client discovery: freeze new publication/attachment, verify no client exists, acknowledge, then trigger the Win32 event consumed by `wmain`. If a client exists, release the reservation and respond without mutation. Close API, controller, and manager in existing teardown order.
- [ ] **Step 5: Update OpenAPI and shared schema.** Declare diagnostics GET/PUT, parameters, responses, errors, entry model, WebSocket `engine.stop`, and connected-client error. Keep historical routes and names excluded from the current contract.
- [ ] **Step 6: Run API tests and verify contract.** Run the build/executable above and `powershell -NoProfile -ExecutionPolicy Bypass -File tests\api-contract.ps1`. Expect passing API tests and OpenAPI checker acceptance of each new route and command.

### Task 3: Safe Electron engine process management

**Files:** Create `gui/electron/engine-manager.js`, `gui/electron/engine-manager.test.js`; modify `gui/electron/main.js`, `gui/electron/preload.js`.

**Interfaces:** `EngineManager.start()` attaches to an active TimoxVasio API or starts one executable. `stop()` reads clients through the API, refuses while one exists, requests `engine.stop`, waits for shutdown, and confirms the API is absent. `restart()` stops then starts without two TimoxVasio processes being active. IPC allowlist: `engine:get-lifecycle`, `engine:start`, `engine:stop`, `engine:restart`.

- [ ] **Step 1: Write failing process manager tests.** Inject spawn, fetch, and ApiClient. Cover attachment without spawn, one spawn for two simultaneous starts, stop refusal with clients, requested stop then API disappearance, timeout retaining `error`, orderly restart, and window closure detaching an engine with clients.
- [ ] **Step 2: Run manager test and verify expected failure.** `node --test gui/electron/engine-manager.test.js` should fail to import before manager creation.
- [ ] **Step 3: Extract process ownership into EngineManager.** Move dev/packaged paths, detection on port 52525, process events, and startup lock into the injectable component. Validate API identity before attaching to an existing engine.
- [ ] **Step 4: Implement stop, restart, and close without client loss.** Use only `engine.stop`; do not call `child.kill()` for normal shutdown. Do not stop an attached or owned engine with a client. On application closure without clients, request orderly shutdown; with clients, detach and leave the engine running.
- [ ] **Step 5: Expose narrow IPC through preload.** Expose lifecycle state and start/stop/restart commands without arbitrary filesystem or shell access. The renderer passes no process path.
- [ ] **Step 6: Run manager tests.** `node --test gui/electron/engine-manager.test.js` should cover attachment, concurrency, active clients, and timeout successfully.

### Task 4: Electron client, local Swagger, and Diagnostics views

**Files:** Modify `gui/electron/api-client.js`, its test, `gui/electron/main.js`, `gui/electron/preload.js`, `gui/src/App.js`, `gui/src/App.test.js`, `gui/package.json`, `gui/package-lock.json`; create `gui/src/ApiDocs.js` and its test.

**Interfaces:** `ApiClient.getDiagnostics(limit)` and `setDiagnosticLevel(level)` call only documented routes. The renderer uses `window.vasio.getDiagnostics`, `setDiagnosticLevel`, `stopEngine`, `startEngine`, `restartEngine`, and `openLogDirectory`. The API view loads the local OpenAPI object included in the build and shows local WebSocket documentation.

- [ ] **Step 1: Write failing ApiClient and UI navigation tests.** Cover diagnostics GET with limit, PUT `info`/`debug`, API error propagation; tab switching, Stop disabled with clients, log entry rendering, level toggle; and API view with a local specification and `configuration.apply` title.
- [ ] **Step 2: Run tests to confirm missing functions/views.** Run `cd gui; node --test electron/api-client.test.js` and `cd gui; npm run react-test -- --watchAll=false --runInBand`; expect new cases to fail because methods and views are absent.
- [ ] **Step 3: Implement ApiClient methods and diagnostics IPC.** Add GET/PUT, allow only `info`/`debug` levels to the API, map structured responses/errors, and expose operations through Electron handlers and preload.
- [ ] **Step 4: Add Configuration/API/Diagnostics navigation.** Move existing sections into Configuration without changing their API sources. Add lifecycle buttons; disable them when the API is absent, a change is in progress, or clients are present for stop/restart.
- [ ] **Step 5: Bundle Swagger UI and OpenAPI.** Add `swagger-ui-react` as a local dependency. Import `../openapi-v1.json` into the renderer at build time; configure Swagger UI without a remote `url`, CDN, outgoing request, or try-it-out feature bypassing the local API. Render WebSocket commands/events from a structured text component driven by the same contract.
- [ ] **Step 6: Add Diagnostics view.** Display engine, PID/attachment, clients, confirmed level, latest log entries, structured error, and refresh/export/open-folder buttons. The debug button calls `setDiagnosticLevel` and renders the state only after the API response.
- [ ] **Step 7: Run ApiClient/UI tests.** Run `cd gui; node --test electron/api-client.test.js`, `cd gui; npm run contract-test`, and `cd gui; npm run react-test -- --watchAll=false --runInBand`. Expect new and existing tests to pass; no test reads registry, profile file, or mapping directly.

### Task 5: Improve interface legibility

**Files:** Modify `gui/src/index.css`, `gui/src/App.css`, `gui/src/App.test.js`.

**Interfaces:** A shared CSS scale supplies color, typography, surfaces, radius, spacing, and focus. The three views use the same navigation, inset, and status-message components.

- [ ] **Step 1: Add structural/visual-accessibility assertions.** Test tab roles and selected state, keyboard focusability, text labels for status, and semantic surface classes instead of scattered color rules.
- [ ] **Step 2: Verify failure before styling.** Run `cd gui; npm run react-test -- --watchAll=false --runInBand`; expect navigation/focus states not yet rendered.
- [ ] **Step 3: Define tokens and apply corrections.** Use one font stack and 16 px base; enlarge `.hint`, summaries, and matrix per spec; reserve cyan for accents; distinguish section/card backgrounds; standardize 1 px neutral borders, 6 px radii, 40 px control height, and contrasting visible focus. Remove conflicting global and identified dead styles.
- [ ] **Step 4: Run React tests and production build.** Run the same test command and `cd gui; npm run react-build`; expect passing tests and no build error or blocking CSS warning.

### Task 6: Package, inspect visually, and publish

**Files:** Modify `gui/package.json`, `gui/GUI_GUIDE.md`, `gui/README.md`, `INSTALL.md`, `API.md`, `openapi-v1.json`, `schemas/api-v1.json`, `docs/superpowers/plans/IMPLEMENTATION_LEDGER.md`.

- [ ] **Step 1: Build engine and run focused suites.** Run `cmake --build build_api --target TimoxVirtualAsioEngine EngineDiagnosticsTests EngineDiagnosticsApiTests --config Release`, both diagnostics test executables, and `powershell -NoProfile -ExecutionPolicy Bypass -File tests\api-contract.ps1`. Expect a successful x64 build and passing diagnostics/API contract tests.
- [ ] **Step 2: Build Electron artifacts.** Run `cd gui; npm run contract-test`, `cd gui; npm run react-test -- --watchAll=false --runInBand`, and `cd gui; npm run electron-build`. Expect a successful React build and installer/portable executable with backend, `openapi-v1.json`, and Swagger UI bundled locally.
- [ ] **Step 3: Check package contents.** Inspect `gui/dist/win-unpacked/resources/app.asar` and `resources/backend/TimoxVirtualAsioEngine.exe`; confirm no CDN URL and the OpenAPI document is present. Open the packaged app, view all three screens, and check the network console for no external requests.
- [ ] **Step 4: Inspect a screenshot of each view.** Capture Configuration, API, and Diagnostics in the current package; inspect the captures for text size, surface hierarchy, keyboard focus, and clipped sections.
- [ ] **Step 5: Verify Mixxx discovery and publishable audio state.** Confirm TimoxVasio in Mixxx and open channels in the API. Validate signal only after choosing/providing SSL 12 routing; without a measured physical signal, publish with an explicit statement that driver/API were verified but hardware path was not.
- [ ] **Step 6: Prepare delivery docs.** Update guides and ledger with diagnostics commands, log path, Swagger retrieval, build version, profiles, and audio criteria actually verified. Do not write `achieved` for an unmeasured audio path.
- [ ] **Step 7: Prepare tag and TimoxVasio publication.** First verify the public TimoxVasio clone's remote and branch, and that the commit contains only this project's changes. Create a versioned tag and attach installer and portable app.

## Plan self-review

- Coverage includes native logger, API contract, Electron lifecycle, Swagger, Diagnostics UI, UX fixes, build, and delivery.
- All business data stays in the API; only OS startup operations pass through narrow IPC.
- Logging, allocation, disk, and waits are explicitly forbidden in all audio callbacks.
- Dedicated checks cover 1–500 bounded log reads, rotation, concurrent start, connected-client shutdown, offline Swagger, and the target Git remote.
- SSL audio routing remains a separate conditional validation from tool publication; no false success is declared.
