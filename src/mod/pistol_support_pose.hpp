// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "xr_types.h"

#include <string_view>

namespace wawvr::mod {

// Presentation-only pistol identity policy. Unknown or malformed names retain
// the ordinary authored hand pose; this does not classify reload mechanisms.
[[nodiscard]] bool pistol_support_pose_for_weapon_name(
    std::string_view internal_weapon_name) noexcept;

// The native snapshot reader copies a bounded memory chunk, which can include
// unrelated nonzero bytes after the first terminator. Locate that terminator
// within the supplied buffer instead of assuming the last byte is NUL.
[[nodiscard]] bool pistol_support_pose_for_weapon_name_buffer(
    std::string_view terminated_name_buffer) noexcept;

// IW units are inches. Position the support palm beside the firing palm, not
// along a rifle fore-end. This visual calibration never alters controller aim.
inline constexpr wawvr::xr::Vec3f kPistolSupportPalmOffsetWeaponLocal{
    0.5F, 1.0F, -0.25F};

// Both extracted gloves have their own mirrored wrist-local attachment. Recover
// the common anatomical grip frame from the authored right wrist, then apply
// the left attachment to a nearby palm anchor in that same frame. The output is
// committed only after all inputs and the complete resulting pose validate.
[[nodiscard]] bool calculate_pistol_support_hand_pose(
    const wawvr::xr::EnginePose& right_wrist,
    const wawvr::xr::Basis3f& right_attachment_axis,
    const wawvr::xr::Vec3f& right_attachment_position,
    const wawvr::xr::Basis3f& left_attachment_axis,
    const wawvr::xr::Vec3f& left_attachment_position,
    const wawvr::xr::Basis3f& weapon_axis,
    wawvr::xr::EnginePose* output) noexcept;

}  // namespace wawvr::mod
