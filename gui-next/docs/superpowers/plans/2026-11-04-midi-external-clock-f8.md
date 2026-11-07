# MIDI External Clock F8 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Allow the existing engine MIDI clock to follow a selected incoming standard MIDI Clock (`F8`) source while continuing to emit clean, reference-clock-timed MIDI Clock output.

**Architecture:** The engine remains the sole clock owner. A configured MIDI source port becomes the external clock input; incoming `F8`, `FA`, `FB`, and `FC` update the engine clock state, while `PumpClock` emits 24 ticks per quarter note from the existing audio reference clock. Routes and existing manual BPM mode remain compatible. The GUI exposes the source selection and state using the existing engine-clock context/details flow.

**Tech Stack:** C++17 engine, Windows MIDI Services / `Windows.Devices.Midi2`, API 1.1 JSON, Electron/React GUI, existing MIDI patchbay tests.

**Spec:** This plan is the implementation spec for the approved proposal: standard MIDI Clock `F8` first; arbitrary note/CC tempo triggers are deferred.

## Global Constraints

- Use only Windows MIDI Services / `Windows.Devices.Midi2`; do not add legacy MIDI APIs.
- Preserve API 1.1 compatibility and existing manual BPM behavior.
- MIDI timing follows the audio reference clock, never Windows timers.
- Do not create a second permanent GUI properties panel; configure through the existing clock action/modal flow.
- External clock timeout must stop generated output cleanly and expose an explicit state.

## Review Focus

- Source unplugged or offline: generated clock stops and the configured source remains recoverable.
- First tick and tempo acquisition: no burst or guessed tempo before enough intervals exist.
- Start/continue/stop messages: state transitions are deterministic and idempotent.
- Source echo: the engine must not treat its own generated clock as a new master.
- Manual mode and old saved configurations: behavior and JSON decoding remain unchanged.

### Task 1: Engine clock-source model and tests

**Files:**
- Modify: `TimoxVAudioEngine/include/midi_patchbay.h`
- Modify: `TimoxVAudioEngine/src/midi_patchbay.cpp`
- Test: `TimoxVAudioEngine/tests/midi_patchbay_tests.cpp`

- [ ] Add an optional external source port id and clock mode to the in-memory configuration, preserving manual mode as the default.
- [ ] Add failing tests for `F8` tempo acquisition, `FA/FB/FC` state transitions, timeout, source removal, and manual-mode compatibility.
- [ ] Implement source matching and interval smoothing using the existing steady/reference-clock state; never schedule from the receive thread.
- [ ] Make `PumpClock` emit only when the external source is running and its tempo is valid.
- [ ] Run `MidiPatchbayTests` and the existing MIDI routing tests.

### Task 2: API contract and serialization

**Files:**
- Modify: `TimoxVAudioEngine/api/schemas/api-v1.json`
- Modify: `TimoxVAudioEngine/api/openapi-v1.json`
- Modify: `TimoxVAudioEngine/src/control_api_server.cpp`
- Test: existing API contract tests.

- [ ] Add nullable external clock source/mode fields to the MIDI configuration and explicit engine-clock state fields for source, lock, BPM and timeout.
- [ ] Keep fields optional when absent so existing API 1.1 clients and saved configurations remain valid.
- [ ] Serialize the source port by stable port id, never by process id or display name.
- [ ] Run the engine API and schema tests.

### Task 3: GUI configuration and status

**Files:**
- Modify: `TimoxVSSC/src/MidiWorkspace.js`
- Modify: `TimoxVSSC/src/midi-model.js`
- Modify: `TimoxVSSC/src/MidiWorkspace.css`
- Test: `TimoxVSSC/src/MidiWorkspace.test.js`, `TimoxVSSC/src/midi-model.test.js`

- [ ] Add an “External MIDI Clock” choice to the existing engine-clock configuration action.
- [ ] Offer only source-capable MIDI ports and show the selected source, lock state, BPM and timeout state in the existing clock node.
- [ ] Keep manual BPM controls available when external mode is disabled.
- [ ] Add tests for selection, unavailable source, and fallback to manual mode.
- [ ] Run focused React tests and production build.

### Task 4: MIDI Learn for custom clock signals

**Files:**
- Modify: `TimoxVAudioEngine/include/midi_patchbay.h`
- Modify: `TimoxVAudioEngine/src/midi_patchbay.cpp`
- Modify: `TimoxVAudioEngine/api/schemas/api-v1.json`
- Modify: `TimoxVSSC/src/MidiWorkspace.js`
- Modify: `TimoxVSSC/src/midi-model.js`
- Test: engine MIDI patchbay tests and focused GUI tests.

- [ ] Add four independent learn slots: `Beat`, `Start`, `Stop`, and optional `Continue`.
- [ ] Capture the complete incoming MIDI 1.0 message signature: stable source port, channel when applicable, status/message type, and data values relevant to matching.
- [ ] Require `Beat`, `Start`, and `Stop` before enabling custom-clock mode; keep `Continue` optional.
- [ ] Match learned messages on the receive path without treating generated output or loopback echo as the external source.
- [ ] Reuse the reference-clock scheduler to emit clean `F8` ticks after beat intervals are learned; stop on timeout or learned stop.
- [ ] Provide a modal MIDI Learn flow with an explicit “waiting for signal” state, cancellation, replacement and clear actions.
- [ ] Test incomplete learns, replacement, wrong-channel messages, timeout, and an optional Continue mapping.

### Task 5: Packaging and handoff

**Files:**
- Modify: `TimoxVSSC/artifacts.lock.json`
- Modify: `TimoxVSSC/README.md` or release documentation if user-facing setup text is needed.

- [ ] Rebuild the clean MIDI-enabled engine and update its receipt and pinned hash.
- [ ] Run engine tests, GUI tests, API contract tests and release preflight.
- [ ] Build the non-release NSIS preview installer and run packaged postflight.
- [ ] Commit and push engine and GUI repositories; do not create a GitHub Release.
