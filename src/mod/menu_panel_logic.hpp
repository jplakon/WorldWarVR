// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "comfort_panel_constants.hpp"
#include "xr_types.h"

namespace wawvr::mod {

struct MenuPointerHit final {
    bool valid{};
    // Normalized texture/UI coordinates: (0,0) top-left, (1,1) bottom-right.
    float u{};
    float v{};
    float distance_meters{};
};

// Places a finite quad straight ahead of the latched, gravity-level entry
// pose. The pose remains in LOCAL space until comfort mode exits/recenters.
[[nodiscard]] bool build_world_menu_panel(
    const wawvr::xr::Posef& leveled_entry_pose,
    wawvr::xr::QuadLayer* panel) noexcept;

// Intersects an OpenXR aim pose (-Z forward) against the visible front of the
// OpenXR quad (+Z normal). A miss never produces clamped edge coordinates.
[[nodiscard]] MenuPointerHit point_at_world_menu_panel(
    const wawvr::xr::TrackedPose& aim,
    const wawvr::xr::QuadLayer& panel) noexcept;

// The quad texture can contain compositor-created bars around a 4:3 or
// cropped source. Reject those bars and convert full-quad UV to the exact
// source-content UV consumed by T4's native UI hit-testing coordinates.
[[nodiscard]] MenuPointerHit remap_menu_pointer_to_content(
    const MenuPointerHit& panel_hit,
    const wawvr::xr::NormalizedViewport& content_viewport) noexcept;

}  // namespace wawvr::mod
