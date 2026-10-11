# vasio-status

Read-only observer for TimoxVasio shared-memory client status. The executable opens
the `Local\\VASIO-Map-v7-<PID>-1` mapping with `FILE_MAP_READ`, reads process names
from Windows, and optionally reads active routes from the local engine HTTP API.
It never changes routes, starts audio, or writes to shared memory. History is
written only when `--history` is supplied.

Build x64 with the project's CMake setup, then run:

```powershell
.\build_vasio_probe\Release\vasio-status.exe --json
.\build_vasio_probe\Release\vasio-status.exe --seconds 60 --history tests\results\status.jsonl
.\build_vasio_probe\Release\vasio-status.exe --once
```

Options: `--json` emits JSON Lines; `--once` emits one observation;
`--seconds N` limits a live observation; `--interval-ms N` sets its polling
interval (minimum 100 ms); `--api-port N` selects the local engine API port
(default 52525); `--history PATH` appends every observation as JSON Lines.
An open or write failure exits nonzero, so a matrix run cannot silently accept
an incomplete history.
Without `--json` or `--once`, it prints continuously until Ctrl+C.

Status layout v7 reports the DLL path and version, client state and channel
masks, requested and confirmed rate and block size, QPC callback timing,
successful exchanged frames, transport error counters, and reset request count.
`callbackCount` includes callbacks even when the output write fails;
`successfulExchangeCount` and `lastExchangeQpc` advance only after a successful
write. `lastCallbackQpc` tracks callback cadence separately.
`lastExchangeAgeMs` is null until a successful exchange has been observed.
`measuredPeriodMs` and `driftPpm` are null until at least two callbacks with
valid QPC timestamps have arrived; zero would incorrectly claim a measured
zero drift. `expectedPeriodMs` is null until the confirmed format is known.
`errors.missedBufferOperations` sums the four transport underrun/overrun
counters. One audio block can cause more than one operation error, so this is
an operation count rather than an exact count of distinct lost blocks.
The test v7 driver requests an ASIO reset when the confirmed rate or buffer
changes while its old buffers are running. Its JSON marks
`resetRequestsSupported: true` and counts delivered requests. A v6 client is
reported as `legacy` with `resetRequestsSupported: false` and no
detailed status. A vanished client is detected from observation history, or
from an engine client PID absent from the Windows process snapshot. The latter
requires the engine API. Routing diagnostics also require that API.
