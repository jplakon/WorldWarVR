// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "weapon_ballistics_logic.hpp"
#include "weapon_grip_logic.hpp"
#include <cmath>
#include <string_view>

namespace wawvr::mod {
struct FieldRifleRootGate final {
    std::string_view weapon{}, model{};
    bool exact_asset_witness{}, current_tracking{}, right_hand_owner{};
    bool transition_pending{}, blocks_attack{};
    std::int32_t native_weapon_state{-1};
    bool retained_render_only{};
};
[[nodiscard]] inline bool field_rifle_root_allowed(const FieldRifleRootGate& g) noexcept {
    const bool exact=(g.weapon=="m1carbine"&&g.model=="viewmodel_usa_m1carbine_rifle")||
        (g.weapon=="kar98k"&&g.model=="viewmodel_ger_kar98_rifle");
    return exact&&g.exact_asset_witness&&(g.current_tracking||g.retained_render_only)&&
        g.right_hand_owner&&!g.transition_pending&&!g.blocks_attack&&
        (g.native_weapon_state==0||g.native_weapon_state==5);
}
// Render-only correction. The caller proves native model/mesh/bind/current
// evaluated root on both sides of its native rebuild. No shot authority or
// controller attachment calibration can be granted by this calculation.
[[nodiscard]] inline bool calculate_field_rifle_root_alignment(
    const FieldRifleRootGate& gate,const wawvr::xr::Vec3f& origin,
    const wawvr::xr::Basis3f& axis,const wawvr::xr::Vec3f& grip,
    const wawvr::xr::Basis3f& evaluated_root,const wawvr::xr::Basis3f& target,
    BazookaViewmodelAlignment* output) noexcept {
    if(!output||!field_rifle_root_allowed(gate)) return false;
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
    if(!valid(axis)||!valid(evaluated_root)||!valid(target)) return false;
    BazookaViewmodelAlignment result{};
    if(!calculate_bazooka_viewmodel_alignment(origin,axis,grip,evaluated_root,target,&result)||
        result.correction_degrees>15.F) return false;
    *output=result;return true;
}
} // namespace wawvr::mod
