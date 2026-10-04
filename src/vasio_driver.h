#pragma once

#include <windows.h>
#include "../asiosdk/common/asio.h"
#include "../asiosdk/host/asiodrivers.h"

// ============================================================================
// Virtual ASIO Driver - Header
// ============================================================================

struct ChannelBuffer {
    void* buffers[2];  // Double-buffer [0] et [1]
    float* float_buffers[2];
    ASIOBool isInput;
};

struct VASIODriverState {
    // Identifiant
    int driverId;          // 1, 2, 3, ou 4
    char driverName[256];

    // Callbacks
    ASIOCallbacks callbacks;

    // Buffers
    ChannelBuffer* channels;
    long numInputChannels;
    long numOutputChannels;
    long bufferSize;
    long allocatedChannels;
    int currentBufferIndex;

    // Configuration
    ASIOSampleRate sampleRate;
    long minBufferSize;
    long maxBufferSize;
    long preferredBufferSize;
    long bufferGranularity;

    // État
    bool initialized;
    bool isRunning;

    // Worker which drives the host's double-buffer callback.
    HANDLE stopEvent;
    HANDLE bufferThread;

    // Timing
    LARGE_INTEGER startTime;
    long samplePosition;
};

// API globale du driver
extern VASIODriverState* g_driver_state;

// Fonctions publiques
ASIOError InitDriver(int driverId);
ASIOError DisposeDriver();
void BufferSwitchCallback(long index, ASIOBool processNow);
long MessageCallback(long selector, long value, void* message, double* opt);
void SampleRateChangeCallback(ASIOSampleRate sampleRate);
