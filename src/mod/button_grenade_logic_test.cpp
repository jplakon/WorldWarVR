// SPDX-License-Identifier: GPL-3.0-only
#include "button_grenade_logic.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void require(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void test_setting_is_strictly_opt_in() {
    using wawvr::mod::button_grenade_setting_enabled;

    require(button_grenade_setting_enabled(L"1"),
            "explicit 1 enables button grenades");
    require(!button_grenade_setting_enabled(L"0") &&
                !button_grenade_setting_enabled(L"") &&
                !button_grenade_setting_enabled(L"true") &&
                !button_grenade_setting_enabled(L"01") &&
                !button_grenade_setting_enabled(L" 1"),
            "missing, disabled, and malformed settings preserve manual grenades");
}

void test_trigger_drives_native_hold_without_pose_or_belt_state() {
    wawvr::mod::ButtonGrenadeTriggerState state{};
    bool hold_native_frag = true;

    require(wawvr::mod::update_button_grenade_trigger(
                false, true, 0.69F, false, &state,
                &hold_native_frag) &&
                !state.trigger_held && !hold_native_frag,
            "trigger below engage does not press the native grenade button");
    require(wawvr::mod::update_button_grenade_trigger(
                false, true, 0.70F, false, &state,
                &hold_native_frag) &&
                state.trigger_held && hold_native_frag,
            "engage threshold immediately holds the native grenade button");
    require(wawvr::mod::update_button_grenade_trigger(
                false, true, 0.36F, false, &state,
                &hold_native_frag) &&
                state.trigger_held && hold_native_frag,
            "hysteresis keeps the native grenade button held");
    require(wawvr::mod::update_button_grenade_trigger(
                false, true, 0.35F, false, &state,
                &hold_native_frag) &&
                !state.trigger_held && !hold_native_frag,
            "release threshold lets the native game throw the grenade");
}

void test_tracking_gap_cannot_manufacture_a_throw() {
    wawvr::mod::ButtonGrenadeTriggerState state{.trigger_held = true};
    bool hold_native_frag = false;

    require(wawvr::mod::update_button_grenade_trigger(
                false, false, 0.0F, false, &state,
                &hold_native_frag) &&
                state.trigger_held && hold_native_frag,
            "unavailable input preserves an existing native hold");
    require(wawvr::mod::update_button_grenade_trigger(
                false, true, 0.0F, false, &state,
                &hold_native_frag) &&
                !state.trigger_held && !hold_native_frag,
            "the next valid physical release ends the native hold");
}

void test_native_button_merge_preserves_desktop_input() {
    constexpr std::uint32_t frag = 0x00004000U;
    constexpr std::uint32_t tactical = 0x00008000U;
    constexpr std::uint32_t unrelated = 0x80000000U;
    const std::uint32_t desktop_buttons = tactical | unrelated;

    require(wawvr::mod::merge_button_grenade_native_hold(
                desktop_buttons, frag | tactical, frag, false) ==
                desktop_buttons,
            "idle controller input leaves every desktop button untouched");
    require(wawvr::mod::merge_button_grenade_native_hold(
                desktop_buttons, frag | tactical, frag, true) ==
                (unrelated | frag),
            "controller hold selects frag and suppresses conflicting tactical input");
    require(wawvr::mod::merge_button_grenade_native_hold(
                desktop_buttons | frag, frag | tactical, frag, false) ==
                (desktop_buttons | frag),
            "controller release never clears a desktop-owned frag button");
}

void test_inactive_gameplay_resets_and_invalid_arguments_fail_safely() {
    wawvr::mod::ButtonGrenadeTriggerState state{.trigger_held = true};
    bool hold_native_frag = true;

    require(wawvr::mod::update_button_grenade_trigger(
                true, false, 1.0F, false, &state,
                &hold_native_frag) &&
                !state.trigger_held && !hold_native_frag,
            "a positively inactive gameplay session clears button ownership");

    state.trigger_held = true;
    hold_native_frag = false;
    require(!wawvr::mod::update_button_grenade_trigger(
                false, true,
                (std::numeric_limits<float>::quiet_NaN)(),
                false, &state, &hold_native_frag) &&
                state.trigger_held && hold_native_frag,
            "non-finite valid input fails without manufacturing a release");
    require(!wawvr::mod::update_button_grenade_trigger(
                false, true, 1.0F, false, nullptr,
                &hold_native_frag) &&
                !wawvr::mod::update_button_grenade_trigger(
                    false, true, 1.0F, false, &state, nullptr),
            "null state and output pointers are rejected");
}

void test_reserved_left_trigger_blocks_only_a_new_grenade_press() {
    wawvr::mod::ButtonGrenadeTriggerState state{};
    bool hold_native_frag = true;

    require(wawvr::mod::update_button_grenade_trigger(
                false, true, 1.0F, true, &state,
                &hold_native_frag) &&
                !state.trigger_held && !hold_native_frag,
            "a left bolt or charging reservation blocks a new button-grenade press");

    require(wawvr::mod::update_button_grenade_trigger(
                false, true, 1.0F, false, &state,
                &hold_native_frag) &&
                !state.trigger_held && !hold_native_frag,
            "clearing the reservation while the rejected trigger remains high cannot manufacture a grenade press");

    require(wawvr::mod::update_button_grenade_trigger(
                false, true, 0.0F, false, &state,
                &hold_native_frag) &&
                !state.trigger_held && !state.new_press_rearm_required &&
                !hold_native_frag,
            "a physical trigger release rearms grenade presses after a blocked attempt");

    require(wawvr::mod::update_button_grenade_trigger(
                false, true, 1.0F, false, &state,
                &hold_native_frag) &&
                state.trigger_held && hold_native_frag,
            "an unreserved fresh press may begin a button grenade hold");

    require(wawvr::mod::update_button_grenade_trigger(
                false, true, 1.0F, true, &state,
                &hold_native_frag) &&
                state.trigger_held && hold_native_frag,
            "a later interaction reservation cannot cancel an already-held grenade");

    require(wawvr::mod::update_button_grenade_trigger(
                false, true, 0.0F, true, &state,
                &hold_native_frag) &&
                !state.trigger_held && !hold_native_frag,
            "an already-held grenade can still release while new presses are blocked");
}

}  // namespace

int main() {
    test_setting_is_strictly_opt_in();
    test_trigger_drives_native_hold_without_pose_or_belt_state();
    test_tracking_gap_cannot_manufacture_a_throw();
    test_native_button_merge_preserves_desktop_input();
    test_inactive_gameplay_resets_and_invalid_arguments_fail_safely();
    test_reserved_left_trigger_blocks_only_a_new_grenade_press();
    std::cout << "Button-grenade logic tests passed\n";
    return 0;
}
