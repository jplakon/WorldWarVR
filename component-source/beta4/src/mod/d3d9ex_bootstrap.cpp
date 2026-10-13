#include "d3d9ex_bootstrap.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>

#if defined(WAWVR_HAS_T4_BINDINGS)
#include "peer_thread_quiescence.hpp"
#include "t4_layout_selector.hpp"
#include "t4/bindings.hpp"

#include <d3d9.h>
#include <windows.h>
#endif

namespace wawvr::mod {
namespace {

[[nodiscard]] bool make_relative_call(
    const std::uintptr_t source,
    const std::uintptr_t destination,
    std::array<std::uint8_t, 5>* const output) noexcept {
    if (output == nullptr) {
        return false;
    }
    const std::int64_t displacement =
        static_cast<std::int64_t>(destination) -
        static_cast<std::int64_t>(source + output->size());
    if (displacement < std::numeric_limits<std::int32_t>::min() ||
        displacement > std::numeric_limits<std::int32_t>::max()) {
        return false;
    }
    (*output)[0] = 0xE8;
    const auto encoded = static_cast<std::int32_t>(displacement);
    std::memcpy(output->data() + 1, &encoded, sizeof(encoded));
    return true;
}

#if defined(WAWVR_HAS_T4_BINDINGS)
using Direct3DCreate9ExMethod =
    HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);
using Direct3DCreate9Method = IDirect3D9*(WINAPI*)(UINT);
using CreateDeviceMethod = HRESULT(STDMETHODCALLTYPE*)(
    IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
    D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
using ResetMethod = HRESULT(STDMETHODCALLTYPE*)(
    IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using CreateTextureMethod = HRESULT(STDMETHODCALLTYPE*)(
    IDirect3DDevice9*, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL,
    IDirect3DTexture9**, HANDLE*);
using CreateVolumeTextureMethod = HRESULT(STDMETHODCALLTYPE*)(
    IDirect3DDevice9*, UINT, UINT, UINT, UINT, DWORD, D3DFORMAT,
    D3DPOOL, IDirect3DVolumeTexture9**, HANDLE*);
using CreateCubeTextureMethod = HRESULT(STDMETHODCALLTYPE*)(
    IDirect3DDevice9*, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL,
    IDirect3DCubeTexture9**, HANDLE*);

constexpr std::size_t kResetVtableIndex = 16;
constexpr std::size_t kCreateTextureVtableIndex = 23;
constexpr std::size_t kCreateVolumeTextureVtableIndex = 24;
constexpr std::size_t kCreateCubeTextureVtableIndex = 25;

std::atomic<bool> g_patch_installed{false};
std::atomic<bool> g_factory_ex_created{false};
std::atomic<bool> g_device_ex_created{false};
std::atomic<bool> g_texture_hooks_installed{false};
std::atomic<bool> g_reset_hook_installed{false};
std::atomic<std::uint64_t> g_factory_calls{0};
std::atomic<std::uint64_t> g_create_device_calls{0};
std::atomic<std::uint64_t> g_converted_static_textures{0};
std::atomic<std::int32_t> g_last_factory_result{E_PENDING};
std::atomic<std::int32_t> g_last_create_device_result{E_PENDING};

std::mutex g_texture_hook_mutex;
std::atomic<std::uintptr_t> g_texture_hook_vtable{0};
std::atomic<void*> g_original_reset{nullptr};
std::atomic<void*> g_original_create_texture{nullptr};
std::atomic<void*> g_original_create_volume_texture{nullptr};
std::atomic<void*> g_original_create_cube_texture{nullptr};
std::atomic<void*> g_original_direct3d_create9{nullptr};

std::mutex g_bridge_device_identity_write_mutex;
std::atomic<std::uint64_t> g_bridge_device_identity_sequence{0};
std::atomic<std::uintptr_t> g_bridge_device{0};
std::atomic<std::uintptr_t> g_bridge_device_vtable{0};
std::atomic<std::uint64_t> g_bridge_device_generation{0};
std::atomic<IDirect3DDevice9Ex*> g_bridge_device_ex{nullptr};
std::uint64_t g_next_bridge_device_generation = 0;

std::mutex g_reset_handler_mutex;
std::atomic<D3D9ExResetHandler> g_reset_handler{nullptr};
std::atomic<std::uintptr_t> g_reset_handler_device{0};
std::atomic<std::uint64_t> g_reset_handler_generation{0};

template <typename Function>
[[nodiscard]] Function function_from_address(void* const address) noexcept {
    static_assert(sizeof(Function) == sizeof(address));
    Function function = nullptr;
    std::memcpy(&function, &address, sizeof(function));
    return function;
}

template <typename Function>
[[nodiscard]] void* function_address(const Function function) noexcept {
    static_assert(sizeof(Function) == sizeof(void*));
    void* address = nullptr;
    std::memcpy(&address, &function, sizeof(address));
    return address;
}

[[nodiscard]] bool read_bridge_device_identity(
    D3D9ExBridgeDeviceIdentity* const identity) noexcept {
    if (identity == nullptr) {
        return false;
    }
    *identity = {};
    const std::uint64_t sequence_before =
        g_bridge_device_identity_sequence.load(std::memory_order_acquire);
    if ((sequence_before & 1U) != 0) {
        return false;
    }
    *identity = {
        .device = g_bridge_device.load(std::memory_order_relaxed),
        .vtable = g_bridge_device_vtable.load(std::memory_order_relaxed),
        .generation =
            g_bridge_device_generation.load(std::memory_order_relaxed),
    };
    const std::uint64_t sequence_after =
        g_bridge_device_identity_sequence.load(std::memory_order_acquire);
    return sequence_before == sequence_after &&
           (sequence_after & 1U) == 0;
}

[[nodiscard]] bool publish_bridge_device_identity(
    IDirect3DDevice9* const device,
    IDirect3DDevice9Ex* const device_ex) noexcept {
    if (device == nullptr || device_ex == nullptr) {
        return false;
    }
    auto** const vtable = *reinterpret_cast<void***>(device);
    const auto vtable_address = reinterpret_cast<std::uintptr_t>(vtable);
    if (vtable == nullptr ||
        g_texture_hook_vtable.load(std::memory_order_acquire) !=
            vtable_address ||
        !g_texture_hooks_installed.load(std::memory_order_acquire) ||
        !g_reset_hook_installed.load(std::memory_order_acquire)) {
        return false;
    }

    // Retain the exact Ex interface while it is healthy. Recovery after a
    // failed ResetEx may call only ResetEx, CheckDeviceState, or Release, so
    // the later renderer service must never need QueryInterface to recover.
    device_ex->AddRef();
    std::scoped_lock lock(
        g_bridge_device_identity_write_mutex, g_reset_handler_mutex);
    g_reset_handler.store(nullptr, std::memory_order_release);
    g_reset_handler_device.store(0, std::memory_order_relaxed);
    g_reset_handler_generation.store(0, std::memory_order_relaxed);
    g_bridge_device_identity_sequence.fetch_add(
        1, std::memory_order_acq_rel);
    const std::uint64_t generation = ++g_next_bridge_device_generation;
    g_bridge_device.store(
        reinterpret_cast<std::uintptr_t>(device),
        std::memory_order_relaxed);
    g_bridge_device_vtable.store(vtable_address, std::memory_order_relaxed);
    g_bridge_device_generation.store(generation, std::memory_order_relaxed);
    g_bridge_device_ex.store(device_ex, std::memory_order_relaxed);
    g_bridge_device_identity_sequence.fetch_add(
        1, std::memory_order_release);
    return true;
}

void clear_bridge_device_identity() noexcept {
    std::scoped_lock lock(
        g_bridge_device_identity_write_mutex, g_reset_handler_mutex);
    g_reset_handler.store(nullptr, std::memory_order_release);
    g_reset_handler_device.store(0, std::memory_order_relaxed);
    g_reset_handler_generation.store(0, std::memory_order_relaxed);
    g_bridge_device_identity_sequence.fetch_add(
        1, std::memory_order_acq_rel);
    g_bridge_device.store(0, std::memory_order_relaxed);
    g_bridge_device_vtable.store(0, std::memory_order_relaxed);
    g_bridge_device_generation.store(0, std::memory_order_relaxed);
    g_bridge_device_ex.store(nullptr, std::memory_order_relaxed);
    g_bridge_device_identity_sequence.fetch_add(
        1, std::memory_order_release);
}

[[nodiscard]] HRESULT perform_d3d9ex_reset_internal(
    IDirect3DDevice9* const device,
    D3DPRESENT_PARAMETERS* const parameters) noexcept {
    if (device == nullptr || parameters == nullptr) {
        return D3DERR_INVALIDCALL;
    }
    D3D9ExBridgeDeviceIdentity identity{};
    if (!read_bridge_device_identity(&identity) ||
        identity.generation == 0 ||
        identity.device != reinterpret_cast<std::uintptr_t>(device)) {
        return D3DERR_INVALIDCALL;
    }
    IDirect3DDevice9Ex* const device_ex =
        g_bridge_device_ex.load(std::memory_order_acquire);
    if (device_ex == nullptr ||
        static_cast<IDirect3DDevice9*>(device_ex) != device) {
        return D3DERR_INVALIDCALL;
    }

    D3DDISPLAYMODEEX fullscreen_mode{};
    D3DDISPLAYMODEEX* fullscreen_mode_pointer = nullptr;
    if (!parameters->Windowed) {
        fullscreen_mode.Size = sizeof(fullscreen_mode);
        fullscreen_mode.Width = parameters->BackBufferWidth;
        fullscreen_mode.Height = parameters->BackBufferHeight;
        fullscreen_mode.RefreshRate =
            parameters->FullScreen_RefreshRateInHz;
        fullscreen_mode.Format = parameters->BackBufferFormat;
        fullscreen_mode.ScanLineOrdering =
            D3DSCANLINEORDERING_PROGRESSIVE;
        fullscreen_mode_pointer = &fullscreen_mode;
    }
    return device_ex->ResetEx(parameters, fullscreen_mode_pointer);
}

HRESULT STDMETHODCALLTYPE BootstrapResetThunk(
    IDirect3DDevice9* const device,
    D3DPRESENT_PARAMETERS* const parameters) noexcept {
    const auto original = function_from_address<ResetMethod>(
        g_original_reset.load(std::memory_order_acquire));
    if (device == nullptr) {
        return original != nullptr
            ? original(device, parameters)
            : D3DERR_INVALIDCALL;
    }
    auto** const candidate_vtable = *reinterpret_cast<void***>(device);
    D3D9ExBridgeDeviceIdentity identity{};
    if (candidate_vtable == nullptr ||
        !read_bridge_device_identity(&identity)) {
        return original != nullptr
            ? original(device, parameters)
            : D3DERR_INVALIDCALL;
    }

    const auto handler =
        g_reset_handler.load(std::memory_order_acquire);
    const D3D9ExResetRoute route = select_d3d9ex_reset_route(
        identity, reinterpret_cast<std::uintptr_t>(device),
        reinterpret_cast<std::uintptr_t>(candidate_vtable),
        handler != nullptr,
        g_reset_handler_device.load(std::memory_order_acquire),
        g_reset_handler_generation.load(std::memory_order_acquire));
    if (route == D3D9ExResetRoute::registered_handler) {
        return static_cast<HRESULT>(handler(device, parameters));
    }
    if (route == D3D9ExResetRoute::direct_reset_ex) {
        return perform_d3d9ex_reset_internal(device, parameters);
    }
    return original != nullptr
        ? original(device, parameters)
        : D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE CreateTextureThunk(
    IDirect3DDevice9* const device,
    const UINT width,
    const UINT height,
    const UINT levels,
    DWORD usage,
    const D3DFORMAT format,
    D3DPOOL pool,
    IDirect3DTexture9** const texture,
    HANDLE* const shared_handle) noexcept {
    const auto original = function_from_address<CreateTextureMethod>(
        g_original_create_texture.load(std::memory_order_acquire));
    if (original == nullptr) {
        return D3DERR_INVALIDCALL;
    }
    if (usage == 0 && pool == D3DPOOL_MANAGED) {
        usage = D3DUSAGE_DYNAMIC;
        pool = D3DPOOL_DEFAULT;
        g_converted_static_textures.fetch_add(1, std::memory_order_relaxed);
    }
    return original(
        device, width, height, levels, usage, format, pool, texture,
        shared_handle);
}

HRESULT STDMETHODCALLTYPE CreateVolumeTextureThunk(
    IDirect3DDevice9* const device,
    const UINT width,
    const UINT height,
    const UINT depth,
    const UINT levels,
    DWORD usage,
    const D3DFORMAT format,
    D3DPOOL pool,
    IDirect3DVolumeTexture9** const texture,
    HANDLE* const shared_handle) noexcept {
    const auto original =
        function_from_address<CreateVolumeTextureMethod>(
            g_original_create_volume_texture.load(
                std::memory_order_acquire));
    if (original == nullptr) {
        return D3DERR_INVALIDCALL;
    }
    if (usage == 0 && pool == D3DPOOL_MANAGED) {
        usage = D3DUSAGE_DYNAMIC;
        pool = D3DPOOL_DEFAULT;
        g_converted_static_textures.fetch_add(1, std::memory_order_relaxed);
    }
    return original(
        device, width, height, depth, levels, usage, format, pool, texture,
        shared_handle);
}

HRESULT STDMETHODCALLTYPE CreateCubeTextureThunk(
    IDirect3DDevice9* const device,
    const UINT edge_length,
    const UINT levels,
    DWORD usage,
    const D3DFORMAT format,
    D3DPOOL pool,
    IDirect3DCubeTexture9** const texture,
    HANDLE* const shared_handle) noexcept {
    const auto original = function_from_address<CreateCubeTextureMethod>(
        g_original_create_cube_texture.load(std::memory_order_acquire));
    if (original == nullptr) {
        return D3DERR_INVALIDCALL;
    }
    if (usage == 0 && pool == D3DPOOL_MANAGED) {
        usage = D3DUSAGE_DYNAMIC;
        pool = D3DPOOL_DEFAULT;
        g_converted_static_textures.fetch_add(1, std::memory_order_relaxed);
    }
    return original(
        device, edge_length, levels, usage, format, pool, texture,
        shared_handle);
}

[[nodiscard]] bool compatibility_slots_match(void** const vtable) noexcept {
    if (vtable == nullptr) {
        return false;
    }
    const D3D9ExCompatibilitySlots observed{
        .reset = reinterpret_cast<std::uintptr_t>(
            vtable[kResetVtableIndex]),
        .create_texture = reinterpret_cast<std::uintptr_t>(
            vtable[kCreateTextureVtableIndex]),
        .create_volume_texture = reinterpret_cast<std::uintptr_t>(
            vtable[kCreateVolumeTextureVtableIndex]),
        .create_cube_texture = reinterpret_cast<std::uintptr_t>(
            vtable[kCreateCubeTextureVtableIndex]),
    };
    const D3D9ExCompatibilitySlots required{
        .reset = reinterpret_cast<std::uintptr_t>(function_address(
            static_cast<ResetMethod>(&BootstrapResetThunk))),
        .create_texture = reinterpret_cast<std::uintptr_t>(function_address(
            static_cast<CreateTextureMethod>(&CreateTextureThunk))),
        .create_volume_texture = reinterpret_cast<std::uintptr_t>(
            function_address(static_cast<CreateVolumeTextureMethod>(
                &CreateVolumeTextureThunk))),
        .create_cube_texture = reinterpret_cast<std::uintptr_t>(
            function_address(static_cast<CreateCubeTextureMethod>(
                &CreateCubeTextureThunk))),
    };
    return d3d9ex_compatibility_slots_match(observed, required);
}

[[nodiscard]] bool install_ex_compatibility_hooks(
    IDirect3DDevice9* const device) noexcept {
    if (device == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_texture_hook_mutex);
    auto** const vtable = *reinterpret_cast<void***>(device);
    if (vtable == nullptr) {
        return false;
    }
    const auto installed_vtable = reinterpret_cast<void**>(
        g_texture_hook_vtable.load(std::memory_order_acquire));
    if (installed_vtable == vtable) {
        return g_texture_hooks_installed.load(std::memory_order_acquire) &&
               g_reset_hook_installed.load(std::memory_order_acquire) &&
               compatibility_slots_match(vtable);
    }
    if (installed_vtable != nullptr) {
        return false;
    }

    constexpr std::array<std::size_t, 4> slot_indices{
        kResetVtableIndex,
        kCreateTextureVtableIndex,
        kCreateVolumeTextureVtableIndex,
        kCreateCubeTextureVtableIndex,
    };
    std::array<void*, slot_indices.size()> originals{
        vtable[kResetVtableIndex],
        vtable[kCreateTextureVtableIndex],
        vtable[kCreateVolumeTextureVtableIndex],
        vtable[kCreateCubeTextureVtableIndex],
    };
    if (std::any_of(
            originals.begin(), originals.end(),
            [](void* const value) { return value == nullptr; })) {
        return false;
    }
    const std::array<void*, slot_indices.size()> replacements{
        function_address(static_cast<ResetMethod>(&BootstrapResetThunk)),
        function_address(
            static_cast<CreateTextureMethod>(&CreateTextureThunk)),
        function_address(
            static_cast<CreateVolumeTextureMethod>(
                &CreateVolumeTextureThunk)),
        function_address(
            static_cast<CreateCubeTextureMethod>(&CreateCubeTextureThunk)),
    };

    // Publish original routes before the first slot becomes callable. The
    // install is still considered unavailable until every replacement is
    // verified and the original page protection has been restored.
    g_original_reset.store(originals[0], std::memory_order_release);
    g_original_create_texture.store(originals[1], std::memory_order_release);
    g_original_create_volume_texture.store(
        originals[2], std::memory_order_release);
    g_original_create_cube_texture.store(
        originals[3], std::memory_order_release);

    void** const first_slot = vtable + kResetVtableIndex;
    constexpr std::size_t protected_slot_count =
        kCreateCubeTextureVtableIndex - kResetVtableIndex + 1;
    constexpr std::size_t byte_count =
        protected_slot_count * sizeof(void*);
    DWORD old_protection = 0;
    if (!VirtualProtect(
            first_slot, byte_count, PAGE_EXECUTE_READWRITE,
            &old_protection)) {
        return false;
    }
    for (std::size_t index = 0; index < slot_indices.size(); ++index) {
        InterlockedExchangePointer(
            reinterpret_cast<PVOID volatile*>(
                vtable + slot_indices[index]),
            replacements[index]);
    }
    const bool written = std::equal(
        slot_indices.begin(), slot_indices.end(), replacements.begin(),
        [vtable](const std::size_t slot, void* const replacement) {
            return vtable[slot] == replacement;
        });
    if (!written) {
        for (std::size_t index = 0; index < slot_indices.size(); ++index) {
            InterlockedExchangePointer(
                reinterpret_cast<PVOID volatile*>(
                    vtable + slot_indices[index]),
                originals[index]);
        }
    }
    DWORD ignored = 0;
    const bool protection_restored =
        VirtualProtect(
            first_slot, byte_count, old_protection, &ignored) != FALSE;
    if (!written || !protection_restored) {
        if (written && !protection_restored) {
            DWORD rollback_protection = 0;
            static_cast<void>(VirtualProtect(
                first_slot, byte_count, PAGE_EXECUTE_READWRITE,
                &rollback_protection));
            for (std::size_t index = 0;
                 index < slot_indices.size(); ++index) {
                InterlockedExchangePointer(
                    reinterpret_cast<PVOID volatile*>(
                        vtable + slot_indices[index]),
                    originals[index]);
            }
            DWORD retry_ignored = 0;
            static_cast<void>(VirtualProtect(
                first_slot, byte_count, old_protection, &retry_ignored));
        }
        return false;
    }

    g_texture_hook_vtable.store(
        reinterpret_cast<std::uintptr_t>(vtable),
        std::memory_order_release);
    g_texture_hooks_installed.store(true, std::memory_order_release);
    g_reset_hook_installed.store(true, std::memory_order_release);
    return true;
}

IDirect3D9* WINAPI Direct3DCreate9ExThunk(
    const UINT sdk_version) noexcept {
    g_factory_calls.fetch_add(1, std::memory_order_relaxed);
    IDirect3D9Ex* interface_ex = nullptr;
    HRESULT result = E_NOINTERFACE;
    const HMODULE d3d9 = GetModuleHandleW(L"d3d9.dll");
    const auto create_ex = d3d9 != nullptr
        ? reinterpret_cast<Direct3DCreate9ExMethod>(
              GetProcAddress(d3d9, "Direct3DCreate9Ex"))
        : nullptr;
    if (create_ex != nullptr) {
        result = create_ex(sdk_version, &interface_ex);
    }
    g_last_factory_result.store(result, std::memory_order_release);
    if (SUCCEEDED(result) && interface_ex != nullptr) {
        g_factory_ex_created.store(true, std::memory_order_release);
        return static_cast<IDirect3D9*>(interface_ex);
    }
    if (interface_ex != nullptr) {
        interface_ex->Release();
    }
    const auto original = function_from_address<Direct3DCreate9Method>(
        g_original_direct3d_create9.load(std::memory_order_acquire));
    return original != nullptr ? original(sdk_version)
                               : Direct3DCreate9(sdk_version);
}

HRESULT STDMETHODCALLTYPE CreateDeviceExThunk(
    IDirect3D9* const direct3d,
    const UINT adapter,
    const D3DDEVTYPE device_type,
    const HWND focus_window,
    const DWORD behavior_flags,
    D3DPRESENT_PARAMETERS* const parameters,
    IDirect3DDevice9** const returned_device) noexcept {
    g_create_device_calls.fetch_add(1, std::memory_order_relaxed);
    if (returned_device == nullptr) {
        g_last_create_device_result.store(
            D3DERR_INVALIDCALL, std::memory_order_release);
        return D3DERR_INVALIDCALL;
    }
    *returned_device = nullptr;

    IDirect3D9Ex* direct3d_ex = nullptr;
    bool compatibility_install_failed = false;
    HRESULT result = direct3d != nullptr
        ? direct3d->QueryInterface(
              __uuidof(IDirect3D9Ex),
              reinterpret_cast<void**>(&direct3d_ex))
        : E_POINTER;
    if (SUCCEEDED(result) && direct3d_ex != nullptr && parameters != nullptr) {
        D3DDISPLAYMODEEX fullscreen_mode{};
        D3DDISPLAYMODEEX* fullscreen_mode_pointer = nullptr;
        if (!parameters->Windowed) {
            fullscreen_mode.Size = sizeof(fullscreen_mode);
            fullscreen_mode.Width = parameters->BackBufferWidth;
            fullscreen_mode.Height = parameters->BackBufferHeight;
            fullscreen_mode.RefreshRate =
                parameters->FullScreen_RefreshRateInHz;
            fullscreen_mode.Format = parameters->BackBufferFormat;
            fullscreen_mode.ScanLineOrdering =
                D3DSCANLINEORDERING_PROGRESSIVE;
            fullscreen_mode_pointer = &fullscreen_mode;
        }

        IDirect3DDevice9Ex* device_ex = nullptr;
        result = direct3d_ex->CreateDeviceEx(
            adapter, device_type, focus_window, behavior_flags, parameters,
            fullscreen_mode_pointer, &device_ex);
        direct3d_ex->Release();
        direct3d_ex = nullptr;
        if (SUCCEEDED(result) && device_ex != nullptr) {
            if (!install_ex_compatibility_hooks(device_ex) ||
                !publish_bridge_device_identity(device_ex, device_ex)) {
                device_ex->Release();
                compatibility_install_failed = true;
                result = D3DERR_NOTAVAILABLE;
            } else {
                *returned_device = static_cast<IDirect3DDevice9*>(device_ex);
                g_device_ex_created.store(true, std::memory_order_release);
                g_last_create_device_result.store(
                    result, std::memory_order_release);
                return result;
            }
        } else if (device_ex != nullptr) {
            device_ex->Release();
        }
    } else if (direct3d_ex != nullptr) {
        direct3d_ex->Release();
    }

    // Preserve the stock renderer as a recovery path when Ex is unavailable.
    // A compatibility-hook failure after a successful CreateDeviceEx is kept
    // fail-closed because returning that Ex device would make T4's managed
    // texture creation invalid.
    if (d3d9ex_should_try_legacy_device_fallback(
            direct3d != nullptr, compatibility_install_failed)) {
        IDirect3DDevice9* candidate = nullptr;
        result = direct3d->CreateDevice(
            adapter, device_type, focus_window, behavior_flags, parameters,
            &candidate);
        if (SUCCEEDED(result) && candidate != nullptr) {
            IDirect3DDevice9Ex* candidate_ex = nullptr;
            const HRESULT query_result = candidate->QueryInterface(
                __uuidof(IDirect3DDevice9Ex),
                reinterpret_cast<void**>(&candidate_ex));
            if (SUCCEEDED(query_result) && candidate_ex != nullptr) {
                if (!install_ex_compatibility_hooks(candidate) ||
                    !publish_bridge_device_identity(
                        candidate, candidate_ex)) {
                    candidate_ex->Release();
                    candidate->Release();
                    candidate = nullptr;
                    result = D3DERR_NOTAVAILABLE;
                } else {
                    candidate_ex->Release();
                    g_device_ex_created.store(
                        true, std::memory_order_release);
                }
            } else {
                if (candidate_ex != nullptr) {
                    candidate_ex->Release();
                }
                clear_bridge_device_identity();
            }
            if (candidate != nullptr) {
                *returned_device = candidate;
            }
        } else if (SUCCEEDED(result)) {
            result = D3DERR_INVALIDCALL;
        }
    }
    g_last_create_device_result.store(result, std::memory_order_release);
    return result;
}

[[nodiscard]] bool restore_bootstrap_bytes(
    std::uint8_t* const factory_call,
    std::uint8_t* const create_device_dispatch) noexcept {
    std::memcpy(
        factory_call, kExpectedD3D9FactoryCall.data(),
        kExpectedD3D9FactoryCall.size());
    std::memcpy(
        create_device_dispatch,
        kExpectedD3D9CreateDeviceDispatch.data(),
        kExpectedD3D9CreateDeviceDispatch.size());
    return FlushInstructionCache(
               GetCurrentProcess(), factory_call,
               kExpectedD3D9FactoryCall.size()) != FALSE &&
           FlushInstructionCache(
               GetCurrentProcess(), create_device_dispatch,
               kExpectedD3D9CreateDeviceDispatch.size()) != FALSE;
}
#endif

}  // namespace

bool build_d3d9ex_bootstrap_patch_plan(
    const std::uintptr_t factory_call,
    const std::uintptr_t create_device_dispatch,
    const std::uintptr_t factory_thunk,
    const std::uintptr_t create_device_thunk,
    D3D9ExBootstrapPatchPlan* const plan) noexcept {
    if (plan == nullptr) {
        return false;
    }
    *plan = {};
    if (!make_relative_call(
            factory_call, factory_thunk, &plan->factory_call)) {
        return false;
    }
    plan->create_device_dispatch[0] = 0x50;
    std::array<std::uint8_t, 5> call{};
    if (!make_relative_call(
            create_device_dispatch + 1, create_device_thunk, &call)) {
        *plan = {};
        return false;
    }
    std::copy(
        call.begin(), call.end(), plan->create_device_dispatch.begin() + 1);
    return true;
}

D3D9ExBootstrapContextState inspect_d3d9ex_bootstrap_context(
    const std::span<const std::uint8_t> factory_call,
    const std::span<const std::uint8_t> create_device_dispatch,
    const D3D9ExBootstrapPatchPlan& plan) noexcept {
    if (factory_call.size() != kExpectedD3D9FactoryCall.size() ||
        create_device_dispatch.size() !=
            kExpectedD3D9CreateDeviceDispatch.size()) {
        return D3D9ExBootstrapContextState::mismatch;
    }
    if (std::equal(
            factory_call.begin(), factory_call.end(),
            kExpectedD3D9FactoryCall.begin()) &&
        std::equal(
            create_device_dispatch.begin(), create_device_dispatch.end(),
            kExpectedD3D9CreateDeviceDispatch.begin())) {
        return D3D9ExBootstrapContextState::expected;
    }
    if (std::equal(
            factory_call.begin(), factory_call.end(),
            plan.factory_call.begin()) &&
        std::equal(
            create_device_dispatch.begin(), create_device_dispatch.end(),
            plan.create_device_dispatch.begin())) {
        return D3D9ExBootstrapContextState::already_patched;
    }
    return D3D9ExBootstrapContextState::mismatch;
}

const char* d3d9ex_bootstrap_patch_status_name(
    const D3D9ExBootstrapPatchStatus status) noexcept {
    switch (status) {
    case D3D9ExBootstrapPatchStatus::applied:
        return "applied";
    case D3D9ExBootstrapPatchStatus::already_applied:
        return "already-applied";
    case D3D9ExBootstrapPatchStatus::not_applicable:
        return "not-applicable";
    case D3D9ExBootstrapPatchStatus::address_out_of_range:
        return "address-out-of-range";
    case D3D9ExBootstrapPatchStatus::patch_target_out_of_range:
        return "patch-target-out-of-range";
    case D3D9ExBootstrapPatchStatus::renderer_already_initialized:
        return "renderer-already-initialized";
    case D3D9ExBootstrapPatchStatus::unexpected_bytes:
        return "unexpected-bytes";
    case D3D9ExBootstrapPatchStatus::peer_thread_quiesce_failed:
        return "peer-thread-quiesce-failed";
    case D3D9ExBootstrapPatchStatus::virtual_protect_failed:
        return "virtual-protect-failed";
    case D3D9ExBootstrapPatchStatus::expected_bytes_changed:
        return "expected-bytes-changed";
    case D3D9ExBootstrapPatchStatus::write_verification_failed:
        return "write-verification-failed";
    case D3D9ExBootstrapPatchStatus::instruction_cache_flush_failed:
        return "instruction-cache-flush-failed";
    case D3D9ExBootstrapPatchStatus::protection_restore_failed:
        return "protection-restore-failed";
    case D3D9ExBootstrapPatchStatus::rollback_failed:
        return "rollback-failed";
    }
    return "unknown";
}

D3D9ExBootstrapRuntimeSnapshot
d3d9ex_bootstrap_runtime_snapshot() noexcept {
#if defined(WAWVR_HAS_T4_BINDINGS)
    return {
        .patch_installed =
            g_patch_installed.load(std::memory_order_acquire),
        .factory_ex_created =
            g_factory_ex_created.load(std::memory_order_acquire),
        .device_ex_created =
            g_device_ex_created.load(std::memory_order_acquire),
        .texture_hooks_installed =
            g_texture_hooks_installed.load(std::memory_order_acquire),
        .reset_hook_installed =
            g_reset_hook_installed.load(std::memory_order_acquire),
        .factory_calls = g_factory_calls.load(std::memory_order_relaxed),
        .create_device_calls =
            g_create_device_calls.load(std::memory_order_relaxed),
        .converted_static_textures =
            g_converted_static_textures.load(std::memory_order_relaxed),
        .last_factory_result =
            g_last_factory_result.load(std::memory_order_acquire),
        .last_create_device_result =
            g_last_create_device_result.load(std::memory_order_acquire),
    };
#else
    return {};
#endif
}

bool d3d9ex_shared_bridge_ready(IDirect3DDevice9* const device) noexcept {
#if defined(WAWVR_HAS_T4_BINDINGS)
    if (device == nullptr ||
        !g_device_ex_created.load(std::memory_order_acquire) ||
        !g_texture_hooks_installed.load(std::memory_order_acquire) ||
        !g_reset_hook_installed.load(std::memory_order_acquire)) {
        return false;
    }
    auto** const candidate_vtable = *reinterpret_cast<void***>(device);
    if (candidate_vtable == nullptr) {
        return false;
    }
    if (candidate_vtable[kResetVtableIndex] != function_address(
            static_cast<ResetMethod>(&BootstrapResetThunk)) ||
        candidate_vtable[kCreateTextureVtableIndex] != function_address(
            static_cast<CreateTextureMethod>(&CreateTextureThunk)) ||
        candidate_vtable[kCreateVolumeTextureVtableIndex] != function_address(
            static_cast<CreateVolumeTextureMethod>(
                &CreateVolumeTextureThunk)) ||
        candidate_vtable[kCreateCubeTextureVtableIndex] != function_address(
            static_cast<CreateCubeTextureMethod>(
                &CreateCubeTextureThunk))) {
        return false;
    }

    D3D9ExBridgeDeviceIdentity registered{};
    if (!read_bridge_device_identity(&registered)) {
        return false;
    }
    return d3d9ex_bridge_device_identity_matches(
               registered, reinterpret_cast<std::uintptr_t>(device),
               reinterpret_cast<std::uintptr_t>(candidate_vtable)) &&
           compatibility_slots_match(candidate_vtable);
#else
    static_cast<void>(device);
    return false;
#endif
}

bool register_d3d9ex_reset_handler(
    IDirect3DDevice9* const device,
    const D3D9ExResetHandler handler) noexcept {
#if defined(WAWVR_HAS_T4_BINDINGS)
    if (device == nullptr || handler == nullptr ||
        !d3d9ex_shared_bridge_ready(device)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_reset_handler_mutex);
    D3D9ExBridgeDeviceIdentity identity{};
    if (!read_bridge_device_identity(&identity) ||
        !d3d9ex_shared_bridge_ready(device)) {
        return false;
    }
    const auto existing =
        g_reset_handler.load(std::memory_order_acquire);
    if (existing != nullptr &&
        (existing != handler ||
         g_reset_handler_device.load(std::memory_order_acquire) !=
             identity.device ||
         g_reset_handler_generation.load(std::memory_order_acquire) !=
             identity.generation)) {
        return false;
    }
    g_reset_handler_device.store(identity.device, std::memory_order_relaxed);
    g_reset_handler_generation.store(
        identity.generation, std::memory_order_relaxed);
    g_reset_handler.store(handler, std::memory_order_release);
    return true;
#else
    static_cast<void>(device);
    static_cast<void>(handler);
    return false;
#endif
}

void unregister_d3d9ex_reset_handler(
    IDirect3DDevice9* const device,
    const D3D9ExResetHandler handler) noexcept {
#if defined(WAWVR_HAS_T4_BINDINGS)
    if (device == nullptr || handler == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_reset_handler_mutex);
    D3D9ExBridgeDeviceIdentity identity{};
    const std::uint64_t registered_generation =
        g_reset_handler_generation.load(std::memory_order_acquire);
    if (g_reset_handler.load(std::memory_order_acquire) != handler ||
        g_reset_handler_device.load(std::memory_order_acquire) !=
            reinterpret_cast<std::uintptr_t>(device) ||
        !read_bridge_device_identity(&identity) ||
        identity.device != reinterpret_cast<std::uintptr_t>(device) ||
        identity.generation != registered_generation) {
        return;
    }
    g_reset_handler.store(nullptr, std::memory_order_release);
    g_reset_handler_device.store(0, std::memory_order_relaxed);
    g_reset_handler_generation.store(0, std::memory_order_relaxed);
#else
    static_cast<void>(device);
    static_cast<void>(handler);
#endif
}

std::int32_t perform_d3d9ex_reset(
    IDirect3DDevice9* const device,
    _D3DPRESENT_PARAMETERS_* const parameters) noexcept {
#if defined(WAWVR_HAS_T4_BINDINGS)
    return perform_d3d9ex_reset_internal(device, parameters);
#else
    static_cast<void>(device);
    static_cast<void>(parameters);
    return static_cast<std::int32_t>(0x8876086CU);
#endif
}

std::int32_t check_d3d9ex_device_state(
    IDirect3DDevice9* const device,
    void* const focus_window) noexcept {
#if defined(WAWVR_HAS_T4_BINDINGS)
    if (device == nullptr || focus_window == nullptr) {
        return D3DERR_INVALIDCALL;
    }
    D3D9ExBridgeDeviceIdentity identity{};
    if (!read_bridge_device_identity(&identity) ||
        identity.generation == 0 ||
        identity.device != reinterpret_cast<std::uintptr_t>(device)) {
        return D3DERR_INVALIDCALL;
    }
    IDirect3DDevice9Ex* const device_ex =
        g_bridge_device_ex.load(std::memory_order_acquire);
    if (device_ex == nullptr ||
        static_cast<IDirect3DDevice9*>(device_ex) != device) {
        return D3DERR_INVALIDCALL;
    }
    return device_ex->CheckDeviceState(
        static_cast<HWND>(focus_window));
#else
    static_cast<void>(device);
    static_cast<void>(focus_window);
    return static_cast<std::int32_t>(0x8876086CU);
#endif
}

#if defined(WAWVR_HAS_T4_BINDINGS)
D3D9ExBootstrapPatchResult install_d3d9ex_bootstrap_patch(
    const wawvr::t4::ValidatedBindings& bindings) noexcept {
    D3D9ExBootstrapPatchResult result{};
    if (select_t4_layout_family(bindings.profile()) !=
        T4LayoutFamily::single_player_1_7_1263) {
        result.status = D3D9ExBootstrapPatchStatus::not_applicable;
        return result;
    }
    const auto factory_address = bindings.module().address(
        kD3D9FactoryCallRva, kExpectedD3D9FactoryCall.size());
    const auto create_address = bindings.module().address(
        kD3D9CreateDeviceDispatchRva,
        kExpectedD3D9CreateDeviceDispatch.size());
    const auto factory_routine = bindings.module().address(
        kD3D9FactoryInitRoutineRva, kD3D9FactoryInitRoutineSize);
    const auto create_routine = bindings.module().address(
        kD3D9CreateDeviceRoutineRva, kD3D9CreateDeviceRoutineSize);
    const auto outer_wrapper = bindings.module().address(
        kD3D9RendererOuterWrapperRva, kD3D9RendererOuterWrapperSize);
    if (!factory_address.has_value() || !create_address.has_value() ||
        !factory_routine.has_value() || !create_routine.has_value() ||
        !outer_wrapper.has_value()) {
        result.status = D3D9ExBootstrapPatchStatus::address_out_of_range;
        return result;
    }
    auto* const factory_call =
        reinterpret_cast<std::uint8_t*>(*factory_address);
    auto* const create_device_dispatch =
        reinterpret_cast<std::uint8_t*>(*create_address);

    D3D9ExBootstrapPatchPlan plan{};
    if (!build_d3d9ex_bootstrap_patch_plan(
            *factory_address, *create_address,
            reinterpret_cast<std::uintptr_t>(&Direct3DCreate9ExThunk),
            reinterpret_cast<std::uintptr_t>(&CreateDeviceExThunk),
            &plan)) {
        result.status =
            D3D9ExBootstrapPatchStatus::patch_target_out_of_range;
        return result;
    }

    auto inspect = [&]() noexcept {
        return inspect_d3d9ex_bootstrap_context(
            std::span<const std::uint8_t>(
                factory_call, kExpectedD3D9FactoryCall.size()),
            std::span<const std::uint8_t>(
                create_device_dispatch,
                kExpectedD3D9CreateDeviceDispatch.size()),
            plan);
    };
    switch (inspect()) {
    case D3D9ExBootstrapContextState::already_patched:
        g_patch_installed.store(true, std::memory_order_release);
        result.status = D3D9ExBootstrapPatchStatus::already_applied;
        return result;
    case D3D9ExBootstrapContextState::mismatch:
        result.status = D3D9ExBootstrapPatchStatus::unexpected_bytes;
        return result;
    case D3D9ExBootstrapContextState::expected:
        break;
    }

    std::int32_t original_factory_displacement = 0;
    std::memcpy(
        &original_factory_displacement,
        factory_call + 1,
        sizeof(original_factory_displacement));
    const std::uintptr_t original_factory_target =
        static_cast<std::uintptr_t>(
            static_cast<std::int64_t>(*factory_address + 5) +
            original_factory_displacement);
    if (original_factory_target == 0) {
        result.status = D3D9ExBootstrapPatchStatus::unexpected_bytes;
        return result;
    }

    const auto live_factory = bindings.module().address(
        kT4D3D9FactoryPointerRva, sizeof(void*));
    const auto live_device = bindings.module().address(
        kT4D3D9DevicePointerRva, sizeof(void*));
    if (!live_factory.has_value() || !live_device.has_value()) {
        result.status = D3D9ExBootstrapPatchStatus::address_out_of_range;
        return result;
    }
    std::memcpy(
        &result.existing_factory,
        reinterpret_cast<const void*>(*live_factory),
        sizeof(result.existing_factory));
    std::memcpy(
        &result.existing_device,
        reinterpret_cast<const void*>(*live_device),
        sizeof(result.existing_device));
    if (result.existing_factory != 0 || result.existing_device != 0) {
        result.status =
            D3D9ExBootstrapPatchStatus::renderer_already_initialized;
        return result;
    }

    const std::array<PeerThreadPatchRange, 3> ranges{{
        {*factory_routine, kD3D9FactoryInitRoutineSize},
        {*create_routine, kD3D9CreateDeviceRoutineSize},
        {*outer_wrapper, kD3D9RendererOuterWrapperSize},
    }};
    const std::array<std::uintptr_t, 2> critical_returns{{
        *factory_address + kExpectedD3D9FactoryCall.size(),
        *create_address + kExpectedD3D9CreateDeviceDispatch.size(),
    }};
    SuspendedPeerThreads suspended;
    PeerThreadQuiesceResult quiesce{};
    if (!suspended.suspend(ranges, &quiesce, critical_returns)) {
        result.status =
            D3D9ExBootstrapPatchStatus::peer_thread_quiesce_failed;
        result.system_error = quiesce.system_error;
        result.thread_id = quiesce.thread_id;
        return result;
    }
    // The renderer pointers were checked before peer suspension only as a
    // fast rejection. Re-read them while every peer is held so the permanent
    // callsite routes cannot be installed over a renderer that became live
    // during quiescence.
    std::memcpy(
        &result.existing_factory,
        reinterpret_cast<const void*>(*live_factory),
        sizeof(result.existing_factory));
    std::memcpy(
        &result.existing_device,
        reinterpret_cast<const void*>(*live_device),
        sizeof(result.existing_device));
    if (result.existing_factory != 0 || result.existing_device != 0) {
        result.status =
            D3D9ExBootstrapPatchStatus::renderer_already_initialized;
        return result;
    }
    if (inspect() != D3D9ExBootstrapContextState::expected) {
        result.status =
            D3D9ExBootstrapPatchStatus::expected_bytes_changed;
        return result;
    }

    const auto span_begin = std::min(*factory_address, *create_address);
    const auto span_end = std::max(
        *factory_address + kExpectedD3D9FactoryCall.size(),
        *create_address + kExpectedD3D9CreateDeviceDispatch.size());
    DWORD old_protection = 0;
    if (!VirtualProtect(
            reinterpret_cast<void*>(span_begin), span_end - span_begin,
            PAGE_EXECUTE_READWRITE, &old_protection)) {
        result.status =
            D3D9ExBootstrapPatchStatus::virtual_protect_failed;
        result.system_error = GetLastError();
        return result;
    }
    if (inspect() != D3D9ExBootstrapContextState::expected) {
        DWORD ignored = 0;
        static_cast<void>(VirtualProtect(
            reinterpret_cast<void*>(span_begin), span_end - span_begin,
            old_protection, &ignored));
        result.status =
            D3D9ExBootstrapPatchStatus::expected_bytes_changed;
        return result;
    }

    std::memcpy(
        factory_call, plan.factory_call.data(), plan.factory_call.size());
    std::memcpy(
        create_device_dispatch, plan.create_device_dispatch.data(),
        plan.create_device_dispatch.size());
    if (inspect() != D3D9ExBootstrapContextState::already_patched) {
        result.status =
            D3D9ExBootstrapPatchStatus::write_verification_failed;
        const bool restored = restore_bootstrap_bytes(
            factory_call, create_device_dispatch);
        DWORD ignored = 0;
        const bool protection_restored = VirtualProtect(
            reinterpret_cast<void*>(span_begin), span_end - span_begin,
            old_protection, &ignored) != FALSE;
        if (!restored || !protection_restored) {
            result.status = D3D9ExBootstrapPatchStatus::rollback_failed;
            if (!protection_restored) {
                result.system_error = GetLastError();
            }
        }
        return result;
    }
    const bool cache_flushed =
        FlushInstructionCache(
            GetCurrentProcess(), factory_call,
            plan.factory_call.size()) != FALSE &&
        FlushInstructionCache(
            GetCurrentProcess(), create_device_dispatch,
            plan.create_device_dispatch.size()) != FALSE;
    if (!cache_flushed) {
        result.status =
            D3D9ExBootstrapPatchStatus::instruction_cache_flush_failed;
        result.system_error = GetLastError();
        const bool restored = restore_bootstrap_bytes(
            factory_call, create_device_dispatch);
        DWORD ignored = 0;
        const bool protection_restored = VirtualProtect(
            reinterpret_cast<void*>(span_begin), span_end - span_begin,
            old_protection, &ignored) != FALSE;
        if (!restored || !protection_restored) {
            result.status = D3D9ExBootstrapPatchStatus::rollback_failed;
        }
        return result;
    }

    DWORD ignored = 0;
    if (!VirtualProtect(
            reinterpret_cast<void*>(span_begin), span_end - span_begin,
            old_protection, &ignored)) {
        result.status =
            D3D9ExBootstrapPatchStatus::protection_restore_failed;
        result.system_error = GetLastError();
        const bool restored = restore_bootstrap_bytes(
            factory_call, create_device_dispatch);
        DWORD retry_ignored = 0;
        const bool protection_restored = VirtualProtect(
            reinterpret_cast<void*>(span_begin), span_end - span_begin,
            old_protection, &retry_ignored) != FALSE;
        if (!restored || !protection_restored) {
            result.status = D3D9ExBootstrapPatchStatus::rollback_failed;
        }
        return result;
    }

    g_patch_installed.store(true, std::memory_order_release);
    g_original_direct3d_create9.store(
        reinterpret_cast<void*>(original_factory_target),
        std::memory_order_release);
    result.status = D3D9ExBootstrapPatchStatus::applied;
    return result;
}
#endif

}  // namespace wawvr::mod
