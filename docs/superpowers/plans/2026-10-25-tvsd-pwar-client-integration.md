# TVSD PWAR Client Integration Plan

> **For agentic workers:** use the existing native implementation workflow; keep each change inside the TimoxVasio/TVSD side of the repository.

**Goal:** Let TVSD start, stop, configure, observe, and remotely control the supervised `PwarAsioClient.exe` while the selected physical ASIO card and existing routes remain in use.

**Architecture:** The Windows engine owns one fixed-path PWAR child and drains its bounded JSON-lines stdin/stdout/stderr pipes. The local API exposes status and validated start/stop/configuration commands; TVSD supplies peer IPv4 and queue age only. Remote calls are reachable only through the separate mutually authenticated TLS gateway; do not expose the loopback API or child pipes directly.

**Tech Stack:** C++17, Win32 process and pipe APIs, existing cpp-httplib/nlohmann-json API, Electron/React, Schannel remote-control gateway.

**Spec:** `patches/pwar/HANDOFF_TIMOX.md` and `patches/pwar/client/CONTROL.md` (read-only references; do not edit PWAR files).

## Global Constraints

- Do not modify `patches/pwar/` or PWAR binaries/source.
- Driver and engine compatibility is TimoxVasio 1.2.7; TVSD remains version 0.1.0 with a distinct application ID and output directory.
- Preserve the selected physical ASIO driver, master clock, and routes; PWAR is an ordinary direct ASIO client, not a replacement physical driver.
- PWAR executable path is fixed beside the engine; never accept an executable path or shell command from API/UI input.
- Verify the packaged client SHA-256 against handoff value `9E47FDE737D54552E0882D8FFF2D49AB912BDB92CE3247A39341634720CA8441`.
- The control protocol is UTF-8 JSON Lines, at most 16,384 bytes per line; correlate replies by request ID and tolerate events between replies.
- Fixed client parameters: ports 8321, channels 1/2, float32, 48 kHz, and the ASIO block announced by the driver. UI exposes only peer IPv4 and `max-queue-ms` (100 default); valid range must follow the PWAR CLI parser.
- Stop the child before reconfiguration, engine shutdown, or replacing the child executable; drain both output pipes and join reader/wait workers before closing handles.
- Remote control is disabled until an authenticated TLS peer is paired. Keep the current Timox API listener on loopback; do not add a plaintext remote listener.
- Do not run or add tests unless the user explicitly requests verification. Build the affected Windows engine and TVSD package for compile/provenance checks.

## Review Focus

1. Child exit, broken pipe, malformed/oversized JSON, and stdout/stderr flooding must not deadlock the API or engine shutdown.
2. A failed start or stop must report the actual child state and must not falsely publish `running` before a recent ASIO callback.
3. Stop/restart and remote-client disconnect must leave the physical card, configuration revision, and unrelated ASIO clients unchanged.
4. Unknown, revoked, or unauthenticated remote peers must receive no PWAR state or command response.
5. A changed or wrong `PwarAsioClient.exe` hash must fail closed before launch.

## Tasks

### Task 1: Fixed-path supervised process

**Files:** create `include/pwar_client_supervisor.h`, `src/pwar_client_supervisor.cpp`; modify engine target source list in `CMakeLists.txt`.

- Add a supervisor with `Start(peerAddress, maxQueueMs)`, `Stop()`, and immutable `Snapshot()` operations.
- Resolve the client beside the engine; verify the expected SHA-256 before every first launch.
- Use `CreateProcessW` with a structured, quoted argument list and inherited stdin/stdout/stderr pipes; do not invoke `cmd.exe` or PowerShell.
- Enforce the protocol line bound, drain stdout and stderr continuously, correlate `status`/`stop` requests, and cache spontaneous status events without blocking the WebSocket thread.
- Define stop semantics as command `stop`, response `stopping`, observe `stopped` or process exit, then close/join with a bounded timeout and force-terminate only this verified child if it fails to exit.
- Stop the child during orderly engine shutdown.

### Task 2: Persisted settings, API control, and telemetry

**Files:** create `include/pwar_client_configuration_store.h`, `src/pwar_client_configuration_store.cpp`; modify `src/control_api_server.cpp`, `include/control_api_server.h`, `src/main.cpp`, `api/openapi-v1.json`, `api/schemas/api-v1.json`.

- Persist only the configurable peer IPv4 and `maxQueueMs`; fixed values remain constants from the handoff.
- Add `GET /api/v1/pwar/client` returning desired config, effective child config, PID, state, network/audio activity, counters, error, and null latency/drift values.
- Add a validated local configuration operation that saves settings and performs stop/restart when the child is running; return a structured error and preserve the previous saved config if restart fails.
- Add WebSocket commands `pwar.client.start` and `pwar.client.stop`, with the existing request ID/error envelope and a status-change event. Start reads the saved settings; no command may supply a path or shell text.
- Keep existing `/api/v1/pwar` behavior for the legacy driver distinct from `/api/v1/pwar/client`.

### Task 3: Remote authentication integration

**Files:** use `docs/superpowers/plans/2026-10-24-remote-timox-api-control.md` as the separate gateway plan; modify its API allowlist/proxy integration only after its certificate and peer-authentication work exists.

- Do not claim remote control is available through the current loopback-only unauthenticated API.
- Once the mTLS gateway is implemented, pass authenticated HTTPS/WSS requests to the same `pwar.client` API and publish its state changes through the same authenticated WebSocket.
- Ensure unknown or revoked peers cannot reach either the status route or start/stop commands; do not add PWAR-specific network sockets.
- If the gateway prerequisite is not complete, ship the local control integration disabled for remote use and report this remaining dependency explicitly.

### Task 4: TVSD controls and packaged client

**Files:** modify `gui/src/App.js`, `gui/src/PwarDialog.js` or add a dedicated PWAR client dialog, `gui/electron/api-client.js`, `gui/electron/preload.js`, `gui/electron/main.js`, `gui/electron-builder.tvsd.json`, `gui/scripts/verify-tvsd-build.ps1`, `gui/TVSD.md`.

- Keep the legacy PWAR driver configuration visibly separate from the new PWAR ASIO client.
- Add controls for peer IPv4, queue age, start/stop, current child state, and meaningful counters; label fixed ports, stereo, 48 kHz, and ASIO-derived block as fixed behavior.
- Package the verified client under the TVSD backend resources path; update TVSD preflight/postflight to verify its SHA-256 and leave the driver install/registration untouched.
- Expose no executable path, arbitrary CLI arguments, or local pipe handles to the renderer.

### Task 5: Build and handoff

- Compile the canonical Windows engine and build `npm run dist:tvsd`.
- Verify engine/driver canonical versions and hashes remain unchanged, the packaged PWAR client matches the handoff hash, and no installer/driver registration is included in the TVSD portable.
- Record any unavailable remote-auth prerequisite and any real-machine audio qualification still outstanding; do not claim measured latency or full stability from client counters alone.

## Completion Boundary

Local integration is complete when TVSD can launch and stop the verified child, inspect its bounded status stream, configure peer/queue age, and observe the normal ASIO client in Timox while the physical card remains selected. The requested remote-control portion is complete only after an mTLS-paired Linux client can read status and issue start/stop through the existing API gateway, and unknown/revoked peers are rejected. Audio continuity, latency, and drift still require real-machine qualification.
