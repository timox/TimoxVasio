# Remote Timox API Control Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task-by-task. Tests and gates are mandatory for implementation; no public listener is enabled by a build or install.

**Goal:** Allow a paired Linux client to read and control Timox over authenticated HTTPS/WebSocket while the engine's existing API remains bound to loopback.

**Architecture:** Add a separate opt-in Windows TLS gateway that authenticates a pinned Linux client certificate with Schannel mutual TLS and forwards HTTP/WebSocket traffic to the local API. Reuse `ByteStream`/`SchannelStream` over a new TCP byte-stream adapter; do not reimplement TLS. The Linux client pins the Windows server certificate; only public certificates are exchanged, while private keys remain local. PWAR remains the audio transport and is not extended with control messages.

**Tech Stack:** C++17, WinSock, Schannel/SSPI and CNG on Windows, OpenSSL client certificate verification on Linux, existing cpp-httplib HTTP/WebSocket API, Electron/React pairing UI.

**Spec:** [Remote Timox API pairing](../specs/2026-10-24-remote-timox-api-pairing.md)

## Global Constraints

- Keep the existing API listener on `127.0.0.1`.
- Remote access is disabled until the user enables a selected interface and pairs a client.
- TLS 1.2 minimum; no plaintext remote fallback.
- Never export or transfer either private key.
- Verify certificate usage, validity, and exact authorized thumbprint; revoke by closing active sessions.
- Remote clients receive API control only, never arbitrary shell, file, registry, driver, or kernel access.
- PWAR UDP audio transport remains unchanged.

## Review Focus

1. Unknown/revoked client, replaced server certificate, or failed fingerprint check must fail before any HTTP or WebSocket bytes reach the local API.
2. HTTP upgrade to WebSocket must remain a single authenticated TLS session and preserve the API subprotocol.
3. Selecting `0.0.0.0`, the wrong adapter, or a stale address must not silently widen exposure.
4. Revocation and shutdown must drain sockets and Schannel contexts before releasing buffers or certificates.
5. A gateway fault or remote client disconnect must not stop the local API or audio engine.

## Files and Responsibilities

- `src/remote_control_gateway_win32.{h,cpp}`: selected-interface TCP listener, Schannel server handshake, peer pin validation, and byte-stream proxy to the loopback API.
- `native-windows-audio/broker/tcp_stream_win32.{h,cpp}`: bounded connected WinSock stream implementing the existing `ByteStream` contract for the Schannel gateway.
- `src/remote_control_cert_store_win32.{h,cpp}`: user-scoped server identity, public certificate import/export, authorized client fingerprints, and revocation.
- `include/control_api_server.h`, `src/control_api_server.cpp`: local opt-in management endpoints for gateway status and pairing operations; existing bind address remains loopback.
- `CMakeLists.txt`: Windows targets and focused gateway/certificate tests.
- `api/openapi-v1.json`, `api/schemas/api-v1.json`, `docs/API.md`: local pairing/status contract and Linux HTTPS/WSS client examples.
- `gui/electron/main.js`, `gui/electron/preload.js`, `gui/src/RemoteApiDialog.js`, `gui/src/App.js`, `gui/src/ProfessionalTheme.css`: certificate export/import, peer fingerprints, interface selection, enable/disable, and revoke UI.
- `tests/remote_control_gateway_tests.cpp`: handshake, proxy, lifecycle, and revocation tests without audio hardware.

## Tasks

### 1. Certificate identity and pin validation

- Add tests for missing private key, server/client EKU mismatch, expiry, malformed certificate, unknown client thumbprint, mismatched pinned server thumbprint, and revocation.
- Implement user-scoped CNG server-key creation and certificate persistence. Export only DER/PEM public certificate bytes.
- Implement client-certificate import as a public certificate and exact SHA-256 pin authorization. Never import or accept a client private key on Windows.
- Verify expected rejection before integrating the network listener.

### 2. Schannel TLS gateway and bounded byte proxy

- Add tests that start a loopback fake HTTP server and reject clients without a valid mutual-TLS pair before forwarding any bytes.
- Implement `TcpByteStream : ByteStream` with deadline-aware overlapped WinSock I/O, bounded cancellation, and joined shutdown. Preserve the existing `SchannelStream` fixed-buffer and exact-peer-pin checks.
- Implement an opt-in listener bound only to one selected unicast adapter address and configured port; use the existing Schannel profile (TLS 1.2, ECDHE-ECDSA AES-GCM) and require a client certificate.
- Validate the Schannel peer certificate against the authorized fingerprint, client-auth EKU, validity interval, and revocation state after handshake; validate the server certificate with the expected server-auth EKU on Linux.
- Proxy the authenticated byte stream to `127.0.0.1:<apiPort>`, supporting HTTP keep-alive and WebSocket upgrade without changing the existing API contract.
- Bound connections and buffers; cancel and join all proxy operations before closing Schannel contexts or sockets.
- Confirm local API and audio keep running after gateway failure, client disconnect, revoke, and gateway stop.

### 3. Local management API and remote client contract

- This task depends on the authenticated local API migration in Lot 5 of
  `2026-10-22-native-audio-production.md`. Do not expose pairing mutations over
  the current unauthenticated loopback API or enable remote control before that
  migration is complete.
- Add loopback-only management routes to query gateway state, select the bind address/port, enable/disable, export the server public certificate, import/revoke client public certificates, and list peer fingerprints.
- Reject pairing-management routes through the remote gateway. Local Electron
  pairing actions must pass the authenticated local API and an explicit user
  confirmation before certificate import, listener enable, or revocation.
- Document Linux HTTPS/WSS usage with client key/certificate, server-certificate pin, and existing JSON commands. Keep credentials out of URLs and logs.
- Verify that an unauthenticated local HTTP request cannot enable or reconfigure the gateway once the API authentication work in the native-audio plan is active.

### 4. Windows pairing and remote-access UI

- Add the interface selector, bind address/port, enable state, Windows certificate fingerprint/export, Linux certificate import, authorized peer list, and revoke action.
- Show exact copy/import steps and state plainly that private keys never leave either computer.
- Display firewall reachability separately from TLS pairing. Never create a broad firewall rule or bind to all adapters automatically.
- Keep all remote-access controls out of the audio-render callback and preserve the API's transactional configuration revision checks.

### 5. Qualification and documentation

- Verify no data reaches HTTP/WebSocket before successful mTLS; reject wrong peer, wrong server pin, expired cert, wrong EKU, and revoked peer.
- Verify REST state reads and WebSocket commands/events over the gateway; verify that changing/revoking a pairing closes existing connections.
- Verify a local API client continues working and an engine/audio restart does not silently re-enable the gateway.
- Run the focused Windows gateway/certificate target, API contract tests, and GUI tests; perform a Linux-to-Windows interoperability run before release.
- Record certificate lifecycle, firewall requirements, API scope, and a repeatable Linux client example in `docs/API.md`.

## Completion Boundary

The feature is not complete until a Linux client can authenticate, read Timox state, issue a configuration command, observe the WebSocket result, and be revoked, while a rejected peer receives no API data and PWAR audio/local Timox operation continue independently.
