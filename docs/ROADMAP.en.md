# Roadmap after release 1.1.0

[Français](ROADMAP.md) | **English**

## ASIO buffer-size diagnostics

Determine whether the configured buffer size suits the selected sample rate and actual workload, without automatically changing the audio configuration.

- Translate the documentation into English.
- Improve the interface design: typography, colors, space use, and readability.
- Measure missed deadlines in the physical ASIO driver's callback, processing duration, and remaining time within a block.
- Distinguish these delays from underruns and overruns already counted in the application-to-engine transport.
- Publish counters and their observation period in the documented API, then display them in Timox VASIO Control diagnostics.
- Flag a potentially insufficient buffer size only after several consistent measurements, with observed values and sample rate.
- Evaluate automatic phase balancing and possible implementation.
- Check several physical drivers and applications to establish whether the counters reflect audible glitches and whether a larger buffer reduces them.
- Evaluate a graphical patchbay with curved connections between channel ports, similar to JACK control tools.

This work is planned **after** completion of release 1.1.0.
