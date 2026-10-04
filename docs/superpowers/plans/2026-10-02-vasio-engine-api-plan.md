# VASIO Audio Engine and API Implementation Plan

> **Archive historique — ne pas exécuter comme plan courant.** Ce document contient des étapes et états intermédiaires dépassés. Suivre [le plan maître](2026-10-03-vasio-256-physical-master-plan.md) et [la spécification](../specs/2026-10-03-vasio-single-driver-256-channel-design.md).

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Carry real samples between VASIO client processes and one selected physical ASIO device through a validated routing graph controlled by the documented local API.

**Architecture:** The physical ASIO driver is the engine’s clock master. Versioned per-client shared-memory SPSC rings carry float32 audio in both directions; a non-real-time configuration controller stops the graph before applying any mutation and restarts only after a complete successful rebuild. HTTP reads and WebSocket commands/events form the documented control contract.

**Tech Stack:** C++17, Steinberg ASIO SDK, CMake/MSVC 19.51 Visual Studio 2026 x64, vendored cpp-httplib 0.58.0 (HTTP and RFC6455 WebSocket) and nlohmann-json 3.12.0, Windows shared memory/events and message-pump thread, PowerShell integration probes.

**Spec:** `docs/superpowers/specs/2026-10-02-virtual-asio-routing-design.md`

## Global Constraints

- Target Windows x64 and Visual Studio 2026 (`Visual Studio 18 2026`).
- Expose four VASIO drivers with six inputs and six outputs each.
- Only applications selecting VASIO explicitly are sources in this version.
- Open one physical ASIO driver at a time; it is the engine clock master.
- All driver, endpoint, route and diagnostic data crosses the documented HTTP/WebSocket API.
- A configuration mutation stops routing before teardown; successful rebuild resumes; failed rebuild stays stopped with an explicit error.
- Reject unsupported sample rates/formats; do not insert implicit converters.
- No allocation, disk access or blocking lock in either physical or virtual ASIO callbacks.
- On underflow, provide silence for the missing frames and publish a counter/event. On overflow, reject the newest write block and increment the counter; the producer never changes the consumer-owned read index.

## Review Focus

- Physical driver disappears or refuses `start`: engine must stop and publish the driver error.
- VASIO DLL starts before the engine: return an explicit connection error; no local synthetic clock.
- Virtual and physical clients choose different block sizes at the same rate: ring transport handles the sizes without frame reordering.
- Client disconnects with queued frames: discard that client’s buffers, preserve other clients and publish its disconnected state.
- Route references an absent endpoint or channel: reject the whole configuration, keep routing stopped, and never retain a partial graph.

---

### Task 1: Replace the stdio API with a versioned control contract

**Files:**
- Modify: `API.md`
- Create: `openapi-v1.json`
- Create: `schemas/api-v1.json`
- Create: `tests/api-contract.ps1`

**Interfaces:**
- Consumes: design spec and existing `RouteMapping` fields.
- Produces: `GET /api/v1/state`, `GET /api/v1/drivers`, WebSocket `/api/v1/ws`, request envelope `{id,command,payload}`, response envelope `{id,success,result|error}`, event envelope `{event,payload}`.

- [x] **Step 1: Add schema examples and contract assertions**

Define stable endpoint IDs `physical:<driverId>:input:<1-based-channel>`, `physical:<driverId>:output:<1-based-channel>`, `virtual:<driverId>:<clientPid>:input:<1-based-channel>` and `virtual:<driverId>:<clientPid>:output:<1-based-channel>`. Define four virtual drivers with six inputs and six outputs each, plus settings `{enabled,sampleRate,preferredBufferFrames}`. Define a route as `{id,sourceEndpointId,destinationEndpointId,gainDb,mute}`. Define `configuration.apply` as the single mutation command carrying the full physical selection, all virtual-driver settings and route array. Define events `engine.status`, `devices.changed`, `routes.changed`, `audio.meter`, and `engine.error`. Add PowerShell checks using `Test-Json -SchemaFile schemas/api-v1.json` for every example and assert that old `routes.add` stdin/stdout messages are absent from the contract.

- [x] **Step 2: Run the API contract checks before changing implementation**

Run: `pwsh -NoProfile -File tests/api-contract.ps1`

Expected: FAIL because `API.md` currently documents stdin/stdout and lacks physical endpoints, command/event envelopes and schemas.

- [x] **Step 3: Document HTTP reads and WebSocket commands/events**

Document `GET /api/v1/state`, `GET /api/v1/drivers`, the WebSocket URL, all payload schemas, request IDs, stable errors, sample rate/buffer/channel units and the reconfiguration state machine. Define successful mutations as: ack accepted, `engine.status=reconfiguring`, then `engine.status=running` after a successful rebuild; on failure publish `engine.status=error` and leave the engine stopped. State that UI never reads `routing.ini` or process memory.

- [x] **Step 4: Run the contract checks**

Run: `pwsh -NoProfile -File tests/api-contract.ps1`

Expected: PASS for every documented payload, event and required field.

---

### Task 2: Implement versioned cross-process audio rings

**Files:**
- Create: `include/audio_transport.h`
- Create: `src/audio_transport.cpp`
- Create: `tests/audio_transport_probe.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: client identity (VASIO driver ID plus process ID), six-channel counts, sample rate and block size.
- Produces: `AudioClientMapping::open(driverId,pid,rate,blockFrames)`, `writeClientOutput(channel,frames,data)`, `readClientInput(channel,frames,data)`, `signalClient()`, and per-direction overrun/underrun counters.

- [x] **Step 1: Write a two-process ring probe**

Create one executable with `--producer` and `--consumer` modes. The parent creates the mapping and child process; producer writes a deterministic sequence `sample[n]=float(n)/1000`, consumer verifies all samples and frame order, then repeats with ring wrap and an intentional empty read. Assert underflow fills the requested output with zero and increments exactly once.

- [x] **Step 2: Run the probe before implementing shared memory**

Run: `cmake --build build_engine --config Release --target AudioTransportProbe; .\build_engine\Release\AudioTransportProbe.exe`

Expected: FAIL before implementation because `audio_transport.h` is not present. Observed: MSVC reports C1083 for the missing header.

- [x] **Step 3: Implement the mapping and SPSC queues**

Use `CreateFileMappingW`/`OpenFileMappingW`, `MapViewOfFile`, per-client auto-reset events and an aligned protocol header containing magic, version, driver ID, PID, sample rate, six-channel count, ring capacity and state generation. Use Windows `Interlocked*` sequence operations for cross-process indices. Store output and input in separate single-producer/single-consumer rings. Validate the header/version on attach; reject mismatched layouts. Underflow reads fill missing frames with zero. On overflow, reject the newest write block and increment the overrun counter; the producer never changes the consumer-owned read index. Put event waits only on worker threads, never in an audio callback.

Use this shared header layout as the starting ABI; keep every shared field fixed-width and append new fields only behind a version increment:

```cpp
struct alignas(64) AudioTransportHeader {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t driverId;
    std::uint32_t processId;
    std::uint32_t sampleRate;
    std::uint32_t channelCount;
    std::uint32_t ringCapacityFrames;
    volatile LONG64 generation;
    volatile LONG64 clientWriteFrame;
    volatile LONG64 engineReadFrame;
    volatile LONG64 engineWriteFrame;
    volatile LONG64 clientReadFrame;
    volatile LONG64 underruns;
    volatile LONG64 overruns;
};
```

- [x] **Step 4: Run wrap, order, underflow and version tests**

Run: `cmake --build build_engine --config Release --target AudioTransportProbe; .\build_engine\Release\AudioTransportProbe.exe`

Observed with Visual Studio 2026/MSVC 19.51: `PASS: two-process ordering, bidirectional wrap, overflow rejection and zero-filled underflow.` The probe also verifies that opening the mapping at a mismatched sample rate is rejected. `DriverAudioProbe` exercises all four DLL callback bridges against an attached test engine. `VasioClientManager` discovers/attaches clients in the default executable; the physical ASIO clock remains unimplemented.

- [x] **Step 5: Discover and attach running VASIO client processes**

The manager scans process module lists for the four exact DLL names, opens only matching process/driver mappings, and clears attachment when a module disappears. It does not start or control a client process. `VasioClientManagerProbe` launches a child that loads VASIO1 and waits for the engine to attach; observed: PASS across processes. The manager is part of the default engine executable. Physical clock and audio graph remain unimplemented.

---

### Task 3: Add the physical ASIO host and real device inventory

**Files:**
- Create: `include/physical_asio_host.h`
- Create: `src/physical_asio_host.cpp`
- Create: `tests/physical_asio_probe.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Steinberg `AsioDriverList` from `asiosdk/host/pc/asiolist.h` and the selected stable driver ID.
- Produces: `PhysicalAsioHost::enumerate()` with stable CLSIDs and names. The selected-driver open/start/stop lifecycle and queried channel/rate/buffer capabilities remain a separate incomplete step below.

- [x] **Step 1: Write the read-only registry inventory probe**

Create a probe that constructs `PhysicalAsioHost` and asserts each registered external ASIO entry has a unique stable CLSID and non-empty name. It must not initialize drivers during inventory.

- [x] **Step 2: Run the probe before implementing inventory**

Run: `cmake --build build_engine --config Release --target PhysicalAsioProbe; .\build_engine\Release\PhysicalAsioProbe.exe`

Observed: FAIL because `physical_asio_host.h` and the inventory implementation did not yet exist.

- [x] **Step 3: Implement read-only registry inventory through the SDK**

Use `AsioDriverList` to return stable CLSID IDs and names without activating hardware. Exclude only the four VASIO driver names from the external inventory. Do not call `ASIOHost::RegisterDrivers` for inventory.

- [x] **Step 4: Run the probe against the installed registry**

Run: `cmake --build build_engine --config Release --target PhysicalAsioProbe; .\build_engine\Release\PhysicalAsioProbe.exe`

Observed: 29 non-VASIO ASIO registry entries were listed, including external virtual ASIO products. No driver was opened and no channel/rate/buffer capability was inferred.

- [ ] **Step 5: Open only the explicitly selected physical driver**

Initialize COM on the engine thread, open the selected CLSID, query real input/output counts, supported rates and buffer ranges, create stable driver buffers, and retain callback state until `stop` and `disposeBuffers` finish. Reject unsupported settings with a structured operation, driver ID and ASIO error. Do not activate every enumerated driver to populate the list.

---

### Task 4: Build a tested channel graph and stop/apply/restart transaction

**Files:**
- Create: `include/routing_graph.h`
- Create: `src/routing_graph.cpp`
- Create: `tests/routing_graph_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: immutable endpoint inventory and `AudioRoute` values `{id,sourceEndpointId,destinationEndpointId,gainDb,mute}`.
- Produces: `RoutingGraph::validate(config,inventory)`, `RoutingGraph::process(physicalBlock,clientRings)`, and `AudioController::applyConfiguration(config)`.

- [x] **Step 1: Write graph tests for endpoint direction and channel mixing**

Test virtual-output → virtual-input, virtual-output → physical-output and physical-input → virtual-input. With source frames `[0.25,-0.5]` and gain `0 dB`, assert exact destination samples. With two routes to one destination at `-6.0206 dB`, assert their sum is approximately the source signal. Assert unknown endpoint, wrong direction, missing channel, duplicate route ID and non-finite gain fail validation.

- [x] **Step 2: Run graph tests against the placeholder engine**

Run: `cmake --build build_engine --config Release --target RoutingGraphTests; .\build_engine\Release\RoutingGraphTests.exe`

Observed RED before implementation: the target could not compile because `routing_graph.cpp` did not exist. The isolated implementation and test target now compile and pass; integration with physical buffers and client rings remains outstanding.

- [x] **Step 3: Implement immutable graph snapshots and reconfiguration state transitions**

`AudioController` now serializes `configuration.apply` on one dedicated Windows message-pump thread, stops/disposes the previous physical session before candidate construction, and stays in `error` with no runtime when validation or restart fails. `AudioRoutingRuntime` and `RoutingGraph` are immutable/preallocated on the controller thread and run from the physical callback without graph allocation or locking. The API smoke probe verifies an invalid endpoint leaves no partial routes. Persistence and real-device activation remain outstanding.

The non-real-time controller owns the transaction; the audio callback reads only the published immutable snapshot:

```cpp
ApplyResult AudioController::applyConfiguration(const AudioConfiguration& next) {
    publishStatus(EngineState::reconfiguring);
    physicalHost_.stop();
    clients_.requestResetAndDisarm();
    auto candidate = buildCandidate(next);  // validation/allocation outside callbacks
    if (!candidate) return remainStopped(candidate.error());
    graph_.publish(std::move(candidate.value()));
    if (!physicalHost_.start()) return remainStopped(lastError_);
    publishStatus(EngineState::running);
    return ApplyResult::success();
}
```

- [ ] **Step 4: Run graph and transaction tests**

Run: `cmake --build build_engine --config Release --target RoutingGraphTests; .\build_engine\Release\RoutingGraphTests.exe`

Observed: `RoutingGraphTests` and `AudioRoutingRuntimeTests` pass for route directions, float mixing and deterministic physical/virtual sample transfer. `tests/api-runtime-smoke.ps1` confirms invalid endpoint configuration leaves `error` and no partial route. Still required: an injected physical-driver start failure and a real selected-driver callback test.

---

### Task 5: Host the HTTP/WebSocket API in the engine

**Files:**
- Create: `vcpkg.json`
- Create: `include/control_api.h`
- Create: `src/control_api.cpp`
- Create: `tests/api_integration.ps1`
- Modify: `CMakeLists.txt`
- Modify: `src/main.cpp`
- Modify: `API.md`

**Interfaces:**
- Consumes: engine inventory, graph/configuration controller and Task 1 JSON schemas.
- Produces: loopback-only HTTP server on a configured port; `GET /api/v1/state`, `GET /api/v1/drivers`, WebSocket `/api/v1/ws`, documented command handlers and event stream.

- [x] **Step 1: Add loopback API integration checks**

Write PowerShell tests that start the engine with `--api-port 0`, read the selected loopback port from a startup event, call both GET routes, connect a .NET `ClientWebSocket`, request `configuration.apply`, and assert the ordered events `reconfiguring` then `running` or `error`. Reject a non-loopback bind and malformed JSON without terminating the process.

- [x] **Step 2: Run integration checks against the current process**

Run: `pwsh -NoProfile -File tests/api_integration.ps1`

Observed RED on the prior backend: the API had no listener or WebSocket command path. `tests/api-runtime-smoke.ps1` now exercises the real loopback server and controller without opening physical hardware.

- [x] **Step 3: Implement the documented server and command dispatch**

Use the existing vendored cpp-httplib and nlohmann-json dependencies; bind only to `127.0.0.1`; validate every command payload against Task 1 schemas; dispatch mutations only to `AudioController::applyConfiguration`; respond with the documented envelope and stream events from a bounded non-audio queue. Remove the stdin/stdout command loop and direct `routing.ini` mutations. Persist the last accepted configuration in the engine’s private configuration store only after a successful apply; startup reload must validate the full graph before starting audio.

The WebSocket handler must preserve the API envelope without UI-side normalization:

```json
{"id":"42","command":"configuration.apply","payload":{"physicalDriverId":"...","routes":[]}}
```

Return the matching request ID with either `{"id":"42","success":true,"result":{}}` or a structured `error`; publish state transitions as separate `{ "event": "engine.status", "payload": { ... } }` messages.

- [x] **Step 4: Run API checks and verify loopback binding**

Run: `pwsh -NoProfile -File tests/api_integration.ps1`

Observed: `tests/api-contract.ps1` and `tests/api-runtime-smoke.ps1` pass, including JSON Schema validation for HTTP state/inventory and WebSocket status/device/routes/error events. The listener binds only to `127.0.0.1`. Applying to a real physical device, persistence and GUI migration are still outstanding.
