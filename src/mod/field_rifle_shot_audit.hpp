// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "field_rifle_sight_basis.hpp"
#include "weapon_grip_logic.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace wawvr::mod {

// Observation only. Nothing in this module selects a production aim profile,
// changes attack freshness, reads native memory or calls a game function.
// The caller supplies a successful SAME-PASS FieldRifleProof and enables the
// observer only for the explicit aim/server-impact diagnostic experiment.
inline constexpr std::size_t kFieldRifleShotAuditHistoryCapacity = 32;
inline constexpr std::uint32_t kFieldRifleShotAuditMaximumReports = 96;

struct FieldRifleShotAuditSource final {
    bool valid{};
    FieldRifleProof geometry{};
    std::uint32_t definition{}, dobj{};
    std::int32_t weapon_index{};
    std::uint64_t controller_generation{}, controller_published_ms{};
    // These identify the ACTUAL published muzzle, not a later geometry sample.
    std::uint64_t publication_ms{}, publication_serial{}, captured_ms{};
    std::uint64_t frame_id{}, action_sequence{};
    wawvr::xr::Basis3f selected_basis{};
    wawvr::xr::Vec3f muzzle{};
    WeaponGripMode grip_mode{WeaponGripMode::Chest};
    bool tracked_live{}, transition_pending{};

    // Optional temporal evidence. Lack of this evidence does not turn a valid
    // same-source geometric observation into a pre-trigger/rested claim.
    bool raw_poses_valid{};
    wawvr::xr::Posef head{}, right_hand{}, left_hand{};
    wawvr::xr::Vec3f parent_camera_origin{};
    wawvr::xr::Basis3f parent_camera_basis{};
    std::int32_t weapon_state{-1}, weapon_time{-1}, weapon_delay{-1};
    bool trigger_active{}, trigger_click_active{}, trigger_click{};
    float trigger{};
};

struct FieldRifleShotAuditHistory final {
    // Callers synchronize publication/matching with their diagnostic lock.
    // This fixed ring contains no allocation and never retries a game action.
    std::array<FieldRifleShotAuditSource, kFieldRifleShotAuditHistoryCapacity> sources{};
    std::size_t next{};
    std::uint64_t last_serial{};
};

struct FieldRifleShotAuditFire final {
    FieldRifleAsset asset{FieldRifleAsset::none};
    // Canonical identity independently resolved for this real local discharge.
    // WeaponParms can carry a different native WeaponDef pointer; record that
    // pointer separately rather than silently equating it with this identity.
    std::uint32_t definition{}, native_definition{};
    std::int32_t weapon_index{};
    std::uint64_t generation{}, source_generation{}, source_published_ms{};
    std::uint64_t source_publication_serial{}, now_ms{};
    wawvr::xr::Vec3f origin{};
    wawvr::xr::Basis3f actual_basis{};
};

enum class FieldRifleShotAuditStatus : std::uint8_t {
    accepted, invalid_source, invalid_fire, missing, source_mismatch,
    stale, muzzle_mismatch, basis_mismatch,
};

struct FieldRifleShotAuditMatch final {
    FieldRifleShotAuditSource source{};
    wawvr::xr::Vec3f world_sight_direction{};
    std::uint64_t generation_lag{}, source_age_ms{};
    float selected_sight_degrees{}, actual_sight_degrees{};
    float selected_actual_degrees{}, muzzle_distance{};
    bool selected_sight_mismatch{};
};

// Bits are diagnostic rejection reasons, not a new production input policy:
// 1 invalid/rested/input evidence; 2 identity/grip; 4 serial/generation order;
// 8 age/lag; 10 raw head/hand movement; 20 parent-camera movement;
// 40 no source press. A candidate is never relabelled the exact shot source.
struct FieldRiflePretriggerComparison final {
    bool available{}, qualified{};
    std::uint32_t rejection_bits{};
    FieldRifleShotAuditSource preceding{};
    float sight_change_degrees{};
};

[[nodiscard]] bool field_rifle_shot_audit_source_valid(
    const FieldRifleShotAuditSource&) noexcept;
// Invalid/replayed publications leave the history intact. Serial order is
// strictly increasing, even across weapon switches and same-generation calls.
[[nodiscard]] bool publish_field_rifle_shot_audit_source(
    const FieldRifleShotAuditSource&, FieldRifleShotAuditHistory*) noexcept;
[[nodiscard]] FieldRifleShotAuditStatus match_field_rifle_shot_audit_source(
    const FieldRifleShotAuditHistory&, const FieldRifleShotAuditFire&,
    FieldRifleShotAuditMatch*) noexcept;
[[nodiscard]] FieldRiflePretriggerComparison compare_field_rifle_pretrigger(
    const FieldRifleShotAuditHistory&, const FieldRifleShotAuditMatch&,
    const FieldRifleShotAuditFire&) noexcept;
[[nodiscard]] const char* field_rifle_shot_audit_status_name(
    FieldRifleShotAuditStatus) noexcept;

} // namespace wawvr::mod
