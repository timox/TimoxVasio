# Timox VASIO Control: integrated configuration, documentation, and diagnostics

[Français](2026-10-04-electron-engine-control-diagnostics-swagger-design.md) | **English**

## Goal

Make the Electron executable shipped with TimoxVasio the direct entry point for configuring the engine, viewing the Swagger API, and diagnosing operation. Clarify the interface with short navigation and more legible surfaces, retain engine logs, and enable a debug mode without writing audio data. Ship the portable executable and installer after functional validation.

## Verified state at the time

- `gui/dist-candidate/Timox VASIO Control 1.0.0.exe` and `gui/dist-candidate/Timox VASIO Control Setup 1.0.0.exe` have been generated. Electron Builder bundles `TimoxVirtualAsioEngine.exe`.
- `gui/electron/main.js` attaches to an engine on port 52525 or starts one, then always stops its child process when the application closes.
- The React page already has per-application profiles, physical driver selection, sample rate, block size, and routing matrix. They are spread across one long page without navigation by function.
- Electron process messages go to its console. Engine stderr is shown as transient UI notifications. No view retains or reads a log, and debug mode cannot be controlled in the UI.
- `openapi-v1.json` describes the API but is not integrated into Electron. The API accepts configuration and profiles but exposes neither diagnostics nor an orderly stop command.
- A static UX audit notes operational text often at 12–13 px, excessive decorative cyan, poorly differentiated inset surfaces, and inconsistent focus rules. A Timox VASIO Control screenshot still needs review before final visual validation.

## Architecture decisions

### Control interface

The application keeps one Electron executable. Its header shows engine status and function navigation. The main area has three views:

1. **Configuration:** existing sections for profiles, physical clock, clients, channels, and routing, supplied exclusively by documented API routes and events.
2. **API:** Swagger UI loaded from packaged resources and the local OpenAPI file, with no CDN or external network service. Since WebSocket is not rendered as a standard OpenAPI REST route, this view also shows commands and examples from `x-websocket`.
3. **Diagnostics:** connection, engine, and client state; log level; debug activation; recent log lines; refresh button; and log export.

Electron preload exposes only narrow, validated functions. The renderer reads neither local files nor shared memory. Profiles, configuration, state, diagnostics, and engine actions pass through documented API routes or commands. Pure OS functions, such as opening an exported file or starting an executable, use dedicated IPC with strict validation.

### Engine lifecycle

Electron retains automatic connection/startup. The Logs view adds start and stop actions with confirmed state and result. Stopping requires zero connected ASIO clients. An existing engine not started by this window is never killed directly: it receives the orderly API stop command under the same zero-client rule. On launch, Electron checks the loopback API first and does not create a second engine when a valid TimoxVasio engine already responds.

Closing the window does not terminate an engine serving a client. The engine process is detached from the window and continues its lifecycle; stopping remains an explicit action when no client is connected. Ownership or attachment state and action results are shown to the user.

### Diagnostics API and logs

The engine writes timestamped entries to `%LOCALAPPDATA%\TimoxVasio\logs\engine.log`, with up to four 5 MiB archives. Each entry has a level, component, and message. Events cover startup/shutdown, API, ASIO opening, confirmed physical settings, clients, configuration, errors, and drop counters available outside callbacks.

Audio callbacks never log, access disk, allocate, or wait additionally. No audio samples or buffer contents are recorded. Normal level is `info`; debug adds control and allocation traces. The level persists in `%LOCALAPPDATA%\TimoxVasio\diagnostics.json`. `GET /api/v1/diagnostics?limit=N` returns the level and at most 500 most recent entries; default is 200. `PUT /api/v1/diagnostics` replaces the level with `{ "level": "info" }` or `{ "level": "debug" }`. The WebSocket command `{ "id": "stop-1", "command": "engine.stop" }` requests an orderly stop and is refused with `ENGINE_CLIENTS_CONNECTED` while an ASIO client is attached. The acceptance response is sent before the stop signal. Responses and errors are described in schemas, OpenAPI, Swagger, and contract tests.

### Visual presentation

- One font stack and a 16 px base size.
- General helper text and summaries use at least 14 px; dense matrix cells use at least 13 px.
- Cyan marks primary actions and landmarks; states use semantic colors with labels.
- Cards use two clearly distinct surfaces, legible neutral borders, and a shared radius scale.
- Fields, buttons, and tables share consistent height and visible focus. Decorative glows and unused legacy styles are removed once rendering confirms they are unnecessary.
- The Configuration/API/Diagnostics hierarchy remains usable at 1400 × 900 and in a smaller window.

### Delivery

The build produces the portable executable and Windows installer from the same interface, engine, and OpenAPI version. Publication targets the public TimoxVasio repository. The tag and assets are created only after build, tests, and previously confirmed Mixxx discovery are validated.

## Expected contracts

- `GET /api/v1/diagnostics?limit=N` returns `{level, entries}` with `{timestamp, level, component, message}` entries. `N` is an integer from 1 to 500; default is 200.
- `PUT /api/v1/diagnostics` accepts exactly `{level}` with `info` or `debug`; it returns the confirmed level and rejects unknown fields or levels.
- `engine.stop` is a WebSocket command with payload `{}`. It refuses requests while at least one client is attached, then publishes `engine.status: stopped` and cleanly shuts down the API and controller.
- Extend OpenAPI and `schemas/api-v1.json`; document every response, error, read limit, and state transition.
- Extend the API client and preload only with operations needed by the three views and lifecycle commands.
- Audio configuration and routes retain the complete `configuration.apply` format; no historical compatibility route remains.

## Verification and acceptance criteria

1. The packaged interface opens Configuration, Swagger, and Diagnostics without an external server; Swagger shows the shipped version's document and WebSocket `configuration.apply` command.
2. Physical settings and routes continue to come from the API. A complete configuration can be applied and read back from confirmed state.
3. Starting never launches two engines. Stop/restart is refused when a client is connected. Closing the UI leaves the engine active while a client uses it.
4. Logs persist after UI closure, rotate at the defined limit, can be viewed/exported, and distinguish startup, API failure, ASIO opening, and reconfiguration.
5. Debug mode can be toggled through the API, its state is confirmed in the UI, and it does not change real-time audio processing.
6. Contract tests cover success, limits, errors, and connected-client protection. UI tests cover navigation, settings, and diagnostics.
7. An Electron build screenshot shows text legibility, surface hierarchy, and keyboard focus. Portable and installer builds include the same API and engine sources.
8. After publication, the tag and assets exist in the public TimoxVasio repository. The SSL 12 audio path remains a separate validation: at this design point the API still reports zero routes and the engine is stopped.

## Out of scope

- Recording, analyzing, or transmitting audio samples in diagnostics.
- Exposing the control API to the network; it remains bound to `127.0.0.1`.
- Publishing to a repository unrelated to TimoxVasio.
- Changing Mixxx preferences or source.
- Claiming audio signal validation without actual playback and measured physical outputs.
