// SPDX-License-Identifier: GPL-3.0-only
#include "kar98_bolt_action_logic.hpp"

#include <algorithm>
#include <cmath>

namespace wawvr::mod {
namespace {

[[nodiscard]] bool trigger_held(
    const BoltActionWeaponProfile& profile,
    const bool trigger_active,
    const float trigger_value,
    const bool prior) noexcept {
    if (!trigger_active || !std::isfinite(trigger_value)) {
        return false;
    }
    const float value = std::clamp(trigger_value, 0.0F, 1.0F);
    return prior ? value >= profile.trigger_release
                 : value >= profile.trigger_engage;
}

[[nodiscard]] Kar98BoltActionUpdate describe(
    const BoltActionWeaponProfile& profile,
    const Kar98BoltActionState& state,
    const Kar98BoltEvent event,
    const bool pressed,
    const bool released) noexcept {
    const bool interaction_active = state.cycle_required ||
        state.bolt_grabbed || state.awaiting_controls_release ||
        state.bolt_fraction > profile.bolt_closed_threshold;
    return {
        .event = event,
        .bolt_fraction = state.bolt_fraction,
        .trigger_pressed_edge = pressed,
        .trigger_released_edge = released,
        .bolt_grabbed = state.bolt_grabbed,
        .begin_reload_gesture =
            state.bolt_grabbed && state.grab_started_closed,
        .action_open =
            state.bolt_fraction >= profile.bolt_open_threshold,
        .cycle_required = state.cycle_required,
        .block_attack = interaction_active,
        .reserve_right_grip = interaction_active &&
            state.manipulating_hand_selected &&
            !state.manipulating_left_hand,
        .reserve_left_grip = interaction_active &&
            state.manipulating_hand_selected &&
            state.manipulating_left_hand,
        .manipulating_left_hand = state.manipulating_hand_selected &&
            state.manipulating_left_hand,
    };
}

}  // namespace

std::int32_t select_bolt_action_rechamber_visual_anim(
    const BoltActionWeaponProfile& profile,
    const bool enabled,
    const bool cycle_required,
    const bool weapon_supported,
    const std::int32_t native_anim) noexcept {
    const bool automatic_rechamber =
        native_anim == profile.rechamber_hip_anim ||
        native_anim == profile.rechamber_ads_anim;
    return enabled && cycle_required && weapon_supported &&
            automatic_rechamber
        ? profile.idle_anim
        : native_anim;
}

std::int32_t select_kar98_rechamber_visual_anim(
    const bool enabled,
    const bool cycle_required,
    const bool weapon_supported,
    const std::int32_t native_anim) noexcept {
    return select_bolt_action_rechamber_visual_anim(
        kKar98BoltActionWeaponProfile, enabled, cycle_required,
        weapon_supported, native_anim);
}

Kar98BoltVisualDecision decide_bolt_action_bolt_visual(
    const BoltActionWeaponProfile& profile,
    const bool closed_pose_latched,
    const float bolt_fraction,
    const bool interaction_active) noexcept {
    const bool capture =
        !closed_pose_latched && std::isfinite(bolt_fraction) &&
        bolt_fraction <= profile.bolt_closed_threshold &&
        !interaction_active;
    return {
        .capture_closed_pose = capture,
        .apply_manual_pose = closed_pose_latched || capture,
    };
}

Kar98BoltVisualDecision decide_kar98_bolt_visual(
    const bool closed_pose_latched,
    const float bolt_fraction,
    const bool interaction_active) noexcept {
    return decide_bolt_action_bolt_visual(
        kKar98BoltActionWeaponProfile, closed_pose_latched, bolt_fraction,
        interaction_active);
}

bool calculate_bolt_action_bolt_parent_travel(
    const BoltActionWeaponProfile& profile,
    const float root_quaternion[4],
    const float bolt_fraction,
    Kar98BoltVector* const travel) noexcept {
    return calculate_linear_action_parent_travel(
        profile.bolt_travel_units, root_quaternion, bolt_fraction, travel);
}

bool calculate_linear_action_parent_travel(
    const float travel_units,
    const float root_quaternion[4],
    const float action_fraction,
    Kar98BoltVector* const travel) noexcept {
    if (root_quaternion == nullptr || travel == nullptr ||
        !std::isfinite(travel_units) || travel_units <= 0.0F ||
        !std::isfinite(action_fraction) ||
        !std::isfinite(root_quaternion[0]) ||
        !std::isfinite(root_quaternion[1]) ||
        !std::isfinite(root_quaternion[2]) ||
        !std::isfinite(root_quaternion[3])) {
        return false;
    }
    const double length_squared =
        static_cast<double>(root_quaternion[0]) * root_quaternion[0] +
        static_cast<double>(root_quaternion[1]) * root_quaternion[1] +
        static_cast<double>(root_quaternion[2]) * root_quaternion[2] +
        static_cast<double>(root_quaternion[3]) * root_quaternion[3];
    if (!std::isfinite(length_squared) || length_squared < 1.0e-8) {
        return false;
    }
    const float scale = static_cast<float>(1.0 / std::sqrt(length_squared));
    const float qx = root_quaternion[0] * scale;
    const float qy = root_quaternion[1] * scale;
    const float qz = root_quaternion[2] * scale;
    const float qw = root_quaternion[3] * scale;
    const float local_x = -travel_units *
        std::clamp(action_fraction, 0.0F, 1.0F);
    // Quaternion-vector rotation specialized for (local_x, 0, 0).
    const float tx = 0.0F;
    const float ty = 2.0F * qz * local_x;
    const float tz = -2.0F * qy * local_x;
    travel->x = local_x + qw * tx + (qy * tz - qz * ty);
    travel->y = qw * ty + (qz * tx - qx * tz);
    travel->z = qw * tz + (qx * ty - qy * tx);
    return std::isfinite(travel->x) && std::isfinite(travel->y) &&
           std::isfinite(travel->z);
}

bool calculate_kar98_bolt_parent_travel(
    const float root_quaternion[4],
    const float bolt_fraction,
    Kar98BoltVector* const travel) noexcept {
    return calculate_bolt_action_bolt_parent_travel(
        kKar98BoltActionWeaponProfile, root_quaternion, bolt_fraction,
        travel);
}

Kar98BoltActionUpdate update_bolt_action(
    const BoltActionWeaponProfile& profile,
    const Kar98BoltActionFrame& frame,
    Kar98BoltActionState* const state) noexcept {
    if (state == nullptr) {
        return {};
    }

    const bool hard_context_valid = frame.enabled && frame.weapon_supported;
    if (frame.reset_requested || !hard_context_valid) {
        const bool changed = state->cycle_required || state->bolt_grabbed ||
            state->awaiting_controls_release ||
            state->bolt_fraction > profile.bolt_closed_threshold;
        reset_kar98_bolt_action(state);
        return describe(
            profile, *state,
            changed ? Kar98BoltEvent::Reset : Kar98BoltEvent::None, false,
            false);
    }

    const bool right_held = trigger_held(
        profile, frame.right_trigger_active, frame.right_trigger_value,
        state->trigger_was_held);
    const bool left_held = trigger_held(
        profile, frame.left_trigger_active, frame.left_trigger_value,
        state->left_trigger_was_held);
    const bool both_trigger_samples_valid =
        frame.right_trigger_active &&
        std::isfinite(frame.right_trigger_value) &&
        frame.left_trigger_active &&
        std::isfinite(frame.left_trigger_value);
    Kar98BoltEvent event = Kar98BoltEvent::None;
    if (frame.shot_fired) {
        state->cycle_required = true;
        state->fully_opened = false;
        event = Kar98BoltEvent::ShotLocked;
    }

    if (!frame.focused || frame.action_sequence == 0) {
        // Tracking/focus gaps may release a physical latch, but they cannot
        // erase a post-shot chamber requirement or teleport an open bolt shut.
        state->input_owned = false;
        state->last_action_sequence = frame.action_sequence;
        state->trigger_was_held = right_held;
        state->left_trigger_was_held = left_held;
        if (state->bolt_grabbed) {
            state->bolt_grabbed = false;
            state->grab_started_closed = false;
            state->manipulating_hand_selected = false;
            state->manipulating_left_hand = false;
            if (event == Kar98BoltEvent::None) {
                event = Kar98BoltEvent::Released;
            }
        }
        return describe(profile, *state, event, false, false);
    }

    bool right_pressed = false;
    bool right_released = false;
    bool left_pressed = false;
    bool left_released = false;
    if (!state->input_owned) {
        // Baseline any control held while focus/context was unavailable.
        state->input_owned = true;
        state->last_action_sequence = frame.action_sequence;
        state->trigger_was_held = right_held;
        state->left_trigger_was_held = left_held;
    } else if (frame.action_sequence != state->last_action_sequence) {
        right_pressed = right_held && !state->trigger_was_held;
        right_released = !right_held && state->trigger_was_held;
        left_pressed = left_held && !state->left_trigger_was_held;
        left_released = !left_held && state->left_trigger_was_held;
        state->last_action_sequence = frame.action_sequence;
        state->trigger_was_held = right_held;
        state->left_trigger_was_held = left_held;
    }
    const bool pressed = right_pressed || left_pressed;
    const bool released = right_released || left_released;

    const auto selected_held = [&]() noexcept {
        return state->manipulating_left_hand ? left_held : right_held;
    };
    const auto selected_released = [&]() noexcept {
        return state->manipulating_left_hand
            ? left_released : right_released;
    };
    const auto selected_opposite_gripped = [&]() noexcept {
        return state->manipulating_left_hand
            ? frame.right_rifle_gripped : frame.left_rifle_gripped;
    };
    const auto selected_pose_valid = [&]() noexcept {
        return state->manipulating_left_hand
            ? frame.left_hand_pose_valid : frame.right_hand_pose_valid;
    };
    const auto selected_near_bolt = [&]() noexcept {
        return state->manipulating_left_hand
            ? frame.left_hand_near_bolt : frame.right_hand_near_bolt;
    };
    const auto selected_grip_held = [&]() noexcept {
        return state->manipulating_left_hand
            ? frame.left_grip_held : frame.right_grip_held;
    };
    const auto selected_forward_coordinate = [&]() noexcept {
        return state->manipulating_left_hand
            ? frame.left_hand_forward_coordinate
            : frame.right_hand_forward_coordinate;
    };

    if (state->awaiting_controls_release) {
        if (state->manipulating_hand_selected &&
            both_trigger_samples_valid && selected_pose_valid() &&
            !right_held && !left_held && !selected_grip_held()) {
            state->awaiting_controls_release = false;
            state->fully_opened = false;
            state->manipulating_hand_selected = false;
            state->manipulating_left_hand = false;
            event = Kar98BoltEvent::ControlsRearmed;
        }
        return describe(profile, *state, event, pressed, released);
    }

    if (state->bolt_grabbed) {
        if (selected_released() || !selected_held() ||
            !selected_opposite_gripped() || selected_grip_held() ||
            !selected_pose_valid() ||
            !std::isfinite(selected_forward_coordinate())) {
            state->bolt_grabbed = false;
            state->grab_started_closed = false;
            state->manipulating_hand_selected = false;
            state->manipulating_left_hand = false;
            event = Kar98BoltEvent::Released;
        } else {
            const float movement =
                selected_forward_coordinate() -
                state->grab_start_coordinate;
            state->bolt_fraction = std::clamp(
                state->grab_start_fraction -
                    movement / profile.bolt_travel_units,
                0.0F, 1.0F);
            if (state->bolt_fraction >= profile.bolt_open_threshold) {
                state->bolt_fraction = 1.0F;
                if (!state->fully_opened) {
                    event = Kar98BoltEvent::FullyOpened;
                }
                state->fully_opened = true;
            } else if (state->bolt_fraction <=
                           profile.bolt_closed_threshold &&
                       (!state->grab_started_closed ||
                        state->fully_opened)) {
                const bool completed_cycle = state->fully_opened;
                state->bolt_fraction = 0.0F;
                state->bolt_grabbed = false;
                state->grab_started_closed = false;
                if (completed_cycle) {
                    state->cycle_required = false;
                    event = Kar98BoltEvent::FullyClosed;
                } else {
                    event = Kar98BoltEvent::Released;
                }
                // Even a failed partial pull must see a release before another
                // trigger edge can grab or fire.
                state->awaiting_controls_release = true;
            }
        }
    } else {
        const bool right_candidate = right_pressed &&
            frame.left_rifle_gripped && !frame.right_grip_held &&
            frame.right_hand_pose_valid && frame.right_hand_near_bolt &&
            std::isfinite(frame.right_hand_forward_coordinate);
        const bool left_candidate = left_pressed &&
            frame.right_rifle_gripped && !frame.left_grip_held &&
            frame.left_hand_pose_valid && frame.left_hand_near_bolt &&
            std::isfinite(frame.left_hand_forward_coordinate);
        if (!right_candidate && !left_candidate) {
            return describe(profile, *state, event, pressed, released);
        }
        // Only one hand is normally free. Preserve the accepted right-hand
        // behavior as the deterministic tie-breaker for an impossible/invalid
        // simultaneous candidate sample.
        state->manipulating_hand_selected = true;
        state->manipulating_left_hand = !right_candidate && left_candidate;
        state->bolt_grabbed = true;
        state->grab_start_coordinate = selected_forward_coordinate();
        state->grab_start_fraction = state->bolt_fraction;
        state->grab_started_closed =
            state->bolt_fraction <= profile.bolt_closed_threshold;
        event = Kar98BoltEvent::Grabbed;
    }

    return describe(profile, *state, event, pressed, released);
}

Kar98BoltActionUpdate update_kar98_bolt_action(
    const Kar98BoltActionFrame& frame,
    Kar98BoltActionState* const state) noexcept {
    return update_bolt_action(kKar98BoltActionWeaponProfile, frame, state);
}

void reset_kar98_bolt_action(Kar98BoltActionState* const state) noexcept {
    if (state != nullptr) {
        *state = {};
    }
}

}  // namespace wawvr::mod
