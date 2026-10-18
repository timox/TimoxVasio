# TimoxVasio 1.2.2

This release improves the routing interface and adds examples for the local API.

## Changes

- Channels shows routes and their applied state more clearly, with a compact view alongside the full matrix.
- The embedded Swagger console resolves OpenAPI schema references offline.
- A web example, a Renoise Tool, and a SuperCollider chain demonstrate API control and routing between applications. The Tool includes an OSC transport bridge and a MIDI clock example.
- The documentation has been reorganized; earlier release notes remain available.
- The driver archive script once again includes the correct installation files.

## Installation

Download all four files from [release 1.2.2](https://github.com/timox/TimoxVasio/releases/tag/v1.2.2). Install the driver and Control at the same version. Restart audio applications that still have an older DLL loaded. See the [Windows downloads guide](RELEASE_WINDOWS_1.2.2.en.md) and [installation guide](INSTALL.en.md).

The audio routing and MIDI clock examples require configuration for the applications and devices on your system; the examples do not automatically synchronize every DAW.
