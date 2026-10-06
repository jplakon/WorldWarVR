// SPDX-License-Identifier: GPL-3.0-only
#include "current_head_local_controller_pose_logic.hpp"

#include <algorithm>
#include <cmath>

namespace wawvr::mod {
namespace {

[[nodiscard]] bool finite(const float value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool finite_vector(const wawvr::xr::Vec3f& value) noexcept {
    return finite(value.x) && finite(value.y) && finite(value.z);
}

[[nodiscard]] float dot(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] wawvr::xr::Vec3f cross(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] bool valid_orientation(
    const wawvr::xr::Quaternionf& value) noexcept {
    if (!finite(value.x) || !finite(value.y) || !finite(value.z) ||
        !finite(value.w)) {
        return false;
    }
    const float length_squared = value.x * value.x + value.y * value.y +
        value.z * value.z + value.w * value.w;
    return finite(length_squared) && length_squared > 1.0e-8F;
}

[[nodiscard]] bool normalize_orientation(
    wawvr::xr::Quaternionf* const value) noexcept {
    if (value == nullptr || !valid_orientation(*value)) {
        return false;
    }
    const float length_squared = value->x * value->x + value->y * value->y +
        value->z * value->z + value->w * value->w;
    const float inverse_length = 1.0F / std::sqrt(length_squared);
    value->x *= inverse_length;
    value->y *= inverse_length;
    value->z *= inverse_length;
    value->w *= inverse_length;
    return valid_orientation(*value);
}

[[nodiscard]] bool valid_basis(const wawvr::xr::Basis3f& value) noexcept {
    if (!finite_vector(value.forward) || !finite_vector(value.left) ||
        !finite_vector(value.up)) {
        return false;
    }
    constexpr float kUnitTolerance = 0.002F;
    constexpr float kOrthogonalTolerance = 0.002F;
    const float forward_length = dot(value.forward, value.forward);
    const float left_length = dot(value.left, value.left);
    const float up_length = dot(value.up, value.up);
    if (std::abs(forward_length - 1.0F) > kUnitTolerance ||
        std::abs(left_length - 1.0F) > kUnitTolerance ||
        std::abs(up_length - 1.0F) > kUnitTolerance ||
        std::abs(dot(value.forward, value.left)) > kOrthogonalTolerance ||
        std::abs(dot(value.forward, value.up)) > kOrthogonalTolerance ||
        std::abs(dot(value.left, value.up)) > kOrthogonalTolerance) {
        return false;
    }
    const wawvr::xr::Vec3f handed = cross(value.forward, value.left);
    return dot(handed, value.up) > 0.998F;
}

[[nodiscard]] bool valid_config(
    const CurrentHeadLocalControllerPoseFilterConfig& config) noexcept {
    return finite(config.position_response) &&
        config.position_response >= 0.0F &&
        config.position_response <= 1.0F &&
        finite(config.orientation_response) &&
        config.orientation_response >= 0.0F &&
        config.orientation_response <= 1.0F &&
        finite(config.maximum_position_discontinuity_meters) &&
        config.maximum_position_discontinuity_meters > 0.0F &&
        finite(config.maximum_orientation_discontinuity_degrees) &&
        config.maximum_orientation_discontinuity_degrees > 0.0F &&
        config.maximum_orientation_discontinuity_degrees <= 180.0F &&
        config.maximum_frame_age_milliseconds > 0;
}

[[nodiscard]] bool same_config(
    const CurrentHeadLocalControllerPoseFilterConfig& left,
    const CurrentHeadLocalControllerPoseFilterConfig& right) noexcept {
    return left.position_response == right.position_response &&
        left.orientation_response == right.orientation_response &&
        left.maximum_position_discontinuity_meters ==
            right.maximum_position_discontinuity_meters &&
        left.maximum_orientation_discontinuity_degrees ==
            right.maximum_orientation_discontinuity_degrees &&
        left.maximum_frame_age_milliseconds ==
            right.maximum_frame_age_milliseconds;
}

struct RawCurrentHeadLocalPose final {
    wawvr::xr::EnginePose engine{};
    wawvr::xr::Quaternionf aim_orientation{};
};

[[nodiscard]] bool derive_raw_pose(
    const wawvr::xr::Posef& frame_head,
    const wawvr::xr::HandActionState& hand,
    const float engine_units_per_meter,
    RawCurrentHeadLocalPose* const pose) noexcept {
    if (pose == nullptr || !finite(engine_units_per_meter) ||
        engine_units_per_meter <= 0.0F ||
        engine_units_per_meter > 1000.0F ||
        !finite_vector(frame_head.position) ||
        !valid_orientation(frame_head.orientation) ||
        !hand.grip.active || !hand.grip.position_valid ||
        !finite_vector(hand.grip.pose.position) ||
        !hand.aim.active || !hand.aim.orientation_valid ||
        !valid_orientation(hand.aim.pose.orientation)) {
        return false;
    }

    wawvr::xr::Quaternionf normalized_head = frame_head.orientation;
    wawvr::xr::Quaternionf normalized_aim = hand.aim.pose.orientation;
    if (!normalize_orientation(&normalized_head) ||
        !normalize_orientation(&normalized_aim)) {
        return false;
    }

    wawvr::xr::Posef combined_controller{};
    combined_controller.position = hand.grip.pose.position;
    combined_controller.orientation = normalized_aim;
    wawvr::xr::Posef normalized_reference = frame_head;
    normalized_reference.orientation = normalized_head;

    RawCurrentHeadLocalPose calculated{};
    calculated.engine = wawvr::xr::OpenXrPoseToIwRelative(
        combined_controller, normalized_reference, engine_units_per_meter);
    calculated.aim_orientation = wawvr::xr::Normalize(
        wawvr::xr::Multiply(
            wawvr::xr::Conjugate(normalized_head), normalized_aim));
    if (!finite_vector(calculated.engine.position) ||
        !valid_basis(calculated.engine.axis) ||
        !normalize_orientation(&calculated.aim_orientation)) {
        return false;
    }

    *pose = calculated;
    return true;
}

[[nodiscard]] bool engine_pose_from_local_components(
    const wawvr::xr::Vec3f& grip_position,
    const wawvr::xr::Quaternionf& aim_orientation,
    wawvr::xr::EnginePose* const pose) noexcept {
    if (pose == nullptr || !finite_vector(grip_position) ||
        !valid_orientation(aim_orientation)) {
        return false;
    }
    wawvr::xr::Posef local{};
    local.orientation = aim_orientation;
    const wawvr::xr::Posef identity{};
    wawvr::xr::EnginePose calculated =
        wawvr::xr::OpenXrPoseToIwRelative(local, identity, 1.0F);
    calculated.position = grip_position;
    if (!finite_vector(calculated.position) ||
        !valid_basis(calculated.axis)) {
        return false;
    }
    *pose = calculated;
    return true;
}

}  // namespace

bool current_head_local_controller_pose(
    const wawvr::xr::Posef& frame_head,
    const wawvr::xr::HandActionState& hand,
    wawvr::xr::EnginePose* const pose,
    const float engine_units_per_meter) noexcept {
    if (pose == nullptr) {
        return false;
    }
    RawCurrentHeadLocalPose calculated{};
    if (!derive_raw_pose(
            frame_head, hand, engine_units_per_meter, &calculated)) {
        return false;
    }
    *pose = calculated.engine;
    return true;
}

bool filtered_current_head_local_controller_pose(
    const wawvr::xr::Posef& frame_head,
    const wawvr::xr::HandActionState& hand,
    const std::uint64_t generation,
    const std::uint64_t publication_milliseconds,
    const std::uint64_t now_milliseconds,
    CurrentHeadLocalControllerPoseFilterState* const state,
    wawvr::xr::EnginePose* const pose,
    const CurrentHeadLocalControllerPoseFilterConfig& config,
    const float engine_units_per_meter) noexcept {
    if (state == nullptr || pose == nullptr || generation == 0 ||
        publication_milliseconds == 0 ||
        now_milliseconds < publication_milliseconds ||
        !valid_config(config) || !finite(engine_units_per_meter) ||
        engine_units_per_meter <= 0.0F ||
        engine_units_per_meter > 1000.0F ||
        now_milliseconds - publication_milliseconds >
            config.maximum_frame_age_milliseconds) {
        return false;
    }

    RawCurrentHeadLocalPose raw{};
    if (!derive_raw_pose(
            frame_head, hand, engine_units_per_meter, &raw)) {
        return false;
    }

    if (state->valid) {
        if (state->generation == 0 ||
            state->publication_milliseconds == 0 ||
            !finite(state->engine_units_per_meter) ||
            state->engine_units_per_meter != engine_units_per_meter ||
            !same_config(state->config, config) ||
            !finite_vector(state->filtered_grip_position) ||
            !valid_orientation(state->filtered_aim_orientation) ||
            !finite_vector(state->cached_pose.position) ||
            !valid_basis(state->cached_pose.axis) ||
            generation < state->generation ||
            publication_milliseconds < state->publication_milliseconds) {
            return false;
        }
        if (generation == state->generation) {
            if (publication_milliseconds !=
                state->publication_milliseconds) {
                return false;
            }
            *pose = state->cached_pose;
            return true;
        }
        if (publication_milliseconds - state->publication_milliseconds >
            config.maximum_frame_age_milliseconds) {
            return false;
        }
    }

    CurrentHeadLocalControllerPoseFilterState next{};
    if (!state->valid) {
        next.valid = true;
        next.generation = generation;
        next.publication_milliseconds = publication_milliseconds;
        next.engine_units_per_meter = engine_units_per_meter;
        next.config = config;
        next.filtered_grip_position = raw.engine.position;
        next.filtered_aim_orientation = raw.aim_orientation;
    } else {
        next = *state;
        const wawvr::xr::Vec3f position_delta{
            raw.engine.position.x - state->filtered_grip_position.x,
            raw.engine.position.y - state->filtered_grip_position.y,
            raw.engine.position.z - state->filtered_grip_position.z,
        };
        const float position_delta_squared = dot(
            position_delta, position_delta);
        const float maximum_position_delta =
            config.maximum_position_discontinuity_meters *
            engine_units_per_meter;

        wawvr::xr::Quaternionf target_orientation = raw.aim_orientation;
        float orientation_dot =
            state->filtered_aim_orientation.x * target_orientation.x +
            state->filtered_aim_orientation.y * target_orientation.y +
            state->filtered_aim_orientation.z * target_orientation.z +
            state->filtered_aim_orientation.w * target_orientation.w;
        if (!finite(position_delta_squared) ||
            position_delta_squared >
                maximum_position_delta * maximum_position_delta ||
            !finite(orientation_dot)) {
            return false;
        }

        const float absolute_orientation_dot = std::abs(orientation_dot);
        constexpr float kRadiansToDegrees =
            180.0F / 3.14159265358979323846F;
        const float orientation_delta_degrees = 2.0F *
            std::acos(std::clamp(
                absolute_orientation_dot, 0.0F, 1.0F)) *
            kRadiansToDegrees;
        if (!finite(orientation_delta_degrees) ||
            orientation_delta_degrees >
                config.maximum_orientation_discontinuity_degrees) {
            return false;
        }

        next.filtered_grip_position.x +=
            position_delta.x * config.position_response;
        next.filtered_grip_position.y +=
            position_delta.y * config.position_response;
        next.filtered_grip_position.z +=
            position_delta.z * config.position_response;

        if (orientation_dot < 0.0F) {
            target_orientation.x = -target_orientation.x;
            target_orientation.y = -target_orientation.y;
            target_orientation.z = -target_orientation.z;
            target_orientation.w = -target_orientation.w;
        }
        next.filtered_aim_orientation.x +=
            (target_orientation.x - next.filtered_aim_orientation.x) *
            config.orientation_response;
        next.filtered_aim_orientation.y +=
            (target_orientation.y - next.filtered_aim_orientation.y) *
            config.orientation_response;
        next.filtered_aim_orientation.z +=
            (target_orientation.z - next.filtered_aim_orientation.z) *
            config.orientation_response;
        next.filtered_aim_orientation.w +=
            (target_orientation.w - next.filtered_aim_orientation.w) *
            config.orientation_response;
        if (!normalize_orientation(&next.filtered_aim_orientation)) {
            return false;
        }
        next.generation = generation;
        next.publication_milliseconds = publication_milliseconds;
    }

    wawvr::xr::EnginePose calculated{};
    if (!engine_pose_from_local_components(
            next.filtered_grip_position,
            next.filtered_aim_orientation, &calculated)) {
        return false;
    }
    next.cached_pose = calculated;
    *state = next;
    *pose = calculated;
    return true;
}

void reset_current_head_local_controller_pose_filter(
    CurrentHeadLocalControllerPoseFilterState* const state) noexcept {
    if (state != nullptr) {
        *state = {};
    }
}

}  // namespace wawvr::mod
