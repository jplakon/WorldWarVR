// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "xr_types.h"
#include <cmath>

namespace wawvr::mod {
struct ShotLineMetrics final {
    float forward_world_units{};
    float left_world_units{};
    float up_world_units{};
    float angular_error_degrees{};
};

// Read-only evidence geometry, not a sight calibration or ballistic override.
// The caller must establish shot/impact provenance before comparing a native
// impact to a rendered sight ray. Atomic failure preserves the output.
[[nodiscard]] inline bool measure_shot_line(
    const wawvr::xr::Vec3f& origin, const wawvr::xr::Basis3f& basis,
    const wawvr::xr::Vec3f& impact, ShotLineMetrics* output) noexcept {
    if (!output) return false;
    const auto finite = [](const wawvr::xr::Vec3f& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    const auto dot = [](const wawvr::xr::Vec3f& a, const wawvr::xr::Vec3f& b) {
        return a.x*b.x + a.y*b.y + a.z*b.z;
    };
    if (!finite(origin) || !finite(impact) || !finite(basis.forward) ||
        !finite(basis.left) || !finite(basis.up)) return false;
    if (std::abs(dot(basis.forward,basis.forward)-1.F) > .002F ||
        std::abs(dot(basis.left,basis.left)-1.F) > .002F ||
        std::abs(dot(basis.up,basis.up)-1.F) > .002F ||
        std::abs(dot(basis.forward,basis.left)) > .002F ||
        std::abs(dot(basis.forward,basis.up)) > .002F ||
        std::abs(dot(basis.left,basis.up)) > .002F) return false;
    const wawvr::xr::Vec3f delta{impact.x-origin.x,impact.y-origin.y,impact.z-origin.z};
    const float forward = dot(delta,basis.forward);
    const float left = dot(delta,basis.left);
    const float up = dot(delta,basis.up);
    if (!(forward > .001F) || !std::isfinite(forward) ||
        !std::isfinite(left) || !std::isfinite(up)) return false;
    const float error = std::atan2(std::hypot(left,up),forward)*57.295779513F;
    if (!std::isfinite(error)) return false;
    *output = {forward,left,up,error};
    return true;
}
} // namespace wawvr::mod
