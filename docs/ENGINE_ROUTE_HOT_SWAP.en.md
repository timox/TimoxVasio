# Updating routes without interrupting the ASIO clock

[Français](ENGINE_ROUTE_HOT_SWAP.md) | **English**

## Observation

The `multiclient` run of October 9, 2026 measured shared pauses of 219 ms when a client stopped and 318 ms when it returned, in `tests/results/multiclient-20261009-151756-085-548ce1c8/`. The sample positions of the surviving clients stayed continuous. The controller called `ApplyConfigurationCore` whenever the resolved routes changed; that function stops and reopens the physical interface before rebuilding the graph.

## Intended path

When the physical driver, sample rate and buffer size stay the same and the physical channels required by the new routes are among those already open, the controller builds a new `AudioRoutingRuntime` on its worker thread. It then publishes its pointer atomically for the physical callback. The callback only performs an atomic load and processes the selected graph. The controller waits for callbacks already in flight to finish before destroying the old graph. No additional lock, allocation or system call is introduced into the callback.

If the format changes, if a new physical channel must be opened or if no route remains active, the existing path that reopens the interface is still used. The physical channels opened at start therefore form the allowed superset for hot swaps; a route cannot use a physical ASIO buffer that was never created.

## Verification

Test the atomic switch and the lifetime of the old context in an isolated concurrency test. `RealtimeCallbackSlotTests` checks that the old context stays alive until the end of the callback; `AudioRoutingHotSwapTests` pushes two different graphs through the same call point with distinct output samples. Both tests pass in Release x64. Then rerun mode 5 with 2, 4 and 8 clients and the same thresholds. The cadence logs of the surviving clients must show that the shared pauses disappear on closes and opens. The development engine currently used by Mixxx and SuperCollider will not be replaced during their session.
