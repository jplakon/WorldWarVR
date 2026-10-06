// SPDX-License-Identifier: GPL-3.0-only
#include "button_grenade_logic.hpp"

#include "manual_grenade_logic.hpp"

#include <algorithm>
#include <cmath>

namespace wawvr::mod {

bool button_grenade_setting_enabled(
    const std::wstring_view value) noexcept {
    return value == L"1";
}

bool update_button_grenade_trigger(
    const bool gameplay_session_inactive,
    const bool input_valid,
    const float left_trigger_value,
    const bool new_press_blocked,
    ButtonGrenadeTriggerState* const state,
    bool* const hold_native_frag_button) noexcept {
    if (state == nullptr || hold_native_frag_button == nullptr) {
        return false;
    }

    if (gameplay_session_inactive) {
        *state = {};
        *hold_native_frag_button = false;
        return true;
    }

    *hold_native_frag_button = state->trigger_held;
    if (!input_valid) {
        return true;
    }
    if (!std::isfinite(left_trigger_value)) {
        return false;
    }

    const float clamped = std::clamp(left_trigger_value, 0.0F, 1.0F);
    if (state->trigger_held) {
        // Ownership changes never stick an already-held native grenade. Its
        // release continues to follow the physical trigger.
        state->trigger_held = clamped > kManualGrenadeTriggerRelease;
    } else if (state->new_press_rearm_required) {
        // Clearing a weapon-action reservation under a continuously squeezed
        // trigger is not a fresh grenade press.
        state->new_press_rearm_required =
            clamped > kManualGrenadeTriggerRelease;
    } else if (new_press_blocked) {
        state->new_press_rearm_required =
            clamped >= kManualGrenadeTriggerEngage;
    } else {
        state->trigger_held =
            clamped >= kManualGrenadeTriggerEngage;
    }
    *hold_native_frag_button = state->trigger_held;
    return true;
}

std::uint32_t merge_button_grenade_native_hold(
    const std::uint32_t native_buttons,
    const std::uint32_t all_offhand_button_mask,
    const std::uint32_t frag_button_mask,
    const bool hold_native_frag_button) noexcept {
    return hold_native_frag_button
        ? (native_buttons & ~all_offhand_button_mask) | frag_button_mask
        : native_buttons;
}

}  // namespace wawvr::mod
