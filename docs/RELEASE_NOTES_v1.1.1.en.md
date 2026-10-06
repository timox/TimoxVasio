# TimoxVasio 1.1.1

This release updates the Timox VASIO Control interface and rebuilds the Windows engine and ASIO driver with version 1.1.1 metadata.

## Downloads

| File | Purpose |
| --- | --- |
| `TimoxVasio.Driver.1.1.1.zip` | ASIO driver DLL, administrator installer, registration script, and license. |
| `Timox.VASIO.Control.Setup.1.1.1.exe` | Installer for Control and its bundled audio engine. |
| `Timox.VASIO.Control.1.1.1.exe` | Portable Control with the bundled audio engine. |
| `MANIFEST.json` | Version, source commit, and SHA-256 values of the validated files. |

The driver ZIP is separate from the Control installers. Extract it and run `Installer TimoxVasio.bat` as administrator. Restart any audio application that has the older driver DLL open. See the [installation guide](https://github.com/timox/TimoxVasio/blob/main/INSTALL.en.md).

## Windows compatibility

This release targets Windows 10 and 11 on x64 systems and 64-bit ASIO applications. Installing and registering the driver requires administrator privileges. To route audio through a physical interface, the engine also needs a compatible physical ASIO driver.

Windows 32-bit, 32-bit ASIO applications, Windows 7/8, and Windows on ARM are not validated for this release. Compatibility with every Windows computer or audio interface is not guaranteed.

## Interface changes

- A graphical patchbay now sits alongside the routing matrix. Users can create, select, inspect, highlight, and remove connections in the graphical view while retaining the matrix for dense routing.
- Collapsible channel groups and a connection inspector reduce scrolling and keep route properties available while reviewing the patchbay.
- One or more selected connections can receive a custom display label and color. These visual choices affect only the cabling view; they do not change the engine's routes or audio processing.
- Typography, spacing, borders, contrast, and colors have been revised for a clearer, more restrained interface. The interface text is in English, and the title links to the GitHub repository.
- The React development server uses port 4000.

## Validation scope

The release process checks the React tests and production build, native builds, binary version metadata, and SHA-256 identity between the built engine and packaged engine and between the built driver and ZIP contents. The graphical routing changes are UI changes; this release does not claim a new physical audio hardware validation.

See the [Windows downloads guide](https://github.com/timox/TimoxVasio/blob/main/docs/RELEASE_WINDOWS_1.1.1.en.md) and [UI design audit](https://github.com/timox/TimoxVasio/blob/main/docs/2026-10-06-control-ui-design-audit.md).
