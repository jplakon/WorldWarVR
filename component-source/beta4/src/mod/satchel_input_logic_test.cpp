// SPDX-License-Identifier: GPL-3.0-only
#include "satchel_input_logic.hpp"

#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <limits>

namespace {

using wawvr::mod::SatchelInput;
using wawvr::mod::SatchelInputResult;
using wawvr::mod::SatchelInputState;
using wawvr::mod::update_satchel_input;

void require(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

SatchelInput selected_satchel() {
    return SatchelInput{
        .weapon_context_valid = true,
        .weapon_index = 19U,
        .weapon_definition_identity = 0x123400U,
        .weapon_name = "satchel_charge",
        .input_valid = true,
        .right_grip_held = true,
    };
}

void start_hold(SatchelInput* const input, SatchelInputState* const state) {
    input->left_trigger_value = 0.0F;
    require(!update_satchel_input(*input, state).hold_native_throw,
            "released selected trigger arms without throwing");
    input->left_trigger_value = 1.0F;
    require(update_satchel_input(*input, state).hold_native_throw,
            "fresh trigger press starts a satchel hold");
}

void test_exact_single_player_weapon_names_only() {
    using wawvr::mod::SatchelWeaponKind;
    using wawvr::mod::satchel_weapon_kind;
    require(satchel_weapon_kind(false, "satchel_charge") ==
                SatchelWeaponKind::satchel_charge &&
                satchel_weapon_kind(false, "satchel_charge_new") ==
                SatchelWeaponKind::satchel_charge_new,
            "both exact single-player satchel names are supported");
    for (const auto name : {"", "satchel_charge_mp", "satchel_charge_extra",
                            "SATCHEL_CHARGE", "m1carbine", "fraggrenade"}) {
        require(satchel_weapon_kind(false, name) == SatchelWeaponKind::none,
                "unrelated and approximate names do not acquire satchel input");
    }
    require(satchel_weapon_kind(true, "satchel_charge") ==
                SatchelWeaponKind::none &&
                satchel_weapon_kind(true, "satchel_charge_new") ==
                SatchelWeaponKind::none,
            "multiplayer never acquires this single-player route");
}

void test_selection_held_trigger_requires_release_and_fresh_press() {
    auto input = selected_satchel();
    SatchelInputState state{};
    input.left_trigger_value = 1.0F;
    auto result = update_satchel_input(input, &state);
    require(result.reserve_left_trigger && !result.hold_native_throw &&
                state.new_press_rearm_required,
            "selecting a satchel under held trigger reserves it without throwing");
    require(!update_satchel_input(input, &state).hold_native_throw,
            "a held trigger is not converted into a press on the next frame");
    input.left_trigger_value = 0.36F;
    require(!update_satchel_input(input, &state).hold_native_throw &&
                state.new_press_rearm_required,
            "partial release above the threshold cannot rearm selection");
    input.left_trigger_value = 0.35F;
    require(!update_satchel_input(input, &state).hold_native_throw &&
                !state.new_press_rearm_required,
            "valid physical release rearms selection");
    input.left_trigger_value = 0.69F;
    require(!update_satchel_input(input, &state).hold_native_throw,
            "below-engage trigger does not begin native placement");
    input.left_trigger_value = 0.70F;
    require(update_satchel_input(input, &state).hold_native_throw,
            "fresh engage threshold begins native placement");
    input.left_trigger_value = 0.36F;
    require(update_satchel_input(input, &state).hold_native_throw,
            "hysteresis preserves the held satchel");
    input.left_trigger_value = 0.35F;
    require(!update_satchel_input(input, &state).hold_native_throw,
            "physical trigger release ends placement hold");
}

void test_focus_gap_preserves_hold_but_cannot_manufacture_new_press() {
    auto input = selected_satchel();
    SatchelInputState state{};
    start_hold(&input, &state);
    input.input_valid = false;
    input.left_trigger_value = 0.0F;
    input.right_grip_held = false;
    auto result = update_satchel_input(input, &state);
    require(result.hold_native_throw && result.reserve_left_trigger,
            "stale focus and neutral synthetic inputs cannot release an owned satchel");
    input.input_valid = true;
    require(!update_satchel_input(input, &state).hold_native_throw,
            "a valid physical release ends the hold even with right grip released");
    input.input_valid = false;
    input.left_trigger_value = 1.0F;
    require(!update_satchel_input(input, &state).hold_native_throw,
            "stale input cannot start a throw");
    input.input_valid = true;
    input.right_grip_held = true;
    require(!update_satchel_input(input, &state).hold_native_throw &&
                state.new_press_rearm_required,
            "focus recovery under held trigger requires physical rearming");
    start_hold(&input, &state);
}

void test_right_grip_and_existing_hand_ownership_gate_only_new_presses() {
    for (const bool blocked_by_grip : {false, true}) {
        auto input = selected_satchel();
        SatchelInputState state{};
        (void)update_satchel_input(input, &state);
        input.right_grip_held = !blocked_by_grip;
        input.new_press_blocked = !blocked_by_grip;
        input.left_trigger_value = 1.0F;
        auto result = update_satchel_input(input, &state);
        require(!result.hold_native_throw && result.reserve_left_trigger &&
                    state.new_press_rearm_required,
                "ungripped or already-owned hand consumes a rejected press edge");
        input.right_grip_held = true;
        input.new_press_blocked = false;
        require(!update_satchel_input(input, &state).hold_native_throw,
                "clearing a new-press gate cannot invent a held-trigger throw");
        start_hold(&input, &state);
        input.right_grip_held = false;
        input.new_press_blocked = true;
        require(update_satchel_input(input, &state).hold_native_throw,
                "ownership changes cannot cancel an existing hold");
        input.left_trigger_value = 0.0F;
        require(!update_satchel_input(input, &state).hold_native_throw,
                "owned throw can release while all new-press gates are blocked");
    }
}

void test_weapon_identity_changes_cannot_inherit_held_throw() {
    for (int change = 0; change < 3; ++change) {
        auto input = selected_satchel();
        SatchelInputState state{};
        start_hold(&input, &state);
        if (change == 0) {
            ++input.weapon_index;
        } else if (change == 1) {
            ++input.weapon_definition_identity;
        } else {
            input.weapon_name = "satchel_charge_new";
        }
        const auto result = update_satchel_input(input, &state);
        require(!result.hold_native_throw && result.reserve_left_trigger &&
                    state.new_press_rearm_required,
                "index, definition and satchel variant changes discard old holds");
        start_hold(&input, &state);
    }
}

void test_inactive_other_weapon_and_unknown_context_reset() {
    for (int change = 0; change < 6; ++change) {
        auto input = selected_satchel();
        SatchelInputState state{};
        start_hold(&input, &state);
        switch (change) {
        case 0: input.gameplay_session_inactive = true; break;
        case 1: input.weapon_context_valid = false; break;
        case 2: input.weapon_index = 0U; break;
        case 3: input.weapon_definition_identity = 0U; break;
        case 4: input.weapon_name = "m1carbine"; break;
        case 5: input.multiplayer = true; break;
        }
        input.input_valid = false;
        const auto result = update_satchel_input(input, &state);
        require(!result.hold_native_throw && !result.reserve_left_trigger &&
                    state.weapon_index == 0U && !state.trigger_held,
                "positive inactivity or unvalidated/different weapon context cannot receive a stale satchel bit");
        input = selected_satchel();
        input.left_trigger_value = 1.0F;
        require(!update_satchel_input(input, &state).hold_native_throw,
                "returning to satchel requires release after a reset");
    }
}

void test_nonfinite_input_and_null_state_fail_without_inventing_release() {
    auto input = selected_satchel();
    SatchelInputState state{};
    start_hold(&input, &state);
    for (const float invalid : {(std::numeric_limits<float>::quiet_NaN)(),
                                (std::numeric_limits<float>::infinity)()}) {
        input.left_trigger_value = invalid;
        require(update_satchel_input(input, &state).hold_native_throw,
                "invalid action data preserves an owned hold for a valid identity");
    }
    input.left_trigger_value = -1.0F;
    require(!update_satchel_input(input, &state).hold_native_throw,
            "finite below-range input clamps to physical release");
    input.left_trigger_value = 2.0F;
    require(update_satchel_input(input, &state).hold_native_throw,
            "finite above-range input clamps to engage");
    const auto result = update_satchel_input(input, nullptr);
    require(!result.hold_native_throw && !result.reserve_left_trigger,
            "missing state never owns native input");
}

void test_native_merge_preserves_attack_and_desktop_bits() {
    using wawvr::mod::kSatchelNativeThrowButton;
    using wawvr::mod::merge_satchel_native_throw;
    constexpr std::uint32_t attack = 1U;
    constexpr std::uint32_t other = 0x8008C000U;
    require(kSatchelNativeThrowButton == 0x00400000U,
            "retail T4 satchel bit must not regress to the IW3 secondary bit");
    require(merge_satchel_native_throw(attack | other, true) ==
                (attack | other | kSatchelNativeThrowButton),
            "placement adds its bit without remapping detonation or other native input");
    require(merge_satchel_native_throw(attack | other, false) == (attack | other),
            "controller release leaves all original keyboard bits intact");
    require(merge_satchel_native_throw(
                attack | kSatchelNativeThrowButton, false) ==
                (attack | kSatchelNativeThrowButton),
            "controller release cannot clear a keyboard-owned satchel throw");
}

}  // namespace

int main() {
    test_exact_single_player_weapon_names_only();
    test_selection_held_trigger_requires_release_and_fresh_press();
    test_focus_gap_preserves_hold_but_cannot_manufacture_new_press();
    test_right_grip_and_existing_hand_ownership_gate_only_new_presses();
    test_weapon_identity_changes_cannot_inherit_held_throw();
    test_inactive_other_weapon_and_unknown_context_reset();
    test_nonfinite_input_and_null_state_fail_without_inventing_release();
    test_native_merge_preserves_attack_and_desktop_bits();
    std::cout << "Satchel-input logic tests passed\n";
    return 0;
}
