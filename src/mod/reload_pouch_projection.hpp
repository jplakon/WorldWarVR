// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "xr_math.h"

#include <algorithm>
#include <cmath>

namespace wawvr::mod {

struct ReloadPouchProjection final {
    bool valid{};
    wawvr::xr::Vec2f center{};       // Native source-eye UV, before compositor remap.
    wawvr::xr::Vec2f radius{};
};

// Project the real tracking-space grab point independently through each XR
// eye. The marker exists only at its real hip position: looking forward must
// not pin a false pouch to the bottom-left corner of the headset. Looking
// down reveals its actual binocular location, including proper disparity.
[[nodiscard]] inline ReloadPouchProjection project_reload_pouch(
    const wawvr::xr::Vec3f& center,
    const wawvr::xr::EyeView& eye,
    const wawvr::xr::NormalizedViewport& destination = {}) noexcept {
    const auto finite3 = [](const wawvr::xr::Vec3f& p) noexcept {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    };
    const auto& q = eye.pose.orientation;
    const float norm = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    if (!finite3(center) || !finite3(eye.pose.position) ||
        !std::isfinite(norm) || norm < .25F || norm > 4.F) return {};
    if (!std::isfinite(destination.x) || !std::isfinite(destination.y) ||
        !std::isfinite(destination.width) || !std::isfinite(destination.height) ||
        std::abs(destination.x)>8.F || std::abs(destination.y)>8.F ||
        destination.width<=0.F || destination.height<=0.F ||
        destination.width>8.F || destination.height>8.F) return {};
    const auto& f = eye.fov;
    for (const float angle : {f.angle_left,f.angle_right,f.angle_up,f.angle_down})
        if (!std::isfinite(angle) || std::abs(angle) >= 1.55F) return {};
    const float left=std::tan(f.angle_left),right=std::tan(f.angle_right);
    const float up=std::tan(f.angle_up),down=std::tan(f.angle_down);
    if (!(left < 0 && right > 0 && down < 0 && up > 0) ||
        right-left < .05F || up-down < .05F) return {};
    const auto p=wawvr::xr::Rotate(wawvr::xr::Conjugate(wawvr::xr::Normalize(q)),
        {center.x-eye.pose.position.x,center.y-eye.pose.position.y,
         center.z-eye.pose.position.z});
    if (!finite3(p)) return {};
    const float depth=-p.z;
    if (depth <= .01F) return {}; // No misleading arrows to behind the eye.
    const float u=(p.x/depth-left)/(right-left);
    const float v=(up-p.y/depth)/(up-down);
    if (!std::isfinite(u) || !std::isfinite(v)) return {};
    ReloadPouchProjection out{};
    if (u < 0.F || u > 1.F || v < 0.F || v > 1.F) return {};
    // T4 paints a symmetric source image; the compositor applies its
    // asymmetric destination only afterwards. Invert that exact remap here,
    // otherwise the runtime FoV shift is applied twice and may crop one eye.
    const float source_u=(u-destination.x)/destination.width;
    const float source_v=(v-destination.y)/destination.height;
    if (!std::isfinite(source_u) || !std::isfinite(source_v) ||
        source_u<0.F || source_u>1.F || source_v<0.F || source_v>1.F) return {};
    out.valid=true;
    out.center={source_u,source_v};
    // Five-centimetre-wide icon, less than half the former physical size.
    // The small raster floor is applied by the renderer, not a HUD clamp.
    out.radius={std::clamp(.025F/depth/(right-left),.005F,.025F)/destination.width,
                std::clamp(.025F/depth/(up-down),.005F,.025F)/destination.height};
    if (!std::isfinite(out.radius.x) || !std::isfinite(out.radius.y)) return {};
    return out;
}

} // namespace wawvr::mod
