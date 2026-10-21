#pragma once

#include "wasapi_backend.h"
#include "wasapi_bridge_io.h"
#include <functional>
#include <memory>

struct WasapiBridgeConfig {
    WasapiOpenConfig endpoint;
    std::uint32_t maxFramesPerPass = 4096;
    std::uint32_t maxPacketsPerPass = 8;
    std::uint32_t waitTimeoutMs = 100;
    std::uint32_t primingTimeoutMs = 2000;
    double targetFillFrames = 0; // Zero selects half the immutable graph FIFO.
    double kp = 0.000003;
    double ki = 0.000001;
};

enum class WasapiBridgeState { Idle, Starting, Priming, Running, Stopped, Failed };
struct WasapiBridgeHealth {
    WasapiBridgeState state = WasapiBridgeState::Idle;
    HRESULT error = S_OK;
    HRESULT mmcssError = S_OK;
    bool mmcssActive = false;
    bool primingTimedOut = false;
    std::uint64_t capturedFrames = 0; // Native device frames received and released.
    std::uint64_t renderedFrames = 0; // Native device frames submitted.
    std::uint64_t droppedCaptureFrames = 0; // Graph-rate output dropped on a full FIFO.
    std::uint64_t renderSilencedInputFrames = 0; // Missing graph-rate frames fed as zero to SRC.
    std::uint64_t renderSilencedDeviceFrames = 0; // Native silence submitted during priming.
    std::uint64_t discontinuities = 0;
    std::uint64_t timestampErrors = 0;
    double ratio = 1; // Actual SRC output/input ratio, including bounded correction.
};

// One worker, one immutable WasapiBridgeIo generation, one Start attempt.
// Construct/Start/Stop/destroy on control threads, NEVER in the graph callback.
// The caller owns IO and keeps it alive until BOTH this worker and the graph
// have retired it. The bridge never clears/resizes/replaces IO or resets SRC.
// Reconnection requires a NEW bridge/generation prepared by the control owner,
// publication at a graph boundary, then retirement of the old borrowed IO.
class WasapiBridge final {
public:
    using BackendFactory = std::function<std::unique_ptr<IWasapiBackend>()>;
    WasapiBridge(WasapiBridgeConfig config, WasapiBridgeIo& io,
                 BackendFactory factory = CreateWasapiBackend);
    ~WasapiBridge();
    WasapiBridge(const WasapiBridge&) = delete;
    WasapiBridge& operator=(const WasapiBridge&) = delete;

    // Returns after worker Open, allocation and native Start, not after signal
    // arrival or FIFO priming. COM/MMCSS/FTZ and backend destruction stay worker-
    // owned. Synchronous native Open cannot be forcibly interrupted: cancellation
    // is observed as soon as it returns, before native Start.
    HRESULT Start() noexcept;
    void RequestStop() noexcept; // May race Start/Stop; only signals the stop event.
    void Stop() noexcept; // Signals first, then serializes and joins; no worker self-call.
    WasapiBridgeHealth Health() const noexcept; // Independent atomic observations.

    // Priming keeps the PI at its previous ratio (initially nominal). Render
    // submits native silence until graph occupancy reaches target; underflow
    // re-enters priming without resetting SRC history (a silence gap is possible).
    // Capture should normally fill before graph publication. If consumption
    // prevents target occupancy for primingTimeoutMs, capture enables PI and
    // exposes primingTimedOut. Render keeps waiting for real input, cancellably.
    // MMCSS failure is exposed as degraded scheduling; it does not hide otherwise
    // usable audio. No latency/performance qualification is implied by Start.
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
