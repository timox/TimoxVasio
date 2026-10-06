# Single TimoxVasio driver, 256 channels per direction, and a physical ASIO clock

[Français](2026-10-03-vasio-single-driver-256-channel-design.md) | **English**

## Context and decision

The previous model exposed four COM DLLs, VASIO1 through VASIO4, each with six inputs and six outputs. That separation was chosen for routing convenience, but limited each application and multiplied driver identities. The new model exposes one ASIO DLL, `TimoxVasio.dll`, registered as `TimoxVasio`, with a maximum capacity of 256 inputs and 256 outputs. ASIO distinguishes the two counts in `getChannels`.

Baseline capacity is 256 inputs and outputs. An API profile keyed by executable name can reduce the advertised counts independently for a given application; applications without a profile keep 256/256. The initial `mixxx.exe` profile is 255/255 to work around the stable version's channel-count representation. Profiles are read before `getChannels` and apply to `getChannels`, `getChannelInfo`, and `createBuffers`. They do not change the shared transport's 256 slots. Each application then selects its actual channels through `createBuffers`; the engine tracks active channels separately for each process. PID and process name identify clients in the API inventory.

The selected physical ASIO driver is the master clock. Its sample rate and a buffer size selected from its capabilities become the effective values for the graph and TimoxVasio driver. TimoxVasio advertises only those values; an application request for another rate or size is rejected. Interprocess rings transport blocks between callbacks; they do not perform sample-rate conversion or implement independent block-size policies.

The native engine is distributed as `TimoxVirtualAsioEngine.exe`. It remains separate from `TimoxVasio.dll` and controls the physical device and client mappings.

## Memory cost

The transport retains two rings per client, each containing 16,384 float32 frames for 256 channels. Their contents total exactly:

```text
2 directions × 16,384 frames × 256 channels × 4 bytes = 33,554,432 bytes = 32 MiB
```

Mapping pages are shared between the client application and engine; they are not duplicated for those two processes. Each client process has its own mapping: two full-capacity applications use 64 MiB for rings, and four use 128 MiB. ASIO double buffers and graph work buffers add to those figures and depend on block size and active channels.

## Client inventory and transport

- The shared format holds 256 input and 256 output slots, plus masks or lists of channels actually supplied to `createBuffers`.
- API channel numbers remain one-based; `ASIOBufferInfo::channelNum` remains zero-based.
- The engine exposes a client's endpoints only for its active channels. A host allocating just a few channels therefore does not create hundreds of usable ports without buffers.
- `createBuffers` accepts up to 512 structure entries total, provided each index is below 256 and is not duplicated in the same direction.
- The shared-memory protocol gets a new version. An old six-channel driver and the new engine cannot attach to the same mapping.
- The engine processes only a client's active channels when copying blocks and constructing the graph.

## Physical host and graph

The API inventory for a physical device reflects the counts returned by its ASIO driver. It exposes available physical ports, while `PhysicalAsioHost` creates buffers only for the union of physical channels referenced by applied routes. Hardware channel indices remain those returned by the driver; no implicit remapping occurs.

When a configuration contains at least one route but that union is empty (for example, a virtual-to-virtual route), some ASIO hosts reject `createBuffers` with zero descriptors. The engine then creates one internal clock buffer on the first available physical input or, failing that, the first output, which it keeps silent. This buffer is neither an API endpoint nor a route and does not change the set of routed physical channels. With an initial configuration containing no routes, the physical driver remains open and its capabilities are published, but the ASIO stream does not start until a route is applied.

Three link types pass through the same typed `RoutingGraph`:

1. A process's TimoxVasio output to another process's TimoxVasio input.
2. TimoxVasio output to physical output.
3. Physical input to TimoxVasio input.

The physical callback is the clock and main processing call. Each TimoxVasio client advertises and creates buffers at the same sample rate and size as the selected physical engine. Any change in device, rate, buffer size, client, or route stops the stream, rebuilds buffers and graph, then restarts or leaves the engine stopped in error.

After selecting the physical sample rate, the engine reads capabilities that may vary with that rate again, notably `getChannels` and `getBufferSize`, before publishing the inventory and creating buffers. Physical capacity is not hard-coded. For SSL 12, SSL documents 12 inputs and 8 outputs; its ADAT input can provide 8 channels at 44.1/48 kHz, 4 at 88.2/96 kHz, and 2 at 176.4/192 kHz ([specifications](https://www.solidstatelogic.com/products/ssl-12), [guide](https://support.solidstatelogic.com/hc/en-gb/articles/5568765809309-SSL-12-User-Guide)).

## API and interface

- The inventory contains one virtual driver, `TimoxVasio`.
- `GET` and `PUT /api/v1/application-profiles` read and replace the complete list of per-application limits. Valid counts range from 1 to 256; an unlisted application keeps 256/256. `PUT` returns active clients that must restart to pick up a profile change.
- Client state includes its PID, application name, and actually allocated input/output channels.
- Virtual IDs have the form `virtual:TimoxVasio:<pid>:input:<channel>` and `virtual:TimoxVasio:<pid>:output:<channel>`.
- Global configuration selects the physical driver, an advertised sample rate and buffer size, and complete routes. It contains no independent TimoxVasio rate or buffer size.
- The state API exposes confirmed physical values after apply; TimoxVasio ASIO getters advertise only those same values. Incompatible rate or buffer requests are rejected before changing the client mapping.
- The interface displays API-discovered ports and connects virtual or physical sources to permitted destinations.
- `TimoxVasio` remains enumerable by ASIO hosts while the engine is stopped; metadata initialization creates no audio clock. Buffer allocation requires connection to the engine and its physical clock, otherwise the driver returns an explicit connection error.
- `configuration.apply` interrupts routing during any effective change. The API publishes state transitions and a structured error on failure.
- Installation registers one driver. Migration removes obsolete VASIO registrations so four names do not continue to appear as independent engines.

## Required verification

- A COM probe observes 256 inputs and 256 outputs on the single driver.
- A probe running as `mixxx.exe` observes 255/255, while an unprofiled host observes 256/256; `createBuffers` rejects indices above the advertised count in either direction.
- A probe creates buffers on low, high, and sparse indices in both directions and validates transported samples.
- Two simultaneous ASIO processes each have their own active channels and endpoints without identifier collisions.
- Graph probes verify all three link types with physical channel indices above 6.
- The physical probe verifies that the engine creates only routed buffers and rereads counts, rates, and sizes after applying the rate.
- TimoxVasio probes verify that `getSampleRate` and `getBufferSize` reflect confirmed physical values and reject other requests.
- The API and interface show clients, active endpoints, physical capabilities, and routing interruptions.
- A real ASIO host, especially Mixxx, validates discovery and allocation beyond channel 6. Hardware acceptance observes a signal on real SSL 12 and ADAT channels; building or enumerating a driver is insufficient.

## Limits and measurements

The advertised limit of 256 per direction is the TimoxVasio driver's ceiling, not the physical device's channel count. The API presents only physical channels actually returned after rate selection. Drop counters and signal/latency measurements must establish behavior on a machine, without inferring latency from a simple sum of buffer sizes.
