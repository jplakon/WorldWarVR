// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "xr_types.h"
#include <array>
#include <cmath>
namespace wawvr::mod {
// Only new nonidentity-bind sight assets call this. Existing accepted assets
// retain their exact original identity-bind floating-point path.
[[nodiscard]] inline bool inverse_sight_bind_vector(
    const wawvr::xr::Vec3f& input,const std::array<float,4>& q,float native_weight,
    wawvr::xr::Vec3f* output) noexcept {
    if (!output||!std::isfinite(input.x)||!std::isfinite(input.y)||!std::isfinite(input.z)||
        !std::isfinite(native_weight)) return false;
    double norm{};
    for (float value:q) { if (!std::isfinite(value)) return false; norm+=double(value)*value; }
    if (std::abs(norm-1.)>.002||norm<=1.e-8||
        std::abs(double(native_weight)-2./norm)>.0001) return false;
    // Conjugate of the exact normalized authored quaternion. Use its actual
    // norm, not a guessed pitch or the identity-bind translation-only shortcut.
    const double x=-q[0],y=-q[1],z=-q[2],w=q[3],scale=2./norm;
    const double cx=y*input.z-z*input.y,cy=z*input.x-x*input.z,cz=x*input.y-y*input.x;
    const wawvr::xr::Vec3f result{
        float(input.x+scale*(w*cx+y*cz-z*cy)),
        float(input.y+scale*(w*cy+z*cx-x*cz)),
        float(input.z+scale*(w*cz+x*cy-y*cx))};
    if (!std::isfinite(result.x)||!std::isfinite(result.y)||!std::isfinite(result.z)) return false;
    *output=result; return true;
}
} // namespace wawvr::mod

