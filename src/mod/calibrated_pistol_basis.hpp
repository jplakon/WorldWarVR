// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "weapon_ballistics_logic.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace wawvr::mod {
struct CalibratedPistolReader final {
    void* context{};
    bool (*read)(void*,std::uintptr_t,void*,std::size_t) noexcept{};
};
struct CalibratedPistolRequest final {
    std::uint32_t definition{},dobj{};
    std::string_view weapon{};
    wawvr::xr::Basis3f evaluated_flash_basis{};
    std::uint64_t controller_generation{},controller_publication_milliseconds{},now_milliseconds{};
    // Native j_bolt resolution performed in this post-T4 pass, not inferred
    // from a nonzero retained skeleton timestamp or tag_flash sibling.
    wawvr::xr::Basis3f evaluated_sight_basis{};
    bool evaluated_sight_basis_valid{};
    std::uint16_t resolved_sight_tag{};
};
struct CalibratedPistolProof final {
    std::uint64_t witness_fingerprint{};
    std::uint32_t model{},sight_bone{},skeleton_timestamp{};
    wawvr::xr::Vec3f rear_reference{},front_reference{},local_direction{},world_direction{};
    // Same double-read native skeleton witness as the exact sight geometry;
    // used only to cancel animated j_gun steering in the rendered outer root.
    wawvr::xr::Basis3f evaluated_root_basis{};
};
enum class CalibratedPistolStatus : std::uint8_t {
    accepted,not_applicable,inaccessible,identity,binding,geometry,hidden,unevaluated,stale,unstable,invalid_axis,
};
// Exact Nacht zombie_colt/model pair only. Small immutable mesh/bind/topology
// and evaluated-bone witnesses are read twice; no cache, game function call,
// native write or muzzle-position change. Rejection leaves both outputs intact.
// Clock fields must be the original current controller publication, never now
// substituted as a fresh source timestamp by the caller.
[[nodiscard]] CalibratedPistolStatus derive_calibrated_pistol_basis(
    const CalibratedPistolReader&,const CalibratedPistolRequest&,
    wawvr::xr::Basis3f*,CalibratedPistolProof* = nullptr) noexcept;
} // namespace wawvr::mod

