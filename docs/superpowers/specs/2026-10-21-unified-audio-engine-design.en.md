# Unified ASIO/WASAPI engine

[Français](2026-10-21-unified-audio-engine-design.md) | **English**

Confirmed goal: a functional counterpart to PipeWire on Windows, connecting ASIO/WASAPI applications and devices. The maintainer asked to reorganize the plan and begin implementation.

## Responsibilities

TimoxVasio.dll remains the virtual ASIO driver: host interface, profiles, buffers and existing transport. It does not open WASAPI devices or contain sample-rate conversion. Its independence concerns backend choice; it still relies on the engine for scheduling and routing.

The engine owns the graph, routes, formats, clock policy and backends. The ASIO host preserves current behavior. Shared WASAPI backends provide capture/render/loopback without a physical ASIO card.

A software clock is the initial design choice for virtual routing and shared WASAPI without an ASIO card; it must be qualified before delivery. It never runs concurrently with a hardware clock on the same graph. Master changes are explicit and coordinated. Device clocks connect through rings and adaptive conversion outside the graph callback.

Loopback does not create a virtual microphone. A third-party virtual cable supplies Windows endpoints initially; native endpoints are a separate later increment. Exclusive WASAPI remains optional.

## Product acceptance

- ASIO to ASIO without a physical card.
- Windows source to an ASIO application.
- ASIO to a shared WASAPI device.
- Graph to a microphone selectable by a Windows application through a virtual cable.
- Routes survive disappearing applications and devices.

## Invariants

Additional maintainer requirement: graphical control, API coverage and performance must reach at least the current TimoxVasio level. Control is the unified interface; essential operations cannot remain probe-only or code-only. Preserve routing tools, profiles, diagnostics, measurements and transactional editing. Compare the ASIO baseline and new paths on the same machine/load using callback distributions, CPU usage, end-to-end latency, incidents and conversion cost. Distinguish added bridge work from regressions in the ASIO path without bridges.

Preserve existing ASIO mode. No added blocking callback work. Immutable format/memory per generation, retirement before release, no concurrent ring reset. Validate before publication and use configuration revisions transactionally. API-only control. UI follows AGENTS.md.

## First increment

Decouple AudioController from PhysicalAsioHost through AudioClockHost. Neutral types retain compatible aliases. Injectable factory runs on the worker, with ASIO as the unchanged default. Test negotiation and lifecycle without hardware; retain runtime suites for audio data. No public JSON or transport ABI changes.

Add a DLL source/dependency boundary check with negative fixtures. This static audit is neither a full CMake interpreter nor proof against dynamic loading.

## Beyond the first increment

Software clock, WASAPI backends, clock API contract, bridge UI and qualification remain scheduled next. No implicit installation, signing or distribution.
