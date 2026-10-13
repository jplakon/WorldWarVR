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

enum class D3D9CpuCaptureResult : std::uint8_t
{
    captured,
    unavailable,
    device_lost,
};

// Bootstrap fallback for stock WaW's non-Ex D3D9 device. This path performs a
// synchronous GPU readback and CPU-to-D3D11 upload; it is intentionally kept
// separate from D3D9ExSharedTextureBridge so callers cannot mistake it for the
// production path.
class D3D9CpuCapture final
{
public:
    D3D9CpuCapture();
    ~D3D9CpuCapture();

    D3D9CpuCapture(const D3D9CpuCapture&) = delete;
    D3D9CpuCapture& operator=(const D3D9CpuCapture&) = delete;

    bool Initialize(const HostCallbacks& host);
    void Shutdown();

    // Call before Present on the D3D9 render thread.
    D3D9CpuCaptureResult CaptureBackBuffer(
        IDirect3DDevice9* device,
        const FrameState* rendered_frame = nullptr);

    // Uploads the newest CPU frame to a reusable D3D11 texture and returns the
    // same source shape consumed by D3D11Compositor.
    bool UploadLatest(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        D3D11SourceFrame* frame);

    // Releases only resources owned by the legacy D3D9 device. The last CPU
    // frame and the independent D3D11 upload remain valid, so OpenXR can keep
    // presenting its last good projection while T4 completes a Reset.
    void InvalidateD3D9();

    // Present can report a lost legacy device before the capture calls do.
    // Preserve the last projection, release reset-blocking D3D9 references,
    // and defer the next synchronous readback while the device recovers.
    void NotifyExternalDeviceLoss();

    void Invalidate();
    std::uint64_t latest_serial() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wawvr::xr
