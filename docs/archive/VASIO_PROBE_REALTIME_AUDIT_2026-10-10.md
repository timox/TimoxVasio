# `vasio-probe` callback audit (2026-10-10)

> **ARCHIVE** — Document historique conservé pour référence. Il ne décrit pas l'état courant de TimoxVasio (1.2.1). Voir le [README](../../README.md) et la documentation courante qu'il référence.

Scope: source review of the ASIO host callbacks in `tools/vasio-probe/lifecycle.cpp`, `cadence.cpp`, and `loopback.cpp`. This does not certify callback timing on a loaded driver; the live cadence and loopback measurements remain separate requirements.

## Verdict

No allocation, blocking lock, file or network I/O was found in the probe's callback call paths. Each path timestamps with `QueryPerformanceCounter`, updates fixed-size state, and tries to enqueue a fixed-size event. The event writer runs on a separate thread.

| Path | Callback work | Evidence |
| --- | --- | --- |
| Lifecycle | Clear one prepared output buffer, increment counters, enqueue event | `lifecycle.cpp:26-92`; writer `lifecycle.cpp:219-229` |
| Cadence | Clear one prepared output buffer, read optional TimeInfo position, enqueue event | `cadence.cpp:24-92`; writer `cadence.cpp:223-241` |
| Loopback | Decode or produce one bounded audio buffer, count errors, enqueue event | `loopback.cpp:20-135`; pure integer/bit operations in `loopback_pattern.h`; writer `loopback.cpp:264-287` |
| Queue | Fixed capacity, at most four producer CAS attempts; a full queue drops an event rather than waiting | `callback_ring.h:22-72` |

The `std::ofstream`, JSON construction, vector growth, `Sleep`, and ASIO stop/reopen calls observed in these files are outside the callback paths. The probe counts dropped events; a run with drops cannot prove uninterrupted callback history. The per-buffer loop in loopback remains proportional to the requested buffer size, so the live cadence and 60-minute signal tests are still needed to establish deadline behavior. The queue asserts lock-free index atomics at compile time; this review has not measured the cost of Windows `QueryPerformanceCounter` on every target CPU.

The three modes already reject queue overflow. This audit also added a post-flush stream check, so a failed callback-event or stage-file write is reported as a test failure. The probe built successfully without loading an ASIO driver. The existing five-minute cadence report has 14,046 callback records for 14,046 callbacks and zero dropped events, so this change does not invalidate that passing result. The rebuilt probe's SHA-256 is recorded in `tests/results/test-build-manifest.json`.

No live ASIO test or route change was performed for this audit while the user is trying the test build.
