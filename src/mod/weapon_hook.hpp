// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "t4/bindings.hpp"
#include "weapon_frame_base_phase_logic.hpp"
#include "xr_types.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace wawvr::mod {

inline constexpr wawvr::t4::Rva kCgAddPlayerWeaponRva = 0x000697A0;
inline constexpr wawvr::t4::Rva kGameplayRefdefOriginRva = 0x03120354;
inline constexpr wawvr::t4::Rva kGameplayRefdefAxisRvaForWeapon = 0x03120364;

enum class WeaponHookStatus : std::uint8_t {
    installed,
    already_installed,
    disabled_by_environment,
    rejected_wrong_profile,
    input_dependency_unavailable,
    preparation_failed,
    address_out_of_range,
    original_target_mismatch,
    original_sentinel_mismatch,
    jump_out_of_range,
    thread_suspend_failed,
    target_protection_failed,
    expected_bytes_changed,
    patch_write_failed,
    patch_cache_flush_failed,
    protection_restore_failed,
};

struct WeaponHookInstallResult final {
    WeaponHookStatus status{WeaponHookStatus::preparation_failed};
    std::uint32_t system_error{};
    std::uintptr_t target{};
    std::uintptr_t original{};
    std::uintptr_t ballistics_target{};
    std::uintptr_t ballistics_original{};
    std::uintptr_t spread_target{};
    std::uintptr_t spread_original{};
    std::uintptr_t client_effects_target{};
    std::uintptr_t client_effects_original{};
    std::uintptr_t client_spread_target{};
    std::uintptr_t client_spread_original{};

    [[nodiscard]] bool ok() const noexcept {
        return status == WeaponHookStatus::installed ||
               status == WeaponHookStatus::already_installed;
    }
};

enum class CampaignTargetingHookStatus : std::uint8_t {
    installed,
    already_installed,
    not_applicable,
    dependency_unavailable,
    rejected_wrong_profile,
    preparation_failed,
    address_out_of_range,
    original_target_mismatch,
    original_sentinel_mismatch,
    jump_out_of_range,
    thread_suspend_failed,
    target_protection_failed,
    expected_bytes_changed,
    patch_write_failed,
    patch_cache_flush_failed,
    protection_restore_failed,
};

struct CampaignTargetingHookInstallResult final {
    CampaignTargetingHookStatus status{
        CampaignTargetingHookStatus::preparation_failed};
    std::uint32_t system_error{};
    std::uintptr_t target{};
    std::uintptr_t original{};

    [[nodiscard]] bool ok() const noexcept {
        return status == CampaignTargetingHookStatus::installed ||
               status == CampaignTargetingHookStatus::already_installed ||
               status == CampaignTargetingHookStatus::not_applicable;
    }
};

[[nodiscard]] WeaponHookInstallResult install_weapon_viewmodel_hook(
    const wawvr::t4::ValidatedBindings& bindings) noexcept;

// SP-only script-angle substitution for exact rocket_barrage and air_support
// designators. This patches only the GScr_GetPlayerAngles -> Scr_AddVector call;
// it never writes native player, camera, or HMD angles.
[[nodiscard]] CampaignTargetingHookInstallResult
install_campaign_rocket_targeting_hook(
    const wawvr::t4::ValidatedBindings& bindings) noexcept;

[[nodiscard]] const char* weapon_hook_status_name(
    WeaponHookStatus status) noexcept;

[[nodiscard]] const char* campaign_targeting_hook_status_name(
    CampaignTargetingHookStatus status) noexcept;

inline constexpr std::size_t kRuntimeWeaponNameCapacity = 64;

// Owned snapshot of one registered SP WeaponDef identity. T4's table and name
// storage remain engine-owned, so callers never retain borrowed pointers.
struct RuntimeWeaponDefinitionIdentity final {
    std::int32_t weapon_index{};
    std::uint32_t registered_count{};
    std::uint32_t definition_address{};
    std::array<char, kRuntimeWeaponNameCapacity> name{};
};

[[nodiscard]] bool read_runtime_sp_weapon_definition(
    std::int32_t weapon_index,
    RuntimeWeaponDefinitionIdentity* identity) noexcept;

[[nodiscard]] bool find_runtime_sp_weapon_definition(
    const void* weapon_definition,
    RuntimeWeaponDefinitionIdentity* identity) noexcept;

// Returns the most recent final visible weapon ray when it is a fresh render
// from this controller stream. T4 builds the next usercmd before rendering it,
// so exact generation equality would reject the normal one-frame-old result.
[[nodiscard]] bool read_final_visible_weapon_aim(
    std::uint64_t controller_generation,
    std::uint64_t now_milliseconds,
    float* pitch_degrees,
    float* yaw_degrees) noexcept;

// Immutable receipt for the weapon root written into one exact OpenXR-backed
// T4 render. Present captures it alongside the matching pixel generation so a
// later deferred GPU acquisition can distinguish a live valid pose from an
// intentionally retained pose without touching placement behavior.
struct FinalVisibleWeaponReceipt final {
    std::uint64_t controller_generation{};
    std::uint64_t frame_id{};
    std::uint64_t action_sequence{};
    std::uint64_t publication_milliseconds{};
    bool live_controller_pose{};
};

[[nodiscard]] bool read_final_visible_weapon_receipt(
    std::uint64_t expected_frame_id,
    FinalVisibleWeaponReceipt* receipt) noexcept;

// Returns the post-T4 weapon placement base only for the exact requested XR
// frame. Stereo may compare it with its own later scene sample without
// retaining any engine-owned storage.
[[nodiscard]] bool read_weapon_frame_base_receipt(
    std::uint64_t expected_frame_id,
    WeaponFrameBaseReceipt* receipt) noexcept;

// Returns the corrected viewmodel tag_flash only when it was published by a
// recent render from the caller's controller stream.
[[nodiscard]] bool read_published_weapon_muzzle(
    std::uint64_t controller_generation,
    std::uint64_t now_milliseconds,
    wawvr::xr::Vec3f* origin) noexcept;

// Returns the same atomic tag_flash publication together with the evaluated
// grip-to-flash barrel basis used by type-two projectile weapons.
[[nodiscard]] bool read_published_projectile_pose(
    std::uint64_t controller_generation,
    std::uint64_t now_milliseconds,
    wawvr::xr::Vec3f* origin,
    wawvr::xr::Basis3f* projectile_basis) noexcept;

// Reads an already evaluated viewmodel bone/tag through T4's validated world-
// tag helper. Tracked-hand rendering uses this to latch a standalone glove to
// the authored wrist position without exposing the opposite stock arm.
[[nodiscard]] bool read_viewmodel_world_tag_position(
    void* viewmodel_dobj,
    std::uint16_t tag,
    const void* viewmodel_pose,
    wawvr::xr::Vec3f* world_position) noexcept;

// Read-only evaluated bone pose for baking a private gripping glove. Does not
// write native skeleton matrices or alter the weapon/root pose.
[[nodiscard]] bool read_viewmodel_world_tag_pose(
    void* viewmodel_dobj,
    std::uint16_t tag,
    const void* viewmodel_pose,
    wawvr::xr::EnginePose* world_pose) noexcept;

[[nodiscard]] bool controller_weapon_uses_support_pose() noexcept;

enum class ControllerPhysicalScopeIdentity : std::uint8_t {
    unavailable,
    unscoped,
    physical_scope,
};

[[nodiscard]] ControllerPhysicalScopeIdentity
classify_controller_weapon_physical_scope(
    std::int32_t weapon_index) noexcept;

void request_weapon_hook_shutdown() noexcept;
[[nodiscard]] bool weapon_viewmodel_hook_installed() noexcept;
[[nodiscard]] bool weapon_viewmodel_hook_enabled() noexcept;

}  // namespace wawvr::mod
