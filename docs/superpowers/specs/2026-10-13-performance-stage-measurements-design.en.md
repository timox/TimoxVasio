# Stage measurements in the Performance tab — 1.2.1

[Français](2026-10-13-performance-stage-measurements-design.md) | **English**

## Validated goal

Show in the Performance tab durations actually observed at the stages of the audio path, over five-second windows. Each figure must be attached to the stage it describes, with its sample count and a short explanation. The additional instrumentation is off at startup and can be enabled from the interface. No automatic buffer, phase or routing adjustment follows from it. The target delivery is version 1.2.1.

## Measurement points and meaning of the figures

1. **ASIO application and virtual driver output.** In the DLL, measure separately the time spent in the application's `bufferSwitch` or `bufferSwitchTimeInfo` call and the time to copy its output block into the shared ring. The first figure includes everything the host does during that call; it does not claim to isolate its DSP. A measurement is published only if the call and the block write complete normally.
2. **Ring wait.** Attach to the written block a first-frame number, its size, its audio epoch and a QPC timestamp taken at publication. When the engine reads it, compute the interval only if the read matches that block and epoch entirely. A partial read, an underrun, an overrun or a reconfiguration discards the sample. This is the time from publication to read, not an acoustic latency.
3. **Engine processing and handoff to the physical driver.** In the physical callback, measure separately the clients' read/de-interleaving, the execution of the routing graph, and the conversion/copy into the driver's output buffers. Keep the existing measurement of the complete physical callback and its period budget. The handoff to the physical buffer measures neither hardware latency nor the moment sound reaches the output.

These durations are presented separately. They are not summed into a "total latency": the virtual and physical callbacks run asynchronously, and a sum of window maxima would describe no real block.

## Bounded-cost instrumentation

An atomic switch read by the audio paths controls every new timestamp. It is off when the engine starts and on every restart. While off, no new QPC call, no block metadata write and no aggregate computation happen in the callbacks; only the switch check remains. The existing `bufferHealth` measurement is unchanged.

Once enabled, sample at most one callback in sixteen per producer. Counters, sums and maxima are fixed-size, prepared outside the callback and published without allocation, lock, logging or network call from an audio thread. Ring timestamps are kept in fixed-size slots tied to frame positions; the producer writes the metadata before publishing its index, and the consumer checks them before releasing the space. The shared transport protocol moves from v8 to v9: the 1.2.1 DLL and engine must ship together, with no legacy compatibility path. The switch is published in the shared status by the engine; the DLL reads it without any network query.

The control task consumes the aggregates in complete five-second windows. It resets the results on stop, on disable, on an audio epoch change or on a configuration change that makes the samples incomparable. Each published duration carries `samples`, `meanNs` and `worstNs`. A result without observation is `null`, never zero. Counters of excluded blocks are published to explain gaps in the ring wait.

## API contract

The documented API is changed before its producers and consumers: `API.md`, `openapi-v1.json`, `schemas/api-v1.json` and contract tests. The `performance.measurements.set` WebSocket command takes exactly `{ "enabled": true | false }` and uses the existing command response format. Its state and the latest complete window appear in `GET /api/v1/state` under `performanceMeasurements`. That object always contains `enabled`, `sampleEveryCallbacks: 16` and `window`. When measurements are off or no complete window exists, `window` is `null`.

A window contains `durationMs: 5000`, `clients` and `physical`. Each `clients` entry carries `processId`, `driverId`, `hostCallback`, `ringWrite`, `ringWait` and `ringWaitDiscardedBlocks`. `physical` carries `clientRead`, `routing` and `outputCopy`. Each duration is either `null` or `{ "samples": positive integer, "meanNs": non-negative integer, "worstNs": non-negative integer }`. A client without samples keeps its identity and a `null` duration. Numbers are integer nanoseconds; the interface chooses µs or ms for display. The schema rejects the old incomplete format once this contract is introduced.

Components exchange this data only through the versioned transport and the documented API. The interface inspects neither shared memory, nor local files, nor engine internals. The command is idempotent: repeating the same value does not restart a session and does not duplicate counters.

## Presentation

The top of the tab keeps the physical callback budget already measured. Immediately below, three groups follow the real path: application and virtual copy; block wait; routing and physical copy. Each value shows mean, worst, samples and window. The engine group details the measured sub-stages without hiding the complete callback figure. The "Measure stages" switch and the collection state appear before the groups. If collection is stopped, in its first window or without an active path, the view says so and invents no value. Explanations stay next to the figures.

## Verification and delivery

Verify the API contract and the command's idempotence, block pairing across the ring wrap-around, partial reads and epoch changes. Verify with a targeted test that the disabled mode runs no new timestamp, and compare the cost of disabled/enabled modes on a bench outside any real audio session. Verify the rendering of the no-data and complete-window states. Tests must validate the target contract without keeping the old one.

Before any validation with audio processes, record their identity, full command line, owning project and link to the session. The user's authorisation allows stopping only the relevant processes after that check. Rebuild the DLL, the engine and the interface; restart the affected processes after a runtime change. Check the origin and SHA-256 of the packaged binaries. Prepare and publish version 1.2.1 with the separate DLL and the Electron executables, with updated documentation and release notes.
