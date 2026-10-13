// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "xr_types.h"

#include <cstdint>

namespace wawvr::mod {

// Post-T4 receipt tying the weapon's world-space placement base to one exact
// OpenXR prediction. The scene hook uses it only as a bounded diagnostic/A-B;
// normal ownership and attachment state remain in the weapon hook.
struct WeaponFrameBaseReceipt final {
    bool valid{};
    std::uint64_t controller_generation{};
    std::uint64_t frame_id{};
    std::uint64_t action_sequence{};
    std::int64_t predicted_display_time{};
    std::uint64_t controller_publication_milliseconds{};
    std::uint64_t weapon_sample_milliseconds{};
    std::uint32_t same_frame_sample_count{};
    bool scene_base_lock_eligible{};
    wawvr::xr::Posef tracking_anchor{};
    wawvr::xr::Posef head_center{};
    wawvr::xr::Vec3f camera_origin{};
    wawvr::xr::Basis3f body_axis{};
    wawvr::xr::Vec3f weapon_origin{};
    wawvr::xr::Basis3f weapon_axis{};
};

struct WeaponFrameBasePhaseComparison final {
    bool valid{};
    float origin_delta_iw_units{};
    float forward_delta_degrees{};
    float left_delta_degrees{};
    float up_delta_degrees{};
};

// Compares the base sampled by weapon placement with the final stock scene
// base only when both belong to the same immutable XR prediction.
[[nodiscard]] bool compare_weapon_frame_base_to_scene(
    const WeaponFrameBaseReceipt& receipt,
    const wawvr::xr::FrameState& frame,
    const wawvr::xr::Posef& tracking_anchor,
    const wawvr::xr::Vec3f& scene_origin,
    const wawvr::xr::Basis3f& scene_body_axis,
    WeaponFrameBasePhaseComparison* comparison) noexcept;

// Diagnostic A/B: returns true and selects the weapon-sampled base only for an
// exact eligible frame. Callers retain the stock scene base on false.
[[nodiscard]] bool select_weapon_aligned_scene_base(
    const WeaponFrameBaseReceipt& receipt,
    const wawvr::xr::FrameState& frame,
    const wawvr::xr::Posef& tracking_anchor,
    const wawvr::xr::Vec3f& scene_origin,
    const wawvr::xr::Basis3f& scene_body_axis,
    wawvr::xr::Vec3f* selected_origin,
    wawvr::xr::Basis3f* selected_axis,
    WeaponFrameBasePhaseComparison* comparison) noexcept;

}  // namespace wawvr::mod
