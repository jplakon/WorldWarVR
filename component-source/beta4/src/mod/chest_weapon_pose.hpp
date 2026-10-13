// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "xr_types.h"

namespace wawvr::mod {

// The visible weapon grip, not its asset-dependent model origin, belongs at
// this sternum offset from the tracked head in a gravity-level IW frame.
inline constexpr wawvr::xr::Vec3f kChestGripLocal{4.0F, 0.0F, -17.0F};
inline constexpr wawvr::xr::Basis3f kChestWeaponAxisLocal{
    {0.226455F, 0.566139F, 0.792594F},
    {-0.928477F, 0.371391F, 0.0F},
    {-0.294346F, -0.735865F, 0.609749F},
};

struct ChestWeaponPoseState final {
    bool heading_valid{};
    wawvr::xr::Vec3f heading_body_local{1.0F, 0.0F, 0.0F};
    wawvr::xr::Quaternionf heading_tracking_anchor_orientation{};
};

// Tracks head translation and head-plus-body yaw, while excluding head pitch
// and roll from the chest frame. Near vertical, head-forward cannot identify
// yaw without introducing head roll: retain the last trustworthy heading, or
// use body heading if there is no history. Retained heading is rebased through
// its capture anchor, so body turns and anchor recenters still compose once.
// The result's position is the target visible grip; callers align the weapon
// model origin to it using the model's actual grip tag. Invalid input leaves
// both output and state unchanged. Update the state while the weapon is held
// too, retain it across ordinary grip/weapon changes, and clear it on a new
// XR session/reference-space reset or hook teardown.
[[nodiscard]] bool build_chest_weapon_pose(
    const wawvr::xr::EnginePose& body_world_pose,
    const wawvr::xr::Posef& head,
    const wawvr::xr::Posef& tracking_anchor,
    wawvr::xr::EnginePose* chest_pose,
    ChestWeaponPoseState* heading_state = nullptr) noexcept;

}  // namespace wawvr::mod
