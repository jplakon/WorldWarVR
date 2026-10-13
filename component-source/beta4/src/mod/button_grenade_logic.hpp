// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <string_view>

namespace wawvr::mod {

inline constexpr wchar_t kButtonGrenadesEnvironmentVariable[] =
    L"WAWVR_BUTTON_GRENADES";

// Runtime settings use an explicit child-process 1/0 contract. Missing,
// empty, and malformed values preserve the default physical grenade behavior.
[[nodiscard]] bool button_grenade_setting_enabled(
    std::wstring_view value) noexcept;

struct ButtonGrenadeTriggerState final {
    bool trigger_held{};
    // A trigger press rejected for weapon-action ownership must be physically
    // released before it may begin a grenade hold after that ownership clears.
    bool new_press_rearm_required{};
};

// Maps the left index trigger to WaW's native frag-grenade button. Unlike the
// physical grenade interaction this path intentionally has no hand-pose or
// belt-volume input. A temporary input gap preserves an existing native hold
// so lost tracking cannot manufacture a throw; a positively inactive gameplay
// session always releases ownership without injecting another command.
[[nodiscard]] bool update_button_grenade_trigger(
    bool gameplay_session_inactive,
    bool input_valid,
    float left_trigger_value,
    bool new_press_blocked,
    ButtonGrenadeTriggerState* state,
    bool* hold_native_frag_button) noexcept;

// While the VR trigger owns the command it replaces both native offhand bits
// with frag so simultaneous desktop tactical input cannot submit an ambiguous
// pair. Idle controller input is a true pass-through that preserves every
// native keyboard/mouse bit.
[[nodiscard]] std::uint32_t merge_button_grenade_native_hold(
    std::uint32_t native_buttons,
    std::uint32_t all_offhand_button_mask,
    std::uint32_t frag_button_mask,
    bool hold_native_frag_button) noexcept;

}  // namespace wawvr::mod
