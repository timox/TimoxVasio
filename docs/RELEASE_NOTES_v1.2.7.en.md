# TimoxVasio 1.2.7

Stable release of the audio optimizations validated in 1.2.7-test, with automatic graph arrangement.

- **Auto-arrange**, beside the zoom controls, arranges visible blocks in columns using their dimensions, fits the view and saves positions. Custom names and audio connections are preserved.
- Windows MMCSS Pro Audio priority for the audio thread, with a fallback when unavailable.
- Output-ready notification for compatible physical drivers.
- Block audio conversion with bit-exact verification across supported sample formats.

The [software audio path benchmark](PERFORMANCE_1.2.7-test.md) measured 41–64% less time for transport, routing and conversion. Local live-session observations at 48 kHz / 2048 frames also showed plausible improvements: output copy 0.501 → 0.122 ms/block, client reads 0.606 → 0.092 ms, routing 0.152 → 0.068 ms. No late starts or processing overruns were observed in the recorded windows. These are audio-stage timings, not total PC CPU reductions; background load was not identical.

Use **Timox VASIO Control Setup 1.2.7.exe** to install the UI, engine and driver. The DLL installs at `C:\Program Files\Steinberg\VirtualASIO\1.2.7\TimoxVasio.dll`. Restart audio applications afterward. The portable includes the engine but does not install the driver. A driver ZIP and SHA-256 manifest are also provided.

GUI and native audio tests cover the change. Packaged binaries are checked against their build outputs. Installed-driver lifecycle, reload and cadence checks passed during prerelease validation. Previously documented probe limitations remain in the [prerelease report](plans/2026-10-20-optimisation-chemin-audio-rapport.md); hardware latency has not been quantified.

The API and v9 audio transport remain compatible. Ongoing WASAPI development is excluded.
