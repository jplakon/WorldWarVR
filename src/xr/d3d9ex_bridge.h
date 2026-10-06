// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "xr_types.h"

#include <cstdint>
#include <memory>

struct IDirect3DDevice9;
struct ID3D11Device;
struct ID3D11DeviceContext;

namespace wawvr::xr
{

struct D3D9ExBridgeConfig
{
    // Four slots leave headroom for D3D9 production, one deferred completed
    // capture, D3D11 sampling, and asynchronous consumer retirement without
    // forcing a CPU/GPU synchronization point.
    std::uint32_t ring_size = 4;

    // AcquireSerial only: bound the exact-serial wait so that a late producer
    // cannot create an unbounded render-thread stall. TryAcquireLatestReady is
    // always nonblocking and does not use this budget.
    std::uint32_t producer_wait_budget_microseconds = 6000;
};

struct SharedFrameToken
{
    D3D11SourceFrame source = {};
    std::uint32_t slot = 0;
    std::uint32_t generation = 0;
};

// Single-render-thread bridge. Capture must be called before the game's
// Present. Acquire/Release bracket every D3D11 use of the returned texture.
// The game device must really be IDirect3DDevice9Ex; this class deliberately
// has no synchronous system-memory fallback.
class D3D9ExSharedTextureBridge final
{
public:
    D3D9ExSharedTextureBridge();
    ~D3D9ExSharedTextureBridge();

    D3D9ExSharedTextureBridge(const D3D9ExSharedTextureBridge&) = delete;
    D3D9ExSharedTextureBridge& operator=(
        const D3D9ExSharedTextureBridge&) = delete;

    bool Initialize(
        const D3D9ExBridgeConfig& config,
        const HostCallbacks& host);
    // Call Invalidate with the live D3D11 context before Shutdown whenever a
    // consumer frame may be pending. Shutdown retains an unretired generation
    // rather than releasing shared resources that either GPU may still use.
    void Shutdown();

    // Returns false under backpressure; a Ready slot is never overwritten.
    // Once resources are created, another D3D9 device is rejected until the
    // caller explicitly invalidates the current bridge generation.
    bool CaptureBackBuffer(
        IDirect3DDevice9* device,
        const FrameState* rendered_frame = nullptr,
        std::uint64_t* captured_serial = nullptr);
    // Polls completed producer and consumer fences before the next producer
    // capture chooses a slot. Without this pre-capture service, a full ring of
    // ConsumerPending slots could never reach AcquireSerial again and would
    // remain permanently backpressured.
    bool RetireCompletedFrames(
        ID3D11Device* device,
        ID3D11DeviceContext* context);
    // Acquires only the capture identified by expected_serial. A completed
    // older frame is never substituted for the requested frame.
    bool AcquireSerial(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        std::uint64_t expected_serial,
        SharedFrameToken* frame);
    // Polls producer completion once, then acquires the newest Ready capture
    // newer than last_consumed_serial. After that selected shared resource is
    // validated, older unacquired Ready slots are retired. It never waits for
    // a Producing slot, frees consumer-owned work, or substitutes metadata from
    // another serial into the returned token.
    bool TryAcquireLatestReady(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        std::uint64_t last_consumed_serial,
        SharedFrameToken* frame);
    // Relinquishes exactly expected_serial when the caller will not consume
    // it. A producing capture is retired asynchronously after its D3D9 fence;
    // an acquired or ConsumerPending capture is never freed by this method.
    // True means ownership of the discard was accepted, not necessarily that
    // the producer fence had already completed when this call returned.
    bool AbandonSerial(std::uint64_t expected_serial);
    void Release(
        ID3D11DeviceContext* context,
        const SharedFrameToken& frame);

    // Call before/after a D3D9 Reset/ResetEx sequence.
    // Returns false while a caller still owns a frame, or when D3D9/D3D11 work
    // did not retire in time. A Reset/ResetEx hook must defer reset on failure.
    bool Invalidate(ID3D11DeviceContext* context = nullptr);

    // Lost-device recovery is intentionally split. Once the Ex device is
    // already lost, this preflight marks the bridge unavailable and retires
    // only D3D11 consumer ownership; it never invokes a D3D9 method and never
    // releases the producer generation. It may be retried after an acquired
    // frame has been released.
    bool PrepareForLostDeviceReset(
        ID3D11DeviceContext* context = nullptr);
    // Call only after ResetEx has succeeded following a successful preflight.
    // The successful reset is the producer-retirement barrier, so this method
    // releases the retained generation without querying its old D3D9 fences.
    // Never call it after a failed ResetEx or for a healthy vid_restart.
    bool FinalizeSuccessfulLostDeviceReset();

    bool active() const;
    std::uint64_t latest_serial() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wawvr::xr
