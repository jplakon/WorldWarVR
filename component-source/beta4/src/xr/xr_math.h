// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "xr_types.h"

namespace wawvr::xr
{

constexpr float kIwUnitsPerMeter = 39.37007874015748f;

Quaternionf Normalize(const Quaternionf& value);
Quaternionf Conjugate(const Quaternionf& value);
Quaternionf Multiply(const Quaternionf& left, const Quaternionf& right);
Vec3f Rotate(const Quaternionf& orientation, const Vec3f& vector);

// OpenXR is +X right, +Y up, -Z forward. IW3/T4 camera-local space is
// +X forward, +Y left, +Z up.
Vec3f OpenXrVectorToIw(const Vec3f& vector);

// Converts a tracked OpenXR pose into a host-friendly IW basis relative to a
// reference pose (normally the current HMD center). Position is scaled into
// engine units; orientation is returned as forward/left/up rows.
EnginePose OpenXrPoseToIwRelative(
    const Posef& pose,
    const Posef& reference,
    float engine_units_per_meter = kIwUnitsPerMeter);

} // namespace wawvr::xr
