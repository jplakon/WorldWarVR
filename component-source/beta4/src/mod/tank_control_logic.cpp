// SPDX-License-Identifier: GPL-3.0-only
#include "tank_control_logic.hpp"

#include <algorithm>
#include <cmath>

namespace wawvr::mod {

TankControlDelta update_tank_control(
    const TankControlInput& input, TankControlState* const state) noexcept {
    if (state == nullptr) {
        return {};
    }
    if (!input.input_owned || !input.stick_active ||
        input.action_sequence == 0 || input.now_milliseconds == 0 ||
        !std::isfinite(input.stick_x) || !std::isfinite(input.stick_y)) {
        reset_tank_control(state);
        return {};
    }

    if (!state->input_was_owned ||
        input.now_milliseconds < state->previous_update_milliseconds ||
        input.action_sequence < state->last_action_sequence) {
        state->input_was_owned = true;
        state->last_action_sequence = input.action_sequence;
        state->previous_update_milliseconds = input.now_milliseconds;
        return {};
    }
    if (input.action_sequence == state->last_action_sequence) {
        return {};
    }

    const std::uint64_t elapsed = std::min(
        input.now_milliseconds - state->previous_update_milliseconds,
        kTankControlMaximumElapsedMilliseconds);
    state->last_action_sequence = input.action_sequence;
    state->previous_update_milliseconds = input.now_milliseconds;

    const float x = std::clamp(input.stick_x, -1.0F, 1.0F);
    const float y = std::clamp(input.stick_y, -1.0F, 1.0F);
    const float magnitude = std::sqrt(x * x + y * y);
    if (elapsed == 0 || magnitude <= kTankControlDeadzone) {
        return {};
    }

    const float remapped =
        (std::min(magnitude, 1.0F) - kTankControlDeadzone) /
        (1.0F - kTankControlDeadzone);
    const float response = 0.35F * remapped +
        0.65F * remapped * remapped * remapped;
    const float seconds = static_cast<float>(elapsed) / 1000.0F;
    const float scale = response * seconds / magnitude;
    return {
        -y * scale * kTankControlPitchDegreesPerSecond,
        -x * scale * kTankControlYawDegreesPerSecond,
    };
}

TankControlDelta limit_tank_control_delta(
    const TankControlDelta requested,
    const float native_degrees_per_second) noexcept {
    if (!std::isfinite(native_degrees_per_second) ||
        native_degrees_per_second <= 0.0F || native_degrees_per_second > 360.0F ||
        !std::isfinite(requested.pitch_degrees) ||
        !std::isfinite(requested.yaw_degrees)) {
        return {};
    }
    return {
        requested.pitch_degrees * std::min(
            1.0F, native_degrees_per_second / kTankControlPitchDegreesPerSecond),
        requested.yaw_degrees * std::min(
            1.0F, native_degrees_per_second / kTankControlYawDegreesPerSecond),
    };
}

bool apply_tank_control_delta(
    const TankControlDelta delta, float* const client_pitch,
    float* const client_yaw,
    std::array<std::int32_t, 3>* const command_view_angles) noexcept {
    // A single input sample cannot legitimately rotate more than one turn.
    // Keep the subsequent float-to-integer encoding bounded even when a caller
    // supplies a corrupted but finite delta.
    if (client_pitch == nullptr || client_yaw == nullptr ||
        command_view_angles == nullptr || !std::isfinite(*client_pitch) ||
        !std::isfinite(*client_yaw) || !std::isfinite(delta.pitch_degrees) ||
        !std::isfinite(delta.yaw_degrees) ||
        std::abs(delta.pitch_degrees) > 360.0F ||
        std::abs(delta.yaw_degrees) > 360.0F) {
        return false;
    }
    const float pitch_sum = *client_pitch + delta.pitch_degrees;
    const float yaw_sum = *client_yaw + delta.yaw_degrees;
    if (!std::isfinite(pitch_sum) || !std::isfinite(yaw_sum)) {
        return false;
    }

    const float changes[2]{delta.pitch_degrees, delta.yaw_degrees};
    float* const live_angles[2]{client_pitch, client_yaw};
    const float sums[2]{pitch_sum, yaw_sum};
    for (std::size_t axis = 0; axis < 2; ++axis) {
        if (changes[axis] == 0.0F) {
            continue;
        }
        const auto encoded_delta = static_cast<std::int32_t>(
            std::lround(changes[axis] * (65536.0F / 360.0F)));
        (*command_view_angles)[axis] = static_cast<std::int32_t>(
            (static_cast<std::uint32_t>((*command_view_angles)[axis]) +
             static_cast<std::uint32_t>(encoded_delta)) & 0xFFFFU);
        *live_angles[axis] = std::remainder(sums[axis], 360.0F);
    }
    return true;
}

void reset_tank_control(TankControlState* const state) noexcept {
    if (state != nullptr) {
        *state = {};
    }
}

}  // namespace wawvr::mod
