// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "input_mapping.hpp"
#include "menu_input_logic.hpp"
#include "menu_surface_logic.hpp"
#include "native_gameplay_command_logic.hpp"
#include "present_hook_logic.hpp"

#include "t4/bindings.hpp"
#include "xr_types.h"

#include <cstdint>

namespace wawvr::mod {

inline constexpr std::int32_t kT4KeyEnter = 0x0D;
inline constexpr std::int32_t kT4KeyEscape = 0x1B;
inline constexpr std::int32_t kT4KeyUpArrow = 0x9A;
inline constexpr std::int32_t kT4KeyDownArrow = 0x9B;
inline constexpr std::int32_t kT4KeyLeftArrow = 0x9C;
inline constexpr std::int32_t kT4KeyRightArrow = 0x9D;
inline constexpr std::int32_t kT4KeyMouse1 = 0xC8;

struct T4MenuInputState final {
    MenuNavigationState navigation{};
    NativeGameplayCommandState native_commands{};
    DirectionalActionState directional_actions{};
    MenuPointerTriggerState pointer_trigger{};
};

struct T4MenuInputServiceResult final {
    bool state_valid{};
    bool ui_active{};
    bool cursor_submitted{};
    bool pointer_submitted{};
    bool confirm_tapped{};
    bool pointer_confirm_tapped{};
    bool confirm_via_enter{};
    bool back_tapped{};
    bool menu_button_tapped{};
    bool weapon_next_queued{};
    bool stance_command_queued{};
    DirectionalAction stance_action{DirectionalAction::none};
    bool campaign_unlock_queued{};
    bool pezbot_autofill_queued{};
    bool simulator_kar98_queued{};
    std::int32_t mission_key_tapped{};
};

// CL_KeyEvent + UI_MouseEvent form the mandatory native-menu capability.
// Cbuf_AddText + the weapnext identity form an independent optional gameplay
// convenience and cannot prevent the mandatory pair from binding.
[[nodiscard]] bool bind_t4_menu_input(
    const wawvr::t4::ValidatedBindings& bindings) noexcept;
void clear_t4_menu_input() noexcept;

// True only during the post-handoff inventory phase in which the single-player
// OpenXR simulator has loaded the campaign zone requested by the exact
// detachable-magazine selector. This lets diagnostics inspect a precached
// weapon asset without affecting normal headset or retail launches.
[[nodiscard]] bool
simulator_requested_magazine_asset_inventory_ready() noexcept;

// Must be called only from the validated WinMain post-Com_Frame boundary.
// OpenXR actions are sampled there; no engine UI function is called by a
// Present callback or controller polling thread.
[[nodiscard]] T4MenuInputServiceResult service_t4_menu_input_after_com_frame(
    const wawvr::xr::FrameState& frame,
    std::uint32_t full_backbuffer_width,
    std::uint32_t full_backbuffer_height,
    ActiveUiMonoSource active_ui_source,
    const MenuPointerSurface* visible_menu_surface,
    bool menu_escape_tap_requested,
    std::uint64_t now_milliseconds,
    T4MenuInputState* state) noexcept;

}  // namespace wawvr::mod
