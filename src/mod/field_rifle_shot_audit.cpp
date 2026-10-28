// SPDX-License-Identifier: GPL-3.0-only
#include "field_rifle_shot_audit.hpp"
#include "weapon_ballistics_logic.hpp"
#include <algorithm>
#include <cmath>

namespace wawvr::mod {
namespace {
using wawvr::xr::Vec3f;
using wawvr::xr::Basis3f;
constexpr float kDegreesPerRadian = 180.F / 3.14159265358979323846F;
constexpr float kMismatchDegrees = .01F;

bool asset_valid(FieldRifleAsset a) noexcept {
    return a == FieldRifleAsset::carbine || a == FieldRifleAsset::kar98;
}
bool finite(Vec3f p) noexcept {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
Vec3f subtract(Vec3f a, Vec3f b) noexcept {
    return {a.x-b.x, a.y-b.y, a.z-b.z};
}
float dot(Vec3f a, Vec3f b) noexcept {
    return a.x*b.x+a.y*b.y+a.z*b.z;
}
Vec3f cross(Vec3f a, Vec3f b) noexcept {
    return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}
float length(Vec3f a) noexcept { return std::sqrt(dot(a,a)); }
bool basis_valid(Basis3f a) noexcept {
    return finite(a.forward) && finite(a.left) && finite(a.up) &&
        std::abs(dot(a.forward,a.forward)-1.F)<.002F &&
        std::abs(dot(a.left,a.left)-1.F)<.002F &&
        std::abs(dot(a.up,a.up)-1.F)<.002F &&
        std::abs(dot(a.forward,a.left))<.002F &&
        std::abs(dot(a.forward,a.up))<.002F &&
        std::abs(dot(a.left,a.up))<.002F &&
        dot(cross(a.forward,a.left),a.up)>.998F;
}
bool pose_valid(const wawvr::xr::Posef& p) noexcept {
    const auto& q=p.orientation;
    const float norm=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    return finite(p.position) && std::isfinite(norm) && std::abs(norm-1.F)<.002F;
}
bool pose_near(const wawvr::xr::Posef& a, const wawvr::xr::Posef& b) noexcept {
    if (!pose_valid(a) || !pose_valid(b) || length(subtract(a.position,b.position))>.001F)
        return false;
    const auto& q=a.orientation; const auto& r=b.orientation;
    const float qn=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    const float rn=r.x*r.x+r.y*r.y+r.z*r.z+r.w*r.w;
    const float similarity=std::abs(q.x*r.x+q.y*r.y+q.z*r.z+q.w*r.w)/std::sqrt(qn*rn);
    // <= 0.1 degree, invariant to quaternion sign.
    return std::isfinite(similarity) && similarity>=.9999996192282494F;
}
bool sight_direction(const FieldRifleShotAuditSource& s, Vec3f* output) noexcept {
    if (!output || !finite(s.geometry.world_front) || !finite(s.geometry.world_rear))
        return false;
    const auto delta=subtract(s.geometry.world_front,s.geometry.world_rear);
    const float distance=length(delta);
    if (!std::isfinite(distance) || distance<.05F || distance>1000.F) return false;
    *output={delta.x/distance,delta.y/distance,delta.z/distance};
    return finite(*output);
}
float angle(Vec3f a, Vec3f b) noexcept {
    // atan2 retains small errors that float acos(dot) would round to zero.
    return std::atan2(length(cross(a,b)),dot(a,b))*kDegreesPerRadian;
}
bool source_released(const FieldRifleShotAuditSource& s) noexcept {
    return s.trigger_active && std::isfinite(s.trigger) && s.trigger>=0.F &&
        s.trigger<.1F && (!s.trigger_click_active || !s.trigger_click);
}
bool source_pressed(const FieldRifleShotAuditSource& s) noexcept {
    return (s.trigger_active && std::isfinite(s.trigger) && s.trigger>=.5F && s.trigger<=1.F) ||
        (s.trigger_click_active && s.trigger_click);
}
bool source_rested(const FieldRifleShotAuditSource& s) noexcept {
    return s.tracked_live && !s.transition_pending && s.raw_poses_valid &&
        s.frame_id && s.action_sequence &&
        s.weapon_state==0 && s.weapon_time==0 && s.weapon_delay==0 && source_released(s);
}
} // namespace

bool field_rifle_shot_audit_source_valid(const FieldRifleShotAuditSource& s) noexcept {
    Vec3f ignored{};
    return s.valid && asset_valid(s.geometry.asset) && s.definition && s.dobj &&
        s.weapon_index>0 && s.geometry.model && s.geometry.skeleton_timestamp &&
        s.geometry.fingerprint && basis_valid(s.geometry.evaluated_root_basis) &&
        s.controller_generation && s.controller_published_ms && s.publication_ms &&
        s.publication_serial && s.captured_ms &&
        s.controller_published_ms<=s.publication_ms && s.publication_ms<=s.captured_ms &&
        s.captured_ms-s.controller_published_ms<=kMaximumPublishedMuzzleAgeMilliseconds &&
        (s.grip_mode==WeaponGripMode::RightHand || s.grip_mode==WeaponGripMode::LeftHand ||
         s.grip_mode==WeaponGripMode::TwoHand) &&
        finite(s.muzzle) && basis_valid(s.selected_basis) && sight_direction(s,&ignored) &&
        (!s.raw_poses_valid || (pose_valid(s.head) && pose_valid(s.right_hand) &&
            pose_valid(s.left_hand) && finite(s.parent_camera_origin) &&
            basis_valid(s.parent_camera_basis)));
}

bool publish_field_rifle_shot_audit_source(const FieldRifleShotAuditSource& s,
    FieldRifleShotAuditHistory* history) noexcept {
    if (!history || history->next>=history->sources.size() ||
        !field_rifle_shot_audit_source_valid(s) || s.publication_serial<=history->last_serial)
        return false;
    history->sources[history->next]=s;
    history->next=(history->next+1U)%history->sources.size();
    history->last_serial=s.publication_serial;
    return true;
}

FieldRifleShotAuditStatus match_field_rifle_shot_audit_source(
    const FieldRifleShotAuditHistory& history, const FieldRifleShotAuditFire& fire,
    FieldRifleShotAuditMatch* output) noexcept {
    if (!output || !asset_valid(fire.asset) || !fire.definition || fire.weapon_index<=0 ||
        !fire.generation || !fire.source_generation || !fire.source_published_ms ||
        !fire.source_publication_serial || !fire.now_ms || !finite(fire.origin) ||
        !basis_valid(fire.actual_basis)) return FieldRifleShotAuditStatus::invalid_fire;
    const FieldRifleShotAuditSource* source=nullptr;
    for (const auto& candidate:history.sources) {
        if (candidate.valid && candidate.publication_serial==fire.source_publication_serial) {
            source=&candidate; break;
        }
    }
    if (!source) return FieldRifleShotAuditStatus::missing;
    if (!field_rifle_shot_audit_source_valid(*source))
        return FieldRifleShotAuditStatus::invalid_source;
    if (source->geometry.asset!=fire.asset || source->definition!=fire.definition ||
        source->weapon_index!=fire.weapon_index ||
        source->controller_generation!=fire.source_generation ||
        source->publication_ms!=fire.source_published_ms)
        return FieldRifleShotAuditStatus::source_mismatch;
    if (fire.source_generation>fire.generation ||
        fire.generation-fire.source_generation>kMaximumPublishedMuzzleGenerationLag ||
        fire.now_ms<source->captured_ms || fire.now_ms<source->publication_ms ||
        fire.now_ms-source->publication_ms>kMaximumPublishedMuzzleAgeMilliseconds)
        return FieldRifleShotAuditStatus::stale;
    FieldRifleShotAuditMatch result{};
    result.source=*source;
    if (!sight_direction(*source,&result.world_sight_direction))
        return FieldRifleShotAuditStatus::invalid_source;
    result.generation_lag=fire.generation-fire.source_generation;
    result.source_age_ms=fire.now_ms-source->publication_ms;
    result.selected_sight_degrees=angle(source->selected_basis.forward,result.world_sight_direction);
    result.actual_sight_degrees=angle(fire.actual_basis.forward,result.world_sight_direction);
    result.selected_actual_degrees=angle(source->selected_basis.forward,fire.actual_basis.forward);
    result.muzzle_distance=length(subtract(source->muzzle,fire.origin));
    result.selected_sight_mismatch=result.selected_sight_degrees>kMismatchDegrees;
    // Identity/time correlation remains inspectable when the observed native
    // vectors disagree; this is not a successful fire-vector match.
    *output=result;
    if (result.muzzle_distance>.001F) return FieldRifleShotAuditStatus::muzzle_mismatch;
    if (result.selected_actual_degrees>kMismatchDegrees ||
        angle(source->selected_basis.left,fire.actual_basis.left)>kMismatchDegrees ||
        angle(source->selected_basis.up,fire.actual_basis.up)>kMismatchDegrees)
        return FieldRifleShotAuditStatus::basis_mismatch;
    return FieldRifleShotAuditStatus::accepted;
}

FieldRiflePretriggerComparison compare_field_rifle_pretrigger(
    const FieldRifleShotAuditHistory& history, const FieldRifleShotAuditMatch& match,
    const FieldRifleShotAuditFire& fire) noexcept {
    FieldRiflePretriggerComparison result{};
    const auto& source=match.source;
    if (!field_rifle_shot_audit_source_valid(source) ||
        source.geometry.asset!=fire.asset || source.definition!=fire.definition ||
        source.weapon_index!=fire.weapon_index ||
        source.controller_generation!=fire.source_generation ||
        source.publication_ms!=fire.source_published_ms ||
        source.publication_serial!=fire.source_publication_serial ||
        fire.source_generation>fire.generation ||
        fire.generation-fire.source_generation>kMaximumPublishedMuzzleGenerationLag ||
        fire.now_ms<source.captured_ms ||
        fire.now_ms-source.publication_ms>kMaximumPublishedMuzzleAgeMilliseconds)
        return result;
    const FieldRifleShotAuditSource* preceding=nullptr;
    for (const auto& candidate:history.sources) {
        if (candidate.publication_serial>=source.publication_serial ||
            !field_rifle_shot_audit_source_valid(candidate) || !source_rested(candidate) ||
            candidate.geometry.asset!=source.geometry.asset || candidate.definition!=source.definition ||
            candidate.weapon_index!=source.weapon_index || candidate.dobj!=source.dobj ||
            candidate.geometry.model!=source.geometry.model ||
            candidate.geometry.fingerprint!=source.geometry.fingerprint) continue;
        if (!preceding || candidate.publication_serial>preceding->publication_serial) preceding=&candidate;
    }
    if (!preceding) return result;
    result.available=true; result.preceding=*preceding;
    auto& reasons=result.rejection_bits;
    if (!source.tracked_live || source.transition_pending || !source.raw_poses_valid ||
        !source.frame_id || !source.action_sequence ||
        !source_rested(*preceding)) reasons|=0x1U;
    if (preceding->grip_mode!=source.grip_mode) reasons|=0x2U;
    if (preceding->controller_generation>=source.controller_generation ||
        preceding->publication_serial>=source.publication_serial ||
        preceding->publication_ms>source.publication_ms ||
        preceding->frame_id>source.frame_id ||
        preceding->action_sequence>=source.action_sequence) reasons|=0x4U;
    if (preceding->controller_generation>source.controller_generation ||
        source.controller_generation-preceding->controller_generation>8U ||
        fire.now_ms<preceding->publication_ms || fire.now_ms-preceding->publication_ms>250U)
        reasons|=0x8U;
    if (!source.raw_poses_valid || !pose_near(preceding->head,source.head) ||
        !pose_near(preceding->right_hand,source.right_hand) ||
        !pose_near(preceding->left_hand,source.left_hand)) reasons|=0x10U;
    if (length(subtract(preceding->parent_camera_origin,source.parent_camera_origin))>.001F ||
        angle(preceding->parent_camera_basis.forward,source.parent_camera_basis.forward)>.001F ||
        angle(preceding->parent_camera_basis.left,source.parent_camera_basis.left)>.001F ||
        angle(preceding->parent_camera_basis.up,source.parent_camera_basis.up)>.001F)
        reasons|=0x20U;
    if (!source_pressed(source)) reasons|=0x40U;
    Vec3f previous_direction{}, current_direction{};
    if (!sight_direction(*preceding,&previous_direction) || !sight_direction(source,&current_direction))
        reasons|=0x1U;
    else result.sight_change_degrees=angle(previous_direction,current_direction);
    result.qualified=reasons==0;
    return result;
}

const char* field_rifle_shot_audit_status_name(FieldRifleShotAuditStatus status) noexcept {
    switch (status) {
    case FieldRifleShotAuditStatus::accepted: return "accepted";
    case FieldRifleShotAuditStatus::invalid_source: return "invalid_source";
    case FieldRifleShotAuditStatus::invalid_fire: return "invalid_fire";
    case FieldRifleShotAuditStatus::missing: return "missing";
    case FieldRifleShotAuditStatus::source_mismatch: return "source_mismatch";
    case FieldRifleShotAuditStatus::stale: return "stale";
    case FieldRifleShotAuditStatus::muzzle_mismatch: return "muzzle_mismatch";
    case FieldRifleShotAuditStatus::basis_mismatch: return "basis_mismatch";
    }
    return "unknown";
}

} // namespace wawvr::mod
