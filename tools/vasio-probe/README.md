# vasio-probe

Independent 64-bit ASIO host used to measure TimoxVasio compatibility. The executable implements modes 1 (`caps`), 2 (`lifecycle`), 3 (`cadence`) and 4 (`loopback`), plus read-only driver discovery (`list`). The PowerShell route wrappers and `run_matrix.ps1` implement modes 5 (`multiclient`) and 6 (`reconfig`) using multiple probe processes. In loopback mode the host now responds to `kAsioResetRequest` by stopping, disposing buffers, closing and reopening the driver, querying the new format, creating buffers and starting again. The report records each completed host restart and final format; mode 6 requires both the rate and buffer restart for every client. The frequency/buffer/client sweep is scripted but has not been run; mode 7 remains pending. The installed 1.1.1 driver currently fails mode 6 because it does not notify clients of engine-initiated live format changes; see `docs/VASIO_PROBE_FINDINGS_2026-10-07.md`.

## Build

From the repository root, with a locally available ASIO SDK:

```powershell
cmake -S . -B build_vasio_probe -A x64 -DASIO_SDK_PATH=C:/path/to/asiosdk
cmake --build build_vasio_probe --config Release --target vasio-probe
```

The probe references the SDK headers and host helper at build time. When the supplied SDK has `host/pc/patched_asiolist.cpp`, it uses that helper to avoid its known array-deletion defect; otherwise it uses `host/pc/asiolist.cpp`. No SDK source is copied into this tool directory.

## Commands

```powershell
build_vasio_probe/Release/vasio-probe.exe list
build_vasio_probe/Release/vasio-probe.exe caps --driver TimoxVasio --output tests/results/caps-timox.json
build_vasio_probe/Release/vasio-probe.exe lifecycle --driver TimoxVasio --output tests/results/lifecycle-timox.json --thresholds tools/vasio-probe/thresholds.json --cycles 10
build_vasio_probe/Release/vasio-probe.exe lifecycle --driver TimoxVasio --output tests/results/lifecycle-reload.json --thresholds tools/vasio-probe/thresholds.json --cycles 10 --reload
build_vasio_probe/Release/vasio-probe.exe lifecycle --driver "ASIO4ALL v2" --output tests/results/asio4all-live-rate.json --thresholds tools/vasio-probe/thresholds.json --cycles 1 --live-rate 44100
build_vasio_probe/Release/vasio-probe.exe cadence --driver TimoxVasio --output tests/results/cadence-timox.json --thresholds tools/vasio-probe/thresholds.json --seconds 300
pwsh -NoProfile -File tests/run_matrix.ps1 -LifecycleCycles 10
pwsh -NoProfile -File tests/run_matrix.ps1 -Sweep -PlanOnly
pwsh -NoProfile -File tests/run_matrix.ps1 -Sweep -LongFormats '48000/512'
pwsh -NoProfile -File tests/with-probe-route.ps1 -LifecycleCycles 10 -CadenceSeconds 300 -LoopbackSeconds 12 -MulticlientSeconds 20
pwsh -NoProfile -File tests/with-multiclient-route.ps1 -ClientCount 8 -Seconds 20 -Seed 42
pwsh -NoProfile -File tests/with-loopback-route.ps1 -Seconds 12 -Order receiver-first
pwsh -NoProfile -File tests/with-loopback-route.ps1 -Seconds 12 -Order sender-first
pwsh -NoProfile -File tests/with-loopback-route.ps1 -Seconds 12 -Self
pwsh -NoProfile -File tests/with-loopback-route.ps1 -Seconds 12 -KillRole receive -KillAfterSeconds 3
```

The probe returns exit code 0 for a passing check, 1 for a reported inconsistency, 6 for an inconclusive run, and 2–5 for command, clock, COM, or output failures. The reports record QPC ticks and frequency so durations can be computed without relying on wall-clock time. Lifecycle callback events are written by a separate thread to `<output>.events.jsonl`; the callback itself uses a bounded lock-free ring and counts dropped events. Lifecycle cycle details are streamed to `<output>.events.jsonl.cycles.jsonl`. The main JSON keeps only the first `lifecycle.maxInlineCycles` cycles, so process-memory measurements no longer grow with an in-memory list of every cycle. The report records `cyclesPath`, `cyclesRecorded`, and `cyclesInlineLimit`; use the JSONL file for the full per-cycle record.

The `caps` mode opens and initializes the selected registered driver. Close applications using an exclusive physical ASIO driver before probing it. No registry writes or driver installation are performed.

`--reload` calls `CoFreeUnusedLibrariesEx` after each close and verifies that the driver's DLL is no longer mapped in the probe process. It is a separate lifecycle variant; keep its report apart from the standard run.

`tests/run_matrix.ps1` creates a timestamped folder in `tests/results`, writes JSON reports and a Markdown summary, and runs the standard lifecycle followed by the DLL reload variant if the first check passes. Pass `-CadenceSeconds 300` to add a five-minute continuous cadence run after lifecycle. The probe's standalone cadence command defaults to 3,600 seconds. Cadence writes callback QPC and available `TimeInfo` sample positions to a JSONL file. It also polls `ASIOGetSamplePosition` on the control thread, outside the audio callback. Reports distinguish callback timing from position checks; without `TimeInfo`, per-callback position steps remain unverified. QPC polling drift is an estimate with buffer-level quantization, especially in short runs. It does not change routing. Once all ASIO clients are closed, `tests/with-probe-route.ps1` can add a **muted** temporary route from the probe to the selected physical output, run the matrix, and restore the original routes and application profiles. It saves a `restore.json` snapshot before changing them. This temporarily reconfigures the audio engine; use it only in a test window.

The matrix runs mode 5 and mode 6 at 2, 4 and 8 clients, with a separate verdict
for each load. `-Sweep -PlanOnly` lists the supported 44.1/48/96 kHz and
64/128/256/512-frame combinations and reports whether current ASIO clients
prevent execution. It changes no configuration.

`-Sweep -PlanOnly` reads the selected physical driver's declared rates and buffer limits and lists supported combinations of 44,100/48,000/96,000 Hz and 64/128/256/512 frames. It reports active clients and changes nothing. `-Sweep` runs the matrix at each listed format with five-minute modes 3–5 and a 30-second mode 6. `-LongFormats '48000/512'` selects one or more formats for 60-minute modes 3–5. Each format gets its own matrix reports, while the route wrapper records a cumulative `sweep-summary.json` and restores the original format, routes and application profiles. The execution refuses to start with any virtual ASIO client open and rechecks this before each format change. A failed test remains a failed entry; later formats still run unless configuration itself cannot be applied or verified. This long sweep has not yet been executed on hardware.

`tests/with-loopback-route.ps1` configures a temporary unity-gain virtual route, runs two probe processes in either start order or one process with `-Self`, records `vasio-status`, and restores the saved routes and profiles. `-Channel` selects an ASIO channel (one-based in the script, zero-based in the probe). `-KillRole send|receive` terminates one child; it also adds a muted physical clock route so the surviving client's callback coverage can be measured against the configured `minPostKillCoverage`. Both kill directions pass on the installed 1.1.1 engine with that clock route. Without any surviving route, the engine stops the physical clock; see `docs/VASIO_PROBE_FINDINGS_2026-10-07.md`.

For the manual Mixxx → SuperCollider coexistence check, `-AllowOtherClients -PlanOnly` inspects the proposed free probe route without changing configuration. `-AllowOtherClients` then permits the normal two-process loopback while those hosts remain open; it still refuses an already attached probe or an occupied probe channel. The wrapper restores its saved routes and profiles afterward, so avoid editing routing concurrently. Applying the temporary route can briefly interrupt audio.

Receiver reports include `signalSynchronized`; a receiver that never recognizes the test pattern fails with `loopback-signal-not-detected`, even if its error counters remain zero. The route wrapper waits for `vasio-status` to close its history before counting observations.

`clockRegression` fits polled sample position against QPC over the whole run. Cadence also saves every successful poll to `<output>.events.jsonl.polls.jsonl` after audio stops, outside the callback; `positionPollsPath` records that file for simultaneous cross-driver comparisons. Its uncertainty band uses the larger of twice the standard error across non-overlapping blocks and a two-buffer endpoint quantization bound. The block duration is set by `cadence.regressionBlockSeconds` in `thresholds.json`. The band is empirical, not a calibrated statistical confidence interval; it becomes useful on longer recordings. Callback arrival jitter reflects Windows scheduling and does not by itself identify the hardware clock.

`tests/with-multiclient-route.ps1` runs 2, 4 or 8 clients in a seeded opening order, on separate channels. It kills one randomly selected client during a common QPC measurement window, reopens the same process while the others run, and records callback coverage, p99 jitter, position steps and signal errors on unaffected receivers. The restarted process has its own report and must receive callbacks and close cleanly. After the configured restart grace period, every surviving client must still receive callbacks and every receiving client must have zero signal errors. A muted route preserves the physical clock, and the script restores the original routes and profiles. Earlier kill-only runs found an intermittent 2-client jitter failure; the restart variant has not yet been run on hardware. `-Reconfigure` tests a live rate and buffer change with two clients; the installed 1.1.1 driver continues callbacks but sends no ASIO reset notification, so mode 6 currently fails. Mode 7 and execution of the full frequency/buffer/client sweep are pending.

Set `-LoopbackSeconds` together with `-CadenceSeconds` on the matrix to run all five mode-4 variants after a passing cadence trial. Add `-MulticlientSeconds` to run 2, 4 and 8 clients after mode 4 passes. The route wrapper passes these options through and preserves its outer configuration snapshot.
