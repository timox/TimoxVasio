# MIDI Services Endpoint Metadata Preview Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Expose Windows MIDI Services endpoint metadata in the engine API, display it in the MIDI graph, and assemble a non-release preview installer containing the synchronized components.

**Architecture:** The engine remains the authority for endpoint enumeration and session metadata. It uses the modern Windows MIDI Services C++/WinRT projection from `Windows.Devices.Midi2`; the GUI only renders optional API fields and never parses endpoint IDs. The GUI repository packages the pinned engine, PWAR client, API schema and preview driver into the existing per-machine NSIS installer.

**Tech Stack:** C++20, C++/WinRT, Windows MIDI Services SDK `Windows.Devices.Midi2`, nlohmann/json, Electron/React, NSIS, CMake/CTest/Jest.

**Spec:** The user request in this conversation: update the other repositories, preserve prior changes, commit, and produce a non-release preview installer.

## Global Constraints

- Use Windows MIDI Services / `Windows.Devices.Midi2`; do not use `Windows.Devices.Midi`, WinMM, or DirectMusic for implementation.
- Keep MIDI 1.0 routing behavior and API 1.1 compatibility.
- Endpoint metadata is optional; null means unavailable, never false or guessed.
- Do not parse Windows endpoint ID strings to derive capabilities.
- Packaging must use pinned engine/PWAR/schema/driver inputs and must not publish a release.
- Do not start GUI/audio/hardware qualification during build verification.

## Review Focus

- A native endpoint with both source and destination groups retains both graph directions.
- An endpoint with unavailable session enumeration reports `activeClients: null`.
- Existing API consumers still accept responses without the new optional fields.
- Virtual application loopbacks do not inherit misleading physical-device metadata.
- The preview installer contains the synchronized engine, PWAR, GUI, schema and preview driver and remains fully uninstallable.

### Task 1: Engine endpoint metadata

**Files:** `TimoxVAudioEngine/include/midi_backend.h`, `include/midi_patchbay.h`, `src/windows_midi_services_backend.cpp`, `src/midi_patchbay.cpp`, `src/control_api_server.cpp`, engine MIDI tests.

- Add endpoint metadata to the backend enumeration result and copy it to native port snapshots.
- Read the SDK properties directly from `MidiEndpointDeviceInformation` and enumerate active MIDI sessions when supported; do not parse endpoint IDs.
- Serialize framework, transport, manufacturer, serial, VID/PID, native data format, multi-client capability and optional active-client count.
- Add unit/contract coverage for the Xone K3-shaped bidirectional, multi-client endpoint and missing session count.

### Task 2: GUI contract and graph

**Files:** current GUI API schema/OpenAPI, `src/midi-model.js`, `src/MidiWorkspace.js`, tests.

- Keep the optional fields added to the API contract and ensure the GUI displays only returned values.
- Identify the backend as Windows MIDI Services / `Windows.Devices.Midi2` in the MIDI graph.
- Preserve the current graph routing and MIDI clock behavior.

### Task 3: Cross-repository pins and preview assembly

**Files:** engine build receipt/binary, GUI `artifacts.lock.json`, release metadata and installer tests.

- Build the engine with the metadata changes and the already pinned MIDI SDK.
- Update the GUI artifact lock with the engine revision/hash and preserve PWAR/driver pins.
- Run preflight, package a per-machine NSIS preview installer, verify component presence and uninstall metadata, but do not create a hosted release.

### Task 4: Commits and handoff

- Commit the engine repository changes first.
- Commit the GUI repository changes and pin update second.
- Report commit IDs, generated installer path, verification results and any external repository limitation.
