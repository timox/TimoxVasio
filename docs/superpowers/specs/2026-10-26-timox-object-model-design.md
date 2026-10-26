# TimoxVasio object model: two state owners

**Status:** proposal for review. This document records a model boundary; it does
not change the current API or implementation.

## Intent

Make the API and its OpenAPI description read like the object model already
present in TimoxVasio. Keep configuration, runtime state, and system
observations distinct, and give each datum one owner. The existing audio graph,
ASIO client attachment, WASAPI copy, native audio, and performance capabilities
remain in scope. This proposal does not redesign their behavior.

## Root ownership

The model has two state-owning aggregates. These are data ownership boundaries,
not a claim that every aggregate must become a C++ class or a new endpoint.

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
- `VirtualAsioDriver` and its attached `AsioClientSession[]`.
- Effective `WasapiBridgeSession[]` and `NativeAudioSession`.
- Runtime measurements, buffer health, meters, correlation, and errors, with
  their observation time and provenance where available.

The target engine model should report which configuration revision it has
applied. Today the API exposes one in-memory `configurationRevision`; it does
not expose a distinct applied revision, particularly when the engine is
stopped. A mismatch should become visible as pending or unapplied configuration,
rather than a second ambiguous configuration list.

## Client, copy scope, device, and endpoint

An **ASIO client session** is direct adhesion: a process opened
`TimoxVasio.dll` and its attachment to the driver's shared mapping is confirmed.
The session owns the ASIO ports allocated by `createBuffers`. Callback and
block-exchange counters show driver activity, but do not prove that the buffers
contain audible signal. The current API reports only PID and process name, so
it must not claim durable identity for a process instance. Session identity is
runtime-scoped unless creation time or executable path is added and verified.

A **WASAPI process-copy definition** identifies a process tree to copy from.
Timox creates a distinct graph stream from shared audio; this does not establish
that the application selected WASAPI, attach it as an ASIO client, or redirect
its original output. The resulting bridge session and graph ports belong to
`AudioEngine`, while the selected scope belongs to `AudioConfiguration`.

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
configuration remains integration settings; a PWAR ASIO connection, when
opened, appears as an ordinary attached ASIO client.

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
configuration persistence as separate services. ASIO clients remain resources
of the virtual driver session, not top-level application roots.

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
