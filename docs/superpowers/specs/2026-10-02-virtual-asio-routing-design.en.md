# Design: virtual ASIO driver and application routing

[Français](2026-10-02-virtual-asio-routing-design.md) | **English**

> **Earlier design — historical state.** The routing principles remain useful context, but the verified state below and driver/executable names are outdated. For the current contract and acceptance criteria, see the [TimoxVasio specification](2026-10-03-vasio-single-driver-256-channel-design.en.md) and [master plan](../plans/2026-10-03-vasio-256-physical-master-plan.en.md).

## Goal

Provide a Windows x64 virtual ASIO driver usable by audio applications, advertising up to 256 inputs and 256 outputs, and a graphical application for building, modifying, and diagnosing circuits among those applications' active channels and a hardware ASIO driver's physical inputs and outputs.

The selected hardware ASIO driver dictates audio transport: the engine adopts its effective sample rate and block size, then presents those same values to applications connected to the virtual driver. Incompatible requests are rejected. The graph can connect virtual applications to one another or to physical inputs and outputs; each application's active virtual channels are determined by the buffers it actually requests, up to 256 per direction.

The initial source scope covers applications explicitly selecting VASIO as their ASIO driver. Capturing shared Windows outputs (WASAPI) is outside it.

## State verified on October 2, 2026

- The default CMake target builds `VirtualASIO.exe` under VS 2026 and embeds the local cpp-httplib HTTP/WebSocket server. `configuration.apply` remains disabled until the audio controller is connected.
- `VASIO_BUILD_DRIVERS=ON` builds four COM DLLs. The Steinberg enumerator discovers and instantiates them after installation.
- The DLLs use a versioned shared mapping. `DriverAudioProbe` attaches a test engine, fills input buffers, and checks output buffers on six channels for all four DLLs. Without an engine, `init()` rejects startup with an explicit error.
- `PhysicalAsioHost::enumerate()` returns CLSIDs and names of external ASIO entries without opening a device; the local probe sees 29, including third-party virtual drivers. Opening the selected CLSID, reading its capabilities, creating/destroying stable buffers, and the hardware callback are implemented and build. Common PCM conversions are handled in that callback; actual activation of a selected device and connection to the graph remain to be verified.
- `VasioClientManager` detects processes loading any of the four DLLs, validates and attaches their versioned mappings, and detaches vanished clients. Integration into the default engine builds; a child-process probe verifies interprocess attachment.
- `openapi-v1.json` and `schemas/api-v1.json` describe the HTTP/WebSocket contract. The native loopback server serves inventory, provisional state, and the WebSocket handshake. `configuration.apply` goes to the controller, which stops the stream, validates endpoints, replaces the runtime, and resumes the clock on success. A local probe covers `stopped` and `error` paths without opening hardware.
- `RoutingGraph` builds and processes the three supported route directions with gain and mute; its tests pass. `AudioRoutingRuntime` connects float32 ASIO buffers to VASIO rings, and its probe confirms samples on physical→VASIO, VASIO→physical, and VASIO→VASIO routes.
- `AudioController` owns `PhysicalAsioHost` on a dedicated thread with a hidden window and Windows message loop. That lifecycle builds; opening and receiving callbacks from real hardware remain unverified at this historical point.
- The Electron/React GUI still calls the old stdin commands `routes.add/remove/list`; it does not yet consume this contract.

## Selected architecture

### Control plane

A native engine process running in the user's Windows session hosts the documented local API and remains separate from the Electron window. Electron starts or joins it and uses only HTTP/WebSocket to read state and request changes. No UI module reads the registry, `routing.ini`, shared audio memory, or engine internals.

The API is the sole configuration and control boundary. It documents device, capability, virtual port, route, error, and state schemas. Mutating commands are idempotent. WebSocket events publish inventory, engine-state, and diagnostics changes, including buffer drops and the latest error. Internal storage stays private to the engine and is not a client contract.

Every configuration mutation explicitly interrupts routing before applying the change. If the engine was active and the new configuration is valid, it recreates buffers and graph and resumes routing. On failure, it stays stopped and publishes the error; it never silently continues with old or partly applied state. The API exposes `running`, `reconfiguring`, `stopped`, and `error` transitions so the GUI can show the interruption.

### Audio plane

A VASIO DLL is a Windows COM driver implementing `IASIO` according to the bundled Steinberg example. It has a stable CLSID, ASIO entry, and correct `InprocServer32` path. It advertises up to 256 inputs and 256 outputs. Each application selects used channels through `createBuffers`; the engine exposes only that client's active channels as endpoints. Driver identity is unique and channel identities are stable.

The versioned shared transport reserves 256 slots per direction per client. The protocol version changes with this layout; incompatible driver and engine versions refuse attachment. Driver names no longer partition routing ports.

The engine opens one selected hardware ASIO driver at a time as master clock. Its effective sample rate and block size determine those of the graph and are the only values VASIO advertises. Clients must accept exactly those values; other requests are rejected before changing their mappings. After `setSampleRate`, the engine rereads physical capabilities that may depend on rate, then creates buffers only for hardware channels referenced by applied routes. The hardware callback processes the audio graph and exchanges blocks with VASIO client queues. Queues support interprocess exchange and do not convert rate or block size. Neither hardware nor client ASIO callbacks allocate, access disk, or wait on blocking locks. Notifications and statistics are published outside callbacks.

The graph supports three explicit links: application VASIO output to another application's VASIO input; VASIO output to hardware ASIO output; and hardware ASIO input to an application's VASIO input. Routes are per channel, have stable IDs, and may expose gain and mute. Unsupported format or rate is rejected with an explicit API error before activation. No implicit rate or block-size conversion is allowed. Interprocess queue underflows and overflows are counted and reported. Underflow fills missing frames with zeros. If a block will not fit, the producer drops it without blocking, increments overflow, and does not change the consumer-owned read index.

### Installation and lifecycle

The installer copies the x64 DLL, idempotently registers its single ASIO entry and COM key, then verifies registered paths and CLSID. Migration removes only old VASIO1–VASIO4 entries and their known CLSIDs; other ASIO drivers are untouched. Uninstallation removes only keys matching documented VASIO identifiers. The GUI does not write driver configuration directly.

The engine runs in the user session because it controls the hardware ASIO device. If absent, the virtual driver returns an explicit connection error and does not simulate an audio stream. Closing the window does not stop an engine still serving clients. The control application can hide its window; the engine remains available while a VASIO client is connected and can be stopped explicitly when no client uses the circuit.

## Delivery stages

Each stage must remain verifiable before the next:

1. **Compliant, registrable ASIO driver:** COM implementation based on the SDK example, stable CLSID, compliant exports and registry, one DLL build, discovery and instantiation through the SDK ASIO enumerator, then channel advertisement and allocation up to 256 per direction.
2. **Interprocess transport and master engine:** versioned shared memory for 256 channels per direction, queues without blocking locks, connection and lifecycle management, an engine applying the hardware driver's rate and block size and publishing capabilities. Client snapshots pin their mappings for the controller.
3. **Audio graph and API:** the three port link types, validation, atomic mutation apply, engine persistence, HTTP/WebSocket API, events, schemas, and diagnostics.
4. **GUI:** API-provided inventory and capabilities, circuit editor, driver and route settings, states, and diagnostics. No hard-coded business data or parallel route storage.
5. **End-to-end validation:** installation and discovery in an ASIO host, application playback to physical output, physical input to application, link between two applications, rate/buffer changes, disconnect/reconnect, and drop/latency observation.

## Acceptance criteria

- The single x64 DLL is registered under its stable CLSID, discovered and instantiated in a real ASIO host, advertises 256 inputs and outputs, and allocates low, high, and sparse channels.
- Installation replaces old VASIO1–VASIO4 entries without touching other ASIO drivers.
- VASIO sample rate and block size equal effective physical values; incompatible requests are rejected.
- API inventory reflects real VASIO ports and the selected physical driver.
- A circuit set in the UI passes through the API, changes the audio stream, and survives an engine restart.
- Every configuration change stops the stream before mutation, then resumes on success or stays stopped with an explicit error.
- The three audio paths above are verified with measurable signals and identifiable physical channels.
- Rate or format incompatibility is visibly rejected; failure or block loss reaches the API and GUI.
- Audio callbacks remain free of allocation, disk access, and blocking locks.

## Out-of-scope decisions

- Capturing applications using shared Windows output.
- Intercepting an application that directly opens another ASIO driver.
- Opening multiple hardware ASIO drivers simultaneously in the first version; one physical device is master clock.
- Adding DSP effects to the graph.
- Changing a circuit without interrupting audio routing.
- Treating `routing.ini`, internal DLL structure, or old architecture files as API contracts.
