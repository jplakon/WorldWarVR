// SPDX-License-Identifier: GPL-3.0-only
// WorldAtWarVR D3D9 diagnostic capture. Comparative research history and the
// Public API usage follows the Direct3D SDK contract.

#include "d3d9_cpu_capture.h"

#if !defined(_WIN32)
#error WorldAtWarVR's D3D9 capture currently supports Windows only.
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace wawvr::xr
{

using Microsoft::WRL::ComPtr;

namespace
{

constexpr ULONGLONG kDeviceLostRetryMilliseconds = 2'000;
constexpr ULONGLONG kExternalDeviceLostRetryMilliseconds = 5'000;

bool IsDeviceLostResult(const HRESULT result) noexcept
{
    return result == D3DERR_DEVICELOST ||
           result == D3DERR_DEVICENOTRESET;
}

} // namespace

struct D3D9CpuCapture::Impl
{
    HostCallbacks host = {};
    mutable std::mutex mutex;
    bool initialized = false;
    bool logged_cost_warning = false;
    bool logged_first_failure = false;
    bool logged_device_lost_pause = false;
    ULONGLONG device_lost_retry_after = 0;

    ComPtr<IDirect3DDevice9> d3d9_device;
    ComPtr<IDirect3DSurface9> readback_surface;
    ComPtr<IDirect3DSurface9> resolve_surface;
    D3DFORMAT d3d9_format = D3DFMT_UNKNOWN;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels;
    std::uint64_t serial = 0;
    bool rendered_views_valid = false;
    EyeView rendered_eyes[kEyeCount] = {};

    ComPtr<ID3D11Device> d3d11_device;
    ComPtr<ID3D11Texture2D> upload_texture;
    ComPtr<ID3D11ShaderResourceView> upload_view;
    std::uint64_t uploaded_serial = 0;

    void Log(const LogLevel level, const char* format, ...) const
    {
        if (host.log == nullptr)
        {
            return;
        }
        char message[1024] = {};
        va_list arguments;
        va_start(arguments, format);
        std::vsnprintf(message, sizeof(message), format, arguments);
        va_end(arguments);
        message[sizeof(message) - 1] = '\0';
        host.log(host.user_data, level, message);
    }

    void ReleaseD3D9Locked(const bool discard_captured_frame)
    {
        resolve_surface.Reset();
        readback_surface.Reset();
        d3d9_device.Reset();
        if (discard_captured_frame)
        {
            d3d9_format = D3DFMT_UNKNOWN;
            width = 0;
            height = 0;
            pixels.clear();
            serial = 0;
            rendered_views_valid = false;
            for (EyeView& eye : rendered_eyes)
            {
                eye = {};
            }
        }
    }

    void ReleaseD3D11Locked()
    {
        upload_view.Reset();
        upload_texture.Reset();
        d3d11_device.Reset();
        uploaded_serial = 0;
    }

    D3D9CpuCaptureResult CaptureFailureLocked(
        const char* operation,
        const HRESULT result)
    {
        if (IsDeviceLostResult(result))
        {
            // Remain reset-safe: once the legacy device reports loss, drop
            // all D3D9-owned references and stop issuing synchronous readbacks
            // until a short recovery interval has elapsed. Retaining the last
            // CPU/D3D11 frame is safe because neither resource belongs to the
            // reset D3D9 pool.
            ReleaseD3D9Locked(false);
            device_lost_retry_after =
                GetTickCount64() + kDeviceLostRetryMilliseconds;
            if (!logged_device_lost_pause)
            {
                Log(LogLevel::Warning,
                    "D3D9 CPU capture paused for device recovery after %s: 0x%08lx",
                    operation,
                    result);
                logged_device_lost_pause = true;
            }
            return D3D9CpuCaptureResult::device_lost;
        }

        if (!logged_first_failure)
        {
            Log(LogLevel::Error,
                "%s (CPU capture) failed: 0x%08lx",
                operation,
                result);
            logged_first_failure = true;
        }
        return D3D9CpuCaptureResult::unavailable;
    }

    bool EnsureD3D9ResourcesLocked(
        IDirect3DDevice9* device,
        const D3DSURFACE_DESC& description,
        HRESULT* const failure_result)
    {
        if (failure_result != nullptr)
        {
            *failure_result = S_OK;
        }
        const bool matches =
            d3d9_device.Get() == device &&
            readback_surface != nullptr &&
            width == description.Width &&
            height == description.Height &&
            d3d9_format == description.Format &&
            ((description.MultiSampleType == D3DMULTISAMPLE_NONE) ||
             resolve_surface != nullptr);
        if (matches)
        {
            return true;
        }

        ReleaseD3D9Locked(false);
        if (description.Format != D3DFMT_A8R8G8B8 &&
            description.Format != D3DFMT_X8R8G8B8)
        {
            Log(LogLevel::Error,
                "CPU capture supports A8R8G8B8/X8R8G8B8, not D3DFORMAT %u",
                static_cast<unsigned>(description.Format));
            if (failure_result != nullptr)
            {
                *failure_result = D3DERR_NOTAVAILABLE;
            }
            return false;
        }

        HRESULT hr = device->CreateOffscreenPlainSurface(
            description.Width,
            description.Height,
            description.Format,
            D3DPOOL_SYSTEMMEM,
            readback_surface.GetAddressOf(),
            nullptr);
        if (FAILED(hr))
        {
            if (failure_result != nullptr)
            {
                *failure_result = hr;
            }
            return false;
        }

        if (description.MultiSampleType != D3DMULTISAMPLE_NONE)
        {
            hr = device->CreateRenderTarget(
                description.Width,
                description.Height,
                description.Format,
                D3DMULTISAMPLE_NONE,
                0,
                FALSE,
                resolve_surface.GetAddressOf(),
                nullptr);
            if (FAILED(hr))
            {
                if (failure_result != nullptr)
                {
                    *failure_result = hr;
                }
                ReleaseD3D9Locked(false);
                return false;
            }
        }

        d3d9_device = device;
        d3d9_format = description.Format;
        width = description.Width;
        height = description.Height;
        pixels.resize(static_cast<std::size_t>(width) * height * 4u);
        return true;
    }

    bool EnsureD3D11ResourcesLocked(ID3D11Device* device)
    {
        if (d3d11_device.Get() == device && upload_texture != nullptr &&
            upload_view != nullptr)
        {
            D3D11_TEXTURE2D_DESC existing = {};
            upload_texture->GetDesc(&existing);
            if (existing.Width == width && existing.Height == height &&
                existing.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS)
            {
                return true;
            }
        }

        ReleaseD3D11Locked();
        D3D11_TEXTURE2D_DESC description = {};
        description.Width = width;
        description.Height = height;
        description.MipLevels = 1;
        description.ArraySize = 1;
        // Store the captured BGRA bytes in a typeless resource so the SRV can
        // apply sRGB decoding directly when the compositor samples it.
        description.Format = DXGI_FORMAT_B8G8R8A8_TYPELESS;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr = device->CreateTexture2D(
            &description, nullptr, upload_texture.GetAddressOf());
        if (FAILED(hr))
        {
            Log(LogLevel::Error,
                "CreateTexture2D(CPU upload) failed: 0x%08lx", hr);
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC view_description = {};
        view_description.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        view_description.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        view_description.Texture2D.MostDetailedMip = 0;
        view_description.Texture2D.MipLevels = 1;
        hr = device->CreateShaderResourceView(
            upload_texture.Get(),
            &view_description,
            upload_view.GetAddressOf());
        if (FAILED(hr))
        {
            Log(LogLevel::Error,
                "CreateShaderResourceView(CPU upload) failed: 0x%08lx", hr);
            upload_texture.Reset();
            return false;
        }
        d3d11_device = device;
        return true;
    }
};

D3D9CpuCapture::D3D9CpuCapture() : impl_(std::make_unique<Impl>()) {}

D3D9CpuCapture::~D3D9CpuCapture()
{
    Shutdown();
}

bool D3D9CpuCapture::Initialize(const HostCallbacks& host)
{
    Shutdown();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->host = host;
    impl_->initialized = true;
    return true;
}

void D3D9CpuCapture::Shutdown()
{
    if (!impl_)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->ReleaseD3D11Locked();
    impl_->ReleaseD3D9Locked(true);
    impl_->device_lost_retry_after = 0;
    impl_->logged_device_lost_pause = false;
    impl_->logged_first_failure = false;
    impl_->initialized = false;
}

D3D9CpuCaptureResult D3D9CpuCapture::CaptureBackBuffer(
    IDirect3DDevice9* device,
    const FrameState* rendered_frame)
{
    if (device == nullptr)
    {
        return D3D9CpuCaptureResult::unavailable;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->initialized)
    {
        return D3D9CpuCaptureResult::unavailable;
    }

    const ULONGLONG now = GetTickCount64();
    if (now < impl_->device_lost_retry_after)
    {
        return D3D9CpuCaptureResult::device_lost;
    }

    HRESULT hr = device->TestCooperativeLevel();
    if (FAILED(hr))
    {
        return impl_->CaptureFailureLocked("TestCooperativeLevel", hr);
    }

    ComPtr<IDirect3DSurface9> back_buffer;
    hr = device->GetBackBuffer(
        0, 0, D3DBACKBUFFER_TYPE_MONO, back_buffer.GetAddressOf());
    if (FAILED(hr))
    {
        return impl_->CaptureFailureLocked("GetBackBuffer", hr);
    }
    D3DSURFACE_DESC description = {};
    hr = back_buffer->GetDesc(&description);
    if (FAILED(hr))
    {
        return impl_->CaptureFailureLocked("GetDesc", hr);
    }
    if (description.Width < 2 || description.Height == 0)
    {
        return D3D9CpuCaptureResult::unavailable;
    }

    HRESULT resource_result = S_OK;
    if (!impl_->EnsureD3D9ResourcesLocked(
            device, description, &resource_result))
    {
        if (resource_result != D3DERR_NOTAVAILABLE)
        {
            return impl_->CaptureFailureLocked(
                "Create D3D9 readback resources",
                resource_result);
        }
        return D3D9CpuCaptureResult::unavailable;
    }

    IDirect3DSurface9* source = back_buffer.Get();
    if (description.MultiSampleType != D3DMULTISAMPLE_NONE)
    {
        hr = device->StretchRect(
            back_buffer.Get(),
            nullptr,
            impl_->resolve_surface.Get(),
            nullptr,
            D3DTEXF_NONE);
        if (FAILED(hr))
        {
            return impl_->CaptureFailureLocked("StretchRect", hr);
        }
        source = impl_->resolve_surface.Get();
    }

    hr = device->GetRenderTargetData(source, impl_->readback_surface.Get());
    if (FAILED(hr))
    {
        return impl_->CaptureFailureLocked("GetRenderTargetData", hr);
    }

    D3DLOCKED_RECT locked = {};
    hr = impl_->readback_surface->LockRect(
        &locked, nullptr, D3DLOCK_READONLY | D3DLOCK_NOSYSLOCK);
    if (FAILED(hr) || locked.pBits == nullptr)
    {
        return impl_->CaptureFailureLocked(
            "LockRect",
            FAILED(hr) ? hr : E_FAIL);
    }

    const std::size_t row_bytes = static_cast<std::size_t>(impl_->width) * 4u;
    if (locked.Pitch <= 0 ||
        static_cast<std::size_t>(locked.Pitch) < row_bytes)
    {
        const HRESULT unlock_result = impl_->readback_surface->UnlockRect();
        return impl_->CaptureFailureLocked(
            "LockRect pitch",
            FAILED(unlock_result) ? unlock_result : E_FAIL);
    }
    const auto* source_row = static_cast<const std::uint8_t*>(locked.pBits);
    auto* destination_row = impl_->pixels.data();
    for (std::uint32_t row = 0; row < impl_->height; ++row)
    {
        std::memcpy(destination_row, source_row, row_bytes);
        source_row += locked.Pitch;
        destination_row += row_bytes;
    }
    hr = impl_->readback_surface->UnlockRect();
    if (FAILED(hr))
    {
        return impl_->CaptureFailureLocked("UnlockRect", hr);
    }

    if (description.Format == D3DFMT_X8R8G8B8)
    {
        for (std::size_t alpha = 3; alpha < impl_->pixels.size(); alpha += 4)
        {
            impl_->pixels[alpha] = 0xff;
        }
    }
    ++impl_->serial;
    impl_->rendered_views_valid =
        rendered_frame != nullptr && rendered_frame->views_valid;
    if (impl_->rendered_views_valid)
    {
        for (std::uint32_t eye = 0; eye < kEyeCount; ++eye)
        {
            impl_->rendered_eyes[eye] = rendered_frame->eyes[eye];
        }
    }

    if (!impl_->logged_cost_warning)
    {
        const double mebibytes =
            static_cast<double>(impl_->pixels.size()) / (1024.0 * 1024.0);
        impl_->Log(
            LogLevel::Warning,
            "CPU D3D9 capture fallback active (%ux%u packed source, %.1f MiB synchronous readback per frame)",
            impl_->width,
            impl_->height,
            mebibytes);
        impl_->logged_cost_warning = true;
    }
    if (impl_->logged_device_lost_pause)
    {
        impl_->Log(LogLevel::Info,
                   "D3D9 CPU capture resumed after cooperative device recovery");
    }
    impl_->device_lost_retry_after = 0;
    impl_->logged_device_lost_pause = false;
    impl_->logged_first_failure = false;
    return D3D9CpuCaptureResult::captured;
}

bool D3D9CpuCapture::UploadLatest(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    D3D11SourceFrame* frame)
{
    if (device == nullptr || context == nullptr || frame == nullptr ||
        !impl_->initialized)
    {
        return false;
    }
    *frame = {};
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->serial == 0 || impl_->pixels.empty() ||
        !impl_->EnsureD3D11ResourcesLocked(device))
    {
        return false;
    }

    if (impl_->uploaded_serial != impl_->serial)
    {
        context->UpdateSubresource(
            impl_->upload_texture.Get(),
            0,
            nullptr,
            impl_->pixels.data(),
            impl_->width * 4u,
            0);
        impl_->uploaded_serial = impl_->serial;
    }

    frame->texture = impl_->upload_texture.Get();
    frame->view = impl_->upload_view.Get();
    frame->width = impl_->width;
    frame->height = impl_->height;
    frame->serial = impl_->serial;
    // The explicit UNORM_SRGB view already performs the decode, so the
    // compositor can sample this resource without making a decode copy.
    frame->pixels_are_srgb_encoded = false;
    frame->rendered_views_valid = impl_->rendered_views_valid;
    if (impl_->rendered_views_valid)
    {
        for (std::uint32_t eye = 0; eye < kEyeCount; ++eye)
        {
            frame->rendered_eyes[eye] = impl_->rendered_eyes[eye];
        }
    }
    return true;
}

void D3D9CpuCapture::InvalidateD3D9()
{
    if (!impl_)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->ReleaseD3D9Locked(false);
}

void D3D9CpuCapture::NotifyExternalDeviceLoss()
{
    if (!impl_)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->initialized)
    {
        return;
    }

    impl_->ReleaseD3D9Locked(false);
    const ULONGLONG retry_after =
        GetTickCount64() + kExternalDeviceLostRetryMilliseconds;
    if (retry_after > impl_->device_lost_retry_after)
    {
        impl_->device_lost_retry_after = retry_after;
    }
    if (!impl_->logged_device_lost_pause)
    {
        impl_->Log(
            LogLevel::Warning,
            "D3D9 CPU capture paused for 5 seconds after external device loss");
        impl_->logged_device_lost_pause = true;
    }
}

void D3D9CpuCapture::Invalidate()
{
    if (!impl_)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->ReleaseD3D11Locked();
    impl_->ReleaseD3D9Locked(true);
}

std::uint64_t D3D9CpuCapture::latest_serial() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->serial;
}

} // namespace wawvr::xr
