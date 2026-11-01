# WASAPI process loopback implementation plan

> **For agentic workers:** Use the native execution path and implement the tasks in order.

**Goal:** Create an independent, routable Timox graph source from a copy of a selected Windows application's shared audio, while leaving its original playback path unchanged.

**Architecture:** Discover active Windows render sessions as candidates, not graph nodes. An explicit process-loopback bridge bound to the verified process identity is the WASAPI adhesion step: it creates a separate source from a copy of the process tree's shared audio. The application's original playback continues independently. Preserve the existing bridge worker, buffering, conversion and graph routing. Keep process/device discovery separate in Audio setup; show existing Timox ASIO clients as graph sources without asking the user to choose an API.

**Tech Stack:** C++17, WASAPI/Core Audio COM, Windows Application Loopback API, existing Electron/React GUI and local API.

**Spec:** `docs/superpowers/specs/2026-10-25-wasapi-process-loopback-design.md`

## Global constraints

- Process loopback requires Windows 10 build 20348 or later.
- Identify Windows applications from active render audio sessions; label these as Windows applications, not direct WASAPI clients.
- Bind process capture to PID, process creation time and image path; validate before and after activation and do not start/read on mismatch.
- Process loopback is capture-only and must reuse the current WASAPI bridge path.
- Process loopback creates a copy of shared audio from the selected process and its children; it does not redirect or replace the original stream.
- Endpoint loopback creates a combined copy of the selected output mix and does not identify individual applications.
- Keep physical devices and applications separate in Audio setup; do not expose an ASIO-versus-WASAPI choice there.
- Do not use, recommend, bundle or configure VB-Audio Cable or another third-party virtual cable.
- Do not call COM, allocate or wait in the graph callback.

## Review focus

- A PID exits and is reused during asynchronous activation: close the unstarted client before the first packet read.
- A WASAPI session spans multiple processes: show it as non-selectable instead of guessing a target PID.
- The process has no active Windows render session: omit it from the Windows application list; do not infer it from its executable name.
- A process exits after bridge creation: withdraw its active graph ports after callback quiescence and retain an unavailable configured bridge without retrying a reused PID.
- An older engine lacks process inventory support: keep existing endpoint bridges usable and do not offer a nonfunctional process-capture action.

---

### Task 1: Discover active Windows audio applications

**Files:**
- Modify `include/wasapi_device_enumerator.h`, `src/wasapi_device_enumerator.cpp`
- Modify `include/audio_controller.h`, `src/audio_controller.cpp`
- Modify `src/control_api_server.cpp`, `api/schemas/api-v1.json`, `api/openapi-v1.json`

**Interface:** Add `WasapiProcessInfo` with PID, decimal-string process creation time, image path, display name and active render endpoint labels. Add `AudioController::EnumerateWasapiProcesses()` and `GET /api/v1/windows-audio-applications`; perform COM enumeration on the controller worker and return only active, single-process render sessions. The UI/API name describes observed Windows playback sessions rather than claiming the application called WASAPI directly.

- [x] Enumerate active render endpoints and `IAudioSessionManager2` sessions; register session-created notifications before taking the session snapshot.
- [x] Map each active session to a PID with `IAudioSessionControl2::GetProcessId`; reject multi-process sessions as process targets.
- [x] Resolve image path and creation time with limited process-query rights; deduplicate by PID while preserving endpoint labels.
- [x] Publish the explicit refresh API and schema without adding enumeration to routine state polling.

### Task 2: Add process identity to bridge configuration

**Files:**
- Modify `include/wasapi_backend.h`, `include/wasapi_bridge_configuration.h`, `src/wasapi_bridge_configuration.cpp`
- Modify `src/audio_configuration_store.cpp`, `src/control_api_server.cpp`, `api/schemas/api-v1.json`, `docs/API.md`, `docs/API.en.md`

**Interface:** Extend `WasapiOpenConfig` with `processId`, `processCreationTime` (`uint64_t`, serialized as a decimal string) and `processImagePath`. `kind: "processLoopback"` requires this identity and forbids `endpointId`/`defaultRole`; all other bridge kinds keep their existing selector rules.

- [x] Encode/decode process-loopback configuration in the persistent store.
- [x] Validate PID range, decimal creation time, bounded UTF-8 image path, bridge ID and capture-only kind at the API boundary.
- [x] Keep old endpoint/default-role configurations byte-compatible and preserve omission semantics.

### Task 3: Implement WASAPI process-loopback activation

**Files:**
- Modify `src/wasapi_backend.cpp`, `include/wasapi_backend.h`

**Interface:** `WasapiBackend::Open` activates `VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK` asynchronously for the configured process tree and returns the existing `IWasapiBackend` capture interface.

- [x] Replace `E_NOTIMPL` with an agile `IActivateAudioInterfaceCompletionHandler` and bounded worker-thread completion wait.
- [x] Verify PID, creation time and image path before activation and after activation completion.
- [x] Only after the second identity check, initialize the shared loopback `IAudioClient`, event handle and capture service using the effective format.
- [x] Close and reject mismatched, unsupported-version, stale-process and failed activations before start or packet consumption.

### Task 4: Integrate bridge lifecycle and graph routing

**Files:**
- Modify `src/wasapi_bridge_manager.cpp`, `include/wasapi_bridge_manager.h`, `src/wasapi_bridge.cpp`
- Modify `include/audio_routing_runtime.h`, `src/audio_routing_runtime.cpp`, `src/persistent_routes.h`
- Modify `src/control_api_server.cpp`, `api/schemas/api-v1.json`

**Interface:** `processLoopback` creates a capture binding with graph ports `wasapi:<bridgeId>:input:<channel>`. Endpoint resolution is skipped; process identity is validated instead.

- [x] Include process identity in bridge equality and persistent configuration.
- [x] Reuse the current capture FIFO/resampler and worker lifecycle; do not add a callback path.
- [x] Treat a changed/exited process identity as unavailable and non-retryable until explicit reselection; active bindings disappear after graph reconciliation.
- [x] Keep endpoint-loopback feedback checks specific to endpoint loopback.

### Task 5: Add the Windows application list to Audio setup

**Files:**
- Create `gui/src/WasapiProcessList.js`
- Modify `gui/src/App.js`, `gui/src/App.css`, `gui/electron/main.js`, `gui/electron/preload.js`, `gui/electron/api-client.js`
- Modify `api/openapi-v1.json`, `docs/API.md`, `docs/API.en.md`

- [x] Add refresh-on-demand process inventory through Electron main/preload IPC.
- [x] Separate endpoint devices from candidate Windows playback sessions; list app name, PID and output endpoint, with technical identity details collapsed.
- [x] Make an explicit “Set up app audio copy” action create the identity-bound process-loopback bridge; passive discovery alone does not create a node.
- [x] Show the resulting independent source in Routing as “App copy”; keep original playback and Timox ASIO client blocks separate.
- [x] Remove the third-party-cable recommendation from the capability table.

### Task 6: Build the engine and preview package

**Files:**
- Build `TimoxVirtualAsioEngine.exe` and the unpacked Electron preview into a fresh output directory.

- [x] Confirm repository root and read `git status --short` before builds.
- [x] Build the Release engine and React frontend, then package the portable preview in a fresh output directory.
- [x] Compare built and packaged engine SHA-256 and confirm the resulting package exists.
- [x] Clarify the app-copy flow, original playback behavior and possible duplicate output in the GUI and user guides.
- [x] Do not run automated tests or restart the user's active engine during this UI clarification work.
- [ ] Qualify process-loopback behavior and latency with a real Windows application and the user's audio route.
- [ ] Qualify the complete Electron flow against the new engine; the currently active engine returns 404 for the application inventory route.
- [x] No automated tests were run and the user's audio engine was not restarted.
