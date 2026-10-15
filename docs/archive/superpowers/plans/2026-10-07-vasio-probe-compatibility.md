# VASIO Probe Compatibility Implementation Plan

> **ARCHIVE** — Document historique conservé pour référence. Il ne décrit pas l'état courant de TimoxVasio (1.2.1). Voir le [README](../../../../README.md) et la documentation courante qu'il référence.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task by task. Complete and verify each mode before the next.

**Goal:** Build an independent 64-bit ASIO host that measures TimoxVasio compatibility, then run an automated matrix and document real-host checks.

**Architecture:** `vasio-probe` loads a named registered ASIO driver through the SDK's host helper. A shared session owns driver and buffer lifecycle. The callback writes fixed-size events into a preallocated lock-free ring; the main thread drains records, computes statistics, and writes JSON. Separate processes coordinate loopback and hardware drift through result files and command-line arguments.

**Tech Stack:** C++17, Windows ASIO SDK, CMake/MSVC x64, PowerShell 7, JSON reports.

**Spec:** `AGENT_TEST.md` (the worktree currently has this name; `TEST_AGENT.md` was requested but is absent).

## Global Constraints

- Do not change the ASIO driver implementation or copy SDK files into `tools/vasio-probe`.
- Use a configurable `ASIO_SDK_PATH` for headers and host helper sources.
- No callback allocations, file I/O, or blocking locks. Timestamp with `QueryPerformanceCounter`.
- Configuration owns acceptance thresholds. Report every failure with minimal reproduction steps.
- Registry changes require explicit user authorization; read-only enumeration does not.
- Run modes 1 through 7 in order; later modes require the prior mode's runtime evidence.

## Review Focus

- A registered driver may refuse `init` while another process owns hardware; record an unavailable result instead of treating it as a false compatibility defect.
- ASIO clocks and latency queries may require buffers first; record API status and stage of query.
- Callback events may exceed ring capacity under stress; count overflows and make verdict inconclusive.
- Crashed child processes must not leave matrix workers running; use per-process timeout and cleanup.
- Bitwise loopback is meaningful only on a direct, unity-gain route with a compatible sample type; capture the route setup in the report.

## File Map

- `tools/vasio-probe/main.cpp`: command line, session, modes, JSON report.
- `tools/vasio-probe/README.md`: build and use.
- `tools/vasio-probe/thresholds.json`: editable acceptance thresholds.
- `tests/run_matrix.ps1`: scenario orchestration and result summary.
- `tests/results/`: timestamped JSON and Markdown outputs from runs.
- `docs/TESTS_HOTES_REELS.md`: Renoise, Mixxx, SuperCollider field procedure.
- `CMakeLists.txt`: x64 probe target with configurable SDK path.

## Tasks

### Task 1: Capabilities (`caps`)

- [x] Add command parser and named registry driver loader; reject ambiguous/missing driver names.
- [x] Query channels, channel names and sample types, buffer limits, rate support, latencies, clock sources.
- [x] Report QPC timestamp, raw API statuses, consistency findings, and verdict in JSON.
- [x] Compile x64 and run `caps` against TimoxVasio without changing registration.
- [x] Document options; no driver defect was established by the capabilities run.

### Task 2: Lifecycle (`lifecycle`)

- [x] Add preallocated callback ring and a consumer outside callbacks; test ring overflow and ordering.
- [x] Add init/create/start/stop/dispose/unload repetitions with stage timing and process memory/handle snapshots.
- [x] Add same-process DLL reload variant, default 1,000 cycles, and JSON outcomes.
- [x] Run a short smoke first, then the full default; one complete 1,000-cycle run passed both variants. An earlier reload run stopped receiving callbacks in cycle 5; see `docs/VASIO_PROBE_FINDINGS_2026-10-07.md`.

### Task 3: Cadence (`cadence`)

- [x] Capture callback QPC and sample position from `TimeInfo` in a lock-free ring.
- [x] Compute interval mean/min/max/p99, missing/duplicate callbacks, and QPC-relative drift ppm.
- [x] Run a five-minute short trial at 48 kHz / 1,024 frames; 60-minute trial and matrix sweep remain pending.

### Task 4: Loopback (`loopback`)

- [x] Add separate sender/receiver commands with pseudo-random framed payload and counters.
- [x] Measure bitwise equality, losses, repeats, transport latency, and start-order variants. Both start orders passed a short real two-process trial on 7 October; the long trial remains pending.
- [x] Test both child-termination directions with a muted route preserving the physical clock; surviving callback coverage was 97.5% and 97.0%. The single-process variant passed for 5 minutes with 14,047 bit-identical blocks and stable 1,024-sample latency. The no-route clock policy is documented separately in `docs/VASIO_PROBE_FINDINGS_2026-10-07.md`.

### Task 5: Multi-client (`multiclient`)

- [x] Launch 2/4/8 clients on distinct channels, in a seeded random order, then terminate one while the others continue. The common QPC measurement window excludes normal staggered exits.
- [x] Record survivor callback coverage, p99 jitter, position steps, signal errors on unaffected receivers, and ring overflows. One intermittent 2-client jitter failure is documented in `docs/VASIO_PROBE_FINDINGS_2026-10-07.md`.

### Task 6: Reconfiguration (`reconfig`)

- [x] Trigger rate and buffer changes while two ASIO clients run; record reset and sample-rate messages per client and QPC phase.
- [ ] Confirm protocol-compliant restart of all clients. The installed 1.1.1 driver sent no reset or sample-rate notification in the real trial; see `docs/VASIO_PROBE_FINDINGS_2026-10-07.md`.

### Task 7: Hardware drift (`drift-vs-hw`)

- [ ] Run separate virtual and physical driver hosts with common QPC clock.
- [ ] Compute relative ppm and time until one-buffer displacement over 60 minutes.

### Task 8: Matrix and real hosts

- [ ] Add `tests/run_matrix.ps1` with supported-rate/buffer filtering, 1/2/4/8 clients, short and long durations.
- [ ] Keep thresholds in `tools/vasio-probe/thresholds.json` and emit one JSON report per run plus Markdown summary.
- [ ] Write manual Renoise, Mixxx, and SuperCollider procedures with evidence fields and LatencyMon steps.
- [ ] Validate report schema and matrix aggregation on actual outputs; include defects with minimal reproduction.

## Verification

- Build target for x64 from a clean isolated directory with `-DASIO_SDK_PATH=<SDK path>`.
- Run probe help and negative-input checks, then each mode in sequence with real registered drivers.
- Check callback ring overflow count, QPC monotonicity, process cleanup, and JSON validity for every run.
- Compare produced reports against configured thresholds and capture hardware/environment details.
