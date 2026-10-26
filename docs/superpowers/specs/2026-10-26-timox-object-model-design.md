# TimoxVasio object model: two state owners

**Status: superseded — historical proposal.** The current design is
[adhérents, nœuds de connexion et supervision](2026-10-26-adherents-noeuds-supervision-design.md).
It replaces the two-aggregate framing and GUI-owned common client identity.
The content below is retained as history, not as implementation instructions.

## Intent

Make the API and its OpenAPI description read like the object model already
present in TimoxVasio. Keep configuration, runtime state, and system
observations distinct, and give each datum one owner. The existing audio graph,
ASIO client attachment, WASAPI copy, native audio, and performance capabilities
remain in scope. This proposal does not redesign their behavior.

## Root ownership

The model has two state-owning aggregates. These are data ownership boundaries,
not a claim that every aggregate must become a C++ class or a new endpoint.
Within the **TimoxVSSC** application, `VSSCClient` is a general graph
participant; ASIO is one way to participate, not the definition of a client.

### `AudioConfiguration`

The desired, persisted configuration owned by the TimoxVasio controller:

- `ClockSelection`: ASIO or software clock; selected physical driver when
  applicable; requested sample rate and buffer size.
- `RouteDefinition[]`: stable route identity, source and destination port
  references, gain, and mute state.
- `WasapiCopyDefinition[]`: enabled state, capture/render/endpoint-copy or
  process-copy scope, endpoint/default-role or process identity, and requested
  buffer duration.
- `NativeAudioIntent`: requested enablement only.
- `ApplicationChannelProfile[]`: virtual-driver compatibility policy for
  applications. A profile is not an attached client or an observation.
- `IntegrationSettings`: for example PWAR address and port. Saved settings do
  not imply that a client is connected or exchanging audio.

The persisted document currently has a format version but no revision.
`configurationRevision` is maintained in memory, and
`expectedConfigurationRevision` is optional today. The target model should
define how desired and applied revisions are represented, whether the expected
revision is required for mutations, and whether a revision is persisted. These
are proposed contract decisions, not current behavior.

### `AudioEngine`

The applied and observed runtime owned by the running engine:

- `EngineRunState` and the effective clock and physical ASIO session, including
  negotiated format and capabilities obtained by opening the selected driver.
- `AppliedGraph`, with route activations linked to their `RouteDefinition` by
  ID and revision. Activation distinguishes active, dormant, and rejected
  routes without treating configuration intent as proof of an active path.
- Runtime resources: TimoxVasio virtual ASIO attachment sessions, effective
  WASAPI bridge sessions, and the native audio session.
- Runtime measurements, buffer health, meters, correlation, and errors, with
  their observation time and provenance where available.

TimoxVSSC owns the generic `VSSCClient` identity and graph role. The
TimoxVAudioEngine API exposes transport-specific runtime resources; TimoxVSSC
may map those resources to its clients. This mapping and VSSC client IDs are
not currently part of the engine API, whose implementation still resides in
the TimoxVasio repository. TimoxVSSC may also create a client and use a
TimoxVWasapi bridge as its transport.

The target engine model should report which configuration revision it has
applied. Today the API exposes one in-memory `configurationRevision`; it does
not expose a distinct applied revision, particularly when the engine is
stopped. A mismatch should become visible as pending or unapplied configuration,
rather than a second ambiguous configuration list.

## TimoxVSSC client, transport, copy scope, device, and endpoint

A **VSSC client** is any participant that TimoxVSSC admits to its graph and can
address through graph ports. The client has a transport-specific participation
mode; its process does not need to open the ASIO driver to be a VSSC client.
The identity and graph role belong to TimoxVSSC. The engine API does not
currently publish a common client type or a mapping from bridge IDs to VSSC
graph IDs; it publishes ASIO attachments and WASAPI bridge runtime resources.

- An **ASIO client** is direct adhesion: an application opened
  `TimoxVasio.dll` and its attachment to the driver's shared mapping is
  confirmed. It owns the ASIO ports allocated by `createBuffers`. Callback and
  block-exchange counters show driver activity, but do not prove audible signal.
  The current API reports PID and process name, so it cannot claim durable
  identity for a process instance. Session identity is runtime-scoped unless
  creation time or executable path is added and verified.
- A **WASAPI endpoint client** is a TimoxVSSC-managed capture or render stream
  opened against a Windows endpoint. The endpoint is its transport binding; it
  is not an ASIO client.
- A **WASAPI process-copy client** is created when TimoxVSSC opens a process-copy
  stream for a selected process tree. The process identity is the source scope;
  the TimoxVSSC-managed stream is the graph client, and the process itself is
  not.
  This copies shared audio and creates a distinct graph stream. It does not
  establish that the application selected WASAPI, attach it to the ASIO driver,
  or redirect its original output.

A process-copy definition belongs to `AudioConfiguration`; its active
TimoxVSSC-managed client and graph ports map to the `AudioEngine` bridge
session. The same
configuration/runtime distinction applies to endpoint streams.

A **Windows endpoint** is one capture or render endpoint with its own ID and
state. A **physical device** may group multiple endpoints, for example capture
and render endpoints sharing a Windows container ID. They are separate
inventory objects and must not be presented as interchangeable.

## System inventory and cross-cutting services

`SystemInventory` is observed catalog data, not a third state-owning root. It
contains physical ASIO drivers, WASAPI endpoints, and shared Windows audio
processes, with enumeration errors. The current schemas do not expose a common
inventory refresh timestamp; adding freshness metadata is a possible contract
improvement. Configuration and runtime refer to inventory identities; a
discovered item does not mean it is selected, open, or active.

Diagnostics remain a cross-cutting service. Performance measurements and
health observations belong to the engine session that produced them. PWAR
configuration remains integration settings; when PWAR opens TimoxVasio, it is
an ASIO client with a PWAR integration label. Its product role does not change
the transport type.

## Desired, applied, observed

| State | Owner | Examples | Meaning |
|---|---|---|---|
| Desired | `AudioConfiguration` | selected driver, route definitions, WASAPI copy scopes | User intent saved by Timox |
| Applied | `AudioEngine` | effective clock, graph generation, activated routes | What the engine accepted and installed |
| Observed | inventory or engine observation | endpoint state, attached client, callbacks, meters, errors | Evidence with source and time; not inferred from intent |

For routes, the configured definition and runtime activation are related by a
stable ID and configuration revision. They are not two unrelated arrays.

## Proposed OpenAPI reading order

Swagger should introduce the contract through two root schemas:

1. `AudioConfiguration` — desired state and its mutation/revision rules.
2. `AudioEngine` — applied state, attached clients, active graph, and runtime
   observations.

System inventory is a linked observation resource used by those roots. Detailed
driver, route, bridge, performance, and diagnostic operations can then be
grouped under the owner that governs them. The existing `GET /api/v1/state` may
remain a convenient combined snapshot, but its schema should compose these
named objects instead of exposing every subsystem as an unrelated peer field.

## Code organization direction

The code already has useful internal boundaries in `AudioControllerConfiguration`,
`AudioControllerSnapshot`, `VasioClientManager`, and `AudioRoutingRuntime`.
Preserve these boundaries. First extract clear API/domain value types and
ownership from the flat `State` serializer in `src/control_api_server.cpp`;
avoid a wholesale engine rewrite. Keep discovery, runtime sessions, and
configuration persistence as separate services. ASIO attachment details remain
resources of the virtual driver session. The TimoxVSSC-level client model can
represent ASIO, WASAPI endpoint, and WASAPI process-copy participants uniformly
without pretending they share the same transport or evidence.

## Open decisions before implementation

- Whether `ApplicationChannelProfile[]` is represented as part of
  `AudioConfiguration` or as a separate virtual-driver policy resource.
- Whether PWAR settings move into the versioned configuration document or
  remain a separately stored integration resource.
- What stable session identifier and process identity Windows exposes for an
  attached ASIO client, beyond the current PID and process name.
- How much inventory detail is included in the combined state snapshot versus
  fetched through dedicated inventory operations.

These decisions affect persistence and API shape; implementation should follow
review of this proposal rather than silently choosing a migration.
