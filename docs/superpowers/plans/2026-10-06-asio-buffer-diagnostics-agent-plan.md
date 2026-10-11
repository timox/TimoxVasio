# ASIO Buffer Diagnostics Agent Plan

> **For agentic workers:** This is a coordination plan. Before implementing an objective, write a focused design and task plan for that objective using `superpowers:writing-plans`; execute it with `superpowers:executing-plans` or `superpowers:subagent-driven-development`. Keep all new and changed documentation in English.

**Goal:** Diagnose whether the selected physical ASIO buffer is too short for the observed callback workload, without changing audio configuration automatically.

**Architecture:** Instrument the physical driver's callback in `PhysicalAsioHost`, summarize observations outside the audio thread, expose a versioned diagnostic contract through the engine API, and show evidence-based guidance in Timox VASIO Control. Existing client transport underrun and overrun counters remain distinct.

**Tech Stack:** Windows x64, C++ ASIO host, CMake, local HTTP/OpenAPI/JSON Schema API, Electron/React, PowerShell contract tests.

**Source:** `docs/ROADMAP.md` (post-1.1.0 roadmap); `API.md` and `schemas/api-v1.json` (current control contract).

## Global constraints

- Start only after the 1.1.0 release work is complete. Do not treat historical 1.0.0 ledger entries as current release gates.
- Keep physical callback work bounded, allocation-free, lock-free, and free of logging, file I/O, and network I/O.
- Report physical callback lateness separately from virtual client transport underruns and overruns.
- Include the observation window, selected sample rate, buffer frames, and measured values in any warning.
- Require repeated consistent observations before warning; never change the configured buffer automatically.
- Keep the separate translation work outside this plan. Do not edit its untracked `*.en.md` files; write any new feature documentation in English.
- Before any build or installation, run `git rev-parse --show-toplevel` and `git status`. Use the canonical 1.1.0 paths in `AGENTS.md` and check path, date, and SHA-256 before packaging.

## Execution order and ownership

```text
O1 contract and measurement design
  ├── O2 physical callback instrumentation ──┐
  ├── O3 deterministic policy and tests ──────┤
  └── O4 API schema and documentation ────────┤
                                      O5 integration of O2–O4
                                               ├── O6 Control UI
                                               └── O7 hardware validation
                                                       └── O8 release verification
```

O2, O3, and O4 may run concurrently after O1, in separate worktrees with exclusive file ownership. O6 may build against agreed fixtures after O1, but its final integration waits for O5. O7 owns the physical ASIO devices and audio applications exclusively; no other agent runs hardware sessions at the same time. O8 begins after O6 and O7 pass.

## Objectives to assign

### O1 — Freeze the measurement and API contract

**Owner:** one design agent. **Dependencies:** none. **Deliverable:** a short English design spec in `docs/superpowers/specs/` and an implementation task plan.

- Trace `PhysicalAsioHost::Session::Process`, `AudioController::Snapshot`, `ControlApiServer`, the existing `audio.meter` event, and the diagnostics screen.
- Define callback start interval, callback execution duration, block period (`bufferFrames / sampleRate`), deadline miss criteria, clock source, units, observation-window length, reset rules, unavailable state, and counter overflow behavior. Distinguish late arrival from work taking longer than a block; neither is by itself proof of an audible dropout.
- Define a bounded aggregation method and the exact API JSON fields, including window start/end or duration, sample count, missed deadlines, worst processing time, worst available margin, transport counters, and a recommendation state. Decide how to label a window with too few callbacks.
- Specify the repeated-window warning rule and recovery hysteresis with explicit values. The rule must use observed evidence and not a single isolated spike.
- Write deterministic acceptance cases for startup, no route, stop/reconfigure, rate or buffer change, clock anomalies, counter rollover, and client transport loss without physical lateness.

**Exit gate:** O2–O4 can implement against the same named fields and semantics without negotiating interfaces during coding.

### O2 — Capture physical callback timing safely

**Owner:** one C++ audio agent. **Dependencies:** O1. **Files owned:** `include/physical_asio_host.h`, `src/physical_asio_host.cpp`, a new focused timing component under `include/` and `src/`, its native tests, and the relevant CMake test target.

- Measure entry-to-entry interval and execution duration around the full physical callback, including sample conversion and routing.
- Publish bounded atomic or preallocated observations for a non-audio consumer. Do not use `EngineDiagnostics::Write` in the callback.
- Reset measurements when the physical session starts or changes; make reads safe across stop and session teardown.
- Add deterministic clock-driven tests for on-time, late, over-budget, first-callback, and reset cases. Review the changed callback with `audio-dsp-review` and `audio-numerics-review`.

**Exit gate:** native tests pass and a realtime-safety review finds no new unbounded callback operation.

### O3 — Aggregate evidence and decide when to warn

**Owner:** one C++ policy agent. **Dependencies:** O1. **Files owned:** a new diagnostic aggregation/policy component and its tests. Avoid files owned by O2 or O4.

- Turn timestamp observations into bounded windows and compute counts, processing duration, margin, and repeated-window state exactly as O1 defines.
- Keep physical timing signals and virtual transport counters as separate inputs and outputs; do not infer a physical deadline miss from a transport underrun.
- Test sustained overload, isolated jitter, sparse samples, recovery, reconfiguration, and independent transport loss with synthetic inputs.

**Exit gate:** deterministic tests prove both warning and non-warning paths without physical hardware.

### O4 — Publish the contract and an English feature reference

**Owner:** one API agent. **Dependencies:** O1. **Files owned:** `schemas/api-v1.json`, `openapi-v1.json`, `src/control_api_server.cpp`, API contract tests, and a new English feature reference under `docs/`. Leave the translation agent's files untouched.

- Add a read-only diagnostic response or event as specified in O1. Preserve the existing `GET/PUT /api/v1/diagnostics` log-level and log-entry behavior and the `audio.meter` transport-counter meaning.
- Specify null/unavailable data when the callback has not run; never present zero as proof that no deadline was missed in an unobserved period.
- Add schema and API tests for exact field names, units, reset behavior, and compatibility with existing consumers. Keep OpenAPI, JSON Schema, runtime responses, and English reference text aligned.

**Exit gate:** contract tests cover real response shapes and the English reference matches the live API.

### O5 — Integrate native measurement, policy, and API

**Owner:** one integration agent. **Dependencies:** O2–O4. **Files owned:** `include/audio_controller.h`, `src/audio_controller.cpp`, integration tests, and narrowly scoped conflict resolution in O2–O4 files.

- Connect the physical host snapshot to the policy and API without blocking the audio thread. Verify session lifetime and snapshots across stop, failed start, and reconfiguration.
- Run the native probes and API contract tests. Review all merged callback and controller changes for races, lifetime errors, and allocation paths.

**Exit gate:** one integrated engine build passes native and API tests and exposes consistent measurements.

### O6 — Show the evidence in Timox VASIO Control

**Owner:** one UI agent. **Dependencies:** O1 fixtures; final verification after O5. **Files owned:** `gui/src/App.js`, `gui/src/App.css`, `gui/src/App.test.js`, and the minimal API client/preload files if O1 requires a new endpoint.

- Show sample rate, configured buffer, window duration, sample count, missed deadlines, processing time, margin, and separate client transport losses in the diagnostics view.
- Show a cautious recommendation only for the policy's warning state. Explain why data is unavailable or insufficient; do not offer an automatic buffer change.
- Test running, stopped, insufficient-data, warning, recovery, and API-disconnected states; check keyboard focus and a reduced window size.

**Exit gate:** React tests and build pass; a visual check confirms readable values and status wording.

### O7 — Validate with physical drivers and applications

**Owner:** one hardware validation agent. **Dependencies:** O5 and O6. **Files owned:** an English validation report under `docs/` and focused bug fixes assigned back to the owning agent.

- Record device name and driver version, application, sample rate, buffer frames, route, window duration, callback counts, transport counts, and audible result for each run.
- Exercise at least two physical ASIO drivers and two host applications when available. Compare a smaller and a larger supported buffer at the same sample rate and similar workload.
- Check whether reported incidents align with audible interruptions and whether increasing the buffer reduces them. Also record counterexamples and any unavailable hardware; do not claim unperformed coverage.
- Recheck no-route, stop/restart, and reconfiguration behavior. Keep device ownership and application control with this agent during the session.

**Exit gate:** evidence supports or rejects the warning rule; any mismatch is resolved before release preparation.

### O8 — Verify release artifacts and provenance

**Owner:** one release agent. **Dependencies:** O6 and O7. **Files owned:** release checklist/report and release-specific fixes.

- Run the repository's required native, API, and UI verification. Confirm repository root and status before each build/install sequence.
- Build the canonical 1.1.0 engine and driver paths, then package only after checking source binary paths, timestamps, and SHA-256. Compare the embedded engine to `build_codex_110/Release/TimoxVirtualAsioEngine.exe` and the driver ZIP DLL to `build_driver_110/Release/TimoxVasio.dll`.
- Check both Electron executables contain the expected engine and API assets. Treat `gui/dist/win-unpacked/resources/backend/TimoxVirtualAsioEngine.exe` as a control copy, not a separate deliverable.
- Review all documentation created by this work for English wording and working links.

**Exit gate:** tests, hardware report, package hashes, and English documentation agree. Publishing remains a separate user-directed action.

## Skills available to workers

The skill files were present in the local catalog on 2026-10-06. Agents should read the applicable skill before work, not rely on this summary.

| Work | Relevant available skills |
|---|---|
| Design and task plans | `superpowers:brainstorming`, `superpowers:writing-plans`, `superpowers:executing-plans` |
| Isolated parallel work | `superpowers:using-git-worktrees`, `superpowers:dispatching-parallel-agents` |
| Implementation and verification | `superpowers:test-driven-development`, `superpowers:verification-before-completion` |
| Callback timing and hardware diagnosis | `audio-dsp-review`, `audio-numerics-review`, `audio-performance-debug` |
| API contract and reference | `api-builder`, `api-documentation-generator` |
| Diagnostics UI | `frontend-design:frontend-design` |

No additional skill installation is required for the known work. The skills do not replace access to physical ASIO hardware, Windows build tools, or the applications needed by O7.

## Review focus

- A late callback start with short processing time must be labeled separately from an over-budget callback.
- A virtual transport underrun with on-time physical callbacks must not trigger a physical-buffer warning.
- No callback observations after stop must render as unavailable, not as a healthy zero.
- Reconfiguration must not combine samples from two rates, buffer sizes, or physical sessions.
- API polling during teardown must not race with freed session storage.
