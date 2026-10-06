# TimoxVasio driver sequences

[Français](DRIVER_SEQUENCES.md) | **English**

This page describes observable exchanges among the ASIO host, DLL, API, engine, and physical driver. GitHub can render the Mermaid diagrams directly. They distinguish the channel count advertised to a host from the channels actually allocated and transported.

## 1. ASIO discovery and allocation

The host loads the driver registered under the stable name `TimoxVasio`, queries its capabilities, then selects the channels to open. The driver instance reads the profile before advertising its capabilities.

```mermaid
sequenceDiagram
    autonumber
    participant H as ASIO host
    participant D as TimoxVasio.dll / IASIO
    participant S as ApplicationProfileStore
    participant M as TimoxVasio engine

    H->>D: Create TimoxVasio COM instance
    D->>S: Resolve executable-name profile
    S-->>D: Effective input and output counts
    H->>D: init(systemHandle)
    D-->>H: ASIOTrue (metadata, no audio stream)
    H->>D: getChannels()
    D-->>H: Profile inputCount, outputCount
    loop For each channel requested by the host
        H->>D: getChannelInfo(direction, zero-based channel)
        D-->>H: Channel name, direction, format, and state
    end
    H->>D: createBuffers(sparse list, frames, callbacks)
    D->>D: Validate count, bounds, duplicates, and size
    D->>M: Attach transport and publish active channels
    M-->>D: Confirm client mapping
    D-->>H: ASE_OK or ASIO error
```

`init` lets the host request driver metadata even when the audio engine has not started. Buffer creation, however, requires the engine's transport and clock. Metadata probes alone therefore do not establish that audio streaming works.

Bounds are directional. With an input count of 255, valid indices are 0–254; index 255 is rejected for that direction. Output counts are checked separately. Requests may be sparse, but an out-of-range or duplicate channel in the same direction is rejected.

## 2. Per-application profile through the API and Swagger/OpenAPI

The API supports reading the complete list and replacing it atomically. Profiles are keyed by the executable basename, compared case-insensitively. A change does not alter an already open ASIO instance: the host must restart to query the driver again.

```mermaid
sequenceDiagram
    autonumber
    participant U as API client / Swagger UI
    participant A as Local HTTP API
    participant C as AudioController
    participant S as ApplicationProfileStore
    participant H as Active ASIO host
    participant D as New TimoxVasio instance

    U->>A: GET /api/v1/application-profiles
    A->>C: Read profiles
    C->>S: Load persistent state
    S-->>C: Explicit profiles
    C-->>A: Profiles
    A-->>U: 200 + profiles
    U->>A: PUT /api/v1/application-profiles {profiles: [...]}
    A->>A: Validate names and counts 1..256
    A->>C: Replace entire list
    C->>S: Atomic write
    S-->>C: Saved
    C-->>A: Profiles + restartRequiredClients
    A-->>U: 200 or structured error
    Note over H: Existing instance keeps advertised counts
    U->>H: Close and restart application
    H->>D: Create a new COM instance
    D->>S: Resolve updated profile
    S-->>D: New effective count
```

The `PUT` route replaces the complete list; sending `profiles: []` removes overrides and applies the 256/256 default. The initial 255/255 `mixxx.exe` profile is provided by default when the profile file does not exist. Saved changes are shared by the API and DLL through `%LOCALAPPDATA%\TimoxVasio\application-profiles.json`. The API and its normative OpenAPI schemas are described in [API.en.md](../API.en.md) and [`openapi-v1.json`](../openapi-v1.json).

Valid counts range from 1 through 256 inclusive. Each profile contains `processName`, `inputChannels`, and `outputChannels`. The API returns `restartRequiredClients` to identify connected clients whose effective capability changes. Host restart is manual; the API does not close any process.

## 3. Audio transport and routing

After ASIO allocation, each application's callbacks exchange samples with its shared ring. The physical driver's callback sets the processing cadence; the engine then applies the current graph.

```mermaid
sequenceDiagram
    autonumber
    participant P as Physical ASIO callback
    participant E as Engine / RoutingGraph
    participant RA as Shared ring A
    participant DA as TimoxVasio instance A
    participant A as Application A / ASIO callback
    participant RB as Shared ring B
    participant DB as TimoxVasio instance B
    participant B as Application B / ASIO callback

    P->>E: Physical callback (frames, inputs)
    E->>RA: Read available A output block
    E->>RB: Read available B output block
    E->>E: Apply routes, gains, and mute
    E->>P: Write routed physical outputs
    E->>RA: Publish routed virtual inputs
    E->>DA: Signal ready input block
    DA->>A: Call bufferSwitch / bufferSwitchTimeInfo
    A->>DA: Read inputs and produce outputs in ASIO buffers
    DA->>RA: Publish A outputs for a later engine callback
    E->>RB: Publish routed virtual inputs
    E->>DB: Signal ready input block
    DB->>B: Call bufferSwitch / bufferSwitchTimeInfo
    B->>DB: Read inputs and produce outputs in ASIO buffers
    DB->>RB: Publish B outputs for a later engine callback
```

On each physical callback, the engine reads available client output blocks, applies the graph, fills physical outputs, writes virtual inputs into the mappings, and wakes client callbacks. A client callback reads its ASIO inputs, lets the application produce outputs, then publishes them for a later engine callback. Callbacks do not communicate directly between applications; they pass through the engine and graph. The engine processes active channels declared at allocation.

Profiles do not resize the rings: each client keeps transport capacity for up to 256 channels per direction. They only limit what the application can discover and allocate. Thus two applications with different profiles can communicate through common channels made active and connected by API routes.

## 4. Sequence relevant to a Mixxx PR

The TimoxVasio workaround advertises 255 channels to `mixxx.exe`; it does not prove that Mixxx can represent 256. The examined stable version receives a count from ASIO/PortAudio, converts it to its `ChannelCount` type, then filters invalid values during inventory. At 256, the 8-bit type observed in that revision produces the invalid sentinel value 0.

```mermaid
sequenceDiagram
    autonumber
    participant T as TimoxVasio
    participant PA as PortAudio ASIO
    participant SD as SoundDevicePortAudio
    participant CC as mixxx::audio::ChannelCount
    participant SM as SoundManager

    T-->>PA: ASIO getChannels = 256/256
    PA-->>SD: maxInputChannels/maxOutputChannels = 256
    SD->>CC: Construct ChannelCount from int(256)
    Note over CC: In examined stable revision: uint8_t max=255
    CC-->>SD: Invalid value (0)
    SD->>SM: Publish converted capabilities
    SM->>SM: Discard device without valid capability
```

This diagram describes the path observed in the Mixxx commit identified in the [compatibility note](mixxx-256-channel-compatibility.en.md). For a Mixxx PR, include the exact commit, source lines for the type and conversion, the PortAudio trace receiving 256, and a focused test covering 255, 256, and the higher capability being rejected or represented. A general fix must preserve 256 through the type and discovery/selection/allocation paths; the TimoxVasio-side 255 profile is a workaround for the stable version, not proof of Mixxx compliance with 256 channels.

## 5. Index conventions

```mermaid
flowchart LR
    subgraph ASIO[Count returned by getChannels]
        C255["255 channels<br/>indices 0 … 254"]
        C256["256 channels<br/>indices 0 … 255"]
    end
    subgraph API[API endpoint numbering]
        A1["First channel: 1"]
        A256["Channel 256: 256"]
    end
    C255 -->|index 0 becomes API channel 1| A1
    C256 -->|index 255 becomes API channel 256| A256
```

ASIO `channelNum` is zero-based; endpoint identifiers published by the API use one-based channel numbers. The mapping preserves channel identity and does not imply that index 255 exists in a profile advertising 255 channels.
