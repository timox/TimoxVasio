# VASIO Circuit GUI Implementation Plan

> **Historical archive — do not execute as the current plan.** This document describes the old four-driver contract. Follow the [master plan](2026-10-03-vasio-256-physical-master-plan.en.md) and [specification](../specs/2026-10-03-vasio-single-driver-256-channel-design.en.md).

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a user select virtual and physical ASIO devices, configure VASIO, build routes graphically, apply them through the documented API and verify real audio and diagnostics.

**Architecture:** React renders only API-provided state. Electron main starts and supervises the native engine, consumes its HTTP/WebSocket API and exposes typed preload calls; React never reads configuration files, the registry or shared memory. A single `configuration.apply` operation makes each edit visibly interrupt routing before the engine rebuilds.

**Tech Stack:** Electron 28, React 18, browser `fetch`/`WebSocket` in the Electron main process, isolated preload/context bridge, Jest through react-scripts, Windows Visual Studio 2026 native engine.

**Spec:** `docs/superpowers/specs/2026-10-02-virtual-asio-routing-design.md`

## Global Constraints

- Target Windows x64 and Visual Studio 2026 (`Visual Studio 18 2026`).
- Show only drivers, endpoints, routes, settings and diagnostics returned by the native engine API.
- The four VASIO drivers expose six inputs and six outputs each; applications select VASIO explicitly.
- Physical-device selection opens one ASIO device at a time.
- All configuration mutations use the documented API and stop audio before application.
- Show `reconfiguring`, successful restart, or stopped/error state from API events; never imply audio is still running during an edit.
- Do not read `routing.ini`, the Windows registry or audio shared memory from any UI process.

## Review Focus

- Engine not installed, not started or exits: show a recoverable unavailable/error screen, not a hardcoded virtual-driver matrix.
- No physical ASIO driver found: explain the empty inventory and keep route creation disabled.
- User edits while a configuration apply is in progress: disable conflicting mutations until the API reports `running` or `error`.
- API rejects an endpoint or channel: display the server’s structured error and retain the last confirmed API state.
- WebSocket disconnects and reconnects: reload HTTP state before accepting another edit so no stale local graph overwrites engine state.

---

### Task 1: Replace Electron stdio forwarding with a typed API client

**Files:**
- Create: `gui/electron/api-client.js`
- Modify: `gui/electron/main.js`
- Modify: `gui/electron/preload.js`
- Create: `gui/tests/api-client.test.js`
- Modify: `gui/package.json`

**Interfaces:**
- Consumes: engine HTTP `GET /api/v1/state`, `GET /api/v1/drivers`, WebSocket `/api/v1/ws` and Task 1 envelopes in `API.md`.
- Produces: preload methods `getState()`, `getDrivers()`, `applyConfiguration(configuration)`, `subscribeEvents(callback)`.

- [ ] **Step 1: Write API-client tests with a local HTTP/WebSocket test server**

Test that `getState()` returns the JSON state unchanged, `applyConfiguration()` sends `{id,command:"configuration.apply",payload}` and resolves only the matching response ID, unrelated events are forwarded once, server errors reject with their documented code, and a closed WebSocket causes pending calls to reject.

- [ ] **Step 2: Run the client tests against the current Electron bridge**

Run: `npm --prefix gui run react-test -- --watchAll=false --runTestsByPath tests/api-client.test.js`

Expected: FAIL because the current preload exposes route-specific stdin/stdout methods and no HTTP/WebSocket API client.

- [ ] **Step 3: Implement the API client and narrow preload surface**

Have Electron main launch `VirtualASIO.exe` with the documented API port/config arguments, wait for successful `GET /api/v1/state`, create one WebSocket connection, and forward API payloads without key renaming or route normalization. Expose only the four named preload methods. Reject unresolved calls when the engine process exits. Remove the line-oriented stdout parser and old `add-route`, `remove-route`, `get-routes`, and `set-gain` IPC handlers.

Keep the preload API narrow and typed:

```js
contextBridge.exposeInMainWorld('vasio', {
  getState: () => ipcRenderer.invoke('api:get-state'),
  getDrivers: () => ipcRenderer.invoke('api:get-drivers'),
  applyConfiguration: (configuration) =>
    ipcRenderer.invoke('api:configuration-apply', configuration),
  subscribeEvents: (callback) => {
    const listener = (_event, payload) => callback(payload);
    ipcRenderer.on('api:event', listener);
    return () => ipcRenderer.removeListener('api:event', listener);
  }
});
```

Main-process events are forwarded with `webContents.send('api:event', payload)`; callbacks are registered in the isolated preload and never serialized through `ipcRenderer.invoke`.

- [ ] **Step 4: Run the API-client tests**

Run: `npm --prefix gui run react-test -- --watchAll=false --runTestsByPath tests/api-client.test.js`

Expected: PASS for matching IDs, unchanged payloads, structured errors, single event delivery and disconnect handling.

---

### Task 2: Render device and driver settings from API inventory

**Files:**
- Modify: `gui/src/App.js`
- Create: `gui/src/components/DeviceSelector.js`
- Create: `gui/src/components/DriverSettings.js`
- Create: `gui/src/styles/DeviceSelector.css`
- Create: `gui/src/styles/DriverSettings.css`
- Create: `gui/src/App.test.js`

**Interfaces:**
- Consumes: `getState`, `getDrivers`, `applyConfiguration`, `subscribeEvents` from preload.
- Produces: view state containing only the last confirmed API snapshot and one draft configuration submitted through `applyConfiguration`.

- [ ] **Step 1: Write rendering tests for empty, loaded and failed inventories**

Assert no VASIO list appears before API response; after response render exactly the VASIO and physical endpoints supplied by the fixture; when no physical driver exists show an empty-state explanation and disable apply; when engine API rejects startup show the structured error.

- [ ] **Step 2: Run the inventory rendering tests**

Run: `npm --prefix gui run react-test -- --watchAll=false --runTestsByPath src/App.test.js`

Expected: FAIL because `App.js` initializes four drivers and six channels directly in React state.

- [ ] **Step 3: Add physical selection and virtual driver controls**

Render the selected physical driver, real input/output channel names, advertised sample rates and buffer sizes. Render VASIO1–VASIO4 only from API inventory, with API-reported client PIDs, six input and six output ports, enabled state, sample-rate state and buffer settings. Keep draft values separate from confirmed state; do not persist them in `localStorage`.

- [ ] **Step 4: Run inventory tests and inspect the rendered state**

Run: `npm --prefix gui run react-test -- --watchAll=false --runTestsByPath src/App.test.js`

Expected: PASS for empty, populated and failed inventory states; no hardcoded endpoint count remains in `App.js`.

---

### Task 3: Build and apply an editable channel circuit

**Files:**
- Modify: `gui/src/components/RoutingMatrix.js`
- Modify: `gui/src/components/RouteList.js`
- Modify: `gui/src/components/ChannelStrip.js`
- Create: `gui/src/components/CircuitEditor.js`
- Create: `gui/src/components/CircuitEditor.test.js`
- Modify: `gui/src/App.js`
- Modify: `gui/src/styles/RoutingMatrix.css`

**Interfaces:**
- Consumes: API endpoint IDs, current route list and configuration mutation result.
- Produces: a complete draft `{physicalDriverId,virtualDrivers,routes}` and submits it only through `applyConfiguration`.

- [ ] **Step 1: Write circuit-editor tests for the three supported link types**

Create one test for virtual output → virtual input, one for virtual output → physical output and one for physical input → virtual input. Assert each draft route has the correct stable endpoint IDs, gain and mute fields. Assert a source cannot be selected as a destination and that channel numbers come from API inventory rather than fixed options.

- [ ] **Step 2: Run the circuit-editor tests**

Run: `npm --prefix gui run react-test -- --watchAll=false --runTestsByPath src/components/CircuitEditor.test.js`

Expected: FAIL because the current routing matrix only connects hardcoded VASIO names and channel numbers.

- [ ] **Step 3: Implement the editor and submit complete configurations**

Use endpoint selectors grouped by physical/virtual device and direction. Provide route add/remove, gain and mute controls. Every edit updates one in-memory draft; Apply submits the whole configuration. Do not call engine-specific helpers from React, call route-specific backend commands or update confirmed state before the API acknowledges the operation.

The apply handler sends the full draft and waits for the matching API result:

```js
async function applyDraft(draft) {
  setUiState('reconfiguring');
  try {
    await window.vasio.applyConfiguration(draft);
  } catch (error) {
    setLastError(error);
  }
}
```

- [ ] **Step 4: Run the circuit-editor tests**

Run: `npm --prefix gui run react-test -- --watchAll=false --runTestsByPath src/components/CircuitEditor.test.js`

Expected: PASS for all supported link types, route edits and API endpoint IDs.

---

### Task 4: Display interruption, meters and audio diagnostics

**Files:**
- Modify: `gui/src/App.js`
- Create: `gui/src/components/EngineStatus.js`
- Create: `gui/src/components/AudioDiagnostics.js`
- Create: `gui/src/components/EngineStatus.test.js`
- Modify: `gui/src/App.css`

**Interfaces:**
- Consumes: `engine.status`, `devices.changed`, `routes.changed`, `audio.meter`, and `engine.error` API events.
- Produces: visible engine state, route state, per-channel level, underflow/overflow totals and last structured error.

- [ ] **Step 1: Write state-transition and diagnostic tests**

Feed events `running → reconfiguring → running` and assert the editor is disabled during the interruption and enabled after success. Feed `running → reconfiguring → error` and assert routing is shown stopped with the error message. Feed meter events and assert the reported channel and xrun count are displayed.

- [ ] **Step 2: Run the status tests**

Run: `npm --prefix gui run react-test -- --watchAll=false --runTestsByPath src/components/EngineStatus.test.js`

Expected: FAIL because current GUI only renders raw stdout/stderr strings and never receives engine state events.

- [ ] **Step 3: Implement confirmed state updates and reconnect behavior**

Update confirmed state only from the API response or server events. During `reconfiguring`, disable route/device edits and display that audio has stopped. If WebSocket closes, display disconnected status and disable Apply; after reconnect, reload `getState()` before re-enabling the editor. Show dBFS peak and cumulative input/output underruns/overruns exactly as the API reports them.

- [ ] **Step 4: Run status and full GUI tests**

Run: `npm --prefix gui run react-test -- --watchAll=false`

Expected: PASS; no GUI component reads local configuration or assumes a running backend.

---

### Task 5: Verify the packaged GUI drives a real physical circuit

**Files:**
- Modify: `gui/package.json`
- Modify: `gui/README.md`
- Modify: `BUILD_DRIVERS.md`
- Modify: `API.md`

**Interfaces:**
- Consumes: built driver/engine, installation procedure, API client and circuit editor.
- Produces: packaged Windows application that controls the engine and retains diagnostics while its window is hidden.

- [ ] **Step 1: Package the application with the engine and all four DLLs**

Update `electron-builder` resources to include `VirtualASIO.exe`, configuration schema and `VASIO1.dll`–`VASIO4.dll`. Fail packaging if any artifact is missing. Keep registry changes in the explicit installer operation, not GUI startup.

- [ ] **Step 2: Run the renderer, API client and packaging checks**

Run: `npm --prefix gui run react-test -- --watchAll=false; npm --prefix gui run dist`

Expected: all UI tests pass; installer and portable package contain the engine and four DLLs and start the local API successfully.

- [ ] **Step 3: Execute the hardware acceptance path**

Install the drivers, launch VASIO Control, select the actual physical ASIO interface and create `virtual:<VASIO-client>:output:1 → physical:<device>:output:1`. Start a test tone in a DAW configured to use VASIO, confirm physical output with a meter/loopback recording, then create the reverse physical-input-to-VASIO-input circuit and confirm recorded samples. Add a second VASIO client and verify inter-application routing. Change one route while audio is active and observe `reconfiguring` followed by `running`; force an invalid driver setting and verify the engine remains stopped with an error.

- [ ] **Step 4: Record measured results in the acceptance report**

Record actual ASIO device name, sample rate, requested/actual buffer sizes, measured round-trip latency, peak level, underflow/overflow totals and each route’s observed signal result. Acceptance requires all three route types to pass through the GUI-created configuration; a successful compile or displayed meter alone is insufficient.
