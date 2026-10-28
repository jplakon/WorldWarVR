// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "xr_types.h"
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace wawvr::mod {
enum class FieldRifleAsset : std::uint8_t { none,carbine,kar98 };
enum class FieldRifleStatus : std::uint8_t {
    accepted,not_applicable,inaccessible,identity,binding,geometry,hidden,
    unevaluated,stale,unstable,invalid_axis,world_position_mismatch,
};
struct FieldRifleReader final {
    void* context{};
    bool (*read)(void*,std::uintptr_t,void*,std::size_t) noexcept{};
};
struct FieldRifleRequest final {
    std::uint32_t definition{},dobj{};
    std::string_view weapon{};
    wawvr::xr::Basis3f evaluated_flash_basis{};
    // Actual same-pass native tag getter WORLD position, not pose placement.
    wawvr::xr::Vec3f evaluated_flash_world_position{};
    bool evaluated_flash_world_position_valid{};
    std::uint64_t controller_generation{},controller_publication_milliseconds{},now_milliseconds{};
    // Floating origin added by native DObj/tag world conversion (retail03520388),
    // not animated viewmodel pose+24 and not a camera/refdef placement origin.
    std::uintptr_t native_skeleton_origin_address{};
};
struct FieldRifleProof final {
    FieldRifleAsset asset{FieldRifleAsset::none};
    std::uint32_t model{},root_bone{},skeleton_timestamp{};
    std::uint64_t fingerprint{};
    wawvr::xr::Vec3f front_reference{},rear_reference{},local_direction{};
    wawvr::xr::Vec3f world_front{},world_rear{};
    wawvr::xr::Basis3f evaluated_root_basis{};
};
[[nodiscard]] FieldRifleAsset field_rifle_asset_for_weapon(std::string_view) noexcept;
[[nodiscard]] std::string_view field_rifle_model_for_asset(FieldRifleAsset) noexcept;
[[nodiscard]] std::string_view field_rifle_recipe_id(FieldRifleAsset) noexcept;
// Exact standard M1Carbine/Kar98 only, not a prefix/variant policy. Authored
// rigid sight topology/binds and current native root/flash/origin are read
// twice. No game calls, native writes, cache or caller timestamp substitution.
// Rejection leaves both caller-owned outputs unchanged.
[[nodiscard]] FieldRifleStatus derive_field_rifle_sight_basis(
    const FieldRifleReader&,const FieldRifleRequest&,
    wawvr::xr::Basis3f*,FieldRifleProof* = nullptr) noexcept;
} // namespace wawvr::mod
