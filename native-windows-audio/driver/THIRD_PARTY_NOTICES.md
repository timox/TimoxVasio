# Microsoft SysVAD provenance

The files under `upstream/` derive from Microsoft Windows-driver-samples,
`audio/sysvad`, commit `bb68ed5e770bce04dc5395ec9258dafab034184d`.
Repository: https://github.com/microsoft/Windows-driver-samples

Copyright notices remain in each file. The Microsoft Public License (MS-PL)
is reproduced in full in `upstream/LICENSE` and governs those files and their
source derivatives. `minipairs.h` and `TimoxWindowsAudio.inx` also derive from
SysVAD table/INF conventions and are distributed under the same MS-PL license.

Changes for the Timox prototype:

- A dedicated project compiles only generic adapter/topology/WaveRT sources.
  Bluetooth/USB/A2DP endpoint sources, sample APOs, keyword adapter DLLs,
  speaker/headphone/HDMI/SPDIF/microphone-array descriptions are not included.
- `minipairs.h` replaces sample registrations with two Timox endpoints and one
  fixed PCM format. Offload, loopback and keyword flags are not exposed.
- `upstream/EndpointsCommon/minwavertstream.cpp/.h` remove tone generation and
  rendered-audio file recording; capture memory is explicitly zeroed and
  silence catch-up is bounded to one ring. No user/kernel transport is present yet.
- `upstream/common.cpp` removes the sample recorder dependency.
- `MiniportAudioEngineNode.cpp` retains the generic volume/mute paths without
  hardware sideband branches. `MiniportStreamAudioEngineNode.cpp` rejects
  loopback protection (no loopback pin exists); `minwavert.cpp` removes the
  recorder callback.
- `minwavert.cpp` bounds-checks property pin identifiers and pin predicates;
  `minwavertstream.cpp` handles mapping allocation failure, empty buffer state,
  and discards render with no memory access. These guards do not replace
  lifecycle synchronization or runtime kernel verification.

Internal class names and unexposed generic helper methods remain recognizable
to make comparison with upstream possible. This is an adaptation, not a claim
that Microsoft authored, certified or endorses TimoxWindowsAudio.

N2 additionally routes stream PCM through the separate Timox private control
device. `adapter.cpp` preserves original dispatch functions and installs the
private dispatcher; `common.cpp/.h` own its lifecycle and expose internal PCM
callbacks. The shared N0 validator now uses kernel-compatible integer aliases
without changing the 64-byte wire layout. `control.cpp/.h` are Timox additions.

Further N2 review replaces sample millisecond/32-bit position arithmetic with
whole-frame 64-bit timing and separate completed/notified packet counters;
aligns EOS/write positions; timestamps the first sample of the completed packet;
accounts for catch-up loss and purges stale queued PCM. Content-right changes
are serialized and private render stays blocked for the stream lifetime after
protection or unknown rights. Timer callbacks are quiesced before releasing
miniport/stream resources, then the reserved render slot is purged/unblocked.
