# TimoxVasio 1.1.0 validation criteria

[Français](RELEASE_VALIDATION_1.1.0.md) | **English**

A build alone does not establish that the application works. Validation must connect the commit's sources to the installed binaries, then verify the real workflow.

## Provenance of the three components

- ASIO driver: `build_driver_110/Release/TimoxVasio.dll`.
- Audio engine and API: `build_codex_110/Release/TimoxVirtualAsioEngine.exe`.
- Interface: manifest and sources under `gui/`.

The engine in the Setup/portable package must have the same SHA-256 as the built engine. The DLL in the driver archive must have the same SHA-256 as the built driver. `npm run dist` must stop if an input or resource is missing or differs, then produce a validation manifest with the commit, versions, paths, and hashes.

## Software validation

For each commit: build the engine, native tests, and DLL; pass driver compatibility, audio runtime, API, and diagnostics tests; pass contract, Electron, and React tests; build the interface, Setup, and portable package; check `git diff --check`. Results from an earlier build do not apply to another commit.

## Windows and hardware validation

Install or run the artifacts whose hashes were checked, and verify:

1. The `Timox VASIO Control` interface displays the engine's actual state.
2. The local API and Swagger respond from the interface.
3. `engine.start` and `engine.stop` control the stream without stopping the API host.
4. Meters appear only on active routed channels and follow the signal.
5. L/R correlation is near `+1` for identical channels, near `−1` after polarity inversion, and reports `no_signal` in silence.
6. The ZIP installs the DLL separately; the tested host and physical ASIO driver discover and use it.
7. Logs record changes and errors without writes from the audio callback.

Record the Windows version, ASIO driver versions, host, sample rate, buffer size, and audio results. The conclusion applies to that verified configuration, not every hardware and driver combination.
