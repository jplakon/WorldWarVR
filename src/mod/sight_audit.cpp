// SPDX-License-Identifier: GPL-3.0-only
#include "sight_audit.hpp"
#include "sight_audit_recipes.hpp"
#include "sight_pixel_geometry_recipes.hpp"
#include "sight_pixel_projection_logic.hpp"
#include "resident_page_access.hpp"
#include "stereo_diagnostics.hpp"
#include "t4_layout_selector.hpp"
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>

namespace wawvr::mod {
namespace {
std::atomic<bool> g_enabled{false};
SRWLOCK g_lock=SRWLOCK_INIT;
SightAuditSnapshot g_snapshot{};
SightAuditStatus g_publication_status=SightAuditStatus::missing;
std::array<SightAuditSnapshot,8> g_pretrigger_history{};
std::size_t g_pretrigger_cursor{};
SightAuditSnapshot g_previous_observation{};
bool g_previous_pass_completed{};
struct Deferred final {
    bool waiting{};
    SightAuditMetadata metadata{};
    SightAuditSourceWitness witness{};
};
Deferred g_deferred{};
struct Pending final {
    SightAuditSnapshot snapshot{};
    SightAuditSnapshot pretrigger{};
    std::uint32_t pretrigger_reason_bits{0x80U};
    SightAuditFireRequest armed{};
    SightAuditStatus status{SightAuditStatus::missing};
};
thread_local Pending g_pending{};
thread_local std::uint64_t g_previous_discharge_ms{};
std::atomic<std::uint32_t> g_records{0};
std::atomic<std::uint32_t> g_metadata_reported_bits{0};
std::atomic<bool> g_header_logged{false};
std::atomic<std::uint32_t> g_deferred_reported_statuses{0};
void log_header_once() noexcept {
    if (!g_header_logged.exchange(true,std::memory_order_relaxed)) stereo_diagnostic_log(
        "GameplaySightAudit enabled=1 profile=sp-1.7.1263 schema=3 recipes=garand-mesh-v1:F65F992B98170A63,mosin-notch-mesh-v1:0F3A6AFAFF69027F,svt-notch-mesh-v1:CB9A2C0EF0FB6399,colt-notch-mesh-v1:23826C4308A596D6 sourceAgeLimitMs=100 sourceLagLimit=4 synchronousTokenLimitMs=25 preTriggerAgeLimitMs=250 preTriggerLagLimit=8 rawPositionStabilityMeters=0.0001 rawQuaternionAbsDotMinimum=0.99999 parentPositionStabilityIw=0.005 parentAxisStability=0.00001 observationOnly=1 perEyePixelsNotClaimed=1");
}
bool requested(const wchar_t* name) noexcept {
    std::array<wchar_t,2> value{};
    return GetEnvironmentVariableW(name,value.data(),2)==1&&value[0]==L'1';
}
bool read_memory(void*,std::uintptr_t address,void* output,std::size_t size) noexcept {
    if (!output||!resident_page_access(reinterpret_cast<const void*>(address),size,false,&VirtualQuery))
        return false;
    std::memcpy(output,reinterpret_cast<const void*>(address),size); return true;
}
// Caller owns g_lock. Waiting for natural evaluation is not accepted geometry
// and cannot arm a discharge; keep predecessor history only until this pass
// either completes or is retired without a complete observation.
void commit_observation_locked(const SightAuditSnapshot& sample,SightAuditStatus status,bool waiting) noexcept {
    g_snapshot=sample; g_publication_status=status;
    g_previous_pass_completed=status==SightAuditStatus::accepted;
    if (waiting) return;
    if (status==SightAuditStatus::accepted&&
        !sight_audit_observation_continuous(g_previous_observation,sample)) {
        g_pretrigger_history={}; g_pretrigger_cursor=0;
    }
    if (status!=SightAuditStatus::accepted) {
        g_pretrigger_history={}; g_pretrigger_cursor=0;
    } else if (sample.rested_eligible&&sight_audit_trigger_released(sample.metadata)) {
        const auto previous=(g_pretrigger_cursor+g_pretrigger_history.size()-1)%g_pretrigger_history.size();
        if (g_pretrigger_history[previous].valid&&
            g_pretrigger_history[previous].metadata.generation==sample.metadata.generation)
            g_pretrigger_history[previous]=sample;
        else {
            g_pretrigger_history[g_pretrigger_cursor]=sample;
            g_pretrigger_cursor=(g_pretrigger_cursor+1)%g_pretrigger_history.size();
        }
    }
    g_previous_observation=status==SightAuditStatus::accepted?sample:SightAuditSnapshot{};
}
} // namespace

bool initialize_sight_audit(const wawvr::t4::ValidatedBindings& bindings) noexcept {
    const DWORD saved=GetLastError();
    const bool enabled=requested(L"WAWVR_GAMEPLAY_DIAGNOSTICS")&&
        requested(L"WAWVR_SERVER_IMPACT_DIAGNOSTICS")&&requested(L"WAWVR_SIGHT_DIAGNOSTICS")&&
        !requested(L"WAWVR_DISABLE_XR")&&
        select_t4_layout_family(bindings.profile())==T4LayoutFamily::single_player_1_7_1263&&
        reinterpret_cast<std::uintptr_t>(bindings.module().base)==0x00400000U&&
        bindings.site_bytes_still_match(wawvr::t4::HookSiteId::dobj_bone_layout_sentinel)&&
        bindings.site_bytes_still_match(wawvr::t4::HookSiteId::renderer_model_layout_sentinel)&&
        bindings.site_bytes_still_match(wawvr::t4::HookSiteId::renderer_hide_part_bits_sentinel);
    g_enabled.store(enabled,std::memory_order_release);
    // Bootstrap's diagnostic logger may not yet have its module. Emit the
    // single header at the first late publication/discharge observation.
    SetLastError(saved); return enabled;
}
void shutdown_sight_audit() noexcept {
    g_enabled.store(false,std::memory_order_release);
}
bool sight_audit_enabled() noexcept { return g_enabled.load(std::memory_order_acquire); }
void invalidate_rendered_sight_audit() noexcept {
    if (!g_enabled.load(std::memory_order_acquire)) return;
    AcquireSRWLockExclusive(&g_lock); g_snapshot={};
    // AddPlayerWeapon retires the current receipt at EVERY entry. Preserve
    // predecessor history only when the immediately preceding pass actually
    // published accepted geometry. A failed/no-publication pass breaks it.
    if (!g_previous_pass_completed) {
        g_pretrigger_history={}; g_pretrigger_cursor=0; g_previous_observation={};
    }
    g_previous_pass_completed=false;
    g_deferred={};
    g_publication_status=SightAuditStatus::missing; ReleaseSRWLockExclusive(&g_lock);
}
void publish_rendered_sight_audit(const SightAuditMetadata& metadata) noexcept {
    if (!g_enabled.load(std::memory_order_acquire)) return;
    const DWORD saved=GetLastError();
    log_header_once();
    auto observed=metadata; observed.staged_ms=metadata.captured_ms; observed.observed_frame=metadata.frame_id;
    SightAuditSnapshot sample{}; sample.metadata=observed;
    const auto* profile=detail::sight_asset_recipe(metadata.asset);
    const auto reason_bits=sight_audit_metadata_rejection_bits(metadata);
    if (reason_bits&&(g_metadata_reported_bits.fetch_or(reason_bits,std::memory_order_relaxed)&reason_bits)!=reason_bits) {
        stereo_diagnostic_log(
            "%s reasonBits=0x%08X name=%s definition=0x%08X dobj=0x%08X weapon=%d generation=%llu frame=%llu action=%llu predictedDisplayTime=%lld controllerPublishedMs=%llu posePublishedMs=%llu capturedMs=%llu poseSerial=%llu trackedLive=%u trackingEvidence=0x%02X transition=%u gripMode=%u rightGrip=%.5f leftGrip=%.5f weaponState=%d weaponTime=%d weaponDelay=%d shotCount=%d gripTag=%.32s observationOnly=1",
            (reason_bits&~kSightAuditRestedReasonMask)?"GameplaySightMetadataRejected":"GameplaySightRestedIneligible",
            reason_bits,profile?profile->weapon:"unknown",metadata.definition,metadata.dobj,metadata.weapon_index,
            static_cast<unsigned long long>(metadata.generation),static_cast<unsigned long long>(metadata.frame_id),
            static_cast<unsigned long long>(metadata.action_sequence),static_cast<long long>(metadata.predicted_display_time),
            static_cast<unsigned long long>(metadata.controller_published_ms),static_cast<unsigned long long>(metadata.pose_published_ms),
            static_cast<unsigned long long>(metadata.captured_ms),static_cast<unsigned long long>(metadata.pose_serial),
            metadata.tracked_live?1U:0U,metadata.tracking_evidence,metadata.transition_pending?1U:0U,metadata.grip_mode,
            metadata.right_grip,metadata.left_grip,metadata.weapon_state,metadata.weapon_time,metadata.weapon_delay,
            metadata.shot_count,metadata.grip_tag.data());
        const auto norm=[](const wawvr::xr::Quaternionf q) noexcept { return q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w; };
        stereo_diagnostic_log(
            "GameplaySightMetadataVectorsRejected reasonBits=0x%08X generation=%llu poseSerial=%llu headNormSquared=%.7f rightNormSquared=%.7f leftNormSquared=%.7f headWorldForward=%.7f,%.7f,%.7f headWorldOrigin=%.5f,%.5f,%.5f muzzle=%.5f,%.5f,%.5f projectileForward=%.7f,%.7f,%.7f projectileLeft=%.7f,%.7f,%.7f projectileUp=%.7f,%.7f,%.7f",
            reason_bits,static_cast<unsigned long long>(metadata.generation),static_cast<unsigned long long>(metadata.pose_serial),
            norm(metadata.head.orientation),norm(metadata.right_hand.orientation),norm(metadata.left_hand.orientation),
            metadata.head_world_forward.x,metadata.head_world_forward.y,metadata.head_world_forward.z,
            metadata.head_world_origin.x,metadata.head_world_origin.y,metadata.head_world_origin.z,
            metadata.muzzle.x,metadata.muzzle.y,metadata.muzzle.z,
            metadata.projectile_basis.forward.x,metadata.projectile_basis.forward.y,metadata.projectile_basis.forward.z,
            metadata.projectile_basis.left.x,metadata.projectile_basis.left.y,metadata.projectile_basis.left.z,
            metadata.projectile_basis.up.x,metadata.projectile_basis.up.y,metadata.projectile_basis.up.z);
    }
    auto status=profile
        ? capture_rendered_sights({nullptr,&read_memory},{profile->fingerprint,metadata.asset},observed,&sample)
        : SightAuditStatus::identity;
    Deferred deferred{};
    if (status==SightAuditStatus::unevaluated) {
        const auto seed_status=capture_sight_audit_source_witness({nullptr,&read_memory},observed,&deferred.witness);
        if (seed_status==SightAuditStatus::accepted) {
            deferred.waiting=true; deferred.metadata=observed;
        } else status=seed_status;
    }
    AcquireSRWLockExclusive(&g_lock);
    g_deferred=deferred; commit_observation_locked(sample,status,deferred.waiting);
    ReleaseSRWLockExclusive(&g_lock); SetLastError(saved);
}
void observe_rendered_sight_audit_after_scene(std::uint64_t frame,std::uint32_t view) noexcept {
    if (!g_enabled.load(std::memory_order_acquire)) return;
    const DWORD saved=GetLastError();
    AcquireSRWLockShared(&g_lock); const auto deferred=g_deferred; ReleaseSRWLockShared(&g_lock);
    if (!deferred.waiting) { SetLastError(saved); return; }
    auto metadata=deferred.metadata;
    metadata.captured_ms=GetTickCount64(); metadata.deferred_capture=true;
    metadata.observed_frame=frame; metadata.observed_view=view;
    SightAuditSnapshot sample{}; sample.metadata=metadata;
    const auto* profile=detail::sight_asset_recipe(metadata.asset);
    const auto status=profile&&view<3
        ? capture_deferred_rendered_sights({nullptr,&read_memory},{profile->fingerprint,metadata.asset},
            metadata,deferred.witness,frame,&sample)
        : SightAuditStatus::identity;
    bool committed=false;
    AcquireSRWLockExclusive(&g_lock);
    if (g_deferred.waiting&&g_deferred.metadata.pose_serial==metadata.pose_serial&&
        g_deferred.metadata.generation==metadata.generation&&g_deferred.metadata.frame_id==metadata.frame_id) {
        const bool waiting=status==SightAuditStatus::unevaluated;
        commit_observation_locked(sample,status,waiting);
        if (!waiting) g_deferred={};
        committed=true;
    }
    ReleaseSRWLockExclusive(&g_lock);
    const auto bit=1U<<static_cast<unsigned>(status);
    if (committed&&!(g_deferred_reported_statuses.fetch_or(bit,std::memory_order_relaxed)&bit))
        stereo_diagnostic_log(
            "GameplaySightDeferred status=%s generation=%llu poseSerial=%llu sourceFrame=%llu observedFrame=%llu observedView=%u stagedMs=%llu capturedMs=%llu seedSkeletonTimestamp=%u seedModel=0x%08X seedDobj=0x%08X naturalEvaluationOnly=1 currentReceiptNotRetagged=1",
            sight_audit_status_name(status),static_cast<unsigned long long>(metadata.generation),
            static_cast<unsigned long long>(metadata.pose_serial),static_cast<unsigned long long>(metadata.frame_id),
            static_cast<unsigned long long>(frame),view,static_cast<unsigned long long>(metadata.staged_ms),
            static_cast<unsigned long long>(metadata.captured_ms),deferred.witness.skeleton_timestamp,
            deferred.witness.model,deferred.witness.dobj);
    SetLastError(saved);
}
void observe_sight_pixel_after_scene(const std::uint64_t frame,const std::uint32_t eye,
    const T4SceneView& view,const std::uint32_t width,const std::uint32_t height,
    wawvr::xr::StereoSourceLayout* const layout) noexcept {
    if (!g_enabled.load(std::memory_order_acquire)) return;
    const DWORD saved=GetLastError();
    struct RestoreError final { DWORD value; ~RestoreError(){SetLastError(value);} } restore{saved};
    static const bool enabled=requested(L"WAWVR_SIGHT_PIXEL_DIAGNOSTICS")&&
        requested(L"WAWVR_ORIENTATION_DIAGNOSTICS");
    if (!enabled||!layout||layout->preserve_source_aspect||layout->physical_scope.active||eye>1||!frame) return;
    struct PixelPair final {
        std::uint64_t frame{};
        bool left_ready{};
        SightAuditSnapshot source{};
        std::array<wawvr::xr::Vec3f,kSightPixelAnchorCount> anchors{};
        std::array<T4SceneView,2> views{};
        std::array<std::array<SightPixelProjection,kSightPixelAnchorCount>,2> pixels{};
    };
    static thread_local PixelPair pair{};
    static thread_local SightPixelFrameBudget budget{};
    static thread_local std::uint32_t attempted_assets{};
    SightAuditSnapshot current{}; SightAuditStatus current_status{};
    AcquireSRWLockShared(&g_lock); current=g_snapshot; current_status=g_publication_status; ReleaseSRWLockShared(&g_lock);
    const auto asset=static_cast<std::uint32_t>(current.metadata.asset)+1U;
    if (current_status!=SightAuditStatus::accepted||!current.valid||!current.rested_eligible||
        !sight_audit_trigger_released(current.metadata)||current.metadata.frame_id!=frame||
        asset<1||asset>4||(attempted_assets&(1U<<(asset-1U)))) return;
    const auto* profile=detail::sight_asset_recipe(current.metadata.asset);
    const auto* recipe=detail::sight_pixel_geometry_recipe(current.metadata.asset);
    if (!profile||!recipe) return;
    // Loading, unsupported weapons and chest poses must not consume the
    // finite geometry-read budget before the controlled fixture is equipped.
    if (!admit_sight_pixel_frame(frame,&budget)) { pair={}; return; }
    if (frame!=pair.frame) { pair={}; pair.frame=frame; }
    // Observe actual post-frontend geometry again, without requesting native
    // evaluation. It must still equal this exact published source, not merely
    // resemble another generation or the preceding diagnostic B receipt.
    auto metadata=current.metadata; metadata.captured_ms=GetTickCount64();
    SightAuditSnapshot checked{};
    if (capture_rendered_sights({nullptr,&read_memory},{profile->fingerprint,metadata.asset},metadata,&checked)!=
        SightAuditStatus::accepted||!same_sight_pixel_source(current,checked)) return;
    const auto& rectangle=layout->eyes[eye];
    const auto nearly_equal=[](float a,float b) noexcept {return std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<.000001F;};
    if (!width||!height||!nearly_equal(rectangle.x,float(view.x)/float(width))||
        !nearly_equal(rectangle.y,float(view.y)/float(height))||!nearly_equal(rectangle.width,float(view.width)/float(width))||
        !nearly_equal(rectangle.height,float(view.height)/float(height))) return;
    std::array<wawvr::xr::Vec3f,kSightPixelAnchorCount> anchors{};
    if (!build_sight_pixel_world_anchors(checked,&anchors)) return;
    std::array<SightPixelProjection,kSightPixelAnchorCount> pixels{};
    for (std::size_t i=0;i<anchors.size();++i)
        if (!project_sight_pixel_anchor(anchors[i],view,layout->destinations[eye],width,height,&pixels[i])||
            !pixels[i].inside_eye) return;
    if (eye==0) {
        pair.left_ready=true; pair.source=checked; pair.anchors=anchors;
        pair.views[0]=view; pair.pixels[0]=pixels; return;
    }
    if (!pair.left_ready||!same_sight_pixel_source(pair.source,checked)) return;
    pair.views[1]=view; pair.pixels[1]=pixels;
    layout->sight_pixel_capture={frame,checked.metadata.pose_serial,asset,width,height};
    attempted_assets|=1U<<(asset-1U);
    const auto serial=static_cast<unsigned long long>(checked.metadata.pose_serial);
    stereo_diagnostic_log(
        "GameplaySightPixelPair frame=%llu poseSerial=%llu generation=%llu name=%s definition=0x%08X model=0x%08X dobj=0x%08X skeletonTimestamp=%u geometryFingerprint=%016llX asset=%u sourceWidth=%u sourceHeight=%u anchors=8 observedFrames=%u budgetFrames=512 requestUntouchedBmp=1 projectionPredictionOnly=1 pixelAgreementNotClaimed=1",
        static_cast<unsigned long long>(frame),serial,static_cast<unsigned long long>(checked.metadata.generation),
        profile->weapon,checked.metadata.definition,checked.model,checked.metadata.dobj,checked.skeleton_timestamp,
        static_cast<unsigned long long>(checked.geometry_fingerprint),asset,width,height,budget.observed);
    for (std::uint32_t e=0;e<2;++e) {
        const auto& v=pair.views[e]; const auto& destination=layout->destinations[e];
        stereo_diagnostic_log(
            "GameplaySightPixelView frame=%llu poseSerial=%llu eye=%u origin=%.6f,%.6f,%.6f forward=%.7f,%.7f,%.7f left=%.7f,%.7f,%.7f up=%.7f,%.7f,%.7f tanFov=%.7f,%.7f viewport=%d,%d,%d,%d destination=%.7f,%.7f,%.7f,%.7f nearClip=%.6f",
            static_cast<unsigned long long>(frame),serial,e,v.origin.x,v.origin.y,v.origin.z,
            v.axis.forward.x,v.axis.forward.y,v.axis.forward.z,v.axis.left.x,v.axis.left.y,v.axis.left.z,
            v.axis.up.x,v.axis.up.y,v.axis.up.z,v.tan_half_fov_x,v.tan_half_fov_y,
            v.x,v.y,v.width,v.height,destination.x,destination.y,destination.width,destination.height,v.near_clip);
        for (std::size_t i=0;i<anchors.size();++i) {
            const auto& a=pair.anchors[i]; const auto& p=pair.pixels[e][i]; const auto& spec=(*recipe)[i];
            stereo_diagnostic_log(
                "GameplaySightPixelAnchor frame=%llu poseSerial=%llu eye=%u anchor=%s surface=%u vertex=%u ownerBone=%u world=%.6f,%.6f,%.6f packedPixel=%.5f,%.5f outputEyeUV=%.7f,%.7f depth=%.6f viewportInside=1 visibleOcclusionNotClaimed=1",
                static_cast<unsigned long long>(frame),serial,e,spec.name,spec.surface,spec.vertex,spec.bone,
                a.x,a.y,a.z,p.packed_x,p.packed_y,p.eye_u,p.eye_v,p.depth);
        }
    }
    pair.left_ready=false;
}
void clear_sight_audit_discharge_source() noexcept { g_pending={}; }
void arm_sight_audit_discharge_source(const SightAuditFireRequest& request) noexcept {
    if (!g_enabled.load(std::memory_order_acquire)) return;
    Pending pending{}; pending.armed=request;
    AcquireSRWLockShared(&g_lock);
    pending.snapshot=g_snapshot; pending.status=g_publication_status;
    if (pending.status==SightAuditStatus::accepted) for (const auto& candidate:g_pretrigger_history) {
        if (!candidate.valid) continue;
        const auto bits=sight_audit_pretrigger_rejection_bits(candidate,pending.snapshot,request);
        const bool preferred=!pending.pretrigger.valid||(!bits&&pending.pretrigger_reason_bits)||
            ((bits==0)==(pending.pretrigger_reason_bits==0)&&
             candidate.metadata.pose_serial>pending.pretrigger.metadata.pose_serial);
        if (preferred) { pending.pretrigger=candidate; pending.pretrigger_reason_bits=bits; }
    }
    ReleaseSRWLockShared(&g_lock);
    if (pending.status==SightAuditStatus::accepted)
        pending.status=match_sight_audit_source(pending.snapshot,request);
    g_pending=pending;
}
namespace {
void log_snapshot_records(std::uint32_t sequence,const SightAuditFireRequest& fired,
    const SightAuditSnapshot& s,std::uint64_t previous,const SightAuditSnapshot& fire_source,
    bool same_source,bool diagnostic_only=false) noexcept {
    const auto& m=s.metadata; const auto* profile=detail::sight_asset_recipe(m.asset);
    if (!profile) return;
    const char* prefix=same_source?"GameplaySight":diagnostic_only
        ?"GameplaySightPreTriggerDiagnostic":"GameplaySightPreTrigger";
    const auto gen=static_cast<unsigned long long>(fired.generation);
    const auto source=static_cast<unsigned long long>(m.generation);
    const auto published=static_cast<unsigned long long>(m.pose_published_ms);
    const auto serial=static_cast<unsigned long long>(m.pose_serial);
    stereo_diagnostic_log(
        "%sFire sequence=%u name=%s weapon=%d generation=%llu sourceGeneration=%llu posePublishedMs=%llu poseSerial=%llu controllerPublishedMs=%llu frame=%llu action=%llu predictedDisplayTime=%lld capturedMs=%llu firedMs=%llu sourceAgeMs=%llu sourceLag=%llu definition=0x%08X model=0x%08X dobj=0x%08X skeletonTimestamp=%u recipe=%s geometryFingerprint=%016llX gripMode=%u rightGrip=%.3f leftGrip=%.3f weaponState=%d weaponTime=%d weaponDelay=%d shotCount=%d priorShotAgeMs=%llu gripTag=%s rearCount=%u frontCount=2 floorCount=%u rearBone=%u frontBone=%u rearReference=%s sameSourcePose=%u restedEligible=%u",
        prefix,sequence,profile->weapon,m.weapon_index,gen,source,published,serial,
        static_cast<unsigned long long>(m.controller_published_ms),static_cast<unsigned long long>(m.frame_id),
        static_cast<unsigned long long>(m.action_sequence),static_cast<long long>(m.predicted_display_time),
        static_cast<unsigned long long>(m.captured_ms),static_cast<unsigned long long>(fired.now_ms),
        static_cast<unsigned long long>(fired.now_ms-m.pose_published_ms),gen-source,
        m.definition,s.model,m.dobj,s.skeleton_timestamp,profile->id,static_cast<unsigned long long>(s.geometry_fingerprint),
        m.grip_mode,m.right_grip,m.left_grip,m.weapon_state,m.weapon_time,m.weapon_delay,m.shot_count,
        static_cast<unsigned long long>(previous&&previous<=fired.now_ms?fired.now_ms-previous:0),m.grip_tag.data(),
        s.rear_count,s.floor_count,s.sight_bone,s.front_bone,profile->rear_reference,
        same_source?1U:0U,s.rested_eligible?1U:0U);
    stereo_diagnostic_log(
        "%sGeometry sequence=%u generation=%llu sourceGeneration=%llu posePublishedMs=%llu poseSerial=%llu rear0=%.6f,%.6f,%.6f rear1=%.6f,%.6f,%.6f rear2=%.6f,%.6f,%.6f front0=%.6f,%.6f,%.6f front1=%.6f,%.6f,%.6f sightLeft=%.7f,%.7f,%.7f sightUp=%.7f,%.7f,%.7f muzzle=%.6f,%.6f,%.6f projectileForward=%.7f,%.7f,%.7f projectileLeft=%.7f,%.7f,%.7f projectileUp=%.7f,%.7f,%.7f",
        prefix,sequence,gen,source,published,serial,
        s.rear[0].x,s.rear[0].y,s.rear[0].z,s.rear[1].x,s.rear[1].y,s.rear[1].z,s.rear[2].x,s.rear[2].y,s.rear[2].z,
        s.front[0].x,s.front[0].y,s.front[0].z,s.front[1].x,s.front[1].y,s.front[1].z,
        s.sight_bone_basis.left.x,s.sight_bone_basis.left.y,s.sight_bone_basis.left.z,
        s.sight_bone_basis.up.x,s.sight_bone_basis.up.y,s.sight_bone_basis.up.z,
        m.muzzle.x,m.muzzle.y,m.muzzle.z,m.projectile_basis.forward.x,m.projectile_basis.forward.y,m.projectile_basis.forward.z,
        m.projectile_basis.left.x,m.projectile_basis.left.y,m.projectile_basis.left.z,
        m.projectile_basis.up.x,m.projectile_basis.up.y,m.projectile_basis.up.z);
    if (m.asset==SightAuditAsset::colt) {
        stereo_diagnostic_log(
            "%sNotch sequence=%u generation=%llu sourceGeneration=%llu posePublishedMs=%llu poseSerial=%llu floor0=%.6f,%.6f,%.6f floor1=%.6f,%.6f,%.6f frontBoneLeft=%.7f,%.7f,%.7f frontBoneUp=%.7f,%.7f,%.7f floorReferenceDescriptiveOnly=1 inverseBindApplied=1",
            prefix,sequence,gen,source,published,serial,s.floor[0].x,s.floor[0].y,s.floor[0].z,
            s.floor[1].x,s.floor[1].y,s.floor[1].z,
            s.front_bone_basis.left.x,s.front_bone_basis.left.y,s.front_bone_basis.left.z,
            s.front_bone_basis.up.x,s.front_bone_basis.up.y,s.front_bone_basis.up.z);
    } else if (m.asset==SightAuditAsset::mosin) {
        stereo_diagnostic_log(
            "%sNotch sequence=%u generation=%llu sourceGeneration=%llu posePublishedMs=%llu poseSerial=%llu rear3=%.6f,%.6f,%.6f rear4=%.6f,%.6f,%.6f floor0=%.6f,%.6f,%.6f floor1=%.6f,%.6f,%.6f floor2=%.6f,%.6f,%.6f frontBoneLeft=%.7f,%.7f,%.7f frontBoneUp=%.7f,%.7f,%.7f floorReferenceDescriptiveOnly=1",
            prefix,sequence,gen,source,published,serial,s.rear[3].x,s.rear[3].y,s.rear[3].z,
            s.rear[4].x,s.rear[4].y,s.rear[4].z,s.floor[0].x,s.floor[0].y,s.floor[0].z,
            s.floor[1].x,s.floor[1].y,s.floor[1].z,s.floor[2].x,s.floor[2].y,s.floor[2].z,
            s.front_bone_basis.left.x,s.front_bone_basis.left.y,s.front_bone_basis.left.z,
            s.front_bone_basis.up.x,s.front_bone_basis.up.y,s.front_bone_basis.up.z);
    } else if (m.asset==SightAuditAsset::svt) {
        stereo_diagnostic_log(
            "%sNotch sequence=%u generation=%llu sourceGeneration=%llu posePublishedMs=%llu poseSerial=%llu rear3=%.6f,%.6f,%.6f floor0=%.6f,%.6f,%.6f floor1=%.6f,%.6f,%.6f frontBoneLeft=%.7f,%.7f,%.7f frontBoneUp=%.7f,%.7f,%.7f floorReferenceDescriptiveOnly=1",
            prefix,sequence,gen,source,published,serial,s.rear[3].x,s.rear[3].y,s.rear[3].z,
            s.floor[0].x,s.floor[0].y,s.floor[0].z,s.floor[1].x,s.floor[1].y,s.floor[1].z,
            s.front_bone_basis.left.x,s.front_bone_basis.left.y,s.front_bone_basis.left.z,
            s.front_bone_basis.up.x,s.front_bone_basis.up.y,s.front_bone_basis.up.z);
    }
    stereo_diagnostic_log(
        "%sPose sequence=%u generation=%llu sourceGeneration=%llu posePublishedMs=%llu poseSerial=%llu headPosition=%.6f,%.6f,%.6f headQuaternion=%.7f,%.7f,%.7f,%.7f rightPosition=%.6f,%.6f,%.6f rightQuaternion=%.7f,%.7f,%.7f,%.7f leftPosition=%.6f,%.6f,%.6f leftQuaternion=%.7f,%.7f,%.7f,%.7f headWorldForward=%.7f,%.7f,%.7f headWorldOrigin=%.6f,%.6f,%.6f gripWorld=%.6f,%.6f,%.6f perEyePixelsNotClaimed=1",
        prefix,sequence,gen,source,published,serial,m.head.position.x,m.head.position.y,m.head.position.z,
        m.head.orientation.x,m.head.orientation.y,m.head.orientation.z,m.head.orientation.w,
        m.right_hand.position.x,m.right_hand.position.y,m.right_hand.position.z,
        m.right_hand.orientation.x,m.right_hand.orientation.y,m.right_hand.orientation.z,m.right_hand.orientation.w,
        m.left_hand.position.x,m.left_hand.position.y,m.left_hand.position.z,
        m.left_hand.orientation.x,m.left_hand.orientation.y,m.left_hand.orientation.z,m.left_hand.orientation.w,
        m.head_world_forward.x,m.head_world_forward.y,m.head_world_forward.z,
        m.head_world_origin.x,m.head_world_origin.y,m.head_world_origin.z,
        m.grip_world.x,m.grip_world.y,m.grip_world.z);
    stereo_diagnostic_log(
        "%sQualification sequence=%u generation=%llu sourceGeneration=%llu posePublishedMs=%llu poseSerial=%llu restedReasonBits=0x%08X rightTrigger=%.5f triggerActive=%u triggerChanged=%u triggerClickActive=%u triggerClick=%u triggerClickChanged=%u parentCameraOrigin=%.6f,%.6f,%.6f parentCameraForward=%.7f,%.7f,%.7f parentCameraLeft=%.7f,%.7f,%.7f parentCameraUp=%.7f,%.7f,%.7f fireSourceGeneration=%llu fireSourcePoseSerial=%llu relation=%s notSameShotAimClaimed=%u evidence=%s deferredCapture=%u stagedMs=%llu observedFrame=%llu observedView=%u preTriggerReasonBits=0x%08X acceptedTemporalWitness=%u",
        prefix,sequence,gen,source,published,serial,s.rested_reason_bits,m.right_trigger,
        m.trigger_active?1U:0U,m.trigger_changed?1U:0U,m.trigger_click_active?1U:0U,
        m.trigger_click?1U:0U,m.trigger_click_changed?1U:0U,
        m.parent_camera_origin.x,m.parent_camera_origin.y,m.parent_camera_origin.z,
        m.parent_camera_basis.forward.x,m.parent_camera_basis.forward.y,m.parent_camera_basis.forward.z,
        m.parent_camera_basis.left.x,m.parent_camera_basis.left.y,m.parent_camera_basis.left.z,
        m.parent_camera_basis.up.x,m.parent_camera_basis.up.y,m.parent_camera_basis.up.z,
        static_cast<unsigned long long>(fire_source.metadata.generation),
        static_cast<unsigned long long>(fire_source.metadata.pose_serial),
        same_source?"native-source":diagnostic_only?"preceding-rested-native-camera-changed-diagnostic"
            :"preceding-rested-trigger-released-observation",same_source?0U:1U,
        same_source?"rendered-mesh-native-discharge-token":diagnostic_only
            ?"preceding-rested-rendered-mesh-diagnostic":"preceding-rested-rendered-mesh",
        m.deferred_capture?1U:0U,static_cast<unsigned long long>(m.staged_ms),
        static_cast<unsigned long long>(m.observed_frame),m.observed_view,
        diagnostic_only?0x20U:0U,diagnostic_only?0U:1U);
}
} // namespace
void log_sight_audit_discharge(std::uint32_t sequence,std::string_view name,
    const SightAuditFireRequest& fired) noexcept {
    if (!g_enabled.load(std::memory_order_acquire)) return;
    const DWORD saved=GetLastError();
    log_header_once();
    const auto pending=g_pending; g_pending={}; // exactly one real discharge consumes a token
    const auto previous=g_previous_discharge_ms; g_previous_discharge_ms=fired.now_ms;
    if (g_records.fetch_add(1,std::memory_order_relaxed)>=1024) { SetLastError(saved); return; }
    auto status=pending.status;
    if (status==SightAuditStatus::accepted)
        status=match_sight_audit_discharge(pending.armed,fired);
    const auto* profile=detail::sight_asset_recipe(pending.snapshot.metadata.asset);
    if (status!=SightAuditStatus::accepted||!profile||name!=profile->weapon) {
        const auto& rejected=pending.snapshot.metadata;
        const auto bits=status==SightAuditStatus::invalid_metadata
            ? sight_audit_metadata_rejection_bits(rejected) : 0U;
        stereo_diagnostic_log(
            "GameplaySightRejected sequence=%u generation=%llu name=%.*s reason=%s reasonBits=0x%08X sourceGeneration=%llu posePublishedMs=%llu poseSerial=%llu weaponState=%d weaponTime=%d weaponDelay=%d trackedLive=%u trackingEvidence=0x%02X noSightImpactPairClaimed=1",
            sequence,static_cast<unsigned long long>(fired.generation),static_cast<int>(name.size()),name.data(),
            profile&&name==profile->weapon?sight_audit_status_name(status):"unsupported-weapon",bits,
            static_cast<unsigned long long>(rejected.generation),static_cast<unsigned long long>(rejected.pose_published_ms),
            static_cast<unsigned long long>(rejected.pose_serial),rejected.weapon_state,rejected.weapon_time,
            rejected.weapon_delay,rejected.tracked_live?1U:0U,rejected.tracking_evidence);
        SetLastError(saved); return;
    }
    log_snapshot_records(sequence,fired,pending.snapshot,previous,pending.snapshot,true);
    auto pre_bits=pending.pretrigger_reason_bits;
    if (pending.pretrigger.valid)
        pre_bits=sight_audit_pretrigger_rejection_bits(pending.pretrigger,pending.snapshot,fired);
    if (!pre_bits) log_snapshot_records(sequence,fired,pending.pretrigger,previous,pending.snapshot,false);
    else {
        stereo_diagnostic_log(
        "GameplaySightPreTriggerRejected sequence=%u generation=%llu fireSourceGeneration=%llu fireSourcePoseSerial=%llu preGeneration=%llu prePoseSerial=%llu reasonBits=0x%08X notSameShotAimClaimed=1",
        sequence,static_cast<unsigned long long>(fired.generation),
        static_cast<unsigned long long>(pending.snapshot.metadata.generation),
        static_cast<unsigned long long>(pending.snapshot.metadata.pose_serial),
        static_cast<unsigned long long>(pending.pretrigger.metadata.generation),
        static_cast<unsigned long long>(pending.pretrigger.metadata.pose_serial),pre_bits);
        if (pending.pretrigger.valid) {
            const auto& a=pending.pretrigger.metadata; const auto& b=pending.snapshot.metadata;
            const auto delta=[](wawvr::xr::Vec3f p,wawvr::xr::Vec3f q) noexcept {
                return (std::max)((std::max)(std::abs(p.x-q.x),std::abs(p.y-q.y)),std::abs(p.z-q.z));
            };
            const auto alignment=[](wawvr::xr::Quaternionf p,wawvr::xr::Quaternionf q) noexcept {
                return std::abs(p.x*q.x+p.y*q.y+p.z*q.z+p.w*q.w);
            };
            const auto axis_delta=(std::max)((std::max)(delta(a.parent_camera_basis.forward,b.parent_camera_basis.forward),
                delta(a.parent_camera_basis.left,b.parent_camera_basis.left)),delta(a.parent_camera_basis.up,b.parent_camera_basis.up));
            stereo_diagnostic_log(
                "GameplaySightPreTriggerStability sequence=%u generation=%llu fireSourceGeneration=%llu fireSourcePoseSerial=%llu preGeneration=%llu prePoseSerial=%llu rawHeadPositionMaxDeltaMeters=%.8f rawRightPositionMaxDeltaMeters=%.8f rawLeftPositionMaxDeltaMeters=%.8f rawHeadQuaternionAbsDot=%.8f rawRightQuaternionAbsDot=%.8f rawLeftQuaternionAbsDot=%.8f parentOriginMaxDeltaIw=%.8f parentAxisMaxDelta=%.8f nativeCameraMayAnimateIndependently=1 noSightBoreEqualityRequired=1",
                sequence,static_cast<unsigned long long>(fired.generation),
                static_cast<unsigned long long>(b.generation),static_cast<unsigned long long>(b.pose_serial),
                static_cast<unsigned long long>(a.generation),static_cast<unsigned long long>(a.pose_serial),
                delta(a.head.position,b.head.position),delta(a.right_hand.position,b.right_hand.position),
                delta(a.left_hand.position,b.left_hand.position),alignment(a.head.orientation,b.head.orientation),
                alignment(a.right_hand.orientation,b.right_hand.orientation),alignment(a.left_hand.orientation,b.left_hand.orientation),
                delta(a.parent_camera_origin,b.parent_camera_origin),axis_delta);
            // Preserve the strict rejection above. This separate, explicitly
            // unaccepted prior receipt permits intrinsic angle comparisons;
            // it makes no stationary-world or same-shot aiming claim.
            if (pre_bits==0x20U)
                log_snapshot_records(sequence,fired,pending.pretrigger,previous,pending.snapshot,false,true);
        }
    }
    SetLastError(saved);
}
} // namespace wawvr::mod
