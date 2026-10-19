# Timox VASIO Control

[Français](README.md) | **English**

Electron and React interface for configuring the VASIO engine. It consumes the contract declared in `../schemas/api-v1.json`: HTTP for state and inventory, and WebSocket `vasio.api.v1` for `configuration.apply` and engine events.

## Development startup

From `gui/`:

```powershell
npm install
npm start
```

Electron attaches to an engine already running on the local port or starts `TimoxVirtualAsioEngine.exe`, then consumes its HTTP API and WebSocket. The React development server uses port 4000. **ASIO setup** presents the physical card before the list of application capacity profiles. **Routing** shows saved connections in a patchbay where a process owns its inputs and outputs. The left menu contains navigation and global actions only. Contextual details open exclusively in dialogs from context menus, with no permanent inspector beside the graph. The graph supports zoom and panning; a scoped matrix opens in a centered dialog from a selected connection. Right-click the background to add mono/stereo connections or inspect process channels, a process to add, edit, or remove one of its links, or a cable to act on that cable alone. The Configure process context-menu action assigns a profile or removes its configuration. On a block of old saved connections, it can also transfer links to an open process in the draft. Applied migrations appear in **Logs** without a suffix on ports. Custom labels, colors, and positions remain local to Control. Performance shows the physical callback budget and, on request, per-stage timing of the audio path. Swagger UI and the schemas are generated from `../api/openapi-v1.json` and `../api/schemas/api-v1.json`, then bundled with local resources by `scripts/embed-openapi.js`.

During development and packaging, Electron uses `../build_codex_110/Release/TimoxVirtualAsioEngine.exe` and its `config/` directory. Before packaging, `npm run dist` checks the presence and SHA-256 hashes of the engine, driver, and driver ZIP. After packaging, it compares the embedded engine with the source build and writes a manifest under `dist/release-validation-1.2.6/`. Setup bundles and registers the ASIO DLL with administrator elevation. The portable executable does not install it.

## Usage

1. In Routing, right-click a present process and choose Configure process to assign its channel profile. Existing profiles come from `GET /api/v1/application-profiles`; assignment uses `PUT /api/v1/application-profiles`.
2. Close and restart applications identified by the API so they advertise the new counts. The initial `mixxx.exe` profile is 255/255; applications without a profile retain 256/256.
3. Choose and apply a physical ASIO driver. The engine then opens the selected driver and publishes its endpoints in the inventory.
4. Select output and input endpoints to build routes. Available endpoints come exclusively from the API.
5. Set gain and mute, then apply the complete configuration.

If the profile list reports HTTP 404, the connected engine predates this API. Rebuild and restart `TimoxVirtualAsioEngine.exe`, then reconnect the interface.

Each apply replaces the engine configuration and interrupts audio while the change takes place. Displayed status and errors come from API events.

## Checks

```powershell
npm run contract-test
npm run react-build
```

These commands check contract shaping without a third-party dependency and build the React interface with the bundled API offline.

## Diagnostics and shutdown

The Logs view reads the engine through `GET /api/v1/diagnostics` and changes the level through `PUT /api/v1/diagnostics`. It can also stop and restart the process. The documented shutdown is guarded: the engine responds with `ENGINE_CLIENTS_CONNECTED` while an ASIO application is attached. Closing the Electron window does not terminate an engine still in use. Persistent logs are stored at `%LOCALAPPDATA%\TimoxVasio\logs\engine.log`; up to four 5 MiB archives are retained.

The global Expand all button expands configured connections only. Unused ports are never drawn. Each block shows In used/capacity and Out used/capacity independently of filtering. Multiple cables sharing one port count as one used port. Available ports remain accessible through the context menu’s Add a connection dialog.
