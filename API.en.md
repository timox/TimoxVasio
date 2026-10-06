# TimoxVasio local control API v1

[Français](API.md) | **English**

This API is the contract between the control application and the native engine. Electron main is its client; the renderer reads neither the registry, configuration file, nor shared audio memory. For an introduction with diagrams and PowerShell/Node.js examples, see the [API quick start](docs/API_QUICKSTART.en.md). The normative machine-readable contract is [OpenAPI 3.1](openapi-v1.json), including its `x-websocket` extension, and the [shared JSON schemas](schemas/api-v1.json). The [1.1.0 feature guide](docs/FONCTIONS_1.1.0.en.md) illustrates measurements and the integrated console.

The native server uses `cpp-httplib` 0.58.0 for HTTP/WebSocket and `nlohmann/json` 3.12.0 for JSON. Dependency notices and licenses are retained in `vendor/`. The physical inventory enumerates registered ASIO drivers without opening them. Until the engine explicitly selects and opens one, `capabilitiesKnown` is `false` and its capabilities are empty.

Messages use UTF-8 JSON. Units are explicit: sample rate in hertz, buffer size in frames, channel numbers starting at 1, gain in dB, and peak level in dBFS.

## Address and reads

The engine listens only on loopback `127.0.0.1`. A startup port can be specified; `0` requests a free port, which the client obtains from the engine startup information.

| Request | Response |
| --- | --- |
| `GET /api/v1/state` | Confirmed engine state, physical selection, TimoxVasio configuration, routes, inventories, active stereo pairs |
| `GET /api/v1/drivers` | Discovered physical ASIO drivers, the virtual TimoxVasio driver, and measurable stereo pairs |
| `GET /api/v1/openapi.json` | Locally served OpenAPI document for Swagger UI |
| `GET /api/v1/schemas/api-v1.json` | Shared JSON schema referenced by OpenAPI |
| `GET /api/v1/application-profiles` | ASIO channel-capacity profiles by executable name |
| `PUT /api/v1/application-profiles` | Complete, persistent replacement of profiles |
| `ws://127.0.0.1:<port>/api/v1/ws` | Configuration commands and state/audio events |

HTTP requests use `http://127.0.0.1:<port>`. Responses conform to `definitions.state` or `definitions.drivers`. A physical driver reports input/output ports, reported sample rates, and buffer capabilities after opening. TimoxVasio reports detected clients and only their `createBuffers`-allocated channels.

Endpoint ID forms:

```text
physical:<driverId>:input:<channel>
physical:<driverId>:output:<channel>
virtual:TimoxVasio:<client-pid>:input:<channel>
virtual:TimoxVasio:<client-pid>:output:<channel>
```

The channel suffix is a decimal number starting at 1. Reuse IDs returned by the engine verbatim. `GET /api/v1/state` distinguishes active `routes` from all saved `configuredRoutes`. A route for an absent application stays configured with a stable ID such as `virtual:TimoxVasio:app:<executable>:output:<channel>` or its `input` form, and activates when that application opens its channels again, even with a different PID. A new stable ID is accepted when the executable has an `application-profiles` entry and the channel lies within its advertised capacity. If several running instances share an executable name, the route waits for an unambiguous match. Old PID-only routes cannot be attributed to another application after that process exits; they remain saved for explicit correction.

## Per-application capacity profiles

`GET /api/v1/application-profiles` returns the complete explicitly configured list:

```json
{
  "profiles": [
    { "processName": "mixxx.exe", "inputChannels": 255, "outputChannels": 255 }
  ]
}
```

`processName` must be a Windows executable filename without a path; matching is case-insensitive. An unlisted application receives 256 inputs and 256 outputs. The initial Mixxx profile is 255/255 because the target stable version interprets a capacity of 256 as zero. The limit applies to `getChannels`, `getChannelInfo`, and `createBuffers`, not to the 256-slot shared transport.

`PUT /api/v1/application-profiles` replaces the entire list:

```json
{
  "profiles": [
    { "processName": "mixxx.exe", "inputChannels": 255, "outputChannels": 255 },
    { "processName": "another-host.exe", "inputChannels": 128, "outputChannels": 64 }
  ]
}
```

Counts are inclusive from 1 to 256. An empty list clears overrides and restores the 256/256 default. The response confirms saved profiles and lists affected connected clients in `restartRequiredClients`. An initialized host retains its existing `getChannels` result until manually closed and restarted; the API does not restart it. An invalid request returns HTTP 400 with `INVALID_APPLICATION_PROFILES`. Storage is atomic; if saving fails, the previous active profiles remain.

After a physical driver is explicitly opened, endpoint `name` uses `IASIO::getChannelInfo` when usable, otherwise a fallback assembled from driver name, direction, and channel number. Before opening, physical endpoints and capabilities are empty.

## WebSocket commands

Each command is JSON:

```json
{
  "id": "request-42",
  "command": "configuration.apply",
  "payload": {
    "physicalDriverId": "physical-driver-stable-id",
    "sampleRate": 48000,
    "bufferFrames": 256,
    "routes": [{
      "id": "route-1",
      "sourceEndpointId": "virtual:TimoxVasio:1234:output:1",
      "destinationEndpointId": "physical:physical-driver-stable-id:output:1",
      "gainDb": 0.0,
      "mute": false
    }]
  }
}
```

`id` is a nonempty 1–128-character string unique among pending commands on the connection. `configuration.apply` replaces the physical driver, sample rate, buffer size, and entire route list. The whole object is validated before mutation. Repeating the command does not duplicate routes, but reconfiguration may briefly interrupt audio.

`sampleRate` and `bufferFrames` are values for the chosen physical driver. `null` requests its current rate and preferred size, respectively. The engine sets the physical rate, re-reads rate-dependent capabilities, and refuses an unadvertised size. TimoxVasio receives these exact values; it has no independent rate or size setting.

`GET /api/v1/state` and `GET /api/v1/drivers` include `stereoPairs`: adjacent L/R output channels of one client, both open and routed. `audio.correlation.start` selects one by `stereoPairId`; `audio.correlation.stop` ends measurement. Selection is global to the engine; only one pair is measured at a time.

`{ "id": "start-1", "command": "engine.start" }` and `{ "id": "stop-1", "command": "engine.stop" }` need no payload. `engine.start` reapplies the last confirmed configuration and is idempotent; the engine stays stopped if no route is configured. `engine.stop` stops audio, closes the physical driver, and retains configuration for a later `engine.start`. It is rejected with `ENGINE_CLIENTS_CONNECTED` while a TimoxVasio ASIO client is connected. Neither command terminates the host process or HTTP/WebSocket server. An external program can therefore control the engine once Control or a supervisor has started the API host; the API cannot launch a nonexistent host process.

Success is acknowledged under the same `id`:

```json
{ "id": "request-42", "success": true, "result": { "accepted": true } }
```

Failure returns a structured error:

```json
{
  "id": "request-42",
  "success": false,
  "error": {
    "code": "UNSUPPORTED_SAMPLE_RATE",
    "message": "The physical driver does not support 88200 Hz",
    "operation": "open",
    "driverId": "physical-driver-stable-id",
    "asioError": -998
  }
}
```

Codes are stable; messages can be localized:

| Code | Meaning |
| --- | --- |
| `INVALID_CONFIGURATION` | Missing fields or malformed configuration |
| `UNKNOWN_ENDPOINT` | Endpoint absent from the current inventory |
| `INVALID_ROUTE_DIRECTION` | Route is not one of the three allowed types |
| `UNSUPPORTED_SAMPLE_RATE` | Selected physical driver does not accept the rate |
| `UNSUPPORTED_BUFFER_SIZE` | Driver refuses the requested frame count |
| `PHYSICAL_DRIVER_OPEN_FAILED` | Physical driver cannot be opened or started |
| `AUDIO_ENGINE_UNAVAILABLE` | Engine or a VASIO client transport is unavailable |
| `INTERNAL_ERROR` | Unclassified error while applying configuration |

## Reconfiguration interruption

An effective change stops audio before destroying or rebuilding buffers and the graph. The observable sequence is: (1) command response `accepted`; (2) `engine.status` with `state: "reconfiguring"`; (3) `engine.status` with `state: "stopped"` if the physical driver is configured but no route exists, with capabilities available through `/api/v1/drivers`; (4) `engine.status` with `state: "running"` if a route exists and validation, opening, buffer creation, and start succeed; otherwise (5) `engine.error` followed by `engine.status` with `state: "error"`. On error, the engine remains stopped and does not restore the old graph.

Even an identical active configuration may rebuild the stream. An invalid command is rejected before interruption. Control disables mutations while `reconfiguring` and displays the engine-confirmed state. `stopped` can mean a configured driver with no routes or a stream stopped by `engine.stop` with saved routes ready for `engine.start`. The HTTP/WebSocket server remains available while the engine process lives; closing the Electron window leaves the engine available to clients that depend on its API.

## Events

Events have `{ "event": "<name>", "payload": { ... } }` and no `id`.

| Event | Payload |
| --- | --- |
| `engine.status` | Complete `state`, `physicalDriverId`, `sampleRate`, `bufferFrames`, `lastError` |
| `devices.changed` | `physicalDrivers` and `virtualDrivers` inventories matching `/api/v1/drivers` |
| `routes.changed` | Complete, confirmed `routes` list |
| `audio.meter` | `endpointId`, `peakDbfs`, cumulative `underruns` and `overruns` |
| `engine.error` | Structured `code`, `message`, and optional operation, driver, ASIO code |
| `audio.stereoPairs.changed` | `{ "stereoPairs": [...] }`, complete list of open and routed L/R pairs |
| `audio.correlation` | `stereoPairId`, L/R IDs, coefficient in `[-1, 1]` or `null`, and `state` (`measuring`, `no_signal`, `stopped`) |

Meter events for routed endpoints are emitted about twice per second. `peakDbfs` describes the most recent observed audio block. Virtual endpoints report cumulative counters for the corresponding client-ring direction; physical endpoint counters are zero. Events do not carry audio. The server publishes measurements outside the real-time callback.

Correlation compares synchronous samples of two client outputs in 100 ms windows, computing a mean-centered, normalized zero-lag coefficient at 10 Hz. Below `-90` dBFS RMS on either channel, `correlation` is `null` and `state` is `no_signal`. Zero means weak correlation, not silence. The coefficient is broadband coherence, not a phase angle in degrees. PCM samples remain in the native callback and never enter the API.

## Routing rules

A route connects individual channels and has `id`, `sourceEndpointId`, `destinationEndpointId`, `gainDb` from -120 to +24, and `mute`. Only these links are allowed:

1. TimoxVasio output to TimoxVasio input.
2. TimoxVasio output to physical ASIO output.
3. Physical ASIO input to TimoxVasio input.

Endpoint IDs must exist and directions must match. Incompatible rates or formats are rejected; no implicit sample-rate conversion is provided. The API reports only client-allocated channels; physical endpoints reflect capabilities discovered after applying the rate.

## Local security and client boundary

The engine does not bind external network interfaces. It accepts local WebSocket connections with no `Origin` header from native clients and from the declared VASIO application origin; other origins are rejected. Control reads inventories, routes, errors, and diagnostics only through the API. Shared-audio-memory structures remain private to engine and DLL.

## Diagnostics and lifecycle

`GET /api/v1/diagnostics?limit=N` returns log level (`info` or `debug`) and recent entries. `limit` defaults to 200 and accepts 1–500. Entries have `timestamp`, `level`, `component`, and `message`.

`PUT /api/v1/diagnostics` accepts exactly `{ "level": "info" }` or `{ "level": "debug" }` and persists it. Logs are written to `%LOCALAPPDATA%\TimoxVasio\logs\engine.log` with four archives. Audio callbacks never write logs. `engine.start` and `engine.stop` control audio without closing the API host; `engine.stop` is refused while a TimoxVasio client is attached.
