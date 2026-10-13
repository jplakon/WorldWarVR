// SPDX-License-Identifier: GPL-3.0-only
#include "weapon_frame_base_phase_logic.hpp"

#include <algorithm>
#include <cmath>

namespace wawvr::mod {
namespace {

[[nodiscard]] bool finite(const float value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool finite(const wawvr::xr::Vec3f& value) noexcept {
    return finite(value.x) && finite(value.y) && finite(value.z);
}

[[nodiscard]] bool finite(const wawvr::xr::Quaternionf& value) noexcept {
    return finite(value.x) && finite(value.y) && finite(value.z) &&
        finite(value.w);
}

[[nodiscard]] bool finite(const wawvr::xr::Posef& value) noexcept {
    return finite(value.position) && finite(value.orientation);
}

[[nodiscard]] bool finite(const wawvr::xr::Basis3f& value) noexcept {
    return finite(value.forward) && finite(value.left) && finite(value.up);
}

[[nodiscard]] float length_squared(const wawvr::xr::Vec3f& value) noexcept {
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

[[nodiscard]] bool normalize(wawvr::xr::Vec3f* const value) noexcept {
    if (value == nullptr || !finite(*value)) {
        return false;
    }
    const float squared = length_squared(*value);
    if (!finite(squared) || squared <= 1.0e-8F) {
        return false;
    }
    const float inverse = 1.0F / std::sqrt(squared);
    value->x *= inverse;
    value->y *= inverse;
    value->z *= inverse;
    return finite(*value);
}

[[nodiscard]] bool valid_basis(const wawvr::xr::Basis3f& value) noexcept {
    if (!finite(value)) {
        return false;
    }
    constexpr float kMinimumLengthSquared = 0.80F;
    constexpr float kMaximumLengthSquared = 1.20F;
    constexpr float kMaximumAxisDot = 0.20F;
    const auto dot = [](const wawvr::xr::Vec3f& left,
                        const wawvr::xr::Vec3f& right) noexcept {
        return left.x * right.x + left.y * right.y + left.z * right.z;
    };
    const float forward_length = length_squared(value.forward);
    const float left_length = length_squared(value.left);
    const float up_length = length_squared(value.up);
    return forward_length >= kMinimumLengthSquared &&
        forward_length <= kMaximumLengthSquared &&
        left_length >= kMinimumLengthSquared &&
        left_length <= kMaximumLengthSquared &&
        up_length >= kMinimumLengthSquared &&
        up_length <= kMaximumLengthSquared &&
        std::abs(dot(value.forward, value.left)) <= kMaximumAxisDot &&
        std::abs(dot(value.forward, value.up)) <= kMaximumAxisDot &&
        std::abs(dot(value.left, value.up)) <= kMaximumAxisDot;
}

[[nodiscard]] bool valid_orientation(
    const wawvr::xr::Quaternionf& value) noexcept {
    if (!finite(value)) {
        return false;
    }
    const float squared = value.x * value.x + value.y * value.y +
        value.z * value.z + value.w * value.w;
    return squared >= 0.90F && squared <= 1.10F;
}

[[nodiscard]] bool same_quaternion_rotation(
    const wawvr::xr::Quaternionf& left,
    const wawvr::xr::Quaternionf& right) noexcept {
    if (!valid_orientation(left) || !valid_orientation(right)) {
        return false;
    }
    const bool exact = left.x == right.x && left.y == right.y &&
        left.z == right.z && left.w == right.w;
    const bool negated = left.x == -right.x && left.y == -right.y &&
        left.z == -right.z && left.w == -right.w;
    return exact || negated;
}

[[nodiscard]] bool same_pose(
    const wawvr::xr::Posef& left,
    const wawvr::xr::Posef& right) noexcept {
    return finite(left.position) && finite(right.position) &&
        left.position.x == right.position.x &&
        left.position.y == right.position.y &&
        left.position.z == right.position.z &&
        same_quaternion_rotation(left.orientation, right.orientation);
}

[[nodiscard]] bool angular_delta_degrees(
    wawvr::xr::Vec3f left,
    wawvr::xr::Vec3f right,
    float* const degrees) noexcept {
    if (degrees == nullptr || !normalize(&left) || !normalize(&right)) {
        return false;
    }
    const float dot = std::clamp(
        left.x * right.x + left.y * right.y + left.z * right.z,
        -1.0F, 1.0F);
    constexpr float kRadiansToDegrees =
        180.0F / 3.14159265358979323846F;
    *degrees = std::acos(dot) * kRadiansToDegrees;
    return finite(*degrees);
}

}  // namespace

bool compare_weapon_frame_base_to_scene(
    const WeaponFrameBaseReceipt& receipt,
    const wawvr::xr::FrameState& frame,
    const wawvr::xr::Posef& tracking_anchor,
    const wawvr::xr::Vec3f& scene_origin,
    const wawvr::xr::Basis3f& scene_body_axis,
    WeaponFrameBasePhaseComparison* const comparison) noexcept {
    if (comparison == nullptr) {
        return false;
    }
    *comparison = {};
    if (!receipt.valid || receipt.controller_generation == 0 ||
        receipt.frame_id == 0 || receipt.frame_id != frame.frame_id ||
        receipt.action_sequence == 0 ||
        receipt.action_sequence != frame.actions.sequence ||
        receipt.predicted_display_time == 0 ||
        receipt.predicted_display_time != frame.predicted_display_time ||
        !same_pose(receipt.tracking_anchor, tracking_anchor) ||
        !same_pose(receipt.head_center, frame.head_center) ||
        !finite(receipt.camera_origin) || !valid_basis(receipt.body_axis) ||
        !finite(scene_origin) || !valid_basis(scene_body_axis)) {
        return false;
    }

    const wawvr::xr::Vec3f origin_delta{
        scene_origin.x - receipt.camera_origin.x,
        scene_origin.y - receipt.camera_origin.y,
        scene_origin.z - receipt.camera_origin.z,
    };
    WeaponFrameBasePhaseComparison measured{};
    measured.valid = true;
    measured.origin_delta_iw_units = std::sqrt(length_squared(origin_delta));
    if (!finite(measured.origin_delta_iw_units) ||
        !angular_delta_degrees(
            receipt.body_axis.forward, scene_body_axis.forward,
            &measured.forward_delta_degrees) ||
        !angular_delta_degrees(
            receipt.body_axis.left, scene_body_axis.left,
            &measured.left_delta_degrees) ||
        !angular_delta_degrees(
            receipt.body_axis.up, scene_body_axis.up,
            &measured.up_delta_degrees)) {
        return false;
    }
    *comparison = measured;
    return true;
}

bool select_weapon_aligned_scene_base(
    const WeaponFrameBaseReceipt& receipt,
    const wawvr::xr::FrameState& frame,
    const wawvr::xr::Posef& tracking_anchor,
    const wawvr::xr::Vec3f& scene_origin,
    const wawvr::xr::Basis3f& scene_body_axis,
    wawvr::xr::Vec3f* const selected_origin,
    wawvr::xr::Basis3f* const selected_axis,
    WeaponFrameBasePhaseComparison* const comparison) noexcept {
    if (selected_origin == nullptr || selected_axis == nullptr) {
        return false;
    }
    *selected_origin = scene_origin;
    *selected_axis = scene_body_axis;
    if (comparison == nullptr) {
        return false;
    }
    if (!compare_weapon_frame_base_to_scene(
            receipt, frame, tracking_anchor, scene_origin, scene_body_axis,
            comparison) || !receipt.scene_base_lock_eligible) {
        return false;
    }
    *selected_origin = receipt.camera_origin;
    *selected_axis = receipt.body_axis;
    return true;
}

}  // namespace wawvr::mod
