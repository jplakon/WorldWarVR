// SPDX-License-Identifier: GPL-3.0-only
#include "sight_geometry_shot_audit.hpp"
#include "weapon_ballistics_logic.hpp"
#include <cmath>

namespace wawvr::mod {
namespace {
using wawvr::xr::Vec3f;using wawvr::xr::Basis3f;
bool finite(Vec3f v) noexcept {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
Vec3f subtract(Vec3f a,Vec3f b) noexcept {return {a.x-b.x,a.y-b.y,a.z-b.z};}
float dot(Vec3f a,Vec3f b) noexcept {return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec3f cross(Vec3f a,Vec3f b) noexcept {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
float length(Vec3f v) noexcept {return std::sqrt(dot(v,v));}
bool basis_valid(Basis3f a) noexcept {
    return finite(a.forward)&&finite(a.left)&&finite(a.up)&&
        std::abs(dot(a.forward,a.forward)-1.F)<.002F&&std::abs(dot(a.left,a.left)-1.F)<.002F&&
        std::abs(dot(a.up,a.up)-1.F)<.002F&&std::abs(dot(a.forward,a.left))<.002F&&
        std::abs(dot(a.forward,a.up))<.002F&&std::abs(dot(a.left,a.up))<.002F&&dot(cross(a.forward,a.left),a.up)>.998F;
}
bool profile_valid(const SightGeometryProfile* p) noexcept {
    return p&&p->recipe&&!p->weapon.empty()&&!p->model.empty()&&
        p->model==p->recipe->model&&p->recipe->fingerprint&&sight_geometry_basis_kind_valid(p->recipe->basis_kind);
}
bool direction(const SightGeometryShotAuditSource& s,Vec3f& output) noexcept {
    if(!finite(s.geometry.world_front)||!finite(s.geometry.world_rear))return false;
    const auto d=subtract(s.geometry.world_front,s.geometry.world_rear);const float distance=length(d);
    if(!std::isfinite(distance)||distance<.05F||distance>1000.F)return false;
    output={d.x/distance,d.y/distance,d.z/distance};return finite(output);
}
float angle(Vec3f a,Vec3f b) noexcept {
    return std::atan2(length(cross(a,b)),dot(a,b))*57.29577951308232F;
}
} // namespace
bool sight_geometry_shot_audit_source_valid(const SightGeometryShotAuditSource& s) noexcept {
    const auto& g=s.geometry;Vec3f ignored{};
    return s.valid&&profile_valid(g.profile)&&g.recipe==g.profile->recipe&&g.fingerprint==g.recipe->fingerprint&&
        g.basis_kind==g.recipe->basis_kind&&
        s.definition&&s.dobj&&s.weapon_index>0&&s.weapon_index<128&&g.definition==s.definition&&g.dobj==s.dobj&&
        g.model&&g.skeleton_timestamp&&basis_valid(g.evaluated_root_basis)&&
        s.controller_generation&&s.controller_published_ms&&g.controller_generation==s.controller_generation&&
        g.publication_milliseconds==s.controller_published_ms&&s.publication_ms&&s.publication_serial&&s.captured_ms&&
        s.controller_published_ms<=s.publication_ms&&s.publication_ms<=s.captured_ms&&
        s.captured_ms-s.controller_published_ms<=kMaximumPublishedMuzzleAgeMilliseconds&&
        (s.grip_mode==WeaponGripMode::RightHand||s.grip_mode==WeaponGripMode::LeftHand||s.grip_mode==WeaponGripMode::TwoHand)&&
        finite(s.muzzle)&&basis_valid(s.selected_basis)&&direction(s,ignored);
}
bool publish_sight_geometry_shot_audit_source(const SightGeometryShotAuditSource& source,
    SightGeometryShotAuditHistory* history) noexcept {
    if(!history||history->next>=history->sources.size()||!sight_geometry_shot_audit_source_valid(source)||
        source.publication_serial<=history->last_serial)return false;
    history->sources[history->next]=source;history->next=(history->next+1)%history->sources.size();
    history->last_serial=source.publication_serial;return true;
}
SightGeometryShotAuditStatus match_sight_geometry_shot_audit_source(const SightGeometryShotAuditHistory& history,
    const SightGeometryShotAuditFire& fire,SightGeometryShotAuditMatch* output) noexcept {
    if(!output||!profile_valid(fire.profile)||!fire.definition||!fire.native_definition||!fire.dobj||!fire.model||
        fire.weapon_index<=0||fire.weapon_index>=128||!fire.fingerprint||!fire.generation||!fire.source_generation||
        !fire.source_published_ms||!fire.source_publication_serial||!fire.now_ms||!finite(fire.origin)||!basis_valid(fire.actual_basis))
        return SightGeometryShotAuditStatus::invalid_fire;
    const SightGeometryShotAuditSource* source{};
    for(const auto& candidate:history.sources)if(candidate.valid&&candidate.publication_serial==fire.source_publication_serial){
        if(source)return SightGeometryShotAuditStatus::source_mismatch;source=&candidate;
    }
    if(!source)return SightGeometryShotAuditStatus::missing;
    if(!sight_geometry_shot_audit_source_valid(*source))return SightGeometryShotAuditStatus::invalid_source;
    const auto& g=source->geometry;
    if(g.profile!=fire.profile||g.recipe!=fire.profile->recipe||g.fingerprint!=fire.fingerprint||g.model!=fire.model||
        source->definition!=fire.definition||source->dobj!=fire.dobj||source->weapon_index!=fire.weapon_index||
        source->controller_generation!=fire.source_generation||source->publication_ms!=fire.source_published_ms)
        return SightGeometryShotAuditStatus::source_mismatch;
    if(fire.source_generation>fire.generation||fire.generation-fire.source_generation>kMaximumPublishedMuzzleGenerationLag||
        fire.now_ms<source->captured_ms||fire.now_ms<source->publication_ms||fire.now_ms<source->controller_published_ms||
        fire.now_ms-source->publication_ms>kMaximumPublishedMuzzleAgeMilliseconds||
        fire.now_ms-source->controller_published_ms>kMaximumPublishedMuzzleAgeMilliseconds)return SightGeometryShotAuditStatus::stale;
    SightGeometryShotAuditMatch result{};result.source=*source;result.native_definition=fire.native_definition;
    if(!direction(*source,result.world_sight_direction))return SightGeometryShotAuditStatus::invalid_source;
    result.generation_lag=fire.generation-fire.source_generation;result.source_age_ms=fire.now_ms-source->publication_ms;
    result.controller_age_ms=fire.now_ms-source->controller_published_ms;
    result.selected_sight_degrees=angle(source->selected_basis.forward,result.world_sight_direction);
    result.actual_sight_degrees=angle(fire.actual_basis.forward,result.world_sight_direction);
    result.selected_actual_degrees=angle(source->selected_basis.forward,fire.actual_basis.forward);
    result.muzzle_distance=length(subtract(source->muzzle,fire.origin));
    result.sight_mismatch=result.selected_sight_degrees>.01F||result.actual_sight_degrees>.01F;
    if(result.muzzle_distance>.001F)return SightGeometryShotAuditStatus::muzzle_mismatch;
    if(result.selected_actual_degrees>.01F||angle(source->selected_basis.left,fire.actual_basis.left)>.01F||
        angle(source->selected_basis.up,fire.actual_basis.up)>.01F)return SightGeometryShotAuditStatus::basis_mismatch;
    *output=result;return SightGeometryShotAuditStatus::accepted;
}
const char* sight_geometry_shot_audit_status_name(SightGeometryShotAuditStatus s) noexcept {
    switch(s){case SightGeometryShotAuditStatus::accepted:return "accepted";
    case SightGeometryShotAuditStatus::invalid_source:return "invalid-source";case SightGeometryShotAuditStatus::invalid_fire:return "invalid-fire";
    case SightGeometryShotAuditStatus::missing:return "missing";case SightGeometryShotAuditStatus::source_mismatch:return "source-mismatch";
    case SightGeometryShotAuditStatus::stale:return "stale";case SightGeometryShotAuditStatus::muzzle_mismatch:return "muzzle-mismatch";
    case SightGeometryShotAuditStatus::basis_mismatch:return "basis-mismatch";}
    return "unknown";
}
} // namespace wawvr::mod
