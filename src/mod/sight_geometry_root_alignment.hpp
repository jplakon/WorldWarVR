// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sight_geometry_basis.hpp"
#include "weapon_ballistics_logic.hpp"
#include <cmath>

namespace wawvr::mod {
struct SightGeometryRootGate final {
    const SightGeometryProfile* profile{};
    const SightGeometryProof* proof{};
    std::uint32_t definition{},dobj{},model{};
    std::uint64_t controller_generation{},now_milliseconds{};
    bool current_tracking{},right_hand_owner{},transition_pending{},blocks_attack{};
    std::int32_t native_weapon_state{-1};
    bool retained_render_only{},retained_same_owner{};
    std::uint64_t retained_publication_milliseconds{};
};

// A proof from another exact model/weapon/generation may not grant native
// render writes. Retained pose permission is visual-only; it does not widen
// the separate150ms shot source or assert current controller tracking.
[[nodiscard]] inline bool sight_geometry_root_allowed(const SightGeometryRootGate& g) noexcept {
    if(!g.profile||!g.proof||!g.profile->recipe||!g.definition||!g.dobj||!g.model||
        !g.controller_generation||!g.now_milliseconds||!g.right_hand_owner||g.transition_pending||g.blocks_attack||
        (g.native_weapon_state!=0&&g.native_weapon_state!=5))return false;
    const auto& p=*g.proof;const auto& profile=*g.profile;const auto& recipe=*profile.recipe;
    if(p.profile!=g.profile||p.recipe!=profile.recipe||p.definition!=g.definition||p.dobj!=g.dobj||p.model!=g.model||
        profile.model!=recipe.model||profile.weapon.empty()||profile.model.empty()||!recipe.fingerprint||
        !sight_geometry_basis_kind_valid(recipe.basis_kind)||p.basis_kind!=recipe.basis_kind||
        p.fingerprint!=recipe.fingerprint||!p.skeleton_timestamp||
        p.controller_generation!=g.controller_generation||!p.publication_milliseconds||
        p.publication_milliseconds>g.now_milliseconds||
        g.now_milliseconds-p.publication_milliseconds>kMaximumPublishedMuzzleAgeMilliseconds)return false;
    const bool retained=g.retained_render_only&&g.retained_same_owner&&g.retained_publication_milliseconds&&
        g.retained_publication_milliseconds<=g.now_milliseconds&&
        g.now_milliseconds-g.retained_publication_milliseconds<=250;
    return g.current_tracking||retained;
}

// Reuses the accepted shortest-arc launcher math, with a narrow15-degree
// bound and the actual hand-grip pivot. It cancels animated ROOT steering;
// it does not equate root-forward with the measured iron-sight shot line.
// The caller must rebuild the native skeleton, re-witness the exact asset,
// check root-to-controller dot>.99999 and grip drift<=.1IW, or roll back.
// Never persist the compensating outer-root transform as hand calibration.
[[nodiscard]] inline bool calculate_sight_geometry_root_alignment(
    const SightGeometryRootGate& gate,const wawvr::xr::Vec3f& origin,
    const wawvr::xr::Basis3f& outer_axis,const wawvr::xr::Vec3f& grip,
    const wawvr::xr::Basis3f& tracked_target,BazookaViewmodelAlignment* output) noexcept {
    if(!output||!sight_geometry_root_allowed(gate))return false;
    const auto valid=[](const wawvr::xr::Basis3f& a) noexcept {
        const auto dot=[](auto p,auto q) noexcept {return p.x*q.x+p.y*q.y+p.z*q.z;};
        const auto finite=[](auto p) noexcept {return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);};
        const wawvr::xr::Vec3f cross{a.forward.y*a.left.z-a.forward.z*a.left.y,
            a.forward.z*a.left.x-a.forward.x*a.left.z,a.forward.x*a.left.y-a.forward.y*a.left.x};
        return finite(a.forward)&&finite(a.left)&&finite(a.up)&&
            std::abs(dot(a.forward,a.forward)-1.F)<.002F&&std::abs(dot(a.left,a.left)-1.F)<.002F&&
            std::abs(dot(a.up,a.up)-1.F)<.002F&&std::abs(dot(a.forward,a.left))<.002F&&
            std::abs(dot(a.forward,a.up))<.002F&&std::abs(dot(a.left,a.up))<.002F&&dot(cross,a.up)>.998F;
    };
    if(!valid(outer_axis)||!valid(gate.proof->evaluated_root_basis)||!valid(tracked_target))return false;
    BazookaViewmodelAlignment result{};
    if(!calculate_bazooka_viewmodel_alignment(origin,outer_axis,grip,
        gate.proof->evaluated_root_basis,tracked_target,&result)||result.correction_degrees>15.F)return false;
    *output=result;return true;
}
} // namespace wawvr::mod
