#pragma once

#include "xr_math.h"
#include "xr_types.h"
#include "physical_scope_logic.hpp"

#include <array>
#include <cstdint>

namespace wawvr::mod {

// Only the independently mapped T4 refdef fields needed by the first stereo
// adapter are represented here. Runtime code reads and writes these fields by
// their validated offsets; it never assumes a size for the proprietary
// structure surrounding them.
struct T4SceneView final {
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t width{};
    std::int32_t height{};
    float tan_half_fov_x{};
    float tan_half_fov_y{};
    wawvr::xr::Vec3f origin{};
    wawvr::xr::Basis3f axis{};
    float near_clip{};
};

struct T4StereoSceneViews final {
    std::array<T4SceneView, wawvr::xr::kEyeCount> eyes{};
    T4SceneView scope{};
    bool scope_active{};
    wawvr::xr::StereoSourceLayout compositor_layout{};
    std::uint64_t frame_id{};
};

// Builds two packed, same-simulation T4 views from one completed stock
// refdef. The frozen tracking anchor is normally the first valid HMD centre of
// the current OpenXR session. Each eye is transformed by
// anchor^-1 * currentEye and then composed into the stock IW forward/left/up
// camera basis.
[[nodiscard]] bool build_t4_stereo_scene_views(
    const T4SceneView& stock,
    const wawvr::xr::FrameState& frame,
    const wawvr::xr::Posef& tracking_anchor,
    T4StereoSceneViews* output,
    float engine_units_per_meter = wawvr::xr::kIwUnitsPerMeter,
    const PhysicalScopeSnapshot* physical_scope = nullptr,
    const wawvr::xr::Vec3f* world_aim_target = nullptr) noexcept;

} // namespace wawvr::mod
