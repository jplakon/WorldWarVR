// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "controller_state.hpp"

#include "t4/bindings.hpp"
#include "xr_types.h"

#include <cstdint>

namespace wawvr::mod {

enum class ManualGrenadeRuntimeStatus : std::uint8_t {
    installed,
    already_installed,
    not_applicable,
    dependency_unavailable,
    rejected_wrong_profile,
    address_out_of_range,
    fingerprint_mismatch,
    thread_suspend_failed,
    patch_failed,
};

struct ManualGrenadeRuntimeInstallResult final {
    ManualGrenadeRuntimeStatus status{
        ManualGrenadeRuntimeStatus::dependency_unavailable};
    std::uint32_t system_error{};

    [[nodiscard]] bool ok() const noexcept {
        return status == ManualGrenadeRuntimeStatus::installed ||
               status == ManualGrenadeRuntimeStatus::already_installed ||
               status == ManualGrenadeRuntimeStatus::not_applicable;
    }
};

struct ManualGrenadeCommandUpdate final {
    bool interaction_active{};
    bool grenade_held{};
    bool native_button_injected{};
    bool left_hand_reserved{};
};

// Connection state is deliberately separate from XR focus/input ownership.
// Only a positively identified inactive engine session may tear down a cooked
// grenade; missing tracking or an unreadable connection word must preserve the
// native hold so it cannot become a synthetic button-up throw.
enum class ManualGrenadeGameplaySession : std::uint8_t {
    unknown,
    active,
    inactive,
};

[[nodiscard]] ManualGrenadeRuntimeInstallResult install_manual_grenade_runtime(
    const wawvr::t4::ValidatedBindings& bindings) noexcept;

// True only when the explicit WAWVR_BUTTON_GRENADES=1 launch contract selected
// the native button path. The physical belt interaction remains the default.
[[nodiscard]] bool button_grenade_mode_enabled() noexcept;

// Runs at the validated post-CL_CreateCmd seam. In the default physical mode,
// the left index trigger may begin a belt grab only on a fresh edge. In button
// mode it directly holds WaW's native frag command and does not reserve either
// hand or consume any pose/belt state. T4 continues to own inventory, pin/cook
// timing, sounds, and script events in both modes.
[[nodiscard]] ManualGrenadeCommandUpdate update_manual_grenade_command(
    const ControllerFrameSnapshot& controller,
    const wawvr::xr::Vec3f& camera_origin,
    const wawvr::xr::Basis3f& camera_axis,
    ManualGrenadeGameplaySession gameplay_session,
    bool input_owned,
    bool new_grab_blocked,
    std::uint8_t native_offhand_index,
    std::uint32_t* buttons) noexcept;

// The renderer publishes the exact native offhand identity after prediction
// selects it. This resolves the projectile model without guessing asset names.
void manual_grenade_observe_player_state(const void* player_state) noexcept;

// True while the physical grenade transaction owns (or is waiting to rearm)
// the left hand. Weapon grip arbitration uses this to prevent a simultaneous
// rifle-support latch.
[[nodiscard]] bool manual_grenade_reserves_left_hand() noexcept;

// Keeps the ordinary firearm selected while T4's native offhand state is
// active, plus a short post-spawn recovery interval that avoids a one-frame
// canned-grenade viewmodel flash.
[[nodiscard]] bool manual_grenade_view_override_active() noexcept;

// Returns the standalone projectile model and its most recent tracked palm
// pose only while the grenade is physically held.
[[nodiscard]] bool read_manual_grenade_render_state(
    void** projectile_model,
    wawvr::xr::EnginePose* world_pose) noexcept;

void request_manual_grenade_shutdown() noexcept;

[[nodiscard]] const char* manual_grenade_runtime_status_name(
    ManualGrenadeRuntimeStatus status) noexcept;

}  // namespace wawvr::mod
