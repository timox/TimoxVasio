# Integration contract

[Français](INTEGRATION.md) | **English**

The C++ engine exposes the versioned `schemas/api-v1.json` contract and its OpenAPI description. Electron starts and stops the engine process; React sends no commands through stdin and reads no configuration file.

## Startup and transport

- Electron starts `TimoxVirtualAsioEngine.exe` with separate stdout/stderr streams.
- It extracts the port from the JSON `api.ready` event. The main process then consumes `GET /api/v1/state`, `GET /api/v1/drivers`, and `ws://127.0.0.1:<port>/api/v1/ws` with the `vasio.api.v1` subprotocol.
- The engine uses cpp-httplib for HTTP/WebSocket; the Electron main process uses Fetch and the `ws` library.
- The preload exposes only state and inventory reads, `configuration.apply`, and event subscriptions. React does not know the API address and does not open a socket.

## Configuration

React prepares a complete configuration containing `physicalDriverId`, the single virtual driver's `virtualDriver` settings, and `routes`. Every change is submitted through the `configuration.apply` WebSocket command. The `engine.status`, `devices.changed`, `routes.changed`, and `engine.error` events reflect engine state. A successful command triggers a fresh HTTP state read.

The `sourceEndpointId` and `destinationEndpointId` values come from the inventory. The interface does not calculate channels, create engine routes directly, or maintain a parallel persistent configuration.

Any configuration change stops the current audio stream while the engine replaces the graph and restarts the selected driver.
