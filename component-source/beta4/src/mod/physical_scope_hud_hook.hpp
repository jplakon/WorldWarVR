#pragma once

#include "t4/bindings.hpp"

namespace wawvr::mod {

enum class PhysicalScopeHudHookResult : unsigned char {
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

// Suppresses only the native scope overlay and its HUD-visibility predicates
// while the validated physical optic owns current-frame stereo presentation.
// Other callers of the native zoom predicate, including weapon motion and
// viewmodel visibility, retain their original behavior.
[[nodiscard]] PhysicalScopeHudHookResult install_physical_scope_hud_hook(
    const wawvr::t4::ValidatedBindings& bindings) noexcept;
void request_physical_scope_hud_hook_shutdown() noexcept;
[[nodiscard]] PhysicalScopeHudHookResult restore_physical_scope_hud_hook() noexcept;
// True only after all three callsites are installed and suppression is enabled.
[[nodiscard]] bool physical_scope_hud_hook_installed() noexcept;
[[nodiscard]] const char* describe_physical_scope_hud_hook_result(
    PhysicalScopeHudHookResult result) noexcept;

} // namespace wawvr::mod
