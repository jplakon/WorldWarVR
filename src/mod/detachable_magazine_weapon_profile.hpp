// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "gameplay/manual_reload_logic.hpp"
#include "magazine_charging_logic.hpp"

#include "xr_types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace wawvr::mod {

enum class DetachableMagazineWeaponProfileId : std::uint8_t {
    None = 0,
    ZombieColt = 1,
    M1Carbine = 2,
    Gewehr43 = 3,
    Stg44 = 4,
    Mp40 = 5,
    Thompson = 6,
    Bar = 7,
    Fg42Bipod = 8,
    Walther = 9,
    Colt = 10,
    Tokarev = 11,
    Nambu = 12,
    Svt40 = 13,
    Ppsh = 14,
    Type100 = 15,
    Type99Lmg = 16,
    Type99LmgBipod = 17,
    Type100NoSound = 18,
    ThompsonWet = 19,
    ColtWet = 20,
    BarBipod = 21,
    ZombieColtDedicated = 22,
    ZombieColtUpgraded = 23,
    ZombieM1Carbine = 24,
    ZombieM1CarbineUpgraded = 25,
    ZombieGewehr43 = 26,
    ZombieGewehr43Upgraded = 27,
    ZombieStg44 = 28,
    ZombieStg44Upgraded = 29,
    ZombieThompson = 30,
    ZombieThompsonUpgraded = 31,
    ZombieMp40 = 32,
    ZombieMp40Upgraded = 33,
    ZombieType100 = 34,
    ZombieType100Upgraded = 35,
    ZombieBar = 36,
    ZombieBarUpgraded = 37,
    ZombieFg42 = 38,
    ZombieFg42Upgraded = 39,
    ZombiePpsh = 40,
    ZombiePpshUpgraded = 41,
    M1Garand = 42,
    M1GarandBayonet = 43,
    Ptrs41 = 44,
    M1GarandGrenadeLauncher = 45,
};

[[nodiscard]] constexpr bool is_known_detachable_magazine_weapon_profile_id(
    const DetachableMagazineWeaponProfileId id) noexcept {
    switch (id) {
        case DetachableMagazineWeaponProfileId::ZombieColt:
        case DetachableMagazineWeaponProfileId::M1Carbine:
        case DetachableMagazineWeaponProfileId::Gewehr43:
        case DetachableMagazineWeaponProfileId::Stg44:
        case DetachableMagazineWeaponProfileId::Mp40:
        case DetachableMagazineWeaponProfileId::Thompson:
        case DetachableMagazineWeaponProfileId::Bar:
        case DetachableMagazineWeaponProfileId::Fg42Bipod:
        case DetachableMagazineWeaponProfileId::Walther:
        case DetachableMagazineWeaponProfileId::Colt:
        case DetachableMagazineWeaponProfileId::Tokarev:
        case DetachableMagazineWeaponProfileId::Nambu:
        case DetachableMagazineWeaponProfileId::Svt40:
        case DetachableMagazineWeaponProfileId::Ppsh:
        case DetachableMagazineWeaponProfileId::Type100:
        case DetachableMagazineWeaponProfileId::Type99Lmg:
        case DetachableMagazineWeaponProfileId::Type99LmgBipod:
        case DetachableMagazineWeaponProfileId::Type100NoSound:
        case DetachableMagazineWeaponProfileId::ThompsonWet:
        case DetachableMagazineWeaponProfileId::ColtWet:
        case DetachableMagazineWeaponProfileId::BarBipod:
        case DetachableMagazineWeaponProfileId::ZombieColtDedicated:
        case DetachableMagazineWeaponProfileId::ZombieColtUpgraded:
        case DetachableMagazineWeaponProfileId::ZombieM1Carbine:
        case DetachableMagazineWeaponProfileId::ZombieM1CarbineUpgraded:
        case DetachableMagazineWeaponProfileId::ZombieGewehr43:
        case DetachableMagazineWeaponProfileId::ZombieGewehr43Upgraded:
        case DetachableMagazineWeaponProfileId::ZombieStg44:
        case DetachableMagazineWeaponProfileId::ZombieStg44Upgraded:
        case DetachableMagazineWeaponProfileId::ZombieThompson:
        case DetachableMagazineWeaponProfileId::ZombieThompsonUpgraded:
        case DetachableMagazineWeaponProfileId::ZombieMp40:
        case DetachableMagazineWeaponProfileId::ZombieMp40Upgraded:
        case DetachableMagazineWeaponProfileId::ZombieType100:
        case DetachableMagazineWeaponProfileId::ZombieType100Upgraded:
        case DetachableMagazineWeaponProfileId::ZombieBar:
        case DetachableMagazineWeaponProfileId::ZombieBarUpgraded:
        case DetachableMagazineWeaponProfileId::ZombieFg42:
        case DetachableMagazineWeaponProfileId::ZombieFg42Upgraded:
        case DetachableMagazineWeaponProfileId::ZombiePpsh:
        case DetachableMagazineWeaponProfileId::ZombiePpshUpgraded:
        case DetachableMagazineWeaponProfileId::M1Garand:
        case DetachableMagazineWeaponProfileId::M1GarandBayonet:
        case DetachableMagazineWeaponProfileId::Ptrs41:
        case DetachableMagazineWeaponProfileId::M1GarandGrenadeLauncher:
            return true;
        case DetachableMagazineWeaponProfileId::None:
        default:
            return false;
    }
}

enum class MagazineChargingHand : std::uint8_t {
    Right,
    Left,
};

struct DetachableMagazineWeaponBinding final {
    DetachableMagazineWeaponProfileId profile_id{
        DetachableMagazineWeaponProfileId::None};
    std::int32_t weapon_index{};
};

// Keep a validated immutable profile and the current map-local T4 weapon index
// in one atomic word. Unknown profile identifiers and non-positive indices
// publish the empty value instead of reaching a retail hook.
[[nodiscard]] constexpr std::uint64_t pack_detachable_magazine_weapon_binding(
    const DetachableMagazineWeaponBinding binding) noexcept {
    if (!is_known_detachable_magazine_weapon_profile_id(
            binding.profile_id) ||
        binding.weapon_index <= 0) {
        return 0;
    }
    return (static_cast<std::uint64_t>(binding.profile_id) << 32U) |
        static_cast<std::uint32_t>(binding.weapon_index);
}

[[nodiscard]] constexpr DetachableMagazineWeaponBinding
unpack_detachable_magazine_weapon_binding(
    const std::uint64_t packed) noexcept {
    return {
        .profile_id = static_cast<DetachableMagazineWeaponProfileId>(
            (packed >> 32U) & 0xFFU),
        .weapon_index = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(packed & 0xFFFFFFFFU)),
    };
}

[[nodiscard]] constexpr bool detachable_magazine_weapon_binding_matches(
    const std::uint64_t packed,
    const DetachableMagazineWeaponProfileId profile_id,
    const std::int32_t weapon_index) noexcept {
    return packed != 0 &&
        pack_detachable_magazine_weapon_binding(
            {profile_id, weapon_index}) == packed;
}

// Engine-space local pose. Translation is in T4 viewmodel units and the
// quaternion rotates from the named source frame into the rendered magazine
// frame. Keeping the two calibrations distinct lets the runtime follow the
// controller while held, then assist only orientation near the magazine well.
struct DetachableMagazineLocalPose final {
    wawvr::xr::Vec3f translation{};
    wawvr::xr::Quaternionf orientation{};
};

// A detached magazine may be authored as more than one rigid range, including
// ranges on different XSurfaces. The primary range remains in
// DetachableMagazineMeshRecipe so every accepted single-piece profile keeps
// its existing representation. These records describe only additional,
// independently audited pieces attached to the same magazine bone.
inline constexpr std::size_t kMaximumDetachableMagazineMeshPieces = 4;
inline constexpr std::size_t kMaximumDetachableMagazineChargingMeshPieces =
    2;

// A normalized view used by validation and extraction. Piece zero is the
// legacy primary recipe; later pieces come from additional_pieces.
struct DetachableMagazineMeshPieceRecipe final {
    const char* material_name{};
    std::uint8_t source_surface_index{};
    std::uint8_t source_surface_rigid_subrange_count{};
    std::uint8_t source_rigid_subrange_index{};
    std::uint16_t source_surface_vertex_count{};
    std::uint16_t source_surface_triangle_count{};
    std::uint16_t vertex_offset{};
    std::uint16_t vertex_count{};
    std::uint16_t triangle_offset{};
    std::uint16_t triangle_count{};
};

// An exact rigid submesh recipe is required because some viewmodels reuse the
// magazine material on the rest of the weapon and expose no standalone
// worldClipModel. Material matching alone would select weapon geometry too.
struct DetachableMagazineMeshRecipe final {
    std::uint8_t expected_model_bone_count{};
    std::uint8_t expected_model_surface_count{};
    std::uint8_t magazine_bone_index{};
    std::uint8_t source_surface_index{};
    std::uint8_t source_surface_rigid_subrange_count{};
    std::uint8_t source_rigid_subrange_index{};
    std::uint16_t source_surface_vertex_count{};
    std::uint16_t source_surface_triangle_count{};
    std::uint16_t vertex_offset{};
    std::uint16_t vertex_count{};
    std::uint16_t triangle_offset{};
    std::uint16_t triangle_count{};
    DetachableMagazineLocalPose bind_pose{};
    std::uint8_t additional_piece_count{};
    std::array<DetachableMagazineMeshPieceRecipe,
               kMaximumDetachableMagazineMeshPieces - 1>
        additional_pieces{};
};

// Exact rigid viewmodel piece used as a spring-return charging handle. A
// disabled recipe is the safe default for magazine-fed weapons whose slide or
// handle has not yet been independently inventoried.
struct DetachableMagazineChargingRecipe final {
    bool enabled{};
    const char* handle_bone_tag_name{};
    const char* surface_material_name{};
    MagazineChargingHand manipulating_hand{MagazineChargingHand::Right};
    // The accepted M1 deliberately seals its charging surface against a
    // misleading native fire/reload flash even outside an active gesture.
    // Other weapons default to yielding back to their native fire animation.
    bool suppress_native_pose_always{};
    std::uint8_t expected_model_bone_count{};
    std::uint8_t expected_model_surface_count{};
    std::uint8_t handle_bone_index{};
    std::uint8_t parent_bone_index{};
    std::uint8_t source_surface_index{};
    std::uint8_t source_surface_rigid_subrange_count{};
    std::uint8_t source_rigid_subrange_index{};
    std::uint16_t source_surface_vertex_count{};
    std::uint16_t source_surface_triangle_count{};
    std::uint16_t rigid_vertex_offset{};
    std::uint16_t rigid_vertex_count{};
    std::uint16_t rigid_triangle_offset{};
    std::uint16_t rigid_triangle_count{};
    std::uint8_t additional_piece_count{};
    std::array<DetachableMagazineMeshPieceRecipe,
               kMaximumDetachableMagazineChargingMeshPieces - 1>
        additional_pieces{};
    DetachableMagazineLocalPose bind_pose{};
    MagazineChargingCalibration interaction{};
    float grab_radius_units{};
    // Native idle can translate an action relative to its immutable bind.
    // Keep that offset separate so extraction still validates the real asset.
    wawvr::xr::Vec3f closed_translation_offset{};
};

// A loading hatch is not the detachable clip and must never be extracted with
// it. Its one audited rigid surface is posed privately at skin consumption.
struct DetachableMagazineFeedDoorRecipe final {
    bool enabled{};
    const char* bone_tag_name{};
    std::uint8_t bone_index{};
    std::uint8_t parent_bone_index{};
    DetachableMagazineMeshPieceRecipe piece{};
    DetachableMagazineLocalPose closed_pose{};
    DetachableMagazineLocalPose open_pose{};
};

// Immutable identity, asset recipe, and initial interaction calibration for a
// single detachable-magazine weapon. Retail weapon indices are intentionally
// excluded because they are assigned in map load order.
struct DetachableMagazineWeaponProfile final {
    DetachableMagazineWeaponProfileId id{
        DetachableMagazineWeaponProfileId::None};
    const char* diagnostic_name{};
    const char* internal_weapon_name{};
    const char* viewmodel_model_name{};
    const char* magazine_material_name{};
    const char* magazine_bone_tag_name{};
    gameplay::ReloadProfileKind reload_kind{
        gameplay::ReloadProfileKind::NativeOnly};
    std::int32_t expected_clip_size{};
    // When no worldClipModel exists, the exact magazine is extracted from the
    // validated j_clip rigid submesh instead.
    bool requires_embedded_viewmodel_magazine{};

    DetachableMagazineMeshRecipe mesh{};
    DetachableMagazineChargingRecipe charging{};
    DetachableMagazineLocalPose held_pose_from_controller{};
    DetachableMagazineLocalPose insertion_guide_pose_from_anchor{};
    float insertion_radius_units{};
    // Some internal clips are parked off-model while idle. Their render bone
    // is not a magazine well; use a separately audited, stable receiver tag.
    // Null preserves the accepted j_clip anchor for existing weapons.
    const char* insertion_anchor_bone_tag_name{};
    std::uint8_t insertion_anchor_bone_index{0xFF};
    bool hide_authored_feed_device_always{};
    DetachableMagazineFeedDoorRecipe feed_door{};
};

[[nodiscard]] constexpr std::size_t detachable_magazine_mesh_piece_count(
    const DetachableMagazineMeshRecipe& recipe) noexcept {
    return 1U + recipe.additional_piece_count;
}

[[nodiscard]] constexpr DetachableMagazineMeshPieceRecipe
detachable_magazine_mesh_piece(
    const DetachableMagazineWeaponProfile& profile,
    const std::size_t piece_index) noexcept {
    if (piece_index == 0) {
        const auto& mesh = profile.mesh;
        return {
            .material_name = profile.magazine_material_name,
            .source_surface_index = mesh.source_surface_index,
            .source_surface_rigid_subrange_count =
                mesh.source_surface_rigid_subrange_count,
            .source_rigid_subrange_index = mesh.source_rigid_subrange_index,
            .source_surface_vertex_count =
                mesh.source_surface_vertex_count,
            .source_surface_triangle_count =
                mesh.source_surface_triangle_count,
            .vertex_offset = mesh.vertex_offset,
            .vertex_count = mesh.vertex_count,
            .triangle_offset = mesh.triangle_offset,
            .triangle_count = mesh.triangle_count,
        };
    }
    if (piece_index > profile.mesh.additional_piece_count) {
        return {};
    }
    const auto& piece = profile.mesh.additional_pieces[piece_index - 1U];
    return {
        .material_name = piece.material_name,
        .source_surface_index = piece.source_surface_index,
        .source_surface_rigid_subrange_count =
            piece.source_surface_rigid_subrange_count,
        .source_rigid_subrange_index = piece.source_rigid_subrange_index,
        .source_surface_vertex_count = piece.source_surface_vertex_count,
        .source_surface_triangle_count = piece.source_surface_triangle_count,
        .vertex_offset = piece.vertex_offset,
        .vertex_count = piece.vertex_count,
        .triangle_offset = piece.triangle_offset,
        .triangle_count = piece.triangle_count,
    };
}

[[nodiscard]] constexpr std::size_t
detachable_magazine_charging_mesh_piece_count(
    const DetachableMagazineChargingRecipe& recipe) noexcept {
    return recipe.enabled ? 1U + recipe.additional_piece_count : 0U;
}

// Charging pieces use the same exact rigid-range description as detached
// magazine pieces. Piece zero is kept in the legacy fields so every accepted
// single-piece profile remains source-compatible.
[[nodiscard]] constexpr DetachableMagazineMeshPieceRecipe
detachable_magazine_charging_mesh_piece(
    const DetachableMagazineChargingRecipe& recipe,
    const std::size_t piece_index) noexcept {
    if (!recipe.enabled) {
        return {};
    }
    if (piece_index == 0) {
        return {
            .material_name = recipe.surface_material_name,
            .source_surface_index = recipe.source_surface_index,
            .source_surface_rigid_subrange_count =
                recipe.source_surface_rigid_subrange_count,
            .source_rigid_subrange_index =
                recipe.source_rigid_subrange_index,
            .source_surface_vertex_count =
                recipe.source_surface_vertex_count,
            .source_surface_triangle_count =
                recipe.source_surface_triangle_count,
            .vertex_offset = recipe.rigid_vertex_offset,
            .vertex_count = recipe.rigid_vertex_count,
            .triangle_offset = recipe.rigid_triangle_offset,
            .triangle_count = recipe.rigid_triangle_count,
        };
    }
    if (piece_index > recipe.additional_piece_count) {
        return {};
    }
    return recipe.additional_pieces[piece_index - 1U];
}

namespace detachable_magazine_detail {

[[nodiscard]] constexpr bool finite_float(const float value) noexcept {
    constexpr float maximum = (std::numeric_limits<float>::max)();
    return value == value && value >= -maximum && value <= maximum;
}

[[nodiscard]] constexpr bool nonempty_string(
    const char* const value) noexcept {
    return value != nullptr && value[0] != '\0';
}

[[nodiscard]] constexpr bool equal_strings(
    const char* left,
    const char* right) noexcept {
    if (left == nullptr || right == nullptr) {
        return left == right;
    }
    while (*left != '\0' && *right != '\0') {
        if (*left != *right) {
            return false;
        }
        ++left;
        ++right;
    }
    return *left == *right;
}

[[nodiscard]] constexpr bool finite_vector(
    const wawvr::xr::Vec3f value) noexcept {
    return finite_float(value.x) && finite_float(value.y) &&
        finite_float(value.z);
}

[[nodiscard]] constexpr bool normalized_quaternion(
    const wawvr::xr::Quaternionf value) noexcept {
    if (!finite_float(value.x) || !finite_float(value.y) ||
        !finite_float(value.z) || !finite_float(value.w)) {
        return false;
    }
    const float length_squared = value.x * value.x + value.y * value.y +
        value.z * value.z + value.w * value.w;
    constexpr float tolerance = 0.001F;
    return length_squared >= 1.0F - tolerance &&
        length_squared <= 1.0F + tolerance;
}

[[nodiscard]] constexpr bool valid_local_pose(
    const DetachableMagazineLocalPose& pose) noexcept {
    return finite_vector(pose.translation) &&
        normalized_quaternion(pose.orientation);
}

}  // namespace detachable_magazine_detail

// Structural validation is constexpr so every shipping registry entry is
// rejected at build time if identity, extraction, or interaction calibration
// becomes incomplete.
[[nodiscard]] constexpr bool validate_detachable_magazine_weapon_profile(
    const DetachableMagazineWeaponProfile& profile) noexcept {
    using namespace detachable_magazine_detail;

    if (!is_known_detachable_magazine_weapon_profile_id(profile.id) ||
        !nonempty_string(profile.diagnostic_name) ||
        !nonempty_string(profile.internal_weapon_name) ||
        !nonempty_string(profile.viewmodel_model_name) ||
        !nonempty_string(profile.magazine_material_name) ||
        !nonempty_string(profile.magazine_bone_tag_name) ||
        profile.reload_kind !=
            gameplay::ReloadProfileKind::DetachableMagazine ||
        profile.expected_clip_size <= 0 ||
        profile.expected_clip_size > 512 ||
        !profile.requires_embedded_viewmodel_magazine) {
        return false;
    }

    const DetachableMagazineMeshRecipe& mesh = profile.mesh;
    if (profile.insertion_anchor_bone_tag_name != nullptr
            ? !nonempty_string(profile.insertion_anchor_bone_tag_name) ||
                  profile.insertion_anchor_bone_index >=
                      mesh.expected_model_bone_count
            : profile.insertion_anchor_bone_index != 0xFF) {
        return false;
    }
    const auto& door = profile.feed_door;
    if (door.enabled) {
        const auto& piece = door.piece;
        if (!profile.charging.enabled ||
            mesh.additional_piece_count >= kMaximumDetachableMagazineMeshPieces ||
            profile.charging.additional_piece_count >=
                kMaximumDetachableMagazineChargingMeshPieces ||
            !nonempty_string(door.bone_tag_name) ||
            door.bone_index >= mesh.expected_model_bone_count ||
            door.parent_bone_index >= mesh.expected_model_bone_count ||
            door.bone_index == door.parent_bone_index ||
            door.bone_index == mesh.magazine_bone_index ||
            door.bone_index == profile.charging.handle_bone_index ||
            !valid_local_pose(door.closed_pose) ||
            !valid_local_pose(door.open_pose) ||
            !nonempty_string(piece.material_name) ||
            piece.source_surface_index >= mesh.expected_model_surface_count ||
            piece.source_surface_rigid_subrange_count != 1 ||
            piece.source_rigid_subrange_index != 0 ||
            piece.vertex_offset != 0 || piece.triangle_offset != 0 ||
            piece.vertex_count == 0 || piece.triangle_count == 0 ||
            piece.vertex_count != piece.source_surface_vertex_count ||
            piece.triangle_count != piece.source_surface_triangle_count) {
            return false;
        }
        for (std::size_t i = 0; i < detachable_magazine_mesh_piece_count(mesh); ++i) {
            if (piece.source_surface_index ==
                detachable_magazine_mesh_piece(profile, i).source_surface_index) {
                return false;
            }
        }
        for (std::size_t i = 0;
             i < detachable_magazine_charging_mesh_piece_count(profile.charging); ++i) {
            if (piece.source_surface_index == detachable_magazine_charging_mesh_piece(
                    profile.charging, i).source_surface_index) {
                return false;
            }
        }
    } else if (door.bone_tag_name != nullptr || door.bone_index != 0 ||
               door.parent_bone_index != 0 || door.piece.material_name != nullptr) {
        return false;
    }
    if (mesh.expected_model_bone_count == 0 ||
        mesh.expected_model_surface_count == 0 ||
        mesh.magazine_bone_index >= mesh.expected_model_bone_count ||
        mesh.additional_piece_count >=
            kMaximumDetachableMagazineMeshPieces) {
        return false;
    }

    const std::size_t piece_count =
        detachable_magazine_mesh_piece_count(mesh);
    for (std::size_t piece_index = 0; piece_index < piece_count;
         ++piece_index) {
        const auto piece =
            detachable_magazine_mesh_piece(profile, piece_index);
        if (!nonempty_string(piece.material_name) ||
            piece.source_surface_index >= mesh.expected_model_surface_count ||
            piece.source_surface_rigid_subrange_count == 0 ||
            piece.source_rigid_subrange_index >=
                piece.source_surface_rigid_subrange_count ||
            piece.source_surface_vertex_count == 0 ||
            piece.source_surface_triangle_count == 0 ||
            piece.vertex_count == 0 || piece.triangle_count == 0) {
            return false;
        }
        const std::uint32_t vertex_end =
            static_cast<std::uint32_t>(piece.vertex_offset) +
            piece.vertex_count;
        const std::uint32_t triangle_end =
            static_cast<std::uint32_t>(piece.triangle_offset) +
            piece.triangle_count;
        if (vertex_end > piece.source_surface_vertex_count ||
            triangle_end > piece.source_surface_triangle_count) {
            return false;
        }
        for (std::size_t earlier_index = 0;
             earlier_index < piece_index; ++earlier_index) {
            const auto earlier =
                detachable_magazine_mesh_piece(profile, earlier_index);
            if (piece.source_surface_index == earlier.source_surface_index &&
                piece.source_rigid_subrange_index ==
                    earlier.source_rigid_subrange_index) {
                return false;
            }
        }
    }
    for (std::size_t piece_index = mesh.additional_piece_count;
         piece_index < mesh.additional_pieces.size(); ++piece_index) {
        const auto& unused = mesh.additional_pieces[piece_index];
        if (unused.material_name != nullptr ||
            unused.source_surface_index != 0 ||
            unused.source_surface_rigid_subrange_count != 0 ||
            unused.source_rigid_subrange_index != 0 ||
            unused.source_surface_vertex_count != 0 ||
            unused.source_surface_triangle_count != 0 ||
            unused.vertex_offset != 0 || unused.vertex_count != 0 ||
            unused.triangle_offset != 0 || unused.triangle_count != 0) {
            return false;
        }
    }

    if (!valid_local_pose(mesh.bind_pose) ||
        !valid_local_pose(profile.held_pose_from_controller) ||
        !valid_local_pose(profile.insertion_guide_pose_from_anchor) ||
        !finite_float(profile.insertion_radius_units) ||
        profile.insertion_radius_units <= 0.0F ||
        profile.insertion_radius_units > 64.0F) {
        return false;
    }

    const DetachableMagazineChargingRecipe& charging = profile.charging;
    if (!finite_vector(charging.closed_translation_offset)) return false;
    if (!charging.enabled) {
        if (charging.handle_bone_tag_name != nullptr ||
            charging.surface_material_name != nullptr ||
            charging.manipulating_hand != MagazineChargingHand::Right ||
            charging.suppress_native_pose_always ||
            charging.expected_model_bone_count != 0 ||
            charging.expected_model_surface_count != 0 ||
            charging.handle_bone_index != 0 ||
            charging.parent_bone_index != 0 ||
            charging.source_surface_index != 0 ||
            charging.source_surface_rigid_subrange_count != 0 ||
            charging.source_rigid_subrange_index != 0 ||
            charging.source_surface_vertex_count != 0 ||
            charging.source_surface_triangle_count != 0 ||
            charging.rigid_vertex_offset != 0 ||
            charging.rigid_vertex_count != 0 ||
            charging.rigid_triangle_offset != 0 ||
            charging.rigid_triangle_count != 0 ||
            charging.additional_piece_count != 0 ||
            charging.bind_pose.translation.x != 0.0F ||
            charging.bind_pose.translation.y != 0.0F ||
            charging.bind_pose.translation.z != 0.0F ||
            charging.bind_pose.orientation.x != 0.0F ||
            charging.bind_pose.orientation.y != 0.0F ||
            charging.bind_pose.orientation.z != 0.0F ||
            charging.bind_pose.orientation.w != 1.0F ||
            charging.interaction.travel_units != 0.0F ||
            charging.interaction.locked_open_offset_units != 0.0F ||
            charging.interaction.open_threshold != 0.0F ||
            charging.interaction.spring_return_seconds != 0.0F ||
            charging.interaction.trigger_engage != 0.0F ||
            charging.interaction.trigger_release != 0.0F ||
            charging.interaction.control_policy !=
                MagazineChargingControlPolicy::ManualPullRelease ||
            charging.interaction.completion !=
                MagazineChargingCompletion::SpringClosed ||
            charging.interaction.return_sample_count != 0 ||
            charging.grab_radius_units != 0.0F ||
            charging.closed_translation_offset.x != 0.0F ||
            charging.closed_translation_offset.y != 0.0F ||
            charging.closed_translation_offset.z != 0.0F) {
            return false;
        }
        for (const float sample : charging.interaction.return_samples) {
            if (sample != 0.0F) {
                return false;
            }
        }
        for (const auto& unused : charging.additional_pieces) {
            if (unused.material_name != nullptr ||
                unused.source_surface_index != 0 ||
                unused.source_surface_rigid_subrange_count != 0 ||
                unused.source_rigid_subrange_index != 0 ||
                unused.source_surface_vertex_count != 0 ||
                unused.source_surface_triangle_count != 0 ||
                unused.vertex_offset != 0 || unused.vertex_count != 0 ||
                unused.triangle_offset != 0 ||
                unused.triangle_count != 0) {
                return false;
            }
        }
        return true;
    }

    const MagazineChargingControlPolicy control_policy =
        charging.interaction.control_policy;
    if (control_policy !=
            MagazineChargingControlPolicy::ManualPullRelease &&
        control_policy != MagazineChargingControlPolicy::EnBlocAutomatic) {
        return false;
    }
    const bool automatic_en_bloc =
        control_policy == MagazineChargingControlPolicy::EnBlocAutomatic;

    if (!nonempty_string(charging.handle_bone_tag_name) ||
        !nonempty_string(charging.surface_material_name) ||
        (charging.manipulating_hand != MagazineChargingHand::Right &&
         charging.manipulating_hand != MagazineChargingHand::Left) ||
        charging.expected_model_bone_count !=
            mesh.expected_model_bone_count ||
        charging.expected_model_surface_count !=
            mesh.expected_model_surface_count ||
        charging.handle_bone_index >= charging.expected_model_bone_count ||
        charging.parent_bone_index >= charging.expected_model_bone_count ||
        charging.handle_bone_index == charging.parent_bone_index ||
        charging.handle_bone_index == mesh.magazine_bone_index ||
        charging.additional_piece_count >=
            kMaximumDetachableMagazineChargingMeshPieces ||
        !valid_local_pose(charging.bind_pose) ||
        !finite_float(charging.interaction.travel_units) ||
        charging.interaction.travel_units <= 0.0F ||
        !finite_float(charging.interaction.locked_open_offset_units) ||
        charging.interaction.locked_open_offset_units < 0.0F ||
        (automatic_en_bloc
             ? charging.interaction.locked_open_offset_units !=
                   charging.interaction.travel_units
             : charging.interaction.locked_open_offset_units >=
                   charging.interaction.travel_units) ||
        !finite_float(charging.interaction.open_threshold) ||
        charging.interaction.open_threshold <= 0.0F ||
        charging.interaction.open_threshold > 1.0F ||
        !finite_float(charging.interaction.trigger_release) ||
        !finite_float(charging.interaction.trigger_engage) ||
        (automatic_en_bloc
             ? charging.interaction.trigger_release != 0.0F ||
                   charging.interaction.trigger_engage != 0.0F
             : charging.interaction.trigger_release < 0.0F ||
                   charging.interaction.trigger_release >=
                       charging.interaction.trigger_engage ||
                   charging.interaction.trigger_engage > 1.0F) ||
        !finite_float(charging.grab_radius_units) ||
        (automatic_en_bloc ? charging.grab_radius_units != 0.0F
                           : charging.grab_radius_units <= 0.0F) ||
        charging.grab_radius_units > 64.0F) {
        return false;
    }

    const std::size_t charging_piece_count =
        detachable_magazine_charging_mesh_piece_count(charging);
    if (charging_piece_count == 0 ||
        charging_piece_count >
            kMaximumDetachableMagazineChargingMeshPieces) {
        return false;
    }
    for (std::size_t piece_index = 0;
         piece_index < charging_piece_count; ++piece_index) {
        const auto piece = detachable_magazine_charging_mesh_piece(
            charging, piece_index);
        if (!nonempty_string(piece.material_name) ||
            piece.source_surface_index >=
                charging.expected_model_surface_count ||
            piece.source_surface_rigid_subrange_count == 0 ||
            piece.source_rigid_subrange_index >=
                piece.source_surface_rigid_subrange_count ||
            piece.source_surface_vertex_count == 0 ||
            piece.source_surface_triangle_count == 0 ||
            piece.vertex_count == 0 || piece.triangle_count == 0) {
            return false;
        }
        const std::uint32_t vertex_end =
            static_cast<std::uint32_t>(piece.vertex_offset) +
            piece.vertex_count;
        const std::uint32_t triangle_end =
            static_cast<std::uint32_t>(piece.triangle_offset) +
            piece.triangle_count;
        if (vertex_end > piece.source_surface_vertex_count ||
            triangle_end > piece.source_surface_triangle_count) {
            return false;
        }
        for (std::size_t earlier_index = 0;
             earlier_index < piece_index; ++earlier_index) {
            const auto earlier =
                detachable_magazine_charging_mesh_piece(
                    charging, earlier_index);
            if (piece.source_surface_index ==
                    earlier.source_surface_index &&
                piece.source_rigid_subrange_index ==
                    earlier.source_rigid_subrange_index) {
                return false;
            }
        }
        for (std::size_t magazine_piece_index = 0;
             magazine_piece_index < piece_count;
             ++magazine_piece_index) {
            const auto magazine_piece = detachable_magazine_mesh_piece(
                profile, magazine_piece_index);
            if (piece.source_surface_index ==
                    magazine_piece.source_surface_index &&
                piece.source_rigid_subrange_index ==
                    magazine_piece.source_rigid_subrange_index) {
                return false;
            }
        }
    }
    for (std::size_t piece_index = charging.additional_piece_count;
         piece_index < charging.additional_pieces.size(); ++piece_index) {
        const auto& unused = charging.additional_pieces[piece_index];
        if (unused.material_name != nullptr ||
            unused.source_surface_index != 0 ||
            unused.source_surface_rigid_subrange_count != 0 ||
            unused.source_rigid_subrange_index != 0 ||
            unused.source_surface_vertex_count != 0 ||
            unused.source_surface_triangle_count != 0 ||
            unused.vertex_offset != 0 || unused.vertex_count != 0 ||
            unused.triangle_offset != 0 || unused.triangle_count != 0) {
            return false;
        }
    }

    const auto& interaction = charging.interaction;
    if (interaction.completion == MagazineChargingCompletion::LatchOpen) {
        if (interaction.spring_return_seconds != 0.0F ||
            interaction.return_sample_count != 0) {
            return false;
        }
        for (const float sample : interaction.return_samples) {
            if (sample != 0.0F) {
                return false;
            }
        }
        return true;
    }
    if (interaction.completion != MagazineChargingCompletion::SpringClosed ||
        !finite_float(interaction.spring_return_seconds) ||
        interaction.spring_return_seconds <= 0.0F ||
        interaction.spring_return_seconds > 2.0F ||
        interaction.return_sample_count < 2 ||
        interaction.return_sample_count >
            kMaximumMagazineChargingReturnSamples ||
        interaction.return_samples[0] != 1.0F ||
        interaction.return_samples[
            interaction.return_sample_count - 1U] != 0.0F) {
        return false;
    }
    float previous_sample = 1.0F;
    for (std::size_t index = 0;
         index < interaction.return_samples.size(); ++index) {
        const float sample = interaction.return_samples[index];
        if (index < interaction.return_sample_count) {
            if (!finite_float(sample) || sample < 0.0F || sample > 1.0F ||
                sample > previous_sample) {
                return false;
            }
            previous_sample = sample;
        } else if (sample != 0.0F) {
            return false;
        }
    }

    return true;
}

inline constexpr DetachableMagazineWeaponProfile
    kZombieColtDetachableMagazineWeaponProfile{
        .id = DetachableMagazineWeaponProfileId::ZombieColt,
        .diagnostic_name = "Nacht zombie Colt M1911",
        .internal_weapon_name = "zombie_colt",
        .viewmodel_model_name = "viewmodel_usa_colt45_pistol",
        .magazine_material_name = "mc/mtl_weapon_colt45",
        .magazine_bone_tag_name = "j_clip",
        .reload_kind = gameplay::ReloadProfileKind::DetachableMagazine,
        .expected_clip_size = 8,
        .requires_embedded_viewmodel_magazine = true,
        .mesh = {
            .expected_model_bone_count = 7,
            .expected_model_surface_count = 5,
            .magazine_bone_index = 2,
            .source_surface_index = 0,
            .source_surface_rigid_subrange_count = 3,
            .source_rigid_subrange_index = 1,
            .source_surface_vertex_count = 252,
            .source_surface_triangle_count = 202,
            .vertex_offset = 87,
            .vertex_count = 75,
            .triangle_offset = 75,
            .triangle_count = 55,
            .bind_pose = {
                .translation = {-1.340145F, 0.071632F, -1.237288F},
                .orientation = {0.0F, 0.0F, 0.0F, 1.0F},
            },
        },
        .charging = {
            .enabled = true,
            .handle_bone_tag_name = "j_bolt",
            .surface_material_name = "mc/mtl_weapon_colt45",
            .manipulating_hand = MagazineChargingHand::Left,
            .suppress_native_pose_always = false,
            .expected_model_bone_count = 7,
            .expected_model_surface_count = 5,
            .handle_bone_index = 1,
            .parent_bone_index = 0,
            .source_surface_index = 2,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 1326,
            .source_surface_triangle_count = 1050,
            .rigid_vertex_count = 1326,
            .rigid_triangle_count = 1050,
            .bind_pose = {
                .translation = {-1.952201F, 0.067739F, 1.572215F},
                .orientation = {0.0F, -0.010224F, 0.0F, 0.999939F},
            },
            .interaction = {
                .travel_units = 1.50F,
                .locked_open_offset_units = 1.35F,
                .open_threshold = 0.98F,
                .spring_return_seconds = 0.10F,
                .trigger_engage = 0.65F,
                .trigger_release = 0.35F,
                .completion = MagazineChargingCompletion::SpringClosed,
                .return_sample_count = 4,
                .return_samples = {1.0F, 0.72F, 0.24F, 0.0F},
            },
            .grab_radius_units = 8.0F,
        },
        // Start from the already accepted tracked feed-device hand alignment;
        // the dedicated field keeps later Colt polish isolated from rifles.
        .held_pose_from_controller = {
            .translation = {3.25F, 0.0F, 1.25F},
            .orientation = {0.0F, 0.0F, 0.0F, 1.0F},
        },
        // tag_clip/j_clip is the insertion anchor and has an identity bind
        // rotation in the audited retail model.
        .insertion_guide_pose_from_anchor = {
            .translation = {0.0F, 0.0F, 0.0F},
            .orientation = {0.0F, 0.0F, 0.0F, 1.0F},
        },
        .insertion_radius_units = 9.0F,
    };

// Vendetta retail ptrs41 inventory, 2026-09-04: internal five-round clip,
// not a per-shot bolt-action. j_clip is parked at Y=65.547646 and comprises
// the metal carrier plus five cartridges on a second material. The receiver
// anchor is independent of that parked/animated bone. Charging remains the
// accepted empty-reload-only, either-free-hand pull/release interaction.
inline constexpr DetachableMagazineWeaponProfile
    kPtrs41DetachableMagazineWeaponProfile{
        .id = DetachableMagazineWeaponProfileId::Ptrs41,
        .diagnostic_name = "PTRS-41",
        .internal_weapon_name = "ptrs41",
        .viewmodel_model_name = "viewmodel_mp_ptrs41",
        .magazine_material_name = "mc/mtl_weapon_ptrs",
        .magazine_bone_tag_name = "j_clip",
        .reload_kind = gameplay::ReloadProfileKind::DetachableMagazine,
        .expected_clip_size = 5,
        .requires_embedded_viewmodel_magazine = true,
        .mesh = {
            .expected_model_bone_count = 8,
            .expected_model_surface_count = 10,
            .magazine_bone_index = 2,
            .source_surface_index = 5,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 108,
            .source_surface_triangle_count = 84,
            .vertex_count = 108,
            .triangle_count = 84,
            .bind_pose = {
                .translation = {2.093333F, 65.547646F, 8.117425F},
                .orientation = {0.0F, 0.0F, 0.0F, 1.0F},
            },
            .additional_piece_count = 1,
            .additional_pieces = {{
                {.material_name = "mc/mtl_ammo_belt",
                 .source_surface_index = 8,
                 .source_surface_rigid_subrange_count = 1,
                 .source_surface_vertex_count = 395,
                 .source_surface_triangle_count = 320,
                 .vertex_count = 395,
                 .triangle_count = 320},
            }},
        },
        .charging = {
            .enabled = true,
            .handle_bone_tag_name = "j_bolt",
            .surface_material_name = "mc/mtl_weapon_ptrs",
            .expected_model_bone_count = 8,
            .expected_model_surface_count = 10,
            .handle_bone_index = 1,
            .parent_bone_index = 0,
            .source_surface_index = 4,
            .source_surface_rigid_subrange_count = 1,
            .source_surface_vertex_count = 215,
            .source_surface_triangle_count = 220,
            .rigid_vertex_count = 215,
            .rigid_triangle_count = 220,
            .additional_piece_count = 1,
            .additional_pieces = {{
                {.material_name = "mc/mtl_ammo_belt",
                 .source_surface_index = 7,
                 .source_surface_rigid_subrange_count = 1,
                 .source_surface_vertex_count = 79,
                 .source_surface_triangle_count = 64,
                 .vertex_count = 79,
                 .triangle_count = 64},
            }},
            .bind_pose = {
                .translation = {11.887321F, -1.767607F, 1.002453F},
                .orientation = {0.0F, 0.0F, 0.0F, 1.0F},
            },
            .interaction = {
                // Native loaded/empty idle and reload_empty keys157→159.
                // Empty PTRS locks fully rearward: the user's deliberate
                // trigger grip/release releases it after inserting the clip.
                .travel_units = 9.090631F,
                .locked_open_offset_units = 9.089901F,
                .open_threshold = 0.95F,
                .spring_return_seconds = 2.0F / 30.0F,
                .trigger_engage = 0.65F,
                .trigger_release = 0.35F,
                .completion = MagazineChargingCompletion::SpringClosed,
                .return_sample_count = 2,
                .return_samples = {1.0F, 0.0F},
            },
            .grab_radius_units = 10.0F,
            .closed_translation_offset = {0.329079F, 0.0F, 0.0F},
        },
        // Rebased mesh bounds center is (5.670, -0.218, -0.283).
        // Place the carrier in the palm, cartridges extending slightly ahead.
        .held_pose_from_controller = {
            .translation = {-3.5F, 0.218F, 0.283F},
        },
        .insertion_guide_pose_from_anchor = {
            // Last visible seated clip at native reload_empty frame81,
            // immediately before the authored clip is moved offscreen.
            .translation = {7.703720F, 0.069681F, -0.430640F},
            .orientation = {0.025605F, 0.030305F, -0.012909F, 0.999115F},
        },
        .insertion_radius_units = 8.0F,
        .insertion_anchor_bone_tag_name = "j_gun",
        .insertion_anchor_bone_index = 0,
        .hide_authored_feed_device_always = true,
        .feed_door = {
            .enabled = true,
            .bone_tag_name = "j_clip_release",
            .bone_index = 3,
            .parent_bone_index = 0,
            .piece = {
                .material_name = "mc/mtl_weapon_ptrs",
                .source_surface_index = 6,
                .source_surface_rigid_subrange_count = 1,
                .source_surface_vertex_count = 146,
                .source_surface_triangle_count = 172,
                .vertex_count = 146,
                .triangle_count = 172,
            },
            .closed_pose = {
                .translation = {16.118538F, -0.004485F, -1.891364F},
            },
            // Stable-open native reload_empty frame51. Keep the front hinge
            // fixed while the rear edge hangs down to expose the clip well.
            .open_pose = {
                .translation = {16.118538F, -0.004485F, -1.891364F},
                .orientation = {0.0F, -0.538377F, 0.0F, 0.842708F},
            },
        },
    };

static_assert(validate_detachable_magazine_weapon_profile(
    kPtrs41DetachableMagazineWeaponProfile));

inline constexpr DetachableMagazineWeaponProfile
    kM1CarbineDetachableMagazineWeaponProfile{
        .id = DetachableMagazineWeaponProfileId::M1Carbine,
        .diagnostic_name = "Nacht M1A1 Carbine",
        .internal_weapon_name = "m1carbine",
        .viewmodel_model_name = "viewmodel_usa_m1carbine_rifle",
        .magazine_material_name = "mc/mtl_weapon_carbine",
        .magazine_bone_tag_name = "j_clip",
        .reload_kind = gameplay::ReloadProfileKind::DetachableMagazine,
        .expected_clip_size = 15,
        .requires_embedded_viewmodel_magazine = true,
        .mesh = {
            .expected_model_bone_count = 5,
            .expected_model_surface_count = 4,
            .magazine_bone_index = 2,
            .source_surface_index = 0,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 74,
            .source_surface_triangle_count = 58,
            .vertex_offset = 0,
            .vertex_count = 74,
            .triangle_offset = 0,
            .triangle_count = 58,
            .bind_pose = {
                .translation = {-2.436279F, 0.012866F, -0.551744F},
                .orientation = {0.0F, 0.0F, 0.0F, 1.0F},
            },
        },
        .charging = {
            .enabled = true,
            .handle_bone_tag_name = "j_bolt",
            .surface_material_name = "mc/mtl_weapon_carbine",
            .manipulating_hand = MagazineChargingHand::Right,
            .suppress_native_pose_always = true,
            .expected_model_bone_count = 5,
            .expected_model_surface_count = 4,
            .handle_bone_index = 1,
            .parent_bone_index = 0,
            .source_surface_index = 2,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 328,
            .source_surface_triangle_count = 316,
            .rigid_vertex_count = 328,
            .rigid_triangle_count = 315,
            .bind_pose = {
                .translation = {-1.946244F, -1.782576F, 0.924210F},
                .orientation = {0.0F, -0.010224F, 0.0F, 0.999939F},
            },
            .interaction = {
                .travel_units = 2.961060F,
                .locked_open_offset_units = 2.384359F,
                .open_threshold = 0.98F,
                .spring_return_seconds = 1.0F / 6.0F,
                .trigger_engage = 0.65F,
                .trigger_release = 0.35F,
                .completion = MagazineChargingCompletion::SpringClosed,
                .return_sample_count = 6,
                .return_samples = {
                    1.0F,
                    228.0F / 255.0F,
                    165.0F / 255.0F,
                    90.0F / 255.0F,
                    27.0F / 255.0F,
                    0.0F,
                },
            },
            .grab_radius_units = 10.0F,
        },
        // Begin with the accepted detachable-magazine controller alignment.
        // The profile keeps later rifle-specific physical polish isolated.
        .held_pose_from_controller = {
            .translation = {3.25F, 0.0F, 1.25F},
            .orientation = {0.0F, 0.0F, 0.0F, 1.0F},
        },
        .insertion_guide_pose_from_anchor = {
            .translation = {0.0F, 0.0F, 0.0F},
            .orientation = {0.0F, 0.0F, 0.0F, 1.0F},
        },
        .insertion_radius_units = 9.0F,
    };

// The M1 Garand uses the detachable-feed-device transaction for its top-loaded
// eight-round en-bloc clip. Unlike a box-magazine weapon, inserting a loaded
// clip releases the already-open action automatically; there is no controller
// charging gesture after insertion. Clip and operating-rod mesh recipes are
// exact retail inventories. Action travel is an intentionally provisional
// simulator seed until physical calibration can replace it without widening
// either audited rigid range.
inline constexpr DetachableMagazineWeaponProfile
    kM1GarandDetachableMagazineWeaponProfile{
        .id = DetachableMagazineWeaponProfileId::M1Garand,
        .diagnostic_name = "M1 Garand",
        .internal_weapon_name = "m1garand",
        .viewmodel_model_name = "viewmodel_usa_m1garand_rifle",
        .magazine_material_name = "mc/mtl_brass_shells",
        .magazine_bone_tag_name = "j_clip",
        .reload_kind = gameplay::ReloadProfileKind::DetachableMagazine,
        .expected_clip_size = 8,
        .requires_embedded_viewmodel_magazine = true,
        .mesh = {
            .expected_model_bone_count = 7,
            .expected_model_surface_count = 6,
            .magazine_bone_index = 2,
            .source_surface_index = 4,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 204,
            .source_surface_triangle_count = 130,
            .vertex_offset = 0,
            .vertex_count = 204,
            .triangle_offset = 0,
            .triangle_count = 130,
            .bind_pose = {
                .translation = {-1.888771F, -0.006035F, -7.283907F},
                .orientation = {0.0F, 0.0F, 0.0F, 1.0F},
            },
        },
        .charging = {
            .enabled = true,
            .handle_bone_tag_name = "j_bolt",
            .surface_material_name = "mc/mtl_weapon_m1garand",
            .manipulating_hand = MagazineChargingHand::Right,
            // Keep the native semiautomatic cycling animation except while the
            // manual-reload action owns this exact operating-rod surface.
            .suppress_native_pose_always = false,
            .expected_model_bone_count = 7,
            .expected_model_surface_count = 6,
            .handle_bone_index = 1,
            .parent_bone_index = 0,
            .source_surface_index = 2,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 534,
            .source_surface_triangle_count = 486,
            .rigid_vertex_offset = 0,
            .rigid_vertex_count = 534,
            .rigid_triangle_offset = 0,
            .rigid_triangle_count = 485,
            .bind_pose = {
                .translation = {-0.273143F, -1.810324F, 1.101824F},
                .orientation = {0.0F, -0.010224F, 0.0F, 0.999939F},
            },
            .interaction = {
                .control_policy =
                    MagazineChargingControlPolicy::EnBlocAutomatic,
                .travel_units = 2.961060F,
                .locked_open_offset_units = 2.961060F,
                .open_threshold = 0.98F,
                .spring_return_seconds = 1.0F / 6.0F,
                .trigger_engage = 0.0F,
                .trigger_release = 0.0F,
                .completion = MagazineChargingCompletion::SpringClosed,
                .return_sample_count = 6,
                .return_samples = {
                    1.0F,
                    228.0F / 255.0F,
                    165.0F / 255.0F,
                    90.0F / 255.0F,
                    27.0F / 255.0F,
                    0.0F,
                },
            },
            .grab_radius_units = 0.0F,
        },
        .held_pose_from_controller = {
            .translation = {3.25F, 0.0F, 1.25F},
            .orientation = {0.0F, 0.0F, 0.0F, 1.0F},
        },
        .insertion_guide_pose_from_anchor = {
            .translation = {0.0F, 0.0F, 0.0F},
            .orientation = {0.0F, 0.0F, 0.0F, 1.0F},
        },
        .insertion_radius_units = 9.0F,
    };

// The campaign bayonet variant retains the base Garand's feed-device and
// operating-rod contract under a distinct WeaponDef and viewmodel identity.
// Keep a separate exact profile so the runtime cannot accidentally admit the
// GL, wet, sailor, or multiplayer variants by a shared-name prefix.
inline constexpr DetachableMagazineWeaponProfile
    kM1GarandBayonetDetachableMagazineWeaponProfile = []() constexpr {
        auto profile = kM1GarandDetachableMagazineWeaponProfile;
        profile.id = DetachableMagazineWeaponProfileId::M1GarandBayonet;
        profile.diagnostic_name = "M1 Garand bayonet";
        profile.internal_weapon_name = "m1garand_bayonet";
        profile.viewmodel_model_name =
            "viewmodel_usa_m1garand_rifle_bayonet";
        // The bayonet is a new rigid root/surface inserted ahead of the base
        // Garand geometry.  The clip and operating rod retain their exact base
        // bones and meshes, shifted forward by one surface slot.
        profile.mesh.expected_model_bone_count = 8;
        profile.mesh.expected_model_surface_count = 7;
        profile.mesh.source_surface_index = 5;
        profile.charging.expected_model_bone_count = 8;
        profile.charging.expected_model_surface_count = 7;
        profile.charging.source_surface_index = 3;
        return profile;
    }();

// Hard Landing's rifle-grenade Garand is a separate WeaponDef and viewmodel,
// even while the rifle-mode reload animations and eight-round en-bloc feed
// remain the base Garand contract. The retail launcher model appends its mount
// surface after the rifle geometry; j_clip and j_bolt therefore retain the
// base Garand's exact surface slots. Keep an exact identity/topology gate
// rather than letting a shared m1garand prefix admit either weapon mode.
inline constexpr DetachableMagazineWeaponProfile
    kM1GarandGrenadeLauncherDetachableMagazineWeaponProfile = []() constexpr {
        auto profile = kM1GarandDetachableMagazineWeaponProfile;
        profile.id =
            DetachableMagazineWeaponProfileId::M1GarandGrenadeLauncher;
        profile.diagnostic_name = "M1 Garand rifle-grenade launcher";
        profile.internal_weapon_name = "m1garand_gl";
        profile.viewmodel_model_name =
            "viewmodel_usa_m1garand_rifle_grenade_mount";
        profile.mesh.expected_model_bone_count = 8;
        profile.mesh.expected_model_surface_count = 7;
        profile.charging.expected_model_bone_count = 8;
        profile.charging.expected_model_surface_count = 7;
        return profile;
    }();

inline constexpr DetachableMagazineLocalPose
    kStandardDetachableMagazineHeldPose{
        .translation = {3.25F, 0.0F, 1.25F},
        .orientation = {0.0F, 0.0F, 0.0F, 1.0F},
    };
inline constexpr DetachableMagazineLocalPose
    kIdentityDetachableMagazineInsertionPose{
        .translation = {0.0F, 0.0F, 0.0F},
        .orientation = {0.0F, 0.0F, 0.0F, 1.0F},
    };

// Campaign Colt and Nacht zombie_colt share one exact retail viewmodel asset.
// Identity remains separate so the map-local WeaponDef gate cannot alias them.
inline constexpr DetachableMagazineWeaponProfile
    kColtDetachableMagazineWeaponProfile = []() constexpr {
        auto profile = kZombieColtDetachableMagazineWeaponProfile;
        profile.id = DetachableMagazineWeaponProfileId::Colt;
        profile.diagnostic_name = "Colt M1911";
        profile.internal_weapon_name = "colt";
        return profile;
    }();

inline constexpr DetachableMagazineWeaponProfile
    kGewehr43DetachableMagazineWeaponProfile{
        .id = DetachableMagazineWeaponProfileId::Gewehr43,
        .diagnostic_name = "Nacht Gewehr 43",
        .internal_weapon_name = "gewehr43",
        .viewmodel_model_name = "viewmodel_ger_g43_rifle",
        .magazine_material_name = "mc/mtl_weapon_g43",
        .magazine_bone_tag_name = "j_clip",
        .reload_kind = gameplay::ReloadProfileKind::DetachableMagazine,
        .expected_clip_size = 10,
        .requires_embedded_viewmodel_magazine = true,
        .mesh = {
            .expected_model_bone_count = 7,
            .expected_model_surface_count = 6,
            .magazine_bone_index = 2,
            .source_surface_index = 1,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 477,
            .source_surface_triangle_count = 398,
            .vertex_offset = 0,
            .vertex_count = 477,
            .triangle_offset = 0,
            .triangle_count = 398,
            .bind_pose = {
                .translation = {-1.020676F, 0.002369F, -0.272053F},
                .orientation = {0.0F, 0.0F, 0.0F, 1.0F},
            },
        },
        .charging = {
            .enabled = true,
            .handle_bone_tag_name = "j_bolt",
            .surface_material_name = "mc/mtl_weapon_g43",
            .manipulating_hand = MagazineChargingHand::Right,
            .suppress_native_pose_always = false,
            .expected_model_bone_count = 7,
            .expected_model_surface_count = 6,
            .handle_bone_index = 1,
            .parent_bone_index = 0,
            .source_surface_index = 3,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 780,
            .source_surface_triangle_count = 696,
            .rigid_vertex_count = 780,
            .rigid_triangle_count = 695,
            .bind_pose = {
                .translation = {-2.577788F, 0.788577F, 1.976399F},
                .orientation = {0.0F, -0.010224F, 0.0F, 0.999939F},
            },
            .interaction = {
                .travel_units = 2.75F,
                .locked_open_offset_units = 2.45F,
                .open_threshold = 0.98F,
                .spring_return_seconds = 0.15F,
                .trigger_engage = 0.65F,
                .trigger_release = 0.35F,
                .completion = MagazineChargingCompletion::SpringClosed,
                .return_sample_count = 5,
                .return_samples = {1.0F, 0.82F, 0.48F, 0.14F, 0.0F},
            },
            .grab_radius_units = 10.0F,
        },
        .held_pose_from_controller = kStandardDetachableMagazineHeldPose,
        .insertion_guide_pose_from_anchor =
            kIdentityDetachableMagazineInsertionPose,
        .insertion_radius_units = 9.0F,
    };

inline constexpr DetachableMagazineWeaponProfile
    kStg44DetachableMagazineWeaponProfile{
        .id = DetachableMagazineWeaponProfileId::Stg44,
        .diagnostic_name = "Nacht StG 44",
        .internal_weapon_name = "stg44",
        .viewmodel_model_name = "viewmodel_ger_mp44_lmg",
        .magazine_material_name = "mc/mtl_weapon_mp44",
        .magazine_bone_tag_name = "j_clip",
        .reload_kind = gameplay::ReloadProfileKind::DetachableMagazine,
        .expected_clip_size = 30,
        .requires_embedded_viewmodel_magazine = true,
        .mesh = {
            .expected_model_bone_count = 6,
            .expected_model_surface_count = 5,
            .magazine_bone_index = 2,
            .source_surface_index = 1,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 202,
            .source_surface_triangle_count = 152,
            .vertex_offset = 0,
            .vertex_count = 202,
            .triangle_offset = 0,
            .triangle_count = 151,
            .bind_pose = {
                .translation = {6.639460F, 0.015012F, 1.835601F},
                .orientation = {0.0F, 0.0F, 0.0F, 1.0F},
            },
        },
        .charging = {
            .enabled = true,
            .handle_bone_tag_name = "j_bolt",
            .surface_material_name = "mc/mtl_weapon_mp44",
            .manipulating_hand = MagazineChargingHand::Right,
            .suppress_native_pose_always = false,
            .expected_model_bone_count = 6,
            .expected_model_surface_count = 5,
            .handle_bone_index = 1,
            .parent_bone_index = 0,
            .source_surface_index = 2,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 304,
            .source_surface_triangle_count = 322,
            .rigid_vertex_count = 304,
            .rigid_triangle_count = 321,
            .bind_pose = {
                .translation = {4.690453F, 0.134476F, 3.205951F},
                .orientation = {0.0F, -0.010224F, 0.0F, 0.999939F},
            },
            .interaction = {
                .travel_units = 3.10F,
                .locked_open_offset_units = 0.0F,
                .open_threshold = 0.98F,
                .spring_return_seconds = 1.0F / 6.0F,
                .trigger_engage = 0.65F,
                .trigger_release = 0.35F,
                .completion = MagazineChargingCompletion::SpringClosed,
                .return_sample_count = 5,
                .return_samples = {1.0F, 0.82F, 0.48F, 0.14F, 0.0F},
            },
            .grab_radius_units = 10.0F,
        },
        .held_pose_from_controller = kStandardDetachableMagazineHeldPose,
        .insertion_guide_pose_from_anchor =
            kIdentityDetachableMagazineInsertionPose,
        .insertion_radius_units = 9.0F,
    };

inline constexpr DetachableMagazineWeaponProfile
    kMp40DetachableMagazineWeaponProfile{
        .id = DetachableMagazineWeaponProfileId::Mp40,
        .diagnostic_name = "Nacht MP40",
        .internal_weapon_name = "mp40",
        .viewmodel_model_name = "viewmodel_ger_mp40_smg",
        .magazine_material_name = "mc/mtl_weapon_mp40",
        .magazine_bone_tag_name = "j_clip",
        .reload_kind = gameplay::ReloadProfileKind::DetachableMagazine,
        .expected_clip_size = 32,
        .requires_embedded_viewmodel_magazine = true,
        .mesh = {
            .expected_model_bone_count = 5,
            .expected_model_surface_count = 3,
            .magazine_bone_index = 2,
            .source_surface_index = 0,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 36,
            .source_surface_triangle_count = 18,
            .vertex_offset = 0,
            .vertex_count = 36,
            .triangle_offset = 0,
            .triangle_count = 18,
            .bind_pose = {
                .translation = {4.207038F, 0.408818F, -0.275403F},
                .orientation = {0.0F, 0.0F, 0.0F, 1.0F},
            },
        },
        .charging = {
            .enabled = true,
            .handle_bone_tag_name = "j_bolt",
            .surface_material_name = "mc/mtl_weapon_mp40",
            .manipulating_hand = MagazineChargingHand::Right,
            .suppress_native_pose_always = false,
            .expected_model_bone_count = 5,
            .expected_model_surface_count = 3,
            .handle_bone_index = 1,
            .parent_bone_index = 0,
            .source_surface_index = 1,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 186,
            .source_surface_triangle_count = 182,
            .rigid_vertex_count = 186,
            .rigid_triangle_count = 182,
            .bind_pose = {
                .translation = {-2.532358F, 1.252526F, 1.000512F},
                .orientation = {0.0F, -0.010224F, 0.0F, 0.999939F},
            },
            .interaction = {
                .travel_units = 3.20F,
                .locked_open_offset_units = 0.0F,
                .open_threshold = 0.98F,
                .spring_return_seconds = 0.0F,
                .trigger_engage = 0.65F,
                .trigger_release = 0.35F,
                .completion = MagazineChargingCompletion::LatchOpen,
            },
            .grab_radius_units = 10.0F,
        },
        .held_pose_from_controller = kStandardDetachableMagazineHeldPose,
        .insertion_guide_pose_from_anchor =
            kIdentityDetachableMagazineInsertionPose,
        .insertion_radius_units = 9.0F,
    };

inline constexpr DetachableMagazineWeaponProfile
    kThompsonDetachableMagazineWeaponProfile{
        .id = DetachableMagazineWeaponProfileId::Thompson,
        .diagnostic_name = "Nacht Thompson",
        .internal_weapon_name = "thompson",
        .viewmodel_model_name = "viewmodel_usa_thompson_smg",
        .magazine_material_name = "mc/mtl_usa_smg_thompson",
        .magazine_bone_tag_name = "j_clip",
        .reload_kind = gameplay::ReloadProfileKind::DetachableMagazine,
        .expected_clip_size = 20,
        .requires_embedded_viewmodel_magazine = true,
        .mesh = {
            .expected_model_bone_count = 5,
            .expected_model_surface_count = 4,
            .magazine_bone_index = 2,
            .source_surface_index = 2,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 722,
            .source_surface_triangle_count = 414,
            .vertex_offset = 0,
            .vertex_count = 722,
            .triangle_offset = 0,
            .triangle_count = 414,
            .bind_pose = {
                .translation = {3.463230F, 0.126403F, -0.144167F},
                .orientation = {0.0F, 0.0F, 0.0F, 1.0F},
            },
        },
        .charging = {
            .enabled = true,
            .handle_bone_tag_name = "j_bolt",
            .surface_material_name = "mc/mtl_usa_smg_thompson",
            .manipulating_hand = MagazineChargingHand::Right,
            .suppress_native_pose_always = false,
            .expected_model_bone_count = 5,
            .expected_model_surface_count = 4,
            .handle_bone_index = 1,
            .parent_bone_index = 0,
            .source_surface_index = 1,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 219,
            .source_surface_triangle_count = 172,
            .rigid_vertex_count = 219,
            .rigid_triangle_count = 172,
            .bind_pose = {
                .translation = {1.304120F, -0.912230F, 2.115713F},
                .orientation = {0.0F, -0.010224F, 0.0F, 0.999939F},
            },
            .interaction = {
                .travel_units = 3.00F,
                .locked_open_offset_units = 0.0F,
                .open_threshold = 0.98F,
                .spring_return_seconds = 0.0F,
                .trigger_engage = 0.65F,
                .trigger_release = 0.35F,
                .completion = MagazineChargingCompletion::LatchOpen,
            },
            .grab_radius_units = 10.0F,
        },
        .held_pose_from_controller = kStandardDetachableMagazineHeldPose,
        .insertion_guide_pose_from_anchor =
            kIdentityDetachableMagazineInsertionPose,
        .insertion_radius_units = 9.0F,
    };

inline constexpr DetachableMagazineWeaponProfile
    kBarDetachableMagazineWeaponProfile{
        .id = DetachableMagazineWeaponProfileId::Bar,
        .diagnostic_name = "Nacht BAR",
        .internal_weapon_name = "bar",
        .viewmodel_model_name = "viewmodel_usa_bar_lmg",
        .magazine_material_name = "mc/mtl_weapon_bar_wood",
        .magazine_bone_tag_name = "j_clip",
        .reload_kind = gameplay::ReloadProfileKind::DetachableMagazine,
        .expected_clip_size = 20,
        .requires_embedded_viewmodel_magazine = true,
        .mesh = {
            .expected_model_bone_count = 8,
            .expected_model_surface_count = 7,
            .magazine_bone_index = 2,
            .source_surface_index = 5,
            .source_surface_rigid_subrange_count = 2,
            .source_rigid_subrange_index = 1,
            .source_surface_vertex_count = 48,
            .source_surface_triangle_count = 28,
            .vertex_offset = 24,
            .vertex_count = 24,
            .triangle_offset = 15,
            .triangle_count = 12,
            .bind_pose = {
                .translation = {-0.218617F, -0.242085F, -0.715188F},
                .orientation = {0.0F, 0.0F, 0.0F, 1.0F},
            },
        },
        .charging = {
            .enabled = true,
            .handle_bone_tag_name = "j_bolt",
            .surface_material_name = "mc/mtl_weapon_bar",
            .manipulating_hand = MagazineChargingHand::Right,
            .suppress_native_pose_always = false,
            .expected_model_bone_count = 8,
            .expected_model_surface_count = 7,
            .handle_bone_index = 1,
            .parent_bone_index = 0,
            .source_surface_index = 3,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 536,
            .source_surface_triangle_count = 574,
            .rigid_vertex_count = 536,
            .rigid_triangle_count = 573,
            .bind_pose = {
                .translation = {-2.240462F, 1.697357F, 0.228652F},
                .orientation = {0.0F, -0.010224F, 0.0F, 0.999939F},
            },
            .interaction = {
                .travel_units = 3.40F,
                .locked_open_offset_units = 0.0F,
                .open_threshold = 0.98F,
                .spring_return_seconds = 0.0F,
                .trigger_engage = 0.65F,
                .trigger_release = 0.35F,
                .completion = MagazineChargingCompletion::LatchOpen,
            },
            .grab_radius_units = 10.0F,
        },
        .held_pose_from_controller = kStandardDetachableMagazineHeldPose,
        .insertion_guide_pose_from_anchor =
            kIdentityDetachableMagazineInsertionPose,
        .insertion_radius_units = 9.0F,
    };

inline constexpr DetachableMagazineWeaponProfile
    kFg42BipodDetachableMagazineWeaponProfile{
        .id = DetachableMagazineWeaponProfileId::Fg42Bipod,
        .diagnostic_name = "Nacht FG42 bipod",
        .internal_weapon_name = "fg42_bipod",
        .viewmodel_model_name = "viewmodel_ger_fg42_bipod_lmg",
        .magazine_material_name = "mc/mtl_weapon_fg42",
        .magazine_bone_tag_name = "j_clip",
        .reload_kind = gameplay::ReloadProfileKind::DetachableMagazine,
        .expected_clip_size = 32,
        .requires_embedded_viewmodel_magazine = true,
        .mesh = {
            .expected_model_bone_count = 9,
            .expected_model_surface_count = 7,
            .magazine_bone_index = 2,
            .source_surface_index = 4,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 358,
            .source_surface_triangle_count = 230,
            .vertex_offset = 0,
            .vertex_count = 358,
            .triangle_offset = 0,
            .triangle_count = 230,
            .bind_pose = {
                .translation = {0.760868F, 0.981267F, 2.555799F},
                .orientation = {0.0F, 0.0F, 0.0F, 1.0F},
            },
        },
        .charging = {
            .enabled = true,
            .handle_bone_tag_name = "j_bolt",
            .surface_material_name = "mc/mtl_weapon_fg42",
            .manipulating_hand = MagazineChargingHand::Right,
            .suppress_native_pose_always = false,
            .expected_model_bone_count = 9,
            .expected_model_surface_count = 7,
            .handle_bone_index = 1,
            .parent_bone_index = 0,
            .source_surface_index = 0,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 123,
            .source_surface_triangle_count = 90,
            .rigid_vertex_count = 123,
            .rigid_triangle_count = 90,
            .bind_pose = {
                .translation = {7.663996F, -1.964798F, 1.898179F},
                .orientation = {0.0F, -0.010224F, 0.0F, 0.999939F},
            },
            .interaction = {
                .travel_units = 3.10F,
                .locked_open_offset_units = 0.0F,
                .open_threshold = 0.98F,
                .spring_return_seconds = 0.0F,
                .trigger_engage = 0.65F,
                .trigger_release = 0.35F,
                .completion = MagazineChargingCompletion::LatchOpen,
            },
            .grab_radius_units = 10.0F,
        },
        .held_pose_from_controller = kStandardDetachableMagazineHeldPose,
        .insertion_guide_pose_from_anchor =
            kIdentityDetachableMagazineInsertionPose,
        .insertion_radius_units = 9.0F,
    };

inline constexpr DetachableMagazineWeaponProfile
    kWaltherDetachableMagazineWeaponProfile{
        .id = DetachableMagazineWeaponProfileId::Walther,
        .diagnostic_name = "Walther P38",
        .internal_weapon_name = "walther",
        .viewmodel_model_name = "viewmodel_ger_walther_pistol",
        .magazine_material_name = "mc/mtl_weapon_walther",
        .magazine_bone_tag_name = "j_clip",
        .reload_kind = gameplay::ReloadProfileKind::DetachableMagazine,
        .expected_clip_size = 8,
        .requires_embedded_viewmodel_magazine = true,
        .mesh = {
            .expected_model_bone_count = 7,
            .expected_model_surface_count = 4,
            .magazine_bone_index = 2,
            .source_surface_index = 0,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 159,
            .source_surface_triangle_count = 144,
            .vertex_offset = 0,
            .vertex_count = 159,
            .triangle_offset = 0,
            .triangle_count = 143,
            .bind_pose = {
                .translation = {-1.907487F, 0.051301F, -1.632552F},
                .orientation = {0.0F, 0.0F, 0.707114F, 0.707114F},
            },
        },
        .charging = {
            .enabled = true,
            .handle_bone_tag_name = "j_bolt",
            .surface_material_name = "mc/mtl_weapon_walther",
            .manipulating_hand = MagazineChargingHand::Left,
            .suppress_native_pose_always = false,
            .expected_model_bone_count = 7,
            .expected_model_surface_count = 4,
            .handle_bone_index = 1,
            .parent_bone_index = 0,
            .source_surface_index = 2,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 1305,
            .source_surface_triangle_count = 1242,
            .rigid_vertex_count = 1305,
            .rigid_triangle_count = 1242,
            .bind_pose = {
                .translation = {-2.722442F, 0.095315F, 1.603951F},
                .orientation = {0.0F, -0.010224F, 0.0F, 0.999939F},
            },
            .interaction = {
                .travel_units = 1.55F,
                .locked_open_offset_units = 1.40F,
                .open_threshold = 0.98F,
                .spring_return_seconds = 0.10F,
                .trigger_engage = 0.65F,
                .trigger_release = 0.35F,
                .completion = MagazineChargingCompletion::SpringClosed,
                .return_sample_count = 4,
                .return_samples = {1.0F, 0.72F, 0.24F, 0.0F},
            },
            .grab_radius_units = 8.0F,
        },
        .held_pose_from_controller = kStandardDetachableMagazineHeldPose,
        .insertion_guide_pose_from_anchor =
            kIdentityDetachableMagazineInsertionPose,
        .insertion_radius_units = 9.0F,
    };

inline constexpr DetachableMagazineWeaponProfile
    kTokarevDetachableMagazineWeaponProfile{
        .id = DetachableMagazineWeaponProfileId::Tokarev,
        .diagnostic_name = "Tokarev TT-33",
        .internal_weapon_name = "tokarev",
        .viewmodel_model_name = "viewmodel_rus_tt30_pistol",
        .magazine_material_name = "mc/mtl_tokarevtt30_pistol",
        .magazine_bone_tag_name = "j_clip",
        .reload_kind = gameplay::ReloadProfileKind::DetachableMagazine,
        .expected_clip_size = 8,
        .requires_embedded_viewmodel_magazine = true,
        .mesh = {
            .expected_model_bone_count = 7,
            .expected_model_surface_count = 5,
            .magazine_bone_index = 2,
            .source_surface_index = 1,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 184,
            .source_surface_triangle_count = 152,
            .vertex_offset = 0,
            .vertex_count = 184,
            .triangle_offset = 0,
            .triangle_count = 151,
            .bind_pose = {
                .translation = {-1.581781F, 0.071632F, -1.741824F},
                .orientation = {0.0F, 0.0F, 0.0F, 1.0F},
            },
        },
        .charging = {
            .enabled = true,
            .handle_bone_tag_name = "j_bolt",
            .surface_material_name = "mc/mtl_tokarevtt30_pistol",
            .manipulating_hand = MagazineChargingHand::Left,
            .suppress_native_pose_always = false,
            .expected_model_bone_count = 7,
            .expected_model_surface_count = 5,
            .handle_bone_index = 1,
            .parent_bone_index = 0,
            .source_surface_index = 3,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 1220,
            .source_surface_triangle_count = 1196,
            .rigid_vertex_count = 1220,
            .rigid_triangle_count = 1195,
            .bind_pose = {
                .translation = {-1.952201F, 0.067739F, 1.572215F},
                .orientation = {0.0F, -0.010224F, 0.0F, 0.999939F},
            },
            .interaction = {
                .travel_units = 1.50F,
                .locked_open_offset_units = 1.35F,
                .open_threshold = 0.98F,
                .spring_return_seconds = 0.10F,
                .trigger_engage = 0.65F,
                .trigger_release = 0.35F,
                .completion = MagazineChargingCompletion::SpringClosed,
                .return_sample_count = 4,
                .return_samples = {1.0F, 0.72F, 0.24F, 0.0F},
            },
            .grab_radius_units = 8.0F,
        },
        .held_pose_from_controller = kStandardDetachableMagazineHeldPose,
        .insertion_guide_pose_from_anchor =
            kIdentityDetachableMagazineInsertionPose,
        .insertion_radius_units = 9.0F,
    };

inline constexpr DetachableMagazineWeaponProfile
    kNambuDetachableMagazineWeaponProfile{
        .id = DetachableMagazineWeaponProfileId::Nambu,
        .diagnostic_name = "Nambu pistol",
        .internal_weapon_name = "nambu",
        .viewmodel_model_name = "viewmodel_jap_nambu_pistol",
        .magazine_material_name = "mc/mtl_weapon_nambu",
        .magazine_bone_tag_name = "j_clip",
        .reload_kind = gameplay::ReloadProfileKind::DetachableMagazine,
        .expected_clip_size = 8,
        .requires_embedded_viewmodel_magazine = true,
        .mesh = {
            .expected_model_bone_count = 5,
            .expected_model_surface_count = 4,
            .magazine_bone_index = 2,
            .source_surface_index = 0,
            .source_surface_rigid_subrange_count = 2,
            .source_rigid_subrange_index = 1,
            .source_surface_vertex_count = 206,
            .source_surface_triangle_count = 160,
            .vertex_offset = 107,
            .vertex_count = 99,
            .triangle_offset = 89,
            .triangle_count = 71,
            .bind_pose = {
                .translation = {-1.907486F, 0.051300F, -1.632552F},
                .orientation = {0.0F, 0.0F, 0.707114F, 0.707114F},
            },
            .additional_piece_count = 1,
            .additional_pieces = {{
                {
                    .material_name = "mc/mtl_weapon_nambu",
                    .source_surface_index = 2,
                    .source_surface_rigid_subrange_count = 1,
                    .source_rigid_subrange_index = 0,
                    .source_surface_vertex_count = 276,
                    .source_surface_triangle_count = 244,
                    .vertex_offset = 0,
                    .vertex_count = 276,
                    .triangle_offset = 0,
                    .triangle_count = 244,
                },
            }},
        },
        .charging = {
            .enabled = true,
            .handle_bone_tag_name = "j_bolt",
            .surface_material_name = "mc/mtl_weapon_nambu",
            .manipulating_hand = MagazineChargingHand::Left,
            .suppress_native_pose_always = false,
            .expected_model_bone_count = 5,
            .expected_model_surface_count = 4,
            .handle_bone_index = 1,
            .parent_bone_index = 0,
            .source_surface_index = 3,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 753,
            .source_surface_triangle_count = 718,
            .rigid_vertex_count = 753,
            .rigid_triangle_count = 718,
            .bind_pose = {
                .translation = {-4.175309F, 0.014211F, 1.154153F},
                .orientation = {
                    0.007233F, -0.007233F, 0.707083F, 0.707053F},
            },
            .interaction = {
                .travel_units = 1.50F,
                .locked_open_offset_units = 1.35F,
                .open_threshold = 0.98F,
                .spring_return_seconds = 0.10F,
                .trigger_engage = 0.65F,
                .trigger_release = 0.35F,
                .completion = MagazineChargingCompletion::SpringClosed,
                .return_sample_count = 4,
                .return_samples = {1.0F, 0.72F, 0.24F, 0.0F},
            },
            .grab_radius_units = 8.0F,
        },
        .held_pose_from_controller = kStandardDetachableMagazineHeldPose,
        .insertion_guide_pose_from_anchor =
            kIdentityDetachableMagazineInsertionPose,
        .insertion_radius_units = 9.0F,
    };

inline constexpr DetachableMagazineWeaponProfile
    kSvt40DetachableMagazineWeaponProfile{
        .id = DetachableMagazineWeaponProfileId::Svt40,
        .diagnostic_name = "SVT-40",
        .internal_weapon_name = "svt40",
        .viewmodel_model_name = "viewmodel_rus_svt40_rifle",
        .magazine_material_name = "mc/mtl_weapon_svt40_clip",
        .magazine_bone_tag_name = "j_clip",
        .reload_kind = gameplay::ReloadProfileKind::DetachableMagazine,
        .expected_clip_size = 10,
        .requires_embedded_viewmodel_magazine = true,
        .mesh = {
            .expected_model_bone_count = 6,
            .expected_model_surface_count = 5,
            .magazine_bone_index = 2,
            .source_surface_index = 4,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 152,
            .source_surface_triangle_count = 88,
            .vertex_offset = 0,
            .vertex_count = 152,
            .triangle_offset = 0,
            .triangle_count = 88,
            .bind_pose = {
                .translation = {-2.917000F, 0.103077F, -0.779568F},
                .orientation = {0.0F, 0.0F, 0.0F, 1.0F},
            },
        },
        .charging = {
            .enabled = true,
            .handle_bone_tag_name = "j_bolt",
            .surface_material_name = "mc/mtl_weapon_svt40",
            .manipulating_hand = MagazineChargingHand::Right,
            .suppress_native_pose_always = false,
            .expected_model_bone_count = 6,
            .expected_model_surface_count = 5,
            .handle_bone_index = 1,
            .parent_bone_index = 0,
            .source_surface_index = 1,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 215,
            .source_surface_triangle_count = 270,
            .rigid_vertex_count = 215,
            .rigid_triangle_count = 270,
            .bind_pose = {
                .translation = {-2.575804F, 0.016471F, 1.261918F},
                .orientation = {0.0F, -0.010224F, 0.0F, 0.999939F},
            },
            .interaction = {
                .travel_units = 2.75F,
                .locked_open_offset_units = 2.45F,
                .open_threshold = 0.98F,
                .spring_return_seconds = 0.15F,
                .trigger_engage = 0.65F,
                .trigger_release = 0.35F,
                .completion = MagazineChargingCompletion::SpringClosed,
                .return_sample_count = 5,
                .return_samples = {1.0F, 0.82F, 0.48F, 0.14F, 0.0F},
            },
            .grab_radius_units = 10.0F,
        },
        .held_pose_from_controller = kStandardDetachableMagazineHeldPose,
        .insertion_guide_pose_from_anchor =
            kIdentityDetachableMagazineInsertionPose,
        .insertion_radius_units = 9.0F,
    };

inline constexpr DetachableMagazineWeaponProfile
    kPpshDetachableMagazineWeaponProfile{
        .id = DetachableMagazineWeaponProfileId::Ppsh,
        .diagnostic_name = "PPSh-41",
        .internal_weapon_name = "ppsh",
        .viewmodel_model_name = "viewmodel_rus_ppsh_smg",
        .magazine_material_name = "mc/mtl_rus_smg_ppsh41",
        .magazine_bone_tag_name = "j_clip",
        .reload_kind = gameplay::ReloadProfileKind::DetachableMagazine,
        .expected_clip_size = 71,
        .requires_embedded_viewmodel_magazine = true,
        .mesh = {
            .expected_model_bone_count = 7,
            .expected_model_surface_count = 6,
            .magazine_bone_index = 2,
            .source_surface_index = 3,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 292,
            .source_surface_triangle_count = 272,
            .vertex_offset = 0,
            .vertex_count = 292,
            .triangle_offset = 0,
            .triangle_count = 272,
            .bind_pose = {
                .translation = {2.162864F, 0.001309F, 0.558384F},
                .orientation = {0.0F, 0.0F, 0.0F, 1.0F},
            },
        },
        .charging = {
            .enabled = true,
            .handle_bone_tag_name = "j_bolt",
            .surface_material_name = "mc/mtl_rus_smg_ppsh41",
            .manipulating_hand = MagazineChargingHand::Right,
            .suppress_native_pose_always = false,
            .expected_model_bone_count = 7,
            .expected_model_surface_count = 6,
            .handle_bone_index = 1,
            .parent_bone_index = 0,
            .source_surface_index = 1,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 196,
            .source_surface_triangle_count = 140,
            .rigid_vertex_count = 196,
            .rigid_triangle_count = 140,
            .bind_pose = {
                .translation = {0.145243F, -0.907277F, 1.807249F},
                .orientation = {0.0F, -0.010224F, 0.0F, 0.999939F},
            },
            .interaction = {
                .travel_units = 3.00F,
                .locked_open_offset_units = 0.0F,
                .open_threshold = 0.98F,
                .spring_return_seconds = 0.0F,
                .trigger_engage = 0.65F,
                .trigger_release = 0.35F,
                .completion = MagazineChargingCompletion::LatchOpen,
            },
            .grab_radius_units = 10.0F,
        },
        .held_pose_from_controller = kStandardDetachableMagazineHeldPose,
        .insertion_guide_pose_from_anchor =
            kIdentityDetachableMagazineInsertionPose,
        .insertion_radius_units = 9.0F,
    };

inline constexpr DetachableMagazineWeaponProfile
    kType100DetachableMagazineWeaponProfile{
        .id = DetachableMagazineWeaponProfileId::Type100,
        .diagnostic_name = "Type 100 SMG",
        .internal_weapon_name = "type100_smg",
        .viewmodel_model_name = "viewmodel_jap_type100_smg",
        .magazine_material_name = "mc/mtl_weapon_type100_metal",
        .magazine_bone_tag_name = "j_clip",
        .reload_kind = gameplay::ReloadProfileKind::DetachableMagazine,
        .expected_clip_size = 30,
        .requires_embedded_viewmodel_magazine = true,
        .mesh = {
            .expected_model_bone_count = 6,
            .expected_model_surface_count = 5,
            .magazine_bone_index = 2,
            .source_surface_index = 3,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 346,
            .source_surface_triangle_count = 374,
            .vertex_offset = 0,
            .vertex_count = 346,
            .triangle_offset = 0,
            .triangle_count = 374,
            .bind_pose = {
                .translation = {2.351090F, 2.122774F, 1.516471F},
                .orientation = {0.0F, 0.0F, 0.0F, 1.0F},
            },
        },
        .charging = {
            .enabled = true,
            .handle_bone_tag_name = "j_bolt",
            .surface_material_name = "mc/mtl_weapon_type100_metal",
            .manipulating_hand = MagazineChargingHand::Right,
            .suppress_native_pose_always = false,
            .expected_model_bone_count = 6,
            .expected_model_surface_count = 5,
            .handle_bone_index = 1,
            .parent_bone_index = 0,
            .source_surface_index = 2,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 314,
            .source_surface_triangle_count = 298,
            .rigid_vertex_count = 314,
            .rigid_triangle_count = 298,
            .bind_pose = {
                .translation = {-1.465501F, -0.620103F, 1.750709F},
                .orientation = {0.0F, -0.010224F, 0.0F, 0.999939F},
            },
            .interaction = {
                .travel_units = 3.00F,
                .locked_open_offset_units = 0.0F,
                .open_threshold = 0.98F,
                .spring_return_seconds = 0.0F,
                .trigger_engage = 0.65F,
                .trigger_release = 0.35F,
                .completion = MagazineChargingCompletion::LatchOpen,
            },
            .grab_radius_units = 10.0F,
        },
        .held_pose_from_controller = kStandardDetachableMagazineHeldPose,
        .insertion_guide_pose_from_anchor =
            kIdentityDetachableMagazineInsertionPose,
        .insertion_radius_units = 9.0F,
    };

inline constexpr DetachableMagazineWeaponProfile
    kType99LmgDetachableMagazineWeaponProfile{
        .id = DetachableMagazineWeaponProfileId::Type99Lmg,
        .diagnostic_name = "Type 99 LMG",
        .internal_weapon_name = "type99_lmg",
        .viewmodel_model_name = "viewmodel_jap_type99_lmg",
        .magazine_material_name = "mc/mtl_weapon_type99_lmg",
        .magazine_bone_tag_name = "j_clip",
        .reload_kind = gameplay::ReloadProfileKind::DetachableMagazine,
        .expected_clip_size = 32,
        .requires_embedded_viewmodel_magazine = true,
        .mesh = {
            .expected_model_bone_count = 8,
            .expected_model_surface_count = 5,
            .magazine_bone_index = 2,
            .source_surface_index = 1,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 243,
            .source_surface_triangle_count = 292,
            .vertex_offset = 0,
            .vertex_count = 243,
            .triangle_offset = 0,
            .triangle_count = 292,
            .bind_pose = {
                .translation = {1.559482F, -0.047803F, 1.436923F},
                .orientation = {0.0F, 0.0F, 0.0F, 1.0F},
            },
        },
        .charging = {
            .enabled = true,
            .handle_bone_tag_name = "j_bolt",
            .surface_material_name = "mc/mtl_weapon_type99_lmg",
            .manipulating_hand = MagazineChargingHand::Right,
            .suppress_native_pose_always = false,
            .expected_model_bone_count = 8,
            .expected_model_surface_count = 5,
            .handle_bone_index = 1,
            .parent_bone_index = 0,
            .source_surface_index = 2,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 138,
            .source_surface_triangle_count = 102,
            .rigid_vertex_count = 138,
            .rigid_triangle_count = 102,
            .bind_pose = {
                .translation = {-7.428697F, -0.894531F, -0.065446F},
                .orientation = {0.0F, -0.010224F, 0.0F, 0.999939F},
            },
            .interaction = {
                .travel_units = 3.40F,
                .locked_open_offset_units = 0.0F,
                .open_threshold = 0.98F,
                .spring_return_seconds = 0.0F,
                .trigger_engage = 0.65F,
                .trigger_release = 0.35F,
                .completion = MagazineChargingCompletion::LatchOpen,
            },
            .grab_radius_units = 10.0F,
        },
        .held_pose_from_controller = kStandardDetachableMagazineHeldPose,
        .insertion_guide_pose_from_anchor =
            kIdentityDetachableMagazineInsertionPose,
        .insertion_radius_units = 9.0F,
    };

// The bipod WeaponDef has a distinct retail viewmodel topology, while its
// magazine and charging-handle pieces are identical to the base Type 99.
inline constexpr DetachableMagazineWeaponProfile
    kType99LmgBipodDetachableMagazineWeaponProfile = []() constexpr {
        auto profile = kType99LmgDetachableMagazineWeaponProfile;
        profile.id = DetachableMagazineWeaponProfileId::Type99LmgBipod;
        profile.diagnostic_name = "Type 99 LMG bipod";
        profile.internal_weapon_name = "type99_lmg_bipod";
        profile.viewmodel_model_name = "viewmodel_jap_type99_bipod_lmg";
        profile.mesh.expected_model_bone_count = 10;
        profile.mesh.expected_model_surface_count = 7;
        profile.charging.expected_model_bone_count = 10;
        profile.charging.expected_model_surface_count = 7;
        return profile;
    }();

// The no-sound campaign WeaponDef reuses the exact Type 100 viewmodel asset.
// Keep a separate identity so lookup never aliases map-local weapon indices.
inline constexpr DetachableMagazineWeaponProfile
    kType100NoSoundDetachableMagazineWeaponProfile = []() constexpr {
        auto profile = kType100DetachableMagazineWeaponProfile;
        profile.id = DetachableMagazineWeaponProfileId::Type100NoSound;
        profile.diagnostic_name = "Type 100 SMG no-sound variant";
        profile.internal_weapon_name = "type100_smg_nosound";
        return profile;
    }();

// Oki2 packages wet-material viewmodels with distinct immutable identities.
// Their audited rigid topology and binds match the corresponding dry weapons,
// so retain the accepted interaction calibration while matching every asset
// name and material exactly.
inline constexpr DetachableMagazineWeaponProfile
    kThompsonWetDetachableMagazineWeaponProfile = []() constexpr {
        auto profile = kThompsonDetachableMagazineWeaponProfile;
        profile.id = DetachableMagazineWeaponProfileId::ThompsonWet;
        profile.diagnostic_name = "Wet Thompson";
        profile.internal_weapon_name = "thompson_wet";
        profile.viewmodel_model_name = "viewmodel_usa_thompson_smg_wet";
        profile.magazine_material_name = "mc/mtl_usa_smg_thompson_wet";
        profile.charging.surface_material_name =
            "mc/mtl_usa_smg_thompson_wet";
        return profile;
    }();

inline constexpr DetachableMagazineWeaponProfile
    kColtWetDetachableMagazineWeaponProfile = []() constexpr {
        auto profile = kColtDetachableMagazineWeaponProfile;
        profile.id = DetachableMagazineWeaponProfileId::ColtWet;
        profile.diagnostic_name = "Wet Colt M1911";
        profile.internal_weapon_name = "colt_wet";
        profile.viewmodel_model_name = "viewmodel_usa_colt45_pistol_wet";
        profile.magazine_material_name = "mc/mtl_weapon_colt45_wet";
        profile.charging.surface_material_name = "mc/mtl_weapon_colt45_wet";
        return profile;
    }();

// The Verruckt BAR bipod model adds deployment bones/surfaces, but its
// j_clip and j_bolt pieces are field-for-field identical to the base BAR.
inline constexpr DetachableMagazineWeaponProfile
    kBarBipodDetachableMagazineWeaponProfile = []() constexpr {
        auto profile = kBarDetachableMagazineWeaponProfile;
        profile.id = DetachableMagazineWeaponProfileId::BarBipod;
        profile.diagnostic_name = "BAR bipod";
        profile.internal_weapon_name = "bar_bipod";
        profile.viewmodel_model_name = "viewmodel_usa_bar_bipod_lmg";
        return profile;
    }();

// Shi No Numa and Der Riese package dedicated Zombie viewmodels, plus
// upgraded variants, under distinct immutable WeaponDef/model pairs. Their
// audited bones preserve the accepted family interaction calibration, while
// every mesh/material/topology delta remains exact and fail-closed.
inline constexpr DetachableMagazineWeaponProfile
    kZombieColtDedicatedDetachableMagazineWeaponProfile = []() constexpr {
        auto profile = kZombieColtDetachableMagazineWeaponProfile;
        profile.id =
            DetachableMagazineWeaponProfileId::ZombieColtDedicated;
        profile.diagnostic_name = "Zombie Colt M1911";
        profile.viewmodel_model_name = "viewmodel_zombie_colt45_pistol";
        return profile;
    }();

inline constexpr DetachableMagazineWeaponProfile
    kZombieColtUpgradedDetachableMagazineWeaponProfile = []() constexpr {
        auto profile =
            kZombieColtDedicatedDetachableMagazineWeaponProfile;
        profile.id = DetachableMagazineWeaponProfileId::ZombieColtUpgraded;
        profile.diagnostic_name = "Upgraded zombie Colt M1911";
        profile.internal_weapon_name = "zombie_colt_upgraded";
        profile.viewmodel_model_name =
            "viewmodel_zombie_colt45_pistol_up";
        profile.magazine_material_name =
            "mc/mtl_weapon_colt45_zombie_up";
        profile.expected_clip_size = 6;
        profile.mesh.expected_model_surface_count = 6;
        profile.charging.surface_material_name =
            "mc/mtl_weapon_colt45_zombie_up";
        profile.charging.expected_model_surface_count = 6;
        profile.charging.additional_piece_count = 1;
        profile.charging.additional_pieces[0] = {
            .material_name = "mc/mtl_silver_etching",
            .source_surface_index = 5,
            .source_surface_rigid_subrange_count = 3,
            .source_rigid_subrange_index = 1,
            .source_surface_vertex_count = 332,
            .source_surface_triangle_count = 286,
            .vertex_offset = 60,
            .vertex_count = 168,
            .triangle_offset = 57,
            .triangle_count = 136,
        };
        return profile;
    }();

inline constexpr DetachableMagazineWeaponProfile
    kZombieM1CarbineDetachableMagazineWeaponProfile = []() constexpr {
        auto profile = kM1CarbineDetachableMagazineWeaponProfile;
        profile.id = DetachableMagazineWeaponProfileId::ZombieM1Carbine;
        profile.diagnostic_name = "Zombie M1A1 Carbine";
        profile.internal_weapon_name = "zombie_m1carbine";
        profile.viewmodel_model_name = "viewmodel_zombie_m1carbine_rifle";
        profile.magazine_material_name = "mc/mtl_weapon_mp_carbine";
        profile.charging.surface_material_name =
            "mc/mtl_weapon_mp_carbine";
        return profile;
    }();

inline constexpr DetachableMagazineWeaponProfile
    kZombieM1CarbineUpgradedDetachableMagazineWeaponProfile =
        []() constexpr {
        auto profile = kZombieM1CarbineDetachableMagazineWeaponProfile;
        profile.id =
            DetachableMagazineWeaponProfileId::ZombieM1CarbineUpgraded;
        profile.diagnostic_name = "Upgraded zombie M1A1 Carbine";
        profile.internal_weapon_name = "zombie_m1carbine_upgraded";
        profile.viewmodel_model_name =
            "viewmodel_zombie_m1carbine_rifle_up";
        profile.magazine_material_name = "mc/mtl_weapon_carbine_gold";
        profile.mesh.expected_model_surface_count = 6;
        profile.mesh.additional_piece_count = 1;
        profile.mesh.additional_pieces[0] = {
            .material_name = "mc/mtl_silver_etching",
            .source_surface_index = 4,
            .source_surface_rigid_subrange_count = 3,
            .source_rigid_subrange_index = 2,
            .source_surface_vertex_count = 204,
            .source_surface_triangle_count = 184,
            .vertex_offset = 192,
            .vertex_count = 12,
            .triangle_offset = 177,
            .triangle_count = 6,
        };
        profile.charging.surface_material_name =
            "mc/mtl_weapon_carbine_gold";
        profile.charging.expected_model_surface_count = 6;
        profile.charging.additional_piece_count = 1;
        profile.charging.additional_pieces[0] = {
            .material_name = "mc/mtl_silver_etching",
            .source_surface_index = 4,
            .source_surface_rigid_subrange_count = 3,
            .source_rigid_subrange_index = 1,
            .source_surface_vertex_count = 204,
            .source_surface_triangle_count = 184,
            .vertex_offset = 121,
            .vertex_count = 71,
            .triangle_offset = 120,
            .triangle_count = 57,
        };
        return profile;
    }();

inline constexpr DetachableMagazineWeaponProfile
    kZombieGewehr43DetachableMagazineWeaponProfile = []() constexpr {
        auto profile = kGewehr43DetachableMagazineWeaponProfile;
        profile.id = DetachableMagazineWeaponProfileId::ZombieGewehr43;
        profile.diagnostic_name = "Zombie Gewehr 43";
        profile.internal_weapon_name = "zombie_gewehr43";
        profile.viewmodel_model_name = "viewmodel_zombie_g43_rifle";
        profile.magazine_material_name = "mc/mtl_weapon_mp_g43";
        profile.mesh.source_surface_index = 2;
        profile.charging.surface_material_name = "mc/mtl_weapon_mp_g43";
        profile.charging.source_surface_index = 4;
        return profile;
    }();

inline constexpr DetachableMagazineWeaponProfile
    kZombieGewehr43UpgradedDetachableMagazineWeaponProfile =
        []() constexpr {
        auto profile = kZombieGewehr43DetachableMagazineWeaponProfile;
        profile.id =
            DetachableMagazineWeaponProfileId::ZombieGewehr43Upgraded;
        profile.diagnostic_name = "Upgraded zombie Gewehr 43";
        profile.internal_weapon_name = "zombie_gewehr43_upgraded";
        profile.viewmodel_model_name = "viewmodel_zombie_g43_rifle_up";
        profile.magazine_material_name = "mc/mtl_weapon_g43_gold";
        profile.expected_clip_size = 12;
        profile.mesh.expected_model_surface_count = 10;
        profile.mesh.additional_piece_count = 1;
        profile.mesh.additional_pieces[0] = {
            .material_name = "mc/mtl_silver_etching",
            .source_surface_index = 7,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 102,
            .source_surface_triangle_count = 106,
            .vertex_offset = 0,
            .vertex_count = 102,
            .triangle_offset = 0,
            .triangle_count = 106,
        };
        profile.charging.surface_material_name = "mc/mtl_weapon_g43_gold";
        profile.charging.expected_model_surface_count = 10;
        profile.charging.additional_piece_count = 1;
        profile.charging.additional_pieces[0] = {
            .material_name = "mc/mtl_silver_etching",
            .source_surface_index = 8,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 296,
            .source_surface_triangle_count = 334,
            .vertex_offset = 0,
            .vertex_count = 296,
            .triangle_offset = 0,
            .triangle_count = 333,
        };
        return profile;
    }();

inline constexpr DetachableMagazineWeaponProfile
    kZombieStg44DetachableMagazineWeaponProfile = []() constexpr {
        auto profile = kStg44DetachableMagazineWeaponProfile;
        profile.id = DetachableMagazineWeaponProfileId::ZombieStg44;
        profile.diagnostic_name = "Zombie StG 44";
        profile.internal_weapon_name = "zombie_stg44";
        profile.viewmodel_model_name = "viewmodel_zombie_mp44_lmg";
        profile.magazine_material_name = "mc/mtl_weapon_mp_mp44";
        profile.mesh.expected_model_surface_count = 4;
        profile.charging.surface_material_name = "mc/mtl_weapon_mp_mp44";
        profile.charging.expected_model_surface_count = 4;
        return profile;
    }();

inline constexpr DetachableMagazineWeaponProfile
    kZombieStg44UpgradedDetachableMagazineWeaponProfile = []() constexpr {
        auto profile = kZombieStg44DetachableMagazineWeaponProfile;
        profile.id = DetachableMagazineWeaponProfileId::ZombieStg44Upgraded;
        profile.diagnostic_name = "Upgraded zombie StG 44";
        profile.internal_weapon_name = "zombie_stg44_upgraded";
        profile.viewmodel_model_name = "viewmodel_zombie_mp44_lmg_up";
        profile.expected_clip_size = 60;
        profile.mesh.expected_model_surface_count = 5;
        profile.charging.expected_model_surface_count = 5;
        return profile;
    }();

inline constexpr DetachableMagazineWeaponProfile
    kZombieThompsonDetachableMagazineWeaponProfile = []() constexpr {
        auto profile = kThompsonDetachableMagazineWeaponProfile;
        profile.id = DetachableMagazineWeaponProfileId::ZombieThompson;
        profile.diagnostic_name = "Zombie Thompson";
        profile.internal_weapon_name = "zombie_thompson";
        profile.viewmodel_model_name = "viewmodel_zombie_thompson_smg";
        profile.magazine_material_name = "mc/mtl_weapon_mp_thompson";
        profile.charging.surface_material_name =
            "mc/mtl_weapon_mp_thompson";
        return profile;
    }();

inline constexpr DetachableMagazineWeaponProfile
    kZombieThompsonUpgradedDetachableMagazineWeaponProfile =
        []() constexpr {
        auto profile = kZombieThompsonDetachableMagazineWeaponProfile;
        profile.id =
            DetachableMagazineWeaponProfileId::ZombieThompsonUpgraded;
        profile.diagnostic_name = "Upgraded zombie Thompson";
        profile.internal_weapon_name = "zombie_thompson_upgraded";
        profile.viewmodel_model_name = "viewmodel_zombie_thompson_smg_up";
        profile.magazine_material_name = "mc/mtl_weapon_thompson_gold";
        profile.expected_clip_size = 40;
        profile.mesh.expected_model_surface_count = 5;
        profile.charging.surface_material_name =
            "mc/mtl_weapon_thompson_gold";
        profile.charging.expected_model_surface_count = 5;
        return profile;
    }();

inline constexpr DetachableMagazineWeaponProfile
    kZombieMp40DetachableMagazineWeaponProfile = []() constexpr {
        auto profile = kMp40DetachableMagazineWeaponProfile;
        profile.id = DetachableMagazineWeaponProfileId::ZombieMp40;
        profile.diagnostic_name = "Zombie MP40";
        profile.internal_weapon_name = "zombie_mp40";
        profile.viewmodel_model_name = "viewmodel_zombie_mp40_smg";
        profile.magazine_material_name = "mc/mtl_weapon_mp_mp40";
        profile.mesh.source_surface_index = 2;
        profile.charging.surface_material_name = "mc/mtl_weapon_mp_mp40";
        return profile;
    }();

inline constexpr DetachableMagazineWeaponProfile
    kZombieMp40UpgradedDetachableMagazineWeaponProfile = []() constexpr {
        auto profile = kZombieMp40DetachableMagazineWeaponProfile;
        profile.id = DetachableMagazineWeaponProfileId::ZombieMp40Upgraded;
        profile.diagnostic_name = "Upgraded zombie MP40";
        profile.internal_weapon_name = "zombie_mp40_upgraded";
        profile.viewmodel_model_name = "viewmodel_zombie_mp40_smg_up";
        profile.magazine_material_name = "mc/mtl_weapon_mp40_gold";
        profile.expected_clip_size = 64;
        profile.mesh.expected_model_surface_count = 5;
        profile.charging.surface_material_name = "mc/mtl_weapon_mp40_gold";
        profile.charging.expected_model_surface_count = 5;
        profile.charging.additional_piece_count = 1;
        profile.charging.additional_pieces[0] = {
            .material_name = "mc/mtl_silver_etching",
            .source_surface_index = 3,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 30,
            .source_surface_triangle_count = 26,
            .vertex_offset = 0,
            .vertex_count = 30,
            .triangle_offset = 0,
            .triangle_count = 26,
        };
        return profile;
    }();

inline constexpr DetachableMagazineWeaponProfile
    kZombieType100DetachableMagazineWeaponProfile = []() constexpr {
        auto profile = kType100DetachableMagazineWeaponProfile;
        profile.id = DetachableMagazineWeaponProfileId::ZombieType100;
        profile.diagnostic_name = "Zombie Type 100 SMG";
        profile.internal_weapon_name = "zombie_type100_smg";
        profile.viewmodel_model_name = "viewmodel_zombie_type100_smg";
        profile.magazine_material_name =
            "mc/mtl_weapon_mp_type100_metal";
        profile.mesh.expected_model_surface_count = 4;
        profile.mesh.source_surface_index = 1;
        profile.charging.surface_material_name =
            "mc/mtl_weapon_mp_type100_metal";
        profile.charging.expected_model_surface_count = 4;
        return profile;
    }();

inline constexpr DetachableMagazineWeaponProfile
    kZombieType100UpgradedDetachableMagazineWeaponProfile =
        []() constexpr {
        auto profile = kZombieType100DetachableMagazineWeaponProfile;
        profile.id =
            DetachableMagazineWeaponProfileId::ZombieType100Upgraded;
        profile.diagnostic_name = "Upgraded zombie Type 100 SMG";
        profile.internal_weapon_name = "zombie_type100_smg_upgraded";
        profile.viewmodel_model_name = "viewmodel_zombie_type100_smg_up";
        profile.magazine_material_name = "mc/mtl_weapon_type100_gold";
        profile.expected_clip_size = 60;
        profile.mesh.expected_model_surface_count = 6;
        profile.mesh.additional_piece_count = 1;
        profile.mesh.additional_pieces[0] = {
            .material_name = "mc/mtl_silver_etching",
            .source_surface_index = 4,
            .source_surface_rigid_subrange_count = 2,
            .source_rigid_subrange_index = 1,
            .source_surface_vertex_count = 82,
            .source_surface_triangle_count = 74,
            .vertex_offset = 22,
            .vertex_count = 60,
            .triangle_offset = 20,
            .triangle_count = 54,
        };
        profile.charging.surface_material_name =
            "mc/mtl_weapon_type100_gold";
        profile.charging.expected_model_surface_count = 6;
        profile.charging.additional_piece_count = 1;
        profile.charging.additional_pieces[0] = {
            .material_name = "mc/mtl_silver_etching",
            .source_surface_index = 4,
            .source_surface_rigid_subrange_count = 2,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 82,
            .source_surface_triangle_count = 74,
            .vertex_offset = 0,
            .vertex_count = 22,
            .triangle_offset = 0,
            .triangle_count = 20,
        };
        return profile;
    }();

inline constexpr DetachableMagazineWeaponProfile
    kZombieBarDetachableMagazineWeaponProfile = []() constexpr {
        auto profile = kBarDetachableMagazineWeaponProfile;
        profile.id = DetachableMagazineWeaponProfileId::ZombieBar;
        profile.diagnostic_name = "Zombie BAR";
        profile.internal_weapon_name = "zombie_bar";
        profile.viewmodel_model_name = "viewmodel_zombie_bar_lmg";
        profile.magazine_material_name = "mc/mtl_weapon_mp_bar";
        profile.mesh.expected_model_surface_count = 6;
        profile.mesh.source_surface_index = 0;
        profile.mesh.source_surface_rigid_subrange_count = 2;
        profile.mesh.source_rigid_subrange_index = 0;
        profile.mesh.source_surface_vertex_count = 76;
        profile.mesh.source_surface_triangle_count = 40;
        profile.mesh.vertex_offset = 0;
        profile.mesh.vertex_count = 24;
        profile.mesh.triangle_offset = 0;
        profile.mesh.triangle_count = 12;
        profile.charging.surface_material_name = "mc/mtl_weapon_mp_bar";
        profile.charging.expected_model_surface_count = 6;
        profile.charging.source_surface_index = 4;
        return profile;
    }();

inline constexpr DetachableMagazineWeaponProfile
    kZombieBarUpgradedDetachableMagazineWeaponProfile = []() constexpr {
        auto profile = kZombieBarDetachableMagazineWeaponProfile;
        profile.id = DetachableMagazineWeaponProfileId::ZombieBarUpgraded;
        profile.diagnostic_name = "Upgraded zombie BAR";
        profile.internal_weapon_name = "zombie_bar_upgraded";
        profile.viewmodel_model_name = "viewmodel_zombie_bar_lmg_up";
        profile.magazine_material_name = "mc/mtl_weapon_bar_gold";
        profile.expected_clip_size = 30;
        profile.mesh.expected_model_surface_count = 8;
        profile.mesh.additional_piece_count = 1;
        profile.mesh.additional_pieces[0] = {
            .material_name = "mc/mtl_silver_etching",
            .source_surface_index = 6,
            .source_surface_rigid_subrange_count = 4,
            .source_rigid_subrange_index = 1,
            .source_surface_vertex_count = 124,
            .source_surface_triangle_count = 118,
            .vertex_offset = 100,
            .vertex_count = 4,
            .triangle_offset = 100,
            .triangle_count = 2,
        };
        profile.charging.surface_material_name = "mc/mtl_weapon_bar_gold";
        profile.charging.expected_model_surface_count = 8;
        return profile;
    }();

inline constexpr DetachableMagazineWeaponProfile
    kZombieFg42DetachableMagazineWeaponProfile = []() constexpr {
        auto profile = kFg42BipodDetachableMagazineWeaponProfile;
        profile.id = DetachableMagazineWeaponProfileId::ZombieFg42;
        profile.diagnostic_name = "Zombie FG42";
        profile.internal_weapon_name = "zombie_fg42";
        profile.viewmodel_model_name = "viewmodel_zombie_fg42_lmg";
        profile.magazine_material_name = "mc/mtl_weapon_mp_fg42";
        profile.mesh.expected_model_bone_count = 8;
        profile.mesh.expected_model_surface_count = 7;
        profile.mesh.source_surface_index = 5;
        profile.charging.surface_material_name = "mc/mtl_weapon_mp_fg42";
        profile.charging.expected_model_bone_count = 8;
        profile.charging.expected_model_surface_count = 7;
        profile.charging.source_surface_index = 0;
        profile.charging.source_surface_rigid_subrange_count = 2;
        profile.charging.source_rigid_subrange_index = 1;
        profile.charging.source_surface_vertex_count = 127;
        profile.charging.source_surface_triangle_count = 92;
        profile.charging.rigid_vertex_offset = 4;
        profile.charging.rigid_vertex_count = 123;
        profile.charging.rigid_triangle_offset = 2;
        profile.charging.rigid_triangle_count = 90;
        return profile;
    }();

inline constexpr DetachableMagazineWeaponProfile
    kZombieFg42UpgradedDetachableMagazineWeaponProfile = []() constexpr {
        auto profile = kZombieFg42DetachableMagazineWeaponProfile;
        profile.id = DetachableMagazineWeaponProfileId::ZombieFg42Upgraded;
        profile.diagnostic_name = "Upgraded zombie FG42";
        profile.internal_weapon_name = "zombie_fg42_upgraded";
        profile.viewmodel_model_name = "viewmodel_zombie_fg42_lmg_up";
        profile.expected_clip_size = 64;
        profile.mesh.expected_model_surface_count = 9;
        profile.mesh.additional_piece_count = 1;
        profile.mesh.additional_pieces[0] = {
            .material_name = "mc/mtl_silver_etching",
            .source_surface_index = 8,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 72,
            .source_surface_triangle_count = 54,
            .vertex_offset = 0,
            .vertex_count = 72,
            .triangle_offset = 0,
            .triangle_count = 54,
        };
        profile.charging.expected_model_surface_count = 9;
        return profile;
    }();

inline constexpr DetachableMagazineWeaponProfile
    kZombiePpshDetachableMagazineWeaponProfile = []() constexpr {
        auto profile = kPpshDetachableMagazineWeaponProfile;
        profile.id = DetachableMagazineWeaponProfileId::ZombiePpsh;
        profile.diagnostic_name = "Zombie PPSh-41";
        profile.internal_weapon_name = "zombie_ppsh";
        profile.viewmodel_model_name = "viewmodel_zombie_ppsh_smg";
        profile.magazine_material_name = "mc/mtl_weapon_mp_ppsh41";
        profile.mesh.expected_model_surface_count = 5;
        profile.charging.surface_material_name =
            "mc/mtl_weapon_mp_ppsh41";
        profile.charging.expected_model_surface_count = 5;
        return profile;
    }();

inline constexpr DetachableMagazineWeaponProfile
    kZombiePpshUpgradedDetachableMagazineWeaponProfile = []() constexpr {
        auto profile = kZombiePpshDetachableMagazineWeaponProfile;
        profile.id = DetachableMagazineWeaponProfileId::ZombiePpshUpgraded;
        profile.diagnostic_name = "Upgraded zombie PPSh-41";
        profile.internal_weapon_name = "zombie_ppsh_upgraded";
        profile.viewmodel_model_name = "viewmodel_zombie_ppsh_smg_up";
        profile.magazine_material_name = "mc/mtl_weapon_ppsh41_gold";
        profile.expected_clip_size = 115;
        profile.mesh.expected_model_surface_count = 7;
        profile.mesh.additional_piece_count = 1;
        profile.mesh.additional_pieces[0] = {
            .material_name = "mc/mtl_silver_etching",
            .source_surface_index = 5,
            .source_surface_rigid_subrange_count = 1,
            .source_rigid_subrange_index = 0,
            .source_surface_vertex_count = 110,
            .source_surface_triangle_count = 100,
            .vertex_offset = 0,
            .vertex_count = 110,
            .triangle_offset = 0,
            .triangle_count = 100,
        };
        profile.charging.surface_material_name =
            "mc/mtl_weapon_ppsh41_gold";
        profile.charging.expected_model_surface_count = 7;
        return profile;
    }();

// The generic validator supports future registry entries, but the shipping
// Colt recipe must remain identical to the audited retail asset. Keep this
// ID-specific check constexpr so a renamed asset or silently widened mesh
// slice cannot enter a build even when unit tests are not compiled.
[[nodiscard]] constexpr bool validate_exact_zombie_colt_profile(
    const DetachableMagazineWeaponProfile& profile) noexcept {
    using detachable_magazine_detail::equal_strings;
    const auto& mesh = profile.mesh;
    const auto& charging = profile.charging;
    return validate_detachable_magazine_weapon_profile(profile) &&
        profile.id == DetachableMagazineWeaponProfileId::ZombieColt &&
        equal_strings(profile.diagnostic_name,
                      "Nacht zombie Colt M1911") &&
        equal_strings(profile.internal_weapon_name, "zombie_colt") &&
        equal_strings(profile.viewmodel_model_name,
                      "viewmodel_usa_colt45_pistol") &&
        equal_strings(profile.magazine_material_name,
                      "mc/mtl_weapon_colt45") &&
        equal_strings(profile.magazine_bone_tag_name, "j_clip") &&
        profile.expected_clip_size == 8 &&
        profile.requires_embedded_viewmodel_magazine &&
        charging.enabled &&
        equal_strings(charging.handle_bone_tag_name, "j_bolt") &&
        equal_strings(charging.surface_material_name,
                      "mc/mtl_weapon_colt45") &&
        charging.manipulating_hand == MagazineChargingHand::Left &&
        !charging.suppress_native_pose_always &&
        charging.expected_model_bone_count == 7 &&
        charging.expected_model_surface_count == 5 &&
        charging.handle_bone_index == 1 &&
        charging.parent_bone_index == 0 &&
        charging.source_surface_index == 2 &&
        charging.source_surface_rigid_subrange_count == 1 &&
        charging.source_rigid_subrange_index == 0 &&
        charging.source_surface_vertex_count == 1326 &&
        charging.source_surface_triangle_count == 1050 &&
        charging.rigid_vertex_offset == 0 &&
        charging.rigid_vertex_count == 1326 &&
        charging.rigid_triangle_offset == 0 &&
        charging.rigid_triangle_count == 1050 &&
        charging.additional_piece_count == 0 &&
        charging.bind_pose.translation.x == -1.952201F &&
        charging.bind_pose.translation.y == 0.067739F &&
        charging.bind_pose.translation.z == 1.572215F &&
        charging.bind_pose.orientation.x == 0.0F &&
        charging.bind_pose.orientation.y == -0.010224F &&
        charging.bind_pose.orientation.z == 0.0F &&
        charging.bind_pose.orientation.w == 0.999939F &&
        charging.interaction.travel_units == 1.50F &&
        charging.interaction.locked_open_offset_units == 1.35F &&
        charging.interaction.open_threshold == 0.98F &&
        charging.interaction.spring_return_seconds == 0.10F &&
        charging.interaction.trigger_engage == 0.65F &&
        charging.interaction.trigger_release == 0.35F &&
        charging.interaction.completion ==
            MagazineChargingCompletion::SpringClosed &&
        charging.interaction.return_sample_count == 4 &&
        charging.interaction.return_samples[0] == 1.0F &&
        charging.interaction.return_samples[1] == 0.72F &&
        charging.interaction.return_samples[2] == 0.24F &&
        charging.interaction.return_samples[3] == 0.0F &&
        charging.grab_radius_units == 8.0F &&
        mesh.expected_model_bone_count == 7 &&
        mesh.expected_model_surface_count == 5 &&
        mesh.magazine_bone_index == 2 &&
        mesh.source_surface_index == 0 &&
        mesh.source_surface_rigid_subrange_count == 3 &&
        mesh.source_rigid_subrange_index == 1 &&
        mesh.source_surface_vertex_count == 252 &&
        mesh.source_surface_triangle_count == 202 &&
        mesh.vertex_offset == 87 && mesh.vertex_count == 75 &&
        mesh.triangle_offset == 75 && mesh.triangle_count == 55 &&
        mesh.additional_piece_count == 0 &&
        mesh.bind_pose.translation.x == -1.340145F &&
        mesh.bind_pose.translation.y == 0.071632F &&
        mesh.bind_pose.translation.z == -1.237288F &&
        mesh.bind_pose.orientation.x == 0.0F &&
        mesh.bind_pose.orientation.y == 0.0F &&
        mesh.bind_pose.orientation.z == 0.0F &&
        mesh.bind_pose.orientation.w == 1.0F &&
        profile.held_pose_from_controller.translation.x == 3.25F &&
        profile.held_pose_from_controller.translation.y == 0.0F &&
        profile.held_pose_from_controller.translation.z == 1.25F &&
        profile.held_pose_from_controller.orientation.x == 0.0F &&
        profile.held_pose_from_controller.orientation.y == 0.0F &&
        profile.held_pose_from_controller.orientation.z == 0.0F &&
        profile.held_pose_from_controller.orientation.w == 1.0F &&
        profile.insertion_guide_pose_from_anchor.translation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.w == 1.0F &&
        profile.insertion_radius_units == 9.0F;
}

// The later-map base Zombie Colt reuses the accepted Colt mechanics and exact
// rigid topology under a dedicated immutable viewmodel identity. Rebase only
// those audited identity deltas before applying the complete Nacht lock so no
// inherited mesh, slide, pose, or interaction field can drift unnoticed.
[[nodiscard]] constexpr bool validate_exact_zombie_colt_dedicated_profile(
    const DetachableMagazineWeaponProfile& profile) noexcept {
    using detachable_magazine_detail::equal_strings;
    if (!validate_detachable_magazine_weapon_profile(profile) ||
        profile.id !=
            DetachableMagazineWeaponProfileId::ZombieColtDedicated ||
        !equal_strings(profile.diagnostic_name, "Zombie Colt M1911") ||
        !equal_strings(profile.internal_weapon_name, "zombie_colt") ||
        !equal_strings(profile.viewmodel_model_name,
                       "viewmodel_zombie_colt45_pistol")) {
        return false;
    }

    auto inherited = profile;
    inherited.id = DetachableMagazineWeaponProfileId::ZombieColt;
    inherited.diagnostic_name = "Nacht zombie Colt M1911";
    inherited.viewmodel_model_name = "viewmodel_usa_colt45_pistol";
    return validate_exact_zombie_colt_profile(inherited);
}

// The M1A1 recipe is independently locked to the audited Nacht retail asset.
[[nodiscard]] constexpr bool validate_exact_m1carbine_profile(
    const DetachableMagazineWeaponProfile& profile) noexcept {
    using detachable_magazine_detail::equal_strings;
    const auto& mesh = profile.mesh;
    const auto& charging = profile.charging;
    return validate_detachable_magazine_weapon_profile(profile) &&
        profile.id == DetachableMagazineWeaponProfileId::M1Carbine &&
        equal_strings(profile.diagnostic_name, "Nacht M1A1 Carbine") &&
        equal_strings(profile.internal_weapon_name, "m1carbine") &&
        equal_strings(profile.viewmodel_model_name,
                      "viewmodel_usa_m1carbine_rifle") &&
        equal_strings(profile.magazine_material_name,
                      "mc/mtl_weapon_carbine") &&
        equal_strings(profile.magazine_bone_tag_name, "j_clip") &&
        profile.expected_clip_size == 15 &&
        profile.requires_embedded_viewmodel_magazine &&
        charging.enabled &&
        equal_strings(charging.handle_bone_tag_name, "j_bolt") &&
        equal_strings(charging.surface_material_name,
                      "mc/mtl_weapon_carbine") &&
        charging.manipulating_hand == MagazineChargingHand::Right &&
        charging.suppress_native_pose_always &&
        charging.expected_model_bone_count == 5 &&
        charging.expected_model_surface_count == 4 &&
        charging.handle_bone_index == 1 &&
        charging.parent_bone_index == 0 &&
        charging.source_surface_index == 2 &&
        charging.source_surface_rigid_subrange_count == 1 &&
        charging.source_rigid_subrange_index == 0 &&
        charging.source_surface_vertex_count == 328 &&
        charging.source_surface_triangle_count == 316 &&
        charging.rigid_vertex_offset == 0 &&
        charging.rigid_vertex_count == 328 &&
        charging.rigid_triangle_offset == 0 &&
        charging.rigid_triangle_count == 315 &&
        charging.additional_piece_count == 0 &&
        charging.bind_pose.translation.x == -1.946244F &&
        charging.bind_pose.translation.y == -1.782576F &&
        charging.bind_pose.translation.z == 0.924210F &&
        charging.bind_pose.orientation.x == 0.0F &&
        charging.bind_pose.orientation.y == -0.010224F &&
        charging.bind_pose.orientation.z == 0.0F &&
        charging.bind_pose.orientation.w == 0.999939F &&
        charging.interaction.travel_units == 2.961060F &&
        charging.interaction.locked_open_offset_units == 2.384359F &&
        charging.interaction.open_threshold == 0.98F &&
        charging.interaction.spring_return_seconds == 1.0F / 6.0F &&
        charging.interaction.trigger_engage == 0.65F &&
        charging.interaction.trigger_release == 0.35F &&
        charging.interaction.completion ==
            MagazineChargingCompletion::SpringClosed &&
        charging.interaction.return_sample_count == 6 &&
        charging.interaction.return_samples[0] == 1.0F &&
        charging.interaction.return_samples[1] == 228.0F / 255.0F &&
        charging.interaction.return_samples[2] == 165.0F / 255.0F &&
        charging.interaction.return_samples[3] == 90.0F / 255.0F &&
        charging.interaction.return_samples[4] == 27.0F / 255.0F &&
        charging.interaction.return_samples[5] == 0.0F &&
        charging.grab_radius_units == 10.0F &&
        mesh.expected_model_bone_count == 5 &&
        mesh.expected_model_surface_count == 4 &&
        mesh.magazine_bone_index == 2 &&
        mesh.source_surface_index == 0 &&
        mesh.source_surface_rigid_subrange_count == 1 &&
        mesh.source_rigid_subrange_index == 0 &&
        mesh.source_surface_vertex_count == 74 &&
        mesh.source_surface_triangle_count == 58 &&
        mesh.vertex_offset == 0 && mesh.vertex_count == 74 &&
        mesh.triangle_offset == 0 && mesh.triangle_count == 58 &&
        mesh.additional_piece_count == 0 &&
        mesh.bind_pose.translation.x == -2.436279F &&
        mesh.bind_pose.translation.y == 0.012866F &&
        mesh.bind_pose.translation.z == -0.551744F &&
        mesh.bind_pose.orientation.x == 0.0F &&
        mesh.bind_pose.orientation.y == 0.0F &&
        mesh.bind_pose.orientation.z == 0.0F &&
        mesh.bind_pose.orientation.w == 1.0F &&
        profile.held_pose_from_controller.translation.x == 3.25F &&
        profile.held_pose_from_controller.translation.y == 0.0F &&
        profile.held_pose_from_controller.translation.z == 1.25F &&
        profile.held_pose_from_controller.orientation.x == 0.0F &&
        profile.held_pose_from_controller.orientation.y == 0.0F &&
        profile.held_pose_from_controller.orientation.z == 0.0F &&
        profile.held_pose_from_controller.orientation.w == 1.0F &&
        profile.insertion_guide_pose_from_anchor.translation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.w == 1.0F &&
        profile.insertion_radius_units == 9.0F;
}

// The Garand en-bloc clip and operating rod are both exact one-piece rigid
// inventories. Keep the automatic close policy separate from the conventional
// controller-pulled charging profiles even while its travel curve remains a
// provisional calibration seed.
[[nodiscard]] constexpr bool validate_exact_m1garand_profile(
    const DetachableMagazineWeaponProfile& profile) noexcept {
    using detachable_magazine_detail::equal_strings;
    const auto& mesh = profile.mesh;
    const auto& charging = profile.charging;
    return validate_detachable_magazine_weapon_profile(profile) &&
        profile.id == DetachableMagazineWeaponProfileId::M1Garand &&
        equal_strings(profile.diagnostic_name, "M1 Garand") &&
        equal_strings(profile.internal_weapon_name, "m1garand") &&
        equal_strings(profile.viewmodel_model_name,
                      "viewmodel_usa_m1garand_rifle") &&
        equal_strings(profile.magazine_material_name,
                      "mc/mtl_brass_shells") &&
        equal_strings(profile.magazine_bone_tag_name, "j_clip") &&
        profile.reload_kind ==
            gameplay::ReloadProfileKind::DetachableMagazine &&
        profile.expected_clip_size == 8 &&
        profile.requires_embedded_viewmodel_magazine &&
        charging.enabled &&
        equal_strings(charging.handle_bone_tag_name, "j_bolt") &&
        equal_strings(charging.surface_material_name,
                      "mc/mtl_weapon_m1garand") &&
        charging.manipulating_hand == MagazineChargingHand::Right &&
        !charging.suppress_native_pose_always &&
        charging.expected_model_bone_count == 7 &&
        charging.expected_model_surface_count == 6 &&
        charging.handle_bone_index == 1 &&
        charging.parent_bone_index == 0 &&
        charging.source_surface_index == 2 &&
        charging.source_surface_rigid_subrange_count == 1 &&
        charging.source_rigid_subrange_index == 0 &&
        charging.source_surface_vertex_count == 534 &&
        charging.source_surface_triangle_count == 486 &&
        charging.rigid_vertex_offset == 0 &&
        charging.rigid_vertex_count == 534 &&
        charging.rigid_triangle_offset == 0 &&
        charging.rigid_triangle_count == 485 &&
        charging.additional_piece_count == 0 &&
        charging.bind_pose.translation.x == -0.273143F &&
        charging.bind_pose.translation.y == -1.810324F &&
        charging.bind_pose.translation.z == 1.101824F &&
        charging.bind_pose.orientation.x == 0.0F &&
        charging.bind_pose.orientation.y == -0.010224F &&
        charging.bind_pose.orientation.z == 0.0F &&
        charging.bind_pose.orientation.w == 0.999939F &&
        charging.interaction.control_policy ==
            MagazineChargingControlPolicy::EnBlocAutomatic &&
        charging.interaction.travel_units == 2.961060F &&
        charging.interaction.locked_open_offset_units == 2.961060F &&
        charging.interaction.open_threshold == 0.98F &&
        charging.interaction.spring_return_seconds == 1.0F / 6.0F &&
        charging.interaction.trigger_engage == 0.0F &&
        charging.interaction.trigger_release == 0.0F &&
        charging.interaction.completion ==
            MagazineChargingCompletion::SpringClosed &&
        charging.interaction.return_sample_count == 6 &&
        charging.interaction.return_samples[0] == 1.0F &&
        charging.interaction.return_samples[1] == 228.0F / 255.0F &&
        charging.interaction.return_samples[2] == 165.0F / 255.0F &&
        charging.interaction.return_samples[3] == 90.0F / 255.0F &&
        charging.interaction.return_samples[4] == 27.0F / 255.0F &&
        charging.interaction.return_samples[5] == 0.0F &&
        charging.grab_radius_units == 0.0F &&
        mesh.expected_model_bone_count == 7 &&
        mesh.expected_model_surface_count == 6 &&
        mesh.magazine_bone_index == 2 &&
        mesh.source_surface_index == 4 &&
        mesh.source_surface_rigid_subrange_count == 1 &&
        mesh.source_rigid_subrange_index == 0 &&
        mesh.source_surface_vertex_count == 204 &&
        mesh.source_surface_triangle_count == 130 &&
        mesh.vertex_offset == 0 && mesh.vertex_count == 204 &&
        mesh.triangle_offset == 0 && mesh.triangle_count == 130 &&
        mesh.additional_piece_count == 0 &&
        mesh.bind_pose.translation.x == -1.888771F &&
        mesh.bind_pose.translation.y == -0.006035F &&
        mesh.bind_pose.translation.z == -7.283907F &&
        mesh.bind_pose.orientation.x == 0.0F &&
        mesh.bind_pose.orientation.y == 0.0F &&
        mesh.bind_pose.orientation.z == 0.0F &&
        mesh.bind_pose.orientation.w == 1.0F &&
        profile.held_pose_from_controller.translation.x == 3.25F &&
        profile.held_pose_from_controller.translation.y == 0.0F &&
        profile.held_pose_from_controller.translation.z == 1.25F &&
        profile.held_pose_from_controller.orientation.x == 0.0F &&
        profile.held_pose_from_controller.orientation.y == 0.0F &&
        profile.held_pose_from_controller.orientation.z == 0.0F &&
        profile.held_pose_from_controller.orientation.w == 1.0F &&
        profile.insertion_guide_pose_from_anchor.translation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.w == 1.0F &&
        profile.insertion_radius_units == 9.0F;
}

// Normalize only the exact asset identity before reusing the base Garand's
// exhaustive topology and en-bloc action validation. This intentionally makes
// any future bayonet-specific topology drift fail closed until it is audited.
[[nodiscard]] constexpr bool validate_exact_m1garand_bayonet_profile(
    const DetachableMagazineWeaponProfile& profile) noexcept {
    using detachable_magazine_detail::equal_strings;
    if (!validate_detachable_magazine_weapon_profile(profile) ||
        profile.id != DetachableMagazineWeaponProfileId::M1GarandBayonet ||
        !equal_strings(profile.diagnostic_name, "M1 Garand bayonet") ||
        !equal_strings(profile.internal_weapon_name, "m1garand_bayonet") ||
        !equal_strings(profile.viewmodel_model_name,
                       "viewmodel_usa_m1garand_rifle_bayonet")) {
        return false;
    }

    if (profile.mesh.expected_model_bone_count != 8 ||
        profile.mesh.expected_model_surface_count != 7 ||
        profile.mesh.source_surface_index != 5 ||
        profile.charging.expected_model_bone_count != 8 ||
        profile.charging.expected_model_surface_count != 7 ||
        profile.charging.source_surface_index != 3) {
        return false;
    }

    auto normalized = profile;
    normalized.id = DetachableMagazineWeaponProfileId::M1Garand;
    normalized.diagnostic_name = "M1 Garand";
    normalized.internal_weapon_name = "m1garand";
    normalized.viewmodel_model_name = "viewmodel_usa_m1garand_rifle";
    normalized.mesh.expected_model_bone_count = 7;
    normalized.mesh.expected_model_surface_count = 6;
    normalized.mesh.source_surface_index = 4;
    normalized.charging.expected_model_bone_count = 7;
    normalized.charging.expected_model_surface_count = 6;
    normalized.charging.source_surface_index = 2;
    return validate_exact_m1garand_profile(normalized);
}

// Reuse the complete Garand en-bloc validation only after the campaign GL
// identity and its appended one-surface mount have both passed exact checks.
// Any retail/model drift therefore leaves native reload behavior untouched.
[[nodiscard]] constexpr bool
validate_exact_m1garand_grenade_launcher_profile(
    const DetachableMagazineWeaponProfile& profile) noexcept {
    using detachable_magazine_detail::equal_strings;
    if (!validate_detachable_magazine_weapon_profile(profile) ||
        profile.id !=
            DetachableMagazineWeaponProfileId::M1GarandGrenadeLauncher ||
        !equal_strings(profile.diagnostic_name,
                       "M1 Garand rifle-grenade launcher") ||
        !equal_strings(profile.internal_weapon_name, "m1garand_gl") ||
        !equal_strings(profile.viewmodel_model_name,
                       "viewmodel_usa_m1garand_rifle_grenade_mount")) {
        return false;
    }

    if (profile.mesh.expected_model_bone_count != 8 ||
        profile.mesh.expected_model_surface_count != 7 ||
        profile.mesh.source_surface_index != 4 ||
        profile.charging.expected_model_bone_count != 8 ||
        profile.charging.expected_model_surface_count != 7 ||
        profile.charging.source_surface_index != 2) {
        return false;
    }

    auto normalized = profile;
    normalized.id = DetachableMagazineWeaponProfileId::M1Garand;
    normalized.diagnostic_name = "M1 Garand";
    normalized.internal_weapon_name = "m1garand";
    normalized.viewmodel_model_name = "viewmodel_usa_m1garand_rifle";
    normalized.mesh.expected_model_bone_count = 7;
    normalized.mesh.expected_model_surface_count = 6;
    normalized.mesh.source_surface_index = 4;
    normalized.charging.expected_model_bone_count = 7;
    normalized.charging.expected_model_surface_count = 6;
    normalized.charging.source_surface_index = 2;
    return validate_exact_m1garand_profile(normalized);
}

// The dedicated base Zombie M1A1 keeps the accepted Nacht geometry, poses,
// and charging policy while changing its immutable weapon/model/material
// identity. Rebase exactly those audited deltas through the complete original
// lock so the clone cannot silently diverge from its accepted family recipe.
[[nodiscard]] constexpr bool validate_exact_zombie_m1carbine_profile(
    const DetachableMagazineWeaponProfile& profile) noexcept {
    using detachable_magazine_detail::equal_strings;
    if (!validate_detachable_magazine_weapon_profile(profile) ||
        profile.id != DetachableMagazineWeaponProfileId::ZombieM1Carbine ||
        !equal_strings(profile.diagnostic_name, "Zombie M1A1 Carbine") ||
        !equal_strings(profile.internal_weapon_name, "zombie_m1carbine") ||
        !equal_strings(profile.viewmodel_model_name,
                       "viewmodel_zombie_m1carbine_rifle") ||
        !equal_strings(profile.magazine_material_name,
                       "mc/mtl_weapon_mp_carbine") ||
        !equal_strings(profile.charging.surface_material_name,
                       "mc/mtl_weapon_mp_carbine")) {
        return false;
    }

    auto inherited = profile;
    inherited.id = DetachableMagazineWeaponProfileId::M1Carbine;
    inherited.diagnostic_name = "Nacht M1A1 Carbine";
    inherited.internal_weapon_name = "m1carbine";
    inherited.viewmodel_model_name = "viewmodel_usa_m1carbine_rifle";
    inherited.magazine_material_name = "mc/mtl_weapon_carbine";
    inherited.charging.surface_material_name = "mc/mtl_weapon_carbine";
    return validate_exact_m1carbine_profile(inherited);
}

// The base Zombie Gewehr 43 recipe is locked independently to the physically
// accepted Der Riese retail asset. The upgraded and campaign variants retain only
// structural validation until each completes its own headset acceptance.
[[nodiscard]] constexpr bool validate_exact_zombie_gewehr43_profile(
    const DetachableMagazineWeaponProfile& profile) noexcept {
    using detachable_magazine_detail::equal_strings;
    const auto& mesh = profile.mesh;
    const auto& charging = profile.charging;
    return validate_detachable_magazine_weapon_profile(profile) &&
        profile.id == DetachableMagazineWeaponProfileId::ZombieGewehr43 &&
        equal_strings(profile.diagnostic_name, "Zombie Gewehr 43") &&
        equal_strings(profile.internal_weapon_name, "zombie_gewehr43") &&
        equal_strings(profile.viewmodel_model_name,
                      "viewmodel_zombie_g43_rifle") &&
        equal_strings(profile.magazine_material_name,
                      "mc/mtl_weapon_mp_g43") &&
        equal_strings(profile.magazine_bone_tag_name, "j_clip") &&
        profile.expected_clip_size == 10 &&
        profile.requires_embedded_viewmodel_magazine &&
        charging.enabled &&
        equal_strings(charging.handle_bone_tag_name, "j_bolt") &&
        equal_strings(charging.surface_material_name,
                      "mc/mtl_weapon_mp_g43") &&
        charging.manipulating_hand == MagazineChargingHand::Right &&
        !charging.suppress_native_pose_always &&
        charging.expected_model_bone_count == 7 &&
        charging.expected_model_surface_count == 6 &&
        charging.handle_bone_index == 1 &&
        charging.parent_bone_index == 0 &&
        charging.source_surface_index == 4 &&
        charging.source_surface_rigid_subrange_count == 1 &&
        charging.source_rigid_subrange_index == 0 &&
        charging.source_surface_vertex_count == 780 &&
        charging.source_surface_triangle_count == 696 &&
        charging.rigid_vertex_offset == 0 &&
        charging.rigid_vertex_count == 780 &&
        charging.rigid_triangle_offset == 0 &&
        charging.rigid_triangle_count == 695 &&
        charging.additional_piece_count == 0 &&
        charging.bind_pose.translation.x == -2.577788F &&
        charging.bind_pose.translation.y == 0.788577F &&
        charging.bind_pose.translation.z == 1.976399F &&
        charging.bind_pose.orientation.x == 0.0F &&
        charging.bind_pose.orientation.y == -0.010224F &&
        charging.bind_pose.orientation.z == 0.0F &&
        charging.bind_pose.orientation.w == 0.999939F &&
        charging.interaction.travel_units == 2.75F &&
        charging.interaction.locked_open_offset_units == 2.45F &&
        charging.interaction.open_threshold == 0.98F &&
        charging.interaction.spring_return_seconds == 0.15F &&
        charging.interaction.trigger_engage == 0.65F &&
        charging.interaction.trigger_release == 0.35F &&
        charging.interaction.completion ==
            MagazineChargingCompletion::SpringClosed &&
        charging.interaction.return_sample_count == 5 &&
        charging.interaction.return_samples[0] == 1.0F &&
        charging.interaction.return_samples[1] == 0.82F &&
        charging.interaction.return_samples[2] == 0.48F &&
        charging.interaction.return_samples[3] == 0.14F &&
        charging.interaction.return_samples[4] == 0.0F &&
        charging.grab_radius_units == 10.0F &&
        mesh.expected_model_bone_count == 7 &&
        mesh.expected_model_surface_count == 6 &&
        mesh.magazine_bone_index == 2 &&
        mesh.source_surface_index == 2 &&
        mesh.source_surface_rigid_subrange_count == 1 &&
        mesh.source_rigid_subrange_index == 0 &&
        mesh.source_surface_vertex_count == 477 &&
        mesh.source_surface_triangle_count == 398 &&
        mesh.vertex_offset == 0 && mesh.vertex_count == 477 &&
        mesh.triangle_offset == 0 && mesh.triangle_count == 398 &&
        mesh.additional_piece_count == 0 &&
        mesh.bind_pose.translation.x == -1.020676F &&
        mesh.bind_pose.translation.y == 0.002369F &&
        mesh.bind_pose.translation.z == -0.272053F &&
        mesh.bind_pose.orientation.x == 0.0F &&
        mesh.bind_pose.orientation.y == 0.0F &&
        mesh.bind_pose.orientation.z == 0.0F &&
        mesh.bind_pose.orientation.w == 1.0F &&
        profile.held_pose_from_controller.translation.x == 3.25F &&
        profile.held_pose_from_controller.translation.y == 0.0F &&
        profile.held_pose_from_controller.translation.z == 1.25F &&
        profile.held_pose_from_controller.orientation.x == 0.0F &&
        profile.held_pose_from_controller.orientation.y == 0.0F &&
        profile.held_pose_from_controller.orientation.z == 0.0F &&
        profile.held_pose_from_controller.orientation.w == 1.0F &&
        profile.insertion_guide_pose_from_anchor.translation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.w == 1.0F &&
        profile.insertion_radius_units == 9.0F;
}

// The upgraded Zombie Gewehr 43 is a distinct accepted retail asset. Lock its
// gold primary geometry and both silver-etching secondary pieces explicitly;
// accepting the base profile does not prove this two-piece topology.
[[nodiscard]] constexpr bool validate_exact_zombie_gewehr43_upgraded_profile(
    const DetachableMagazineWeaponProfile& profile) noexcept {
    using detachable_magazine_detail::equal_strings;
    const auto& mesh = profile.mesh;
    const auto& magazine_detail = mesh.additional_pieces[0];
    const auto& charging = profile.charging;
    const auto& charging_detail = charging.additional_pieces[0];
    return validate_detachable_magazine_weapon_profile(profile) &&
        profile.id ==
            DetachableMagazineWeaponProfileId::ZombieGewehr43Upgraded &&
        equal_strings(profile.diagnostic_name,
                      "Upgraded zombie Gewehr 43") &&
        equal_strings(profile.internal_weapon_name,
                      "zombie_gewehr43_upgraded") &&
        equal_strings(profile.viewmodel_model_name,
                      "viewmodel_zombie_g43_rifle_up") &&
        equal_strings(profile.magazine_material_name,
                      "mc/mtl_weapon_g43_gold") &&
        equal_strings(profile.magazine_bone_tag_name, "j_clip") &&
        profile.expected_clip_size == 12 &&
        profile.requires_embedded_viewmodel_magazine &&
        charging.enabled &&
        equal_strings(charging.handle_bone_tag_name, "j_bolt") &&
        equal_strings(charging.surface_material_name,
                      "mc/mtl_weapon_g43_gold") &&
        charging.manipulating_hand == MagazineChargingHand::Right &&
        !charging.suppress_native_pose_always &&
        charging.expected_model_bone_count == 7 &&
        charging.expected_model_surface_count == 10 &&
        charging.handle_bone_index == 1 &&
        charging.parent_bone_index == 0 &&
        charging.source_surface_index == 4 &&
        charging.source_surface_rigid_subrange_count == 1 &&
        charging.source_rigid_subrange_index == 0 &&
        charging.source_surface_vertex_count == 780 &&
        charging.source_surface_triangle_count == 696 &&
        charging.rigid_vertex_offset == 0 &&
        charging.rigid_vertex_count == 780 &&
        charging.rigid_triangle_offset == 0 &&
        charging.rigid_triangle_count == 695 &&
        charging.additional_piece_count == 1 &&
        equal_strings(charging_detail.material_name,
                      "mc/mtl_silver_etching") &&
        charging_detail.source_surface_index == 8 &&
        charging_detail.source_surface_rigid_subrange_count == 1 &&
        charging_detail.source_rigid_subrange_index == 0 &&
        charging_detail.source_surface_vertex_count == 296 &&
        charging_detail.source_surface_triangle_count == 334 &&
        charging_detail.vertex_offset == 0 &&
        charging_detail.vertex_count == 296 &&
        charging_detail.triangle_offset == 0 &&
        charging_detail.triangle_count == 333 &&
        charging.bind_pose.translation.x == -2.577788F &&
        charging.bind_pose.translation.y == 0.788577F &&
        charging.bind_pose.translation.z == 1.976399F &&
        charging.bind_pose.orientation.x == 0.0F &&
        charging.bind_pose.orientation.y == -0.010224F &&
        charging.bind_pose.orientation.z == 0.0F &&
        charging.bind_pose.orientation.w == 0.999939F &&
        charging.interaction.travel_units == 2.75F &&
        charging.interaction.locked_open_offset_units == 2.45F &&
        charging.interaction.open_threshold == 0.98F &&
        charging.interaction.spring_return_seconds == 0.15F &&
        charging.interaction.trigger_engage == 0.65F &&
        charging.interaction.trigger_release == 0.35F &&
        charging.interaction.completion ==
            MagazineChargingCompletion::SpringClosed &&
        charging.interaction.return_sample_count == 5 &&
        charging.interaction.return_samples[0] == 1.0F &&
        charging.interaction.return_samples[1] == 0.82F &&
        charging.interaction.return_samples[2] == 0.48F &&
        charging.interaction.return_samples[3] == 0.14F &&
        charging.interaction.return_samples[4] == 0.0F &&
        charging.grab_radius_units == 10.0F &&
        mesh.expected_model_bone_count == 7 &&
        mesh.expected_model_surface_count == 10 &&
        mesh.magazine_bone_index == 2 &&
        mesh.source_surface_index == 2 &&
        mesh.source_surface_rigid_subrange_count == 1 &&
        mesh.source_rigid_subrange_index == 0 &&
        mesh.source_surface_vertex_count == 477 &&
        mesh.source_surface_triangle_count == 398 &&
        mesh.vertex_offset == 0 && mesh.vertex_count == 477 &&
        mesh.triangle_offset == 0 && mesh.triangle_count == 398 &&
        mesh.additional_piece_count == 1 &&
        equal_strings(magazine_detail.material_name,
                      "mc/mtl_silver_etching") &&
        magazine_detail.source_surface_index == 7 &&
        magazine_detail.source_surface_rigid_subrange_count == 1 &&
        magazine_detail.source_rigid_subrange_index == 0 &&
        magazine_detail.source_surface_vertex_count == 102 &&
        magazine_detail.source_surface_triangle_count == 106 &&
        magazine_detail.vertex_offset == 0 &&
        magazine_detail.vertex_count == 102 &&
        magazine_detail.triangle_offset == 0 &&
        magazine_detail.triangle_count == 106 &&
        mesh.bind_pose.translation.x == -1.020676F &&
        mesh.bind_pose.translation.y == 0.002369F &&
        mesh.bind_pose.translation.z == -0.272053F &&
        mesh.bind_pose.orientation.x == 0.0F &&
        mesh.bind_pose.orientation.y == 0.0F &&
        mesh.bind_pose.orientation.z == 0.0F &&
        mesh.bind_pose.orientation.w == 1.0F &&
        profile.held_pose_from_controller.translation.x == 3.25F &&
        profile.held_pose_from_controller.translation.y == 0.0F &&
        profile.held_pose_from_controller.translation.z == 1.25F &&
        profile.held_pose_from_controller.orientation.x == 0.0F &&
        profile.held_pose_from_controller.orientation.y == 0.0F &&
        profile.held_pose_from_controller.orientation.z == 0.0F &&
        profile.held_pose_from_controller.orientation.w == 1.0F &&
        profile.insertion_guide_pose_from_anchor.translation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.w == 1.0F &&
        profile.insertion_radius_units == 9.0F;
}

// The base Zombie StG 44 is locked to its independently accepted four-surface
// retail viewmodel.
[[nodiscard]] constexpr bool validate_exact_zombie_stg44_profile(
    const DetachableMagazineWeaponProfile& profile) noexcept {
    using detachable_magazine_detail::equal_strings;
    const auto& mesh = profile.mesh;
    const auto& charging = profile.charging;
    return validate_detachable_magazine_weapon_profile(profile) &&
        profile.id == DetachableMagazineWeaponProfileId::ZombieStg44 &&
        equal_strings(profile.diagnostic_name, "Zombie StG 44") &&
        equal_strings(profile.internal_weapon_name, "zombie_stg44") &&
        equal_strings(profile.viewmodel_model_name,
                      "viewmodel_zombie_mp44_lmg") &&
        equal_strings(profile.magazine_material_name,
                      "mc/mtl_weapon_mp_mp44") &&
        equal_strings(profile.magazine_bone_tag_name, "j_clip") &&
        profile.expected_clip_size == 30 &&
        profile.requires_embedded_viewmodel_magazine &&
        charging.enabled &&
        equal_strings(charging.handle_bone_tag_name, "j_bolt") &&
        equal_strings(charging.surface_material_name,
                      "mc/mtl_weapon_mp_mp44") &&
        charging.manipulating_hand == MagazineChargingHand::Right &&
        !charging.suppress_native_pose_always &&
        charging.expected_model_bone_count == 6 &&
        charging.expected_model_surface_count == 4 &&
        charging.handle_bone_index == 1 &&
        charging.parent_bone_index == 0 &&
        charging.source_surface_index == 2 &&
        charging.source_surface_rigid_subrange_count == 1 &&
        charging.source_rigid_subrange_index == 0 &&
        charging.source_surface_vertex_count == 304 &&
        charging.source_surface_triangle_count == 322 &&
        charging.rigid_vertex_offset == 0 &&
        charging.rigid_vertex_count == 304 &&
        charging.rigid_triangle_offset == 0 &&
        charging.rigid_triangle_count == 321 &&
        charging.additional_piece_count == 0 &&
        charging.bind_pose.translation.x == 4.690453F &&
        charging.bind_pose.translation.y == 0.134476F &&
        charging.bind_pose.translation.z == 3.205951F &&
        charging.bind_pose.orientation.x == 0.0F &&
        charging.bind_pose.orientation.y == -0.010224F &&
        charging.bind_pose.orientation.z == 0.0F &&
        charging.bind_pose.orientation.w == 0.999939F &&
        charging.interaction.travel_units == 3.10F &&
        charging.interaction.locked_open_offset_units == 0.0F &&
        charging.interaction.open_threshold == 0.98F &&
        charging.interaction.spring_return_seconds == 1.0F / 6.0F &&
        charging.interaction.trigger_engage == 0.65F &&
        charging.interaction.trigger_release == 0.35F &&
        charging.interaction.completion ==
            MagazineChargingCompletion::SpringClosed &&
        charging.interaction.return_sample_count == 5 &&
        charging.interaction.return_samples[0] == 1.0F &&
        charging.interaction.return_samples[1] == 0.82F &&
        charging.interaction.return_samples[2] == 0.48F &&
        charging.interaction.return_samples[3] == 0.14F &&
        charging.interaction.return_samples[4] == 0.0F &&
        charging.grab_radius_units == 10.0F &&
        mesh.expected_model_bone_count == 6 &&
        mesh.expected_model_surface_count == 4 &&
        mesh.magazine_bone_index == 2 &&
        mesh.source_surface_index == 1 &&
        mesh.source_surface_rigid_subrange_count == 1 &&
        mesh.source_rigid_subrange_index == 0 &&
        mesh.source_surface_vertex_count == 202 &&
        mesh.source_surface_triangle_count == 152 &&
        mesh.vertex_offset == 0 && mesh.vertex_count == 202 &&
        mesh.triangle_offset == 0 && mesh.triangle_count == 151 &&
        mesh.additional_piece_count == 0 &&
        mesh.bind_pose.translation.x == 6.639460F &&
        mesh.bind_pose.translation.y == 0.015012F &&
        mesh.bind_pose.translation.z == 1.835601F &&
        mesh.bind_pose.orientation.x == 0.0F &&
        mesh.bind_pose.orientation.y == 0.0F &&
        mesh.bind_pose.orientation.z == 0.0F &&
        mesh.bind_pose.orientation.w == 1.0F &&
        profile.held_pose_from_controller.translation.x == 3.25F &&
        profile.held_pose_from_controller.translation.y == 0.0F &&
        profile.held_pose_from_controller.translation.z == 1.25F &&
        profile.held_pose_from_controller.orientation.x == 0.0F &&
        profile.held_pose_from_controller.orientation.y == 0.0F &&
        profile.held_pose_from_controller.orientation.z == 0.0F &&
        profile.held_pose_from_controller.orientation.w == 1.0F &&
        profile.insertion_guide_pose_from_anchor.translation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.w == 1.0F &&
        profile.insertion_radius_units == 9.0F;
}

// The upgraded Zombie StG 44 is a separately accepted 60-round retail asset.
// Its fifth surface is a rifle-body detail, while the magazine and charging
// handle retain the base weapon's accepted single-piece geometry.
[[nodiscard]] constexpr bool validate_exact_zombie_stg44_upgraded_profile(
    const DetachableMagazineWeaponProfile& profile) noexcept {
    using detachable_magazine_detail::equal_strings;
    const auto& mesh = profile.mesh;
    const auto& charging = profile.charging;
    return validate_detachable_magazine_weapon_profile(profile) &&
        profile.id ==
            DetachableMagazineWeaponProfileId::ZombieStg44Upgraded &&
        equal_strings(profile.diagnostic_name,
                      "Upgraded zombie StG 44") &&
        equal_strings(profile.internal_weapon_name,
                      "zombie_stg44_upgraded") &&
        equal_strings(profile.viewmodel_model_name,
                      "viewmodel_zombie_mp44_lmg_up") &&
        equal_strings(profile.magazine_material_name,
                      "mc/mtl_weapon_mp_mp44") &&
        equal_strings(profile.magazine_bone_tag_name, "j_clip") &&
        profile.expected_clip_size == 60 &&
        profile.requires_embedded_viewmodel_magazine &&
        charging.enabled &&
        equal_strings(charging.handle_bone_tag_name, "j_bolt") &&
        equal_strings(charging.surface_material_name,
                      "mc/mtl_weapon_mp_mp44") &&
        charging.manipulating_hand == MagazineChargingHand::Right &&
        !charging.suppress_native_pose_always &&
        charging.expected_model_bone_count == 6 &&
        charging.expected_model_surface_count == 5 &&
        charging.handle_bone_index == 1 &&
        charging.parent_bone_index == 0 &&
        charging.source_surface_index == 2 &&
        charging.source_surface_rigid_subrange_count == 1 &&
        charging.source_rigid_subrange_index == 0 &&
        charging.source_surface_vertex_count == 304 &&
        charging.source_surface_triangle_count == 322 &&
        charging.rigid_vertex_offset == 0 &&
        charging.rigid_vertex_count == 304 &&
        charging.rigid_triangle_offset == 0 &&
        charging.rigid_triangle_count == 321 &&
        charging.additional_piece_count == 0 &&
        charging.bind_pose.translation.x == 4.690453F &&
        charging.bind_pose.translation.y == 0.134476F &&
        charging.bind_pose.translation.z == 3.205951F &&
        charging.bind_pose.orientation.x == 0.0F &&
        charging.bind_pose.orientation.y == -0.010224F &&
        charging.bind_pose.orientation.z == 0.0F &&
        charging.bind_pose.orientation.w == 0.999939F &&
        charging.interaction.travel_units == 3.10F &&
        charging.interaction.locked_open_offset_units == 0.0F &&
        charging.interaction.open_threshold == 0.98F &&
        charging.interaction.spring_return_seconds == 1.0F / 6.0F &&
        charging.interaction.trigger_engage == 0.65F &&
        charging.interaction.trigger_release == 0.35F &&
        charging.interaction.completion ==
            MagazineChargingCompletion::SpringClosed &&
        charging.interaction.return_sample_count == 5 &&
        charging.interaction.return_samples[0] == 1.0F &&
        charging.interaction.return_samples[1] == 0.82F &&
        charging.interaction.return_samples[2] == 0.48F &&
        charging.interaction.return_samples[3] == 0.14F &&
        charging.interaction.return_samples[4] == 0.0F &&
        charging.grab_radius_units == 10.0F &&
        mesh.expected_model_bone_count == 6 &&
        mesh.expected_model_surface_count == 5 &&
        mesh.magazine_bone_index == 2 &&
        mesh.source_surface_index == 1 &&
        mesh.source_surface_rigid_subrange_count == 1 &&
        mesh.source_rigid_subrange_index == 0 &&
        mesh.source_surface_vertex_count == 202 &&
        mesh.source_surface_triangle_count == 152 &&
        mesh.vertex_offset == 0 && mesh.vertex_count == 202 &&
        mesh.triangle_offset == 0 && mesh.triangle_count == 151 &&
        mesh.additional_piece_count == 0 &&
        mesh.bind_pose.translation.x == 6.639460F &&
        mesh.bind_pose.translation.y == 0.015012F &&
        mesh.bind_pose.translation.z == 1.835601F &&
        mesh.bind_pose.orientation.x == 0.0F &&
        mesh.bind_pose.orientation.y == 0.0F &&
        mesh.bind_pose.orientation.z == 0.0F &&
        mesh.bind_pose.orientation.w == 1.0F &&
        profile.held_pose_from_controller.translation.x == 3.25F &&
        profile.held_pose_from_controller.translation.y == 0.0F &&
        profile.held_pose_from_controller.translation.z == 1.25F &&
        profile.held_pose_from_controller.orientation.x == 0.0F &&
        profile.held_pose_from_controller.orientation.y == 0.0F &&
        profile.held_pose_from_controller.orientation.z == 0.0F &&
        profile.held_pose_from_controller.orientation.w == 1.0F &&
        profile.insertion_guide_pose_from_anchor.translation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.w == 1.0F &&
        profile.insertion_radius_units == 9.0F;
}

namespace detachable_magazine_detail {

// The two Zombie Thompson assets intentionally share one audited rigid recipe.
// Keep the identity/material/surface-count differences explicit at each public
// entry point while locking every inherited extraction and interaction value.
[[nodiscard]] constexpr bool validate_exact_zombie_thompson_profile_common(
    const DetachableMagazineWeaponProfile& profile,
    const DetachableMagazineWeaponProfileId expected_id,
    const char* const expected_diagnostic_name,
    const char* const expected_internal_weapon_name,
    const char* const expected_viewmodel_model_name,
    const char* const expected_material_name,
    const std::int32_t expected_clip_size,
    const std::uint8_t expected_model_surface_count) noexcept {
    const auto& mesh = profile.mesh;
    const auto& charging = profile.charging;
    return validate_detachable_magazine_weapon_profile(profile) &&
        profile.id == expected_id &&
        equal_strings(profile.diagnostic_name, expected_diagnostic_name) &&
        equal_strings(profile.internal_weapon_name,
                      expected_internal_weapon_name) &&
        equal_strings(profile.viewmodel_model_name,
                      expected_viewmodel_model_name) &&
        equal_strings(profile.magazine_material_name,
                      expected_material_name) &&
        equal_strings(profile.magazine_bone_tag_name, "j_clip") &&
        profile.expected_clip_size == expected_clip_size &&
        profile.requires_embedded_viewmodel_magazine &&
        charging.enabled &&
        equal_strings(charging.handle_bone_tag_name, "j_bolt") &&
        equal_strings(charging.surface_material_name,
                      expected_material_name) &&
        charging.manipulating_hand == MagazineChargingHand::Right &&
        !charging.suppress_native_pose_always &&
        charging.expected_model_bone_count == 5 &&
        charging.expected_model_surface_count ==
            expected_model_surface_count &&
        charging.handle_bone_index == 1 &&
        charging.parent_bone_index == 0 &&
        charging.source_surface_index == 1 &&
        charging.source_surface_rigid_subrange_count == 1 &&
        charging.source_rigid_subrange_index == 0 &&
        charging.source_surface_vertex_count == 219 &&
        charging.source_surface_triangle_count == 172 &&
        charging.rigid_vertex_offset == 0 &&
        charging.rigid_vertex_count == 219 &&
        charging.rigid_triangle_offset == 0 &&
        charging.rigid_triangle_count == 172 &&
        charging.additional_piece_count == 0 &&
        charging.bind_pose.translation.x == 1.304120F &&
        charging.bind_pose.translation.y == -0.912230F &&
        charging.bind_pose.translation.z == 2.115713F &&
        charging.bind_pose.orientation.x == 0.0F &&
        charging.bind_pose.orientation.y == -0.010224F &&
        charging.bind_pose.orientation.z == 0.0F &&
        charging.bind_pose.orientation.w == 0.999939F &&
        charging.interaction.travel_units == 3.00F &&
        charging.interaction.locked_open_offset_units == 0.0F &&
        charging.interaction.open_threshold == 0.98F &&
        charging.interaction.spring_return_seconds == 0.0F &&
        charging.interaction.trigger_engage == 0.65F &&
        charging.interaction.trigger_release == 0.35F &&
        charging.interaction.completion ==
            MagazineChargingCompletion::LatchOpen &&
        charging.interaction.return_sample_count == 0 &&
        charging.grab_radius_units == 10.0F &&
        mesh.expected_model_bone_count == 5 &&
        mesh.expected_model_surface_count == expected_model_surface_count &&
        mesh.magazine_bone_index == 2 &&
        mesh.source_surface_index == 2 &&
        mesh.source_surface_rigid_subrange_count == 1 &&
        mesh.source_rigid_subrange_index == 0 &&
        mesh.source_surface_vertex_count == 722 &&
        mesh.source_surface_triangle_count == 414 &&
        mesh.vertex_offset == 0 && mesh.vertex_count == 722 &&
        mesh.triangle_offset == 0 && mesh.triangle_count == 414 &&
        mesh.additional_piece_count == 0 &&
        mesh.bind_pose.translation.x == 3.463230F &&
        mesh.bind_pose.translation.y == 0.126403F &&
        mesh.bind_pose.translation.z == -0.144167F &&
        mesh.bind_pose.orientation.x == 0.0F &&
        mesh.bind_pose.orientation.y == 0.0F &&
        mesh.bind_pose.orientation.z == 0.0F &&
        mesh.bind_pose.orientation.w == 1.0F &&
        profile.held_pose_from_controller.translation.x == 3.25F &&
        profile.held_pose_from_controller.translation.y == 0.0F &&
        profile.held_pose_from_controller.translation.z == 1.25F &&
        profile.held_pose_from_controller.orientation.x == 0.0F &&
        profile.held_pose_from_controller.orientation.y == 0.0F &&
        profile.held_pose_from_controller.orientation.z == 0.0F &&
        profile.held_pose_from_controller.orientation.w == 1.0F &&
        profile.insertion_guide_pose_from_anchor.translation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.w == 1.0F &&
        profile.insertion_radius_units == 9.0F;
}

}  // namespace detachable_magazine_detail

// The base Zombie Thompson is locked to the physically accepted Der Riese
// asset, including its open-bolt charging behavior and one-piece magazine and
// charging-handle recipes.
[[nodiscard]] constexpr bool validate_exact_zombie_thompson_profile(
    const DetachableMagazineWeaponProfile& profile) noexcept {
    return detachable_magazine_detail::
        validate_exact_zombie_thompson_profile_common(
            profile,
            DetachableMagazineWeaponProfileId::ZombieThompson,
            "Zombie Thompson",
            "zombie_thompson",
            "viewmodel_zombie_thompson_smg",
            "mc/mtl_weapon_mp_thompson",
            20,
            4);
}

// The upgraded Zombie Thompson is separately locked to the physically accepted
// 40-round Der Riese retail asset. Its fifth surface is weapon-body detail, so
// both manipulated meshes remain single-piece recipes.
[[nodiscard]] constexpr bool validate_exact_zombie_thompson_upgraded_profile(
    const DetachableMagazineWeaponProfile& profile) noexcept {
    return detachable_magazine_detail::
        validate_exact_zombie_thompson_profile_common(
            profile,
            DetachableMagazineWeaponProfileId::ZombieThompsonUpgraded,
            "Upgraded zombie Thompson",
            "zombie_thompson_upgraded",
            "viewmodel_zombie_thompson_smg_up",
            "mc/mtl_weapon_thompson_gold",
            40,
            5);
}

// This locks the base 32-round Zombie MP40 to the physically accepted Der
// Riese retail asset. Its magazine moves to surface two while the charging
// handle remains the single piece on surface one, so any inherited
// Nacht-topology drift fails closed.
[[nodiscard]] constexpr bool validate_exact_zombie_mp40_profile(
    const DetachableMagazineWeaponProfile& profile) noexcept {
    using detachable_magazine_detail::equal_strings;
    const auto& mesh = profile.mesh;
    const auto& charging = profile.charging;
    return validate_detachable_magazine_weapon_profile(profile) &&
        profile.id == DetachableMagazineWeaponProfileId::ZombieMp40 &&
        equal_strings(profile.diagnostic_name, "Zombie MP40") &&
        equal_strings(profile.internal_weapon_name, "zombie_mp40") &&
        equal_strings(profile.viewmodel_model_name,
                      "viewmodel_zombie_mp40_smg") &&
        equal_strings(profile.magazine_material_name,
                      "mc/mtl_weapon_mp_mp40") &&
        equal_strings(profile.magazine_bone_tag_name, "j_clip") &&
        profile.expected_clip_size == 32 &&
        profile.requires_embedded_viewmodel_magazine &&
        charging.enabled &&
        equal_strings(charging.handle_bone_tag_name, "j_bolt") &&
        equal_strings(charging.surface_material_name,
                      "mc/mtl_weapon_mp_mp40") &&
        charging.manipulating_hand == MagazineChargingHand::Right &&
        !charging.suppress_native_pose_always &&
        charging.expected_model_bone_count == 5 &&
        charging.expected_model_surface_count == 3 &&
        charging.handle_bone_index == 1 &&
        charging.parent_bone_index == 0 &&
        charging.source_surface_index == 1 &&
        charging.source_surface_rigid_subrange_count == 1 &&
        charging.source_rigid_subrange_index == 0 &&
        charging.source_surface_vertex_count == 186 &&
        charging.source_surface_triangle_count == 182 &&
        charging.rigid_vertex_offset == 0 &&
        charging.rigid_vertex_count == 186 &&
        charging.rigid_triangle_offset == 0 &&
        charging.rigid_triangle_count == 182 &&
        charging.additional_piece_count == 0 &&
        charging.bind_pose.translation.x == -2.532358F &&
        charging.bind_pose.translation.y == 1.252526F &&
        charging.bind_pose.translation.z == 1.000512F &&
        charging.bind_pose.orientation.x == 0.0F &&
        charging.bind_pose.orientation.y == -0.010224F &&
        charging.bind_pose.orientation.z == 0.0F &&
        charging.bind_pose.orientation.w == 0.999939F &&
        charging.interaction.travel_units == 3.20F &&
        charging.interaction.locked_open_offset_units == 0.0F &&
        charging.interaction.open_threshold == 0.98F &&
        charging.interaction.spring_return_seconds == 0.0F &&
        charging.interaction.trigger_engage == 0.65F &&
        charging.interaction.trigger_release == 0.35F &&
        charging.interaction.completion ==
            MagazineChargingCompletion::LatchOpen &&
        charging.interaction.return_sample_count == 0 &&
        charging.grab_radius_units == 10.0F &&
        mesh.expected_model_bone_count == 5 &&
        mesh.expected_model_surface_count == 3 &&
        mesh.magazine_bone_index == 2 &&
        mesh.source_surface_index == 2 &&
        mesh.source_surface_rigid_subrange_count == 1 &&
        mesh.source_rigid_subrange_index == 0 &&
        mesh.source_surface_vertex_count == 36 &&
        mesh.source_surface_triangle_count == 18 &&
        mesh.vertex_offset == 0 && mesh.vertex_count == 36 &&
        mesh.triangle_offset == 0 && mesh.triangle_count == 18 &&
        mesh.additional_piece_count == 0 &&
        mesh.bind_pose.translation.x == 4.207038F &&
        mesh.bind_pose.translation.y == 0.408818F &&
        mesh.bind_pose.translation.z == -0.275403F &&
        mesh.bind_pose.orientation.x == 0.0F &&
        mesh.bind_pose.orientation.y == 0.0F &&
        mesh.bind_pose.orientation.z == 0.0F &&
        mesh.bind_pose.orientation.w == 1.0F &&
        profile.held_pose_from_controller.translation.x == 3.25F &&
        profile.held_pose_from_controller.translation.y == 0.0F &&
        profile.held_pose_from_controller.translation.z == 1.25F &&
        profile.held_pose_from_controller.orientation.x == 0.0F &&
        profile.held_pose_from_controller.orientation.y == 0.0F &&
        profile.held_pose_from_controller.orientation.z == 0.0F &&
        profile.held_pose_from_controller.orientation.w == 1.0F &&
        profile.insertion_guide_pose_from_anchor.translation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.w == 1.0F &&
        profile.insertion_radius_units == 9.0F;
}

// This locks the explicitly inventoried upgraded Zombie MP40 as a simulator
// regression candidate only; physical acceptance remains separate. The
// five-surface 64-round asset keeps the base magazine piece and adds one exact
// silver-etching piece to its gold open-bolt charging handle.
[[nodiscard]] constexpr bool validate_exact_zombie_mp40_upgraded_profile(
    const DetachableMagazineWeaponProfile& profile) noexcept {
    using detachable_magazine_detail::equal_strings;
    const auto& mesh = profile.mesh;
    const auto& charging = profile.charging;
    const auto& charging_detail = charging.additional_pieces[0];
    return validate_detachable_magazine_weapon_profile(profile) &&
        profile.id ==
            DetachableMagazineWeaponProfileId::ZombieMp40Upgraded &&
        equal_strings(profile.diagnostic_name, "Upgraded zombie MP40") &&
        equal_strings(profile.internal_weapon_name,
                      "zombie_mp40_upgraded") &&
        equal_strings(profile.viewmodel_model_name,
                      "viewmodel_zombie_mp40_smg_up") &&
        equal_strings(profile.magazine_material_name,
                      "mc/mtl_weapon_mp40_gold") &&
        equal_strings(profile.magazine_bone_tag_name, "j_clip") &&
        profile.expected_clip_size == 64 &&
        profile.requires_embedded_viewmodel_magazine &&
        charging.enabled &&
        equal_strings(charging.handle_bone_tag_name, "j_bolt") &&
        equal_strings(charging.surface_material_name,
                      "mc/mtl_weapon_mp40_gold") &&
        charging.manipulating_hand == MagazineChargingHand::Right &&
        !charging.suppress_native_pose_always &&
        charging.expected_model_bone_count == 5 &&
        charging.expected_model_surface_count == 5 &&
        charging.handle_bone_index == 1 &&
        charging.parent_bone_index == 0 &&
        charging.source_surface_index == 1 &&
        charging.source_surface_rigid_subrange_count == 1 &&
        charging.source_rigid_subrange_index == 0 &&
        charging.source_surface_vertex_count == 186 &&
        charging.source_surface_triangle_count == 182 &&
        charging.rigid_vertex_offset == 0 &&
        charging.rigid_vertex_count == 186 &&
        charging.rigid_triangle_offset == 0 &&
        charging.rigid_triangle_count == 182 &&
        charging.additional_piece_count == 1 &&
        equal_strings(charging_detail.material_name,
                      "mc/mtl_silver_etching") &&
        charging_detail.source_surface_index == 3 &&
        charging_detail.source_surface_rigid_subrange_count == 1 &&
        charging_detail.source_rigid_subrange_index == 0 &&
        charging_detail.source_surface_vertex_count == 30 &&
        charging_detail.source_surface_triangle_count == 26 &&
        charging_detail.vertex_offset == 0 &&
        charging_detail.vertex_count == 30 &&
        charging_detail.triangle_offset == 0 &&
        charging_detail.triangle_count == 26 &&
        charging.bind_pose.translation.x == -2.532358F &&
        charging.bind_pose.translation.y == 1.252526F &&
        charging.bind_pose.translation.z == 1.000512F &&
        charging.bind_pose.orientation.x == 0.0F &&
        charging.bind_pose.orientation.y == -0.010224F &&
        charging.bind_pose.orientation.z == 0.0F &&
        charging.bind_pose.orientation.w == 0.999939F &&
        charging.interaction.travel_units == 3.20F &&
        charging.interaction.locked_open_offset_units == 0.0F &&
        charging.interaction.open_threshold == 0.98F &&
        charging.interaction.spring_return_seconds == 0.0F &&
        charging.interaction.trigger_engage == 0.65F &&
        charging.interaction.trigger_release == 0.35F &&
        charging.interaction.completion ==
            MagazineChargingCompletion::LatchOpen &&
        charging.interaction.return_sample_count == 0 &&
        charging.grab_radius_units == 10.0F &&
        mesh.expected_model_bone_count == 5 &&
        mesh.expected_model_surface_count == 5 &&
        mesh.magazine_bone_index == 2 &&
        mesh.source_surface_index == 2 &&
        mesh.source_surface_rigid_subrange_count == 1 &&
        mesh.source_rigid_subrange_index == 0 &&
        mesh.source_surface_vertex_count == 36 &&
        mesh.source_surface_triangle_count == 18 &&
        mesh.vertex_offset == 0 && mesh.vertex_count == 36 &&
        mesh.triangle_offset == 0 && mesh.triangle_count == 18 &&
        mesh.additional_piece_count == 0 &&
        mesh.bind_pose.translation.x == 4.207038F &&
        mesh.bind_pose.translation.y == 0.408818F &&
        mesh.bind_pose.translation.z == -0.275403F &&
        mesh.bind_pose.orientation.x == 0.0F &&
        mesh.bind_pose.orientation.y == 0.0F &&
        mesh.bind_pose.orientation.z == 0.0F &&
        mesh.bind_pose.orientation.w == 1.0F &&
        profile.held_pose_from_controller.translation.x == 3.25F &&
        profile.held_pose_from_controller.translation.y == 0.0F &&
        profile.held_pose_from_controller.translation.z == 1.25F &&
        profile.held_pose_from_controller.orientation.x == 0.0F &&
        profile.held_pose_from_controller.orientation.y == 0.0F &&
        profile.held_pose_from_controller.orientation.z == 0.0F &&
        profile.held_pose_from_controller.orientation.w == 1.0F &&
        profile.insertion_guide_pose_from_anchor.translation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.w == 1.0F &&
        profile.insertion_radius_units == 9.0F;
}

// This locks the base 30-round Zombie Type 100 to the physically accepted Der
// Riese retail asset. The metal material is also used by the weapon body, so
// retain the exact bone-owned magazine and charging surfaces rather than
// accepting a material-only match.
[[nodiscard]] constexpr bool validate_exact_zombie_type100_profile(
    const DetachableMagazineWeaponProfile& profile) noexcept {
    using detachable_magazine_detail::equal_strings;
    const auto& mesh = profile.mesh;
    const auto& charging = profile.charging;
    return validate_detachable_magazine_weapon_profile(profile) &&
        profile.id == DetachableMagazineWeaponProfileId::ZombieType100 &&
        equal_strings(profile.diagnostic_name, "Zombie Type 100 SMG") &&
        equal_strings(profile.internal_weapon_name,
                      "zombie_type100_smg") &&
        equal_strings(profile.viewmodel_model_name,
                      "viewmodel_zombie_type100_smg") &&
        equal_strings(profile.magazine_material_name,
                      "mc/mtl_weapon_mp_type100_metal") &&
        equal_strings(profile.magazine_bone_tag_name, "j_clip") &&
        profile.expected_clip_size == 30 &&
        profile.requires_embedded_viewmodel_magazine &&
        charging.enabled &&
        equal_strings(charging.handle_bone_tag_name, "j_bolt") &&
        equal_strings(charging.surface_material_name,
                      "mc/mtl_weapon_mp_type100_metal") &&
        charging.manipulating_hand == MagazineChargingHand::Right &&
        !charging.suppress_native_pose_always &&
        charging.expected_model_bone_count == 6 &&
        charging.expected_model_surface_count == 4 &&
        charging.handle_bone_index == 1 &&
        charging.parent_bone_index == 0 &&
        charging.source_surface_index == 2 &&
        charging.source_surface_rigid_subrange_count == 1 &&
        charging.source_rigid_subrange_index == 0 &&
        charging.source_surface_vertex_count == 314 &&
        charging.source_surface_triangle_count == 298 &&
        charging.rigid_vertex_offset == 0 &&
        charging.rigid_vertex_count == 314 &&
        charging.rigid_triangle_offset == 0 &&
        charging.rigid_triangle_count == 298 &&
        charging.additional_piece_count == 0 &&
        charging.bind_pose.translation.x == -1.465501F &&
        charging.bind_pose.translation.y == -0.620103F &&
        charging.bind_pose.translation.z == 1.750709F &&
        charging.bind_pose.orientation.x == 0.0F &&
        charging.bind_pose.orientation.y == -0.010224F &&
        charging.bind_pose.orientation.z == 0.0F &&
        charging.bind_pose.orientation.w == 0.999939F &&
        charging.interaction.travel_units == 3.00F &&
        charging.interaction.locked_open_offset_units == 0.0F &&
        charging.interaction.open_threshold == 0.98F &&
        charging.interaction.spring_return_seconds == 0.0F &&
        charging.interaction.trigger_engage == 0.65F &&
        charging.interaction.trigger_release == 0.35F &&
        charging.interaction.completion ==
            MagazineChargingCompletion::LatchOpen &&
        charging.interaction.return_sample_count == 0 &&
        charging.grab_radius_units == 10.0F &&
        mesh.expected_model_bone_count == 6 &&
        mesh.expected_model_surface_count == 4 &&
        mesh.magazine_bone_index == 2 &&
        mesh.source_surface_index == 1 &&
        mesh.source_surface_rigid_subrange_count == 1 &&
        mesh.source_rigid_subrange_index == 0 &&
        mesh.source_surface_vertex_count == 346 &&
        mesh.source_surface_triangle_count == 374 &&
        mesh.vertex_offset == 0 && mesh.vertex_count == 346 &&
        mesh.triangle_offset == 0 && mesh.triangle_count == 374 &&
        mesh.additional_piece_count == 0 &&
        mesh.bind_pose.translation.x == 2.351090F &&
        mesh.bind_pose.translation.y == 2.122774F &&
        mesh.bind_pose.translation.z == 1.516471F &&
        mesh.bind_pose.orientation.x == 0.0F &&
        mesh.bind_pose.orientation.y == 0.0F &&
        mesh.bind_pose.orientation.z == 0.0F &&
        mesh.bind_pose.orientation.w == 1.0F &&
        profile.held_pose_from_controller.translation.x == 3.25F &&
        profile.held_pose_from_controller.translation.y == 0.0F &&
        profile.held_pose_from_controller.translation.z == 1.25F &&
        profile.held_pose_from_controller.orientation.x == 0.0F &&
        profile.held_pose_from_controller.orientation.y == 0.0F &&
        profile.held_pose_from_controller.orientation.z == 0.0F &&
        profile.held_pose_from_controller.orientation.w == 1.0F &&
        profile.insertion_guide_pose_from_anchor.translation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.w == 1.0F &&
        profile.insertion_radius_units == 9.0F;
}

// This locks the base 20-round Zombie BAR to the physically accepted Der Riese
// retail asset. Surface zero contains two rigid owners and the metal material
// also covers the body, so select only j_clip's rigid zero and j_bolt's exact
// surface-four range.
[[nodiscard]] constexpr bool validate_exact_zombie_bar_profile(
    const DetachableMagazineWeaponProfile& profile) noexcept {
    using detachable_magazine_detail::equal_strings;
    const auto& mesh = profile.mesh;
    const auto& charging = profile.charging;
    return validate_detachable_magazine_weapon_profile(profile) &&
        profile.id == DetachableMagazineWeaponProfileId::ZombieBar &&
        equal_strings(profile.diagnostic_name, "Zombie BAR") &&
        equal_strings(profile.internal_weapon_name, "zombie_bar") &&
        equal_strings(profile.viewmodel_model_name,
                      "viewmodel_zombie_bar_lmg") &&
        equal_strings(profile.magazine_material_name,
                      "mc/mtl_weapon_mp_bar") &&
        equal_strings(profile.magazine_bone_tag_name, "j_clip") &&
        profile.expected_clip_size == 20 &&
        profile.requires_embedded_viewmodel_magazine &&
        charging.enabled &&
        equal_strings(charging.handle_bone_tag_name, "j_bolt") &&
        equal_strings(charging.surface_material_name,
                      "mc/mtl_weapon_mp_bar") &&
        charging.manipulating_hand == MagazineChargingHand::Right &&
        !charging.suppress_native_pose_always &&
        charging.expected_model_bone_count == 8 &&
        charging.expected_model_surface_count == 6 &&
        charging.handle_bone_index == 1 &&
        charging.parent_bone_index == 0 &&
        charging.source_surface_index == 4 &&
        charging.source_surface_rigid_subrange_count == 1 &&
        charging.source_rigid_subrange_index == 0 &&
        charging.source_surface_vertex_count == 536 &&
        charging.source_surface_triangle_count == 574 &&
        charging.rigid_vertex_offset == 0 &&
        charging.rigid_vertex_count == 536 &&
        charging.rigid_triangle_offset == 0 &&
        charging.rigid_triangle_count == 573 &&
        charging.additional_piece_count == 0 &&
        charging.bind_pose.translation.x == -2.240462F &&
        charging.bind_pose.translation.y == 1.697357F &&
        charging.bind_pose.translation.z == 0.228652F &&
        charging.bind_pose.orientation.x == 0.0F &&
        charging.bind_pose.orientation.y == -0.010224F &&
        charging.bind_pose.orientation.z == 0.0F &&
        charging.bind_pose.orientation.w == 0.999939F &&
        charging.interaction.travel_units == 3.40F &&
        charging.interaction.locked_open_offset_units == 0.0F &&
        charging.interaction.open_threshold == 0.98F &&
        charging.interaction.spring_return_seconds == 0.0F &&
        charging.interaction.trigger_engage == 0.65F &&
        charging.interaction.trigger_release == 0.35F &&
        charging.interaction.completion ==
            MagazineChargingCompletion::LatchOpen &&
        charging.interaction.return_sample_count == 0 &&
        charging.grab_radius_units == 10.0F &&
        mesh.expected_model_bone_count == 8 &&
        mesh.expected_model_surface_count == 6 &&
        mesh.magazine_bone_index == 2 &&
        mesh.source_surface_index == 0 &&
        mesh.source_surface_rigid_subrange_count == 2 &&
        mesh.source_rigid_subrange_index == 0 &&
        mesh.source_surface_vertex_count == 76 &&
        mesh.source_surface_triangle_count == 40 &&
        mesh.vertex_offset == 0 && mesh.vertex_count == 24 &&
        mesh.triangle_offset == 0 && mesh.triangle_count == 12 &&
        mesh.additional_piece_count == 0 &&
        mesh.bind_pose.translation.x == -0.218617F &&
        mesh.bind_pose.translation.y == -0.242085F &&
        mesh.bind_pose.translation.z == -0.715188F &&
        mesh.bind_pose.orientation.x == 0.0F &&
        mesh.bind_pose.orientation.y == 0.0F &&
        mesh.bind_pose.orientation.z == 0.0F &&
        mesh.bind_pose.orientation.w == 1.0F &&
        profile.held_pose_from_controller.translation.x == 3.25F &&
        profile.held_pose_from_controller.translation.y == 0.0F &&
        profile.held_pose_from_controller.translation.z == 1.25F &&
        profile.held_pose_from_controller.orientation.x == 0.0F &&
        profile.held_pose_from_controller.orientation.y == 0.0F &&
        profile.held_pose_from_controller.orientation.z == 0.0F &&
        profile.held_pose_from_controller.orientation.w == 1.0F &&
        profile.insertion_guide_pose_from_anchor.translation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.w == 1.0F &&
        profile.insertion_radius_units == 9.0F;
}

// This locks the base 32-round Zombie FG42 to the physically accepted Der
// Riese retail asset. Its metal material spans multiple body surfaces, while
// charging surface zero contains a bone-zero sliver before j_bolt, so preserve
// the exact bone-owned ranges.
[[nodiscard]] constexpr bool validate_exact_zombie_fg42_profile(
    const DetachableMagazineWeaponProfile& profile) noexcept {
    using detachable_magazine_detail::equal_strings;
    const auto& mesh = profile.mesh;
    const auto& charging = profile.charging;
    return validate_detachable_magazine_weapon_profile(profile) &&
        profile.id == DetachableMagazineWeaponProfileId::ZombieFg42 &&
        equal_strings(profile.diagnostic_name, "Zombie FG42") &&
        equal_strings(profile.internal_weapon_name, "zombie_fg42") &&
        equal_strings(profile.viewmodel_model_name,
                      "viewmodel_zombie_fg42_lmg") &&
        equal_strings(profile.magazine_material_name,
                      "mc/mtl_weapon_mp_fg42") &&
        equal_strings(profile.magazine_bone_tag_name, "j_clip") &&
        profile.expected_clip_size == 32 &&
        profile.requires_embedded_viewmodel_magazine &&
        charging.enabled &&
        equal_strings(charging.handle_bone_tag_name, "j_bolt") &&
        equal_strings(charging.surface_material_name,
                      "mc/mtl_weapon_mp_fg42") &&
        charging.manipulating_hand == MagazineChargingHand::Right &&
        !charging.suppress_native_pose_always &&
        charging.expected_model_bone_count == 8 &&
        charging.expected_model_surface_count == 7 &&
        charging.handle_bone_index == 1 &&
        charging.parent_bone_index == 0 &&
        charging.source_surface_index == 0 &&
        charging.source_surface_rigid_subrange_count == 2 &&
        charging.source_rigid_subrange_index == 1 &&
        charging.source_surface_vertex_count == 127 &&
        charging.source_surface_triangle_count == 92 &&
        charging.rigid_vertex_offset == 4 &&
        charging.rigid_vertex_count == 123 &&
        charging.rigid_triangle_offset == 2 &&
        charging.rigid_triangle_count == 90 &&
        charging.additional_piece_count == 0 &&
        charging.bind_pose.translation.x == 7.663996F &&
        charging.bind_pose.translation.y == -1.964798F &&
        charging.bind_pose.translation.z == 1.898179F &&
        charging.bind_pose.orientation.x == 0.0F &&
        charging.bind_pose.orientation.y == -0.010224F &&
        charging.bind_pose.orientation.z == 0.0F &&
        charging.bind_pose.orientation.w == 0.999939F &&
        charging.interaction.travel_units == 3.10F &&
        charging.interaction.locked_open_offset_units == 0.0F &&
        charging.interaction.open_threshold == 0.98F &&
        charging.interaction.spring_return_seconds == 0.0F &&
        charging.interaction.trigger_engage == 0.65F &&
        charging.interaction.trigger_release == 0.35F &&
        charging.interaction.completion ==
            MagazineChargingCompletion::LatchOpen &&
        charging.interaction.return_sample_count == 0 &&
        charging.grab_radius_units == 10.0F &&
        mesh.expected_model_bone_count == 8 &&
        mesh.expected_model_surface_count == 7 &&
        mesh.magazine_bone_index == 2 &&
        mesh.source_surface_index == 5 &&
        mesh.source_surface_rigid_subrange_count == 1 &&
        mesh.source_rigid_subrange_index == 0 &&
        mesh.source_surface_vertex_count == 358 &&
        mesh.source_surface_triangle_count == 230 &&
        mesh.vertex_offset == 0 && mesh.vertex_count == 358 &&
        mesh.triangle_offset == 0 && mesh.triangle_count == 230 &&
        mesh.additional_piece_count == 0 &&
        mesh.bind_pose.translation.x == 0.760868F &&
        mesh.bind_pose.translation.y == 0.981267F &&
        mesh.bind_pose.translation.z == 2.555799F &&
        mesh.bind_pose.orientation.x == 0.0F &&
        mesh.bind_pose.orientation.y == 0.0F &&
        mesh.bind_pose.orientation.z == 0.0F &&
        mesh.bind_pose.orientation.w == 1.0F &&
        profile.held_pose_from_controller.translation.x == 3.25F &&
        profile.held_pose_from_controller.translation.y == 0.0F &&
        profile.held_pose_from_controller.translation.z == 1.25F &&
        profile.held_pose_from_controller.orientation.x == 0.0F &&
        profile.held_pose_from_controller.orientation.y == 0.0F &&
        profile.held_pose_from_controller.orientation.z == 0.0F &&
        profile.held_pose_from_controller.orientation.w == 1.0F &&
        profile.insertion_guide_pose_from_anchor.translation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.w == 1.0F &&
        profile.insertion_radius_units == 9.0F;
}

// This locks the base 71-round Zombie PPSh-41 to the physically accepted Der
// Riese retail asset. Its metal material spans every model surface, so retain
// j_clip and j_bolt's exact bone-owned surfaces instead of accepting a
// material-only match.
[[nodiscard]] constexpr bool validate_exact_zombie_ppsh_profile(
    const DetachableMagazineWeaponProfile& profile) noexcept {
    using detachable_magazine_detail::equal_strings;
    const auto& mesh = profile.mesh;
    const auto& charging = profile.charging;
    return validate_detachable_magazine_weapon_profile(profile) &&
        profile.id == DetachableMagazineWeaponProfileId::ZombiePpsh &&
        equal_strings(profile.diagnostic_name, "Zombie PPSh-41") &&
        equal_strings(profile.internal_weapon_name, "zombie_ppsh") &&
        equal_strings(profile.viewmodel_model_name,
                      "viewmodel_zombie_ppsh_smg") &&
        equal_strings(profile.magazine_material_name,
                      "mc/mtl_weapon_mp_ppsh41") &&
        equal_strings(profile.magazine_bone_tag_name, "j_clip") &&
        profile.expected_clip_size == 71 &&
        profile.requires_embedded_viewmodel_magazine &&
        charging.enabled &&
        equal_strings(charging.handle_bone_tag_name, "j_bolt") &&
        equal_strings(charging.surface_material_name,
                      "mc/mtl_weapon_mp_ppsh41") &&
        charging.manipulating_hand == MagazineChargingHand::Right &&
        !charging.suppress_native_pose_always &&
        charging.expected_model_bone_count == 7 &&
        charging.expected_model_surface_count == 5 &&
        charging.handle_bone_index == 1 &&
        charging.parent_bone_index == 0 &&
        charging.source_surface_index == 1 &&
        charging.source_surface_rigid_subrange_count == 1 &&
        charging.source_rigid_subrange_index == 0 &&
        charging.source_surface_vertex_count == 196 &&
        charging.source_surface_triangle_count == 140 &&
        charging.rigid_vertex_offset == 0 &&
        charging.rigid_vertex_count == 196 &&
        charging.rigid_triangle_offset == 0 &&
        charging.rigid_triangle_count == 140 &&
        charging.additional_piece_count == 0 &&
        charging.bind_pose.translation.x == 0.145243F &&
        charging.bind_pose.translation.y == -0.907277F &&
        charging.bind_pose.translation.z == 1.807249F &&
        charging.bind_pose.orientation.x == 0.0F &&
        charging.bind_pose.orientation.y == -0.010224F &&
        charging.bind_pose.orientation.z == 0.0F &&
        charging.bind_pose.orientation.w == 0.999939F &&
        charging.interaction.travel_units == 3.00F &&
        charging.interaction.locked_open_offset_units == 0.0F &&
        charging.interaction.open_threshold == 0.98F &&
        charging.interaction.spring_return_seconds == 0.0F &&
        charging.interaction.trigger_engage == 0.65F &&
        charging.interaction.trigger_release == 0.35F &&
        charging.interaction.completion ==
            MagazineChargingCompletion::LatchOpen &&
        charging.interaction.return_sample_count == 0 &&
        charging.grab_radius_units == 10.0F &&
        mesh.expected_model_bone_count == 7 &&
        mesh.expected_model_surface_count == 5 &&
        mesh.magazine_bone_index == 2 &&
        mesh.source_surface_index == 3 &&
        mesh.source_surface_rigid_subrange_count == 1 &&
        mesh.source_rigid_subrange_index == 0 &&
        mesh.source_surface_vertex_count == 292 &&
        mesh.source_surface_triangle_count == 272 &&
        mesh.vertex_offset == 0 && mesh.vertex_count == 292 &&
        mesh.triangle_offset == 0 && mesh.triangle_count == 272 &&
        mesh.additional_piece_count == 0 &&
        mesh.bind_pose.translation.x == 2.162864F &&
        mesh.bind_pose.translation.y == 0.001309F &&
        mesh.bind_pose.translation.z == 0.558384F &&
        mesh.bind_pose.orientation.x == 0.0F &&
        mesh.bind_pose.orientation.y == 0.0F &&
        mesh.bind_pose.orientation.z == 0.0F &&
        mesh.bind_pose.orientation.w == 1.0F &&
        profile.held_pose_from_controller.translation.x == 3.25F &&
        profile.held_pose_from_controller.translation.y == 0.0F &&
        profile.held_pose_from_controller.translation.z == 1.25F &&
        profile.held_pose_from_controller.orientation.x == 0.0F &&
        profile.held_pose_from_controller.orientation.y == 0.0F &&
        profile.held_pose_from_controller.orientation.z == 0.0F &&
        profile.held_pose_from_controller.orientation.w == 1.0F &&
        profile.insertion_guide_pose_from_anchor.translation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.translation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.x == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.y == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.z == 0.0F &&
        profile.insertion_guide_pose_from_anchor.orientation.w == 1.0F &&
        profile.insertion_radius_units == 9.0F;
}

static_assert(validate_detachable_magazine_weapon_profile(
    kZombieColtDetachableMagazineWeaponProfile));
static_assert(validate_exact_zombie_colt_profile(
    kZombieColtDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kM1CarbineDetachableMagazineWeaponProfile));
static_assert(validate_exact_m1carbine_profile(
    kM1CarbineDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kM1GarandDetachableMagazineWeaponProfile));
static_assert(validate_exact_m1garand_profile(
    kM1GarandDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kM1GarandBayonetDetachableMagazineWeaponProfile));
static_assert(validate_exact_m1garand_bayonet_profile(
    kM1GarandBayonetDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kM1GarandGrenadeLauncherDetachableMagazineWeaponProfile));
static_assert(validate_exact_m1garand_grenade_launcher_profile(
    kM1GarandGrenadeLauncherDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kColtDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kGewehr43DetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kStg44DetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kMp40DetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kThompsonDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kBarDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kFg42BipodDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kWaltherDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kTokarevDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kNambuDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kSvt40DetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kPpshDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kType100DetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kType99LmgDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kType99LmgBipodDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kType100NoSoundDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kThompsonWetDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kColtWetDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kBarBipodDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kZombieColtDedicatedDetachableMagazineWeaponProfile));
static_assert(validate_exact_zombie_colt_dedicated_profile(
    kZombieColtDedicatedDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kZombieColtUpgradedDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kZombieM1CarbineDetachableMagazineWeaponProfile));
static_assert(validate_exact_zombie_m1carbine_profile(
    kZombieM1CarbineDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kZombieM1CarbineUpgradedDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kZombieGewehr43DetachableMagazineWeaponProfile));
static_assert(validate_exact_zombie_gewehr43_profile(
    kZombieGewehr43DetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kZombieGewehr43UpgradedDetachableMagazineWeaponProfile));
static_assert(validate_exact_zombie_gewehr43_upgraded_profile(
    kZombieGewehr43UpgradedDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kZombieStg44DetachableMagazineWeaponProfile));
static_assert(validate_exact_zombie_stg44_profile(
    kZombieStg44DetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kZombieStg44UpgradedDetachableMagazineWeaponProfile));
static_assert(validate_exact_zombie_stg44_upgraded_profile(
    kZombieStg44UpgradedDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kZombieThompsonDetachableMagazineWeaponProfile));
static_assert(validate_exact_zombie_thompson_profile(
    kZombieThompsonDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kZombieThompsonUpgradedDetachableMagazineWeaponProfile));
static_assert(validate_exact_zombie_thompson_upgraded_profile(
    kZombieThompsonUpgradedDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kZombieMp40DetachableMagazineWeaponProfile));
static_assert(validate_exact_zombie_mp40_profile(
    kZombieMp40DetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kZombieMp40UpgradedDetachableMagazineWeaponProfile));
static_assert(validate_exact_zombie_mp40_upgraded_profile(
    kZombieMp40UpgradedDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kZombieType100DetachableMagazineWeaponProfile));
static_assert(validate_exact_zombie_type100_profile(
    kZombieType100DetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kZombieType100UpgradedDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kZombieBarDetachableMagazineWeaponProfile));
static_assert(validate_exact_zombie_bar_profile(
    kZombieBarDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kZombieBarUpgradedDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kZombieFg42DetachableMagazineWeaponProfile));
static_assert(validate_exact_zombie_fg42_profile(
    kZombieFg42DetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kZombieFg42UpgradedDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kZombiePpshDetachableMagazineWeaponProfile));
static_assert(validate_exact_zombie_ppsh_profile(
    kZombiePpshDetachableMagazineWeaponProfile));
static_assert(validate_detachable_magazine_weapon_profile(
    kZombiePpshUpgradedDetachableMagazineWeaponProfile));

[[nodiscard]] std::span<const DetachableMagazineWeaponProfile* const>
detachable_magazine_weapon_profiles() noexcept;

[[nodiscard]] const DetachableMagazineWeaponProfile*
find_detachable_magazine_weapon_profile(
    DetachableMagazineWeaponProfileId id) noexcept;

// Membership does not select a profile: one exact weapon name may have
// multiple registered viewmodels. Resolve the full identity before binding.
[[nodiscard]] bool has_detachable_magazine_weapon_profile_for_internal_name(
    std::string_view internal_weapon_name) noexcept;

[[nodiscard]] const DetachableMagazineWeaponProfile*
find_detachable_magazine_weapon_profile_by_internal_name(
    std::string_view internal_weapon_name) noexcept;

// Weapon names are reused by later Zombies maps whose exact viewmodel assets
// differ from the base/campaign definition. Resolve the immutable identity
// pair together; either half alone deliberately returns no guess when it is
// shared by more than one registered profile.
[[nodiscard]] const DetachableMagazineWeaponProfile*
find_detachable_magazine_weapon_profile_by_identity(
    std::string_view internal_weapon_name,
    std::string_view viewmodel_model_name) noexcept;

[[nodiscard]] const DetachableMagazineWeaponProfile*
find_detachable_magazine_weapon_profile_by_viewmodel_model_name(
    std::string_view viewmodel_model_name) noexcept;

[[nodiscard]] bool validate_detachable_magazine_weapon_profile_registry(
    std::span<const DetachableMagazineWeaponProfile* const> profiles) noexcept;

[[nodiscard]] bool
validate_detachable_magazine_weapon_profile_registry() noexcept;

}  // namespace wawvr::mod
