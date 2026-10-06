# TimoxVasio — ASIO compliance assessment

[Français](ASIO_CONFORMITE.md) | **English**

## Conclusion

The active code implements the SDK's `IASIO` interface and its principal discovery, format, allocation, and callback operations. Local probes check 256 channels in each direction, index 255, rejection of index 256, and audio transport on channels 0, 127, and 255.

These results establish tested interface behavior. They are neither Steinberg certification nor validation with every ASIO host. The end-to-end test on this workstation is described below. The interface reference is `asiosdk/common/iasiodrv.h` in the included SDK and the [SDK IASIO definition](https://github.com/audiosdk/asio/blob/main/common/iasiodrv.h).

## Verified elements

| ASIO topic | Active behavior | Local evidence | Status |
| --- | --- | --- | --- |
| Driver interface | `VASIODriver` implements abstract `IASIO` methods | [vasio_com_driver.h](src/vasio_com_driver.h) compiles against the SDK header | Build verified |
| Discovery and name | Driver name is `TimoxVasio`; COM class and factory are registered in the DLL | [vasio_driver_factory.cpp](src/vasio_driver_factory.cpp), [vasio_com_driver.h](src/vasio_com_driver.h), COM probe | Probe verified |
| Channel counts | `getChannels` advertises 256 inputs and 256 outputs | [driver_probe.cpp](tests/driver_probe.cpp), [driver_audio_probe.cpp](tests/driver_audio_probe.cpp) | Verified |
| Channel indexes | `getChannelInfo` accepts 0–255 and rejects 256; sparse allocation accepts 0, 127, and 255 | [vasio_driver.cpp](src/vasio_driver.cpp), driver probes | Verified |
| Buffer format | Little-endian float32, double-buffered; bounds and duplicate indexes checked | `getChannelInfo` and `createBuffers` in [vasio_driver.cpp](src/vasio_driver.cpp) | Code and audio probe verified |
| Block size and rate | Without an attached engine, driver-defined advertised capabilities; with one, only actual physical values accepted | `getBufferSize`, `canSampleRate`, `setSampleRate`, `createBuffers` in [vasio_driver.cpp](src/vasio_driver.cpp); October 5 Renoise test | Verified in Renoise at 48 kHz / 1024 frames; other hosts remain additional coverage |
| Callbacks and position | `start`/`stop` control a worker; buffer callbacks and sample position/timestamp are published | `callbackLoop` in [vasio_driver.cpp](src/vasio_driver.cpp), [driver_audio_probe.cpp](tests/driver_audio_probe.cpp) | Transport probe verified |
| Optional control | `controlPanel`, `future`, `outputReady`, and explicit clock-source selection are unavailable (`ASE_NotPresent`) | [vasio_driver.cpp](src/vasio_driver.cpp) | Declared limitation |

## Advertised capacity and used channels

ASIO distinguishes the count advertised by `getChannels` from the channels for which a host requests buffers in `createBuffers`. TimoxVasio advertises at most 256 per direction; the engine reports only each client's allocated channels. Advertising 256 does not mean that an application opens all 256.

This matters to hosts that store a channel count in a type too small for 256. The behavior observed in some Mixxx versions is documented in the [compatibility note](docs/mixxx-256-channel-compatibility.md). It does not change the driver contract.

## Host and hardware validation — October 5, 2026

After reinstalling version 1.0.0, the user confirmed audible end-to-end audio with Renoise and SSL ASIO Driver 1. The API showed the engine `running` at 48 kHz / 1024 frames, one Renoise client with 64 inputs and 64 outputs, and eight active routes. The `audio.meter` stream produced 60 events over 3.5 seconds for routed physical outputs and source virtual outputs; observed peaks ranged from `-101.65` to `-26.43 dBFS`. Diagnostics recorded application of all eight routes and the physical configuration. The user also verified Swagger in Electron.

This validates the signal path tested on that workstation. Other ASIO hosts, devices, and transitions can be covered separately. Reproducible build and probe instructions are in [BUILD_DRIVERS.md](BUILD_DRIVERS.md); see also the [README](README.en.md).
