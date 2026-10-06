// SPDX-License-Identifier: GPL-3.0-only
#include "pistol_support_pose.hpp"

#include <array>
#include <cmath>

namespace wawvr::mod {
namespace {

using wawvr::xr::Basis3f;
using wawvr::xr::Vec3f;

[[nodiscard]] bool finite_vector(const Vec3f& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] float dot(const Vec3f& a, const Vec3f& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] Vec3f cross(const Vec3f& a, const Vec3f& b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

[[nodiscard]] bool valid_basis(const Basis3f& value) noexcept {
    constexpr float tolerance = 0.02F;
    return finite_vector(value.forward) && finite_vector(value.left) &&
           finite_vector(value.up) &&
           std::fabs(dot(value.forward, value.forward) - 1.0F) <= tolerance &&
           std::fabs(dot(value.left, value.left) - 1.0F) <= tolerance &&
           std::fabs(dot(value.up, value.up) - 1.0F) <= tolerance &&
           std::fabs(dot(value.forward, value.left)) <= tolerance &&
           std::fabs(dot(value.forward, value.up)) <= tolerance &&
           std::fabs(dot(value.left, value.up)) <= tolerance &&
           dot(cross(value.forward, value.left), value.up) > 0.98F;
}

[[nodiscard]] Vec3f compose(
    const Basis3f& basis, const Vec3f& local) noexcept {
    return {
        basis.forward.x * local.x + basis.left.x * local.y + basis.up.x * local.z,
        basis.forward.y * local.x + basis.left.y * local.y + basis.up.y * local.z,
        basis.forward.z * local.x + basis.left.z * local.y + basis.up.z * local.z,
    };
}

[[nodiscard]] Basis3f compose_axes(
    const Basis3f& outer, const Basis3f& inner) noexcept {
    return {compose(outer, inner.forward), compose(outer, inner.left),
            compose(outer, inner.up)};
}

[[nodiscard]] Basis3f transpose(const Basis3f& value) noexcept {
    return {
        {value.forward.x, value.left.x, value.up.x},
        {value.forward.y, value.left.y, value.up.y},
        {value.forward.z, value.left.z, value.up.z},
    };
}

}  // namespace

bool pistol_support_pose_for_weapon_name(
    const std::string_view internal_weapon_name) noexcept {
    // Magazine-pistol identities match the audited manual-reload profiles.
    // WAW-GSC maps/_debug.gsc classifies sw_357 as genericPistol; the zombie
    // maps list its two exact variants. maps/_laststand.gsc identifies
    // colt_dirty_harry as a last-stand pistol. This is not a prefix match.
    constexpr std::array<std::string_view, 11> identities{
        "colt", "colt_wet", "walther", "tokarev", "nambu",
        "zombie_colt", "zombie_colt_upgraded", "sw_357", "zombie_sw_357",
        "zombie_sw_357_upgraded", "colt_dirty_harry",
    };
    for (const std::string_view identity : identities) {
        if (internal_weapon_name == identity) {
            return true;
        }
    }
    return false;
}

bool pistol_support_pose_for_weapon_name_buffer(
    const std::string_view terminated_name_buffer) noexcept {
    const std::size_t terminator = terminated_name_buffer.find('\0');
    return terminator != std::string_view::npos &&
           pistol_support_pose_for_weapon_name(
               terminated_name_buffer.substr(0, terminator));
}

bool calculate_pistol_support_hand_pose(
    const wawvr::xr::EnginePose& right_wrist,
    const Basis3f& right_attachment_axis,
    const Vec3f& right_attachment_position,
    const Basis3f& left_attachment_axis,
    const Vec3f& left_attachment_position,
    const Basis3f& weapon_axis,
    wawvr::xr::EnginePose* const output) noexcept {
    if (output == nullptr || !finite_vector(right_wrist.position) ||
        !valid_basis(right_wrist.axis) || !valid_basis(right_attachment_axis) ||
        !finite_vector(right_attachment_position) ||
        !valid_basis(left_attachment_axis) ||
        !finite_vector(left_attachment_position) || !valid_basis(weapon_axis)) {
        return false;
    }

    const Basis3f anatomical_grip = compose_axes(
        right_wrist.axis, transpose(right_attachment_axis));
    if (!valid_basis(anatomical_grip)) {
        return false;
    }
    const Vec3f right_wrist_from_palm = compose(
        anatomical_grip, right_attachment_position);
    const Vec3f support_offset = compose(
        weapon_axis, kPistolSupportPalmOffsetWeaponLocal);
    const Vec3f left_wrist_from_palm = compose(
        anatomical_grip, left_attachment_position);
    const wawvr::xr::EnginePose candidate{
        .position = {
            right_wrist.position.x - right_wrist_from_palm.x +
                support_offset.x + left_wrist_from_palm.x,
            right_wrist.position.y - right_wrist_from_palm.y +
                support_offset.y + left_wrist_from_palm.y,
            right_wrist.position.z - right_wrist_from_palm.z +
                support_offset.z + left_wrist_from_palm.z,
        },
        .axis = compose_axes(anatomical_grip, left_attachment_axis),
    };
    if (!finite_vector(candidate.position) || !valid_basis(candidate.axis)) {
        return false;
    }
    *output = candidate;
    return true;
}

}  // namespace wawvr::mod
