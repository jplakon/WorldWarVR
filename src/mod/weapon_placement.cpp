// SPDX-License-Identifier: GPL-3.0-only
// WorldAtWarVR rigid-controller placement implementation. Comparative
// implemented against the validated T4 weapon-placement contract.
#include "weapon_placement.hpp"

#include "current_head_local_controller_pose_logic.hpp"

#include "input_mapping.hpp"
#include "xr_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace wawvr::mod {
namespace {

[[nodiscard]] bool finite(const float value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] float dot(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] wawvr::xr::Vec3f add(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] wawvr::xr::Vec3f subtract(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
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

[[nodiscard]] bool normalize(wawvr::xr::Vec3f* const value) noexcept {
    if (value == nullptr || !finite(value->x) || !finite(value->y) ||
        !finite(value->z)) {
        return false;
    }
    const float length_squared = dot(*value, *value);
    if (!finite(length_squared) || length_squared <= 1.0e-8F) {
        return false;
    }
    const float inverse_length = 1.0F / std::sqrt(length_squared);
    value->x *= inverse_length;
    value->y *= inverse_length;
    value->z *= inverse_length;
    return true;
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

[[nodiscard]] wawvr::xr::Vec3f project_rows(
    const wawvr::xr::Basis3f& basis,
    const wawvr::xr::Vec3f& world) noexcept {
    return {
        dot(world, basis.forward),
        dot(world, basis.left),
        dot(world, basis.up),
    };
}

[[nodiscard]] bool finite_vector(const wawvr::xr::Vec3f& value) noexcept {
    return finite(value.x) && finite(value.y) && finite(value.z);
}

[[nodiscard]] bool valid_orientation(
    const wawvr::xr::Quaternionf& value) noexcept {
    if (!finite(value.x) || !finite(value.y) || !finite(value.z) ||
        !finite(value.w)) {
        return false;
    }
    const float length_squared = value.x * value.x + value.y * value.y +
                                 value.z * value.z + value.w * value.w;
    return length_squared >= 0.90F && length_squared <= 1.10F;
}

[[nodiscard]] bool normalize_orientation(
    wawvr::xr::Quaternionf* const value) noexcept {
    if (value == nullptr || !finite(value->x) || !finite(value->y) ||
        !finite(value->z) || !finite(value->w)) {
        return false;
    }
    const float length_squared = value->x * value->x +
                                 value->y * value->y +
                                 value->z * value->z +
                                 value->w * value->w;
    if (!finite(length_squared) || length_squared <= 1.0e-8F) {
        return false;
    }
    const float inverse_length = 1.0F / std::sqrt(length_squared);
    value->x *= inverse_length;
    value->y *= inverse_length;
    value->z *= inverse_length;
    value->w *= inverse_length;
    return true;
}

[[nodiscard]] bool valid_basis(const wawvr::xr::Basis3f& axis) noexcept {
    if (!finite_vector(axis.forward) || !finite_vector(axis.left) ||
        !finite_vector(axis.up)) {
        return false;
    }
    constexpr float kMinimumLengthSquared = 0.80F;
    constexpr float kMaximumLengthSquared = 1.20F;
    constexpr float kMaximumAxisDot = 0.20F;
    const float forward_length = dot(axis.forward, axis.forward);
    const float left_length = dot(axis.left, axis.left);
    const float up_length = dot(axis.up, axis.up);
    return forward_length >= kMinimumLengthSquared &&
           forward_length <= kMaximumLengthSquared &&
           left_length >= kMinimumLengthSquared &&
           left_length <= kMaximumLengthSquared &&
           up_length >= kMinimumLengthSquared &&
           up_length <= kMaximumLengthSquared &&
           std::abs(dot(axis.forward, axis.left)) <= kMaximumAxisDot &&
           std::abs(dot(axis.forward, axis.up)) <= kMaximumAxisDot &&
           std::abs(dot(axis.left, axis.up)) <= kMaximumAxisDot;
}

[[nodiscard]] wawvr::xr::Vec3f row(
    const wawvr::xr::Basis3f& axis,
    const std::size_t index) noexcept {
    switch (index) {
    case 0: return axis.forward;
    case 1: return axis.left;
    default: return axis.up;
    }
}

void set_row(
    wawvr::xr::Basis3f* const axis,
    const std::size_t index,
    const wawvr::xr::Vec3f& value) noexcept {
    if (index == 0) {
        axis->forward = value;
    } else if (index == 1) {
        axis->left = value;
    } else {
        axis->up = value;
    }
}

[[nodiscard]] float component(
    const wawvr::xr::Vec3f& value,
    const std::size_t index) noexcept {
    switch (index) {
    case 0: return value.x;
    case 1: return value.y;
    default: return value.z;
    }
}

}  // namespace

namespace {

[[nodiscard]] float adaptive_weapon_response(
    const float magnitude,
    const float quiet_magnitude,
    const float fast_magnitude,
    const float quiet_response,
    const float fast_response) noexcept {
    if (!finite(magnitude) || magnitude < 0.0F ||
        !finite(quiet_magnitude) || !finite(fast_magnitude) ||
        fast_magnitude <= quiet_magnitude ||
        !finite(quiet_response) || !finite(fast_response)) {
        return fast_response;
    }
    const float ramp = std::clamp(
        (magnitude - quiet_magnitude) /
            (fast_magnitude - quiet_magnitude),
        0.0F, 1.0F);
    // Smoothstep avoids a response-slope discontinuity at either end while
    // remaining monotonic and reaching the exact COD4 response for deliberate
    // movement.
    const float shaped = ramp * ramp * (3.0F - 2.0F * ramp);
    return quiet_response +
        (fast_response - quiet_response) * shaped;
}

}  // namespace

float adaptive_weapon_position_response(
    const float error_meters) noexcept {
    return adaptive_weapon_response(
        error_meters,
        kQuietWeaponPositionErrorMeters,
        kFastWeaponPositionErrorMeters,
        kQuietWeaponPositionResponse,
        kWeaponPositionResponse);
}

float adaptive_weapon_orientation_response(
    const float error_degrees) noexcept {
    return adaptive_weapon_response(
        error_degrees,
        kQuietWeaponOrientationErrorDegrees,
        kFastWeaponOrientationErrorDegrees,
        kQuietWeaponOrientationResponse,
        kWeaponOrientationResponse);
}

bool two_hand_controller_weapon_pose(
    const ControllerWeaponPose& right,
    const ControllerWeaponPose& left,
    ControllerWeaponPose* const pose) noexcept {
    if (pose == nullptr || !finite_vector(right.grip_position) ||
        !finite_vector(left.grip_position) || !valid_basis(right.aim_axis)) {
        return false;
    }

    wawvr::xr::Vec3f pair_forward =
        subtract(left.grip_position, right.grip_position);
    const float separation_squared = dot(pair_forward, pair_forward);
    constexpr float kMinimumSeparationSquared =
        kCod4TwoHandMinimumSeparationIwUnits *
        kCod4TwoHandMinimumSeparationIwUnits;
    constexpr float kMaximumSeparationSquared =
        kCod4TwoHandMaximumSeparationIwUnits *
        kCod4TwoHandMaximumSeparationIwUnits;
    if (!finite(separation_squared) ||
        separation_squared < kMinimumSeparationSquared ||
        separation_squared > kMaximumSeparationSquared ||
        !normalize(&pair_forward)) {
        return false;
    }

    // Preserve the weapon hand's roll by projecting its up vector onto the
    // plane normal to the new hand-line forward. When those vectors are
    // parallel, use COD4's established left-axis cross-product fallback.
    const float up_along_forward = dot(right.aim_axis.up, pair_forward);
    wawvr::xr::Vec3f pair_up = subtract(
        right.aim_axis.up,
        {
            pair_forward.x * up_along_forward,
            pair_forward.y * up_along_forward,
            pair_forward.z * up_along_forward,
        });
    if (!normalize(&pair_up)) {
        pair_up = cross(right.aim_axis.left, pair_forward);
        if (!normalize(&pair_up)) {
            return false;
        }
    }

    wawvr::xr::Vec3f pair_left = cross(pair_up, pair_forward);
    if (!normalize(&pair_left)) {
        return false;
    }
    // Recompute up from the normalized forward/left pair so accumulated input
    // error cannot leak a scaled or sheared basis into weapon placement.
    pair_up = cross(pair_forward, pair_left);
    if (!normalize(&pair_up)) {
        return false;
    }

    ControllerWeaponPose calculated{};
    calculated.grip_position = right.grip_position;
    calculated.aim_axis = {pair_forward, pair_left, pair_up};
    if (!finite_vector(calculated.grip_position) ||
        !valid_basis(calculated.aim_axis)) {
        return false;
    }

    *pose = calculated;
    return true;
}

bool validated_cod4_two_hand_controller_weapon_pose(
    const bool right_pose_ready,
    const bool left_pose_ready,
    const bool raw_pair_ready,
    const ControllerWeaponPose& stabilized_right,
    const ControllerWeaponPose& raw_left,
    ControllerWeaponPose* const pose) noexcept {
    if (!right_pose_ready || !left_pose_ready || !raw_pair_ready) {
        return false;
    }
    return two_hand_controller_weapon_pose(
        stabilized_right, raw_left, pose);
}

bool two_hand_controller_weapon_pose_from_raw_grip_delta(
    const ControllerWeaponPose& stabilized_right,
    const ControllerWeaponPose& raw_right,
    const ControllerWeaponPose& raw_left,
    ControllerWeaponPose* const pose) noexcept {
    if (pose == nullptr ||
        !finite_vector(stabilized_right.grip_position) ||
        !valid_basis(stabilized_right.aim_axis) ||
        !finite_vector(raw_right.grip_position) ||
        !valid_basis(raw_right.aim_axis) ||
        !finite_vector(raw_left.grip_position)) {
        return false;
    }

    const wawvr::xr::Vec3f raw_hand_delta = subtract(
        raw_left.grip_position, raw_right.grip_position);
    ControllerWeaponPose support_on_stabilized_anchor{};
    support_on_stabilized_anchor.grip_position = add(
        stabilized_right.grip_position, raw_hand_delta);
    support_on_stabilized_anchor.aim_axis = raw_left.aim_axis;
    if (!finite_vector(support_on_stabilized_anchor.grip_position)) {
        return false;
    }

    return two_hand_controller_weapon_pose(
        stabilized_right, support_on_stabilized_anchor, pose);
}

bool cod4_two_hand_blended_controller_weapon_pose(
    const ControllerWeaponPose& stabilized_right,
    const ControllerWeaponPose& raw_right,
    const ControllerWeaponPose& stabilized_left,
    const float blend,
    ControllerWeaponPose* const pose) noexcept {
    if (pose == nullptr || !finite(blend) ||
        !finite_vector(stabilized_right.grip_position) ||
        !valid_basis(stabilized_right.aim_axis) ||
        !finite_vector(raw_right.grip_position)) {
        return false;
    }

    const float clamped_blend = std::clamp(blend, 0.0F, 1.0F);
    if (clamped_blend == 0.0F) {
        *pose = stabilized_right;
        return true;
    }

    // Keep both hand-line endpoints in the same filtered tracking-space
    // domain. raw_right remains a same-frame validity gate, but does not steer.
    ControllerWeaponPose pair_target{};
    if (!two_hand_controller_weapon_pose(
            stabilized_right, stabilized_left, &pair_target)) {
        return false;
    }
    if (clamped_blend == 1.0F) {
        *pose = pair_target;
        return true;
    }

    const float right_weight = 1.0F - clamped_blend;
    wawvr::xr::Vec3f blended_forward = add(
        {
            stabilized_right.aim_axis.forward.x * right_weight,
            stabilized_right.aim_axis.forward.y * right_weight,
            stabilized_right.aim_axis.forward.z * right_weight,
        },
        {
            pair_target.aim_axis.forward.x * clamped_blend,
            pair_target.aim_axis.forward.y * clamped_blend,
            pair_target.aim_axis.forward.z * clamped_blend,
        });
    wawvr::xr::Vec3f blended_up = add(
        {
            stabilized_right.aim_axis.up.x * right_weight,
            stabilized_right.aim_axis.up.y * right_weight,
            stabilized_right.aim_axis.up.z * right_weight,
        },
        {
            pair_target.aim_axis.up.x * clamped_blend,
            pair_target.aim_axis.up.y * clamped_blend,
            pair_target.aim_axis.up.z * clamped_blend,
        });
    if (!normalize(&blended_forward)) {
        return false;
    }

    const float up_along_forward = dot(blended_up, blended_forward);
    blended_up = subtract(
        blended_up,
        {
            blended_forward.x * up_along_forward,
            blended_forward.y * up_along_forward,
            blended_forward.z * up_along_forward,
        });
    if (!normalize(&blended_up)) {
        blended_up = cross(
            stabilized_right.aim_axis.left, blended_forward);
        if (!normalize(&blended_up)) {
            return false;
        }
    }

    wawvr::xr::Vec3f blended_left = cross(blended_up, blended_forward);
    if (!normalize(&blended_left)) {
        return false;
    }
    blended_up = cross(blended_forward, blended_left);
    if (!normalize(&blended_up)) {
        return false;
    }

    ControllerWeaponPose calculated{};
    calculated.grip_position = stabilized_right.grip_position;
    calculated.aim_axis = {
        blended_forward, blended_left, blended_up};
    if (!valid_basis(calculated.aim_axis)) {
        return false;
    }
    *pose = calculated;
    return true;
}

bool update_cod4_two_hand_engagement_pose(
    const std::uint64_t generation,
    const bool target_active,
    const wawvr::xr::Quaternionf& tracking_anchor_orientation,
    const ControllerWeaponPose& stabilized_right,
    const ControllerWeaponPose& raw_right,
    const ControllerWeaponPose& stabilized_left,
    Cod4TwoHandEngagementBlendState* const state,
    ControllerWeaponPose* const pose) noexcept {
    if (state == nullptr || pose == nullptr || generation == 0) {
        return false;
    }

    wawvr::xr::Quaternionf current_tracking_anchor =
        tracking_anchor_orientation;
    if (!valid_orientation(current_tracking_anchor) ||
        !normalize_orientation(&current_tracking_anchor)) {
        return false;
    }

    if (state->valid) {
        if (state->generation == 0 || !finite(state->blend) ||
            state->blend < 0.0F || state->blend > 1.0F ||
            !valid_orientation(state->tracking_anchor_orientation) ||
            !finite_vector(state->cached_pose.grip_position) ||
            !valid_basis(state->cached_pose.aim_axis) ||
            generation < state->generation) {
            return false;
        }

        const float anchor_dot =
            state->tracking_anchor_orientation.x * current_tracking_anchor.x +
            state->tracking_anchor_orientation.y * current_tracking_anchor.y +
            state->tracking_anchor_orientation.z * current_tracking_anchor.z +
            state->tracking_anchor_orientation.w * current_tracking_anchor.w;
        if (!finite(anchor_dot)) {
            return false;
        }
        if (anchor_dot < 0.0F) {
            current_tracking_anchor.x = -current_tracking_anchor.x;
            current_tracking_anchor.y = -current_tracking_anchor.y;
            current_tracking_anchor.z = -current_tracking_anchor.z;
            current_tracking_anchor.w = -current_tracking_anchor.w;
        }
        const bool anchor_changed =
            current_tracking_anchor.x !=
                state->tracking_anchor_orientation.x ||
            current_tracking_anchor.y !=
                state->tracking_anchor_orientation.y ||
            current_tracking_anchor.z !=
                state->tracking_anchor_orientation.z ||
            current_tracking_anchor.w !=
                state->tracking_anchor_orientation.w;
        if (generation == state->generation) {
            if (anchor_changed) {
                ControllerWeaponPose rebased{};
                if (!cod4_two_hand_blended_controller_weapon_pose(
                        stabilized_right, raw_right, stabilized_left,
                        state->blend,
                        &rebased)) {
                    return false;
                }
                Cod4TwoHandEngagementBlendState rebaselined = *state;
                rebaselined.tracking_anchor_orientation =
                    current_tracking_anchor;
                rebaselined.cached_pose = rebased;
                *state = rebaselined;
                *pose = rebased;
                return true;
            }
            *pose = state->cached_pose;
            return true;
        }
    }

    const float prior_blend = state->valid ? state->blend : 0.0F;
    const float target_blend = target_active ? 1.0F : 0.0F;
    const float response_per_publication = target_active
        ? kCod4TwoHandEngageResponse
        : kCod4TwoHandReleaseResponse;
    const std::uint64_t generation_delta =
        state->valid ? generation - state->generation : 1;
    const float response = 1.0F - std::pow(
        1.0F - response_per_publication,
        static_cast<float>(generation_delta));
    float next_blend =
        prior_blend + (target_blend - prior_blend) * response;
    if (!finite(next_blend)) {
        return false;
    }
    if (next_blend < kCod4TwoHandBlendSnapMinimum) {
        next_blend = 0.0F;
    } else if (next_blend > kCod4TwoHandBlendSnapMaximum) {
        next_blend = 1.0F;
    }

    ControllerWeaponPose calculated{};
    if (!cod4_two_hand_blended_controller_weapon_pose(
            stabilized_right, raw_right, stabilized_left, next_blend,
            &calculated)) {
        return false;
    }

    const Cod4TwoHandEngagementBlendState advanced{
        true, generation, current_tracking_anchor, next_blend, calculated};
    *state = advanced;
    *pose = calculated;
    return true;
}

void reset_cod4_two_hand_engagement_blend(
    Cod4TwoHandEngagementBlendState* const state) noexcept {
    if (state != nullptr) {
        *state = {};
    }
}

bool controller_weapon_pose(
    const ControllerFrameSnapshot& snapshot,
    const wawvr::xr::Hand hand,
    const std::uint64_t now_milliseconds,
    ControllerWeaponPose* const pose) noexcept {
    if (pose == nullptr || snapshot.frame.frame_id == 0 ||
        snapshot.frame.actions.sequence == 0 ||
        !snapshot.frame.actions.focused || !snapshot.frame.views_valid ||
        now_milliseconds < snapshot.publication_milliseconds ||
        now_milliseconds - snapshot.publication_milliseconds >
            kMaximumControllerFrameAgeMilliseconds ||
        !valid_orientation(snapshot.tracking_anchor.orientation)) {
        return false;
    }

    const auto hand_index = static_cast<std::uint32_t>(hand);
    if (hand_index >= wawvr::xr::kHandCount) {
        return false;
    }
    const auto& tracked = snapshot.frame.actions.hands[hand_index];
    if (!tracked.grip.active || !tracked.grip.position_valid ||
        !finite_vector(tracked.grip.pose.position)) {
        return false;
    }
    const auto& orientation = tracked.aim;
    if (!orientation.active || !orientation.orientation_valid ||
        !valid_orientation(orientation.pose.orientation)) {
        return false;
    }

    const wawvr::xr::EnginePose grip_relative =
        wawvr::xr::OpenXrPoseToIwRelative(
            tracked.grip.pose, snapshot.tracking_anchor,
            wawvr::xr::kIwUnitsPerMeter);
    const wawvr::xr::EnginePose orientation_relative =
        wawvr::xr::OpenXrPoseToIwRelative(
            orientation.pose, snapshot.tracking_anchor, 1.0F);
    if (!finite_vector(grip_relative.position) ||
        !valid_basis(orientation_relative.axis)) {
        return false;
    }

    pose->grip_position = grip_relative.position;
    pose->aim_axis = orientation_relative.axis;
    return true;
}

bool right_controller_weapon_pose(
    const ControllerFrameSnapshot& snapshot,
    const std::uint64_t now_milliseconds,
    RightControllerWeaponPose* const pose) noexcept {
    return controller_weapon_pose(
        snapshot, wawvr::xr::Hand::Right, now_milliseconds, pose);
}

bool published_controller_weapon_pose(
    const ControllerFrameSnapshot& snapshot,
    const wawvr::xr::Hand hand,
    const std::uint64_t now_milliseconds,
    ControllerWeaponPose* const pose) noexcept {
    if (pose == nullptr || snapshot.frame.frame_id == 0 ||
        snapshot.frame.actions.sequence == 0 ||
        !snapshot.frame.actions.focused || !snapshot.frame.views_valid ||
        now_milliseconds < snapshot.publication_milliseconds ||
        now_milliseconds - snapshot.publication_milliseconds >
            kMaximumControllerFrameAgeMilliseconds ||
        !valid_orientation(snapshot.tracking_anchor.orientation)) return false;
    const auto index = static_cast<std::uint32_t>(hand);
    if (index >= wawvr::xr::kHandCount) return false;
    const auto& filtered = snapshot.weapon_poses[index];
    if (!filtered.valid || !finite_vector(filtered.filtered_grip_position) ||
        !valid_orientation(filtered.filtered_aim_orientation)) return false;
    wawvr::xr::Posef grip{};
    grip.position = filtered.filtered_grip_position;
    wawvr::xr::Posef aim{};
    aim.orientation = filtered.filtered_aim_orientation;
    const auto grip_relative = wawvr::xr::OpenXrPoseToIwRelative(
        grip, snapshot.tracking_anchor, wawvr::xr::kIwUnitsPerMeter);
    const auto aim_relative = wawvr::xr::OpenXrPoseToIwRelative(
        aim, snapshot.tracking_anchor, 1.0F);
    if (!finite_vector(grip_relative.position) ||
        !valid_basis(aim_relative.axis)) return false;
    *pose = {grip_relative.position, aim_relative.axis};
    return true;
}

bool rebase_controller_weapon_pose_to_reference(
    const wawvr::xr::EnginePose& reference_in_source,
    const ControllerWeaponPose& source_pose,
    ControllerWeaponPose* const pose) noexcept {
    if (pose == nullptr ||
        !finite_vector(reference_in_source.position) ||
        !valid_basis(reference_in_source.axis) ||
        !finite_vector(source_pose.grip_position) ||
        !valid_basis(source_pose.aim_axis)) {
        return false;
    }

    ControllerWeaponPose calculated{};
    calculated.grip_position = project_rows(
        reference_in_source.axis,
        subtract(source_pose.grip_position, reference_in_source.position));
    calculated.aim_axis = {
        project_rows(reference_in_source.axis, source_pose.aim_axis.forward),
        project_rows(reference_in_source.axis, source_pose.aim_axis.left),
        project_rows(reference_in_source.axis, source_pose.aim_axis.up),
    };
    if (!finite_vector(calculated.grip_position) ||
        !valid_basis(calculated.aim_axis)) {
        return false;
    }

    *pose = calculated;
    return true;
}

bool current_head_local_controller_weapon_pose(
    const ControllerFrameSnapshot& snapshot,
    const wawvr::xr::Hand hand,
    const std::uint64_t now_milliseconds,
    ControllerWeaponPose* const pose) noexcept {
    if (pose == nullptr || snapshot.frame.frame_id == 0 ||
        snapshot.frame.actions.sequence == 0 ||
        !snapshot.frame.actions.focused || !snapshot.frame.views_valid ||
        now_milliseconds < snapshot.publication_milliseconds ||
        now_milliseconds - snapshot.publication_milliseconds >
            kMaximumControllerFrameAgeMilliseconds) {
        return false;
    }
    const auto index = static_cast<std::uint32_t>(hand);
    if (index >= wawvr::xr::kHandCount) {
        return false;
    }
    wawvr::xr::EnginePose calculated{};
    if (!wawvr::mod::current_head_local_controller_pose(
            snapshot.frame.head_center, snapshot.frame.actions.hands[index],
            &calculated) ||
        !finite_vector(calculated.position) || !valid_basis(calculated.axis)) {
        return false;
    }
    *pose = {calculated.position, calculated.axis};
    return true;
}

bool published_current_head_local_controller_weapon_pose(
    const ControllerFrameSnapshot& snapshot,
    const wawvr::xr::Hand hand,
    const std::uint64_t now_milliseconds,
    ControllerWeaponPose* const pose) noexcept {
    if (pose == nullptr || snapshot.frame.frame_id == 0 ||
        snapshot.frame.actions.sequence == 0 ||
        !snapshot.frame.actions.focused || !snapshot.frame.views_valid ||
        now_milliseconds < snapshot.publication_milliseconds ||
        now_milliseconds - snapshot.publication_milliseconds >
            kMaximumControllerFrameAgeMilliseconds) {
        return false;
    }
    const auto index = static_cast<std::uint32_t>(hand);
    if (index >= wawvr::xr::kHandCount) {
        return false;
    }
    const auto& filtered = snapshot.weapon_poses[index];
    if (!filtered.current_head_local_valid ||
        !finite_vector(filtered.filtered_current_head_local_pose.position) ||
        !valid_basis(filtered.filtered_current_head_local_pose.axis)) {
        return false;
    }
    *pose = {
        filtered.filtered_current_head_local_pose.position,
        filtered.filtered_current_head_local_pose.axis};
    return true;
}

bool published_cod4_current_head_local_controller_weapon_pose(
    const ControllerFrameSnapshot& snapshot,
    const wawvr::xr::Hand hand,
    const std::uint64_t now_milliseconds,
    ControllerWeaponPose* const pose) noexcept {
    if (pose == nullptr || snapshot.frame.frame_id == 0 ||
        snapshot.frame.actions.sequence == 0 ||
        !snapshot.frame.actions.focused || !snapshot.frame.views_valid ||
        now_milliseconds < snapshot.publication_milliseconds ||
        now_milliseconds - snapshot.publication_milliseconds >
            kMaximumControllerFrameAgeMilliseconds ||
        !valid_orientation(snapshot.frame.head_center.orientation)) {
        return false;
    }
    const auto index = static_cast<std::uint32_t>(hand);
    if (index >= wawvr::xr::kHandCount) {
        return false;
    }
    const auto& filtered = snapshot.weapon_poses[index];
    if (!filtered.valid || !finite_vector(filtered.filtered_grip_position) ||
        !valid_orientation(filtered.filtered_aim_orientation)) {
        return false;
    }

    wawvr::xr::Posef filtered_grip{};
    filtered_grip.position = filtered.filtered_grip_position;
    wawvr::xr::Posef filtered_aim{};
    filtered_aim.orientation = filtered.filtered_aim_orientation;
    const auto grip_relative = wawvr::xr::OpenXrPoseToIwRelative(
        filtered_grip, snapshot.frame.head_center,
        wawvr::xr::kIwUnitsPerMeter);
    const auto aim_relative = wawvr::xr::OpenXrPoseToIwRelative(
        filtered_aim, snapshot.frame.head_center, 1.0F);
    if (!finite_vector(grip_relative.position) ||
        !valid_basis(aim_relative.axis)) {
        return false;
    }
    *pose = {grip_relative.position, aim_relative.axis};
    return true;
}

bool stabilized_controller_weapon_pose(
    const ControllerFrameSnapshot& snapshot,
    const wawvr::xr::Hand hand,
    const std::uint64_t now_milliseconds,
    ControllerWeaponPoseFilterState* const state,
    ControllerWeaponPose* const pose) noexcept {
    if (state == nullptr || pose == nullptr ||
        snapshot.frame.frame_id == 0 ||
        snapshot.frame.actions.sequence == 0 ||
        !snapshot.frame.actions.focused || !snapshot.frame.views_valid ||
        now_milliseconds < snapshot.publication_milliseconds ||
        now_milliseconds - snapshot.publication_milliseconds >
            kMaximumControllerFrameAgeMilliseconds ||
        !valid_orientation(snapshot.tracking_anchor.orientation)) {
        reset_controller_weapon_pose_filter(state);
        return false;
    }

    const auto hand_index = static_cast<std::uint32_t>(hand);
    if (hand_index >= wawvr::xr::kHandCount) {
        reset_controller_weapon_pose_filter(state);
        return false;
    }
    const auto& tracked = snapshot.frame.actions.hands[hand_index];
    if (!tracked.grip.active || !tracked.grip.position_valid ||
        !finite_vector(tracked.grip.pose.position) || !tracked.aim.active ||
        !tracked.aim.orientation_valid ||
        !valid_orientation(tracked.aim.pose.orientation)) {
        return false;
    }

    wawvr::xr::Quaternionf target_orientation =
        tracked.aim.pose.orientation;
    if (!normalize_orientation(&target_orientation)) {
        return false;
    }

    const bool publication_regressed = state->valid &&
        snapshot.publication_milliseconds < state->publication_milliseconds;
    const bool publication_gap = state->valid &&
        snapshot.publication_milliseconds >= state->publication_milliseconds &&
        snapshot.publication_milliseconds - state->publication_milliseconds >
            kMaximumControllerFrameAgeMilliseconds;
    const bool generation_regressed =
        state->valid && snapshot.generation < state->generation;

    if (!state->valid || publication_regressed || publication_gap ||
        generation_regressed) {
        state->valid = true;
        state->generation = snapshot.generation;
        state->publication_milliseconds =
            snapshot.publication_milliseconds;
        state->filtered_grip_position = tracked.grip.pose.position;
        state->filtered_aim_orientation = target_orientation;
    } else if (snapshot.generation != state->generation) {
        const wawvr::xr::Vec3f position_delta = subtract(
            tracked.grip.pose.position, state->filtered_grip_position);
        const float position_delta_squared = dot(position_delta, position_delta);
        const float raw_orientation_dot = std::abs(
            state->filtered_aim_orientation.x * target_orientation.x +
            state->filtered_aim_orientation.y * target_orientation.y +
            state->filtered_aim_orientation.z * target_orientation.z +
            state->filtered_aim_orientation.w * target_orientation.w);
        constexpr float kRadiansToDegrees =
            180.0F / 3.14159265358979323846F;
        const float orientation_delta_degrees =
            2.0F *
            std::acos(std::clamp(raw_orientation_dot, 0.0F, 1.0F)) *
            kRadiansToDegrees;
        constexpr float kMaximumPositionDeltaSquared =
            kMaximumWeaponPositionDiscontinuityMeters *
            kMaximumWeaponPositionDiscontinuityMeters;
        if (!finite(position_delta_squared) ||
            position_delta_squared > kMaximumPositionDeltaSquared ||
            !finite(orientation_delta_degrees) ||
            orientation_delta_degrees >
                kMaximumWeaponOrientationDiscontinuityDegrees) {
            // Keep the last accepted sample authoritative. Resetting here
            // would let the same relocalized controller become an unchecked
            // first sample on the very next render call. The hook freezes the
            // retained weapon during its bounded recovery window and performs
            // the explicit reset only after ownership is retired/rearmed.
            return false;
        }

        state->filtered_grip_position.x +=
            (tracked.grip.pose.position.x -
             state->filtered_grip_position.x) * kWeaponPositionResponse;
        state->filtered_grip_position.y +=
            (tracked.grip.pose.position.y -
             state->filtered_grip_position.y) * kWeaponPositionResponse;
        state->filtered_grip_position.z +=
            (tracked.grip.pose.position.z -
             state->filtered_grip_position.z) * kWeaponPositionResponse;

        const float orientation_dot =
            state->filtered_aim_orientation.x * target_orientation.x +
            state->filtered_aim_orientation.y * target_orientation.y +
            state->filtered_aim_orientation.z * target_orientation.z +
            state->filtered_aim_orientation.w * target_orientation.w;
        if (orientation_dot < 0.0F) {
            target_orientation.x = -target_orientation.x;
            target_orientation.y = -target_orientation.y;
            target_orientation.z = -target_orientation.z;
            target_orientation.w = -target_orientation.w;
        }
        state->filtered_aim_orientation.x +=
            (target_orientation.x - state->filtered_aim_orientation.x) *
            kWeaponOrientationResponse;
        state->filtered_aim_orientation.y +=
            (target_orientation.y - state->filtered_aim_orientation.y) *
            kWeaponOrientationResponse;
        state->filtered_aim_orientation.z +=
            (target_orientation.z - state->filtered_aim_orientation.z) *
            kWeaponOrientationResponse;
        state->filtered_aim_orientation.w +=
            (target_orientation.w - state->filtered_aim_orientation.w) *
            kWeaponOrientationResponse;
        if (!normalize_orientation(&state->filtered_aim_orientation)) {
            reset_controller_weapon_pose_filter(state);
            return false;
        }
        state->generation = snapshot.generation;
        state->publication_milliseconds =
            snapshot.publication_milliseconds;
    }

    wawvr::xr::Posef filtered_grip{};
    filtered_grip.position = state->filtered_grip_position;
    wawvr::xr::Posef filtered_aim{};
    filtered_aim.orientation = state->filtered_aim_orientation;
    const wawvr::xr::EnginePose grip_relative =
        wawvr::xr::OpenXrPoseToIwRelative(
            filtered_grip, snapshot.tracking_anchor,
            wawvr::xr::kIwUnitsPerMeter);
    const wawvr::xr::EnginePose orientation_relative =
        wawvr::xr::OpenXrPoseToIwRelative(
            filtered_aim, snapshot.tracking_anchor, 1.0F);
    if (!finite_vector(grip_relative.position) ||
        !valid_basis(orientation_relative.axis)) {
        reset_controller_weapon_pose_filter(state);
        return false;
    }

    pose->grip_position = grip_relative.position;
    pose->aim_axis = orientation_relative.axis;
    return true;
}

void reset_controller_weapon_pose_filter(
    ControllerWeaponPoseFilterState* const state) noexcept {
    if (state != nullptr) {
        *state = {};
    }
}

bool rebaseline_two_hand_controller_weapon_poses(
    const ControllerFrameSnapshot& snapshot,
    const std::uint64_t now_milliseconds,
    std::array<ControllerWeaponPoseFilterState, wawvr::xr::kHandCount>* const
        states,
    ControllerWeaponPose* const right_pose,
    ControllerWeaponPose* const left_pose,
    ControllerWeaponPose* const two_hand_pose) noexcept {
    if (states == nullptr || right_pose == nullptr || left_pose == nullptr ||
        two_hand_pose == nullptr) {
        return false;
    }

    std::array<ControllerWeaponPoseFilterState, wawvr::xr::kHandCount>
        rebaselined_states{};
    ControllerWeaponPose rebaselined_right{};
    ControllerWeaponPose rebaselined_left{};
    ControllerWeaponPose raw_right{};
    ControllerWeaponPose raw_left{};
    ControllerWeaponPose rebaselined_pair{};
    const auto right_index = static_cast<std::size_t>(wawvr::xr::Hand::Right);
    const auto left_index = static_cast<std::size_t>(wawvr::xr::Hand::Left);
    if (!controller_weapon_pose(
            snapshot, wawvr::xr::Hand::Right, now_milliseconds,
            &raw_right) ||
        !controller_weapon_pose(
            snapshot, wawvr::xr::Hand::Left, now_milliseconds,
            &raw_left) ||
        !stabilized_controller_weapon_pose(
            snapshot, wawvr::xr::Hand::Right, now_milliseconds,
            &rebaselined_states[right_index], &rebaselined_right) ||
        !stabilized_controller_weapon_pose(
            snapshot, wawvr::xr::Hand::Left, now_milliseconds,
            &rebaselined_states[left_index], &rebaselined_left) ||
        !two_hand_controller_weapon_pose_from_raw_grip_delta(
            rebaselined_right, raw_right, raw_left, &rebaselined_pair)) {
        return false;
    }

    *states = rebaselined_states;
    *right_pose = rebaselined_right;
    *left_pose = rebaselined_left;
    *two_hand_pose = rebaselined_pair;
    return true;
}

bool stabilized_two_hand_controller_weapon_pose(
    const std::uint64_t generation,
    const std::uint64_t publication_milliseconds,
    const wawvr::xr::Quaternionf& tracking_anchor_orientation,
    const ControllerWeaponPose& raw_pair,
    TwoHandWeaponOrientationFilterState* const state,
    ControllerWeaponPose* const pose) noexcept {
    if (state == nullptr || pose == nullptr || generation == 0 ||
        publication_milliseconds == 0 ||
        !finite_vector(raw_pair.grip_position) ||
        !valid_basis(raw_pair.aim_axis)) {
        return false;
    }

    wawvr::xr::Quaternionf current_tracking_anchor =
        tracking_anchor_orientation;
    if (!valid_orientation(current_tracking_anchor) ||
        !normalize_orientation(&current_tracking_anchor)) {
        return false;
    }

    wawvr::xr::Vec3f target_direction = raw_pair.aim_axis.forward;
    if (!normalize(&target_direction)) {
        return false;
    }

    if (!state->valid) {
        TwoHandWeaponOrientationFilterState initialized{};
        initialized.valid = true;
        initialized.generation = generation;
        initialized.publication_milliseconds = publication_milliseconds;
        initialized.tracking_anchor_orientation = current_tracking_anchor;
        initialized.previous_raw_direction = target_direction;
        initialized.filtered_direction = target_direction;
        initialized.cached_axis = raw_pair.aim_axis;
        *state = initialized;
        *pose = raw_pair;
        return true;
    }

    if (state->generation == 0 || state->publication_milliseconds == 0 ||
        !valid_orientation(state->tracking_anchor_orientation) ||
        !finite_vector(state->previous_raw_direction) ||
        !finite_vector(state->filtered_direction) ||
        !valid_basis(state->cached_axis) ||
        generation < state->generation ||
        publication_milliseconds < state->publication_milliseconds) {
        return false;
    }

    const std::uint64_t elapsed_milliseconds =
        publication_milliseconds - state->publication_milliseconds;
    if (elapsed_milliseconds > kMaximumControllerFrameAgeMilliseconds) {
        return false;
    }

    const float tracking_anchor_dot =
        state->tracking_anchor_orientation.x * current_tracking_anchor.x +
        state->tracking_anchor_orientation.y * current_tracking_anchor.y +
        state->tracking_anchor_orientation.z * current_tracking_anchor.z +
        state->tracking_anchor_orientation.w * current_tracking_anchor.w;
    if (!finite(tracking_anchor_dot)) {
        return false;
    }
    // Normalize q and -q to the same representation relative to the stored
    // anchor before doing an exact cache-key comparison. Identical snapshot
    // data then remains bit-stable, while every representable anchor change is
    // observed without an angular dead zone.
    if (tracking_anchor_dot < 0.0F) {
        current_tracking_anchor.x = -current_tracking_anchor.x;
        current_tracking_anchor.y = -current_tracking_anchor.y;
        current_tracking_anchor.z = -current_tracking_anchor.z;
        current_tracking_anchor.w = -current_tracking_anchor.w;
    }
    const bool tracking_anchor_changed =
        current_tracking_anchor.x != state->tracking_anchor_orientation.x ||
        current_tracking_anchor.y != state->tracking_anchor_orientation.y ||
        current_tracking_anchor.z != state->tracking_anchor_orientation.z ||
        current_tracking_anchor.w != state->tracking_anchor_orientation.w;
    if (tracking_anchor_changed) {
        // Body-yaw synchronization changes this camera-local coordinate frame
        // and counter-rotates the game camera without publishing a new XR
        // sample. Never interpolate across that basis change: doing so turns a
        // world-invariant aim into a visible residual yaw error.
        TwoHandWeaponOrientationFilterState rebaselined{};
        rebaselined.valid = true;
        rebaselined.generation = generation;
        rebaselined.publication_milliseconds = publication_milliseconds;
        rebaselined.tracking_anchor_orientation = current_tracking_anchor;
        rebaselined.previous_raw_direction = target_direction;
        rebaselined.filtered_direction = target_direction;
        rebaselined.cached_axis = raw_pair.aim_axis;
        *state = rebaselined;
        *pose = raw_pair;
        return true;
    }

    ControllerWeaponPose filtered_pair{};
    filtered_pair.grip_position = raw_pair.grip_position;

    // Both values identify one published XR sample. Repeated consumers of a
    // generation, and a generation that aliases the previous millisecond,
    // must observe exactly the same orientation without advancing history.
    if (generation == state->generation ||
        publication_milliseconds == state->publication_milliseconds) {
        filtered_pair.aim_axis = state->cached_axis;
        *pose = filtered_pair;
        return true;
    }

    wawvr::xr::Vec3f previous_raw_direction =
        state->previous_raw_direction;
    wawvr::xr::Vec3f filtered_direction = state->filtered_direction;
    if (!normalize(&previous_raw_direction) ||
        !normalize(&filtered_direction)) {
        return false;
    }

    const float adjacent_direction_dot = std::clamp(
        dot(previous_raw_direction, target_direction), -1.0F, 1.0F);
    constexpr float kRadiansToDegrees =
        180.0F / 3.14159265358979323846F;
    const float direction_step_degrees =
        std::acos(adjacent_direction_dot) *
        kRadiansToDegrees;
    if (!finite(direction_step_degrees) ||
        direction_step_degrees >
            kMaximumWeaponOrientationDiscontinuityDegrees) {
        return false;
    }

    const float response = 1.0F - std::exp2(
        -static_cast<float>(elapsed_milliseconds) /
        kTwoHandDirectionFilterHalfLifeMilliseconds);
    if (!finite(response) || response <= 0.0F || response > 1.0F) {
        return false;
    }

    // Spherical interpolation gives the exponential response a true time
    // constant: subdividing the same elapsed interval produces the same result
    // for a stationary target. A near-collinear normalized lerp avoids the
    // poorly conditioned sine denominator without changing visible behavior.
    const float filter_target_dot = std::clamp(
        dot(filtered_direction, target_direction), -1.0F, 1.0F);
    if (filter_target_dot > 0.9995F) {
        filtered_direction = add(
            {
                filtered_direction.x * (1.0F - response),
                filtered_direction.y * (1.0F - response),
                filtered_direction.z * (1.0F - response),
            },
            {
                target_direction.x * response,
                target_direction.y * response,
                target_direction.z * response,
            });
    } else {
        const float angle = std::acos(filter_target_dot);
        const float sine_angle = std::sin(angle);
        if (!finite(angle) || !finite(sine_angle) ||
            std::abs(sine_angle) <= 1.0e-6F) {
            return false;
        }
        const float filtered_weight =
            std::sin((1.0F - response) * angle) / sine_angle;
        const float target_weight =
            std::sin(response * angle) / sine_angle;
        filtered_direction = add(
            {
                filtered_direction.x * filtered_weight,
                filtered_direction.y * filtered_weight,
                filtered_direction.z * filtered_weight,
            },
            {
                target_direction.x * target_weight,
                target_direction.y * target_weight,
                target_direction.z * target_weight,
            });
    }
    if (!normalize(&filtered_direction)) {
        return false;
    }

    // Preserve the current dominant-hand roll while replacing only the noisy
    // hand-line forward direction. This is the same projected-up construction
    // used by two_hand_controller_weapon_pose().
    const float up_along_forward =
        dot(raw_pair.aim_axis.up, filtered_direction);
    wawvr::xr::Vec3f filtered_up = subtract(
        raw_pair.aim_axis.up,
        {
            filtered_direction.x * up_along_forward,
            filtered_direction.y * up_along_forward,
            filtered_direction.z * up_along_forward,
        });
    if (!normalize(&filtered_up)) {
        filtered_up = cross(raw_pair.aim_axis.left, filtered_direction);
        if (!normalize(&filtered_up)) {
            return false;
        }
    }
    wawvr::xr::Vec3f filtered_left =
        cross(filtered_up, filtered_direction);
    if (!normalize(&filtered_left)) {
        return false;
    }
    filtered_up = cross(filtered_direction, filtered_left);
    if (!normalize(&filtered_up)) {
        return false;
    }
    filtered_pair.aim_axis = {
        filtered_direction, filtered_left, filtered_up};
    if (!valid_basis(filtered_pair.aim_axis)) {
        return false;
    }

    TwoHandWeaponOrientationFilterState advanced = *state;
    advanced.generation = generation;
    advanced.publication_milliseconds = publication_milliseconds;
    advanced.previous_raw_direction = target_direction;
    advanced.filtered_direction = filtered_direction;
    advanced.cached_axis = filtered_pair.aim_axis;
    *state = advanced;
    *pose = filtered_pair;
    return true;
}

void reset_two_hand_weapon_orientation_filter(
    TwoHandWeaponOrientationFilterState* const state) noexcept {
    if (state != nullptr) {
        *state = {};
    }
}

bool update_pistol_two_hand_support_pose(
    const std::uint64_t generation,
    const std::uint64_t publication_milliseconds,
    const wawvr::xr::Quaternionf& tracking_anchor_orientation,
    const ControllerWeaponPose& stabilized_right,
    const ControllerWeaponPose& raw_right,
    const ControllerWeaponPose& raw_left,
    RightRayTwoHandSteeringState* const state,
    ControllerWeaponPose* const pose) noexcept {
    if (state == nullptr || pose == nullptr || generation == 0 ||
        publication_milliseconds == 0 ||
        !finite_vector(stabilized_right.grip_position) ||
        !valid_basis(stabilized_right.aim_axis) ||
        !finite_vector(raw_right.grip_position) ||
        !valid_basis(raw_right.aim_axis) ||
        !finite_vector(raw_left.grip_position)) {
        return false;
    }
    wawvr::xr::Quaternionf anchor = tracking_anchor_orientation;
    if (!valid_orientation(anchor) || !normalize_orientation(&anchor)) {
        return false;
    }
    if (state->valid) {
        if (state->generation == 0 || state->publication_milliseconds == 0 ||
            generation < state->generation ||
            publication_milliseconds < state->publication_milliseconds ||
            !valid_orientation(state->tracking_anchor_orientation)) {
            return false;
        }
        // Match the quaternion hemisphere for exact cache/anchor identity.
        const auto& previous = state->tracking_anchor_orientation;
        const float anchor_dot = previous.x * anchor.x + previous.y * anchor.y +
            previous.z * anchor.z + previous.w * anchor.w;
        if (anchor_dot < 0.0F) {
            anchor.x = -anchor.x;
            anchor.y = -anchor.y;
            anchor.z = -anchor.z;
            anchor.w = -anchor.w;
        }
    }

    // Fully replace the directional constraint rather than inheriting a
    // rifle's support steering history. The immutable stabilized right pose
    // already comes from this XR generation, so repeated consumers see the
    // same ray without introducing another orientation filter or latch angle.
    constexpr wawvr::xr::Vec3f forward{1, 0, 0};
    RightRayTwoHandSteeringState staged{};
    staged.valid = true;
    staged.generation = generation;
    staged.publication_milliseconds = publication_milliseconds;
    staged.tracking_anchor_orientation = anchor;
    staged.phase = RightRayTwoHandSteeringPhase::Quiet;
    staged.quiet_input_anchor_right_local = forward;
    staged.event_input_anchor_right_local = forward;
    staged.event_output_forward_right_local = forward;
    staged.accepted_forward_right_local = forward;
    staged.converging_target_forward_right_local = forward;
    staged.previous_support_direction_right_local = forward;
    staged.settling_input_anchor_right_local = forward;
    staged.cached_pose = stabilized_right;
    *state = staged;
    *pose = stabilized_right;
    return true;
}

bool update_right_ray_two_hand_steering_pose(
    const std::uint64_t generation,
    const std::uint64_t publication_milliseconds,
    const wawvr::xr::Quaternionf& tracking_anchor_orientation,
    const ControllerWeaponPose& stabilized_right,
    const ControllerWeaponPose& raw_right,
    const ControllerWeaponPose& raw_left,
    RightRayTwoHandSteeringState* const state,
    ControllerWeaponPose* const pose) noexcept {
    if (state == nullptr || pose == nullptr || generation == 0 ||
        publication_milliseconds == 0 ||
        !finite_vector(stabilized_right.grip_position) ||
        !valid_basis(stabilized_right.aim_axis) ||
        !finite_vector(raw_right.grip_position) ||
        !valid_basis(raw_right.aim_axis) ||
        !finite_vector(raw_left.grip_position)) {
        return false;
    }

    wawvr::xr::Quaternionf current_tracking_anchor =
        tracking_anchor_orientation;
    if (!valid_orientation(current_tracking_anchor) ||
        !normalize_orientation(&current_tracking_anchor)) {
        return false;
    }

    wawvr::xr::Vec3f raw_hand_delta = subtract(
        raw_left.grip_position, raw_right.grip_position);
    const float separation_squared = dot(raw_hand_delta, raw_hand_delta);
    constexpr float kMinimumSeparationSquared =
        kCod4TwoHandMinimumSeparationIwUnits *
        kCod4TwoHandMinimumSeparationIwUnits;
    constexpr float kMaximumSeparationSquared =
        kCod4TwoHandMaximumSeparationIwUnits *
        kCod4TwoHandMaximumSeparationIwUnits;
    if (!finite(separation_squared) ||
        separation_squared < kMinimumSeparationSquared ||
        separation_squared > kMaximumSeparationSquared ||
        !normalize(&raw_hand_delta)) {
        return false;
    }

    wawvr::xr::Vec3f current_support_right_local = project_rows(
        raw_right.aim_axis, raw_hand_delta);
    if (!normalize(&current_support_right_local)) {
        return false;
    }

    constexpr float kRadiansToDegrees =
        180.0F / 3.14159265358979323846F;
    constexpr float kDegreesToRadians =
        3.14159265358979323846F / 180.0F;
    constexpr wawvr::xr::Vec3f kRightLocalForward{1.0F, 0.0F, 0.0F};

    const auto direction_axis_angle = [=](
        wawvr::xr::Vec3f from,
        wawvr::xr::Vec3f to,
        wawvr::xr::Vec3f* const axis,
        float* const degrees) noexcept -> bool {
        if (axis == nullptr || degrees == nullptr || !normalize(&from) ||
            !normalize(&to)) {
            return false;
        }
        const float cosine = std::clamp(dot(from, to), -1.0F, 1.0F);
        const float angle = std::acos(cosine);
        if (!finite(angle)) {
            return false;
        }
        *degrees = angle * kRadiansToDegrees;
        *axis = cross(from, to);
        if (*degrees <= 1.0e-4F) {
            *axis = {};
            return true;
        }
        return normalize(axis);
    };
    const auto rotate_about_axis = [=](
        const wawvr::xr::Vec3f& value,
        wawvr::xr::Vec3f axis,
        const float degrees,
        wawvr::xr::Vec3f* const rotated) noexcept -> bool {
        if (rotated == nullptr || !finite_vector(value) || !finite(degrees)) {
            return false;
        }
        if (std::abs(degrees) <= 1.0e-4F) {
            *rotated = value;
            return normalize(rotated);
        }
        if (!normalize(&axis)) {
            return false;
        }
        const float radians = degrees * kDegreesToRadians;
        const float sine = std::sin(radians);
        const float cosine = std::cos(radians);
        const wawvr::xr::Vec3f axis_cross_value = cross(axis, value);
        const float axis_dot_value = dot(axis, value);
        *rotated = add(
            add(
                {
                    value.x * cosine,
                    value.y * cosine,
                    value.z * cosine,
                },
                {
                    axis_cross_value.x * sine,
                    axis_cross_value.y * sine,
                    axis_cross_value.z * sine,
                }),
            {
                axis.x * axis_dot_value * (1.0F - cosine),
                axis.y * axis_dot_value * (1.0F - cosine),
                axis.z * axis_dot_value * (1.0F - cosine),
            });
        return normalize(rotated);
    };
    const auto rotate_from_to = [&direction_axis_angle, &rotate_about_axis](
        const wawvr::xr::Vec3f& from,
        const wawvr::xr::Vec3f& to,
        const wawvr::xr::Vec3f& value,
        wawvr::xr::Vec3f* const rotated) noexcept -> bool {
        wawvr::xr::Vec3f axis{};
        float degrees = 0.0F;
        return direction_axis_angle(from, to, &axis, &degrees) &&
            rotate_about_axis(value, axis, degrees, rotated);
    };
    const auto rotate_toward = [&direction_axis_angle, &rotate_about_axis](
        const wawvr::xr::Vec3f& from,
        const wawvr::xr::Vec3f& to,
        const float maximum_degrees,
        wawvr::xr::Vec3f* const rotated) noexcept -> bool {
        if (!finite(maximum_degrees) || maximum_degrees < 0.0F) {
            return false;
        }
        wawvr::xr::Vec3f axis{};
        float degrees = 0.0F;
        if (!direction_axis_angle(from, to, &axis, &degrees)) {
            return false;
        }
        if (degrees <= maximum_degrees) {
            *rotated = to;
            return normalize(rotated);
        }
        return rotate_about_axis(from, axis, maximum_degrees, rotated);
    };
    const auto build_pose = [&stabilized_right](
        wawvr::xr::Vec3f accepted_forward_right_local,
        ControllerWeaponPose* const calculated) noexcept -> bool {
        if (calculated == nullptr ||
            !normalize(&accepted_forward_right_local)) {
            return false;
        }
        *calculated = stabilized_right;
        if (accepted_forward_right_local.x == 1.0F &&
            accepted_forward_right_local.y == 0.0F &&
            accepted_forward_right_local.z == 0.0F) {
            return true;
        }

        wawvr::xr::Vec3f corrected_forward = compose(
            stabilized_right.aim_axis, accepted_forward_right_local);
        if (!normalize(&corrected_forward)) {
            return false;
        }
        const float up_along_forward = dot(
            stabilized_right.aim_axis.up, corrected_forward);
        wawvr::xr::Vec3f corrected_up = subtract(
            stabilized_right.aim_axis.up,
            {
                corrected_forward.x * up_along_forward,
                corrected_forward.y * up_along_forward,
                corrected_forward.z * up_along_forward,
            });
        if (!normalize(&corrected_up)) {
            corrected_up = cross(
                stabilized_right.aim_axis.left, corrected_forward);
            if (!normalize(&corrected_up)) {
                return false;
            }
        }
        wawvr::xr::Vec3f corrected_left = cross(
            corrected_up, corrected_forward);
        if (!normalize(&corrected_left)) {
            return false;
        }
        corrected_up = cross(corrected_forward, corrected_left);
        if (!normalize(&corrected_up)) {
            return false;
        }
        calculated->aim_axis = {
            corrected_forward, corrected_left, corrected_up};
        return finite_vector(calculated->grip_position) &&
            valid_basis(calculated->aim_axis);
    };

    bool tracking_anchor_changed = false;
    if (state->valid) {
        const bool valid_phase =
            state->phase == RightRayTwoHandSteeringPhase::Quiet ||
            state->phase == RightRayTwoHandSteeringPhase::Arming ||
            state->phase == RightRayTwoHandSteeringPhase::Steering ||
            state->phase == RightRayTwoHandSteeringPhase::Converging ||
            state->phase == RightRayTwoHandSteeringPhase::Settling;
        if (state->generation == 0 ||
            state->publication_milliseconds == 0 || !valid_phase ||
            !valid_orientation(state->tracking_anchor_orientation) ||
            !finite_vector(state->quiet_input_anchor_right_local) ||
            !finite_vector(state->event_input_anchor_right_local) ||
            !finite_vector(state->event_output_forward_right_local) ||
            !finite_vector(state->accepted_forward_right_local) ||
            !finite_vector(
                state->converging_target_forward_right_local) ||
            !finite_vector(
                state->previous_support_direction_right_local) ||
            !finite_vector(state->settling_input_anchor_right_local) ||
            (state->phase == RightRayTwoHandSteeringPhase::Arming &&
             !finite_vector(state->arming_axis_right_local)) ||
            !finite_vector(state->cached_pose.grip_position) ||
            !valid_basis(state->cached_pose.aim_axis) ||
            generation < state->generation ||
            publication_milliseconds < state->publication_milliseconds) {
            return false;
        }
        const float anchor_dot =
            state->tracking_anchor_orientation.x *
                current_tracking_anchor.x +
            state->tracking_anchor_orientation.y *
                current_tracking_anchor.y +
            state->tracking_anchor_orientation.z *
                current_tracking_anchor.z +
            state->tracking_anchor_orientation.w *
                current_tracking_anchor.w;
        if (!finite(anchor_dot)) {
            return false;
        }
        if (anchor_dot < 0.0F) {
            current_tracking_anchor.x = -current_tracking_anchor.x;
            current_tracking_anchor.y = -current_tracking_anchor.y;
            current_tracking_anchor.z = -current_tracking_anchor.z;
            current_tracking_anchor.w = -current_tracking_anchor.w;
        }
        tracking_anchor_changed =
            current_tracking_anchor.x !=
                state->tracking_anchor_orientation.x ||
            current_tracking_anchor.y !=
                state->tracking_anchor_orientation.y ||
            current_tracking_anchor.z !=
                state->tracking_anchor_orientation.z ||
            current_tracking_anchor.w !=
                state->tracking_anchor_orientation.w;
        if (generation == state->generation && !tracking_anchor_changed) {
            *pose = state->cached_pose;
            return true;
        }
    }

    RightRayTwoHandSteeringState advanced{};
    if (!state->valid) {
        advanced.valid = true;
        advanced.generation = generation;
        advanced.publication_milliseconds = publication_milliseconds;
        advanced.tracking_anchor_orientation = current_tracking_anchor;
        advanced.phase = RightRayTwoHandSteeringPhase::Quiet;
        advanced.quiet_input_anchor_right_local =
            current_support_right_local;
        advanced.event_input_anchor_right_local =
            current_support_right_local;
        advanced.event_output_forward_right_local = kRightLocalForward;
        advanced.accepted_forward_right_local = kRightLocalForward;
        advanced.converging_target_forward_right_local =
            kRightLocalForward;
        advanced.previous_support_direction_right_local =
            current_support_right_local;
        advanced.settling_input_anchor_right_local =
            current_support_right_local;
        advanced.cached_pose = stabilized_right;
        *state = advanced;
        *pose = stabilized_right;
        return true;
    }

    advanced = *state;
    if (!normalize(&advanced.quiet_input_anchor_right_local) ||
        !normalize(&advanced.event_input_anchor_right_local) ||
        !normalize(&advanced.event_output_forward_right_local) ||
        !normalize(&advanced.accepted_forward_right_local) ||
        !normalize(&advanced.converging_target_forward_right_local) ||
        !normalize(&advanced.previous_support_direction_right_local) ||
        !normalize(&advanced.settling_input_anchor_right_local) ||
        ((advanced.phase == RightRayTwoHandSteeringPhase::Arming ||
          advanced.phase == RightRayTwoHandSteeringPhase::Steering) &&
         !normalize(&advanced.arming_axis_right_local))) {
        return false;
    }

    // A tracking-anchor rebase or duplicate timestamp may reuse a hand sample.
    // Recompose the accepted bore around the current stable root without
    // advancing any clutch timer or steering state.
    const bool new_timed_sample =
        generation != state->generation &&
        publication_milliseconds != state->publication_milliseconds;
    if (new_timed_sample) {
        const std::uint64_t elapsed_milliseconds =
            publication_milliseconds - state->publication_milliseconds;
        wawvr::xr::Vec3f step_axis{};
        float step_degrees = 0.0F;
        if (!direction_axis_angle(
                advanced.previous_support_direction_right_local,
                current_support_right_local, &step_axis, &step_degrees)) {
            return false;
        }
        const float elapsed_seconds =
            static_cast<float>(elapsed_milliseconds) / 1000.0F;
        if (!finite(elapsed_seconds) || elapsed_seconds <= 0.0F) {
            return false;
        }
        const float step_speed_degrees_per_second =
            step_degrees / elapsed_seconds;
        if (!finite(step_speed_degrees_per_second)) {
            return false;
        }

        if (elapsed_milliseconds >
            kRightRayTwoHandMaximumIntentSampleIntervalMilliseconds) {
            // A cadence gap is not evidence of intent. Absorb the new support
            // sample as a quiet input zero without moving the accepted bore.
            advanced.phase = RightRayTwoHandSteeringPhase::Quiet;
            advanced.quiet_input_anchor_right_local =
                current_support_right_local;
            advanced.event_input_anchor_right_local =
                current_support_right_local;
            advanced.event_output_forward_right_local =
                advanced.accepted_forward_right_local;
            advanced.converging_target_forward_right_local =
                advanced.accepted_forward_right_local;
            advanced.arming_axis_right_local = {};
            advanced.settling_input_anchor_right_local =
                current_support_right_local;
            advanced.arming_elapsed_milliseconds = 0;
            advanced.settling_elapsed_milliseconds = 0;
        } else if (advanced.phase ==
                   RightRayTwoHandSteeringPhase::Quiet) {
            wawvr::xr::Vec3f error_axis{};
            float error_degrees = 0.0F;
            wawvr::xr::Vec3f previous_error_axis{};
            float previous_error_degrees = 0.0F;
            if (!direction_axis_angle(
                    advanced.quiet_input_anchor_right_local,
                    current_support_right_local, &error_axis,
                    &error_degrees) ||
                !direction_axis_angle(
                    advanced.quiet_input_anchor_right_local,
                    advanced.previous_support_direction_right_local,
                    &previous_error_axis, &previous_error_degrees)) {
                return false;
            }
            const float outward_speed =
                (error_degrees - previous_error_degrees) / elapsed_seconds;
            if (error_degrees >=
                    kRightRayTwoHandIntentEngageDegrees &&
                outward_speed >=
                    kRightRayTwoHandMinimumIntentSpeedDegreesPerSecond &&
                step_speed_degrees_per_second >=
                    kRightRayTwoHandMinimumIntentSpeedDegreesPerSecond &&
                normalize(&error_axis)) {
                advanced.phase = RightRayTwoHandSteeringPhase::Arming;
                advanced.event_input_anchor_right_local =
                    current_support_right_local;
                advanced.event_output_forward_right_local =
                    advanced.accepted_forward_right_local;
                advanced.arming_axis_right_local = error_axis;
                advanced.arming_elapsed_milliseconds = 0;
            }
        } else if (advanced.phase ==
                   RightRayTwoHandSteeringPhase::Arming) {
            wawvr::xr::Vec3f error_axis{};
            float error_degrees = 0.0F;
            wawvr::xr::Vec3f previous_error_axis{};
            float previous_error_degrees = 0.0F;
            if (!direction_axis_angle(
                    advanced.quiet_input_anchor_right_local,
                    current_support_right_local, &error_axis,
                    &error_degrees) ||
                !direction_axis_angle(
                    advanced.quiet_input_anchor_right_local,
                    advanced.previous_support_direction_right_local,
                    &previous_error_axis, &previous_error_degrees)) {
                return false;
            }
            const float outward_speed =
                (error_degrees - previous_error_degrees) / elapsed_seconds;
            const bool coherent_step = step_degrees > 1.0e-4F &&
                normalize(&step_axis) &&
                dot(step_axis, advanced.arming_axis_right_local) >=
                    kRightRayTwoHandIntentDirectionCoherenceCosine;
            const bool qualifying_intent =
                error_degrees >= kRightRayTwoHandIntentEngageDegrees &&
                outward_speed >=
                    kRightRayTwoHandMinimumIntentSpeedDegreesPerSecond &&
                step_speed_degrees_per_second >=
                    kRightRayTwoHandMinimumIntentSpeedDegreesPerSecond &&
                coherent_step;
            if (error_degrees <=
                    kRightRayTwoHandIntentReleaseDegrees ||
                !qualifying_intent) {
                advanced.phase = RightRayTwoHandSteeringPhase::Quiet;
                advanced.quiet_input_anchor_right_local =
                    current_support_right_local;
                advanced.event_input_anchor_right_local =
                    current_support_right_local;
                advanced.event_output_forward_right_local =
                    advanced.accepted_forward_right_local;
                advanced.converging_target_forward_right_local =
                    advanced.accepted_forward_right_local;
                advanced.arming_axis_right_local = {};
                advanced.arming_elapsed_milliseconds = 0;
            } else {
                advanced.arming_elapsed_milliseconds +=
                    elapsed_milliseconds;
                if (advanced.arming_elapsed_milliseconds >=
                    kRightRayTwoHandIntentArmMilliseconds) {
                    advanced.phase =
                        RightRayTwoHandSteeringPhase::Steering;
                    advanced.settling_input_anchor_right_local =
                        current_support_right_local;
                    advanced.arming_elapsed_milliseconds = 0;
                    advanced.settling_elapsed_milliseconds = 0;
                }
            }
        } else if (advanced.phase ==
                   RightRayTwoHandSteeringPhase::Steering) {
            const bool coherent_active_step =
                step_degrees > 1.0e-4F && normalize(&step_axis) &&
                step_speed_degrees_per_second >=
                    kRightRayTwoHandMinimumIntentSpeedDegreesPerSecond &&
                normalize(&advanced.arming_axis_right_local) &&
                dot(step_axis, advanced.arming_axis_right_local) >=
                    kRightRayTwoHandIntentDirectionCoherenceCosine;

            // A reversed, stalled, or noisy sample closes the live raw-hand
            // connection before it can perturb the bore.  Preserve the last
            // coherent support sample as a fixed endpoint and converge toward
            // it at the same bounded slew rate.
            const wawvr::xr::Vec3f& target_support =
                coherent_active_step
                ? current_support_right_local
                : advanced.previous_support_direction_right_local;
            wawvr::xr::Vec3f target_forward{};
            if (!rotate_from_to(
                    advanced.event_input_anchor_right_local,
                    target_support,
                    advanced.event_output_forward_right_local,
                    &target_forward)) {
                return false;
            }
            wawvr::xr::Vec3f correction_axis{};
            float correction_degrees = 0.0F;
            if (!direction_axis_angle(
                    kRightLocalForward, target_forward,
                    &correction_axis, &correction_degrees)) {
                return false;
            }
            if (correction_degrees >
                kRightRayTwoHandMaximumCorrectionDegrees) {
                if (!rotate_about_axis(
                        kRightLocalForward, correction_axis,
                        kRightRayTwoHandMaximumCorrectionDegrees,
                        &target_forward)) {
                    return false;
                }
            }

            const float maximum_step_degrees =
                kRightRayTwoHandMaximumSlewDegreesPerSecond *
                elapsed_seconds;
            if (!rotate_toward(
                    advanced.accepted_forward_right_local, target_forward,
                    maximum_step_degrees,
                    &advanced.accepted_forward_right_local)) {
                return false;
            }
            if (coherent_active_step) {
                // A deliberate sweep remains continuously steerable, even
                // when its direction curves gradually.  Only coherent motion
                // refreshes this activity latch; high-frequency reversals do
                // not keep the clutch open forever.
                advanced.arming_axis_right_local = step_axis;
                advanced.settling_input_anchor_right_local =
                    current_support_right_local;
                advanced.settling_elapsed_milliseconds = 0;
            } else {
                advanced.phase =
                    RightRayTwoHandSteeringPhase::Converging;
                advanced.converging_target_forward_right_local =
                    target_forward;
                advanced.settling_input_anchor_right_local =
                    current_support_right_local;
                advanced.settling_elapsed_milliseconds = 0;
            }
        } else if (advanced.phase ==
                   RightRayTwoHandSteeringPhase::Converging) {
            const float maximum_step_degrees =
                kRightRayTwoHandMaximumSlewDegreesPerSecond *
                elapsed_seconds;
            if (!rotate_toward(
                    advanced.accepted_forward_right_local,
                    advanced.converging_target_forward_right_local,
                    maximum_step_degrees,
                    &advanced.accepted_forward_right_local)) {
                return false;
            }
            wawvr::xr::Vec3f target_error_axis{};
            float target_error_degrees = 0.0F;
            if (!direction_axis_angle(
                    advanced.accepted_forward_right_local,
                    advanced.converging_target_forward_right_local,
                    &target_error_axis, &target_error_degrees)) {
                return false;
            }
            if (target_error_degrees <=
                kRightRayTwoHandSettleToleranceDegrees) {
                advanced.phase =
                    RightRayTwoHandSteeringPhase::Settling;
                advanced.settling_input_anchor_right_local =
                    current_support_right_local;
                advanced.settling_elapsed_milliseconds = 0;
            }
        } else {
            wawvr::xr::Vec3f settle_axis{};
            float settle_degrees = 0.0F;
            if (!direction_axis_angle(
                    advanced.settling_input_anchor_right_local,
                    current_support_right_local, &settle_axis,
                    &settle_degrees)) {
                return false;
            }
            if (settle_degrees <=
                kRightRayTwoHandIntentReleaseDegrees) {
                // A bounded positional envelope is a more reliable quiet
                // classifier than instantaneous velocity: sub-degree tracker
                // shimmer can be fast while still expressing no intent.
                advanced.settling_elapsed_milliseconds +=
                    elapsed_milliseconds;
                if (advanced.settling_elapsed_milliseconds >=
                    kRightRayTwoHandSettleMilliseconds) {
                    advanced.phase = RightRayTwoHandSteeringPhase::Quiet;
                    advanced.quiet_input_anchor_right_local =
                        current_support_right_local;
                    advanced.event_input_anchor_right_local =
                        current_support_right_local;
                    advanced.event_output_forward_right_local =
                        advanced.accepted_forward_right_local;
                    advanced.converging_target_forward_right_local =
                        advanced.accepted_forward_right_local;
                    advanced.arming_axis_right_local = {};
                    advanced.arming_elapsed_milliseconds = 0;
                    advanced.settling_elapsed_milliseconds = 0;
                }
            } else if (settle_degrees >=
                           kRightRayTwoHandIntentEngageDegrees &&
                       step_speed_degrees_per_second >=
                           kRightRayTwoHandMinimumIntentSpeedDegreesPerSecond &&
                       normalize(&settle_axis)) {
                advanced.phase = RightRayTwoHandSteeringPhase::Arming;
                advanced.quiet_input_anchor_right_local =
                    advanced.settling_input_anchor_right_local;
                advanced.event_input_anchor_right_local =
                    current_support_right_local;
                advanced.event_output_forward_right_local =
                    advanced.accepted_forward_right_local;
                advanced.converging_target_forward_right_local =
                    advanced.accepted_forward_right_local;
                advanced.arming_axis_right_local = settle_axis;
                advanced.arming_elapsed_milliseconds = 0;
                advanced.settling_elapsed_milliseconds = 0;
            } else if (step_speed_degrees_per_second >
                       kRightRayTwoHandSettleSpeedDegreesPerSecond) {
                // Keep the settle anchor fixed while meaningful motion is in
                // progress.  That lets an ordinary resumed sweep accumulate
                // toward the spatial intent gate instead of dragging the
                // anchor behind it, while sub-threshold tracker shimmer still
                // cannot reopen steering.
                advanced.settling_elapsed_milliseconds = 0;
            } else {
                // Slow flex is not deliberate steering.  Rebase it as the new
                // settling center and require a fresh quiet dwell.
                advanced.settling_input_anchor_right_local =
                    current_support_right_local;
                advanced.settling_elapsed_milliseconds = 0;
            }
        }
        advanced.previous_support_direction_right_local =
            current_support_right_local;
    }

    ControllerWeaponPose calculated{};
    if (!build_pose(
            advanced.accepted_forward_right_local, &calculated)) {
        return false;
    }

    advanced.generation = generation;
    advanced.publication_milliseconds = publication_milliseconds;
    advanced.tracking_anchor_orientation = current_tracking_anchor;
    advanced.cached_pose = calculated;
    *state = advanced;
    *pose = calculated;
    return true;
}

void reset_right_ray_two_hand_steering(
    RightRayTwoHandSteeringState* const state) noexcept {
    if (state != nullptr) {
        *state = {};
    }
}

bool calibrate_controller_weapon_attachment(
    const wawvr::xr::Vec3f& camera_origin,
    const wawvr::xr::Basis3f& camera_axis,
    const ControllerWeaponPose& controller,
    const wawvr::xr::Vec3f& weapon_origin,
    const wawvr::xr::Basis3f& weapon_axis,
    WeaponAttachmentState* const attachment) noexcept {
    if (attachment == nullptr || !finite_vector(camera_origin) ||
        !valid_basis(camera_axis) ||
        !finite_vector(controller.grip_position) ||
        !valid_basis(controller.aim_axis) ||
        !finite_vector(weapon_origin) || !valid_basis(weapon_axis)) {
        return false;
    }

    WeaponAttachmentState calculated{};
    const wawvr::xr::Vec3f origin_camera_local = project_rows(
        camera_axis, subtract(weapon_origin, camera_origin));
    calculated.position = project_rows(
        controller.aim_axis,
        subtract(origin_camera_local, controller.grip_position));
    for (std::size_t weapon_row = 0; weapon_row < 3; ++weapon_row) {
        const wawvr::xr::Vec3f weapon_row_camera_local = project_rows(
            camera_axis, row(weapon_axis, weapon_row));
        set_row(
            &calculated.axis, weapon_row,
            project_rows(controller.aim_axis, weapon_row_camera_local));
    }
    if (!finite_vector(calculated.position) ||
        !valid_basis(calculated.axis)) {
        return false;
    }
    calculated.valid = true;
    *attachment = calculated;
    return true;
}

bool transfer_controller_weapon_attachment(
    const wawvr::xr::Vec3f& camera_origin,
    const wawvr::xr::Basis3f& camera_axis,
    const ControllerWeaponPose& outgoing_controller,
    const WeaponAttachmentState& outgoing_attachment,
    const ControllerWeaponPose& incoming_controller,
    WeaponAttachmentState* const incoming_attachment,
    wawvr::xr::Vec3f* const weapon_origin,
    wawvr::xr::Basis3f* const weapon_axis) noexcept {
    if (incoming_attachment == nullptr || weapon_origin == nullptr ||
        weapon_axis == nullptr || !outgoing_attachment.valid) {
        return false;
    }

    // Work entirely in temporaries. In particular, apply_controller_weapon_
    // placement must receive a valid copy so it cannot capture a replacement
    // transform from the seed pose when the outgoing state is malformed.
    WeaponAttachmentState outgoing_copy = outgoing_attachment;
    wawvr::xr::Vec3f transferred_origin = camera_origin;
    wawvr::xr::Basis3f transferred_axis = camera_axis;
    if (!apply_controller_weapon_placement(
            camera_origin, camera_axis, outgoing_controller, &outgoing_copy,
            &transferred_origin, &transferred_axis)) {
        return false;
    }

    WeaponAttachmentState transferred_attachment{};
    if (!calibrate_controller_weapon_attachment(
            camera_origin, camera_axis, incoming_controller,
            transferred_origin, transferred_axis,
            &transferred_attachment)) {
        return false;
    }

    // Round-trip the new owner before committing. This guards the continuity
    // contract against invalid bases and future changes to either primitive.
    WeaponAttachmentState verification_attachment = transferred_attachment;
    wawvr::xr::Vec3f verified_origin = transferred_origin;
    wawvr::xr::Basis3f verified_axis = transferred_axis;
    if (!apply_controller_weapon_placement(
            camera_origin, camera_axis, incoming_controller,
            &verification_attachment, &verified_origin, &verified_axis)) {
        return false;
    }

    *incoming_attachment = transferred_attachment;
    *weapon_origin = verified_origin;
    *weapon_axis = verified_axis;
    return true;
}

bool align_controller_weapon_attachment_to_controller_forward(
    const wawvr::xr::Vec3f& camera_origin,
    const wawvr::xr::Basis3f& camera_axis,
    const ControllerWeaponPose& controller,
    const wawvr::xr::Vec3f& weapon_origin,
    WeaponAttachmentState* const attachment) noexcept {
    if (attachment == nullptr || !finite_vector(camera_origin) ||
        !valid_basis(camera_axis) ||
        !finite_vector(controller.grip_position) ||
        !valid_basis(controller.aim_axis) ||
        !finite_vector(weapon_origin)) {
        return false;
    }

    WeaponAttachmentState aligned = *attachment;
    if (aligned.valid) {
        if (!finite_vector(aligned.position) || !valid_basis(aligned.axis)) {
            return false;
        }
    } else {
        const wawvr::xr::Vec3f origin_camera_local = project_rows(
            camera_axis, subtract(weapon_origin, camera_origin));
        aligned.position = project_rows(
            controller.aim_axis,
            subtract(origin_camera_local, controller.grip_position));
        if (!finite_vector(aligned.position)) {
            return false;
        }
    }

    aligned.valid = true;
    aligned.axis = {};
    *attachment = aligned;
    return true;
}

bool prepare_controller_forward_weapon_pickup(
    const wawvr::xr::Basis3f& camera_axis,
    WeaponAttachmentState* const attachment,
    wawvr::xr::Basis3f* const weapon_axis) noexcept {
    if (attachment == nullptr || weapon_axis == nullptr ||
        !valid_basis(camera_axis)) {
        return false;
    }
    *attachment = {};
    *weapon_axis = camera_axis;
    return true;
}

bool apply_controller_weapon_placement(
    const wawvr::xr::Vec3f& camera_origin,
    const wawvr::xr::Basis3f& camera_axis,
    const ControllerWeaponPose& controller,
    WeaponAttachmentState* const attachment,
    wawvr::xr::Vec3f* const weapon_origin,
    wawvr::xr::Basis3f* const weapon_axis) noexcept {
    if (attachment == nullptr || weapon_origin == nullptr ||
        weapon_axis == nullptr || !finite_vector(camera_origin) ||
        !valid_basis(camera_axis) ||
        !finite_vector(controller.grip_position) ||
        !valid_basis(controller.aim_axis) ||
        !finite_vector(*weapon_origin) || !valid_basis(*weapon_axis)) {
        return false;
    }

    if (!attachment->valid) {
        const wawvr::xr::Vec3f origin_camera_local = project_rows(
            camera_axis, subtract(*weapon_origin, camera_origin));
        // The attachment is consumed in the controller aim frame below, so
        // capture the root delta in that same frame. Keeping this delta in
        // camera-local coordinates makes a rotated controller move the rifle
        // on the very first pickup.
        attachment->position = project_rows(
            controller.aim_axis,
            subtract(origin_camera_local, controller.grip_position));

        for (std::size_t weapon_row = 0; weapon_row < 3; ++weapon_row) {
            set_row(
                &attachment->axis, weapon_row,
                project_rows(camera_axis, row(*weapon_axis, weapon_row)));
        }
        attachment->valid = true;
    }

    if (!finite_vector(attachment->position) ||
        !valid_basis(attachment->axis)) {
        return false;
    }

    const wawvr::xr::Vec3f origin_camera_local = add(
        controller.grip_position,
        compose(controller.aim_axis, attachment->position));
    *weapon_origin = add(
        camera_origin, compose(camera_axis, origin_camera_local));

    wawvr::xr::Basis3f final_axis{};
    for (std::size_t weapon_row = 0; weapon_row < 3; ++weapon_row) {
        const wawvr::xr::Vec3f attachment_row =
            row(attachment->axis, weapon_row);
        const wawvr::xr::Vec3f weapon_row_camera_local =
            compose(controller.aim_axis, attachment_row);
        set_row(
            &final_axis, weapon_row,
            compose(camera_axis, weapon_row_camera_local));
    }

    if (!finite_vector(*weapon_origin) || !valid_basis(final_axis)) {
        return false;
    }
    *weapon_axis = final_axis;
    return true;
}

bool apply_right_controller_weapon_placement(
    const wawvr::xr::Vec3f& camera_origin,
    const wawvr::xr::Basis3f& camera_axis,
    const RightControllerWeaponPose& controller,
    WeaponAttachmentState* const attachment,
    wawvr::xr::Vec3f* const weapon_origin,
    wawvr::xr::Basis3f* const weapon_axis) noexcept {
    return apply_controller_weapon_placement(
        camera_origin, camera_axis, controller, attachment,
        weapon_origin, weapon_axis);
}

bool controller_grip_world(
    const wawvr::xr::Vec3f& camera_origin,
    const wawvr::xr::Basis3f& camera_axis,
    const ControllerWeaponPose& controller,
    wawvr::xr::Vec3f* const grip_world) noexcept {
    if (grip_world == nullptr || !finite_vector(camera_origin) ||
        !valid_basis(camera_axis) ||
        !finite_vector(controller.grip_position)) {
        return false;
    }
    const wawvr::xr::Vec3f result = add(
        camera_origin, compose(camera_axis, controller.grip_position));
    if (!finite_vector(result)) {
        return false;
    }
    *grip_world = result;
    return true;
}

bool right_controller_grip_world(
    const wawvr::xr::Vec3f& camera_origin,
    const wawvr::xr::Basis3f& camera_axis,
    const RightControllerWeaponPose& controller,
    wawvr::xr::Vec3f* const grip_world) noexcept {
    return controller_grip_world(
        camera_origin, camera_axis, controller, grip_world);
}

bool align_viewmodel_origin_to_grip(
    const wawvr::xr::Vec3f& tracked_grip_world,
    const wawvr::xr::Vec3f& viewmodel_grip_tag_world,
    wawvr::xr::Vec3f* const viewmodel_origin) noexcept {
    if (viewmodel_origin == nullptr ||
        !finite_vector(tracked_grip_world) ||
        !finite_vector(viewmodel_grip_tag_world) ||
        !finite_vector(*viewmodel_origin)) {
        return false;
    }

    const wawvr::xr::Vec3f correction = subtract(
        tracked_grip_world, viewmodel_grip_tag_world);
    constexpr float kMaximumCorrectionIwUnits = 512.0F;
    const float length_squared = dot(correction, correction);
    if (!finite(length_squared) ||
        length_squared >
            kMaximumCorrectionIwUnits * kMaximumCorrectionIwUnits) {
        return false;
    }

    const wawvr::xr::Vec3f corrected = add(*viewmodel_origin, correction);
    if (!finite_vector(corrected)) {
        return false;
    }
    *viewmodel_origin = corrected;
    return true;
}

bool translate_evaluated_viewmodel_skeleton(
    const wawvr::xr::Vec3f& original_viewmodel_origin,
    const wawvr::xr::Vec3f& corrected_viewmodel_origin,
    const std::array<std::uint32_t, kViewmodelSkeletonBitWordCount>&
        evaluated_bones,
    const std::span<EvaluatedViewmodelBoneTransform> bone_transforms,
    wawvr::xr::Vec3f* const pose_origin) noexcept {
    if (pose_origin == nullptr || bone_transforms.empty() ||
        bone_transforms.size() > kMaximumViewmodelBoneCount ||
        !finite_vector(original_viewmodel_origin) ||
        !finite_vector(corrected_viewmodel_origin) ||
        !finite_vector(*pose_origin)) {
        return false;
    }

    const wawvr::xr::Vec3f translation = subtract(
        corrected_viewmodel_origin, original_viewmodel_origin);
    constexpr float kMaximumTranslationIwUnits = 512.0F;
    const float length_squared = dot(translation, translation);
    if (!finite(length_squared) ||
        length_squared >
            kMaximumTranslationIwUnits * kMaximumTranslationIwUnits) {
        return false;
    }

    const wawvr::xr::Vec3f translated_pose = add(*pose_origin, translation);
    if (!finite_vector(translated_pose)) {
        return false;
    }

    std::size_t evaluated_count = 0;
    for (std::size_t bone = 0; bone < kMaximumViewmodelBoneCount; ++bone) {
        const std::uint32_t mask =
            0x80000000U >> (bone & 31U);
        if ((evaluated_bones[bone >> 5U] & mask) == 0) {
            continue;
        }
        if (bone >= bone_transforms.size()) {
            return false;
        }
        const auto translated_bone = add(
            bone_transforms[bone].translation, translation);
        if (!finite_vector(bone_transforms[bone].translation) ||
            !finite_vector(translated_bone)) {
            return false;
        }
        ++evaluated_count;
    }
    if (evaluated_count == 0) {
        return false;
    }

    *pose_origin = translated_pose;
    for (std::size_t bone = 0; bone < bone_transforms.size(); ++bone) {
        const std::uint32_t mask =
            0x80000000U >> (bone & 31U);
        if ((evaluated_bones[bone >> 5U] & mask) != 0) {
            bone_transforms[bone].translation = add(
                bone_transforms[bone].translation, translation);
        }
    }
    return true;
}

bool translated_viewmodel_tag_matches(
    const wawvr::xr::Vec3f& expected,
    const wawvr::xr::Vec3f& observed) noexcept {
    if (!finite_vector(expected) || !finite_vector(observed)) {
        return false;
    }

    const auto component_matches = [](const float expected_component,
                                      const float observed_component) noexcept {
        const float magnitude = (std::max)(
            1.0F,
            (std::max)(std::abs(expected_component),
                       std::abs(observed_component)));
        const float next = std::nextafter(
            magnitude, std::numeric_limits<float>::infinity());
        const float ulp = next - magnitude;
        const float tolerance = (std::max)(1.0e-4F, ulp * 2.0F);
        return finite(ulp) && finite(tolerance) &&
            std::abs(observed_component - expected_component) <= tolerance;
    };

    return component_matches(expected.x, observed.x) &&
           component_matches(expected.y, observed.y) &&
           component_matches(expected.z, observed.z);
}

bool iw_axis_to_unit_quaternion(
    const wawvr::xr::Basis3f& axis,
    wawvr::xr::Quaternionf* const quaternion) noexcept {
    if (quaternion == nullptr || !valid_basis(axis)) {
        return false;
    }

    const std::array<std::array<float, 3>, 3> matrix{{
        {{axis.forward.x, axis.forward.y, axis.forward.z}},
        {{axis.left.x, axis.left.y, axis.left.z}},
        {{axis.up.x, axis.up.y, axis.up.z}},
    }};
    std::array<std::array<float, 4>, 4> test{};
    test[0] = {
        matrix[1][2] - matrix[2][1],
        matrix[2][0] - matrix[0][2],
        matrix[0][1] - matrix[1][0],
        matrix[0][0] + matrix[1][1] + matrix[2][2] + 1.0F,
    };
    test[1] = {
        matrix[2][0] + matrix[0][2],
        matrix[2][1] + matrix[1][2],
        matrix[2][2] - matrix[1][1] - matrix[0][0] + 1.0F,
        test[0][2],
    };
    test[2] = {
        matrix[0][0] - matrix[1][1] - matrix[2][2] + 1.0F,
        matrix[1][0] + matrix[0][1],
        test[1][0],
        test[0][0],
    };
    test[3] = {
        test[2][1],
        matrix[1][1] - matrix[0][0] - matrix[2][2] + 1.0F,
        test[1][1],
        test[0][1],
    };

    std::size_t best = 0;
    float best_length_squared = 0.0F;
    for (std::size_t index = 0; index < test.size(); ++index) {
        float length_squared = 0.0F;
        for (const float value : test[index]) {
            length_squared += value * value;
        }
        if (length_squared > best_length_squared) {
            best = index;
            best_length_squared = length_squared;
        }
    }
    if (!finite(best_length_squared) || best_length_squared < 1.0F) {
        return false;
    }

    const float inverse_length = 1.0F / std::sqrt(best_length_squared);
    const auto& value = test[best];
    wawvr::xr::Quaternionf result{
        value[0] * inverse_length,
        value[1] * inverse_length,
        value[2] * inverse_length,
        value[3] * inverse_length,
    };
    if (!valid_orientation(result)) {
        return false;
    }
    *quaternion = result;
    return true;
}

}  // namespace wawvr::mod
