# Timox VASIO Control

[Français](README.md) | **English**

Electron and React interface for configuring the VASIO engine. It consumes the contract declared in `../schemas/api-v1.json`: HTTP for state and inventory, and WebSocket `vasio.api.v1` for `configuration.apply` and engine events.

## Development startup

From `gui/`:

```powershell
npm install
npm start
```

Electron attaches to an engine already running on the local port or starts `TimoxVirtualAsioEngine.exe`, then consumes its HTTP routes and WebSocket. The React development server uses port 4000. Navigation includes Configuration, Channels, Analysis, API, and Logs. Channels offers a graphical patchbay and the complementary routing matrix. Select a connection to inspect gain and mute, or remove it; select multiple connections to assign a shared display label and color. These visual settings do not change audio routing. Swagger UI and the schemas are generated from `../openapi-v1.json` and `../schemas/api-v1.json`, then bundled with local resources by `scripts/embed-openapi.js`.

During development and packaging, Electron uses `../build_codex_110/Release/TimoxVirtualAsioEngine.exe` and its `config/` directory. Before packaging, `npm run dist` checks the presence and SHA-256 hashes of the engine, driver, and driver ZIP. After packaging, it compares the embedded engine with the source build and writes a manifest under `dist/release-validation-1.1.1/`. The ASIO DLL is distributed separately; it is not included in the Electron application.

## Usage

1. Set per-application channel profiles. Existing profiles come from `GET /api/v1/application-profiles`; “Save profiles” replaces the list through `PUT /api/v1/application-profiles`.
2. Close and restart applications identified by the API so they advertise the new counts. The initial `mixxx.exe` profile is 255/255; applications without a profile retain 256/256.
3. Choose and apply a physical ASIO driver. The engine then opens the selected driver and publishes its endpoints in the inventory.
4. Select output and input endpoints to build routes. Available endpoints come exclusively from the API.
5. Set gain and mute, then apply the complete configuration.

If the profile panel reports HTTP 404, the connected engine predates this API. Rebuild and restart `TimoxVirtualAsioEngine.exe`, then reconnect the interface.

Each apply replaces the engine configuration and interrupts audio while the change takes place. Displayed status and errors come from API events.

## Checks

```powershell
npm run contract-test
npm run react-build
```

These commands check contract shaping without a third-party dependency and build the React interface with the bundled API offline.

## Diagnostics and shutdown

The Logs view reads the engine through `GET /api/v1/diagnostics` and changes the level through `PUT /api/v1/diagnostics`. It can also stop and restart the process. The documented shutdown is guarded: the engine responds with `ENGINE_CLIENTS_CONNECTED` while an ASIO application is attached. Closing the Electron window does not terminate an engine still in use. Persistent logs are stored at `%LOCALAPPDATA%\TimoxVasio\logs\engine.log`; up to four 5 MiB archives are retained.
