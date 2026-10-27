# WASAPI per-process audio-copy design

## Intent

Let the user capture the sound produced by a currently active Windows
application directly into the Timox routing graph. The captured application
appears as a WASAPI capture source and can be routed to any graph destination,
including a physical ASIO output. The route must not depend on VB-Audio Cable
or another third-party virtual cable.

## Implementation status

- The original design described a feature that was not yet implemented.
- The implementation plan is in
  [`../plans/2026-10-25-wasapi-process-loopback.md`](../plans/2026-10-25-wasapi-process-loopback.md).
- Current user-facing terminology calls this an **app audio copy**. The API
  retains `kind: "processLoopback"` as its technical identifier.
- The Windows Application Loopback API can capture a selected process tree
  through `ActivateAudioInterfaceAsync` and
  `VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK` on supported Windows builds.
- The user wants to identify applications by sound activity, rather than type
  process IDs or guess which endpoint a process uses.

## Goals

1. Discover processes that have an active Windows render audio session on an
   active output endpoint.
2. Show enough detail to distinguish entries: application name, executable
   name, PID and the output endpoint where Windows reports the active session.
3. Capture the selected process and its child-process tree directly into a
   WASAPI bridge with the existing capture worker and graph-rate conversion.
4. Keep the capture source independently routeable through the existing graph
   and controllable through the local API.
5. Retain desired configuration safely when the process exits, without ever
   attaching to an unrelated process that later reuses its PID.
6. Make the supported Windows version and process-capture errors visible.

## Non-goals

- Installing, bundling or configuring VB-Audio Cable or other third-party
  virtual cables.
- Capturing all system output; this feature targets one selected process tree.
- Changing the ASIO driver or making `TimoxVasio.dll` depend on the process
  capture bridge.
- Automatically selecting or routing a process when it becomes audible.
- Persisting an executable-name rule that silently follows future processes.

## User flow

Audio setup presents separate “Devices” and “Windows applications” areas.
The application list contains processes with an active Windows playback
session; it is not mixed into the physical device tree. The user selects an
application and adds “Add application audio”. The resulting source appears in
Routing and can be connected to a headphone or any other destination. The UI
does not ask the user to choose between ASIO and WASAPI. Applications already
connected to Timox ASIO continue to appear as graph sources as they do today.
If the same process also plays through Windows, the two actual sources remain
separate in the graph, but the user does not need to configure the distinction
in Audio setup. The UI explains that Windows reports an active playback
stream, which may still be silent; it does not claim to measure audible level
in the inventory.

The inventory is a snapshot: if an application starts playing after the last
refresh, refresh the list before adding it. A process that stops after being
selected remains visible as unavailable until the user removes or reselects
it. Do not silently retarget another process with the same name.

## Process identity and lifetime

### Direct client membership versus audio copies

An application is a directly managed Timox client only when it opens the
Timox ASIO driver (or, in the future, explicitly selects a qualified Timox
virtual Windows endpoint). A WASAPI bridge is owned by the Timox engine; it
does not enroll the application whose audio it observes.

Process loopback is an app audio copy: Timox asks the Windows shared audio
engine for an independent copy associated with the selected process tree, then
publishes that copy as a new graph source. The original stream keeps its
existing Windows route. Endpoint loopback also makes a copy, but of the
combined shared mix from one render endpoint, and cannot attribute the
applications in that mix. Device capture/render bridges open an existing
Windows endpoint on behalf of Timox; they are not virtual endpoints offered
to other applications.

The graph and UI identify these separately: an ASIO client node means
that the application opened the Timox driver; an app-copy node means
Timox owns only the copied input. If the same process uses both paths, the two
sources remain independent and can produce duplicate audio when both are
routed. Windows session discovery also does not reveal whether an application
used WASAPI directly or a higher-level API backed by the shared engine.

The process inventory is built from active render endpoints and their Windows
audio sessions. A Windows audio session proves that the audio engine has an
active render stream associated with that process; it does not reveal whether
the application called WASAPI directly or used a higher-level API that routes
through the Windows audio engine. Therefore the UI says “active Windows audio
session”, not “application uses WASAPI”. ASIO streams bypass this inventory.
Process loopback uses the Windows shared audio mixer; ASIO output and
WASAPI-exclusive output are not promised to appear in this list or be included
in this feature's app copies.

Timox ASIO client processes are already known from the existing
`virtualDrivers` client inventory and continue to appear as graph sources.
This proves only that the process opened the Timox ASIO driver, not that it is
producing non-silent samples. Do not claim to inventory applications using
other vendors' ASIO drivers. Keep the implementation paths separate; the GUI
does not present an ASIO-versus-WASAPI configuration choice.

Use `IAudioSessionManager2`, `IAudioSessionEnumerator` and
`IAudioSessionControl2::GetProcessId` to identify the owning process. A
session can represent more than one process; when Windows reports
`AUDCLNT_S_NO_SINGLE_PROCESS`, do not offer it as a process-specific target.
Duplicate sessions for the same process are coalesced while preserving the
distinct endpoint names. Resolve the process image and creation time on the
controller worker; skip sessions whose process identity cannot be read, but
retain the endpoint-level inventory result. Register for session-created and
session-state notifications while building the inventory so sessions created
around a refresh are not silently omitted.

Configuration stores the target PID, process creation time as a decimal
string, and image path. Compare all three against the live process before
activation and again after `ActivateAudioInterfaceAsync` completes. Do not
call `IAudioClient::Start` or read any capture packet until the second identity
check succeeds. If the process exited or its identity differs, close the
unstarted audio client, publish an unavailable bridge and do not retry against
that PID. The user must refresh and select the active process instance again.
This allows Windows to resolve the asynchronous activation by PID while
ensuring no packet from a replacement process can enter Timox.

Process capture uses `PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE`, so
audio rendered by the selected process and its children is included. It is a
capture-only bridge; no render or self-loop configuration is valid.

## Architecture

### Discovery

- Add a worker-thread process-audio enumerator alongside the existing WASAPI
  endpoint enumerator.
- Initialize COM on the controller worker for the full enumeration call.
- Enumerate active render endpoints and active audio sessions, then return a
  deduplicated process inventory with endpoint labels and stable process
  identity metadata.
- Add `GET /api/v1/windows-audio-applications` as an explicit refresh, matching the
  existing `GET /api/v1/wasapi-devices` refresh pattern. Do not add process
  enumeration to every state poll.
- Expose the refresh through Electron main/preload IPC; the renderer does not
  enumerate arbitrary OS processes itself.

### Capture backend

- Replace the `E_NOTIMPL` branch with process-loopback activation using the
  target identity and `ActivateAudioInterfaceAsync`.
- Keep completion-handler lifetime and COM apartment rules explicit. The
  owning bridge worker may wait for activation completion, but graph callbacks
  never call COM or block.
- Initialize the returned `IAudioClient` in shared mode, use its effective
  mix format and event callback, and reuse the existing capture packet,
  bounded FIFO, resampling and health-counter path.
- Return the actual process-loopback format and latency metadata through the
  existing `WasapiStreamInfo` and bridge snapshot structures.
- On unsupported Windows versions, stale process identity, activation failure
  or access denial, fail the bridge with a visible typed/error state while
  leaving unrelated ASIO audio operational.

### Bridge manager and routing

- Extend bridge configuration with the process identity selector. Exactly one
  selector is valid: endpoint ID, default role, or process identity.
- Validate process-loopback as capture-only and include process identity in
  bridge equality, persistence and API snapshots.
- Skip endpoint resolution for process-loopback sessions; resolve and verify
  the process identity before activation instead.
- Publish the bridge as a capture binding with stable graph IDs
  `wasapi:<bridgeId>:input:<channel>`. Do not treat it as endpoint loopback;
  endpoint-loopback feedback checks remain specific to the endpoint source.
- Retain a configured bridge when its process exits, but withdraw its active
  graph ports after callback quiescence. Reappearance of a matching PID and
  creation time may reconnect; a new process instance requires explicit user
  selection.

### API and GUI

- Add the process inventory response schema with PID, process creation time,
  executable path, display name and output endpoint labels.
- Extend WASAPI bridge configuration and status schemas with process identity
  fields; the endpoint selector is absent for process-loopback bridges.
- Update the API contract, configuration store and compatibility behavior so
  older API 1.1 engines continue to handle existing endpoint bridges. The GUI
  only offers process capture when the process inventory route is supported.
- Add the application list and capture action within the WASAPI tab. Keep
  contextual process information out of left navigation, and use the existing
  bridge detail/dialog pattern for configuration and removal.
- Keep physical endpoints and application processes in separate areas. Do not
  ask the user to choose an audio API; show path details only where needed to
  explain why a source is available or unavailable.
- Clearly label direction as “Application output → Timox graph”; keep the
  process image path and PID in expandable technical details.

## Constraints and failure behavior

- Process discovery and process identity reads happen off the audio callback.
- WASAPI activation, `IAudioClient` operations and teardown remain on the
  bridge's owning worker thread.
- The graph callback only reads the existing fixed-size bridge IO buffers.
- Process IDs and creation times are validated at the API trust boundary; a
  malformed or mismatched identity cannot be opened.
- The process identity is checked both before and after asynchronous
  activation. A mismatched activation is closed before Start or packet read.
- Process-loopback capture requires Windows 10 build 20348 or later. Older
  Windows versions keep normal endpoint capture/render support and receive a
  clear explanation when process capture is unavailable.
- No third-party audio cable is a dependency or instruction in the flow.

## Acceptance evidence

- While a known application is playing, the refreshed process inventory shows
  its name, PID and active render endpoint without listing unrelated inactive
  processes.
- Adding that process creates a capture bridge and graph input ports; routing
  those ports to the selected ASIO output produces the application's sound.
- Muting/removing the route stops the sound at that destination; other ASIO
  routes remain operational.
- Stopping or restarting the target process never captures audio from a
  different process that reuses its PID.
- Unsupported OS, no active session, access denied and process exit states are
  distinguishable from a healthy running bridge.
- No VB-Audio Cable is required, recommended or automatically selected.

## Implementation calls left to the builder

- Keep the process inventory response lightweight and refresh-on-demand.
- Reuse the existing bridge status and retry model; do not create a second
  audio transport abstraction.
- Keep process-loopback bridges within the existing maximum of 32 WASAPI
  bridges.

## Platform references

- [Windows audio sessions](https://learn.microsoft.com/en-us/windows/win32/coreaudio/audio-sessions)
- [Enumerating audio sessions](https://learn.microsoft.com/en-us/windows/win32/api/audiopolicy/nf-audiopolicy-iaudiosessionmanager2-getsessionenumerator)
- [Getting the process ID for a session](https://learn.microsoft.com/en-us/windows/win32/api/audiopolicy/nf-audiopolicy-iaudiosessioncontrol2-getprocessid)
- [Process-loopback activation parameters](https://learn.microsoft.com/en-us/windows/win32/api/audioclientactivationparams/ns-audioclientactivationparams-audioclient_process_loopback_params)
- [Microsoft Application Loopback sample](https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/ApplicationLoopback)
