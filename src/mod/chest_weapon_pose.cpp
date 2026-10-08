// SPDX-License-Identifier: GPL-3.0-only
#include "chest_weapon_pose.hpp"

#include "camera_comfort_logic.hpp"
#include "head_relative_pose_freeze_logic.hpp"
#include "xr_math.h"

#include <cmath>

namespace wawvr::mod {
namespace {

// Within about 5.7 degrees of vertical, normalizing head-forward magnifies
// tracking noise and the camera helper's left-row fallback leaks head roll
// into chest yaw. Chest placement owns this policy; camera math is unchanged.
constexpr float kTrustworthyHorizontalLengthSquared = 0.01F;

[[nodiscard]] bool finite(const wawvr::xr::Vec3f& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool valid_tracking_pose(
    const wawvr::xr::Posef& pose) noexcept {
    const auto& q = pose.orientation;
    const float squared_length =
        q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    // The shared XR conversion normalizes quaternions, including non-unit
    // samples. Do not let its zero-quaternion fallback invent a valid pose.
    return finite(pose.position) && std::isfinite(q.x) &&
        std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w) &&
        std::isfinite(squared_length) && squared_length > 1.0e-12F;
}

[[nodiscard]] wawvr::xr::Vec3f compose(
    const wawvr::xr::Basis3f& basis,
    const wawvr::xr::Vec3f& local) noexcept {
    return {
        basis.forward.x * local.x + basis.left.x * local.y +
            basis.up.x * local.z,
        basis.forward.y * local.x + basis.left.y * local.y +
            basis.up.y * local.z,
        basis.forward.z * local.x + basis.left.z * local.y +
            basis.up.z * local.z,
    };
}

[[nodiscard]] float dot(const wawvr::xr::Vec3f& a,
                       const wawvr::xr::Vec3f& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] wawvr::xr::Vec3f project(
    const wawvr::xr::Basis3f& basis,
    const wawvr::xr::Vec3f& world) noexcept {
    return {dot(basis.forward, world), dot(basis.left, world),
            dot(basis.up, world)};
}

[[nodiscard]] bool retained_chest_frame(
    const ChestWeaponPoseState& state,
    const wawvr::xr::EnginePose& body,
    const wawvr::xr::Posef& anchor,
    wawvr::xr::Basis3f* const chest_frame) noexcept {
    if (!state.heading_valid || !finite(state.heading_body_local) ||
        !valid_tracking_pose({state.heading_tracking_anchor_orientation, {}})) {
        return false;
    }
    // Inverse of OpenXrVectorToIw: IW (+forward,+left,+up) becomes
    // OpenXR (+right,+up,-forward), then rebase the captured anchor once.
    const auto& local = state.heading_body_local;
    const wawvr::xr::Vec3f reference_heading = wawvr::xr::Rotate(
        state.heading_tracking_anchor_orientation,
        {-local.y, local.z, -local.x});
    const wawvr::xr::Vec3f current_local = wawvr::xr::OpenXrVectorToIw(
        wawvr::xr::Rotate(wawvr::xr::Conjugate(
                            wawvr::xr::Normalize(anchor.orientation)),
                         reference_heading));
    const auto world = compose(body.axis, current_local);
    // There is no rolled left row in this candidate. If an unusual anchor
    // change makes its heading vertical, fail over to the valid body frame.
    return gravity_level_t4_camera_axis(
        {world, {-world.y, world.x, 0.0F}, {0.0F, 0.0F, 1.0F}}, chest_frame);
}

}  // namespace

bool build_chest_weapon_pose(
    const wawvr::xr::EnginePose& body_world_pose,
    const wawvr::xr::Posef& head,
    const wawvr::xr::Posef& tracking_anchor,
    wawvr::xr::EnginePose* const chest_pose,
    ChestWeaponPoseState* const heading_state) noexcept {
    if (chest_pose == nullptr || !valid_tracking_pose(head) ||
        !valid_tracking_pose(tracking_anchor)) {
        return false;
    }

    wawvr::xr::EnginePose head_world{};
    wawvr::xr::Basis3f chest_frame{};
    if (!compose_head_world_pose(body_world_pose, head, tracking_anchor,
                                 wawvr::xr::kIwUnitsPerMeter, &head_world)) {
        return false;
    }

    ChestWeaponPoseState next_state = heading_state != nullptr
        ? *heading_state : ChestWeaponPoseState{};
    const auto& forward = head_world.axis.forward;
    const float horizontal_length_squared =
        forward.x * forward.x + forward.y * forward.y;
    if (horizontal_length_squared >= kTrustworthyHorizontalLengthSquared) {
        if (!gravity_level_t4_camera_axis(head_world.axis, &chest_frame)) {
            return false;
        }
        next_state.heading_valid = true;
        next_state.heading_body_local =
            project(body_world_pose.axis, chest_frame.forward);
        next_state.heading_tracking_anchor_orientation =
            wawvr::xr::Normalize(tracking_anchor.orientation);
    } else if (!retained_chest_frame(next_state, body_world_pose,
                                     tracking_anchor, &chest_frame) &&
               !gravity_level_t4_camera_axis(body_world_pose.axis,
                                             &chest_frame)) {
        return false;
    }

    const auto offset = compose(chest_frame, kChestGripLocal);
    const wawvr::xr::EnginePose result{
        {head_world.position.x + offset.x,
         head_world.position.y + offset.y,
         head_world.position.z + offset.z},
        {compose(chest_frame, kChestWeaponAxisLocal.forward),
         compose(chest_frame, kChestWeaponAxisLocal.left),
         compose(chest_frame, kChestWeaponAxisLocal.up)},
    };
    if (!finite(result.position) || !finite(result.axis.forward) ||
        !finite(result.axis.left) || !finite(result.axis.up)) {
        return false;
    }
    *chest_pose = result;
    if (heading_state != nullptr) {
        *heading_state = next_state;
    }
    return true;
}

}  // namespace wawvr::mod
