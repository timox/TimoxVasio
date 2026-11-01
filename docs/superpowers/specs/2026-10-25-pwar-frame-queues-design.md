# PWAR frame queues and callback adaptation

**Status:** target design; partial Linux queue patches 0011/0012 and router
patch 0013 exist as unqualified candidates. A separate experimental two-stream
Linux CLI candidate exists in
[`experimental-pipewire-stream-backend.patch`](../../../patches/pwar/experimental-pipewire-stream-backend.patch).
It depends on router validation patch 0001 and queue patch 0011, but has not been
applied to the pinned source or built. Windows frame rings and end-to-end
behavior remain unimplemented or unqualified.

## Intent

Make the existing PWAR Windows/Linux audio path carry audio when the actual
PipeWire callback quantum differs from the Windows ASIO block size. Keep the
existing PWAR UDP packet structure and 128-frame per-packet limit. Keep all
socket activity and network waiting away from the PipeWire and ASIO audio
callbacks.

The user approved bounded frame queues as the adaptation model. This document
records the target architecture and distinguishes it from the partial
candidate path now under review.

## Current behavior and defect

The PWAR packet has `seq`, `num_packets`, `packet_index` and up to 128 float
frames per channel. Patch 0008 teaches the generic router to reassemble a short
tail, such as 192 frames sent as 128+64. Its `partial` test calls the router
directly; it does not exercise the ASIO UDP listener.

The Linux `on_process` path currently silences quanta above 128 frames. Its
ping-pong function sends one packet for the callback and performs `sendto` and
mutex operations in that callback. On Windows, `udp_packet_listener` derives
the fragment count from `blockFrames / chunk_size`, rejects a nonzero remainder,
and copies `blockFrames` even when the router reports fewer `samples_ready`.
Thus neither the Linux callback nor the unpatched Windows listener implements
variable-size end-to-end blocks. Candidate patch 0013 replaces a positive
router result whose size differs from `blockFrames` with a silent full block
and counts an xrun; incomplete results (`samples_ready == 0`) still do not
trigger a host callback.

## Proposed data flow

Each endpoint has two preallocated single-producer/single-consumer frame rings:

| Side | Producer → consumer | Direction |
|---|---|---|
| Linux | PipeWire callback → UDP sender worker | Linux audio to Windows |
| Linux | UDP receiver worker → PipeWire callback | Windows audio to Linux |
| Windows | UDP receiver worker → ASIO callback scheduler | Linux audio to ASIO client |
| Windows | ASIO callback scheduler → UDP sender worker | ASIO client audio to Linux |

Linux uses separate PipeWire capture and playback `pw_stream` objects,
negotiated as float32 stereo at 48 kHz. PipeWire's format adapters handle the
graph's real rate and quantum; the bridge does not try to force the graph. This
is required by the observed case where the graph ran at 44.1 kHz with a 1024
frame quantum despite a 48 kHz / 128 frame request. Each stream callback copies
its actual available frame count to or from its SPSC ring. A single network
worker owns the UDP sockets, packetizes outgoing ring data and validates and
reassembles incoming data into the playback ring. No socket, mutex, condition
wait, heap allocation, or logging belongs in either PipeWire callback.
Connect each stream with `PW_STREAM_FLAG_AUTOCONNECT`,
`PW_STREAM_FLAG_MAP_BUFFERS`, and `PW_STREAM_FLAG_RT_PROCESS`; do not set
`PW_STREAM_FLAG_NO_CONVERT`, because the PipeWire stream adapter must convert
between the fixed 48 kHz network format and the graph format. The process
callbacks use only the RT-safe dequeue/queue operations and bounded copies.
Report negotiated stream format and PipeWire timing outside those callbacks.

On Windows, the host's ASIO block size and callback cadence remain authoritative
for the ASIO driver. The UDP listener is a network worker, not an audio clock:
it validates packets and pushes complete decoded frames into the ASIO input
ring. The ASIO callback consumes exactly its configured `blockFrames`, zeroes
any unavailable tail, and records an underflow. On its output side it pushes
the host's block into a separate ring for the sender worker. The callback never
waits for a packet. If the driver implementation needs a timer to invoke the
host's ASIO callback, that timer follows the negotiated ASIO rate and block
size; it must not be paced by UDP arrivals.

The target design sends fixed 128-frame datagrams whenever that many frames
are available, with `num_packets = 1`, `packet_index = 0`, and a monotonically
incremented `seq` for each datagram. The local frame rings, rather than packet
reassembly, reblock the continuous stream for each callback. The Linux
candidate currently uses 64-frame datagrams instead: this makes its existing
ASIO listener's block-size divisibility check work for every supported
`blockFrames` multiple of 64. It remains a transitional use of `pw_filter`,
not the target pair of adapted `pw_stream`s described above.

The existing protocol has two sequence forms. Linux stream datagrams each
advance `seq`; Windows responses group explicit fragments under the input
block's base sequence and set `packet_index`. The ASIO listener marks stream
datagrams locally before adding `num_packets`, so the generic router does not
overwrite explicit fragment indices on Windows responses. The marker is
internal and does not change the UDP packet ABI. In the current candidate,
Linux accounts for a dropped or incomplete Windows response block when a newer
base sequence arrives: it inserts silence for the missing frame duration,
counts an xrun, and caps one gap to 4096 frames. Late old fragments are
discarded. Patch 0013 synthesizes silence for missing interior Linux chunks
when a later chunk reveals the gap, so that block can still complete and drive
an ASIO callback. If the final chunk of a block is lost, no later chunk reveals
the hole; the incomplete block is discarded only at the next sequence boundary
and one callback can still be delayed. Independent ASIO-clock scheduling and
full recovery remain target work.

Preserve timestamps per datagram and keep send/receive sequence spaces
independent in each direction. A tail shorter than the packet size stays
buffered until more frames arrive; shutdown may discard that final tail.

## Real-time and capacity rules

- PipeWire and ASIO audio callbacks perform bounded copies and counter updates
  only: no sockets, mutexes, condition waits, heap allocation, console output,
  or packet logging.
- Frame rings are preallocated and have a fixed, documented maximum. A queue
  overflow never overwrites frames the consumer may be reading. Each accepted
  span carries its absolute stream-frame offset; the producer advances that
  offset for every callback even when a complete callback quantum cannot be
  queued. The worker detects the resulting gap and emits silence for the lost
  duration before later audio, while incrementing an overflow counter. Thus a
  dropped callback does not compress the stream timeline. The metadata and
  counters use fixed-width, wrap-safe arithmetic and are preallocated with the
  ring.
- Each ring holds at most 4096 frames per channel, matching the existing
  router's maximum logical block and 64 minimum-size fragments. At 48 kHz this
  is an 85.3 ms storage ceiling, not the target fill. Normal occupancy should
  stay at or below one local callback block; the maximum logical input block
  is also 4096 frames.
- Queue read/write requests larger than 4096 frames are rejected before any
  copy. The audio callback that requested an oversized span supplies silence
  and records the overrun; it must never ask the ring to clear or copy beyond
  its fixed storage capacity.
- An output underflow is filled with zeroes for the missing frames. No callback
  waits for network data.
- The packet/reassembly size remains bounded by the existing PWAR router limit.
  Reject a larger logical block before copying it; do not grow buffers in the
  callback.
- Receive sequence gaps represent missing frame spans in the sequence's packet
  unit. Insert silence
  for a bounded gap and count the loss; for a gap larger than the ring
  capacity, clear/resynchronize the frame ring and count a reset. Duplicate and
  stale packets are discarded without moving the consumer position.
- A local output callback remains clocked by its host. It consumes the exact
  requested frame count, fills missing or late frames with silence, advances
  its stream-frame position, and counts an underrun; packets arriving after
  their frame position has passed are discarded rather than replayed late.
- Queue capacity and target fill are separate. The target is the smallest
  occupancy that survives measured scheduling and network jitter; it must be
  qualified with the existing 48 kHz route before being made configurable.

The v1 PWAR network format remains fixed at 48 kHz and two channels. Linux
PipeWire adapters convert between that rate and the graph's negotiated rate;
they do not solve clock drift between the Linux network stream and the
independent Windows ASIO clock. The bounded queues make that drift visible and
contain its immediate effects, but a production-stable bridge still needs a
separately designed and measured clock-drift strategy. The design does not
change the raw UDP packet ABI. Automatic recovery after an unidentified peer
restart also remains unresolved; bounded frame queues alone do not solve it.

## Failure behavior and observability

Keep separate counters for PipeWire input overflow, Linux output underflow,
Windows ASIO input underflow, Windows output overflow, malformed/incomplete
sequences and late packets. Report the actual PipeWire quantum, ASIO block size
and queue occupancy outside the callbacks. A callback must remain silent and
bounded if its DSP input pointer is null or a sequence is invalid.

## Qualification required before calling the path functional

Use deterministic, distinct left/right sample sequences and verify continuity
and channel order in both directions for PipeWire graph rates 44.1 and 48 kHz,
observed graph quanta 32, 64, 96, 128, 192, 256, 512 and 1024. For the current
ASIO driver, verify its declared block sizes: multiples of 64 from 64 through
2048 frames. Do not claim 96-frame ASIO support unless the driver advertises
that granularity. Confirm the negotiated Linux stream remains 48 kHz while
PipeWire adapts to the graph. The target frame rings decouple each host callback
size from the 128-frame network datagrams, which use one sequence each.
Include packet loss, duplicates, out-of-order delivery, queue overflow,
underflow, shutdown/restart, and the largest accepted callback. Verify no stale
frames are delivered after a malformed packet or resync.

On a real PipeWire/Windows pair, measure xrun counts, queue occupancy, CPU cost,
and end-to-end latency at p50/p95/max under idle and loaded conditions. Inspect
the audio callbacks to confirm they contain no network call, lock, wait,
allocation or logging. A router-only test is not evidence for the listener or
for the live audio path.

## Non-goals for this change

- Changing the PWAR UDP structure, port or protocol version.
- Replacing PWAR with a new network protocol.
- Adding remote Timox API control or certificate pairing.
- Claiming clock-drift correction, arbitrary sample rates or production-level
  latency before those properties have been measured.
