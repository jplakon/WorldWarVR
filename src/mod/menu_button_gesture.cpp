// SPDX-License-Identifier: GPL-3.0-only
#include "menu_button_gesture.hpp"

namespace wawvr::mod {

MenuButtonSourceSelection select_menu_button_source(
    const bool menu_action_active,
    const bool menu_action_current,
    const bool secondary_action_active,
    const bool secondary_action_current) noexcept {
    if (menu_action_active) {
        return {menu_action_current, false};
    }
    return {
        secondary_action_active && secondary_action_current,
        true,
    };
}

MenuButtonGestureUpdate update_menu_button_gesture(
    const bool menu_pressed,
    const bool hmd_pose_valid,
    const std::uint64_t now_milliseconds,
    const std::uint64_t hold_milliseconds,
    MenuButtonGestureState* const state) noexcept {
    MenuButtonGestureUpdate update{};
    if (state == nullptr) {
        return update;
    }

    if (!menu_pressed) {
        if (state->holding) {
            update.hold_released = true;
            const bool threshold_elapsed =
                now_milliseconds >= state->started_milliseconds &&
                now_milliseconds - state->started_milliseconds >=
                    hold_milliseconds;
            if (!state->long_hold_recognized && threshold_elapsed) {
                state->long_hold_recognized = true;
                update.long_hold_recognized = true;
            }
            update.short_tap_released = !state->long_hold_recognized;
            // A delayed frame may observe only the release after a complete
            // hold. It is still a recenter gesture, never an Escape tap.
            if (state->long_hold_recognized && !state->recenter_captured &&
                hmd_pose_valid) {
                update.recenter_capture_requested = true;
            }
        }
        *state = {};
        return update;
    }

    if (!state->holding || now_milliseconds < state->started_milliseconds) {
        *state = {};
        state->holding = true;
        state->started_milliseconds = now_milliseconds;
        update.hold_started = true;
    }

    if (!state->long_hold_recognized &&
        now_milliseconds - state->started_milliseconds >= hold_milliseconds) {
        state->long_hold_recognized = true;
        update.long_hold_recognized = true;
    }
    if (!state->long_hold_recognized || state->recenter_captured) {
        return update;
    }

    if (!hmd_pose_valid) {
        if (!state->waiting_for_valid_pose_reported) {
            state->waiting_for_valid_pose_reported = true;
            update.waiting_for_valid_pose = true;
        }
        return update;
    }

    state->recenter_captured = true;
    state->waiting_for_valid_pose_reported = false;
    update.recenter_capture_requested = true;
    return update;
}

}  // namespace wawvr::mod
