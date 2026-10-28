#pragma once

#include "xr_types.h"
#include "physical_scope_layout.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace wawvr::mod {

struct PhysicalScopeProfile final {
    const char* internal_weapon_name{};
    wawvr::xr::Vec3f lens_from_grip_weapon_local{};
    // Generic tag_scope/tag_scope_animate bones identify the optic assembly,
    // not necessarily its rear glass. This model-local correction places the
    // physical quad at the actual eyepiece. Exact rear-lens tags pass a zero
    // correction instead.
    wawvr::xr::Vec3f lens_from_scope_tag_weapon_local{};
    float zoom_fov_degrees{20.0F};
    float lens_radius_meters{0.032F};
    // Some viewmodels have a very different receiver/optic layout from the
    // legacy grip-relative fallback. Do not draw a floating lens if their
    // evaluated optic tag is unavailable during a scripted weapon change.
    bool requires_model_tag{};
    // Explicit model lookup never falls back to a name-only profile. Some
    // Zombie weapon names resolve to different map-owned model families.
    const char* exact_model_name{};
    // Non-null selects a reviewed model point owned by this exact bone/tag,
    // rather than the generic tag_scope + weapon-local legacy correction.
    const char* lens_owner_tag{};
    std::uint8_t lens_owner_bone{};
    wawvr::xr::Vec3f lens_center_model_space{};
    std::array<float, 8> lens_owner_bind{};
    std::uint8_t lens_surface_index{};
    std::uint16_t lens_center_vertex{};
    std::uint16_t lens_vertex_count{};
    std::uint16_t lens_triangle_count{};
    const char* lens_material_name{};
};

struct PhysicalScopeSnapshot final {
    bool active{};
    bool anchored_to_model_tag{};
    std::uint64_t controller_generation{};
    std::uint64_t publication_milliseconds{};
    std::int32_t weapon_index{};
    wawvr::xr::Vec3f lens_origin_world{};
    wawvr::xr::Basis3f lens_axis_world{};
    // CoD camera-local coordinates: +forward, +left, +up, in IW units.
    wawvr::xr::Vec3f lens_origin_camera_local{};
    wawvr::xr::Basis3f lens_axis_camera_local{};
    float zoom_fov_degrees{};
    float lens_radius_meters{};
    // Optical rendering can follow the actual shot ray without moving or
    // tilting the physical glass quad. Older/unmodified profiles retain the
    // lens transform as their camera when this flag is false.
    bool ballistic_camera_aligned{};
    wawvr::xr::Vec3f camera_origin_world{};
    wawvr::xr::Basis3f camera_axis_world{};
};

[[nodiscard]] const PhysicalScopeProfile* find_physical_scope_profile(
    std::string_view internal_weapon_name) noexcept;
[[nodiscard]] const PhysicalScopeProfile* find_physical_scope_profile(
    std::string_view internal_weapon_name,
    std::string_view exact_model_name) noexcept;

// The caller must evaluate this profile's actual owner tag in the current
// canonical DObj. Transform model-space point by inverse full bind, then by
// that owner's evaluated world transform. Failure leaves output unchanged.
[[nodiscard]] bool evaluate_physical_scope_owned_lens(
    const PhysicalScopeProfile* profile,
    const wawvr::xr::Vec3f& actual_owner_origin_world,
    const wawvr::xr::Basis3f& actual_owner_axis_world,
    wawvr::xr::Vec3f* output_lens_world) noexcept;

[[nodiscard]] bool build_physical_scope_snapshot(
    const PhysicalScopeProfile* profile,
    std::int32_t weapon_index,
    std::uint64_t controller_generation,
    std::uint64_t publication_milliseconds,
    bool right_gripping,
    bool left_gripping,
    bool support_pose_latched,
    const wawvr::xr::Vec3f* lens_tag_world,
    const wawvr::xr::Vec3f* lens_tag_weapon_local_offset,
    const wawvr::xr::Vec3f& grip_world,
    const wawvr::xr::Basis3f& weapon_axis_world,
    const wawvr::xr::Vec3f& camera_origin_world,
    const wawvr::xr::Basis3f& camera_axis_world,
    PhysicalScopeSnapshot* output) noexcept;

// Put the optical camera on the same evaluated line used for bullets, at the
// point nearest the rear lens. No arbitrary zero distance, trajectory change,
// or movement of the visible lens is involved. The caller supplies this render
// pass's validated muzzle (never an older global publication). Invalid inputs
// leave the existing scope snapshot unchanged.
[[nodiscard]] bool align_physical_scope_camera_to_projectile(
    const wawvr::xr::Vec3f& muzzle_origin_world,
    const wawvr::xr::Basis3f& projectile_axis_world,
    PhysicalScopeSnapshot* snapshot) noexcept;

void publish_physical_scope_snapshot(
    const PhysicalScopeSnapshot& snapshot) noexcept;
void invalidate_physical_scope_snapshot() noexcept;

[[nodiscard]] bool read_fresh_physical_scope_snapshot(
    std::uint64_t now_milliseconds,
    PhysicalScopeSnapshot* output) noexcept;

}  // namespace wawvr::mod
