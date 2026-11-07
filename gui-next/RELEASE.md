# Version and assembly tracking

This repository's next candidate is **TimoxVSSC 0.2.0-alpha.1**, not the old
combined TimoxVasio 1.2.7 package. It requires engine **1.3.0-alpha.1**, API **1.1**,
and a Windows MIDI Services enabled build for its MIDI workspace. The preview
bundles ASIO driver **1.2.7-test** in its NSIS installer. Its uninstaller removes
the GUI-owned engine, PWAR client and ASIO clients loading that preview DLL,
then removes the preview registration when it is still active, so the stable
driver can be installed afterward.
The bundled PWAR candidate is independently versioned **0.1.0-alpha.1**, with
control protocol **v1**. Its audio continuity remains unqualified.

## Verified component builds

- **PWAR 0.1.0-alpha.1**, source `7179c868e7110ae90fbe5bfd24eb1f0f56f154e0`:
  [Windows/Linux run 41240762850](https://github.com/timox/Timox-Pwar/actions/runs/41240762850).
  Windows **22/22** and Linux **23/23** CTest tests passed. The Windows client,
  Linux library/backends and their clean-source receipts are available as artifacts.
- **Engine 1.3.0-alpha.1**, source `78ec5f9734389382215f706799372015fa7a061c`:
  [Windows run 41240930939](https://github.com/timox/TimoxVAudioEngine/actions/runs/41240930939).
  MIDI-enabled compilation, **34/34** CTest tests and the provenance test passed.
  This build requires the exact PWAR version/commit/hash above; it supersedes the
  initial MIDI engine built before the versioned PWAR client.

Verified Windows SHA-256:

| Binary | SHA-256 |
|---|---|
| TimoxVirtualAsioEngine.exe | `a037bf3d27ac9998272e609f75b15efb68064d6aeff323e855dd83599b4227f5` |
| PwarAsioClient.exe | `c0b9287c5c19efc9906fe80b748959a36cf93b1c47cd9b87f49a338062476b6d` |

Both executables and receipts are pinned in artifacts.lock.json. The complete
`npm run release-preflight` passes for these local inputs. This validates binary
provenance and isolated tests, not real MIDI/audio hardware, PWAR continuity,
a final portable GUI package or publication. PWAR's documented discontinuity
remains unresolved; existing React suite failures are listed below.

`artifacts.lock.json` is the single assembly manifest: independent component
versions, exact expected source revisions, feature flags, binary/schema hashes
and explicit qualification gaps. `package.json` owns the GUI product version;
`package-lock.json` must agree. API version is a compatibility contract and does
not imply that the optional MIDI backend is present.

## From source candidate to package

1. Integrate the paired engine and GUI PRs. Update the engine revision in the
   manifest if the final source commit changes. Keep historical extraction
   provenance in import-manifest.json; do not confuse that manifest's original
   sourceCommit with the current component revision.
2. Build the engine from that clean commit on Windows using its RELEASE.md,
   with TIMOX_ENABLE_WINDOWS_MIDI_SERVICES=ON and the verified MIDI metadata.
   Run the Windows suite and qualify the real MIDI backend separately. Copy
   TimoxVirtualAsioEngine.exe and engine-build.json together to artifacts/backend.
3. Run `npm run pin-engine`. This validates the receipt against the expected
   source commit, version, API, MIDI option and actual binary SHA-256, then updates
   only the engine artifact pin. The previous October 26 binary cannot satisfy
   this MIDI candidate; the current pin identifies the verified build above.
4. Supply PwarAsioClient.exe and pwar-build.json from the selected clean PWAR
   source revision, then run `npm run pin-pwar`. The engine must be rebuilt with
   that same client hash. Its receipt declares the required PWAR product version,
   source commit and SHA-256; preflight checks them against the bundled client.
   Refresh schema snapshots/pins only from the selected engine source revision.
5. Run `npm ci`, `npm run release-metadata`, `npm run contract-test`,
   `npm run electron-test`, `CI=true npm run react-test -- --watchAll=false --runInBand`
   and `npm run react-build`. Record failures honestly: four existing React suites
   currently fail; successful focused MIDI tests do not make the full suite green.
6. Commit manifest/version/source changes. Run `npm run release-preflight` and
   `npm run dist` on the coordinated Windows packaging environment. beforePack
   refuses dirty GUI sources and emits build/release-manifest.json with the exact
   GUI Git revision, component pins and engine receipt. It is included both in
   the renderer build and as resources/release-manifest.json. Postflight checks
   the packaged binary/schema copies and metadata.

Expected filename: `TimoxVSSC-0.2.0-alpha.1-preview.exe`. This is an unsigned
candidate name, not a claim of publication or successful runtime qualification.
Keep the release manifest alongside any distributed file and record the final
distributable's SHA-256 externally after packaging (a file cannot contain its
own final hash).

## What comes after 1.2.7

| Stage | Implemented scope | Qualification |
|---|---|---|
| Public combined 1.2.7 | ASIO driver, routing engine, Control GUI, API 1.0 | Historical published release |
| Extracted audio baseline | Software clock, WASAPI bridges, PWAR supervisor, API 1.1 | Existing bounded tests/hardware observations; no blanket qualification |
| Supervision baseline | Engine-owned adherents/nodes/links, observations and corresponding GUI | Focused tests; freshness limits remain |
| This paired MIDI candidate | MIDI 1.0 ports/routes, BPM, reclocking to the audio reference, separate GUI workspace | Windows compilation and isolated tests passed; real runtime/hardware unqualified |

Native Windows audio is experimental; the shared transport and ASIO driver are
owned by TimoxVasio, WASAPI remains inside TimoxVAudioEngine, and PWAR transport
and Linux backends are owned by Timox-Pwar. The original repository retaining
historical source copies does not make it the current owner of every component.
