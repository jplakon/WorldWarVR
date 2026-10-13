// SPDX-License-Identifier: GPL-3.0-only
#include "satchel_input_logic.hpp"

#include "manual_grenade_logic.hpp"

#include <algorithm>
#include <cmath>

namespace wawvr::mod {

SatchelWeaponKind satchel_weapon_kind(
    const bool multiplayer, const std::string_view weapon_name) noexcept {
    if (multiplayer) {
        return SatchelWeaponKind::none;
    }
    if (weapon_name == "satchel_charge") {
        return SatchelWeaponKind::satchel_charge;
    }
    if (weapon_name == "satchel_charge_new") {
        return SatchelWeaponKind::satchel_charge_new;
    }
    return SatchelWeaponKind::none;
}

SatchelInputResult update_satchel_input(
    const SatchelInput& input, SatchelInputState* const state) noexcept {
    if (state == nullptr) {
        return {};
    }
    const auto kind = satchel_weapon_kind(input.multiplayer, input.weapon_name);
    if (input.gameplay_session_inactive || !input.weapon_context_valid ||
        input.weapon_index == 0U || input.weapon_definition_identity == 0U ||
        kind == SatchelWeaponKind::none) {
        *state = {};
        return {};
    }

    const bool identity_changed = state->weapon_index != input.weapon_index ||
        state->weapon_definition_identity != input.weapon_definition_identity ||
        state->weapon_kind != kind;
    if (identity_changed) {
        *state = {};
        state->weapon_index = input.weapon_index;
        state->weapon_definition_identity = input.weapon_definition_identity;
        state->weapon_kind = kind;
        state->new_press_rearm_required = true;
    }

    SatchelInputResult result{
        .hold_native_throw = state->trigger_held,
        .reserve_left_trigger = true,
    };
    if (!input.input_valid || !std::isfinite(input.left_trigger_value)) {
        // Do not turn lost focus/tracking into a native throw release. An idle
        // trigger must instead be rearmed on the next valid physical release.
        if (!state->trigger_held) {
            state->new_press_rearm_required = true;
        }
        return result;
    }

    const float trigger = std::clamp(input.left_trigger_value, 0.0F, 1.0F);
    if (state->trigger_held) {
        // Releasing the right grip cannot stick or synthetically release a
        // satchel already held by the left trigger.
        state->trigger_held = trigger > kManualGrenadeTriggerRelease;
    } else if (state->new_press_rearm_required) {
        state->new_press_rearm_required =
            trigger > kManualGrenadeTriggerRelease;
    } else if (input.new_press_blocked || !input.right_grip_held) {
        state->new_press_rearm_required =
            trigger >= kManualGrenadeTriggerEngage;
    } else {
        state->trigger_held = trigger >= kManualGrenadeTriggerEngage;
    }
    result.hold_native_throw = state->trigger_held;
    return result;
}

std::uint32_t merge_satchel_native_throw(
    const std::uint32_t native_buttons, const bool hold_native_throw) noexcept {
    return hold_native_throw
        ? native_buttons | kSatchelNativeThrowButton
        : native_buttons;
}

}  // namespace wawvr::mod
