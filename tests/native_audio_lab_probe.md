# Native audio laboratory probe

`NativeAudioLabProbe` is a separate executable. It does not activate the native
backend in AudioController, Electron, the permanent engine or the ASIO DLL.

```
NativeAudioLabProbe --help
NativeAudioLabProbe --fake --seconds 2
```

No arguments and `--help` start neither a clock nor a transport. `--fake` uses an
in-process PCM32 stereo source/sink, the real native worker and SRC, the real
software clock, and the common routing runtime. Its explicit fixture routes left
at -6.0206 dB and right at unity. Expected output is approximately +0.125/-0.25;
the check proves channel/gain transfer, not signal fidelity or latency.

The session harness publishes through `RealtimeCallbackSlot`. On detach/failure
it publishes an empty runtime, waits for callback quiescence, retires the old
runtime, then stops/joins the backend. Desired routes remain in the lab owner;
replacement uses a fresh local epoch even if the driver's wire generation repeats.
Route-only changes reuse the live backend and IO. Missing, denied and already-owned
transports retain distinct statuses and do not stop the selected software master.
The harness creates no automatic retry, production persistence or broker policy.

On a **separately authorized dedicated target only**, a manual permitted caller
may run `NativeAudioLabProbe --native --seconds 10`. It opens the N2 control
transport with no routes. Adding `--loopback-audio` explicitly routes Windows
playback to Windows recording with the same stereo gains as the fake fixture.
It never requests elevation, installs a driver, changes TESTSIGNING, changes audio
defaults, or launches/stops the permanent engine. Access denied is not driver absence.
Ctrl+C/Ctrl+Break request normal shutdown. Duration is bounded to 1..600 seconds.

JSON reports session completion and counters, with `physicalQualified:false` in
every mode. `sessionCompleted` requires the final health snapshot to report
Running without an error, callbacks, and (in fake mode) the expected PCM fixture.
It describes that snapshot, not a guarantee against a failure between the
snapshot and Stop. Native session completion alone does not prove Windows applications
played/recorded audio. Native device loading, real duplex audio, suspend/removal,
Driver Verifier, ten-minute endurance, CPU and latency distributions remain target
qualification work. Fake runs provide no evidence for these claims.

The native bindings add no transport operation, lock, syscall or shared-reference
change to the graph callback. This is not a claim that every existing callback
path is syscall-free: ASIO client signaling and optional stage timing already exist.
