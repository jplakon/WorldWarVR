// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "t4/bindings.hpp"
#include "xr_types.h"

#include <array>
#include <cstdint>

namespace wawvr::mod {

// The gameplay refdef symbol owns the viewport prefix; origin and axis retain
// the same offsets in both independently mapped SP and MP layouts.
inline constexpr wawvr::t4::Rva kGameplayRefdefOriginOffset = 0x1C;
inline constexpr wawvr::t4::Rva kGameplayRefdefAxisOffset = 0x2C;
inline constexpr std::array<std::uint8_t, 5>
    kPostBuildUsercmdDisplacedInstruction{
        0xB9, 0x0E, 0x00, 0x00, 0x00,  // mov ecx, 14 dwords
    };

enum class InputHookStatus : std::uint8_t {
    installed,
    already_installed,
    disabled_by_environment,
    rejected_wrong_profile,
    unsupported_compiler_or_architecture,
    camera_axis_out_of_range,
    snap_turn_data_out_of_range,
    preparation_failed,
    unexpected_instruction_boundary,
    trampoline_allocation_failed,
    trampoline_protection_failed,
    trampoline_cache_flush_failed,
    jump_out_of_range,
    thread_snapshot_failed,
    thread_open_failed,
    thread_suspend_failed,
    thread_context_failed,
    target_thread_inside_patch,
    expected_bytes_changed,
    target_protection_failed,
    patch_write_failed,
    patch_cache_flush_failed,
    protection_restore_failed,
    rollback_failed,
};

struct InputHookInstallResult final {
    InputHookStatus status{InputHookStatus::preparation_failed};
    std::uint32_t system_error{};
    std::uintptr_t target{};
    std::uintptr_t trampoline{};

    [[nodiscard]] bool ok() const noexcept {
        return status == InputHookStatus::installed ||
               status == InputHookStatus::already_installed;
    }
};

[[nodiscard]] InputHookInstallResult install_controller_input_hook(
    const wawvr::t4::ValidatedBindings& bindings) noexcept;

[[nodiscard]] const char* input_hook_status_name(InputHookStatus status) noexcept;

// Shutdown is deliberately pass-through rather than an unsafe detach-time
// unpatch: the bridge continues the exact displaced instruction but no longer
// reads or mutates controller state.
void request_controller_input_shutdown() noexcept;
[[nodiscard]] bool controller_input_hook_installed() noexcept;
[[nodiscard]] bool controller_input_hook_enabled() noexcept;

// Exact-profile SP vehicle driver + native tracked-tank identity, not merely
// weapon==0 or a map name. Shared with the menu/stance input producer.
[[nodiscard]] bool controller_tank_controls_active() noexcept;

// Read the native vehicle weapon trace endpoint while a verified local tank
// is owned. No HMD/controller pose contributes to this world-space target.
[[nodiscard]] bool read_controller_tank_aim_target(
    wawvr::xr::Vec3f* target_world) noexcept;

}  // namespace wawvr::mod
