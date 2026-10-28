// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sight_geometry_basis.hpp"
#include "weapon_grip_logic.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace wawvr::mod {
// Diagnostic correlation ONLY; no native memory, game call, production
// profile selection or attack-authority/freshness mutation occurs here.
inline constexpr std::size_t kSightGeometryShotAuditHistoryCapacity=64;
inline constexpr std::uint32_t kSightGeometryShotAuditMaximumReports=2048;
struct SightGeometryShotAuditSource final {
    bool valid{};
    SightGeometryProof geometry{};
    std::uint32_t definition{},dobj{};
    std::int32_t weapon_index{};
    std::uint64_t controller_generation{},controller_published_ms{};
    std::uint64_t publication_ms{},publication_serial{},captured_ms{};
    wawvr::xr::Basis3f selected_basis{};
    wawvr::xr::Vec3f muzzle{};
    WeaponGripMode grip_mode{WeaponGripMode::Chest};
};
struct SightGeometryShotAuditHistory final {
    std::array<SightGeometryShotAuditSource,kSightGeometryShotAuditHistoryCapacity> sources{};
    std::size_t next{};
    std::uint64_t last_serial{};
};
struct SightGeometryShotAuditFire final {
    const SightGeometryProfile* profile{};
    std::uint32_t definition{},native_definition{},dobj{},model{};
    std::int32_t weapon_index{};
    std::uint64_t fingerprint{},generation{},source_generation{},source_published_ms{};
    std::uint64_t source_publication_serial{},now_ms{};
    wawvr::xr::Vec3f origin{};
    wawvr::xr::Basis3f actual_basis{};
};
enum class SightGeometryShotAuditStatus : std::uint8_t {
    accepted,invalid_source,invalid_fire,missing,source_mismatch,stale,
    muzzle_mismatch,basis_mismatch,
};
struct SightGeometryShotAuditMatch final {
    SightGeometryShotAuditSource source{};
    // Retain the actual native alias separately from the canonical WeaponDef.
    std::uint32_t native_definition{};
    wawvr::xr::Vec3f world_sight_direction{};
    std::uint64_t generation_lag{},source_age_ms{},controller_age_ms{};
    float selected_sight_degrees{},actual_sight_degrees{},selected_actual_degrees{},muzzle_distance{};
    bool sight_mismatch{};
};
[[nodiscard]] bool sight_geometry_shot_audit_source_valid(const SightGeometryShotAuditSource&) noexcept;
[[nodiscard]] bool publish_sight_geometry_shot_audit_source(
    const SightGeometryShotAuditSource&,SightGeometryShotAuditHistory*) noexcept;
// An accepted result proves an exact source/native-parameter correlation,
// NOT an authoritative server trace, impact hit, rested zero or headset pass.
// sight_mismatch separately reports native/selected sight angular disagreement.
// Every nonaccepted result leaves the caller output completely untouched.
[[nodiscard]] SightGeometryShotAuditStatus match_sight_geometry_shot_audit_source(
    const SightGeometryShotAuditHistory&,const SightGeometryShotAuditFire&,
    SightGeometryShotAuditMatch*) noexcept;
[[nodiscard]] const char* sight_geometry_shot_audit_status_name(SightGeometryShotAuditStatus) noexcept;
} // namespace wawvr::mod
