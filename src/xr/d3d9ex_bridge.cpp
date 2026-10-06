// SPDX-License-Identifier: GPL-3.0-only
// WorldAtWarVR D3D9Ex/D3D11 bridge. Comparative research history and the
// Public API usage follows the pinned OpenXR and Direct3D SDK contracts.

#include "d3d9ex_bridge.h"
#include "d3d9ex_bridge_lifecycle.hpp"

#if !defined(_WIN32)
#error WorldAtWarVR's D3D9Ex bridge currently supports Windows only.
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
#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <vector>

namespace wawvr::xr
{

using Microsoft::WRL::ComPtr;

struct D3D9ExSharedTextureBridge::Impl
{
    using SlotState = detail::SharedBridgeSlotState;

    struct Slot
    {
        ComPtr<IDirect3DTexture9> producer_texture;
        ComPtr<IDirect3DSurface9> producer_surface;
        ComPtr<IDirect3DQuery9> producer_fence;
        HANDLE shared_handle = nullptr;

        ComPtr<ID3D11Texture2D> consumer_texture;
        ComPtr<ID3D11ShaderResourceView> consumer_view;
        ComPtr<ID3D11Query> consumer_fence;

        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t generation = 0;
        std::uint64_t serial = 0;
        bool rendered_views_valid = false;
        EyeView rendered_eyes[kEyeCount] = {};
        SlotState state = SlotState::Free;
    };

    D3D9ExBridgeConfig config = {};
    HostCallbacks host = {};
    mutable std::mutex mutex;
    std::vector<Slot> slots;
    ComPtr<IDirect3DDevice9> producer_device;
    ComPtr<ID3D11Device> consumer_device;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t generation_counter = 0;
    std::uint64_t serial_counter = 0;
    std::uint64_t last_acquired_serial = 0;
    bool initialized = false;
    bool active = false;
    bool unavailable_until_invalidate = false;
    bool logged_backpressure = false;
    bool logged_consumer_recycling = false;
    bool logged_producer_wait = false;
    bool logged_producer_wait_timeout = false;
    bool producer_fence_failed = false;
    bool consumer_fence_failed = false;
    detail::LostDeviceResetPhase lost_device_reset_phase =
        detail::LostDeviceResetPhase::idle;

    void DisableUntilInvalidateLocked()
    {
        active = false;
        unavailable_until_invalidate = true;
    }

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

    void ReleaseResourcesLocked()
    {
        slots.clear();
        producer_device.Reset();
        consumer_device.Reset();
        width = 0;
        height = 0;
        active = false;
        logged_backpressure = false;
        logged_consumer_recycling = false;
        logged_producer_wait = false;
        logged_producer_wait_timeout = false;
        last_acquired_serial = 0;
        producer_fence_failed = false;
        consumer_fence_failed = false;
        lost_device_reset_phase = detail::LostDeviceResetPhase::idle;
    }

    void FailResourceCreationLocked()
    {
        // No producer copy has been submitted while a generation is being
        // constructed, so its partial resources can be released immediately.
        // Keep the bridge unavailable afterward so the render loop cannot
        // repeat the same allocations and error log every frame.
        ReleaseResourcesLocked();
        DisableUntilInvalidateLocked();
    }

    bool HasResourceGenerationLocked() const
    {
        return detail::should_quarantine_unretired_generation(
            !slots.empty(), producer_device != nullptr,
            consumer_device != nullptr);
    }

    bool ValidateConsumerContextLocked(
        ID3D11Device* expected_device,
        ID3D11DeviceContext* context,
        const char* operation)
    {
        const auto bound_device = reinterpret_cast<std::uintptr_t>(
            consumer_device.Get());
        const auto expected = reinterpret_cast<std::uintptr_t>(
            expected_device);
        if (expected == 0 || context == nullptr ||
            (bound_device != 0 && bound_device != expected))
        {
            Log(LogLevel::Error,
                "%s rejected a mismatched D3D11 consumer device/context",
                operation);
            DisableUntilInvalidateLocked();
            return false;
        }

        ComPtr<ID3D11Device> context_device;
        context->GetDevice(context_device.GetAddressOf());
        if (!detail::consumer_context_matches(
                bound_device, expected,
                reinterpret_cast<std::uintptr_t>(context_device.Get())))
        {
            Log(LogLevel::Error,
                "%s rejected a mismatched D3D11 consumer device/context",
                operation);
            DisableUntilInvalidateLocked();
            return false;
        }
        return true;
    }

    bool AllSlotsFreeLocked() const
    {
        return std::all_of(
            slots.begin(),
            slots.end(),
            [](const Slot& slot)
            {
                return slot.state == SlotState::Free;
            });
    }

    bool PollProducerLocked(const bool flush)
    {
        if (producer_fence_failed)
        {
            return false;
        }
        const DWORD flags = flush ? D3DGETDATA_FLUSH : 0;
        for (Slot& slot : slots)
        {
            if (!detail::producer_fence_pending(slot.state) ||
                slot.producer_fence == nullptr)
            {
                continue;
            }
            const HRESULT hr = slot.producer_fence->GetData(nullptr, 0, flags);
            if (hr == S_OK)
            {
                slot.state =
                    detail::producer_fence_completion_state(slot.state);
                if (slot.state == SlotState::Free)
                {
                    logged_backpressure = false;
                }
            }
            else if (FAILED(hr))
            {
                Log(LogLevel::Error,
                    "D3D9 producer fence failed: 0x%08lx", hr);
                // A failed query does not prove that the shared-surface write
                // retired. Preserve ownership and require explicit recovery;
                // reusing or releasing this slot here could race the producer.
                producer_fence_failed = true;
                DisableUntilInvalidateLocked();
                return false;
            }
        }
        return true;
    }

    bool PollConsumerLocked(ID3D11DeviceContext* context)
    {
        if (consumer_fence_failed)
        {
            return false;
        }
        if (context == nullptr)
        {
            return true;
        }
        HRESULT failed_fence_result = S_OK;
        const auto poll_fence =
            [context, &failed_fence_result](Slot& slot) noexcept
            -> detail::ConsumerFencePollDisposition {
            if (slot.consumer_fence == nullptr)
            {
                return detail::ConsumerFencePollDisposition::pending;
            }
            BOOL complete = FALSE;
            const HRESULT hr = context->GetData(
                slot.consumer_fence.Get(),
                &complete,
                sizeof(complete),
                D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (hr == S_OK && complete)
            {
                return detail::ConsumerFencePollDisposition::complete;
            }
            if (FAILED(hr))
            {
                failed_fence_result = hr;
                return detail::ConsumerFencePollDisposition::failed;
            }
            return detail::ConsumerFencePollDisposition::pending;
        };
        const detail::ConsumerFenceServiceResult service =
            detail::service_consumer_fences(slots, poll_fence);
        if (!service.ok)
        {
            Log(LogLevel::Error,
                "D3D11 consumer fence failed: 0x%08lx",
                failed_fence_result);
            // The query belongs to the current D3D11 device generation. Keep
            // the slot pending (and therefore non-reusable), but stop
            // accepting producer work until explicit invalidation can retire
            // or safely reject the generation.
            consumer_fence_failed = true;
            DisableUntilInvalidateLocked();
            return false;
        }
        return true;
    }

    bool CreateSlotsLocked(
        IDirect3DDevice9* device,
        const D3DSURFACE_DESC& back_buffer_description)
    {
        const detail::ProducerDeviceDisposition device_disposition =
            detail::classify_producer_device(
                reinterpret_cast<std::uintptr_t>(producer_device.Get()),
                reinterpret_cast<std::uintptr_t>(device));
        if (device_disposition ==
            detail::ProducerDeviceDisposition::changed)
        {
            Log(LogLevel::Error,
                "D3D9 producer device changed before explicit bridge invalidation");
            DisableUntilInvalidateLocked();
            return false;
        }
        if (device_disposition ==
            detail::ProducerDeviceDisposition::invalid)
        {
            return false;
        }

        ComPtr<IDirect3DDevice9Ex> device_ex;
        const HRESULT query_result = device->QueryInterface(
            IID_PPV_ARGS(device_ex.GetAddressOf()));
        if (FAILED(query_result) || device_ex == nullptr)
        {
            Log(LogLevel::Warning,
                "Game renderer is not using IDirect3DDevice9Ex; GPU bridge disabled");
            DisableUntilInvalidateLocked();
            return false;
        }

        if (!AllSlotsFreeLocked())
        {
            Log(LogLevel::Warning,
                "D3D9Ex resource recreation deferred until explicit invalidation retires the current shared-frame generation");
            DisableUntilInvalidateLocked();
            return false;
        }
        ReleaseResourcesLocked();
        producer_device = device;
        width = back_buffer_description.Width;
        height = back_buffer_description.Height;
        try
        {
            slots.resize(config.ring_size);
        }
        catch (...)
        {
            Log(LogLevel::Error,
                "Could not allocate D3D9Ex shared bridge slot metadata");
            FailResourceCreationLocked();
            return false;
        }

        for (Slot& slot : slots)
        {
            slot.generation = ++generation_counter;
            slot.width = width;
            slot.height = height;
            HRESULT hr = device_ex->CreateTexture(
                width,
                height,
                1,
                D3DUSAGE_RENDERTARGET,
                D3DFMT_A8R8G8B8,
                D3DPOOL_DEFAULT,
                slot.producer_texture.GetAddressOf(),
                &slot.shared_handle);
            if (FAILED(hr) || slot.shared_handle == nullptr)
            {
                Log(LogLevel::Error,
                    "CreateTexture(D3D9Ex shared) failed: 0x%08lx", hr);
                FailResourceCreationLocked();
                return false;
            }
            hr = slot.producer_texture->GetSurfaceLevel(
                0, slot.producer_surface.GetAddressOf());
            if (FAILED(hr))
            {
                Log(LogLevel::Error,
                    "GetSurfaceLevel(D3D9Ex shared) failed: 0x%08lx", hr);
                FailResourceCreationLocked();
                return false;
            }
            hr = device->CreateQuery(
                D3DQUERYTYPE_EVENT, slot.producer_fence.GetAddressOf());
            if (FAILED(hr))
            {
                Log(LogLevel::Error,
                    "CreateQuery(D3D9 producer) failed: 0x%08lx", hr);
                FailResourceCreationLocked();
                return false;
            }
        }

        active = true;
        Log(LogLevel::Info,
            "D3D9Ex shared bridge created %u fenced %ux%u textures",
            static_cast<unsigned>(slots.size()), width, height);
        return true;
    }

    bool EnsureConsumerResourcesLocked(ID3D11Device* device, Slot& slot)
    {
        if (consumer_device != nullptr && consumer_device.Get() != device)
        {
            Log(LogLevel::Error,
                "D3D11 consumer device changed while the bridge was active");
            DisableUntilInvalidateLocked();
            return false;
        }
        if (consumer_device == nullptr)
        {
            D3DDEVICE_CREATION_PARAMETERS creation = {};
            ComPtr<IDirect3D9> d3d9;
            ComPtr<IDirect3D9Ex> d3d9_ex;
            LUID d3d9_luid = {};
            ComPtr<IDXGIDevice> dxgi_device;
            ComPtr<IDXGIAdapter> dxgi_adapter;
            DXGI_ADAPTER_DESC dxgi_description = {};
            const bool identified =
                producer_device != nullptr &&
                SUCCEEDED(producer_device->GetCreationParameters(&creation)) &&
                SUCCEEDED(producer_device->GetDirect3D(d3d9.GetAddressOf())) &&
                SUCCEEDED(d3d9.As(&d3d9_ex)) &&
                SUCCEEDED(d3d9_ex->GetAdapterLUID(
                    creation.AdapterOrdinal, &d3d9_luid)) &&
                SUCCEEDED(device->QueryInterface(
                    IID_PPV_ARGS(dxgi_device.GetAddressOf()))) &&
                SUCCEEDED(dxgi_device->GetAdapter(
                    dxgi_adapter.GetAddressOf())) &&
                SUCCEEDED(dxgi_adapter->GetDesc(&dxgi_description));
            if (!identified ||
                d3d9_luid.HighPart != dxgi_description.AdapterLuid.HighPart ||
                d3d9_luid.LowPart != dxgi_description.AdapterLuid.LowPart)
            {
                Log(LogLevel::Error,
                    "D3D9Ex and OpenXR D3D11 devices are on different or unidentified adapters; use CPU capture or recreate D3D9Ex on the OpenXR adapter");
                DisableUntilInvalidateLocked();
                return false;
            }
            consumer_device = device;
        }

        if (slot.consumer_texture == nullptr)
        {
            HRESULT hr = device->OpenSharedResource(
                slot.shared_handle,
                IID_PPV_ARGS(slot.consumer_texture.GetAddressOf()));
            if (FAILED(hr))
            {
                Log(LogLevel::Error,
                    "OpenSharedResource(D3D9Ex frame) failed: 0x%08lx", hr);
                DisableUntilInvalidateLocked();
                return false;
            }

            D3D11_TEXTURE2D_DESC description = {};
            slot.consumer_texture->GetDesc(&description);
            const detail::SharedTextureDescriptionFacts facts{
                description.Width,
                description.Height,
                description.MipLevels,
                description.ArraySize,
                static_cast<std::uint32_t>(description.Format),
                description.SampleDesc.Count,
                description.SampleDesc.Quality,
                static_cast<std::uint32_t>(description.Usage),
                description.CPUAccessFlags,
                description.BindFlags,
                description.MiscFlags,
            };
            constexpr std::uint32_t required_bind_flags =
                D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            if (!detail::valid_shared_texture_description(
                    facts, slot.width, slot.height,
                    static_cast<std::uint32_t>(
                        DXGI_FORMAT_B8G8R8A8_UNORM),
                    static_cast<std::uint32_t>(D3D11_USAGE_DEFAULT),
                    required_bind_flags,
                    D3D11_RESOURCE_MISC_SHARED))
            {
                Log(LogLevel::Error,
                    "D3D9Ex shared texture has an unexpected D3D11 description");
                slot.consumer_texture.Reset();
                DisableUntilInvalidateLocked();
                return false;
            }
            hr = device->CreateShaderResourceView(
                slot.consumer_texture.Get(),
                nullptr,
                slot.consumer_view.GetAddressOf());
            if (FAILED(hr))
            {
                Log(LogLevel::Error,
                    "CreateShaderResourceView(shared frame) failed: 0x%08lx",
                    hr);
                slot.consumer_texture.Reset();
                DisableUntilInvalidateLocked();
                return false;
            }
        }

        if (slot.consumer_fence == nullptr)
        {
            D3D11_QUERY_DESC query = {};
            query.Query = D3D11_QUERY_EVENT;
            const HRESULT hr = device->CreateQuery(
                &query, slot.consumer_fence.GetAddressOf());
            if (FAILED(hr))
            {
                Log(LogLevel::Error,
                    "CreateQuery(D3D11 consumer) failed: 0x%08lx", hr);
                DisableUntilInvalidateLocked();
                return false;
            }
        }
        return true;
    }
};

D3D9ExSharedTextureBridge::D3D9ExSharedTextureBridge()
    : impl_(std::make_unique<Impl>())
{
}

D3D9ExSharedTextureBridge::~D3D9ExSharedTextureBridge()
{
    Shutdown();
    if (!impl_)
    {
        return;
    }

    bool quarantine = false;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        quarantine = impl_->HasResourceGenerationLocked();
    }
    if (quarantine)
    {
        impl_->Log(LogLevel::Error,
            "D3D9Ex bridge quarantined an unretired generation for process lifetime");
        // Destruction of Impl would Release COM resources whose producer or
        // consumer work was never proven retired. A bounded process-lifetime
        // leak is safer than a use-after-free on either GPU command stream.
        static_cast<void>(impl_.release());
    }
}

bool D3D9ExSharedTextureBridge::Initialize(
    const D3D9ExBridgeConfig& config,
    const HostCallbacks& host)
{
    Shutdown();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->initialized || !impl_->slots.empty())
    {
        // Shutdown deliberately retains an unretired generation. Do not hide
        // that ownership failure by overwriting the bridge configuration.
        return false;
    }
    impl_->config = config;
    impl_->config.ring_size = std::clamp(config.ring_size, 2u, 8u);
    impl_->config.producer_wait_budget_microseconds = std::clamp(
        config.producer_wait_budget_microseconds, 0u, 10000u);
    impl_->host = host;
    impl_->initialized = true;
    return true;
}

void D3D9ExSharedTextureBridge::Shutdown()
{
    if (!impl_)
    {
        return;
    }
    // A context-free shutdown can retire producer work and any generation
    // with no outstanding consumer. ConsumerPending/Acquired ownership must
    // first be retired explicitly with Invalidate(context).
    if (!Invalidate())
    {
        impl_->Log(LogLevel::Error,
            "D3D9Ex bridge shutdown retained an unretired shared-frame generation");
        return;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->unavailable_until_invalidate = false;
    impl_->initialized = false;
}

bool D3D9ExSharedTextureBridge::CaptureBackBuffer(
    IDirect3DDevice9* device,
    const FrameState* rendered_frame,
    std::uint64_t* captured_serial)
{
    if (captured_serial != nullptr)
    {
        *captured_serial = 0;
    }
    if (device == nullptr || !impl_)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    const detail::CaptureEntryDisposition entry =
        detail::classify_capture_entry(
            impl_->initialized,
            impl_->unavailable_until_invalidate,
            impl_->active,
            reinterpret_cast<std::uintptr_t>(
                impl_->producer_device.Get()),
            reinterpret_cast<std::uintptr_t>(device));
    if (entry == detail::CaptureEntryDisposition::reject_without_d3d)
    {
        return false;
    }
    if (entry ==
        detail::CaptureEntryDisposition::reject_changed_device_without_d3d)
    {
        impl_->Log(LogLevel::Error,
            "D3D9 producer device changed before explicit bridge invalidation");
        impl_->DisableUntilInvalidateLocked();
        return false;
    }

    // All unavailable/device-identity decisions above are deliberately made
    // before this first D3D call. A disabled bridge therefore cannot create an
    // allocation or logging retry storm in the render loop.
    ComPtr<IDirect3DSurface9> back_buffer;
    HRESULT hr = device->GetBackBuffer(
        0, 0, D3DBACKBUFFER_TYPE_MONO, back_buffer.GetAddressOf());
    if (FAILED(hr))
    {
        impl_->Log(LogLevel::Error, "GetBackBuffer failed: 0x%08lx", hr);
        impl_->DisableUntilInvalidateLocked();
        return false;
    }
    D3DSURFACE_DESC description = {};
    hr = back_buffer->GetDesc(&description);
    if (FAILED(hr) || description.Width < 2 || description.Height == 0)
    {
        impl_->Log(LogLevel::Error,
            "D3D9 back buffer has no valid capture description: 0x%08lx",
            hr);
        impl_->DisableUntilInvalidateLocked();
        return false;
    }

    if (!impl_->active ||
        impl_->producer_device.Get() != device ||
        impl_->width != description.Width ||
        impl_->height != description.Height)
    {
        if (!impl_->CreateSlotsLocked(device, description))
        {
            return false;
        }
    }

    if (!impl_->PollProducerLocked(false))
    {
        return false;
    }
    Impl::Slot* destination = nullptr;
    for (Impl::Slot& slot : impl_->slots)
    {
        if (slot.state == Impl::SlotState::Free)
        {
            destination = &slot;
            break;
        }
    }
    if (destination == nullptr)
    {
        if (!impl_->logged_backpressure)
        {
            impl_->Log(LogLevel::Warning,
                       "D3D9Ex capture dropped a frame while all slots were busy");
            impl_->logged_backpressure = true;
        }
        return false;
    }

    hr = device->StretchRect(
        back_buffer.Get(),
        nullptr,
        destination->producer_surface.Get(),
        nullptr,
        D3DTEXF_NONE);
    const bool producer_copy_submitted = SUCCEEDED(hr);
    if (producer_copy_submitted)
    {
        hr = destination->producer_fence->Issue(D3DISSUE_END);
    }
    if (FAILED(hr))
    {
        impl_->Log(LogLevel::Error,
                   "D3D9Ex back-buffer resolve failed: 0x%08lx", hr);
        // If StretchRect succeeded but the following fence could not be
        // issued, the producer write has no safe retirement signal. Preserve
        // the slot and fail closed until bridge teardown/recovery.
        if (producer_copy_submitted)
        {
            destination->state = Impl::SlotState::Producing;
            impl_->producer_fence_failed = true;
        }
        impl_->DisableUntilInvalidateLocked();
        return false;
    }

    // Submit the resolve/fence immediately, but never wait here.  The native
    // desktop Present that follows this hook can overlap its own work with the
    // shared-surface copy.  By the post-Com_Frame consumer boundary the exact
    // serial is commonly complete, avoiding the render-thread polling wait
    // that can push a 90 Hz runtime into half-rate reprojection.
    const HRESULT producer_submit_result =
        destination->producer_fence->GetData(
            nullptr, 0, D3DGETDATA_FLUSH);
    if (FAILED(producer_submit_result))
    {
        impl_->Log(LogLevel::Error,
            "D3D9 producer fence submission failed: 0x%08lx",
            producer_submit_result);
        destination->state = Impl::SlotState::Producing;
        impl_->producer_fence_failed = true;
        impl_->DisableUntilInvalidateLocked();
        return false;
    }

    destination->serial = ++impl_->serial_counter;
    destination->rendered_views_valid =
        rendered_frame != nullptr && rendered_frame->views_valid;
    if (destination->rendered_views_valid)
    {
        for (std::uint32_t eye = 0; eye < kEyeCount; ++eye)
        {
            destination->rendered_eyes[eye] = rendered_frame->eyes[eye];
        }
    }
    destination->state = producer_submit_result == S_OK
        ? Impl::SlotState::Ready
        : Impl::SlotState::Producing;
    if (captured_serial != nullptr)
    {
        *captured_serial = destination->serial;
    }
    return true;
}

bool D3D9ExSharedTextureBridge::RetireCompletedFrames(
    ID3D11Device* device,
    ID3D11DeviceContext* context)
{
    if (device == nullptr || context == nullptr || !impl_)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->initialized)
    {
        return false;
    }
    if (!impl_->active)
    {
        // No shared generation exists yet. The first producer capture is
        // responsible for constructing it.
        return true;
    }
    if (!impl_->ValidateConsumerContextLocked(
            device, context, "D3D9Ex bridge pre-capture retirement"))
    {
        return false;
    }

    const auto consumer_pending_before = std::count_if(
        impl_->slots.begin(), impl_->slots.end(),
        [](const Impl::Slot& slot)
        {
            return slot.state == Impl::SlotState::ConsumerPending;
        });
    if (!impl_->PollConsumerLocked(context) ||
        !impl_->PollProducerLocked(false))
    {
        return false;
    }
    const auto consumer_pending_after = std::count_if(
        impl_->slots.begin(), impl_->slots.end(),
        [](const Impl::Slot& slot)
        {
            return slot.state == Impl::SlotState::ConsumerPending;
        });
    if (consumer_pending_after < consumer_pending_before)
    {
        impl_->logged_backpressure = false;
        if (!impl_->logged_consumer_recycling)
        {
            impl_->Log(LogLevel::Info,
                "D3D9Ex pre-capture consumer-fence service recycled %llu shared slot(s)",
                static_cast<unsigned long long>(
                    consumer_pending_before - consumer_pending_after));
            impl_->logged_consumer_recycling = true;
        }
    }
    return true;
}

bool D3D9ExSharedTextureBridge::AcquireSerial(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    const std::uint64_t expected_serial,
    SharedFrameToken* frame)
{
    if (device == nullptr || context == nullptr || frame == nullptr ||
        expected_serial == 0 || !impl_)
    {
        return false;
    }
    *frame = {};

    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->initialized || !impl_->active)
    {
        return false;
    }
    if (!impl_->ValidateConsumerContextLocked(
            device, context, "D3D9Ex bridge acquisition"))
    {
        return false;
    }
    if (!impl_->PollConsumerLocked(context))
    {
        return false;
    }

    const auto wait_started = std::chrono::steady_clock::now();
    const auto wait_deadline = wait_started + std::chrono::microseconds(
        impl_->config.producer_wait_budget_microseconds);
    bool waited_for_producer = false;
    for (;;)
    {
        if (!impl_->PollProducerLocked(true))
        {
            return false;
        }

        const auto exact = std::find_if(
            impl_->slots.begin(), impl_->slots.end(),
            [expected_serial](const Impl::Slot& slot)
            {
                return slot.serial == expected_serial;
            });
        if (exact == impl_->slots.end() ||
            exact->state != Impl::SlotState::Producing)
        {
            break;
        }
        if (impl_->config.producer_wait_budget_microseconds == 0 ||
            std::chrono::steady_clock::now() >= wait_deadline)
        {
            if (!impl_->logged_producer_wait_timeout)
            {
                if (impl_->config.producer_wait_budget_microseconds == 0)
                {
                    impl_->Log(LogLevel::Info,
                        "D3D9Ex exact-frame producer fence was still pending at the nonblocking poll (serial=%llu)",
                        static_cast<unsigned long long>(expected_serial));
                }
                else
                {
                    impl_->Log(LogLevel::Warning,
                        "D3D9Ex producer fence remained pending after the bounded %u us exact-frame wait (serial=%llu)",
                        impl_->config.producer_wait_budget_microseconds,
                        static_cast<unsigned long long>(expected_serial));
                }
                impl_->logged_producer_wait_timeout = true;
            }
            return false;
        }

        waited_for_producer = true;
        // D3DGETDATA_FLUSH submits outstanding producer work but does not wait
        // for it. Yield and poll again until the exact fence completes or the
        // hard deadline expires.
        Sleep(0);
    }

    if (waited_for_producer && !impl_->logged_producer_wait)
    {
        const auto waited_microseconds =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - wait_started).count();
        impl_->Log(LogLevel::Info,
            "D3D9Ex exact-frame producer fence completed after %lld us of bounded polling (serial=%llu)",
            static_cast<long long>(waited_microseconds),
            static_cast<unsigned long long>(expected_serial));
        impl_->logged_producer_wait = true;
    }

    Impl::Slot* expected = nullptr;
    for (Impl::Slot& slot : impl_->slots)
    {
        if (slot.state != Impl::SlotState::Ready)
        {
            continue;
        }
        if (slot.serial < expected_serial)
        {
            // Asking for expected_serial explicitly relinquishes any older
            // completed capture. This is not producer-side overwrite.
            slot.state = Impl::SlotState::Free;
        }
        else if (slot.serial == expected_serial)
        {
            expected = &slot;
        }
    }
    if (expected == nullptr ||
        expected_serial <= impl_->last_acquired_serial)
    {
        return false;
    }

    if (!impl_->EnsureConsumerResourcesLocked(device, *expected))
    {
        expected->state = Impl::SlotState::Free;
        return false;
    }

    expected->state = Impl::SlotState::Acquired;
    impl_->last_acquired_serial = expected->serial;
    frame->source.texture = expected->consumer_texture.Get();
    frame->source.view = expected->consumer_view.Get();
    frame->source.width = expected->width;
    frame->source.height = expected->height;
    frame->source.serial = expected->serial;
    frame->source.pixels_are_srgb_encoded = true;
    frame->source.rendered_views_valid = expected->rendered_views_valid;
    if (expected->rendered_views_valid)
    {
        for (std::uint32_t eye = 0; eye < kEyeCount; ++eye)
        {
            frame->source.rendered_eyes[eye] = expected->rendered_eyes[eye];
        }
    }
    frame->slot = static_cast<std::uint32_t>(expected - impl_->slots.data());
    frame->generation = expected->generation;
    return true;
}

bool D3D9ExSharedTextureBridge::TryAcquireLatestReady(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    const std::uint64_t last_consumed_serial,
    SharedFrameToken* frame)
{
    if (device == nullptr || context == nullptr || frame == nullptr || !impl_)
    {
        return false;
    }
    *frame = {};

    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->initialized || !impl_->active)
    {
        return false;
    }
    if (!impl_->ValidateConsumerContextLocked(
            device, context, "D3D9Ex bridge nonblocking acquisition"))
    {
        return false;
    }
    if (!impl_->PollConsumerLocked(context) ||
        !impl_->PollProducerLocked(true))
    {
        return false;
    }

    const std::uint64_t acquisition_floor =
        std::max(last_consumed_serial, impl_->last_acquired_serial);
    const detail::ReadySerialSelection selection =
        detail::select_latest_ready_serial(impl_->slots, acquisition_floor);
    if (!selection || selection.index >= impl_->slots.size())
    {
        return false;
    }

    Impl::Slot& selected = impl_->slots[selection.index];
    if (selected.state != Impl::SlotState::Ready ||
        selected.serial != selection.serial ||
        !impl_->EnsureConsumerResourcesLocked(device, selected))
    {
        if (selected.state == Impl::SlotState::Ready)
        {
            selected.state = Impl::SlotState::Free;
        }
        return false;
    }

    // Keep older completed frames available until the newest shared resource
    // has proved usable. Once validation succeeds, D3D11 will acquire the
    // newest serial and every older unacquired Ready image is obsolete.
    const std::size_t retired_ready =
        detail::retire_ready_serials_before(
            impl_->slots, selected.serial);
    if (retired_ready != 0)
    {
        impl_->logged_backpressure = false;
    }

    selected.state = Impl::SlotState::Acquired;
    impl_->last_acquired_serial = selected.serial;
    frame->source.texture = selected.consumer_texture.Get();
    frame->source.view = selected.consumer_view.Get();
    frame->source.width = selected.width;
    frame->source.height = selected.height;
    frame->source.serial = selected.serial;
    frame->source.pixels_are_srgb_encoded = true;
    frame->source.rendered_views_valid = selected.rendered_views_valid;
    if (selected.rendered_views_valid)
    {
        for (std::uint32_t eye = 0; eye < kEyeCount; ++eye)
        {
            frame->source.rendered_eyes[eye] = selected.rendered_eyes[eye];
        }
    }
    frame->slot = static_cast<std::uint32_t>(selection.index);
    frame->generation = selected.generation;
    return true;
}

bool D3D9ExSharedTextureBridge::AbandonSerial(
    const std::uint64_t expected_serial)
{
    if (!impl_ || expected_serial == 0)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->initialized)
    {
        return false;
    }

    const detail::AbandonSerialDisposition disposition =
        detail::abandon_exact_serial(impl_->slots, expected_serial);
    switch (disposition)
    {
    case detail::AbandonSerialDisposition::retired:
        impl_->logged_backpressure = false;
        return true;
    case detail::AbandonSerialDisposition::producer_retirement_pending:
        // Capture/Acquire will poll without blocking, while Invalidate performs
        // the bounded flushing wait during reset or shutdown. Until then
        // AbandonPending remains producer-owned and non-reusable.
        return true;
    case detail::AbandonSerialDisposition::consumer_owned:
    case detail::AbandonSerialDisposition::not_found:
        return false;
    }
    return false;
}

void D3D9ExSharedTextureBridge::Release(
    ID3D11DeviceContext* context,
    const SharedFrameToken& frame)
{
    if (context == nullptr || !impl_)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (frame.slot >= impl_->slots.size())
    {
        return;
    }
    Impl::Slot& slot = impl_->slots[frame.slot];
    if (slot.state != Impl::SlotState::Acquired ||
        slot.generation != frame.generation ||
        slot.serial != frame.source.serial)
    {
        return;
    }
    if (!impl_->ValidateConsumerContextLocked(
            impl_->consumer_device.Get(), context,
            "D3D9Ex bridge release"))
    {
        // Leave the slot acquired. Using a query from the retained consumer
        // device on another device's context would not prove retirement.
        return;
    }

    ID3D11ShaderResourceView* null_view = nullptr;
    context->PSSetShaderResources(0, 1, &null_view);
    context->End(slot.consumer_fence.Get());
    // Flush is necessary because D3D9 and D3D11 have no keyed-mutex contract.
    // It submits the consumer fence without synchronously waiting for it.
    context->Flush();
    slot.state = Impl::SlotState::ConsumerPending;
}

bool D3D9ExSharedTextureBridge::Invalidate(ID3D11DeviceContext* context)
{
    if (!impl_)
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!detail::d3d9_access_allowed(impl_->lost_device_reset_phase))
    {
        impl_->Log(LogLevel::Error,
            "Normal D3D9Ex invalidation is forbidden after lost-device preflight; a successful ResetEx must be finalized explicitly");
        return false;
    }
    if (context != nullptr && impl_->consumer_device != nullptr &&
        !impl_->ValidateConsumerContextLocked(
            impl_->consumer_device.Get(), context,
            "D3D9Ex bridge invalidation"))
    {
        return false;
    }
    for (const Impl::Slot& slot : impl_->slots)
    {
        if (slot.state == Impl::SlotState::Acquired)
        {
            impl_->Log(LogLevel::Error,
                       "Cannot invalidate D3D9Ex bridge while a frame is acquired");
            return false;
        }
    }

    bool producer_pending = std::any_of(
        impl_->slots.begin(),
        impl_->slots.end(),
        [](const Impl::Slot& slot)
        {
            return detail::producer_fence_pending(slot.state);
        });
    if (producer_pending)
    {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        do
        {
            if (!impl_->PollProducerLocked(true))
            {
                impl_->Log(LogLevel::Error,
                    "Could not retire D3D9 shared frames before invalidation");
                return false;
            }
            producer_pending = std::any_of(
                impl_->slots.begin(),
                impl_->slots.end(),
                [](const Impl::Slot& slot)
                {
                    return detail::producer_fence_pending(slot.state);
                });
            if (!producer_pending)
            {
                break;
            }
            Sleep(0);
        } while (std::chrono::steady_clock::now() < deadline);

        if (producer_pending)
        {
            impl_->Log(LogLevel::Error,
                "Timed out retiring D3D9 shared frames before invalidation");
            return false;
        }
    }

    bool consumer_pending = std::any_of(
        impl_->slots.begin(),
        impl_->slots.end(),
        [](const Impl::Slot& slot)
        {
            return slot.state == Impl::SlotState::ConsumerPending;
        });
    if (consumer_pending && context == nullptr)
    {
        impl_->Log(LogLevel::Error,
                   "D3D11 context is required to retire shared frames before reset");
        return false;
    }
    if (consumer_pending)
    {
        context->Flush();
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        do
        {
            if (!impl_->PollConsumerLocked(context))
            {
                impl_->Log(LogLevel::Error,
                    "Could not retire D3D11 shared frames before invalidation");
                return false;
            }
            consumer_pending = std::any_of(
                impl_->slots.begin(),
                impl_->slots.end(),
                [](const Impl::Slot& slot)
                {
                    return slot.state == Impl::SlotState::ConsumerPending;
                });
            if (!consumer_pending)
            {
                break;
            }
            Sleep(0);
        } while (std::chrono::steady_clock::now() < deadline);

        if (consumer_pending)
        {
            impl_->Log(LogLevel::Error,
                       "Timed out retiring D3D11 shared frames before reset");
            return false;
        }
    }

    impl_->ReleaseResourcesLocked();
    impl_->unavailable_until_invalidate = false;
    return true;
}

bool D3D9ExSharedTextureBridge::PrepareForLostDeviceReset(
    ID3D11DeviceContext* context)
{
    if (!impl_)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->initialized)
    {
        return false;
    }

    // From this point onward neither this method nor normal invalidation may
    // invoke a D3D9 method. ResetEx failure leaves the generation quarantined
    // and a later retry can repeat this D3D11-only preflight.
    impl_->lost_device_reset_phase = detail::begin_lost_device_reset();
    impl_->DisableUntilInvalidateLocked();

    if (context != nullptr && impl_->consumer_device != nullptr &&
        !impl_->ValidateConsumerContextLocked(
            impl_->consumer_device.Get(), context,
            "Lost-device ResetEx preflight"))
    {
        return false;
    }

    for (const Impl::Slot& slot : impl_->slots)
    {
        if (slot.state == Impl::SlotState::Acquired)
        {
            impl_->Log(LogLevel::Error,
                "Cannot prepare lost-device ResetEx while a shared frame is acquired");
            return false;
        }
    }
    if (impl_->consumer_fence_failed)
    {
        impl_->Log(LogLevel::Error,
            "Cannot prepare lost-device ResetEx after a D3D11 consumer fence failure");
        return false;
    }

    bool consumer_pending = std::any_of(
        impl_->slots.begin(),
        impl_->slots.end(),
        [](const Impl::Slot& slot)
        {
            return slot.state == Impl::SlotState::ConsumerPending;
        });
    if (consumer_pending && context == nullptr)
    {
        impl_->Log(LogLevel::Error,
            "D3D11 context is required for lost-device ResetEx preflight");
        return false;
    }
    if (consumer_pending)
    {
        context->Flush();
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        do
        {
            if (!impl_->PollConsumerLocked(context))
            {
                impl_->Log(LogLevel::Error,
                    "Could not retire D3D11 shared frames during lost-device ResetEx preflight");
                return false;
            }
            consumer_pending = std::any_of(
                impl_->slots.begin(),
                impl_->slots.end(),
                [](const Impl::Slot& slot)
                {
                    return slot.state == Impl::SlotState::ConsumerPending;
                });
            if (!consumer_pending)
            {
                break;
            }
            Sleep(0);
        } while (std::chrono::steady_clock::now() < deadline);

        if (consumer_pending)
        {
            impl_->Log(LogLevel::Error,
                "Timed out retiring D3D11 shared frames during lost-device ResetEx preflight");
            return false;
        }
    }

    impl_->lost_device_reset_phase =
        detail::complete_consumer_retirement(
            impl_->lost_device_reset_phase);
    return true;
}

bool D3D9ExSharedTextureBridge::FinalizeSuccessfulLostDeviceReset()
{
    if (!impl_)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!detail::can_finalize_successful_lost_device_reset(
            impl_->lost_device_reset_phase))
    {
        return false;
    }
    const bool consumer_owned = std::any_of(
        impl_->slots.begin(),
        impl_->slots.end(),
        [](const Impl::Slot& slot)
        {
            return slot.state == Impl::SlotState::Acquired ||
                slot.state == Impl::SlotState::ConsumerPending;
        });
    if (consumer_owned)
    {
        impl_->Log(LogLevel::Error,
            "Successful ResetEx cannot be finalized while D3D11 still owns a shared frame");
        return false;
    }

    // The caller's successful ResetEx is the explicit producer retirement
    // barrier. Do not query any old D3D9 fence after reset; just release the
    // retained generation and permit a fresh one on the next capture.
    impl_->ReleaseResourcesLocked();
    impl_->unavailable_until_invalidate = false;
    return true;
}

bool D3D9ExSharedTextureBridge::active() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->active;
}

std::uint64_t D3D9ExSharedTextureBridge::latest_serial() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->serial_counter;
}

} // namespace wawvr::xr
