# Mixxx compatibility with 256 ASIO channels

[Français](mixxx-256-channel-compatibility.md) | **English**

> **Overall project status:** The maintainer confirms that end-to-end ASIO host and hardware tests were performed in their environment. This document preserves the account of a specific Mixxx 2.7 test whose first step was driver discovery; that historical observation alone does not describe later tests.

## Established diagnosis

Mixxx 2.6 beta x64 does not display `TimoxVasio` because its internal representation of a channel count cannot represent 256.

The installed build's log identifies Mixxx commit `2.6-beta-402-ge1c1e5b72b`. The code at that commit confirms this path:

1. PortAudio fills `PaDeviceInfo::maxInputChannels` and `maxOutputChannels` from the counts returned by the ASIO driver.
2. `SoundDevicePortAudio` converts each `int` to `mixxx::audio::ChannelCount`.
3. In `src/audio/types.h`, `ChannelCount::value_t` is `uint8_t`, and `valueFromInt()` accepts at most `std::numeric_limits<uint8_t>::max()`, or 255. Thus 256 becomes the invalid sentinel value 0.
4. `SoundManager::getDeviceList()` discards a device when both its input and output channel counts are invalid.

TimoxVasio deliberately advertises 256 inputs and 256 outputs under the project's ASIO contract. Mixxx receives 256/256 from PortAudio, converts both to invalid counts, and then removes the device from the list. This matches the observed absence in the interface. The issue occurs after ASIO enumeration, at Mixxx's representation limit.

## Compared evidence

- The PortAudio probe built in this repository enumerates `TimoxVasio` with 256 inputs, 256 outputs, and a supported format at 48 kHz when the engine is locked to 48 kHz.
- The driver returns 256/256 through `getChannels()` and provides the channel information requested by PortAudio.
- Mixxx source at the commit identified in its log contains the `ChannelCount(int)` conversion and filtering described above.
- The Mixxx log shows it starting on VB-Matrix. A Mixxx PID previously recorded by the TimoxVasio engine did not prove that Mixxx retained the device in its list.

## Required host-side fix

For this Mixxx build to display and use all 256 channels, Mixxx must represent at least 256 in `ChannelCount` and preserve that value through channel selection and allocation. Changing TimoxVasio to advertise 255 would hide the symptom by reducing the advertised capacity and would not satisfy the 256/256 contract.

A minimal patch that widens `ChannelCount::value_t` to `uint16_t` is provided in [`patches/mixxx/0001-audio-channel-count-support-256.patch`](../patches/mixxx/0001-audio-channel-count-support-256.patch). It was applied only to a local copy of Mixxx 2.7 source under `vendor/mixxx-2.7-256`; the x64 build produced `build_mixxx_2.7_256/mixxx.exe`. On October 4, the user launched that copy and confirmed that it discovers TimoxVasio. This dated observation concerns discovery of that copy; it does not claim to summarize end-to-end tests later confirmed by the maintainer. The patched copy is separate from the installed version: Mixxx 2.6 beta, commit `2.6-beta-402-ge1c1e5b72b`, remains unpatched.

At the end of the October 4 observation, the Timox engine was stopped and had no configured physical clock; TimoxVasio therefore advertised its fallback sample rate of 44.1 kHz. The engine was then configured through `configuration.apply` with `SSL ASIO Driver 1`, at its confirmed current sample rate of 48 kHz and preferred buffer size of 1024 frames. API state was `stopped` with no routes, and no Mixxx client was attached during this observation. These dated observations describe that initial test, not the end-to-end tests subsequently confirmed by the maintainer.

The TimoxVasio repository does not modify the Mixxx installation. The Mixxx 2.6 binary identified above still lacks the `ChannelCount` fix; that specific version cannot represent the 256 channels advertised by TimoxVasio. The proposed Mixxx fix in this repository remains separate and must be integrated into the Mixxx project to resolve the incompatibility at its source.
