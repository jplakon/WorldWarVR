#pragma once

#include "xr_types.h"

namespace wawvr::mod {

// VR owns pitch and roll through the tracked HMD.  T4's completed stock
// refdef still contains useful body yaw, but can also contain weapon/melee
// animation, recoil, damage kick, or a briefly vertical spawn basis.  Reduce
// that basis to a stable, gravity-level IW forward/left/up frame before it is
// composed with tracked head/controller poses.
[[nodiscard]] bool gravity_level_t4_camera_axis(
    const wawvr::xr::Basis3f& stock,
    wawvr::xr::Basis3f* leveled) noexcept;

}  // namespace wawvr::mod
