# Performance Stage Measurements Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship TimoxVasio 1.2.1 with actual, optional, five-second measurements beside each stage in Performance.

**Architecture:** The v9 shared mapping carries a measurement switch and sampled block metadata from DLL to engine. Fixed-size counters accumulate on audio threads; the control worker snapshots them every five seconds. The documented WebSocket command and HTTP state are the only UI interface.

**Tech Stack:** Windows C++20, ASIO, QPC, shared memory, JSON Schema/OpenAPI, React/Electron, CMake, PowerShell.

**Spec:** `docs/superpowers/specs/2026-10-13-performance-stage-measurements-design.md`

## Global Constraints

- New measurement collection defaults off and samples no more than one callback in sixteen when enabled.
- No allocation, lock, log, or network call from an audio callback.
- No automatic buffer, phase, or routing adjustment.
- `GET /api/v1/state` and documented WebSocket commands are the only UI state/action paths.
- Transport v9 requires a matching 1.2.1 DLL and engine. No v8 fallback.
- Report missing observations as `null`, not zero, and never claim speaker latency.
- Identify processes by PID, full command, project ownership, and session relationship immediately before stopping them.

## File map

- `schemas/api-v1.json`, `openapi-v1.json`, `API.md`, `tests/api-contract.ps1`: authoritative command and state contract.
- `src/audio_transport_layout.h`, `src/audio_transport.h`, `src/audio_transport.cpp`: v9 switch, frame-matched metadata, client counters.
- `src/vasio_driver.cpp`: measure application callback and output ring write.
- `src/audio_routing_runtime.h/.cpp`: measure client read and graph routing.
- `src/physical_asio_host.h/.cpp`: measure physical output conversion/copy.
- `src/audio_controller.h/.cpp`, `src/control_api_server.cpp`: five-second snapshot and command handling.
- `gui/src/App.js`, `gui/src/App.test.js`, `gui/src/ProfessionalTheme.css`: collection toggle and values next to stages.
- `tests/audio_transport_probe.cpp`, `tests/audio_routing_runtime_tests.cpp`, new focused counter test: behavior without a live audio session.
- Version/release metadata and packaging scripts: coordinated 1.2.1 artifacts and notes.

## Review Focus

- A partial ring read must not be reported as a complete block wait; Task 2 pins this.
- Ring wrap and reuse must not associate a stale timestamp with a new block; Task 2 pins this.
- Disable during a window must clear both snapshots and shared sampling state; Task 3 pins this.
- An unattached client or stopped physical driver must produce `null` timings; Tasks 3 and 4 pin this.
- Repeating the same enable command must not reset or duplicate the active window; Task 3 pins this.

---

### Task 1: Publish the API contract

**Files:** Modify `schemas/api-v1.json`, `openapi-v1.json`, `API.md`, `tests/api-contract.ps1`, `gui/src/api-contract.test.cjs`.

**Interfaces:** Produce WebSocket `performance.measurements.set` with payload `{enabled:boolean}`; add required `performanceMeasurements` to state. `window` is null or `{durationMs:5000,clients:[{processId,driverId,hostCallback,ringWrite,ringWait,ringWaitDiscardedBlocks}],physical:{clientRead,routing,outputCopy}}`. Each duration is null or `{samples,meanNs,worstNs}`.

- [ ] Add schema fixtures for enabled/disabled states and the new command; assert that missing `performanceMeasurements`, invalid payloads, and incomplete duration objects fail.
- [ ] Run `pwsh -File tests/api-contract.ps1` and `npm run contract-test` from `gui`; expect the new fixtures to fail first.
- [ ] Add the exact state and command definitions to JSON Schema, OpenAPI and API.md, including five-second window, 1:16 sampling, QPC meaning, and no acoustic latency claim.
- [ ] Rerun both contract commands; require passing results, then commit the contract files.

### Task 2: Versioned transport and DLL samples

**Files:** Modify `src/audio_transport_layout.h`, `src/audio_transport.h`, `src/audio_transport.cpp`, `src/vasio_driver.cpp`, `tests/audio_transport_probe.cpp`.

**Interfaces:** Add `SetStageMeasurementsEnabled(bool) noexcept`, `StageMeasurementsEnabled() const noexcept`, `WriteClientOutputMeasured(data,frames,callbackTicks,copyTicks,publishQpc) noexcept`, and `ConsumeClientStageCounters() noexcept` on `AudioClientMapping`. The sampled block record stores `{firstFrame,frames,epoch,publishQpc}`. These interfaces may be adjusted before Task 3, but all callers must use the final signatures consistently.

- [ ] Write transport tests for off mode (no sampled record), normal full-block pairing, wrap, stale epoch, partial read, and ring overflow. Check exactly one discard for each invalid sampled block.
- [ ] Run the focused transport probe; confirm the added assertions fail.
- [ ] Bump transport to v9 and add fixed-size, frame-indexed records and counters. Publish metadata before `clientWriteFrame`; read and validate before advancing `engineReadFrame`. Keep v8 rejection.
- [ ] Measure only every sixteenth DLL callback while enabled; take QPC around the host callback and output write, and publish a record only after successful output write. Do not allocate or lock in callback.
- [ ] Rebuild the focused probe, require all cases to pass, and commit.

### Task 3: Engine accumulation and API command

**Files:** Modify `src/audio_routing_runtime.h/.cpp`, `src/physical_asio_host.h/.cpp`, `src/audio_controller.h/.cpp`, `src/control_api_server.cpp`; add a focused test under `tests/` and wire it in `CMakeLists.txt`.

**Interfaces:** `AudioController::SetPerformanceMeasurementsEnabled(bool)` is idempotent; `ReadPerformanceMeasurements()` returns enabled plus optional last complete window. Runtime and host expose fixed counter snapshots consumed by the existing five-second control worker.

- [ ] Write focused tests for default-off, enable, complete window, repeated enable, disable/reset, reconfiguration/epoch reset, absent client, and absent physical callback. Verify empty statistics serialize as `null`.
- [ ] Run the focused test and confirm failure before implementation.
- [ ] Add sampled QPC spans around client read/demultiplex and graph process in runtime, and output conversion/copy in physical host. Aggregate with fixed atomic counters; no heap, lock or log in callbacks. Keep whole-callback `bufferHealth` unchanged.
- [ ] Collect DLL, ring, runtime and physical counters every five seconds in controller. Reject mixed epochs/configurations, map client identity, and clear incomplete windows on disable/restart.
- [ ] Handle `performance.measurements.set` in WebSocket dispatch and add `performanceMeasurements` to `stateJson`; repeat command with the same bool without resetting.
- [ ] Run focused controller/API tests and contract tests; commit.

### Task 4: Performance view with measured stages

**Files:** Modify `gui/src/App.js`, `gui/src/App.test.js`, `gui/src/ProfessionalTheme.css`.

**Interfaces:** Read only `apiState.performanceMeasurements`; issue only documented `performance.measurements.set`. Preserve the top `bufferHealth` panel and its existing two-second state refresh.

- [ ] Add React tests for off, collecting, complete window, no samples, multiple clients, disabled command, and failed command response. Assert each stage displays mean, worst, count, and five-second context where available.
- [ ] Run the focused App test and confirm the new expectations fail.
- [ ] Add the toggle beside the first measured group, render values inside the existing 1/2/3 path, add routing substage values, and concise explanations. Format ns as µs below 1 ms and ms otherwise; render `—` for null.
- [ ] Rerun focused React tests and `npm run react-build`; commit.

### Task 5: Bounded verification and 1.2.1 release

**Files:** Modify version sources, packaging configuration/scripts, release notes and user docs that carry 1.2.0 artifact names.

**Interfaces:** Deliver 1.2.1 DLL ZIP separately, Setup EXE and portable EXE with matching engine/OpenAPI/schema.

- [ ] Run only the focused contract/transport/controller/UI tests and a detached benchmark comparing off/on at representative frame and channel counts. Record counts, durations and overhead without claiming live latency from the bench.
- [ ] Inventory audio processes immediately before any stop. Stop only confirmed TimoxVasio-related processes authorized by the user; record PID and full command. Rebuild DLL and engine, then restart only session-owned services for any runtime validation.
- [ ] Bump all release versions/paths to 1.2.1; build Release DLL, engine and Electron deliverables. Verify SHA-256 equality of packaged binaries against canonical builds, and API resources against source.
- [ ] Run release preflight; inspect git status and diff; commit release changes, push the branch, integrate on main, tag `v1.2.1`, and publish a GitHub release with the three expected assets. Verify remote asset hashes and release URL.
