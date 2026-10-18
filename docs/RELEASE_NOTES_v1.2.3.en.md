# TimoxVasio 1.2.3

This release fixes audio stopping when Timox VASIO Control is closed.

## Fix

- Control now launches the engine as an independent Windows process. Closing the window leaves the engine and audio running; the explicit “Stop engine” command keeps its existing role.
- A regression test covers engine survival after its launcher exits.
- Channels adds “Quick routing” beside Connections and Matrix: choose a source and destination, then add a mono or stereo connection using a suggested free channel pair. Routes remain in the draft until “Apply configuration”.
- The Overview tab shows used and available physical channels and summarizes application flows. Saved connections are grouped by source/destination pair, with compact channel ranges and applied or pending status.

## Installation

Download all four files from [release 1.2.3](https://github.com/timox/TimoxVasio/releases/tag/v1.2.3). Install the 1.2.3 driver and Control, then restart audio applications still using the older DLL. See the [Windows downloads guide](RELEASE_WINDOWS_1.2.3.en.md) and [installation guide](INSTALL.en.md).

After installation, close Control during audio playback to check that sound continues. The engine remains available through its local API; use “Stop engine” to stop audio explicitly after ASIO clients have closed.
