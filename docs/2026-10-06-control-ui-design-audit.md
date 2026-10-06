# Timox VASIO Control: source review and UI action plan

Date: 2026-10-06
Status: source review and action plan; a first routing/theme implementation was added separately, with live visual verification still limited.

## Brief and scope

Create a professional, sober, visually lighter audio control interface with finer strokes, smaller typography, muted colors, and a graphical routing view alongside the existing matrix. The primary job is to understand and change audio connections confidently during setup and operation.

Recommended direction: a light, cool neutral workspace with restrained teal accents, compact native Windows typography, and a readable left-to-right patchbay. Reserve strong color for selected connections, focus, and meaningful operating states. Preserve the existing matrix and its channel filters as an alternative representation of the same route draft.

This document is an independent review, not an acceptance report. The implementation work in this session added an initial graphical routing view and light theme after the review; the remaining action items below still need evaluation against the rendered application. No translation, binary, or package was changed for this review.

## Evidence and limits

Reviewed current `gui/src/App.js`, `App.css`, `index.css`, `index.js`, `api-contract.js`, `App.test.js`, `gui/package.json`, and routing test/source references. File links below are relative to this document.

The in-app browser setup failed before tab inspection with `codex/sandbox-state-meta: missing field sandboxPolicy`. Consequently this is a source review, not a completed interactive product audit. Actual rendering, text clipping, pointer behavior, screen-reader output, OS scaling, and audio behavior remain unverified.

The existing [routing screenshot](ux-audit/evidence/04-routing-matrix.png) was opened for historical context. Its glowing “VASIO Control” title and colors differ from the current source, which has subsequent visual overrides. It is not current screenshot evidence and is not used to establish current defects.

The Product Design audit guidance was consulted; its screenshot requirement cannot be satisfied in this review. The frontend-design and brainstorming guidance informed the proposal. No claims are made about exact QjackCtl behavior; “patchbay” here means grouped ports with visible connections, as requested.

## Current task flow and findings

| Step | Source evidence | Assessment and practical change |
| --- | --- | --- |
| 1. Identify engine state | `App.js` header and status indicator | State text accompanies color, which is useful. The explanatory engine executable subtitle competes with the product title. Use a short product header and place operating state, sample rate, and buffer size in a compact status strip when known. |
| 2. Configure device and application profiles | `App.js` configuration view and independent profile save handler | Device configuration and profile saving have different effects. Keep separate action labels and the existing message identifying applications that need restarting. Make their save boundaries visually obvious. |
| 3. Find routing | Channels view places application cards and client details before routing; `.routing-details` starts closed | The central routing task is less discoverable than supporting information. Put routing first in Channels, label the view “Channels & routing” or an equivalent localized name, and move client diagnostics into a secondary disclosure. |
| 4. Locate two channels | Zone filters, separate search fields, 8/16/32 pagination, vertical destination labels | Filtering and pagination are valuable for large inventories. Long repeated device/application names in every matrix heading create scanning work. Group by owner, retain short channel names in rows, and expose the full endpoint identity in accessible text and details. |
| 5. Add or remove a route | `toggleRoute()` changes local `configuration.routes`; matrix uses `aria-pressed` | The matrix supports button interaction and avoids duplicate endpoint pairs. A selected cell currently describes draft presence, which can be mistaken for live audio. Show “Pending changes” and keep applied state distinct from edited state. |
| 6. Set gain/mute or inspect a route | Configured route list with gain, mute, remove, and “Show in matrix” | Useful precise controls already exist. Retain them as a shared route inspector/list for both graphical and matrix views. Give repeated action controls route-specific accessible names. |
| 7. Apply changes | `applyConfiguration()` sends the whole configuration and reports interruption afterward | Make the pending change count and existing interruption behavior visible next to Apply before activation. Keep Apply visible while routing scrolls. Do not imply that drawing a cable immediately changes live audio. |
| 8. Handle external changes | `routes.changed` and connection restoration call `refreshState()`, which replaces `configuration` | An external update can overwrite local draft edits. Track an applied baseline and dirty draft explicitly; retain edits or announce a conflict before replacing them. This risk is visible in code; it has not been reproduced interactively. |

### Effective styling, rather than obsolete rules

`App.css` includes initial styles followed by multiple later override blocks. The current effective intent already removes the title glow and gradient page. Its later palette uses `#181e2b`, `#202a3a`, and `#65bfd0`, while earlier token families and hardcoded values remain in use. A redesign should consolidate these into one token system rather than append another global override layer.

Relevant sizes in current rules include a 16 px interface base, 24 px brand title, 20 px section titles, and mostly 40 px controls. Matrix cells have a more specific 36 px minimum height. Numerous nested sections, inset cards, and 8 px radii create visual weight. Selected `.view-navigation` styles are not the live top navigation: the rendered element uses `.view-tabs`. Style the component actually present.

Current strengths to preserve include visible focus rules, a reduced-motion override, native labels/inputs/buttons, matrix row and column headings, searchable endpoint inventories, explicit connection text, and route gain/mute controls. Existing tests cover API-driven inventory, profile saving/restart notices, diagnostics/API views, and analysis/meter behavior. They do not establish graphical routing, focus navigation across a large matrix, or live rendering quality.

## Visual system proposal

### Color tokens

Use opaque surfaces. Apply the same tokens to configuration, routing, analysis, and diagnostics. Keep code samples in a deliberately distinct readable surface only when useful.

| Token | Value | Role |
| --- | --- | --- |
| `--surface-page` | `#F3F5F7` | Main workspace |
| `--surface-panel` | `#FFFFFF` | Controls and principal content |
| `--surface-inset` | `#EBEFF2` | Group headers and quiet inset areas |
| `--border-subtle` | `#D5DCE2` | Decorative separators; not the sole control boundary |
| `--border-control` | `#83909D` | Input borders and visible graphical boundaries |
| `--text-primary` | `#25313D` | Labels, values, titles |
| `--text-secondary` | `#53616F` | Supporting text |
| `--text-muted` | `#637180` | Metadata on white/page surfaces only |
| `--accent` | `#376F7B` | Primary action and selected connections |
| `--accent-surface` | `#E2EEF0` | Selected row background, paired with accent edge/icon |
| `--focus` | `#1F637D` | Two-pixel external focus ring with two-pixel gap |
| `--success` | `#3F725A` | Running state, accompanied by text |
| `--warning` | `#8A641E` | Pending/reconfiguration status with label |
| `--danger` | `#A44446` | Errors, clipping, destructive action emphasis |

Calculated sRGB contrast for proposed pairs: primary text/white 13.25:1; secondary text/page 5.81:1; muted text/page 4.57:1; accent/white 5.64:1; control border/white 3.26:1; focus/white 6.69:1. These are checks of specified pairs, not an accessibility certification. Recheck actual rendered combinations, especially muted text on inset surfaces and borders alongside the page background. Decorative separators may be subtle; functional states must remain discernible.

### Typography and geometry

| Role | Proposal |
| --- | --- |
| Font family | `"Segoe UI Variable Text", "Segoe UI", sans-serif`; use installed native fonts without remote loading |
| Main title | 18 px / 24 px, weight 600 |
| Section title | 15 px / 21 px, weight 600 |
| Interface text | 13 px / 19 px, weight 400 |
| Labels and button text | 13 px / 18 px, weight 500 |
| Metadata | 12 px / 17 px, weight 400; never the only place for critical instructions |
| Numeric readings | Same font with tabular numerals; monospace reserved for literal code/IDs |
| Controls | 32 px minimum height; increase height naturally for wrapped text and scaling |
| Port rows | 32 px minimum height; whole row selectable, even if the port dot is only 6 px |
| Matrix cells | Keep 36 px minimum targets initially; reduce surrounding chrome before shrinking cell targets |
| Spacing | 4, 8, 12, 16, 24 px scale; 16 px panel padding and 12–16 px inter-panel gap |
| Borders and radius | One-pixel structural borders; 4 px controls, 6 px major panels |
| Cables | 1.5 px normal, 2 px selected; wider invisible pointer target when cable selection is implemented |

Use sentence case, normal letter spacing, and left-aligned labels. Use one quiet section boundary instead of a border around each nested container. Remove decorative glow, pulsing status animation, and unnecessary shadows. Keep all necessary engine states and metering distinctions.

“Smaller” should mean denser and calmer at default scaling. Retain zoom, wrapping, and keyboard target usability; do not set a fixed device-pixel assumption for the whole app.

## Graphical routing proposal

### Choice of approach

| Approach | Benefit | Cost | Recommendation |
| --- | --- | --- | --- |
| Grouped source list, central cables, grouped destination list | Clear direction, stable port positions, efficient keyboard controls | Large connection counts require filtering | First release |
| Free-position node canvas | Flexible spatial arrangements | Requires pan/zoom, layout persistence, cable hit testing, and stronger keyboard alternative | Defer until user need is demonstrated |
| Matrix only with cosmetic refresh | Lowest interaction risk, efficient dense editing | Does not fulfill requested graphical view | Retain as companion |

The recommended patchbay has a compact toolbar with a “Patchbay / Matrix” view switch, source and destination filters, search, and visible/total connection counts. Below it, source owners appear at left and destination owners at right. A central region draws the visible routes. A shared route list or inspector below exposes connection name, gain, mute, and removal. Keep the Apply area outside the scrolling port area.

Use endpoint inventories returned by the engine. Group virtual ports by application instance and physical ports by device; include PID in secondary identity when two instances share an executable name. Use short channel labels inside each group. Do not infer channels from a profile maximum or invent inactive endpoints.

### Supported direction model

Mirror the existing UI/backend contract rather than treating every listed endpoint as interchangeable:

| Source | Destination |
| --- | --- |
| Virtual output: application sends audio | Physical output: device playback |
| Virtual output: application sends audio | Virtual input: application receives audio |
| Physical input: device capture | Virtual input: application receives audio |

Use headings such as “Sources — audio from” and “Destinations — audio to” in the final localized UI. A hardware input is a source to the routing engine; the interface must make that viewpoint clear.

### First release interaction contract

1. Select a source row, then a compatible destination row, or drag a cable between them as the user requested. Highlight selected endpoints and their full names. Enter/Space must support the same selection flow as a pointer.
2. Display the selected pair’s connection state. If it already exists, expose Disconnect and reveal its shared gain/mute inspector. Selecting a cable or endpoint never disconnects it implicitly.
3. Add, remove, gain, and mute operations update the same local draft used by the matrix. Switching views must preserve edits, filters where meaningful, and the selected route.
4. Show pending changes in text, with a visible Apply action and a discard/revert action where draft tracking is available. State that applying configuration briefly interrupts audio, matching current behavior.
5. On Apply, lock both views consistently during reconfiguration. Only show an applied success state after engine confirmation. Preserve editable draft data and show the error when application fails.
6. Include counts for hidden routes. Searching or collapsing groups must never imply hidden routes are deleted. A “Show connection” action should reveal both endpoints or explain why one is unavailable.
7. An unresolved persisted endpoint remains visible in the route list as unavailable. Do not silently remove its route or draw a cable to an unrelated endpoint. Reconcile changing client identities using the engine’s resolved state.
8. Distinguish muted, pending, unavailable, selected, and applied connections with text in the route list plus stroke/icon differences. Cable color alone cannot communicate state.
9. Offer Escape to cancel endpoint selection. Return focus to a useful control after removal. Use specific names such as “Disconnect App Out 1 from Device Output 1.”
10. Dragging is a shortcut, not the only way to connect. It needs direct pointer verification across scrolling and resizing. Stereo pairing and multichannel batch wiring require explicit preview and backend-supported semantics before introduction.

For port inventories up to hundreds of channels, start with the existing paging/filter approach. Do not render every possible endpoint pair as a graphical connection. Draw actual visible routes and highlight connected partners when a row is selected. Cable positions must remain aligned after list scrolling, resizing, collapsing, and text wrapping. Avoid reordering owners on every meter event.

At narrow widths, keep the source and destination pickers and a connection list usable in a stacked layout; let the cable drawing become secondary. Never make horizontal panning the only way to connect or inspect endpoints.

### Coexistence and shared state

Keep the matrix available through a clearly labeled local view switch. For the first release, preserve Matrix as the initial view for existing users unless a saved preference explicitly selects Patchbay. Persist only the presentation preference locally; the engine remains authoritative for applied routing.

Both views must use the same endpoint inventory, supported-direction predicate, route draft, lock state, and gain/mute/remove operations. Add no second routing API and no independent patchbay route store. Treat the graph as a renderer/controller over the existing model.

Separate draft configuration from the latest applied snapshot before claiming robust pending-state behavior. External route events should update the applied baseline; if a dirty draft conflicts, surface a readable conflict state with deliberate reload/reapply choices. Do not quietly replace edits.

## Prioritized action plan

| Priority | Deliverable | Acceptance evidence |
| --- | --- | --- |
| P0 | Consolidate CSS tokens and adopt the lighter palette/type scale across live selectors | Compare all five views at the same viewport; no residual neon styling, unreadable inputs, or accidental cross-view overrides |
| P0 | Shared route model and graphical view beside matrix | Add/remove in either view appears in the other without duplicate endpoint pairs; switching views makes no API call that applies routing |
| P0 | Keyboard connection flow and named controls | Complete source selection, destination selection, Connect, gain/mute editing, removal, and Apply without a pointer |
| P0 | Honest draft/applied and engine busy states | Pending changes remain visible, failures retain edits, both views lock during Apply, and success reflects confirmed state |
| P1 | Move routing ahead of client details and simplify nested panels | Primary routing controls are easy to find without scrolling through all client channel inventories |
| P1 | Resolve external update versus dirty draft conflicts | A routes event or reconnect cannot silently discard local edits |
| P1 | Graph readability with large inventories and missing clients | Validate sparse/dense routes, 256-channel groups, duplicate application names, long device names, filtered routes, and disconnected endpoints |
| P1 | Narrow window, zoom, and accessibility pass | Labels remain understandable; controls reachable; connection list remains operable when cables are compressed |
| P2 | Richer spatial layout and stereo/batch shortcuts | Add only after first-release operation and real user feedback justify the complexity |

Suggested implementation boundaries: central theme tokens; a routing workspace with Matrix and Patchbay renderers; shared route controls; one endpoint grouping/direction helper. Extract only what is needed for the new view and reliable tests; avoid unrelated application rewrites.

## Verification plan and remaining risks

### Implementation checkpoint

#### Contrast correction after user review

The first light palette was rejected because several labels blended into gray surfaces. A second source audit found a particularly severe inherited rule: the matrix empty-state text could render as `#e0e7ee` on a pale warning background, approximately 1.2:1 contrast. Disabled patchbay ports also inherited 55% opacity, making their names hard to read. The revised theme uses darker text (`#18212b` primary, `#364552` secondary, `#4e5d6c` metadata), explicit text and background colors for the matrix empty state, opaque disabled controls, darker focus outlines, and 13 px labels and ports. The page uses `#f6f7f8` and keeps interactive surfaces white. The application title links to the public GitHub repository, and the React development server is explicitly configured for port 4000.

The specialist agent re-read `frontend-design`, `product-design:index`, and `product-design:audit`, then read and applied `ui-ux-pro-max` for this correction. Its bundled search script was unavailable because Python was not installed and the skill's script pointer did not resolve. Its browser connection also failed with a sandbox policy error. The contrast figures are calculated from CSS source values, not measured from computed browser styles. A rendered, five-view contrast and zoom review remains required before release.

The visible React interface, graphical routing view, Electron menu, document language, and local API client error were translated to English. API keys, endpoint identifiers, and engine protocol values remain stable. Dynamic names and messages supplied by the engine may still reflect their source language. The English interface should be reviewed in a live session with a running engine and populated routing inventory.

After user feedback on route visibility, Connections gained selectable cables with a wider pointer target, a stronger selected stroke, subdued surrounding cables, and emphasis on the selected source and destination ports. The same selection is available through a button in each route row. A selected connection can be removed directly in the graphical view; removal updates the shared draft and still requires Apply. Filters report hidden routes and offer a way to reveal a selected connection. The Matrix view remains available through the existing switch and uses the same routing draft.

Connection labels and colors are presentation preferences only. The UI stores them locally under the exact source and destination endpoint IDs, so a route can keep its appearance if the engine reassigns the route ID. Editing them does not mark the audio configuration as pending and does not add fields to the strict `configuration.apply` payload. The selected connection editor accepts a short label and a color; the cable and route row reflect both. Preferences are local to this installation and are not synchronized to other machines. If a virtual client's endpoint IDs change, the preference for its old endpoint pair will not automatically follow it.

Channel groups in the graphical view are now collapsible. Groups with more than eight visible ports start collapsed, and the toolbar can expand or collapse all groups. Search results expand automatically; a selected connection can reveal its source and destination groups. The status text reports connections hidden by collapsed groups or filters. A fixed connection inspector sits beside the graph at wide window sizes and above it in narrow windows. It offers connection selection plus label, color, gain, mute, and removal controls without scrolling below the graph. Clicking or right-clicking a cable selects it in this inspector. Gain, mute, and removal still change the shared audio draft and require Apply; label and color remain local display preferences.

The inspector also offers bulk appearance editing. Users can find routes, select any number of them, then apply a common label, color, or both. The label and color switches determine which properties change, so a color-only operation preserves existing labels. Bulk edits update the local presentation store in one step and leave the engine configuration and pending audio state untouched.

The first pass now includes a light theme in `gui/src/ProfessionalTheme.css`, a grouped graphical routing view in `gui/src/RoutingPatchbay.js`, and a Matrix/Connections switch in the Channels view. Routing was moved ahead of client details. Both views edit the same configuration draft; the existing Apply action remains the only operation that sends it to the engine. The graphical view supports drag and click/keyboard selection, and the route list retains gain, mute, and removal controls. React tests cover shared draft submission, permitted directions, and a drag followed by a click. The React build succeeds.

These are implementation facts, not a claim of full UX validation. A headless Chrome preview with a mocked device and application was inspected for the light palette, port grouping, cable placement, and routing prominence. The live Electron application, real API inventory, all five views at multiple sizes, and audio operation still require direct checks. The final Apply bar was made opaque and placed in normal flow after the preview revealed overlap with the route list.

The next work items are CSS consolidation, explicit confirmation before disconnecting an existing link, dirty-draft protection against external `routes.changed` events, dense-inventory performance, hidden/unavailable route presentation, and a keyboard and screen-reader pass. These remain open and should be reviewed against real sessions before a release package is made.

Run existing React and API contract tests after behavior changes. Add focused tests for shared draft synchronization, duplicate prevention, unsupported direction rejection, locked/busy state, unavailable endpoints, and stale external updates. Tests should assert user-visible behavior and submitted configuration, rather than exact component internals.

Capture fresh screenshots of Configuration, Channels with each routing view, Analysis, API, and Logs at desktop size and a narrower Electron window. Test 100%, 125%, 150%, and 200% scaling/zoom where supported. Include empty inventory, populated inventory, pending change, Apply failure, and unavailable endpoint states. Confirm that cable drawing stays aligned after scrolling and resizing.

Use keyboard and screen-reader checks in the rendered app. Current native buttons and labels are a useful base, but hundreds of matrix cell buttons can create a long Tab sequence. Consider roving focus with arrow-key navigation for the matrix only as a complete, tested interaction pattern. Give the graph an equivalent connection list and ordinary controls; visual SVG paths alone are insufficient.

Check actual contrast, clipping, focus visibility, reduced motion, and meter update announcements. Do not announce every meter sample to assistive technology. Maintain readable state text and selected connection details.

The source review itself performed no build, installation, packaging, or audio execution. The subsequent implementation test and build results are recorded in the checkpoint above. Before any later build/install, follow repository instructions to confirm the Git root and inspect status. Release packaging additionally requires the documented binary provenance and SHA-256 checks.
