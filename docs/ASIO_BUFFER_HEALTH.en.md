# Physical ASIO buffer health

[Français](ASIO_BUFFER_HEALTH.md) | **English**

Timox VASIO Control shows the latest five-second physical ASIO callback observation in **Performance → Physical ASIO buffer**. The same values are available as `bufferHealth` in `GET /api/v1/state`.

The block period is `bufferFrames / sampleRate`. The engine measures processing time around sample conversion and routing, counts callbacks that take longer than a block, and counts starts separated by more than 1.5 block periods. The worst margin is the block period minus the longest processing call in the window. A negative margin means at least one callback took longer than its nominal block period.

The indicator says **insufficient** when a window has fewer than 100 callbacks or less than one second of observations. It says **warning** after three consecutive sufficiently populated windows with at least 1% delayed starts or processing overruns, with a minimum of three incidents per window. It returns to **healthy** after three sufficiently populated windows below that threshold. A healthy reading means this rule has not found repeated timing pressure; it does not guarantee glitch-free sound.

`bufferHealth` is `null` before the first completed window and after stop or reconfiguration. The engine never changes the buffer size automatically. Callback timing cannot identify every driver or hardware failure, and a delayed callback does not by itself prove an audible interruption. Compare the displayed counts with actual audio and, when testing a larger buffer, keep the sample rate and workload comparable. Virtual client transport losses are reported separately through `audio.meter`.

The Performance tab diagrams the audio path and the observed physical callback time within one buffer period. The diagram does not measure total application-to-speaker latency or recommend another buffer size.

The clock diagram follows the selected physical ASIO device: its callbacks drive TimoxVasio, which wakes virtual ASIO clients. This avoids a separate virtual timer drifting from that device; callback scheduling can still jitter, and another physical device may drift.

The transport diagram shows that an application writes a completed output block into shared memory and the engine reads it on a following physical callback. A route through another ASIO application adds another handoff. The block duration is calculated from the selected frame count and sample rate; it is not an end-to-end latency measurement.

The **Virtual transport** section displays the existing `audio.meter` underrun and overrun counters for client directions used by active routes. These counters are cumulative for the lifetime of each client transport. A completed client callback is not proof that it carried audible signal. The interface does not request per-block timing or inspect samples to populate this section, so it adds no work to the audio callback. Actual application-to-speaker latency requires a separate end-to-end measurement, including hardware conversion.
