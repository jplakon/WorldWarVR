// SPDX-License-Identifier: GPL-3.0-only
#include "magazine_charging_logic.hpp"

#include <algorithm>
#include <cmath>

namespace wawvr::mod {
namespace {

[[nodiscard]] bool trigger_held(
    const MagazineChargingCalibration& calibration,
    const bool trigger_active,
    const float trigger_value,
    const bool prior) noexcept {
    if (calibration.control_policy ==
        MagazineChargingControlPolicy::EnBlocAutomatic) {
        return false;
    }
    if (!trigger_active || !std::isfinite(trigger_value)) {
        return false;
    }
    const float value =
        std::clamp(trigger_value, 0.0F, 1.0F);
    return prior ? value >= calibration.trigger_release
                 : value >= calibration.trigger_engage;
}

[[nodiscard]] MagazineChargingUpdate describe(
    const MagazineChargingCalibration& calibration,
    const MagazineChargingState& state,
    const MagazineChargingEvent event,
    const bool pressed,
    const bool released) noexcept {
    const bool interaction_active = state.charge_required ||
        state.handle_grabbed || state.spring_returning ||
        state.awaiting_controls_release;
    return {
        .event = event,
        .handle_fraction = state.handle_fraction,
        .trigger_pressed_edge = pressed,
        .trigger_released_edge = released,
        .handle_grabbed = state.handle_grabbed,
        .charge_required = state.charge_required,
        .block_attack = interaction_active,
        .reserve_right_grip = interaction_active &&
            state.manipulating_hand_selected &&
            !state.manipulating_left_hand &&
            calibration.control_policy ==
                MagazineChargingControlPolicy::ManualPullRelease,
        .reserve_left_grip = interaction_active &&
            state.manipulating_hand_selected &&
            state.manipulating_left_hand &&
            calibration.control_policy ==
                MagazineChargingControlPolicy::ManualPullRelease,
        .manipulating_left_hand = state.manipulating_hand_selected &&
            state.manipulating_left_hand,
    };
}

[[nodiscard]] float locked_open_fraction(
    const MagazineChargingCalibration& calibration) noexcept {
    return std::clamp(
        calibration.locked_open_offset_units / calibration.travel_units,
        0.0F, 1.0F);
}

[[nodiscard]] float spring_fraction(
    const MagazineChargingCalibration& calibration,
    const float elapsed_seconds,
    const float duration_seconds) noexcept {
    // Each audited weapon owns its authored return samples. Interpolate them
    // so high render rates preserve the native easing without silently
    // cloning the M1 curve onto unrelated slides and charging handles.
    const std::size_t final_index =
        static_cast<std::size_t>(calibration.return_sample_count - 1U);
    const float normalized = std::clamp(
        elapsed_seconds / duration_seconds, 0.0F, 1.0F);
    const float sample_coordinate =
        normalized * static_cast<float>(final_index);
    const auto lower = static_cast<std::size_t>(sample_coordinate);
    if (lower >= final_index) {
        return 0.0F;
    }
    const float blend = sample_coordinate - static_cast<float>(lower);
    return calibration.return_samples[lower] +
        (calibration.return_samples[lower + 1] -
         calibration.return_samples[lower]) * blend;
}

}  // namespace

bool validate_magazine_charging_calibration(
    const MagazineChargingCalibration& calibration) noexcept {
    const bool manual_policy = calibration.control_policy ==
        MagazineChargingControlPolicy::ManualPullRelease;
    const bool en_bloc_policy = calibration.control_policy ==
        MagazineChargingControlPolicy::EnBlocAutomatic;
    if (!std::isfinite(calibration.travel_units) ||
        calibration.travel_units <= 0.0F ||
        !std::isfinite(calibration.locked_open_offset_units) ||
        calibration.locked_open_offset_units < 0.0F ||
        !std::isfinite(calibration.open_threshold) ||
        calibration.open_threshold <= 0.0F ||
        calibration.open_threshold > 1.0F ||
        !std::isfinite(calibration.trigger_release) ||
        !std::isfinite(calibration.trigger_engage) ||
        (!manual_policy && !en_bloc_policy)) {
        return false;
    }
    if (manual_policy &&
        (calibration.locked_open_offset_units >= calibration.travel_units ||
         calibration.trigger_release < 0.0F ||
         calibration.trigger_release >= calibration.trigger_engage ||
         calibration.trigger_engage > 1.0F)) {
        return false;
    }
    if (en_bloc_policy &&
        (calibration.locked_open_offset_units != calibration.travel_units ||
         calibration.trigger_release != 0.0F ||
         calibration.trigger_engage != 0.0F ||
         calibration.completion !=
             MagazineChargingCompletion::SpringClosed)) {
        return false;
    }

    if (calibration.completion ==
        MagazineChargingCompletion::LatchOpen) {
        if (calibration.spring_return_seconds != 0.0F ||
            calibration.return_sample_count != 0) {
            return false;
        }
        for (const float sample : calibration.return_samples) {
            if (sample != 0.0F) {
                return false;
            }
        }
        return true;
    }
    if (calibration.completion !=
            MagazineChargingCompletion::SpringClosed ||
        !std::isfinite(calibration.spring_return_seconds) ||
        calibration.spring_return_seconds <= 0.0F ||
        calibration.return_sample_count < 2 ||
        calibration.return_sample_count >
            kMaximumMagazineChargingReturnSamples ||
        calibration.return_samples[0] != 1.0F ||
        calibration.return_samples[
            calibration.return_sample_count - 1U] != 0.0F) {
        return false;
    }
    float previous = 1.0F;
    for (std::size_t index = 0;
         index < calibration.return_sample_count; ++index) {
        const float sample = calibration.return_samples[index];
        if (!std::isfinite(sample) || sample < 0.0F || sample > 1.0F ||
            sample > previous) {
            return false;
        }
        previous = sample;
    }
    for (std::size_t index = calibration.return_sample_count;
         index < calibration.return_samples.size(); ++index) {
        if (calibration.return_samples[index] != 0.0F) {
            return false;
        }
    }
    return true;
}

MagazineChargingUpdate update_magazine_charging(
    const MagazineChargingCalibration& calibration,
    const MagazineChargingFrame& frame,
    MagazineChargingState* const state) noexcept {
    if (state == nullptr ||
        !validate_magazine_charging_calibration(calibration)) {
        return {};
    }

    const bool hard_context_valid = frame.enabled && frame.weapon_supported;
    if (frame.reset_requested || !hard_context_valid) {
        const bool changed = state->charge_required || state->handle_grabbed ||
            state->spring_returning || state->awaiting_controls_release ||
            state->handle_fraction > 0.0F;
        reset_magazine_charging(state);
        return describe(
            calibration, *state,
            changed ? MagazineChargingEvent::Reset
                    : MagazineChargingEvent::None,
            false, false);
    }

    const bool right_held = trigger_held(
        calibration, frame.right_trigger_active,
        frame.right_trigger_value, state->trigger_was_held);
    const bool left_held = trigger_held(
        calibration, frame.left_trigger_active,
        frame.left_trigger_value, state->left_trigger_was_held);
    const bool both_trigger_samples_valid =
        frame.right_trigger_active &&
        std::isfinite(frame.right_trigger_value) &&
        frame.left_trigger_active &&
        std::isfinite(frame.left_trigger_value);
    const auto right_controls_ready = [&]() noexcept {
        return frame.left_rifle_gripped && !frame.right_grip_held &&
            !right_held;
    };
    const auto left_controls_ready = [&]() noexcept {
        return frame.right_rifle_gripped && !frame.left_grip_held &&
            !left_held;
    };
    MagazineChargingEvent event = MagazineChargingEvent::None;
    if (frame.arm_charge) {
        state->charge_required = true;
        state->handle_fraction = calibration.control_policy ==
                MagazineChargingControlPolicy::EnBlocAutomatic
            ? 1.0F
            : locked_open_fraction(calibration);
        state->handle_grabbed = false;
        state->fully_opened = false;
        state->spring_returning = false;
        state->spring_return_elapsed_seconds = 0.0F;
        state->awaiting_controls_release =
            calibration.control_policy ==
                MagazineChargingControlPolicy::ManualPullRelease &&
            !right_controls_ready() && !left_controls_ready();
        event = MagazineChargingEvent::ChargeRequired;
    }

    if (!frame.focused || frame.action_sequence == 0) {
        state->input_owned = false;
        state->last_action_sequence = frame.action_sequence;
        state->trigger_was_held = right_held;
        state->left_trigger_was_held = left_held;
        if (state->charge_required &&
            (state->handle_grabbed || state->handle_fraction > 0.0F)) {
            state->handle_grabbed = false;
            state->handle_fraction = calibration.control_policy ==
                    MagazineChargingControlPolicy::EnBlocAutomatic
                ? 1.0F
                : locked_open_fraction(calibration);
            state->fully_opened = false;
            state->spring_returning = false;
            state->spring_return_elapsed_seconds = 0.0F;
            state->manipulating_hand_selected = false;
            state->manipulating_left_hand = false;
            if (event == MagazineChargingEvent::None) {
                event = MagazineChargingEvent::Released;
            }
        }
        return describe(calibration, *state, event, false, false);
    }

    if (state->spring_returning && std::isfinite(frame.delta_seconds) &&
        frame.delta_seconds > 0.0F) {
        state->spring_return_elapsed_seconds = (std::min)(
            calibration.spring_return_seconds,
            state->spring_return_elapsed_seconds + frame.delta_seconds);
        state->handle_fraction = spring_fraction(
            calibration,
            state->spring_return_elapsed_seconds,
            calibration.spring_return_seconds);
        if (state->spring_return_elapsed_seconds >=
            calibration.spring_return_seconds) {
            state->handle_fraction = 0.0F;
            state->spring_returning = false;
            state->spring_return_elapsed_seconds = 0.0F;
            state->charge_required = false;
            event = MagazineChargingEvent::Charged;
        }
    }

    if (calibration.control_policy ==
        MagazineChargingControlPolicy::EnBlocAutomatic) {
        state->input_owned = true;
        state->last_action_sequence = frame.action_sequence;
        state->trigger_was_held = false;
        state->left_trigger_was_held = false;
        state->handle_grabbed = false;
        state->fully_opened = false;
        state->awaiting_controls_release = false;
        state->manipulating_hand_selected = false;
        state->manipulating_left_hand = false;
        if (frame.automatic_spring_release && frame.cartridge_available &&
            state->charge_required && !state->spring_returning) {
            state->handle_fraction = 1.0F;
            state->spring_returning = true;
            state->spring_return_elapsed_seconds = 0.0F;
            event = MagazineChargingEvent::SpringReleased;
        }
        return describe(calibration, *state, event, false, false);
    }

    bool right_pressed = false;
    bool right_released = false;
    bool left_pressed = false;
    bool left_released = false;
    if (!state->input_owned) {
        // A trigger already held when focus returns is a baseline, not a grab.
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
    const auto selected_near_handle = [&]() noexcept {
        return state->manipulating_left_hand
            ? frame.left_hand_near_handle : frame.right_hand_near_handle;
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
        if ((state->manipulating_hand_selected &&
             both_trigger_samples_valid && selected_pose_valid() &&
             !right_held && !left_held && !selected_grip_held()) ||
            (!state->manipulating_hand_selected &&
             both_trigger_samples_valid &&
             (right_controls_ready() || left_controls_ready()))) {
            state->awaiting_controls_release = false;
            state->manipulating_hand_selected = false;
            state->manipulating_left_hand = false;
            if (event == MagazineChargingEvent::None) {
                event = MagazineChargingEvent::ControlsRearmed;
            }
        }
        return describe(calibration, *state, event, pressed, released);
    }

    if (state->handle_grabbed) {
        const bool deliberate_trigger_release =
            selected_released() || !selected_held();
        const bool trigger_sample_valid = state->manipulating_left_hand
            ? frame.left_trigger_active &&
                std::isfinite(frame.left_trigger_value)
            : frame.right_trigger_active &&
                std::isfinite(frame.right_trigger_value);
        const bool gesture_context_valid = trigger_sample_valid &&
            selected_opposite_gripped() && !selected_grip_held() &&
            selected_pose_valid() &&
            std::isfinite(selected_forward_coordinate());
        if (deliberate_trigger_release) {
            const bool completed = state->fully_opened &&
                gesture_context_valid && frame.cartridge_available;
            state->handle_grabbed = false;
            state->handle_fraction = completed
                ? 1.0F : locked_open_fraction(calibration);
            state->fully_opened = false;
            state->awaiting_controls_release = true;
            if (completed) {
                if (calibration.completion ==
                    MagazineChargingCompletion::LatchOpen) {
                    state->handle_fraction = 1.0F;
                    state->charge_required = false;
                    state->spring_returning = false;
                    state->spring_return_elapsed_seconds = 0.0F;
                    event = MagazineChargingEvent::Charged;
                } else {
                    state->spring_returning = true;
                    state->spring_return_elapsed_seconds = 0.0F;
                    event = MagazineChargingEvent::SpringReleased;
                }
            } else {
                state->spring_return_elapsed_seconds = 0.0F;
                event = MagazineChargingEvent::Released;
            }
        } else if (!gesture_context_valid) {
            state->handle_grabbed = false;
            state->handle_fraction = locked_open_fraction(calibration);
            state->fully_opened = false;
            state->spring_return_elapsed_seconds = 0.0F;
            state->manipulating_hand_selected = false;
            state->manipulating_left_hand = false;
            event = MagazineChargingEvent::Released;
        } else {
            const float movement = selected_forward_coordinate() -
                state->grab_start_coordinate;
            state->handle_fraction = std::clamp(
                state->grab_start_fraction -
                    movement / calibration.travel_units,
                0.0F, 1.0F);
            if (state->handle_fraction >= calibration.open_threshold) {
                state->handle_fraction = 1.0F;
                if (!state->fully_opened) {
                    event = MagazineChargingEvent::FullyOpened;
                }
                state->fully_opened = true;
            }
        }
    } else if (state->charge_required && !state->spring_returning) {
        const bool right_candidate = right_pressed &&
            frame.left_rifle_gripped && !frame.right_grip_held &&
            frame.right_hand_pose_valid && frame.right_hand_near_handle &&
            std::isfinite(frame.right_hand_forward_coordinate);
        const bool left_candidate = left_pressed &&
            frame.right_rifle_gripped && !frame.left_grip_held &&
            frame.left_hand_pose_valid && frame.left_hand_near_handle &&
            std::isfinite(frame.left_hand_forward_coordinate);
        if (!right_candidate && !left_candidate) {
            return describe(calibration, *state, event, pressed, released);
        }
        state->manipulating_hand_selected = true;
        state->manipulating_left_hand = !right_candidate && left_candidate;
        state->handle_grabbed = true;
        state->grab_start_coordinate = selected_forward_coordinate();
        state->grab_start_fraction = state->handle_fraction;
        state->fully_opened = false;
        event = MagazineChargingEvent::Grabbed;
    }

    return describe(calibration, *state, event, pressed, released);
}

void reset_magazine_charging(MagazineChargingState* const state) noexcept {
    if (state != nullptr) {
        *state = {};
    }
}

}  // namespace wawvr::mod
