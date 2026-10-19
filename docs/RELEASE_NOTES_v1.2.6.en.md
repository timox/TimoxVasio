# TimoxVasio 1.2.6

This release adds audio performance history and simplifies routing.

- **Performance** illustrates samples, sample rate, ASIO buffers, and block time. It shows routed application CPU and each application's callback time relative to the block deadline. Stage timing remains opt-in.
- **Analysis** plots physical callback load, application CPU and callback load, and changes in underrun/overrun counters over time. State is sampled every five seconds outside the audio thread. CSV export supports later analysis.
- Configured, open, and currently routed channels are distinguished. Transport counters are shared across channels of one process and direction; they cannot isolate an individual channel or VST.
- **Routing** opens directly on a patchbay inspired by [PipeWireController](https://github.com/knightinfected/PipeWireController), now built with React Flow. A process holds its inputs and outputs in one block. The +/− buttons or pinch gesture zoom the graph, while the mouse wheel scrolls the page. **Recenter graph** immediately brings blocks back into view. The graph pans by dragging its background; blocks move freely from their headers and can be renamed locally. Right-clicking a cable opens a menu leading to its properties dialog; dragging its end to another port stages a connection change while preserving gain and mute. The matrix stays scoped to a selected connection. Port types and cable states have distinct visual signs.
- Nodes remain initialized while being dragged, and the graph recenters when filters change. A search that hides every port shows an explanation and a way back to the connections.
- The left menu is reserved for navigation and global actions. Contextual details open exclusively in dialogs from context menus; no permanent inspector borders the graph. A connection's matrix opens in a centered dialog. Connections saved under an old executable name have a different block from a channel profile; the Configure process context-menu action can move them to an open process in the draft for review before **Apply configuration**.
- Right-click the background to add a connection or inspect process channels. A process menu lets you add a link or choose the exact link to edit or remove. A cable menu acts on that cable alone. Ports no longer show “(saved)”; migrated connections are recorded in **Logs** after application.
- **ASIO setup** places the physical card before the list of application capacity profiles. In **Routing**, the Configure process context-menu action on a present process assigns its profile (suggesting two inputs and two outputs). The same panel removes its configuration and connections after confirmation without closing the Windows process.
- When a different physical card is selected, **Routing** offers to transfer saved connections to the same channel numbers. The transfer retains sources, gains, and mute states. If the new card lacks a channel, a local backup lets you restore the previous card and its connections.
- Control keeps stable application IDs in the draft. Saved connections remain visible while their ports are closed. If another application such as Mixette adds routes through the API, **Apply configuration** merges independent edits and reports a conflict on the same connection.
- **Discard pending edits** abandons only the draft. **Reset physical driver** reopens the last saved configuration without deleting connections; audio pauses briefly.

A per-block frequency spectrum is not yet measured. Engine and driver both use version 1.2.6. Saved routes are retained.

- **Expand all** expands connected ports only. Unused ports remain hidden; each process shows used / available inputs and outputs even when collapsed. Available ports can be selected in the Add a connection dialog.

## Installation

Download and run [Setup 1.2.6](https://github.com/timox/TimoxVasio/releases/tag/v1.2.6). It installs Control, the engine, and the ASIO DLL without deleting the saved configuration. Restart audio applications that were already open. To change physical cards, select the new card in **ASIO setup**, then use **Switch driver and transfer connections**: Control opens the card and transfers the connections in one action. See the [Windows downloads guide](RELEASE_WINDOWS_1.2.6.en.md).
