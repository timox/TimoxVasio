# TimoxVasio 256 channels and physical clock — implementation plan

[Français](2026-10-03-vasio-256-physical-master-plan.md) | **English**

> **For implementation agents:** Execute this plan one task at a time in this session, validating each task before the next.

**Goal:** Replace six-channel VASIO1–VASIO4 with a single `TimoxVasio.dll` driver providing 256 inputs and 256 outputs, and strictly align virtual transport with the physical ASIO device's effective sample rate and block size.

**Architecture:** `TimoxVasio.dll` maintains a versioned shared-memory mapping per client process and exposes channels actually allocated by `createBuffers`. One physical ASIO host provides clock, rate, and block size; `TimoxVirtualAsioEngine.exe` recalculates rate-dependent capabilities and allocates only routed physical channels. The documented API is the sole configuration and inventory contract for engine and GUI.

**Technologies:** C++20, bundled Steinberg ASIO SDK, Windows COM, CMake/Visual Studio 2026, cpp-httplib, JSON API v1, Electron/React.

**Specification:** `docs/superpowers/specs/2026-10-03-vasio-single-driver-256-channel-design.en.md` and the goal/architecture in `docs/superpowers/specs/2026-10-02-virtual-asio-routing-design.en.md`.

## Global constraints

- One registered driver, `TimoxVasio`, advertising 256 inputs and 256 outputs.
- ASIO indices are zero-based; API channel numbers are one-based.
- Each client exposes only inputs/outputs actually requested through `createBuffers`.
- A client application may request only the active physical sample rate and block size.
- Set the physical rate before rereading capabilities that may depend on it.
- A configuration change stops routing before modifying host, buffers, or graph; on failure, the engine remains stopped and publishes the error.
- No audio callback allocates, accesses disk, or waits on a blocking lock.
- No file outside the `asio` directory may be modified or included in a commit.
- The October 2, 2026 driver, engine/API, and graph/GUI plans describe the old four-driver/six-channel model. This plan supersedes them; do not execute them as written.
- A 256/256 mapping uses 32 MiB of float32 ring data per client, excluding temporary and ASIO buffers.

## Review points

- `createBuffers` receives sparse or duplicate channels or index 255: test validation and two-way transport.
- Multiple clients have distinct PIDs and overlapping channels: test identity, attach, and detach.
- Physical driver channel counts vary with rate: probe set-rate ordering, capability reread, and buffers.
- Client rate or size differs from physical transport: reject before mapping or stream mutation.
- A route references an invalid or unallocated physical port: validate before startup and leave the engine stopped with a structured error.

## File map

- `include/audio_transport.h`, `src/audio_transport.cpp`: shared layout, capacity, active-channel masks/lists, protocol version.
- `src/vasio_driver.cpp`, `src/vasio_driver_factory.cpp`, `src/vasio_com_driver.h`: `IASIO` contract, sparse allocation, client transport.
- `src/vasio_client_manager.cpp`, `include/vasio_client_manager.h`: single-DLL discovery, version validation, client snapshots.
- `src/physical_asio_host.cpp`, `include/physical_asio_host.h`, `src/audio_controller.cpp`, `include/audio_controller.h`: physical negotiation, post-rate capabilities, effective size, routed buffers.
- `src/audio_routing_runtime.cpp`, `src/routing_graph.cpp`, their headers and tests: active-channel and real physical-index processing.
- `src/control_api_server.cpp`, OpenAPI schemas, `API.md`, `gui/electron/*`, `gui/src/*`: API-driven contract and GUI.
- `CMakeLists.txt`, install/uninstall scripts, and probes under `tests/`: one DLL target, idempotent old-CLSID migration, external verification.
- `README.md`, `INSTALL.md`, `BUILD_DRIVERS.md`, `gui/GUI_GUIDE.md`, and delivery plans: documentation aligned with the single-driver contract.

## Tasks

### Task 1: Lock the new capacity and transport contract

**Files:** `include/audio_transport.h`, `src/audio_transport.cpp`, `tests/audio_transport_probe.cpp`.

- [ ] Write a probe transporting blocks with active channels 0, 127, and 255 in both directions and rejecting an older protocol mapping.
- [ ] Run the probe and confirm it currently fails on six-channel capacity.
- [ ] Replace constant capacity with 256 and verify high-index channels traverse both rings.
- [ ] Calculate ring offsets using fixed capacity 256 and keep the read index consumer-owned.
- [ ] Explicitly increment the mapping version and reject incompatible versions or layouts.
- [ ] Publish active channels with the driver in task 2, where `createBuffers` is their source and the test can verify end-to-end ASIO allocation.
- [ ] Rerun transport probes, including wraparound, overrun, and underrun, and check for regressions.

### Task 2: Advertise and transport 256/256 through the single DLL

**Files:** `src/vasio_driver.cpp`, `src/vasio_driver_factory.cpp`, `src/vasio_com_driver.h`, `tests/driver_probe.cpp`, `tests/driver_audio_probe.cpp`.

- [ ] Extend probes to require 256 inputs/outputs, accept sparse allocation through channel 255, and reject index 256, a same-direction duplicate, and over 512 structures.
- [ ] Observe their failure before driver changes.
- [ ] Replace six-channel bounds with the shared constant; validate input and output indices separately.
- [ ] Publish channels allocated by `createBuffers` in the shared mapping, with a coherent engine-side snapshot read.
- [ ] Build client buffers only for requested channels, without buffers for unrequested channels.
- [ ] Adapt interleaved transport so each ASIO channel position retains the correct index in the 256-slot mapping.
- [ ] Run unit and COM probes for `getChannels`, low/high indices, sparse channels, duplicates, and sample transfer.

### Task 3: Reduce identity and attachment to one driver

**Files:** `CMakeLists.txt`, `src/vasio_driver_factory.cpp`, `src/physical_asio_host.cpp`, `src/vasio_client_manager.cpp`, `include/vasio_client_manager.h`, registry and client tests.

- [ ] Write/adapt a registry test to discover and instantiate only one VASIO driver and verify migration removes four documented old names/CLSIDs without changing other drivers.
- [ ] Verify the expected current failure with four hard-coded drivers.
- [ ] Replace four targets/identities with one stable CLSID and x64 artifact; retain COM exports and registration required by the Steinberg enumerator.
- [ ] Detect the single VASIO module and attach a client once per PID, checking process identity and mapping version.
- [ ] Make install, migration, uninstall, and clean idempotent and strictly limited to known VASIO CLSIDs.
- [ ] Build the DLL and pass registration, enumeration, instantiation, and child attachment tests/probes.

### Task 4: Align the engine with effective hardware settings

**Files:** `src/physical_asio_host.cpp`, `include/physical_asio_host.h`, `src/audio_controller.cpp`, `include/audio_controller.h`, `tests/physical_asio_probe.cpp`.

- [ ] Add negotiation order to the probe: choose a supported rate, call `setSampleRate`, reread rate and `getChannels`/`getBufferSize`, then create requested buffers.
- [ ] Fail the test if post-rate capabilities are not used or an unrouted channel is allocated.
- [ ] Change opening/configuration to publish only confirmed rate and choose a size allowed by physical driver capabilities.
- [ ] Build physical buffers from the union of route-referenced channels while preserving exact hardware indices.
- [ ] Pass the same effective values to runtime and VASIO; reject client requests for other rates or sizes without changing the active mapping.
- [ ] Verify `stopped`, success, and error paths; any reconfiguration failure leaves the engine stopped.

### Task 5: Make graph and API dynamic and consistent

**Files:** `src/audio_routing_runtime.cpp`, `src/routing_graph.cpp`, `src/control_api_server.cpp`, JSON/OpenAPI schemas, `API.md`, graph/runtime/API tests.

- [ ] Add cases for active channels through 256 and physical channels above six in virtual→virtual, virtual→physical, and physical→virtual links.
- [ ] Verify failures while inventory or runtime is limited to four drivers/six channels.
- [ ] Build virtual endpoints from client and active-channel snapshots, with unique IDs by PID and channel.
- [ ] Remove VASIO1–VASIO4 lists and `<= 6` loops from the API contract; describe one driver and confirmed physical values.
- [ ] Add structured errors for incompatible rate/size, invalid capacity, and failed reconfiguration.
- [ ] Verify apply stops the stream before mutation and restarts only after full validation.
- [ ] Regenerate or validate OpenAPI and run all graph, runtime, and API contract tests.

### Task 6: Adapt the interface to the single API contract

**Files:** `gui/electron/api-client.js`, `gui/electron/main.js`, `gui/electron/preload.js`, `gui/src/App.js`, tests and GUI documentation.

- [ ] Update contract fixtures for one driver, active per-client endpoints, and effective physical rate/size.
- [ ] First verify UI tests fail if the interface renders four drivers or independent virtual settings.
- [ ] Show API-provided physical inventory and VASIO clients/endpoints; submit changes through `configuration.apply`.
- [ ] Remove local business data and independent VASIO rate/size configuration.
- [ ] Verify three route categories, interruption transitions, and API errors with UI tests and React/Electron build.

### Task 7: Integrate, document, and verify with a real host

**Files:** scripts, `README.md`, `INSTALL.md`, `BUILD_DRIVERS.md`, `API.md`, GUI documentation, and end-to-end probes.

- [ ] Update guides for one 256/256 DLL, migration of old names, exact per-client memory cost, and hardware rate/size locking.
- [ ] Build cleanly in x64 with Visual Studio 2026 and run focused probes then the full CMake/GUI suite.
- [ ] Install the single DLL and verify through an ASIO enumerator that it exposes 256 inputs/outputs and allocates high/sparse channels.
- [ ] Use a real ASIO host to verify advertised physical values, rejection of setting mismatches, and three measurable audio flows.
- [ ] Verify repeated installation, migration from VASIO1–VASIO4, engine restart, disconnect/reconnect, and stopped error state.
- [ ] Announce full delivery only after proving specification acceptance criteria and updating the ledger.

## Plan self-review

- Coverage: tasks 1–2 cover capacity, shared layout, and client allocations; 3 covers identity, COM, and migration; 4 covers physical rate/size/channels; 5 covers graph/API/stop; 6 covers GUI; 7 covers documentation, build, and real verification.
- Edge cases: indices 255/256, sparse/duplicate channels, simultaneous clients, rate-dependent hardware capacity, incompatible size/rate, vanished hardware endpoint, and obsolete memory version are explicitly assigned to the owning tasks' probes.
- Interface consistency: task 1 defines shared mapping capacity; driver and manager consume its version in tasks 2–3; controller publishes effective settings in task 4; graph, API, and GUI then use snapshots and confirmed values.
- Scope: no neighboring project's file appears in the file map or Git steps. Real ASIO runtime operations are reserved for final verification after documented build and installation.
