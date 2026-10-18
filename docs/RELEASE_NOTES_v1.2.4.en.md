# TimoxVasio 1.2.4

This release fixes route application in Timox VASIO Control.

## Fixes

- A `routes.changed` event received during an edit no longer discards draft routes before “Apply configuration” is clicked.
- Saved connection groups show applied status separately from the number of routes actually active. An applied route waits until its applications open the relevant ASIO channels.

The engine and driver are both distributed at version 1.2.4. Existing routes are retained.

## Installation

Download all four files from [release 1.2.4](https://github.com/timox/TimoxVasio/releases/tag/v1.2.4). Install the 1.2.4 driver and Control, then restart audio applications still using the older DLL. See the [Windows downloads guide](RELEASE_WINDOWS_1.2.4.en.md) and [installation guide](INSTALL.en.md).
