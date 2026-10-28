// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "weapon_ballistics_logic.hpp"
#include "weapon_grip_logic.hpp"
#include <cmath>
#include <string_view>

namespace wawvr::mod {
struct ColtRootAlignmentGate final {
    std::string_view weapon{}, viewmodel{};
    bool exact_asset_witness{}, current_tracking{}, right_hand_owner{};
    bool transition_pending{}, manual_reload_blocks_attack{};
    std::int32_t native_weapon_state{-1};
    // Render-only bounded owner retention. This flag never participates in
    // controller/attack freshness or any authoritative muzzle gate.
    bool retained_render_only{};
};

[[nodiscard]] inline bool colt_retained_render_allowed(bool current_frame,
    bool same_exact_retained_owner,std::uint64_t now,std::uint64_t retained_source_ms) noexcept {
    return current_frame&&same_exact_retained_owner&&retained_source_ms&&
        now>=retained_source_ms&&now-retained_source_ms<=kWeaponGripTrackingGraceMilliseconds;
}

// This is render-root stabilization, not a ballistic pitch offset. The exact
// Colt mesh witness is required on each side of the native pose rebuild.
// Only READY(0) and FIRING(5) are measured here: equip, holster, reload,
// melee/offhand and ownership changes stay untouched. Qualified brief retained
// rendering is separate from live controller/shot authority.
[[nodiscard]] inline bool colt_root_alignment_allowed(
    const ColtRootAlignmentGate& gate) noexcept {
    return gate.weapon == "zombie_colt" &&
        gate.viewmodel == "viewmodel_usa_colt45_pistol" &&
        gate.exact_asset_witness && (gate.current_tracking||gate.retained_render_only) &&
        gate.right_hand_owner && !gate.transition_pending &&
        !gate.manual_reload_blocks_attack &&
        (gate.native_weapon_state == 0 || gate.native_weapon_state == 5);
}

[[nodiscard]] inline bool calculate_colt_root_alignment(
    const ColtRootAlignmentGate& gate,
    const wawvr::xr::Vec3f& outer_origin,
    const wawvr::xr::Basis3f& outer_axis,
    const wawvr::xr::Vec3f& grip_world,
    const wawvr::xr::Basis3f& evaluated_j_gun_axis,
    const wawvr::xr::Basis3f& controller_owned_axis,
    BazookaViewmodelAlignment* output) noexcept {
    if (!output || !colt_root_alignment_allowed(gate)) return false;
    const auto valid_basis = [](const wawvr::xr::Basis3f& a) noexcept {
        const auto dot = [](const auto& p, const auto& q) noexcept {
            return p.x*q.x+p.y*q.y+p.z*q.z;
        };
        const auto finite = [](const auto& p) noexcept {
            return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
        };
        const wawvr::xr::Vec3f cross{
            a.forward.y*a.left.z-a.forward.z*a.left.y,
            a.forward.z*a.left.x-a.forward.x*a.left.z,
            a.forward.x*a.left.y-a.forward.y*a.left.x};
        return finite(a.forward)&&finite(a.left)&&finite(a.up)&&
            std::abs(dot(a.forward,a.forward)-1.F)<.002F&&
            std::abs(dot(a.left,a.left)-1.F)<.002F&&
            std::abs(dot(a.up,a.up)-1.F)<.002F&&
            std::abs(dot(a.forward,a.left))<.002F&&
            std::abs(dot(a.forward,a.up))<.002F&&
            std::abs(dot(a.left,a.up))<.002F&&dot(cross,a.up)>.998F;
    };
    if (!valid_basis(outer_axis)||!valid_basis(evaluated_j_gun_axis)||
        !valid_basis(controller_owned_axis)) return false;
    BazookaViewmodelAlignment result{};
    // Reuse the already-audited shortest-arc/pivot math. The narrower 15-degree
    // bound covers measured Colt idle/firing motion, never arbitrary poses.
    if (!calculate_bazooka_viewmodel_alignment(outer_origin,outer_axis,
            grip_world,evaluated_j_gun_axis,controller_owned_axis,&result)||
        result.correction_degrees>15.F) return false;
    *output=result;
    return true;
}
} // namespace wawvr::mod
