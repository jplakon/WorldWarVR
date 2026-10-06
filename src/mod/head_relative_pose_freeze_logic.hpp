// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "xr_types.h"

namespace wawvr::mod {

// Diagnostic-only pose retained in the head's local coordinate system. The
// helper owns no runtime state and never reads controllers, clocks, or T4.
struct HeadRelativePoseFreeze final {
    bool valid{};
    wawvr::xr::EnginePose head_local_pose{};
};

// Builds the current head pose in T4 world coordinates from the exact body
// camera base, tracking anchor, and predicted OpenXR head sample used by one
// frame. Keeping this composition in one helper prevents the stereo camera and
// diagnostic weapon path from drifting into subtly different transforms.
[[nodiscard]] bool compose_head_world_pose(
    const wawvr::xr::EnginePose& body_world_pose,
    const wawvr::xr::Posef& head_reference_pose,
    const wawvr::xr::Posef& tracking_anchor,
    float engine_units_per_meter,
    wawvr::xr::EnginePose* head_world_pose) noexcept;

// Captures a finite, right-handed orthonormal world pose relative to a finite,
// right-handed orthonormal head-world pose. The output is cleared on failure.
[[nodiscard]] bool capture_head_relative_pose_freeze(
    const wawvr::xr::EnginePose& head_world_pose,
    const wawvr::xr::EnginePose& world_pose,
    HeadRelativePoseFreeze* freeze) noexcept;

// Reconstructs the captured pose under a new finite, right-handed orthonormal
// head-world pose. The output is cleared on failure.
[[nodiscard]] bool reconstruct_head_relative_pose_freeze(
    const HeadRelativePoseFreeze& freeze,
    const wawvr::xr::EnginePose& head_world_pose,
    wawvr::xr::EnginePose* world_pose) noexcept;

}  // namespace wawvr::mod
