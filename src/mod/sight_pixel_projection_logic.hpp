// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sight_audit_logic.hpp"
#include "stereo_scene_math.hpp"

namespace wawvr::mod {
inline constexpr std::uint32_t kSightPixelMaximumObservedFrames=512;
inline constexpr std::size_t kSightPixelAnchorCount=8;
struct SightPixelFrameBudget final { std::uint64_t last_frame{}; std::uint32_t observed{}; };
[[nodiscard]] inline bool admit_sight_pixel_frame(std::uint64_t frame,SightPixelFrameBudget* budget) noexcept {
    if (!budget||!frame||frame<budget->last_frame) return false;
    if (frame==budget->last_frame) return budget->observed>0&&budget->observed<=kSightPixelMaximumObservedFrames;
    budget->last_frame=frame;
    if (budget->observed>=kSightPixelMaximumObservedFrames) {
        budget->observed=kSightPixelMaximumObservedFrames+1; return false;
    }
    ++budget->observed; return true;
}
struct SightPixelProjection final {
    float packed_x{},packed_y{},eye_u{},eye_v{},depth{};
    bool inside_eye{}; // projected viewport membership, NOT an occlusion test
};
[[nodiscard]] bool project_sight_pixel_anchor(const wawvr::xr::Vec3f& world,
    const T4SceneView&,const wawvr::xr::NormalizedViewport& destination,
    std::uint32_t source_width,std::uint32_t source_height,SightPixelProjection*) noexcept;
[[nodiscard]] bool build_sight_pixel_world_anchors(const SightAuditSnapshot&,
    std::array<wawvr::xr::Vec3f,kSightPixelAnchorCount>*) noexcept;
[[nodiscard]] bool same_sight_pixel_source(const SightAuditSnapshot&,const SightAuditSnapshot&) noexcept;
} // namespace wawvr::mod
