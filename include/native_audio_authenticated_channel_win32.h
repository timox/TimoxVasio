#pragma once
#include "native_audio_broker_transport.h"
#include <Windows.h>
#include <wincrypt.h>
#include <array>

struct NativeAudioChannelCredentials {
    // Already provisioned; factory duplicates the context, never creates keys.
    PCCERT_CONTEXT clientCertificate=nullptr;
    std::array<unsigned char,32> serverCertificateSha256{};
};
// Local fixed bootstrap endpoint. No unencrypted or direct-driver fallback.
std::unique_ptr<INativeAudioAuthenticatedChannel> CreateWindowsNativeAudioAuthenticatedChannel(
    const NativeAudioChannelCredentials&);
#if defined(TIMOX_NATIVE_CHANNEL_TEST_ENDPOINT)
// Isolated test builds only: no endpoint override in production configuration.
std::unique_ptr<INativeAudioAuthenticatedChannel> CreateWindowsNativeAudioAuthenticatedChannelForTest(
    const NativeAudioChannelCredentials&,const wchar_t* bootstrapName);
std::size_t WindowsNativeAudioChannelRetainedBytesForTest() noexcept;
#endif
