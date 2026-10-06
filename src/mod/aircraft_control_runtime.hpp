// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "t4/bindings.hpp"
#include "t4/usercmd.hpp"
#include "xr_types.h"

namespace wawvr::mod {

void bind_aircraft_controls(const wawvr::t4::ValidatedBindings* bindings) noexcept;
[[nodiscard]] bool controller_aircraft_controls_active() noexcept;
// True means this native passenger station owns the command, even when XR
// input is unavailable. Never fall through to handheld/snap-turn controls.
[[nodiscard]] bool apply_aircraft_controller_command(
    wawvr::t4::UsercmdSp& command, bool gameplay_allowed) noexcept;
// Called only after the native camera has run, using its unmodified origin.
// The first capture at a new station seeds the camera before any aim write.
[[nodiscard]] bool read_aircraft_camera_base(
    const wawvr::xr::Vec3f& stock_origin,
    wawvr::xr::Vec3f* origin, wawvr::xr::Basis3f* axis) noexcept;

} // namespace wawvr::mod
