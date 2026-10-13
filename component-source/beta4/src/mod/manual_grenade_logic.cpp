// SPDX-License-Identifier: GPL-3.0-only
#include "manual_grenade_logic.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace wawvr::mod {
namespace {

[[nodiscard]] bool finite_vector(
    const ManualGrenadePoint& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] float vector_length(
    const ManualGrenadePoint& value) noexcept {
    const double length_squared =
        static_cast<double>(value.x) * value.x +
        static_cast<double>(value.y) * value.y +
        static_cast<double>(value.z) * value.z;
    if (!std::isfinite(length_squared) || length_squared < 0.0) {
        return (std::numeric_limits<float>::quiet_NaN)();
    }
    const double length = std::sqrt(length_squared);
    if (!std::isfinite(length) ||
        length > static_cast<double>((std::numeric_limits<float>::max)())) {
        return (std::numeric_limits<float>::quiet_NaN)();
    }
    return static_cast<float>(length);
}

[[nodiscard]] bool capped_velocity(
    const ManualGrenadePoint& input,
    ManualGrenadePoint* const output,
    float* const original_speed = nullptr) noexcept {
    if (output == nullptr || !finite_vector(input)) {
        return false;
    }
    const float speed = vector_length(input);
    if (!std::isfinite(speed)) {
        return false;
    }
    if (original_speed != nullptr) {
        *original_speed = speed;
    }
    *output = input;
    if (speed > kManualGrenadeMaximumHandSpeed) {
        const float scale = kManualGrenadeMaximumHandSpeed / speed;
        output->x *= scale;
        output->y *= scale;
        output->z *= scale;
    }
    return finite_vector(*output);
}

[[nodiscard]] bool valid_slot(const ManualGrenadeSlot slot) noexcept {
    return slot == ManualGrenadeSlot::none ||
           slot == ManualGrenadeSlot::frag ||
           slot == ManualGrenadeSlot::tactical;
}

[[nodiscard]] bool valid_gesture_state(
    const ManualGrenadeGestureState& state) noexcept {
    if (!valid_slot(state.held_slot)) {
        return false;
    }
    switch (state.stage) {
        case ManualGrenadeStage::ready:
            return state.held_slot == ManualGrenadeSlot::none;
        case ManualGrenadeStage::holding:
        case ManualGrenadeStage::released_pending:
            return state.held_slot == ManualGrenadeSlot::frag ||
                   state.held_slot == ManualGrenadeSlot::tactical;
    }
    return false;
}

[[nodiscard]] bool valid_calibration_component(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F &&
           value <= kManualGrenadeMaximumNativeProjectileSpeed;
}

}  // namespace

bool manual_grenade_left_hip_contains(
    const ManualGrenadePoint& head_local_position) noexcept {
    return finite_vector(head_local_position) &&
           head_local_position.x >= kManualGrenadeBeltForwardMinimum &&
           head_local_position.x <= kManualGrenadeBeltForwardMaximum &&
           head_local_position.y >= kManualGrenadeBeltSideMinimum &&
           head_local_position.y <= kManualGrenadeBeltSideMaximum &&
           head_local_position.z >= kManualGrenadeBeltUpMinimum &&
           head_local_position.z <= kManualGrenadeBeltUpMaximum;
}

bool manual_grenade_right_hip_contains(
    const ManualGrenadePoint& head_local_position) noexcept {
    return finite_vector(head_local_position) &&
           head_local_position.x >= kManualGrenadeBeltForwardMinimum &&
           head_local_position.x <= kManualGrenadeBeltForwardMaximum &&
           head_local_position.y <= -kManualGrenadeBeltSideMinimum &&
           head_local_position.y >= -kManualGrenadeBeltSideMaximum &&
           head_local_position.z >= kManualGrenadeBeltUpMinimum &&
           head_local_position.z <= kManualGrenadeBeltUpMaximum;
}

ManualGrenadeSlot manual_grenade_slot_at(
    const ManualGrenadePoint& head_local_position) noexcept {
    if (manual_grenade_left_hip_contains(head_local_position)) {
        return ManualGrenadeSlot::frag;
    }
    if (manual_grenade_right_hip_contains(head_local_position)) {
        return ManualGrenadeSlot::tactical;
    }
    return ManualGrenadeSlot::none;
}

bool manual_grenade_has_frag_throwback_override(
    const ManualGrenadeSlot slot,
    const std::int32_t throwback_time_left) noexcept {
    return slot == ManualGrenadeSlot::frag &&
           throwback_time_left > 0;
}

bool update_manual_grenade_gesture(
    const bool input_valid,
    const bool left_trigger_active,
    const float left_trigger_value,
    const ManualGrenadePoint& left_hand_head_local_position,
    const bool new_grab_blocked,
    ManualGrenadeGestureState* const state,
    ManualGrenadeEvent* const event) noexcept {
    if (state == nullptr || event == nullptr) {
        return false;
    }
    *event = ManualGrenadeEvent::none;

    const bool sample_valid =
        input_valid && left_trigger_active &&
        std::isfinite(left_trigger_value) &&
        finite_vector(left_hand_head_local_position);
    if (!sample_valid || !valid_gesture_state(*state)) {
        const bool changed =
            state->stage != ManualGrenadeStage::ready ||
            state->held_slot != ManualGrenadeSlot::none ||
            state->input_initialized || state->trigger_was_held;
        *state = {};
        if (changed) {
            *event = ManualGrenadeEvent::reset;
        }
        return true;
    }

    const float clamped =
        std::clamp(left_trigger_value, 0.0F, 1.0F);
    const bool trigger_held = state->trigger_was_held
        ? clamped > kManualGrenadeTriggerRelease
        : clamped >= kManualGrenadeTriggerEngage;

    if (!state->input_initialized) {
        state->input_initialized = true;
        state->trigger_was_held = trigger_held;
        return true;
    }

    const bool pressed_edge = trigger_held && !state->trigger_was_held;
    const bool released_edge = !trigger_held && state->trigger_was_held;
    state->trigger_was_held = trigger_held;

    if (state->stage == ManualGrenadeStage::ready && pressed_edge &&
        !new_grab_blocked) {
        const ManualGrenadeSlot slot =
            manual_grenade_slot_at(left_hand_head_local_position);
        if (slot == ManualGrenadeSlot::frag) {
            state->stage = ManualGrenadeStage::holding;
            state->held_slot = slot;
            *event = ManualGrenadeEvent::grab_frag;
        } else if (slot == ManualGrenadeSlot::tactical) {
            state->stage = ManualGrenadeStage::holding;
            state->held_slot = slot;
            *event = ManualGrenadeEvent::grab_tactical;
        }
    } else if (state->stage == ManualGrenadeStage::holding &&
               released_edge) {
        state->stage = ManualGrenadeStage::released_pending;
        *event = ManualGrenadeEvent::release;
    }
    return true;
}

bool rearm_manual_grenade_gesture(
    ManualGrenadeGestureState* const state) noexcept {
    if (state == nullptr || !valid_gesture_state(*state) ||
        state->stage == ManualGrenadeStage::holding) {
        return false;
    }
    state->stage = ManualGrenadeStage::ready;
    state->held_slot = ManualGrenadeSlot::none;
    return true;
}

void reset_manual_grenade_gesture(
    ManualGrenadeGestureState* const state) noexcept {
    if (state != nullptr) {
        *state = {};
    }
}

void clear_manual_grenade_velocity_history(
    ManualGrenadeVelocityHistory* const history) noexcept {
    if (history != nullptr) {
        *history = {};
    }
}

bool record_manual_grenade_velocity(
    const std::uint64_t sampled_monotonic_nanoseconds,
    const ManualGrenadePoint& velocity_game_units_per_second,
    ManualGrenadeVelocityHistory* const history) noexcept {
    if (sampled_monotonic_nanoseconds == 0U || history == nullptr ||
        history->write_index >= history->samples.size()) {
        return false;
    }
    ManualGrenadePoint bounded{};
    if (!capped_velocity(velocity_game_units_per_second, &bounded)) {
        return false;
    }
    ManualGrenadeVelocitySample& sample =
        history->samples[history->write_index];
    sample.valid = true;
    sample.sampled_monotonic_nanoseconds =
        sampled_monotonic_nanoseconds;
    sample.velocity_game_units_per_second = bounded;
    history->write_index =
        (history->write_index + 1U) % history->samples.size();
    return true;
}

bool select_manual_grenade_release_velocity(
    const std::uint64_t now_monotonic_nanoseconds,
    const ManualGrenadeVelocityHistory& history,
    ManualGrenadePoint* const velocity_game_units_per_second,
    std::uint64_t* const sample_age_nanoseconds) noexcept {
    if (velocity_game_units_per_second == nullptr ||
        sample_age_nanoseconds == nullptr) {
        return false;
    }
    *velocity_game_units_per_second = {};
    *sample_age_nanoseconds = 0U;
    if (now_monotonic_nanoseconds == 0U ||
        history.write_index >= history.samples.size()) {
        return false;
    }

    bool selected = false;
    float selected_score = -1.0F;
    ManualGrenadePoint selected_velocity{};
    std::uint64_t selected_age = 0U;
    for (const ManualGrenadeVelocitySample& sample : history.samples) {
        if (!sample.valid || sample.sampled_monotonic_nanoseconds == 0U ||
            sample.sampled_monotonic_nanoseconds >
                now_monotonic_nanoseconds ||
            !finite_vector(sample.velocity_game_units_per_second)) {
            continue;
        }
        const std::uint64_t age = now_monotonic_nanoseconds -
            sample.sampled_monotonic_nanoseconds;
        if (age > kManualGrenadeVelocityHistoryWindowNanoseconds) {
            continue;
        }
        const float speed =
            vector_length(sample.velocity_game_units_per_second);
        if (!std::isfinite(speed)) {
            continue;
        }
        const float normalized_age = static_cast<float>(
            static_cast<double>(age) /
            static_cast<double>(
                kManualGrenadeVelocityHistoryWindowNanoseconds));
        const float score =
            speed * (1.0F -
                     kManualGrenadeVelocityAgePenalty * normalized_age);
        if (!std::isfinite(score)) {
            continue;
        }
        if (!selected || score > selected_score) {
            selected = true;
            selected_score = score;
            selected_velocity = sample.velocity_game_units_per_second;
            selected_age = age;
        }
    }
    if (!selected ||
        !capped_velocity(selected_velocity,
                         velocity_game_units_per_second)) {
        *velocity_game_units_per_second = {};
        return false;
    }
    *sample_age_nanoseconds = selected_age;
    return true;
}

bool build_manual_grenade_launch_velocity(
    const ManualGrenadePoint& physical_velocity,
    const ManualGrenadePoint& fallback_forward,
    const ManualGrenadeLaunchCalibration& calibration,
    ManualGrenadeLaunchResult* const result) noexcept {
    if (result == nullptr) {
        return false;
    }
    *result = {};
    if (!finite_vector(physical_velocity) ||
        !finite_vector(fallback_forward) ||
        !valid_calibration_component(calibration.native_projectile_speed) ||
        !valid_calibration_component(
            calibration.native_projectile_speed_forward) ||
        !valid_calibration_component(
            calibration.native_projectile_speed_up)) {
        return false;
    }

    const float physical_speed = vector_length(physical_velocity);
    if (!std::isfinite(physical_speed)) {
        return false;
    }
    result->deliberate_drop =
        physical_speed < kManualGrenadeDeliberateDropSpeed;
    if (result->deliberate_drop) {
        result->velocity_game_units_per_second = physical_velocity;
        return true;
    }

    const float strength = std::clamp(
        (physical_speed - kManualGrenadeDeliberateDropSpeed) /
            (kManualGrenadeFullStrengthHandSpeed -
             kManualGrenadeDeliberateDropSpeed),
        0.0F, 1.0F);
    result->normalized_strength = strength;

    float horizontal_x = physical_velocity.x;
    float horizontal_y = physical_velocity.y;
    float horizontal_length = static_cast<float>(std::hypot(
        static_cast<double>(horizontal_x),
        static_cast<double>(horizontal_y)));
    if (!std::isfinite(horizontal_length)) {
        *result = {};
        return false;
    }
    if (horizontal_length <
        kManualGrenadeStableHorizontalDirectionSpeed) {
        horizontal_x = fallback_forward.x;
        horizontal_y = fallback_forward.y;
        horizontal_length = static_cast<float>(std::hypot(
            static_cast<double>(horizontal_x),
            static_cast<double>(horizontal_y)));
        result->used_fallback_direction = true;
    }
    if (!std::isfinite(horizontal_length) || horizontal_length < 0.001F) {
        horizontal_x = 1.0F;
        horizontal_y = 0.0F;
        horizontal_length = 1.0F;
        result->used_fallback_direction = true;
    }
    horizontal_x /= horizontal_length;
    horizontal_y /= horizontal_length;

    float native_horizontal = calibration.native_projectile_speed +
        calibration.native_projectile_speed_forward;
    if (!std::isfinite(native_horizontal)) {
        *result = {};
        return false;
    }
    if (native_horizontal < 1.0F) {
        native_horizontal = 700.0F;
    }
    const float horizontal_launch = native_horizontal *
        (kManualGrenadeMinimumNativeStrength +
         (kManualGrenadeMaximumNativeStrength -
          kManualGrenadeMinimumNativeStrength) *
             strength);
    result->velocity_game_units_per_second.x =
        horizontal_x * horizontal_launch;
    result->velocity_game_units_per_second.y =
        horizontal_y * horizontal_launch;

    const float native_up = calibration.native_projectile_speed_up;
    float vertical =
        native_up + physical_velocity.z * kManualGrenadeVerticalHandScale;
    const float minimum_arc = std::max(native_up * 0.35F, 80.0F);
    const float maximum_arc =
        std::max(native_up + native_horizontal * 0.55F, 400.0F);
    vertical = std::clamp(vertical, minimum_arc, maximum_arc);
    result->velocity_game_units_per_second.z = vertical;

    if (!finite_vector(result->velocity_game_units_per_second) ||
        !std::isfinite(result->normalized_strength)) {
        *result = {};
        return false;
    }
    return true;
}

}  // namespace wawvr::mod
