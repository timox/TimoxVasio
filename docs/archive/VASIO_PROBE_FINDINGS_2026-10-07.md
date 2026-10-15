# TimoxVasio probe findings — 2026-10-07

> **ARCHIVE** — Document historique conservé pour référence. Il ne décrit pas l'état courant de TimoxVasio (1.2.1). Voir le [README](../../README.md) et la documentation courante qu'il référence.

## Intermittent missing callback during DLL reload

Environment: registered `TimoxVasio.dll` from `C:\Program Files\Steinberg\VirtualASIO\1.1.1`, engine configured with SSL ASIO Driver 1 at 48,000 Hz and 1,024 frames. No Renoise or Mixxx process was open. A muted temporary route connected `vasio-probe.exe` output 1 to physical output 1; the script restored the original 20 routes and 3 application profiles afterward.

Minimal observed reproduction:

1. From this repository, run `pwsh -NoProfile -File tests/with-probe-route.ps1 -LifecycleCycles 1000` with the engine API available at `127.0.0.1:52525` and no other virtual ASIO clients.
2. The standard 1,000-cycle lifecycle run passes.
3. In the first DLL reload run, `init`, `createBuffers`, and `start` returned success in cycle 5, but zero callbacks arrived during the configured 5,000 ms wait. Earlier reload cycles each received three callbacks. The probe stopped, disposed the buffers, and unloaded the DLL successfully.

Evidence: `tests/results/20261007-172334-504-5cb48507/lifecycle-reload.json` and its stage log. The standard run in the same directory completed 1,000 cycles and 3,000 callbacks. A second identical full run in `tests/results/20261007-172555-400-353e99a9/` completed both standard and reload variants (1,000 cycles and 3,000 callbacks each), with no ring overflow or handle growth. This shows an intermittent failure; the available evidence does not isolate whether the missing callback originates in the DLL, the engine, or the physical host.

The probe reports the first run as `inconclusive` because `start` succeeded but the callback prerequisite was not met. Capture engine state and diagnostics *during* a recurrence to narrow the cause; logs collected afterward contained no explicit audio error.

## The engine stops callbacks when the last route disappears

Environment: the installed 1.1.1 driver and engine, SSL ASIO Driver 1, 48,000 Hz, 1,024 frames, with no other TimoxVasio clients. A temporary route connects `vasio-probe-send.exe` output 1 to `vasio-probe-receive.exe` input 1. The wrapper restores routes and application profiles after every run.

Minimal reproduction:

1. Use the earlier two-process test configuration with only the sender-to-receiver route, then terminate one client after three seconds. The current wrapper adds a muted clock route for this variant and therefore no longer reproduces this exact no-route condition.
2. The two probes exchange clean blocks before the receiver is killed. The sender remains alive for the requested 12 seconds.
3. Observe that the sender receives only 32 callbacks after the receiver dies, versus about 417 expected for the remaining time. Its last callback occurs about 0.7 seconds after the kill, while the client remains visible in the engine state. The wrapper now fails this run based on callback coverage (7.7%, below the configured 80% minimum).

Evidence: `tests/results/loopback-session-20261007-212351-470-d9510e71/summary.json`, `send.json`, `send.json.events.jsonl`, and `status.jsonl`. The reciprocal test in `tests/results/loopback-session-20261007-212444-075-7dbba106/` kills the sender: the receiver gets only 31 callbacks versus about 270 expected after the kill (11.5% coverage), then stops receiving callbacks while still present. Normal runs in both start orders pass: `tests/results/loopback-session-20261007-212159-727-7651267e/` has 401 bit-identical blocks and `tests/results/loopback-session-20261007-212427-378-ae4b6663/` has 453, both with constant 1,024-sample transport latency.

The engine removes the resolved route when either endpoint disappears. `AudioController::ApplyConfigurationCore` leaves the physical host stopped when no routes remain, which is consistent with the observed callback loss. This is a code-based explanation, not a DLL failure. With a separate muted route that keeps the physical clock running, both kill directions pass: sender survival has 408 callbacks after the receiver exits, versus 418 expected (97.5% coverage), in `tests/results/loopback-session-20261007-213903-211-79b3a9a0/`; receiver survival has 271 versus 279 expected (97.0% coverage) in `tests/results/loopback-session-20261007-213920-877-41402caa/`. The receiver reports corrupted samples after its signal source dies because the remaining samples are silence; its pre-kill samples are clean. The test has not changed the driver or engine. The desired behavior for an otherwise unrouted surviving client remains a product policy decision.

## Intermittent callback jitter in a two-client run

Run `pwsh -NoProfile -File tests/with-multiclient-route.ps1 -ClientCount 2 -Seconds 20 -Seed 42` with the installed 1.1.1 driver, SSL ASIO Driver 1 at 48,000 Hz and 1,024 frames, and no other virtual clients. The wrapper creates a separate muted clock route, opens sender and receiver, kills the sender after the first third of a common 20-second measurement window, then restores the original configuration.

One run, `tests/results/multiclient-20261007-215014-221-5a74533e/summary.json`, measured 7.64 ms p99 callback jitter on the surviving receiver after the kill, above the configured 25% buffer-period limit of 5.33 ms. It had no sample-position step errors and 98.6% callback coverage. The exact same command immediately afterward passed with 4.34 ms p99 jitter in `tests/results/multiclient-20261007-215121-491-045a7239/summary.json`. Separate 4- and 8-client runs passed within the common measurement window. This is intermittent; current evidence does not attribute it to the DLL, engine, physical ASIO driver, or OS scheduling.

The integrated matrix reproduced the 2-client jitter failure at 8.60 ms p99 in `tests/results/20261007-215252-944-95eb28cf/multiclient-2/summary.json`; the same session passed 4 and 8 clients. The matrix correctly stopped before mode 6 and restored the original 16 routes and 3 application profiles.
Its receiver event log includes a 208.77 ms callback gap about 0.54 seconds after the sender was killed, plus shorter late and early intervals later in the run. Sample position still advanced by exactly 1,024 frames across these intervals, so this is a timing interruption rather than a sample-position jump.

On 9 October, the new kill-and-restart variant passed at 2 clients (`tests/results/multiclient-20261009-151708-861-23d05cdd/summary.json`) but failed at 4 clients (`tests/results/multiclient-20261009-151756-085-548ce1c8/summary.json`): two unaffected clients had 5.91 and 5.52 ms p99 callback jitter, above the 5.33 ms limit. Their event logs show simultaneous gaps of about 219 ms 0.55 s after the kill and 318 ms 0.73 s after the restart, with no sample-position step errors or sample corruption. The run restored all 18 routes, 3 profiles, 48 kHz and 1,024 frames. The user reopened Mixxx immediately afterward, so the 4-client repetition and 8-client variant were not run.

Code inspection found the direct cause of the common pause: on every resolved-route change, `AudioController::Run` called `ApplyConfigurationCore`, which stops and closes the physical ASIO host before rebuilding the runtime. A test-only engine change now builds a replacement routing runtime on the controller worker and atomically publishes it to the physical callback when the format stays fixed and the new physical channels are already open. It passed an isolated callback-lifetime test and the short SSL trials described below; it has not been installed or packaged.

On 2026-10-09, the isolated `build_status_v7/Release/TimoxVirtualAsioEngine.exe`
(SHA-256 `A3A04B1A5BF36BD730D59FB4C49294EFA9677103493CC6483AB24618DA535E2A`)
was temporarily launched against the SSL at 48 kHz / 1,024 frames. The
4-client kill/restart trial passed in
`tests/results/multiclient-20261009-161539-717-8d57707e/summary.json`:
survivors had no missing/duplicated samples, no sample-position step errors,
and p99 callback jitter at most 3.34 ms. The 8-client trial passed in
`tests/results/multiclient-20261009-161626-198-392a4444/summary.json`:
survivors had no missing/duplicated samples, no position errors, and p99
jitter at most 3.51 ms. The test verdict ignores the terminated client's
raw report and evaluates its restarted instance after the documented grace
window. These are 20-second trials, not the required 5- or 60-minute runs;
the 2-client variant was not rerun with this engine. Both scripts restored
all 18 routes, application profiles, 48 kHz and 1,024 frames. The canonical
`build_codex_110/Release/TimoxVirtualAsioEngine.exe` (SHA-256
`5AAC8C186BFD0914DC66A4F01E110ADA0AA2728618E2A1BBA0D85E3C7A42C299`)
was restored afterward. No installed driver or packaged application changed.

## Live rate and buffer changes are not announced to ASIO clients

Minimal reproduction with the registered 1.1.1 driver and SSL ASIO Driver 1:

1. Start from 48,000 Hz / 1,024 frames with no other virtual ASIO clients.
2. Run `pwsh -NoProfile -File tests/with-multiclient-route.ps1 -Reconfigure -ClientCount 2 -Seconds 30 -Seed 42`.
3. The script starts an ASIO sender and receiver, applies 44,100 Hz while both run, then applies 512 frames, records their `asioMessage` and `sampleRateDidChange` callbacks, and restores the original configuration.

Evidence: `tests/results/multiclient-20261007-215857-234-6b8ab83c/summary.json` and the two `.events.jsonl` files. The engine reported `running` at 44,100 Hz / 1,024 frames and then 44,100 Hz / 512 frames. Each client received hundreds of audio callbacks after both changes, but **zero** `kAsioResetRequest` messages and **zero** sample-rate notifications. The ASIO SDK header in this repository describes `kAsioResetRequest` as the mechanism for a driver to request that the host close and reopen it after a sample-rate or other basic configuration change; it says buffer-size change should currently use that reset request too. The receiver's raw signal verdict failed after reconfiguration. The test restores 48,000 Hz / 1,024 frames, 16 routes and 3 profiles.

`VASIODriver::callbackLoop` currently waits for audio blocks and does not compare the shared transport generation or notify the host when the engine changes its rate or buffer. The test changed neither the driver nor the engine. Under the user's minimal-DLL constraint, any later protocol fix needs separate approval of a design that stays within the driver's permitted state-publication scope, or a decision to prohibit live reconfiguration while clients run.

## ASIO4ALL profile and independent reference check — 2026-10-08

`vasio-doctor --probe "ASIO4ALL v2"` accepted 44,100, 48,000, 96,000 and 192,000 Hz, rejected 88,200 and 176,400 Hz, and created buffers at 64, 128, 256, 512 and 2,048 frames. It restored the prior rate after each trial. A second process initialized and opened the driver while the first cadence process was running. See `tests/results/profiles/ASIO4ALL_v2.json`.

ASIO4ALL delivered `bufferSwitch` callbacks but no valid `TimeInfo` positions. A separate 60-second measurement in `tests/results/asio4all-cadence-60s.json` counted 5,125 callbacks at 44,100 Hz / 512 frames. The mean interval was 11.610 ms versus 11.610 ms theoretical, but p99 absolute jitter was 3.631 ms, above the configured 25% period limit (2.902 ms). Three intervals were over 1.5 times the nominal period. `ASIOGetSamplePosition` advanced monotonically in 5,741 successful polls; it cannot establish every callback's position step without `TimeInfo`. The first-to-last position estimate of -2,273 ppm was strongly biased by short-window endpoint quantization and should not be interpreted as a hardware clock rate. A subsequent 60-second regression over 5,712 position polls, `tests/results/asio4all-clock-regression-60s.json`, estimates -53 ppm with a broad empirical uncertainty interval of -440 to +334 ppm. Its callback p99 jitter was 3.624 ms, again above the configured limit. A longer, independent-device run is required for a useful hardware comparison. These are ASIO4ALL-on-Windows results, not TimoxVasio failures.

The ASIO4ALL channels named `ADAT 1/2` and `Output 5/6` match active Windows endpoints of the **SSL 12 USB Audio Device**. Thus the current ASIO4ALL profile is using the same physical card as the engine's `SSL ASIO Driver 1`; it is not an independent clock for mode 7. The user selected a USB headset as the intended reference. It was not present in the Windows audio device inventory during this check. Mode 7 must wait until the headset is connected and selected in ASIO4ALL, and mode 6 currently fails the required reset-notification check.

The read-only USB hub query in `tests/results/ssl-usb-descriptors.json` found one `VID_31E9/PID_0005` device across 15 accessible hubs. Its configuration descriptor contains an asynchronous host-to-device audio stream and asynchronous device-to-host streams marked as implicit-feedback data. This is direct descriptor evidence for the SSL USB transport, but it does not prove which ASIO clock source the SSL driver has selected or whether an external digital source is locked. The doctor therefore keeps the clock conclusion indeterminate until that state and an independent simultaneous comparison are available. The USB query follows Microsoft's documented hub `IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION` path.
