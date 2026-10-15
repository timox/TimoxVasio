# TimoxVasio DLL boundary audit — 2026-10-10

> **ARCHIVE** — Document historique conservé pour référence. Il ne décrit pas l'état courant de TimoxVasio (1.2.1). Voir le [README](../../README.md) et la documentation courante qu'il référence.

The driver DLL may publish its own status in the client mapping and send the approved minimal ASIO reset notification. System inventory, opening other audio drivers and disk writes belong outside it.

## Source boundary

- `CMakeLists_VASIO_Drivers.txt` links `application_profile_store.cpp` for the driver's existing profile **read** path. Profile serialization and `ApplicationProfileStore::Save` now live in `application_profile_store_write.cpp`, which is linked only into the engine and persistence tests by `CMakeLists.txt`.
- The driver's added status path uses pagefile-backed `CreateFileMappingW`, atomics and `QueryPerformanceCounter`. It obtains only its own module path with `GetModuleHandleExW` / `GetModuleFileNameW`. The approved reset callback is queued away from the audio worker.
- A search of the driver's project sources found no system-device enumeration, opening of another ASIO driver or disk-write call. Existing profile loading uses `CreateFileW` with `GENERIC_READ` and `ReadFile`; this is a read, not a write.

## Binary evidence

- Existing registered test DLL: SHA-256 `B9F0F6D074625C51F23D0C5300DA6C1ADE443725FC538B214B30B4E2FDCD8FBB`. `dumpbin /imports` showed `CreateFileW` and `ReadFile`, but no `WriteFile`, `MoveFileExW`, `CreateDirectoryW`, `FlushFileBuffers` or `DeleteFileW`. The linker had removed the unused save implementation even before the source split.
- Isolated new DLL: `build_driver_boundary/Release/TimoxVasio.dll`, SHA-256 `9589423E6F0D9782A01F4B98903B89DC1AF5C66B08D6E9DADCEB39F14930D3A6`. The same import check shows `CreateFileW` and `ReadFile` only. It has **not** been registered or packaged for the user's trial.
- `build_engine_boundary/Release/TimoxVirtualAsioEngine.exe` compiled with the separated write source. `build_engine_boundary/Release/ApplicationProfileStoreTests.exe` passed its default, persistence, normalization and validation checks.

The source split strengthens the boundary without changing the DLL currently registered for the user's test. A new packaged/installed test DLL would need its own provenance and live compatibility run before a release verdict. Import checks cover direct imports; source review remains necessary for dynamic calls and future changes.

`tests/verify_driver_boundary.ps1` now checks both the compiled DLL imports and the driver source list, and `tests/write_test_build_manifest.ps1` requires its passing result before writing a test-build manifest. Its import-policy fixture detects `WriteFile` and `MoveFileExW` while allowing the profile reader's `CreateFileW` / `ReadFile`. Both the registered-build copy and the isolated DLL passed this gate.
