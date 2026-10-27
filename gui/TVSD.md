# TimoxVSoundDealer (TVSD) 0.1

TVSD 0.1 is a separate portable application built from the GUI sources in this repository. It is paired with the TimoxVasio engine and ASIO driver 1.2.7.

- Package identity: `com.timox.vsounddealer`
- Application version: `0.1.0`
- Output: `gui/dist/tvsd-0.1.0/TimoxVSoundDealer-0.1.0-portable.exe`
- Build: run `npm run dist:tvsd` from `gui/`
- The portable package includes the canonical 1.2.7 engine. It does not install, register, replace, or move the ASIO driver.
- When another engine is already running, TVSD 0.1 only attaches to API 1.1; it will not attach to the older API 1.0 engine.
- The existing TimoxVasio 1.2.7 driver and its release artifacts remain governed by the root repository release workflow.

The shared GUI sources keep their existing Timox VASIO Control branding in the legacy build. The TVSD build sets its own product name and package identity without changing the legacy release configuration.
