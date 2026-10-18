# Roadmap

[Français](ROADMAP.md) | **English**

Work being considered after version 1.2.4. None of it is scheduled or committed; the order does not reflect priority.

- **Validate compatibility with ASIO4ALL.** ASIO4ALL can be selected as the physical driver, but no TimoxVasio release has been validated with it. Test opening, reconfiguration and routing with a WDM device behind ASIO4ALL, and document the limits.
- **Correct MIDI clock drift from the audio interface clock.** Use the physical driver's sample position, which already serves as the master clock, as the time reference for a MIDI clock so that it does not drift against the audio.
- **Make the SuperCollider example and audio tests reliable.** Diagnose the WAV rendering failures observed so far: one channel in the six-channel chain can be silent, and the compressor test can measure a higher output level than expected. Check channel routing, processing gain, and test assertions before treating the example as validated.
- **Produce time-series graphs without hurting performance.** Show in Control how levels, correlation and performance measurements evolve over time, without adding work to the audio callback or raising the API publication rate.
- **Simplify the Windows installer.** Merge the installation of Control, the engine and the ASIO driver registration into a single step; today they go through two deliverables and a script run as administrator.
- **Port TimoxVasio to Linux on top of PipeWire.** Study a version where PipeWire replaces the virtual ASIO driver and the physical interface, keeping the routing engine, the local API and Control.
- **Make test feedback easier with a shareable dump.** Produce in one step, from Control or the API, a diagnostic archive (DLL and engine versions and hashes, configuration, profiles, routes, state, latest performance measurements, logs) that the user can attach to a bug report, with no audio data and no unnecessary personal information.
