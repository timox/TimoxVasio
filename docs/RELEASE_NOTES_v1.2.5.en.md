# TimoxVasio 1.2.5

This release makes saved routes easier to read and installs Control and the ASIO driver together through one Setup.

## Changes

- Saved connections are grouped by source/destination pair. Expanding a group shows a small matrix of the channels in use, their activity, and controls for each connection.
- Routes waiting for a disconnected application stay grouped under its executable name.
- Setup installs and registers `TimoxVasio.dll` together with the interface and engine. Windows requests administrator rights. The portable build does not change ASIO registration.
- Uninstall removes the driver registration if it still points to this version's DLL.

The 1.2.4 Apply configuration fix is included. Existing routes are preserved. The engine and driver both use version 1.2.5.

## Installation

Download and run [Timox VASIO Control Setup 1.2.5.exe](https://github.com/timox/TimoxVasio/releases/tag/v1.2.5). Then restart audio applications still using the older DLL. See the [Windows downloads guide](RELEASE_WINDOWS_1.2.5.en.md) and [installation guide](INSTALL.en.md).
