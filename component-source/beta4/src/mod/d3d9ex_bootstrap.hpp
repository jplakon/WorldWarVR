#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

struct IDirect3DDevice9;
struct _D3DPRESENT_PARAMETERS_;

#if defined(WAWVR_HAS_T4_BINDINGS)
namespace wawvr::t4 {
class ValidatedBindings;
}
#endif

namespace wawvr::mod {

inline constexpr std::uint32_t kD3D9FactoryCallRva = 0x002D62BB;
inline constexpr std::uint32_t kD3D9CreateDeviceDispatchRva = 0x002D6056;
inline constexpr std::uint32_t kD3D9FactoryInitRoutineRva = 0x002D62A0;
inline constexpr std::size_t kD3D9FactoryInitRoutineSize = 0x67;
inline constexpr std::uint32_t kD3D9CreateDeviceRoutineRva = 0x002D5FF0;
inline constexpr std::size_t kD3D9CreateDeviceRoutineSize = 0x118;
inline constexpr std::uint32_t kD3D9RendererOuterWrapperRva = 0x002D6310;
inline constexpr std::size_t kD3D9RendererOuterWrapperSize = 0x65;
inline constexpr std::uint32_t kT4D3D9FactoryPointerRva = 0x037F3B04;
inline constexpr std::uint32_t kT4D3D9DevicePointerRva = 0x037F3B08;
inline constexpr std::array<std::uint8_t, 5> kExpectedD3D9FactoryCall{
    0xE8, 0xE8, 0x46, 0x08, 0x00};
inline constexpr std::array<std::uint8_t, 6>
    kExpectedD3D9CreateDeviceDispatch{
        0x50, 0x8B, 0x41, 0x40, 0xFF, 0xD0};

struct D3D9ExBootstrapPatchPlan final {
    std::array<std::uint8_t, 5> factory_call{};
    std::array<std::uint8_t, 6> create_device_dispatch{};
};

[[nodiscard]] bool build_d3d9ex_bootstrap_patch_plan(
    std::uintptr_t factory_call,
    std::uintptr_t create_device_dispatch,
    std::uintptr_t factory_thunk,
    std::uintptr_t create_device_thunk,
    D3D9ExBootstrapPatchPlan* plan) noexcept;

enum class D3D9ExBootstrapContextState : std::uint8_t {
    expected,
    already_patched,
    mismatch,
};

[[nodiscard]] D3D9ExBootstrapContextState
inspect_d3d9ex_bootstrap_context(
    std::span<const std::uint8_t> factory_call,
    std::span<const std::uint8_t> create_device_dispatch,
    const D3D9ExBootstrapPatchPlan& plan) noexcept;

enum class D3D9ExBootstrapPatchStatus : std::uint8_t {
    applied,
    already_applied,
    not_applicable,
    address_out_of_range,
    patch_target_out_of_range,
    renderer_already_initialized,
    unexpected_bytes,
    peer_thread_quiesce_failed,
    virtual_protect_failed,
    expected_bytes_changed,
    write_verification_failed,
    instruction_cache_flush_failed,
    protection_restore_failed,
    rollback_failed,
};

struct D3D9ExBootstrapPatchResult final {
    D3D9ExBootstrapPatchStatus status{
        D3D9ExBootstrapPatchStatus::unexpected_bytes};
    std::uint32_t system_error{};
    std::uint32_t thread_id{};
    std::uintptr_t existing_factory{};
    std::uintptr_t existing_device{};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return status == D3D9ExBootstrapPatchStatus::applied ||
               status == D3D9ExBootstrapPatchStatus::already_applied ||
               status == D3D9ExBootstrapPatchStatus::not_applicable;
    }
};

struct D3D9ExBootstrapRuntimeSnapshot final {
    bool patch_installed{};
    bool factory_ex_created{};
    bool device_ex_created{};
    bool texture_hooks_installed{};
    bool reset_hook_installed{};
    std::uint64_t factory_calls{};
    std::uint64_t create_device_calls{};
    std::uint64_t converted_static_textures{};
    std::int32_t last_factory_result{};
    std::int32_t last_create_device_result{};
};

using D3D9ExResetHandler = std::int32_t (*)(
    IDirect3DDevice9*, _D3DPRESENT_PARAMETERS_*) noexcept;

struct D3D9ExBridgeDeviceIdentity final {
    std::uintptr_t device{};
    std::uintptr_t vtable{};
    std::uint64_t generation{};
};

struct D3D9ExCompatibilitySlots final {
    std::uintptr_t reset{};
    std::uintptr_t create_texture{};
    std::uintptr_t create_volume_texture{};
    std::uintptr_t create_cube_texture{};
};

// A shared implementation vtable can be changed after the Ex device was
// published (for example by an overlay installing a later hook).  Readiness
// therefore proves the four permanent compatibility routes themselves, not
// merely the vtable address that originally contained them.
[[nodiscard]] constexpr bool d3d9ex_compatibility_slots_match(
    const D3D9ExCompatibilitySlots& observed,
    const D3D9ExCompatibilitySlots& required) noexcept {
    return required.reset != 0 && required.create_texture != 0 &&
           required.create_volume_texture != 0 &&
           required.create_cube_texture != 0 &&
           observed.reset == required.reset &&
           observed.create_texture == required.create_texture &&
           observed.create_volume_texture == required.create_volume_texture &&
           observed.create_cube_texture == required.create_cube_texture;
}

// An ordinary CreateDeviceEx failure, including D3DERR_NOTAVAILABLE, still
// gets COD4's legacy CreateDevice recovery. Only a successful Ex creation
// whose mandatory compatibility routes could not be installed is fail-closed.
[[nodiscard]] constexpr bool d3d9ex_should_try_legacy_device_fallback(
    const bool direct3d_available,
    const bool compatibility_install_failed) noexcept {
    return direct3d_available && !compatibility_install_failed;
}

// QueryInterface proves only that a candidate supports D3D9Ex. Shared-resource
// readiness additionally requires the exact device instance whose compatible
// creation path was completed and published by the bootstrap.
[[nodiscard]] constexpr bool d3d9ex_bridge_device_identity_matches(
    const D3D9ExBridgeDeviceIdentity& registered,
    const std::uintptr_t candidate_device,
    const std::uintptr_t candidate_vtable) noexcept {
    return registered.generation != 0 && registered.device != 0 &&
           registered.vtable != 0 &&
           registered.device == candidate_device &&
           registered.vtable == candidate_vtable;
}

enum class D3D9ExResetRoute : std::uint8_t {
    original_reset,
    direct_reset_ex,
    registered_handler,
};

[[nodiscard]] constexpr D3D9ExResetRoute select_d3d9ex_reset_route(
    const D3D9ExBridgeDeviceIdentity& registered,
    const std::uintptr_t candidate_device,
    const std::uintptr_t candidate_vtable,
    const bool handler_present,
    const std::uintptr_t handler_device,
    const std::uint64_t handler_generation) noexcept {
    if (!d3d9ex_bridge_device_identity_matches(
            registered, candidate_device, candidate_vtable)) {
        return D3D9ExResetRoute::original_reset;
    }
    return handler_present && handler_device == registered.device &&
                   handler_generation == registered.generation
        ? D3D9ExResetRoute::registered_handler
        : D3D9ExResetRoute::direct_reset_ex;
}

[[nodiscard]] const char* d3d9ex_bootstrap_patch_status_name(
    D3D9ExBootstrapPatchStatus status) noexcept;

[[nodiscard]] D3D9ExBootstrapRuntimeSnapshot
d3d9ex_bootstrap_runtime_snapshot() noexcept;

// True only for the exact device produced by the validated CreateDeviceEx
// path after the static managed-texture compatibility hooks were installed.
[[nodiscard]] bool d3d9ex_shared_bridge_ready(
    IDirect3DDevice9* device) noexcept;

// The bootstrap permanently owns Reset slot 16 for an Ex device. The later
// Present service registers lifecycle work through this route instead of
// stacking another vtable patch over it.
[[nodiscard]] bool register_d3d9ex_reset_handler(
    IDirect3DDevice9* device,
    D3D9ExResetHandler handler) noexcept;
void unregister_d3d9ex_reset_handler(
    IDirect3DDevice9* device,
    D3D9ExResetHandler handler) noexcept;

// These calls use the retained exact IDirect3DDevice9Ex interface directly;
// they never recurse through base-device Reset or perform QueryInterface.
[[nodiscard]] std::int32_t perform_d3d9ex_reset(
    IDirect3DDevice9* device,
    _D3DPRESENT_PARAMETERS_* parameters) noexcept;
[[nodiscard]] std::int32_t check_d3d9ex_device_state(
    IDirect3DDevice9* device,
    void* focus_window) noexcept;

#if defined(WAWVR_HAS_T4_BINDINGS)
[[nodiscard]] D3D9ExBootstrapPatchResult install_d3d9ex_bootstrap_patch(
    const wawvr::t4::ValidatedBindings& bindings) noexcept;
#endif

}  // namespace wawvr::mod
