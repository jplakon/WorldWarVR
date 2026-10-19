// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "xr_types.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace wawvr::mod {

// Read-only observations. No value in this interface is an aiming override.
inline constexpr std::uint64_t kGarandSightGeometryFingerprint = 0xF65F992B98170A63ULL;
inline constexpr std::uint64_t kSightAuditMaximumSourceAgeMs = 100;
inline constexpr std::uint64_t kSightAuditMaximumGenerationLag = 4;
inline constexpr std::uint64_t kSightAuditMaximumTokenAgeMs = 25;
inline constexpr std::uint64_t kSightAuditMaximumPreTriggerAgeMs = 250;
inline constexpr std::uint64_t kSightAuditMaximumPreTriggerLag = 8;
inline constexpr std::uint32_t kSightAuditRestedReasonMask = 0xE000U;
enum class SightAuditAsset : std::uint8_t { garand,mosin,svt,colt };
inline constexpr std::uint32_t kRequiredSightTrackingEvidence = 0x3FU;
// Metadata rejection bit values below are hexadecimal, stable diagnostic schema:
// 1 identity,2 generation,4 publication IDs,8 live tracking,10 transition,
// 20 grip mode,40 clocks,80 muzzle/basis,100 native head,200 grip world,
// 400 raw poses,800 grip values,1000 grip tag,2000 state,4000 time,8000 delay,
// 10000 trigger values. State/time/delay qualify rest, not geometric validity.
struct SightAuditReader final {
    void* context{};
    bool (*read)(void*,std::uintptr_t,void*,std::size_t) noexcept{};
};
struct SightAuditRecipe final {
    // Production always supplies the frozen measured Garand fingerprint. An
    // injectable recipe lets tests exercise the same bounded memory reader
    // with synthetic geometry; no environment/config value selects a recipe.
    std::uint64_t geometry_fingerprint{kGarandSightGeometryFingerprint};
    SightAuditAsset asset{SightAuditAsset::garand};
};
struct SightAuditMetadata final {
    std::uint32_t definition{},dobj{};
    SightAuditAsset asset{SightAuditAsset::garand};
    std::int32_t weapon_index{};
    std::uint64_t generation{},controller_published_ms{},pose_published_ms{};
    std::uint64_t pose_serial{};
    std::uint64_t frame_id{},action_sequence{};
    std::int64_t predicted_display_time{};
    std::uint64_t captured_ms{};
    std::uint64_t staged_ms{},observed_frame{};
    std::uint32_t observed_view{};
    bool deferred_capture{};
    bool tracked_live{},transition_pending{};
    std::uint32_t tracking_evidence{};
    std::uint32_t grip_mode{};
    float right_grip{},left_grip{};
    float right_trigger{};
    bool trigger_active{},trigger_changed{},trigger_click_active{},trigger_click{},trigger_click_changed{};
    wawvr::xr::Posef head{},right_hand{},left_hand{};
    wawvr::xr::Vec3f head_world_forward{1,0,0},head_world_origin{},grip_world{},muzzle{};
    wawvr::xr::Vec3f parent_camera_origin{};
    wawvr::xr::Basis3f parent_camera_basis{};
    wawvr::xr::Basis3f projectile_basis{};
    std::array<char,32> grip_tag{};
    std::int32_t weapon_state{},weapon_time{},weapon_delay{},shot_count{};
};
struct SightAuditSnapshot final {
    bool valid{};
    bool rested_eligible{};
    std::uint32_t rested_reason_bits{};
    SightAuditMetadata metadata{};
    std::uint32_t model{},skeleton_timestamp{},sight_bone{};
    std::uint32_t front_bone{},rear_count{},floor_count{};
    std::uint64_t geometry_fingerprint{};
    std::array<wawvr::xr::Vec3f,5> rear{};
    std::array<wawvr::xr::Vec3f,2> front{};
    std::array<wawvr::xr::Vec3f,3> floor{};
    wawvr::xr::Basis3f sight_bone_basis{};
    wawvr::xr::Basis3f front_bone_basis{};
};
enum class SightAuditStatus : std::uint8_t {
    accepted,invalid_metadata,inaccessible,identity,geometry,hidden,
    unevaluated,unstable,transform,muzzle_mismatch,weapon_mismatch,
    source_mismatch,stale,fire_mismatch,missing,seed_mismatch,scene_frame_mismatch,
};
// Partial evaluation is not acceptance. This immutable seed permits a later
// READ-ONLY retry only while the exact source root/flash/model/skeleton remains.
struct SightAuditSourceWitness final {
    bool valid{};
    SightAuditAsset asset{};
    std::uint32_t definition{},dobj{},model{},model_table{},matrices{},skeleton_timestamp{};
    std::uint64_t generation{},pose_published_ms{},pose_serial{},frame_id{};
    std::uint32_t total_bones{},root_bone{},flash_bone{};
    std::array<std::uint32_t,2> models{};
    std::array<std::uint32_t,4> hidden{};
    std::array<std::byte,64> root_flash_transforms{};
    wawvr::xr::Vec3f native_origin{};
    // Only Colt sights ride a separately animated slide in this recipe set.
    // Keep its source transform frozen across deferred capture; old assets
    // retain zero/default values and their original root/flash proof.
    std::uint32_t sight_bone{};
    std::array<std::byte,32> sight_transform{};
};
[[nodiscard]] const char* sight_audit_status_name(SightAuditStatus) noexcept;
[[nodiscard]] std::uint32_t sight_audit_metadata_rejection_bits(const SightAuditMetadata&) noexcept;
[[nodiscard]] bool sight_audit_trigger_released(const SightAuditMetadata&) noexcept;
[[nodiscard]] bool sight_audit_observation_continuous(
    const SightAuditSnapshot& previous,const SightAuditSnapshot& current) noexcept;

// Reads the actual DObj weapon XModel, exact mesh topology and evaluated bone
// matrices twice. The geometry recipe is grounded in the measured central
// front-post bevel and three closed rear-aperture cross-sections, not tags or
// protective-wing extrema. Failure leaves output untouched.
[[nodiscard]] SightAuditStatus capture_garand_rendered_sights(
    const SightAuditReader&,const SightAuditRecipe&,
    const SightAuditMetadata&,SightAuditSnapshot*) noexcept;
[[nodiscard]] SightAuditStatus capture_rendered_sights(
    const SightAuditReader&,const SightAuditRecipe&,
    const SightAuditMetadata&,SightAuditSnapshot*) noexcept;
[[nodiscard]] SightAuditStatus capture_sight_audit_source_witness(
    const SightAuditReader&,const SightAuditMetadata&,SightAuditSourceWitness*) noexcept;
[[nodiscard]] SightAuditStatus capture_deferred_rendered_sights(
    const SightAuditReader&,const SightAuditRecipe&,const SightAuditMetadata&,
    const SightAuditSourceWitness&,std::uint64_t scene_frame,SightAuditSnapshot*) noexcept;

struct SightAuditFireRequest final {
    std::uintptr_t weapon_parms{};
    std::uint32_t definition{};
    std::int32_t weapon_index{};
    std::uint64_t generation{},source_generation{},source_published_ms{},now_ms{};
    std::uint64_t source_pose_serial{};
    wawvr::xr::Vec3f origin{};
    wawvr::xr::Basis3f basis{};
};
// Same-source means exact sight/muzzle generation AND publication timestamp,
// identity AND observed fire vectors. The current command generation remains
// separately recorded; a bounded older render is never relabelled zero-lag.
[[nodiscard]] SightAuditStatus match_sight_audit_source(
    const SightAuditSnapshot&,const SightAuditFireRequest&) noexcept;
[[nodiscard]] SightAuditStatus match_sight_audit_discharge(
    const SightAuditFireRequest& armed,const SightAuditFireRequest& fired) noexcept;
// Independent temporal witness, never a substitute for the exact source.
// Bits: 1 invalid/rest/input,2 identity/grip,4 serial/generation ordering,
// 8 age/lag,10 head/controller movement,20 parent camera movement,40 no press.
[[nodiscard]] std::uint32_t sight_audit_pretrigger_rejection_bits(
    const SightAuditSnapshot& preceding,const SightAuditSnapshot& source,
    const SightAuditFireRequest&) noexcept;

} // namespace wawvr::mod
