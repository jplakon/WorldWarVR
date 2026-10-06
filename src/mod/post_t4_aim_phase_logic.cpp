// SPDX-License-Identifier: GPL-3.0-only
#include "post_t4_aim_phase_logic.hpp"

#include "xr_math.h"

#include <algorithm>
#include <cmath>

namespace wawvr::mod {
namespace {

[[nodiscard]] bool finite_vector(const wawvr::xr::Vec3f& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] bool finite_orientation(
    const wawvr::xr::Quaternionf& value) noexcept {
    if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
        !std::isfinite(value.z) || !std::isfinite(value.w)) {
        return false;
    }
    const float length_squared = value.x * value.x + value.y * value.y +
        value.z * value.z + value.w * value.w;
    return std::isfinite(length_squared) && length_squared > 1.0e-8F;
}

[[nodiscard]] bool finite_basis(const wawvr::xr::Basis3f& value) noexcept {
    return finite_vector(value.forward) && finite_vector(value.left) &&
           finite_vector(value.up);
}

[[nodiscard]] float dot(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] wawvr::xr::Vec3f compose(
    const wawvr::xr::Basis3f& basis,
    const wawvr::xr::Vec3f& local) noexcept {
    return {
        local.x * basis.forward.x + local.y * basis.left.x +
            local.z * basis.up.x,
        local.x * basis.forward.y + local.y * basis.left.y +
            local.z * basis.up.y,
        local.x * basis.forward.z + local.y * basis.left.z +
            local.z * basis.up.z,
    };
}

[[nodiscard]] wawvr::xr::Basis3f compose_axis(
    const wawvr::xr::Basis3f& basis,
    const wawvr::xr::Basis3f& local) noexcept {
    return {
        compose(basis, local.forward),
        compose(basis, local.left),
        compose(basis, local.up),
    };
}

[[nodiscard]] bool normalize(
    wawvr::xr::Vec3f value,
    wawvr::xr::Vec3f* const result) noexcept {
    if (result == nullptr || !finite_vector(value)) {
        return false;
    }
    const float length_squared = dot(value, value);
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-8F) {
        return false;
    }
    const float inverse_length = 1.0F / std::sqrt(length_squared);
    value.x *= inverse_length;
    value.y *= inverse_length;
    value.z *= inverse_length;
    if (!finite_vector(value)) {
        return false;
    }
    *result = value;
    return true;
}

[[nodiscard]] float angle_degrees(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    constexpr float kRadiansToDegrees =
        180.0F / 3.14159265358979323846F;
    return std::acos(std::clamp(dot(left, right), -1.0F, 1.0F)) *
        kRadiansToDegrees;
}

[[nodiscard]] bool equivalent_orientation(
    const wawvr::xr::Quaternionf& left,
    const wawvr::xr::Quaternionf& right) noexcept {
    if (!finite_orientation(left) || !finite_orientation(right)) {
        return false;
    }
    const wawvr::xr::Quaternionf normalized_left =
        wawvr::xr::Normalize(left);
    const wawvr::xr::Quaternionf normalized_right =
        wawvr::xr::Normalize(right);
    const float orientation_dot = std::abs(
        normalized_left.x * normalized_right.x +
        normalized_left.y * normalized_right.y +
        normalized_left.z * normalized_right.z +
        normalized_left.w * normalized_right.w);
    return std::isfinite(orientation_dot) && orientation_dot >= 0.999999F;
}

[[nodiscard]] bool equivalent_direction(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    wawvr::xr::Vec3f normalized_left{};
    wawvr::xr::Vec3f normalized_right{};
    return normalize(left, &normalized_left) &&
        normalize(right, &normalized_right) &&
        dot(normalized_left, normalized_right) >= 0.999999F;
}

[[nodiscard]] bool equivalent_basis(
    const wawvr::xr::Basis3f& left,
    const wawvr::xr::Basis3f& right) noexcept {
    return finite_basis(left) && finite_basis(right) &&
        equivalent_direction(left.forward, right.forward) &&
        equivalent_direction(left.left, right.left) &&
        equivalent_direction(left.up, right.up);
}

[[nodiscard]] bool seed_state(
    const PostT4AimPhaseInput& input,
    PostT4AimPhaseState* const state) noexcept {
    if (state == nullptr ||
        !finite_basis(input.production_controller_world_axis) ||
        !finite_basis(input.weapon_attachment_axis) ||
        !finite_vector(input.visible_world_direction)) {
        return false;
    }
    const wawvr::xr::Basis3f predicted = compose_axis(
        input.production_controller_world_axis,
        input.weapon_attachment_axis);
    wawvr::xr::Vec3f predicted_world{};
    if (!finite_basis(predicted) ||
        !normalize(predicted.forward, &predicted_world)) {
        return false;
    }
    *state = {
        true,
        input.generation,
        input.frame_id,
        input.publication_milliseconds,
        input.tracking_anchor.orientation,
        input.weapon_attachment_axis,
        predicted_world,
    };
    return true;
}

}  // namespace

bool post_t4_world_direction_to_head_local(
    const wawvr::xr::Basis3f& body_axis,
    const wawvr::xr::Posef& head_pose,
    const wawvr::xr::Posef& tracking_anchor,
    const wawvr::xr::Vec3f& world_direction,
    wawvr::xr::Vec3f* const head_local_direction) noexcept {
    if (head_local_direction == nullptr || !finite_basis(body_axis) ||
        !finite_orientation(head_pose.orientation) ||
        !finite_orientation(tracking_anchor.orientation) ||
        !finite_vector(head_pose.position) ||
        !finite_vector(tracking_anchor.position)) {
        return false;
    }

    const wawvr::xr::EnginePose head_relative =
        wawvr::xr::OpenXrPoseToIwRelative(
            head_pose, tracking_anchor, 1.0F);
    if (!finite_basis(head_relative.axis)) {
        return false;
    }
    const wawvr::xr::Basis3f head_world_axis{
        compose(body_axis, head_relative.axis.forward),
        compose(body_axis, head_relative.axis.left),
        compose(body_axis, head_relative.axis.up),
    };
    if (!finite_basis(head_world_axis)) {
        return false;
    }

    const wawvr::xr::Vec3f local{
        dot(world_direction, head_world_axis.forward),
        dot(world_direction, head_world_axis.left),
        dot(world_direction, head_world_axis.up),
    };
    return normalize(local, head_local_direction);
}

bool update_post_t4_aim_phase(
    const PostT4AimPhaseInput& input,
    PostT4AimPhaseState* const state,
    PostT4AimPhaseObservation* const observation) noexcept {
    if (state == nullptr || observation == nullptr) {
        return false;
    }
    *observation = {};

    wawvr::xr::Vec3f visible_head_local{};
    if (input.generation == 0 || input.frame_id == 0 ||
        input.publication_milliseconds == 0 ||
        !finite_basis(input.production_controller_world_axis) ||
        !finite_basis(input.weapon_attachment_axis) ||
        !finite_vector(input.visible_world_direction) ||
        !post_t4_world_direction_to_head_local(
            input.body_axis, input.head_pose, input.tracking_anchor,
            input.visible_world_direction, &visible_head_local)) {
        *state = {};
        return false;
    }

    if (!state->valid) {
        if (!seed_state(input, state)) {
            *state = {};
            return false;
        }
        return true;
    }

    // A repeated renderer consumer of one immutable controller publication is
    // neither a new comparison nor a discontinuity.
    if (input.generation == state->generation &&
        input.frame_id == state->frame_id &&
        input.publication_milliseconds == state->publication_milliseconds) {
        return true;
    }

    const bool publication_increases =
        input.publication_milliseconds > state->publication_milliseconds;
    const std::uint64_t publication_delta = publication_increases
        ? input.publication_milliseconds - state->publication_milliseconds
        : 0;
    const bool consecutive = input.generation > state->generation &&
        input.generation - state->generation == 1U &&
        input.frame_id > state->frame_id && publication_increases &&
        publication_delta <= kPostT4AimPhaseMaximumPublicationGapMs &&
        equivalent_orientation(
            input.tracking_anchor.orientation,
            state->tracking_anchor_orientation) &&
        equivalent_basis(
            input.weapon_attachment_axis,
            state->weapon_attachment_axis);
    if (!consecutive) {
        if (!seed_state(input, state)) {
            *state = {};
            return false;
        }
        return true;
    }

    const wawvr::xr::Basis3f predicted_applied_axis = compose_axis(
        input.production_controller_world_axis,
        input.weapon_attachment_axis);
    wawvr::xr::Vec3f predicted_applied_world{};
    wawvr::xr::Vec3f predicted_applied_head_local{};
    wawvr::xr::Vec3f previous_predicted_applied_head_local{};
    if (!finite_basis(predicted_applied_axis) ||
        !normalize(
            predicted_applied_axis.forward, &predicted_applied_world) ||
        !post_t4_world_direction_to_head_local(
            input.body_axis, input.head_pose, input.tracking_anchor,
            predicted_applied_world, &predicted_applied_head_local) ||
        !post_t4_world_direction_to_head_local(
            input.body_axis, input.head_pose, input.tracking_anchor,
            state->previous_predicted_applied_world,
            &previous_predicted_applied_head_local)) {
        *state = {};
        return false;
    }

    observation->compared = true;
    observation->lag0_error_degrees =
        angle_degrees(visible_head_local, predicted_applied_head_local);
    observation->lag1_error_degrees =
        angle_degrees(
            visible_head_local,
            previous_predicted_applied_head_local);
    observation->publication_delta_milliseconds =
        static_cast<float>(publication_delta);
    constexpr float kLagWinnerEpsilonDegrees = 0.0001F;
    observation->lag1_better =
        observation->lag1_error_degrees + kLagWinnerEpsilonDegrees <
        observation->lag0_error_degrees;
    const bool finite_observation =
        std::isfinite(observation->lag0_error_degrees) &&
        std::isfinite(observation->lag1_error_degrees) &&
        std::isfinite(observation->publication_delta_milliseconds);
    if (!finite_observation) {
        *state = {};
        return false;
    }
    state->generation = input.generation;
    state->frame_id = input.frame_id;
    state->publication_milliseconds = input.publication_milliseconds;
    state->tracking_anchor_orientation =
        input.tracking_anchor.orientation;
    state->weapon_attachment_axis = input.weapon_attachment_axis;
    state->previous_predicted_applied_world = predicted_applied_world;
    return true;
}

}  // namespace wawvr::mod
