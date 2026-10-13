#pragma once

#include "t4/bindings.hpp"

namespace wawvr::mod {

enum class TankReticleHookResult : unsigned char {
    installed,
    already_installed,
    restored,
    not_installed,
    unsupported_profile,
    unsupported_architecture,
    disabled_by_environment,
    target_mismatch,
    jump_out_of_range,
    thread_suspend_failed,
    patch_write_failed,
    foreign_patch_preserved,
};

// Only the native vehicle-crosshair call inside CG_DrawCrosshair is replaced.
// Other HUD elements and non-tank/flat rendering always retain the stock path.
[[nodiscard]] TankReticleHookResult install_tank_reticle_hook(
    const wawvr::t4::ValidatedBindings& bindings) noexcept;
void request_tank_reticle_hook_shutdown() noexcept;
[[nodiscard]] TankReticleHookResult restore_tank_reticle_hook() noexcept;
[[nodiscard]] bool tank_reticle_hook_installed() noexcept;
[[nodiscard]] const char* describe_tank_reticle_hook_result(
    TankReticleHookResult result) noexcept;

} // namespace wawvr::mod
