// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "xr_types.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace wawvr::mod {

// Explicit immutable recipes only: no weapon-name prefixes, global pitch,
// grip-to-muzzle approximation or discovery of an intended sight from bounds.
inline constexpr std::size_t kSightGeometryMaximumBones = 64;
inline constexpr std::size_t kSightGeometryMaximumSurfaces = 4;
inline constexpr std::size_t kSightGeometryMaximumRanges = 8;
inline constexpr std::size_t kSightGeometryMaximumVertices = 96;
inline constexpr std::size_t kSightGeometryMaximumTriangles = 96;
inline constexpr std::size_t kSightGeometryMaximumReferenceVertices = 64;

struct SightGeometryBone final {
    std::array<float,4> quaternion{};
    wawvr::xr::Vec3f translation{};
    float weight{};
};
static_assert(sizeof(SightGeometryBone)==32);
struct SightGeometryVertex final {
    std::uint16_t index{};
    wawvr::xr::Vec3f point{};
};
struct SightGeometryTriangle final {
    std::uint16_t index{};
    std::array<std::uint16_t,3> vertices{};
};
// Vertex starts are cumulative, exactly as in native ordered rigid ranges.
// Bone is the model-local ordinal, NOT the native bone*64 encoding.
struct SightGeometryRigidRange final {
    std::uint16_t bone{},vertices{},first_triangle{},triangles{};
};
struct SightGeometrySurface final {
    std::uint8_t index{};
    std::uint16_t vertex_count{},triangle_count{};
    std::string_view material{};
    std::span<const SightGeometryRigidRange> rigid_ranges{};
    std::span<const SightGeometryVertex> vertices{};
    std::span<const SightGeometryTriangle> triangles{};
};
struct SightGeometryReferenceVertex final {
    // Actual native surface index, not its ordinal in the recipe span.
    std::uint8_t surface{};
    std::uint16_t vertex{};
    float weight{1.F};
};
enum class SightGeometryReferenceMethod : std::uint8_t {
    weighted_centroid,
    // Fits an authored aperture in the model Y/Z plane; X is its weighted
    // mean. This is NOT an outer protective hood chosen from a bounding box.
    aperture_circle_yz,
};
struct SightGeometryReference final {
    std::uint8_t owner_bone{};
    SightGeometryReferenceMethod method{};
    std::span<const SightGeometryReferenceVertex> vertices{};
};
enum class SightGeometryBasisKind : std::uint8_t { iron_sight=0,barrel_axis=1 };
[[nodiscard]] inline constexpr bool sight_geometry_basis_kind_valid(SightGeometryBasisKind kind) noexcept {
    return kind==SightGeometryBasisKind::iron_sight||kind==SightGeometryBasisKind::barrel_axis;
}
[[nodiscard]] inline constexpr const char* sight_geometry_basis_kind_name(SightGeometryBasisKind kind) noexcept {
    switch(kind){case SightGeometryBasisKind::iron_sight:return "iron_sight";
    case SightGeometryBasisKind::barrel_axis:return "barrel_axis";}
    return "unknown";
}
struct SightGeometryRecipe final {
    std::string_view id{},model{};
    std::span<const std::string_view> bone_names{};
    // Exact native relative parent offsets for non-root bones.
    std::span<const std::uint8_t> parent_offsets{};
    // Full absolute model bind transforms, including nonidentity rotations.
    std::span<const SightGeometryBone> binds{};
    std::uint8_t surface_count{},root_bone{},flash_bone{};
    std::span<const SightGeometrySurface> surfaces{};
    SightGeometryReference front{},rear{};
    std::uint64_t fingerprint{};
    // A witnessed barrel axis is NOT an iron-sight or finite-distance zero.
    // Appended default preserves existing explicit iron-only aggregates.
    SightGeometryBasisKind basis_kind{SightGeometryBasisKind::iron_sight};
};
struct SightGeometryProfile final {
    std::string_view weapon{},model{};
    const SightGeometryRecipe* recipe{};
};
struct SightGeometryReader final {
    void* context{};
    bool (*read)(void*,std::uintptr_t,void*,std::size_t) noexcept{};
};
struct SightGeometryRequest final {
    std::uint32_t definition{},dobj{};
    std::string_view weapon{},model{};
    wawvr::xr::Basis3f evaluated_flash_basis{};
    wawvr::xr::Vec3f evaluated_flash_world{};
    bool evaluated_flash_world_valid{};
    std::uint64_t controller_generation{},controller_publication_milliseconds{},now_milliseconds{};
    // Passed from layout-validated native bindings; no hardcoded executable
    // addresses, no assumption that every hands model has seventy bones.
    std::uintptr_t native_skeleton_origin_address{},script_string_pool_address{};
};
struct SightGeometryProof final {
    const SightGeometryProfile* profile{};
    const SightGeometryRecipe* recipe{};
    std::uint32_t definition{},dobj{},model{},skeleton_timestamp{};
    std::uint16_t root_bone{},front_bone{},rear_bone{},flash_bone{};
    std::uint64_t fingerprint{},controller_generation{},publication_milliseconds{};
    wawvr::xr::Vec3f front_reference{},rear_reference{},world_front{},world_rear{};
    wawvr::xr::Basis3f evaluated_root_basis{};
    SightGeometryBasisKind basis_kind{SightGeometryBasisKind::iron_sight};
};
enum class SightGeometryStatus : std::uint8_t {
    accepted,not_applicable,invalid_recipe,inaccessible,identity,binding,
    geometry,hidden,unevaluated,stale,unstable,invalid_axis,world_position_mismatch,
};

// Expected immutable witness digest. The digest excludes recipe.fingerprint
// itself and includes identity, binds, topology, ownership and reference
// selection. Returns zero for malformed/unbounded/unbacked recipe data.
[[nodiscard]] std::uint64_t sight_geometry_recipe_fingerprint(
    const SightGeometryRecipe&) noexcept;
[[nodiscard]] const SightGeometryProfile* find_sight_geometry_profile(
    std::span<const SightGeometryProfile>,std::string_view weapon,
    std::string_view model) noexcept;

// Two bounded independent native reads; no allocation, cache, native game
// call, memory write or controller calibration mutation. Each authored sight
// reference is inverse-bound then transformed by its OWN current native bone.
// Failure preserves caller-owned output/proof bit-for-bit. A valid output is
// a measured rendered sight ray, not proof of server impact or headset zero.
[[nodiscard]] SightGeometryStatus derive_sight_geometry_basis(
    const SightGeometryReader&,const SightGeometryRequest&,
    std::span<const SightGeometryProfile>,wawvr::xr::Basis3f*,
    SightGeometryProof* = nullptr) noexcept;
[[nodiscard]] const char* sight_geometry_status_name(SightGeometryStatus) noexcept;
} // namespace wawvr::mod
