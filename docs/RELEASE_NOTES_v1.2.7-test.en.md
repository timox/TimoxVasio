# TimoxVasio 1.2.7-test

Audio optimization prerelease. Version 1.2.6 remains the stable release.

- Give the audio callback worker MMCSS Pro Audio / CRITICAL priority, with a fallback.
- Notify compatible physical drivers when output processing is complete (`outputReady`).
- Select PCM conversion once per channel and process whole blocks, preserving sample bits.

Release x64 builds, conversion and routing tests, and seven DriverAudioProbe scenarios pass.
Block conversion matches the scalar implementation across 18 formats, exhaustive 16-bit
values and million-sample random cases. The latest local conversion-only benchmark was
roughly 3.5–4.2 times faster; this is not a whole-engine performance measurement.

A [broader audio-path benchmark (French)](PERFORMANCE_1.2.7-test.md) measures 41–64% less
elapsed time for transport, routing and conversion, excluding application DSP and the
hardware driver. This is not a reduction in total system CPU usage.

DriverProbe has a pre-existing failure on fixed-size buffers (zero granularity).
PhysicalAsioProbe only checks registry inventory. Hardware loopback latency, cadence under
load and real-host compatibility still require measurement. M4 is excluded; UI, API and
shared-memory v9 remain unchanged.

The Setup installs Control, the engine and the driver. Its versioned driver location is
`C:\Program Files\Steinberg\VirtualASIO\1.2.7-test\TimoxVasio.dll`.
Restart audio applications to load the new DLL. Portable includes the engine but does not
install the driver; ZIP supports manual installation. To return to stable, reinstall the
1.2.6 Setup and restart audio applications.

See the [verification report (French)](plans/2026-10-20-optimisation-chemin-audio-rapport.md).
The accompanying `MANIFEST.json` records the source commit and SHA-256 hashes.
