# Timox VASIO Control user guide

[Français](GUI_GUIDE.md) | **English**

## Audio configuration

The interface shows physical ASIO drivers discovered by the engine and the single virtual driver, `TimoxVasio`. Select the physical driver, then choose a sample rate and buffer size from its advertised capabilities. The engine and TimoxVasio clients use the same sample rate and buffer size.

The API inventory shows connected applications with their PID, name, and channels actually allocated by each one. Available virtual endpoints correspond to these active channels. Physical ports are those returned by the driver after the sample rate is applied.

## Per-application channel profiles

The “Per-application channel profiles” panel reads and replaces the complete list through the documented `GET` and `PUT /api/v1/application-profiles` HTTP routes. A profile associates an executable name with separate input and output counts, each from 1 to 256. An empty list restores the 256/256 default; the initial `mixxx.exe` profile is 255/255.

The API returns clients whose advertised capability will change. Close and restart each listed application: an already initialized ASIO instance retains its counts until its next startup. Profiles do not reduce shared transport capacity or routes between applications.

If the panel reports that the route is unavailable (HTTP 404), rebuild and restart the connected engine with a version that includes the profiles API.

## Selection in an audio application

In an ASIO host, choose the ASIO sound API and then the `TimoxVasio` device. After installing the driver, close and restart the host to refresh its list. Mixxx 2.6 beta x64 currently removes this driver from its list because it advertises 256 inputs and outputs; see the [Mixxx compatibility analysis](../docs/mixxx-256-channel-compatibility.en.md). `TimoxVasio` remains visible for discovery even if the engine is not ready. To open an audio stream, start Timox VASIO Control and apply a physical ASIO driver.

## Routing

Routing is configured in a single matrix whose axes are selected with the area menus. Virtual outputs can connect to physical outputs or virtual inputs. Physical inputs can connect to virtual inputs. Channels appear in pages of 8, 16, or 32, with separate search for each axis. Wide, contrasting scrollbars remain easy to distinguish from cells. Channels marked `●` already participate in a route; color helps locate them. The list below the matrix keeps all routes visible, and its “Show in matrix” button navigates to the corresponding pair. Gain and mute remain adjustable in this list. Apply sends the complete configuration through `configuration.apply`.

The axes show only endpoints published by the API. To show physical ports, select and apply a physical ASIO driver. To show TimoxVasio channels, open the driver in an ASIO application and activate its channels. When an axis is empty, the interface indicates the required action.

A configuration change interrupts the stream while the graph is rebuilt. Engine status and structured errors appear in the interface.

## API, logs, and engine lifecycle

The “API and Swagger” view shows the OpenAPI specification and WebSocket commands/events. Swagger UI, its styles, and the OpenAPI schemas are bundled with the application; display does not depend on a CDN. The WebSocket description comes from the `x-websocket` field in `openapi-v1.json`.

The “Logs” view lets users select the standard (`info`) or detailed (`debug`) level, refresh entries, and start or stop the engine. Detailed mode records more control events; no audio samples are written. Entries are read through `GET /api/v1/diagnostics`; the level is saved through `PUT /api/v1/diagnostics`. The log is at `%LOCALAPPDATA%\TimoxVasio\logs\engine.log`, with up to four archives. The selected level is stored in `%LOCALAPPDATA%\TimoxVasio\diagnostics.json`.

Shutdown uses the `engine.stop` WebSocket command. It is refused while an ASIO client is connected. When Electron closes, the engine remains active and can continue serving audio applications; use “Stop engine” when no client is connected.

## Launch and development

See [README.en.md](README.en.md) for installation, Electron startup, and development commands. The contract is in `../schemas/api-v1.json` and `../openapi-v1.json`.
