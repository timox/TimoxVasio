# TimoxVasio implementation status

[Français](IMPLEMENTATION_LEDGER.md) | **English**

> Updated October 5, 2026. This ledger supersedes tracking for the four-driver prototype. Observations dated October 4 or earlier are historical; final status is recorded below.

## Current contract

The target product uses one ASIO DLL, `TimoxVasio.dll`, advertised as `TimoxVasio`, with a maximum of 256 inputs and 256 outputs. `TimoxVirtualAsioEngine.exe` is the separate engine process. The selected physical ASIO device provides the effective sample rate and buffer size. The API is the inventory and configuration contract consumed by the interface.

Design and acceptance criteria are in the [256-channel specification](../specs/2026-10-03-vasio-single-driver-256-channel-design.en.md). Tasks are tracked in the [master plan](2026-10-03-vasio-256-physical-master-plan.en.md).

## Final validation — October 5, 2026

- Build/release: `v1.0.0` published on [timox/TimoxVasio](https://github.com/timox/TimoxVasio/releases/tag/v1.0.0). The Electron Setup, ASIO DLL, and engine were checked against release SHA-256 hashes.
- Local reinstall: Timox VASIO Control 1.0.0 installed by Setup; `TimoxVasio.dll` reinstalled and registered under `HKLM\SOFTWARE\ASIO\TimoxVasio`; engine launched from `resources/backend/TimoxVirtualAsioEngine.exe` inside the Electron installation.
- Audio: the user confirmed an audible end-to-end test with Renoise and SSL ASIO Driver 1. The API reported `running`, 48 kHz/1024 frames, one Renoise client with 64 inputs/outputs, and eight routes. `audio.meter` delivered 60 events in 3.5 seconds, with peaks from `-101.65` to `-26.43 dBFS`.
- Logs: `/api/v1/diagnostics` and `%LOCALAPPDATA%/TimoxVasio/logs/engine.log` contain startup, physical configuration, and application of the eight routes.
- Swagger: verified by the user in the installed Electron application.

## Completed items and available verification

- Versioned shared transport and the single driver support 256 channels per direction and publish allocated channels per client.
- Rate/size configuration passed to clients now uses one global value pair, consistent with a single virtual driver. The x64 builds of `TimoxVirtualAsioEngine` and `VasioClientManagerProbe` succeeded after initializing `VsDevCmd`.
- The engine discovers TimoxVasio clients, drives the physical ASIO host, and connects routes through the audio graph.
- Configuration uses the documented HTTP/WebSocket API; Electron consumes this contract.
- Active guides, executable names, and packaging resources use Timox names.
- Reported software checks include transport, graph, runtime, API, and GUI probes, x64 build, and registered COM enumeration of `TimoxVasio` at 256/256.
- The installer migrates known VASIO registry entries to `TimoxVasio`. Confirmed October 3: HKLM contained only `TimoxVasio` among product names, old CLSIDs were absent, and only `TimoxVasio.dll` remained in the installation directory.
- Observed October 3: `DriverProbe --registered TimoxVasio` advertised 256 inputs/outputs. `VasioClientManagerProbe` confirmed interprocess attachment and propagation of 96 kHz/512 frames. `VasioExternalHostProbe` discovered the driver through `AsioDriverList` and validated `init/createBuffers/start`, reset/restart, and reopen at 44.1 kHz/256 frames with the then-current engine. `--list-asio` enumerated physical drivers, including SSL entries, without opening a device. Those probes did not validate real hardware audio; 44.1 kHz/256 frames were defaults before selecting a physical master.
- October 3 resumption: the x64 `TimoxVirtualAsioEngine` build succeeded after a physical buffer initialization fix. Through the documented API, `SSL ASIO Driver 1` configuration without routes was confirmed as `stopped` at 48 kHz/1024 frames; the API published real capabilities of 16 inputs and 8 outputs. The control page responded on port 4000. No TimoxVasio client was connected, so this did not prove hardware signal transfer.

## Additional coverage

- With a real ASIO host, check discovery, sparse allocation, and rejection of incompatible settings.
- Build a circuit in the interface with a real physical device, then measure the specification's three audio flows, including a hardware channel above 6.
- Check reconfiguration, disconnect/reconnect, and error scenarios on target hardware.

The end-to-end test on this machine was completed. A build, COM probe, or enumeration does not replace the additional checks on other hosts and transitions above.

## October 4, 2026 resumption

- The x64 registry and the PortAudio probe built in this repository found `TimoxVasio` with 256 inputs and 256 outputs. The probe accepted 48 kHz when the physical clock was set to 48 kHz.
- The Mixxx screenshot was consistent with internal filtering, not missing ASIO registration. Its log identified build `2.6-beta-402-ge1c1e5b72b`. At that commit, `SoundDevicePortAudio` converts channel counts to `ChannelCount`, whose `value_t` is `uint8_t`; 256 becomes invalid, and `SoundManager::getDeviceList()` discards the device when both directions are invalid. See the [Mixxx compatibility note](../../mixxx-256-channel-compatibility.en.md).
- A minimal patch widening `ChannelCount::value_t` is in `patches/mixxx/0001-audio-channel-count-support-256.patch`. Applied to a copy of Mixxx 2.7 source under `vendor/mixxx-2.7-256`, the x64 build produced `build_mixxx_2.7_256/mixxx.exe`. On October 4, the user launched that copy and confirmed discovery of TimoxVasio. This did not yet prove streaming or allocation of 256 channels. Installed Mixxx 2.6 beta was unchanged and unpatched.
- Engine API was reset to `SSL ASIO Driver 1`, 48 kHz/512 frames, `stopped`, without routes or virtual client. That confirmed configuration state, not audio transfer. A direct probe of the PortAudio DLL shipped with Mixxx remained stuck in `Pa_Initialize()` in isolation and was not used as evidence for the ChannelCount diagnosis.

## Resumption after interruption — physical names and API state

- `PhysicalAsioHost` queries each selected-driver channel through `IASIO::getChannelInfo`. The API publishes the driver-supplied name as `endpoint.name`, using a fallback only when the field is empty. The path is common to all physical drivers, with no SSL-specific branch at this point.
- An actual query of `SSL ASIO Driver 1` at 48 kHz/1024 frames, without routes or stream start, returned 16 named inputs (`Analogue 1–4`, `Talkback`, `Loopback L/R`, `ADAT 1–8`) and 8 outputs (`Mon L/R`, then `Out 3–8`). Configuration was released after reading.
- The documented API then still returned `state: stopped`, no selected driver, and no error. Virtual inventory exposed Renoise PID 19488 with 64 active inputs and outputs. This did not prove signal transfer to the SSL.
- The previous Electron log recorded the child engine exiting with `signal=SIGTERM`; Electron code sent that signal in `before-quit`. The log explains why the API vanished when Electron exited, but does not attribute that exit to Apply.
- GUI success text now clarified that `stopped` is expected when applied configuration has no routes.
- `npm run react-build` succeeded. Electron packaging failed when `electron-builder` attempted to create its cache in `AppData\Local`; no updated Electron package was produced then.
- A test starting an explicitly muted route from Renoise to `SSL ASIO Driver 1 · Mon L` was initially confirmed by the API as `running` at 48 kHz/1024 frames. Soon after, a Windows window reported an invalid memory write in `TimoxVirtualAsioEngine.exe`, and process/API disappeared. Windows logs had no matching report; the exact cause was then unknown.
- An opt-in CMake diagnostic option, `VASIO_ENABLE_CRASH_DUMP`, wrote a minidump near the executable on unhandled exceptions. A diagnostic x64 build started as PID 23388; Renoise republished 64 inputs/outputs. A muted route from Renoise Out 1 to SSL `Mon L` was accepted, then the engine stopped on an access violation (`0xc0000005`, address `0x567250`) and produced a minidump.
- The crash was reproduced with the same route after rebuilding as `RelWithDebInfo`; API accepted configuration before another stop (`0xc0000005`, address `0x560FA0`). The dump and `TimoxVirtualAsioEngine.pdb` confirmed an indirect jump through a stale pointer. Bundled Steinberg source under `asiosdk/driver/asiosample` retains the `ASIOCallbacks*` passed to `createBuffers`; its host example uses a global structure. Our host had made that structure local in `PhysicalAsioHost::start`, so its stack was reused after return. It became a `Session` member, passed as `&session.callbacks`.
- After rebuild and restart, the muted route remained `running` without error. The same route enabled (Renoise Out 1 to SSL `Mon L`, 48 kHz/1024 frames) remained `running` for eight seconds, with one route and no new dump. API accepted and confirmed the configuration. Audible audio was not measured independently then.

## Archive

The historical `CMakeLists_PortAudio.txt` and `src/main_portaudio.cpp` prototype still creates six-channel VASIO1–VASIO4. Orphan `src/vasio_driver_correct.cpp` also contains an old six-channel driver. None is referenced by the main build. The PortAudio prototype target is named `LegacyVASIO_PortAudioPrototype` to distinguish it from `TimoxVirtualAsioEngine`. `config/routing.ini` is retained only for that prototype and is not active engine configuration.

## Before-user-restart resumption — October 4, 2026

### Goal

Provide one Windows x64 virtual ASIO driver, `TimoxVasio`, with up to 256 inputs/outputs, and a GUI linking application active channels to physical ASIO ports. The physical master driver sets engine and virtual-driver rate/buffer size. The interface applies configuration only through the documented API.

### State to resume at that time

- Active engine API had been observed with SSL ASIO Driver 1, 48 kHz, 1024 frames, and four stereo Renoise/Ableton routes to Monitor L/R. Renoise and Ableton appeared as clients; Mixxx remained at 0 inputs/outputs.
- `build_engine_names_check` contained resolved SSL 12 labels, but active `build_engine_vs2026_ninja` had not been replaced. Generic `Out 3`–`Out 8` names were therefore expected until controlled installation/restart.
- At the date of that old note, end-to-end audio was unconfirmed; the `-120 dBFS` reading had no playback context. The later test and measurements are in “Final validation — October 5, 2026” above.
- Installed Mixxx remained 2.6 beta x64; its 8-bit `ChannelCount` limit excluded TimoxVasio at 256/256. The patch was tried only on the 2.7 source copy.
- Matrix interface and documentation had been updated. `gui/INTEGRATION.md` described one virtual driver, and the GUI guide and Mixxx note explained the observed limit. `ASIO_AUDIT.md` retained session facts and distinguished software validation from hardware signal.
- The user said they would restart. The historical instruction was not to restart, kill, or control audio hosts or validate runtime until the user said restarting had finished.

### Historical resumption list — end-to-end test since completed

These actions described the state before the user's test report. The audio workflow was subsequently tested and confirmed on October 5, 2026; they are no longer an audio validation blocker.

1. Resume an interrupted Mixxx build, if needed; it does not touch the installation. Even if successful, treat installed Mixxx as unpatched until a matching version is explicitly installed.
2. After user return and controlled closure/restart, rebuild/install the current engine, then check physical port names, status, and selected-driver capacity through the API.
3. During signal playback, check meters, counters, and physical sound without assuming a confirmed route means audible sound.
4. Check discovery and opening in a real ASIO host; resolve Mixxx incompatibility separately. These are retained as additional coverage, not conditions of the completed end-to-end test.

## Deliverable audit — October 4, 2026

### Interface, engine access, and diagnostics

- Electron included Configuration, API/Swagger, and Logs views. Swagger UI was bundled locally, without CDN. Visual settings increased text size, contrast, surface clarity, and keyboard-focus visibility.
- The interface could start and stop the engine through API/WebSocket; window closure left it active. Stop was refused while a virtual client was attached. The engine remained `TimoxVirtualAsioEngine.exe`, distinct from the ASIO DLL.
- The engine wrote JSONL logs with configurable levels, bounded API reading, and limited rotation. Configuration and startup events were logged.
- Software checks in that resumption succeeded: native `EngineDiagnosticsTests` and `ApplicationProfilesApiTests`, React tests, API client and contract tests, the PowerShell contract script under pwsh 7, React build, and both candidate Electron packages. Archive inspection confirmed bundled Swagger, and packaged engine matched the built binary. These checks did not validate interactive visual appearance or physical sound.
- Previous candidates in `gui/dist-candidate` were `VASIO Control 1.0.0.exe` and `VASIO Control Setup 1.0.0.exe`. They were not launched during that audit because an older interface and engine were already active.

### Latest read-only runtime observation at that time

- On October 4, the active engine was the old `build_engine_vs2026_ninja/TimoxVirtualAsioEngine.exe` (PID 20452), with the old Electron interface (PID 9128) and Mixxx (PID 19832). API port 52525 belonged to that engine. No process was stopped, replaced, or reconfigured during the audit.
- That old engine's API reported `running` at 48 kHz. The truncated response could not establish routes, buffer size, or signal level. It was not a check of the new candidate or proof of audible sound.
- At that observation, hardware end-to-end signal, the three acceptance audio flows, reconfiguration/disconnect scenarios, and targeted Mixxx installation behavior were still to be checked. This historical snapshot predates the user's October 5 end-to-end test. Further runtime validation awaited the user's launch/test action and report.

### Publication state on October 4, superseded on October 5

- The license notice explained GPL-3.0-only with the ASIO SDK's free license path and specified the official name `TimoxVasio`. Free licensing permits renamed forks; it cannot guarantee names of derivatives.
- README pointed to public `TimoxVasio`, but the publishing clone was not yet available in that historical session.
- No publication commit, tag, or push existed then. Publication awaited access to the public clone.

That October 4 state was superseded: release `v1.0.0` was later published on `timox/TimoxVasio`, and the end-to-end test was confirmed October 5.

### Evidence matrix for October 4, 2026 observation (historical)

| Criterion | Established state | Evidence and limit |
|---|---|
| Single x64 DLL, `TimoxVasio` identity, 256/256; installation without old product entries | Verified by probes and registry check reported above | Local enumeration/COM and registry; recheck in published installation |
| Per-application profiles, Mixxx limits, low/high/sparse allocation | Verified by software tests/probes; real Mixxx discovery confirmed | Stable Mixxx allocation retains its documented limit; end-to-end audio confirmed separately |
| Multi-client shared transport and endpoint identity | Verified by interprocess probes and recorded inventories | Does not replace simultaneous testing of multiple real hosts in the final circuit |
| Three graph route families with physical channels above 6 | Verified by simulated runtime/graph tests | Channel 10 test used simulated buffers; no measured SSL signal in that observation |
| Rate negotiation, physical capability reread, effective ASIO values | Verified by probes and recorded SSL ASIO Driver 1 query | Probes document protocol; end-to-end transfer was separately tested and confirmed by the user |
| Documented API, interface, profiles, bundled Swagger, logs, engine control | **Verified in installed 1.0.0** | User confirmed Swagger; diagnostics API and `engine.log` recorded startup/configuration |
| End-to-end routed audio | **Tested; user confirmed October 5, 2026** | Renoise, 64 inputs/outputs, eight routes, SSL ASIO Driver 1 at 48 kHz/1024 frames; 60 `audio.meter` events over 3.5 s, peaks -101.65 to -26.43 dBFS |
| Reconfiguration, restart, disconnect/reconnect, mismatch refusal, observable failure | **Partially verified** | Tests/probes cover software branches; full validation on target host/device remains |
| GPL license supplied with app and accessible legal notice | **Added to candidate package; UI access not planned** | Electron configured to bundle `LICENSE`, `LICENSING.md`, and ASIO SDK notice under `resources/legal/`. No License screen; rights holder not identified in notice and must not be invented |
| Versioned publication in public `TimoxVasio` repository | **Completed: `v1.0.0`** | Release and binaries published; see release notes |

This historical matrix was updated with test and publication results. October 4 concerns about audio and missing release were resolved. Other-host/transition coverage and accessibility checks remain scope extensions, not version 1.0.0 blockers.

### UX/UI, engine, and logs resumption

- The UX/UI review based on five user screenshots is in [`UX_UI_REVIEW.en.md`](../../UX_UI_REVIEW.en.md), with captures under `docs/ux-audit/evidence/`. They show configuration and routes, but not Logs/Swagger or the post-correction candidate build.
- Screenshots show “VASIO Control” in the header and no Logs/Swagger navigation; the candidate build exposed those views and used “Timox VASIO Control.”
- Old CSS rules restoring 18 px cyan headings and bright cyan scrollbars were corrected. React and candidate Electron packages were rebuilt afterward.
- The matrix legend then showed route count on the current page or stated that none appeared in the view. React and candidate packages were rebuilt after the screenshot-derived correction.
- The development engine was `build_engine_profile_audit_bin/TimoxVirtualAsioEngine.exe`. The packaged copy was `gui/dist-candidate/win-unpacked/resources/backend/TimoxVirtualAsioEngine.exe`; both had SHA-256 `2D97CE37F53B012E685498C81CF61CDF089873261412C36AACBBC454BF9380D5`.
- The “Logs” tab exposed `info/debug`, diagnostics reading, and controlled engine start/stop. Configured path was `%LOCALAPPDATA%/TimoxVasio/logs/engine.log`; at that resumption it did not yet exist in the checked profile. Actual log creation needed observation after starting the new engine.
- Public Electron application name was **Timox VASIO Control**. Rebuilt packages used it; internal ID remained `com.vasio.control` to preserve installation identity.
- Candidate release notes were in [`RELEASE_NOTES_v1.0.0.en.md`](../../RELEASE_NOTES_v1.0.0.en.md); at that point they noted missing hardware audio proof and visual-audit limits.
