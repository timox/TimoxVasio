# vasio-doctor

Independent, read-only Windows inventory. It reads the ASIO registry in both
32-bit and 64-bit views, resolves each CLSID to its DLL and PE architecture,
lists processes with a registered ASIO DLL loaded, reads Windows audio endpoint
names, default roles, shared-mode format (rate, channels, sample encoding and
bit depth), PnP driver versions, and enumerates WinMM MIDI
ports. It checks Windows MIDI Services only when its service is already running.
It never changes the registry, device format, driver registration, MIDI service,
or TimoxVasio configuration.

For TimoxVasio, `timoxDlls` records the on-disk SHA-256 and Windows file version
of each DLL path found in the registry, loaded-process scan, or v7 status. The
verdict compares hashes: two paths containing identical bytes are one build;
two different hashes are reported even if both files claim version 1.1.1. A
file that cannot be read produces an orange, unverified finding. The hash is
of the file currently at that path; Windows does not expose the loaded module
bytes through this check, so a file replaced after a client loaded it still
requires restarting that client to establish its actual build.

```powershell
pwsh -File tools/vasio-doctor/vasio-doctor.ps1 --json
pwsh -File tools/vasio-doctor/vasio-doctor.ps1 --output tests/results/doctor.json
pwsh -File tools/vasio-doctor/vasio-doctor.ps1 --expected tools/vasio-doctor/expected.example.json
pwsh -File tools/vasio-doctor/vasio-doctor.ps1 --listen "MIDI port name" 30
pwsh -File tools/vasio-doctor/vasio-doctor.ps1 --probe "ASIO4ALL v2" --clock-seconds 60
pwsh -File tools/vasio-doctor/vasio-doctor.ps1 --probe "ASIO4ALL v2" --against "SSL ASIO Driver 1" --clock-seconds 600
```

`--listen` explicitly opens one WinMM input until the requested seconds elapse.
It counts MIDI Clock, Start and Stop messages and derives tempo and clock
jitter from QueryPerformanceCounter. An open failure is reported as a possible
exclusive-port conflict.
The default inventory briefly opens and closes each WinMM port without starting
capture or sending MIDI data. WinMM error 4 is reported as an exclusive-port
conflict; other opening errors are reported separately because they do not prove
that another application owns the port.

`--probe <driver>` refuses a driver currently loaded by an inspected process.
For buffer acceptance, it tests only sizes allowed by the driver's declared ASIO granularity: powers of two for `-1`, the preferred size for `0`, or steps from the minimum for a positive value.
A rate or buffer is marked usable only after the requested rate is confirmed, buffers are created, playback starts, callbacks arrive, and the lifecycle report passes. Missing callbacks produce an inconclusive finding instead of a false hardware-refusal verdict.
It records declared capabilities, short lifecycle, 60-second cadence, each
declared supported rate tested with `setSampleRate` and restored, representative
buffer sizes tested with `createBuffers`, an optional live `setSampleRate` trial
while callbacks are running, and a second opening while a first instance is
running. A host-requested rate change is recorded separately from a
driver-initiated reset; absence of a notification to the host about its own
request is not treated as a protocol defect. ASIO has no host-side buffer-size
setter, so the driver-initiated buffer-reset check remains explicitly untested
until a control-panel or device event can trigger it. It also measures sample-position drift against QPC for
10 minutes by default (`--clock-seconds 60` is the minimum fast check) and
records a block-based empirical uncertainty interval. Results and individual trial reports are saved under
`tests/results/profiles/`. A failed restoration stops further trials. A live
driver-initiated buffer change while clients are running is still untested;
the live-rate trial records the result of a host-initiated `setSampleRate`.
Only a cadence report with a passing verdict and zero process exit code can
contribute its regression to the clock-source conclusion. An inconclusive run
may contain a fitted number for debugging, but the profile marks that
measurement unusable and reports the reason.
This is not yet a full compatibility verdict. `--probe` performs temporary ASIO API
calls on the selected driver; the doctor does not change persistent system or
TimoxVasio configuration.

Clock identification currently records the ASIO clock-source declaration and
the measured QPC regression. It also correlates ASIO channel names with active
Windows endpoint names to suggest an underlying device for wrapper drivers;
this is a heuristic, so confirm the selected endpoint in the driver's panel.
For TimoxVasio, `cadenceSource` identifies the physical ASIO driver selected by
the engine and whether its callback is active. The virtual callbacks follow
that physical callback; the profile does not infer the selected device's
oscillator from its name alone. See `docs/archive/VASIO_CLOCK_SOURCE_TRACE.md`.
The USB isochronous endpoint descriptors and
independent cross-driver comparison are not both guaranteed; the conclusion is
explicitly `indéterminée` until enough evidence exists. The separate
`vasio-usb-descriptors` reader queries present USB hubs with read-only IOCTLs,
matches VID/PID from the Windows endpoint and records audio streaming endpoint
synchronization and explicit or implicit feedback. Build it with
`cmake --build build_vasio_probe --config Release --target vasio-usb-descriptors`
(or pass `--usb-path`). An ASIO wrapper such as
ASIO4ALL must be matched to its selected Windows endpoint before attributing
the measured clock to a physical device.

`--against` starts two `vasio-probe cadence` processes together, one for each
driver, after the primary profile. It refuses the run if a selected driver is
loaded or process inspection cannot establish that it is free. Both reports
must contain raw sample-position/QPC polls with sufficient overlapping
acquisition time. `vasio-clock-compare` refits both clocks using only their
shared QPC window. The profile records relative ppm, an empirical interval,
and the estimated time to drift by one primary buffer. Build the comparator
with `cmake --build build_vasio_probe --config Release --target vasio-clock-compare`
or pass `--compare-path`.
The command does not prove that two ASIO names correspond to independent
physical clocks. Confirm each selected endpoint, especially for ASIO wrappers.

The doctor calls the installed Windows MIDI Services SDK for endpoint and
legacy-port correlation only when MidiSrv is already running. On the current
development PC, the SDK activation returns `REGDB_E_CLASSNOTREG`; the doctor
marks that inventory incomplete instead of silently treating it as empty.
WinMM ports remain visible. It does not launch the separately installed
`midi.exe` console, which has produced an application exception on this PC.

Expected MIDI ports are matched against WinMM first, then Windows MIDI Services
when its endpoint inventory is complete. If a port is absent from WinMM and
Services enumeration failed, its presence is unverified (orange), not missing
(red). A Services-only name can establish presence, but its requested input or
output direction remains unverified until the SDK provides that mapping. Edit
`expected.example.json` with the actual names of any virtual Mixxx → SC and
SC → follower ports before using it as a full system expectation; those names
are not fixed by this repository.
