// SPDX-License-Identifier: GPL-3.0-only
#include "menu_panel_logic.hpp"

#include "xr_math.h"

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

[[nodiscard]] bool valid_orientation(
    const wawvr::xr::Quaternionf& value) noexcept {
    if (!finite(value.x) || !finite(value.y) || !finite(value.z) ||
        !finite(value.w)) {
        return false;
    }
    const float length_squared = value.x * value.x + value.y * value.y +
                                 value.z * value.z + value.w * value.w;
    return length_squared > 1.0e-8F && finite(length_squared);
}

[[nodiscard]] float dot(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] wawvr::xr::Vec3f subtract(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

}  // namespace

bool build_world_menu_panel(
    const wawvr::xr::Posef& leveled_entry_pose,
    wawvr::xr::QuadLayer* const panel) noexcept {
    if (panel == nullptr || !finite_vector(leveled_entry_pose.position) ||
        !valid_orientation(leveled_entry_pose.orientation)) {
        return false;
    }

    const wawvr::xr::Quaternionf orientation =
        wawvr::xr::Normalize(leveled_entry_pose.orientation);
    const wawvr::xr::Vec3f forward = wawvr::xr::Rotate(
        orientation, {0.0F, 0.0F, -1.0F});
    const float half_width = kMenuPanelDistanceMeters *
                             std::tan(kMenuPanelHalfHorizontalAngleRadians);
    const float half_height = kMenuPanelDistanceMeters *
                              std::tan(kMenuPanelHalfVerticalAngleRadians);
    if (!finite_vector(forward) || !finite(half_width) ||
        !finite(half_height) || half_width <= 0.0F || half_height <= 0.0F) {
        return false;
    }

    wawvr::xr::QuadLayer result{};
    result.pose.orientation = orientation;
    result.pose.position = {
        leveled_entry_pose.position.x +
            forward.x * kMenuPanelDistanceMeters,
        leveled_entry_pose.position.y +
            forward.y * kMenuPanelDistanceMeters,
        leveled_entry_pose.position.z +
            forward.z * kMenuPanelDistanceMeters,
    };
    result.size_meters = {2.0F * half_width, 2.0F * half_height};
    result.source_eye = 0;
    if (!finite_vector(result.pose.position)) {
        return false;
    }
    *panel = result;
    return true;
}

MenuPointerHit point_at_world_menu_panel(
    const wawvr::xr::TrackedPose& aim,
    const wawvr::xr::QuadLayer& panel) noexcept {
    MenuPointerHit hit{};
    if (!aim.active || !aim.orientation_valid || !aim.position_valid ||
        !finite_vector(aim.pose.position) ||
        !valid_orientation(aim.pose.orientation) ||
        !finite_vector(panel.pose.position) ||
        !valid_orientation(panel.pose.orientation) ||
        !finite(panel.size_meters.x) || !finite(panel.size_meters.y) ||
        panel.size_meters.x <= 0.0F || panel.size_meters.y <= 0.0F) {
        return hit;
    }

    const wawvr::xr::Quaternionf aim_orientation =
        wawvr::xr::Normalize(aim.pose.orientation);
    const wawvr::xr::Quaternionf panel_orientation =
        wawvr::xr::Normalize(panel.pose.orientation);
    const wawvr::xr::Vec3f direction = wawvr::xr::Rotate(
        aim_orientation, {0.0F, 0.0F, -1.0F});
    const wawvr::xr::Vec3f right = wawvr::xr::Rotate(
        panel_orientation, {1.0F, 0.0F, 0.0F});
    const wawvr::xr::Vec3f up = wawvr::xr::Rotate(
        panel_orientation, {0.0F, 1.0F, 0.0F});
    // Core OpenXR quads expose only their local +Z front face.
    const wawvr::xr::Vec3f front_normal = wawvr::xr::Rotate(
        panel_orientation, {0.0F, 0.0F, 1.0F});
    if (!finite_vector(direction) || !finite_vector(right) ||
        !finite_vector(up) || !finite_vector(front_normal)) {
        return hit;
    }

    const float denominator = dot(direction, front_normal);
    // A controller in front of the panel must point against its +Z normal.
    if (!finite(denominator) || denominator >= -1.0e-5F) {
        return hit;
    }
    const wawvr::xr::Vec3f center_from_origin = subtract(
        panel.pose.position, aim.pose.position);
    const float distance = dot(center_from_origin, front_normal) / denominator;
    if (!finite(distance) || distance <= 0.0F) {
        return hit;
    }

    const wawvr::xr::Vec3f intersection = {
        aim.pose.position.x + direction.x * distance,
        aim.pose.position.y + direction.y * distance,
        aim.pose.position.z + direction.z * distance,
    };
    const wawvr::xr::Vec3f local = subtract(
        intersection, panel.pose.position);
    const float local_x = dot(local, right);
    const float local_y = dot(local, up);
    const float half_width = 0.5F * panel.size_meters.x;
    const float half_height = 0.5F * panel.size_meters.y;
    if (!finite(local_x) || !finite(local_y) ||
        local_x < -half_width || local_x > half_width ||
        local_y < -half_height || local_y > half_height) {
        return hit;
    }

    hit.valid = true;
    hit.u = std::clamp(local_x / panel.size_meters.x + 0.5F, 0.0F, 1.0F);
    hit.v = std::clamp(0.5F - local_y / panel.size_meters.y, 0.0F, 1.0F);
    hit.distance_meters = distance;
    return hit;
}

MenuPointerHit remap_menu_pointer_to_content(
    const MenuPointerHit& panel_hit,
    const wawvr::xr::NormalizedViewport& content_viewport) noexcept {
    MenuPointerHit hit{};
    if (!panel_hit.valid || !finite(panel_hit.u) || !finite(panel_hit.v) ||
        !finite(panel_hit.distance_meters) ||
        !finite(content_viewport.x) || !finite(content_viewport.y) ||
        !finite(content_viewport.width) ||
        !finite(content_viewport.height) || content_viewport.width <= 0.0F ||
        content_viewport.height <= 0.0F) {
        return hit;
    }
    const float right = content_viewport.x + content_viewport.width;
    const float bottom = content_viewport.y + content_viewport.height;
    if (!finite(right) || !finite(bottom) ||
        panel_hit.u < content_viewport.x || panel_hit.u > right ||
        panel_hit.v < content_viewport.y || panel_hit.v > bottom) {
        return hit;
    }
    hit.valid = true;
    hit.u = std::clamp(
        (panel_hit.u - content_viewport.x) / content_viewport.width,
        0.0F, 1.0F);
    hit.v = std::clamp(
        (panel_hit.v - content_viewport.y) / content_viewport.height,
        0.0F, 1.0F);
    hit.distance_meters = panel_hit.distance_meters;
    return hit;
}

}  // namespace wawvr::mod
