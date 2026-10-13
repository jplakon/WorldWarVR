// SPDX-License-Identifier: GPL-3.0-only
#include "head_relative_pose_freeze_logic.hpp"

#include "xr_math.h"

#include <cmath>

namespace wawvr::mod {
namespace {

constexpr float kOrthonormalTolerance = 1.0e-3F;
constexpr float kHandednessTolerance = 2.0e-3F;

[[nodiscard]] bool finite(const float value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool finite(const wawvr::xr::Vec3f& value) noexcept {
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

[[nodiscard]] float length_squared(
    const wawvr::xr::Vec3f& value) noexcept {
    return dot(value, value);
}

[[nodiscard]] bool orthonormal(
    const wawvr::xr::Basis3f& value) noexcept {
    if (!finite(value.forward) || !finite(value.left) || !finite(value.up)) {
        return false;
    }
    if (std::abs(length_squared(value.forward) - 1.0F) >
            kOrthonormalTolerance ||
        std::abs(length_squared(value.left) - 1.0F) >
            kOrthonormalTolerance ||
        std::abs(length_squared(value.up) - 1.0F) >
            kOrthonormalTolerance ||
        std::abs(dot(value.forward, value.left)) >
            kOrthonormalTolerance ||
        std::abs(dot(value.forward, value.up)) >
            kOrthonormalTolerance ||
        std::abs(dot(value.left, value.up)) >
            kOrthonormalTolerance) {
        return false;
    }

    const wawvr::xr::Vec3f expected_up = cross(value.forward, value.left);
    const wawvr::xr::Vec3f handedness_error{
        expected_up.x - value.up.x,
        expected_up.y - value.up.y,
        expected_up.z - value.up.z,
    };
    return length_squared(handedness_error) <=
        kHandednessTolerance * kHandednessTolerance;
}

[[nodiscard]] bool valid_pose(
    const wawvr::xr::EnginePose& pose) noexcept {
    return finite(pose.position) && orthonormal(pose.axis);
}

[[nodiscard]] wawvr::xr::Vec3f project_into_basis(
    const wawvr::xr::Basis3f& basis,
    const wawvr::xr::Vec3f& world_vector) noexcept {
    return {
        dot(world_vector, basis.forward),
        dot(world_vector, basis.left),
        dot(world_vector, basis.up),
    };
}

[[nodiscard]] wawvr::xr::Vec3f compose_from_basis(
    const wawvr::xr::Basis3f& basis,
    const wawvr::xr::Vec3f& local_vector) noexcept {
    return {
        basis.forward.x * local_vector.x +
            basis.left.x * local_vector.y + basis.up.x * local_vector.z,
        basis.forward.y * local_vector.x +
            basis.left.y * local_vector.y + basis.up.y * local_vector.z,
        basis.forward.z * local_vector.x +
            basis.left.z * local_vector.y + basis.up.z * local_vector.z,
    };
}

[[nodiscard]] wawvr::xr::Basis3f project_into_basis(
    const wawvr::xr::Basis3f& parent,
    const wawvr::xr::Basis3f& world) noexcept {
    return {
        project_into_basis(parent, world.forward),
        project_into_basis(parent, world.left),
        project_into_basis(parent, world.up),
    };
}

[[nodiscard]] wawvr::xr::Basis3f compose_from_basis(
    const wawvr::xr::Basis3f& parent,
    const wawvr::xr::Basis3f& local) noexcept {
    return {
        compose_from_basis(parent, local.forward),
        compose_from_basis(parent, local.left),
        compose_from_basis(parent, local.up),
    };
}

}  // namespace

bool compose_head_world_pose(
    const wawvr::xr::EnginePose& body_world_pose,
    const wawvr::xr::Posef& head_reference_pose,
    const wawvr::xr::Posef& tracking_anchor,
    const float engine_units_per_meter,
    wawvr::xr::EnginePose* const head_world_pose) noexcept {
    if (head_world_pose == nullptr) {
        return false;
    }
    *head_world_pose = {};
    if (!valid_pose(body_world_pose) || !finite(engine_units_per_meter) ||
        engine_units_per_meter <= 0.0F || engine_units_per_meter > 1000.0F) {
        return false;
    }

    const wawvr::xr::EnginePose relative_head =
        wawvr::xr::OpenXrPoseToIwRelative(
            head_reference_pose, tracking_anchor, engine_units_per_meter);
    if (!valid_pose(relative_head)) {
        return false;
    }

    const wawvr::xr::Vec3f world_offset = compose_from_basis(
        body_world_pose.axis, relative_head.position);
    wawvr::xr::EnginePose composed{};
    composed.position = {
        body_world_pose.position.x + world_offset.x,
        body_world_pose.position.y + world_offset.y,
        body_world_pose.position.z + world_offset.z,
    };
    composed.axis = compose_from_basis(
        body_world_pose.axis, relative_head.axis);
    if (!valid_pose(composed)) {
        return false;
    }
    *head_world_pose = composed;
    return true;
}

bool capture_head_relative_pose_freeze(
    const wawvr::xr::EnginePose& head_world_pose,
    const wawvr::xr::EnginePose& world_pose,
    HeadRelativePoseFreeze* const freeze) noexcept {
    if (freeze == nullptr) {
        return false;
    }
    *freeze = {};
    if (!valid_pose(head_world_pose) || !valid_pose(world_pose)) {
        return false;
    }

    const wawvr::xr::Vec3f world_offset{
        world_pose.position.x - head_world_pose.position.x,
        world_pose.position.y - head_world_pose.position.y,
        world_pose.position.z - head_world_pose.position.z,
    };
    HeadRelativePoseFreeze captured{};
    captured.valid = true;
    captured.head_local_pose.position =
        project_into_basis(head_world_pose.axis, world_offset);
    captured.head_local_pose.axis =
        project_into_basis(head_world_pose.axis, world_pose.axis);
    if (!valid_pose(captured.head_local_pose)) {
        return false;
    }
    *freeze = captured;
    return true;
}

bool reconstruct_head_relative_pose_freeze(
    const HeadRelativePoseFreeze& freeze,
    const wawvr::xr::EnginePose& head_world_pose,
    wawvr::xr::EnginePose* const world_pose) noexcept {
    if (world_pose == nullptr) {
        return false;
    }
    *world_pose = {};
    if (!freeze.valid || !valid_pose(freeze.head_local_pose) ||
        !valid_pose(head_world_pose)) {
        return false;
    }

    const wawvr::xr::Vec3f world_offset = compose_from_basis(
        head_world_pose.axis, freeze.head_local_pose.position);
    wawvr::xr::EnginePose reconstructed{};
    reconstructed.position = {
        head_world_pose.position.x + world_offset.x,
        head_world_pose.position.y + world_offset.y,
        head_world_pose.position.z + world_offset.z,
    };
    reconstructed.axis = compose_from_basis(
        head_world_pose.axis, freeze.head_local_pose.axis);
    if (!valid_pose(reconstructed)) {
        return false;
    }
    *world_pose = reconstructed;
    return true;
}

}  // namespace wawvr::mod
