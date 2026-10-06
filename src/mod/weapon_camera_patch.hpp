#pragma once

#include "t4/bindings.hpp"

#include <array>
#include <cstdint>

namespace wawvr::mod {

inline constexpr std::array<std::uint8_t, 5> kWeaponCameraTagMatrixCall{
    0xE8, 0x5E, 0x52, 0x01, 0x00,
};

// `xor eax,eax; nop; nop; nop` makes the existing caller take its native
// "tag not found" path. The caller still cleans the three arguments it pushed,
// so no proprietary ABI bridge is needed.
inline constexpr std::array<std::uint8_t, 5> kSuppressWeaponCameraTagMatrix{
    0x31, 0xC0, 0x90, 0x90, 0x90,
};

// AimAssist_UpdateGamePadInput's auto-melee-enabled branch normally enters
// target pitch/yaw adjustment. The replacement jumps to that function's own
// clear/reset epilogue regardless of the cheat dvar, then pads the sixth byte.
inline constexpr std::array<std::uint8_t, 6> kAutoMeleeEnabledBranch{
    0x0F, 0x84, 0x03, 0x02, 0x00, 0x00,
};
inline constexpr std::array<std::uint8_t, 6> kSuppressAutoMeleeTargeting{
    0xE9, 0x04, 0x02, 0x00, 0x00, 0x90,
};

enum class WeaponCameraPatchStatus : std::uint8_t {
    applied,
    already_applied,
    restored,
    already_restored,
    not_installed,
    disabled_by_environment,
    frame_boundary_unavailable,
    rejected_wrong_profile,
    address_out_of_range,
    context_sentinel_mismatch,
    thread_suspend_failed,
    target_protection_failed,
    expected_bytes_changed,
    patch_write_failed,
    patch_cache_flush_failed,
    protection_restore_failed,
    foreign_patch_preserved,
};

struct WeaponCameraPatchResult final {
    WeaponCameraPatchStatus status{WeaponCameraPatchStatus::not_installed};
    std::uintptr_t target{};
    std::uint32_t system_error{};

    [[nodiscard]] bool ok() const noexcept {
        return status == WeaponCameraPatchStatus::applied ||
               status == WeaponCameraPatchStatus::already_applied ||
               status == WeaponCameraPatchStatus::restored ||
               status == WeaponCameraPatchStatus::already_restored ||
               status == WeaponCameraPatchStatus::not_installed;
    }
};

enum class MeleeCameraPatchStatus : std::uint8_t {
    applied,
    already_applied,
    restored,
    already_restored,
    not_installed,
    disabled_by_environment,
    rejected_wrong_profile,
    address_out_of_range,
    context_sentinel_mismatch,
    thread_suspend_failed,
    target_protection_failed,
    expected_bytes_changed,
    patch_write_failed,
    patch_cache_flush_failed,
    protection_restore_failed,
    foreign_patch_preserved,
};

struct MeleeCameraPatchResult final {
    MeleeCameraPatchStatus status{MeleeCameraPatchStatus::not_installed};
    std::uintptr_t target{};
    std::uint32_t system_error{};

    [[nodiscard]] bool ok() const noexcept {
        return status == MeleeCameraPatchStatus::applied ||
               status == MeleeCameraPatchStatus::already_applied ||
               status == MeleeCameraPatchStatus::restored ||
               status == MeleeCameraPatchStatus::already_restored ||
               status == MeleeCameraPatchStatus::not_installed;
    }
};

// Removes only CG_ApplyViewAnimation's tag_camera -> refdef mutation. Native
// viewmodel pose updates still run, preserving reload and weapon animation.
[[nodiscard]] WeaponCameraPatchResult install_weapon_camera_patch(
    const wawvr::t4::ValidatedBindings& bindings) noexcept;

// Reversible process-lifetime patch. A byte pattern owned by another module is
// never overwritten during restoration.
[[nodiscard]] WeaponCameraPatchResult restore_weapon_camera_patch() noexcept;

[[nodiscard]] bool weapon_camera_patch_installed() noexcept;
[[nodiscard]] const char* weapon_camera_patch_status_name(
    WeaponCameraPatchStatus status) noexcept;

// Bypasses only T4's target-driven auto-melee aim block. The post-build input
// bridge separately clears charge yaw/distance, while the melee button,
// animation, trace, damage, and native clear epilogue remain intact.
[[nodiscard]] MeleeCameraPatchResult install_melee_camera_patch(
    const wawvr::t4::ValidatedBindings& bindings) noexcept;
[[nodiscard]] MeleeCameraPatchResult restore_melee_camera_patch() noexcept;
[[nodiscard]] bool melee_camera_patch_installed() noexcept;
[[nodiscard]] const char* melee_camera_patch_status_name(
    MeleeCameraPatchStatus status) noexcept;

}  // namespace wawvr::mod
