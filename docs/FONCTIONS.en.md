# Using Timox VASIO Control

[Français](FONCTIONS.md) | **English**

This guide describes Timox VASIO Control in [TimoxVasio 1.2.4](https://github.com/timox/TimoxVasio/releases/tag/v1.2.4). The application has six views:

| View | Main use |
| --- | --- |
| **Configuration** | Select the physical ASIO driver, sample rate, and buffer size; configure per-application channel profiles. |
| **Channels** | Inspect connected applications, open and routed channels, and meters; edit routes with Quick routing, the graphical patchbay, or the matrix. |
| **Analysis** | Follow L/R correlation for an open, routed stereo output pair. |
| **Performance** | Check that the physical driver callback fits within one block and measure each stage of the audio path. |
| **API and Swagger** | Read the embedded contract and send HTTP requests or WebSocket commands from the integrated console. |
| **Logs** | Inspect engine events and set the diagnostic level. |

## Three components, two paths

```mermaid
flowchart LR
    A[ASIO audio applications] <-->|ASIO buffers| D[TimoxVasio.dll]
    D <-->|shared audio transport| E[TimoxVirtualAsioEngine.exe]
    E <-->|ASIO callbacks and clock| P[Physical ASIO driver]
    U[Timox VASIO Control] <-->|local HTTP and WebSocket| API[Engine API]
    API <--> E
```

Samples follow the audio path. Control, Swagger, and the console use the local API to read state and send commands; they do not transport samples. The engine must be running before an ASIO application opens TimoxVasio buffers. See [Installation](INSTALL.en.md) for startup order.

## Open channels, routes, and meters

An application profile can advertise up to 256 inputs and 256 outputs. Only channels it opens through ASIO become active endpoints. An open channel is not automatically routed. In **Channels**, meters appear only for the application's channels that are both open and part of an active route.

**Overview** summarizes used and available physical channels and application flows. **Quick routing** suggests a mono or stereo connection between source and destination groups; you can change the proposed channels. Routes can also be created in the graphical **patchbay** (ports grouped by application, visible cables, a connection inspector) or in the **matrix**, which suits dense routing. Saved connections are grouped by source/destination pair; each expandable group shows a compact channel range, applied or pending status, and an active route count, with controls for individual routes inside. An applied route becomes active only when both ASIO channels are open. Audio changes remain in the draft until **Apply configuration**. One or more selected connections can receive a label and a color; these choices exist only in the cabling view and change neither the engine's routes nor the audio processing.

```mermaid
flowchart LR
    O[Channel opened by application] --> R{Active route?}
    R -- No --> N[Channel visible without meter]
    R -- Yes --> M[Meter in Channels]
    M --> S[WebSocket audio.meter event]
```

`audio.meter` associates a dBFS peak with an `endpointId` and includes cumulative `underruns` and `overruns` counters for client transport:

```json
{
  "event": "audio.meter",
  "payload": {
    "endpointId": "virtual:TimoxVasio:1234:output:1",
    "peakDbfs": -9.0,
    "underruns": 0,
    "overruns": 0
  }
}
```

The API emits measurements about twice per second. Silence is represented as `-120` dBFS. The UI bar covers `-60` to `0` dBFS while its numeric label retains the received value. These counters describe virtual transport rings; alone they do not measure missed physical callback deadlines.

## L/R stereo correlation

**Analysis** offers adjacent output pairs from one application when both channels are open and routed. Select a pair and start analysis. The graph plots a coefficient from `-1` to `+1`:

| Value | Meaning |
| --- | --- |
| Near `+1` | Signals vary together. |
| Near `0` | Weak correlation over the measured window. |
| Near `-1` | Signals vary in opposition. |
| `null`, state `no_signal` | At least one channel is silent or too quiet for reliable measurement. |

The engine computes a normalized, zero-lag correlation over roughly 100 ms windows. If either channel's RMS is below `-90` dBFS, it reports `no_signal`. The coefficient describes the relationship between L and R, not a phase angle in degrees; `0` does not mean silence.

```mermaid
sequenceDiagram
    participant U as Analysis / API client
    participant A as WebSocket API
    participant E as Audio engine
    U->>A: Read /api/v1/state and stereoPairs
    U->>A: audio.correlation.start {stereoPairId}
    A->>E: Select two active outputs
    loop About 10 times per second
        E-->>A: Coefficient or no signal
        A-->>U: audio.correlation {correlation, state}
    end
    U->>A: audio.correlation.stop
```

## Persistent routes across reconnections

`GET /api/v1/state` contains `configuredRoutes` (all saved routes) and `routes` (those currently active with open channels). A virtual route saved under an executable name, such as `virtual:TimoxVasio:app:renoise.exe:output:1`, waits for that application to reconnect. Once its channel opens, the API reports the active route with the current PID in `endpointId`.

```mermaid
stateDiagram-v2
    [*] --> Waiting: Route saved by executable name
    Waiting --> Active: Application connects and opens channel
    Active --> Waiting: Application closes or removes channel
```

For example, with no application connected, state may show eight `configuredRoutes` and zero `routes`. The engine retains the eight routes and does not start a physical audio stream until a route becomes active. To create a new waiting route, the application must have a profile in `GET /api/v1/application-profiles`, and the channel number must fit that profile. `configuration.apply` replaces the entire list: send **all** routes you want to keep. Reconfiguration may briefly interrupt audio.

## Performance: callback budget and stage timing

The **Performance** view answers two questions: does the physical driver callback fit within one block, and where does the time go between the application and the interface.

**Physical callback budget.** For each five-second window the engine publishes `bufferHealth` in `GET /api/v1/state`: the slowest callback, the time available for one block (`bufferFrames / sampleRate`), the remaining margin or overrun (*Time left* / *Over time*), the number of delayed starts, processing overruns and observed callbacks. The state is `healthy`, `warning`, or `insufficient` when the window does not hold enough callbacks. A delayed start is a scheduling observation, not proof of an audible dropout. Nothing is adjusted automatically: raising the buffer size remains the user's decision.

**Stage timing.** The *Enable stage measurements* checkbox sends the `performance.measurements.set` WebSocket command. Collection is off when the engine starts; once enabled it samples at most one callback in sixteen and publishes, for the last complete five-second window, the mean, worst value and sample count of each stage:

| Stage | What is measured |
| --- | --- |
| *Host callback* | The ASIO application's `bufferSwitch` call, that is all the work it does during that call. |
| *Virtual ring write* | Copying the application's outputs into the shared ring. |
| *Ring wait* | The wait between publishing a complete block and the engine reading it. |
| *Client read* | The physical callback reading the client rings. |
| *Routing* | Applying routes, gains and mutes. |
| *Physical output copy* | Copying into the physical driver's buffer. |

An unobserved stage stays blank. The physical copy ends at the driver handoff: these numbers measure neither hardware latency nor when sound is heard, and worst values from separate stages do not add up to a total latency.

```json
{ "id": "measure-1", "command": "performance.measurements.set", "payload": { "enabled": true } }
```

The command is idempotent; `false` stops collection and clears the window. `GET /api/v1/state` exposes `performanceMeasurements` with `enabled`, `sampleEveryCallbacks` (16) and `window` (`null` until a window completes). In PowerShell:

```powershell
$state = Invoke-RestMethod 'http://127.0.0.1:52525/api/v1/state'
$state.bufferHealth | Format-List state, callbackCount, delayedStartCount, processingOverrunCount
$state.performanceMeasurements.window.physical | Format-List
```

## Integrated API console and programming examples

Open **API and Swagger**. Swagger describes HTTP endpoints, WebSocket commands, and schemas. The **API Console** provides request templates, a JSON editor, and a response panel. `configuration.apply` and `engine.stop` require confirmation in Control.

To read state without changing the engine, select **HTTP request** and enter:

```json
{ "method": "GET", "path": "/api/v1/state" }
```

The same request in PowerShell (`52525` is the default port; if the engine picked another one, Control shows it):

```powershell
$state = Invoke-RestMethod 'http://127.0.0.1:52525/api/v1/state'
$state.engine | Format-List
$state.configuredRoutes | Format-Table id, sourceEndpointId, destinationEndpointId
$state.stereoPairs | Format-Table id, label
```

To start correlation in the WebSocket console, use an `id` actually returned in `stereoPairs`:

```json
{
  "id": "analysis-1",
  "command": "audio.correlation.start",
  "payload": { "stereoPairId": "stereo:TimoxVasio:1234:output:1-2" }
}
```

To receive measurements in Node.js, install `ws` with `npm install ws` and use the documented `vasio.api.v1` subprotocol:

```javascript
const WebSocket = require('ws');
const socket = new WebSocket('ws://127.0.0.1:52525/api/v1/ws', 'vasio.api.v1');

socket.on('message', raw => {
  const message = JSON.parse(raw.toString());
  if (message.event === 'audio.meter') {
    const { endpointId, peakDbfs } = message.payload;
    console.log(endpointId, `${peakDbfs.toFixed(1)} dBFS`);
  }
  if (message.event === 'audio.correlation') {
    const { correlation, state } = message.payload;
    console.log(state, correlation);
  }
});

socket.on('error', error => console.error(error.message));
```

To activate correlation in a program, first read `GET /api/v1/state`, select a pair from `stereoPairs`, and send the `audio.correlation.start` command above. A missing or unrouted pair is rejected. See the [API contract](API.en.md) and [API quick start](API_QUICKSTART.en.md) for configuration and diagnostics.
