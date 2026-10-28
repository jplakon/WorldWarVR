#include "physical_scope_logic.hpp"

#include <array>
#include <cmath>
#include <mutex>

namespace wawvr::mod {
namespace {

// These grip-relative anchors are deliberately data-only. They match the
// COD4 physical-scope fallback (roughly 22 cm forward and 5.5 cm above the
// weapon hand) while allowing individual WaW viewmodels to be polished later
// without changing the renderer or controller contract.
constexpr std::array<PhysicalScopeProfile, 12> kProfiles{{
    // The Kar98 values come from its live viewmodel: tag_scope is the optic
    // assembly root, while the rear lens is 7.50 IW toward the shooter and
    // 1.575 IW above it. The 0.0153 m radius matches the actual 0.602 IW glass.
    {"kar98k_scoped", {9.25F, 0.0F, 2.15F},
     {-7.50F, 0.0F, 1.575F}, 20.0F, 0.0153F, false,
     "viewmodel_ger_kar98_scoped_rifle"},
    {"kar98k_scoped_zombie", {9.25F, 0.0F, 2.15F},
     {-7.50F, 0.0F, 1.575F}, 20.0F, 0.0153F, false,
     "viewmodel_ger_kar98_scoped_rifle"},
    {"springfield_scoped", {9.0F, 0.0F, 2.25F},
     {-3.910136F, -0.001673F, 0.583315F}, 18.0F, 0.011916F, false,
     "viewmodel_usa_springfield_rifle_scoped"},
    {"mosin_rifle_scoped", {9.25F, 0.0F, 2.15F},
     {-5.924734F, 0.039932F, 1.815408F}, 20.0F, 0.013495F, false,
     "viewmodel_rus_mosinnagant_scoped_rifle"},
    {"type99_rifle_scoped", {9.0F, 0.0F, 2.2F},
     {-4.016429F, 0.023786F, 0.016107F}, 20.0F, 0.013855F, false,
     "viewmodel_jap_type99_rifle_scoped"},
    {"fg42_scoped", {8.25F, 0.0F, 2.4F},
     {}, 22.0F, 0.032F, false, "viewmodel_ger_fg42_scoped_lmg"},
    // Vendetta's ptrs41 uses viewmodel_mp_ptrs41. Its transparent rear glass
    // (surface 9, mc/mtl_rus_scope_trans) has center (-2.777846, 0, 3.704640)
    // and aperture radius 0.673388 IW. Subtracting tag_scope's bind position
    // (-0.484172, 0.459957, 1.370917) gives this evaluated-tag correction.
    // This unusually long rifle must not use the small-rifle grip fallback.
    {"ptrs41", {}, {-2.293674F, -0.459957F, 2.333723F},
     20.0F, 0.0171F, true, "viewmodel_mp_ptrs41"},
    // Complete captured geometry, ordered owner binds and lens geometry are
    // identical to the corresponding base profiles. Wet metal materials are
    // different; the Type99 glass geometry/material/owner remain identical.
    {"mosin_rifle_scoped_noflash", {9.25F, 0.0F, 2.15F},
     {-5.924734F, 0.039932F, 1.815408F}, 20.0F, 0.013495F, false,
     "viewmodel_rus_mosinnagant_scoped_rifle"},
    {"type99_rifle_scoped_wet", {9.0F, 0.0F, 2.2F},
     {-4.016429F, 0.023786F, 0.016107F}, 20.0F, 0.013855F, false,
     "viewmodel_jap_type99_rifle_scoped_wet"},
    // Same weapon name, different actual model and lens owner. The explicit
    // center is vertex1 of each transparent 21-vertex/20-triangle lens fan;
    // radius is its measured inscribed projected aperture (0.673387527 IW).
    {"ptrs41_zombie", {}, {}, 20.0F, 0.017104044F, true,
     "viewmodel_mp_ptrs41", "tag_scope", 7,
     {-2.777846098F, 0.0F, 3.704639912F},
     {0.0F, 0.0F, 0.0F, 1.0F, -0.484171987F, 0.459957004F, 1.370916963F, 2.0F},
     9, 1, 21, 20, "mc/mtl_rus_scope_trans"},
    {"ptrs41_zombie", {}, {}, 20.0F, 0.017104044F, true,
     "viewmodel_zombie_ptrs41", "j_gun", 0,
     {-2.777846098F, 0.0F, 3.704639912F},
     {0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 2.0F},
     7, 1, 21, 20, "mc/mtl_rus_scope_trans"},
    {"ptrs41_zombie_upgraded", {}, {}, 20.0F, 0.017104044F, true,
     "viewmodel_zombie_ptrs41_up", "j_gun", 0,
     {-2.777846098F, 0.0F, 3.704639912F},
     {0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 2.0F},
     7, 1, 21, 20, "mc/mtl_rus_scope_trans"},
}};

std::mutex g_scope_mutex;
PhysicalScopeSnapshot g_scope_snapshot{};

[[nodiscard]] bool finite(const wawvr::xr::Vec3f& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] float dot(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] bool valid_axis(const wawvr::xr::Basis3f& axis) noexcept {
    if (!finite(axis.forward) || !finite(axis.left) || !finite(axis.up)) {
        return false;
    }
    constexpr float kMinimumLengthSquared = 0.64F;
    constexpr float kMaximumLengthSquared = 1.44F;
    constexpr float kMaximumDot = 0.20F;
    const float forward_length = dot(axis.forward, axis.forward);
    const float left_length = dot(axis.left, axis.left);
    const float up_length = dot(axis.up, axis.up);
    return forward_length >= kMinimumLengthSquared &&
           forward_length <= kMaximumLengthSquared &&
           left_length >= kMinimumLengthSquared &&
           left_length <= kMaximumLengthSquared &&
           up_length >= kMinimumLengthSquared &&
           up_length <= kMaximumLengthSquared &&
           std::fabs(dot(axis.forward, axis.left)) <= kMaximumDot &&
           std::fabs(dot(axis.forward, axis.up)) <= kMaximumDot &&
           std::fabs(dot(axis.left, axis.up)) <= kMaximumDot;
}

[[nodiscard]] wawvr::xr::Vec3f compose(
    const wawvr::xr::Basis3f& basis,
    const wawvr::xr::Vec3f& local) noexcept {
    return {
        local.x * basis.forward.x + local.y * basis.left.x +
            local.z * basis.up.x,
        local.x * basis.forward.y + local.y * basis.left.y +
            local.z * basis.up.y,
        local.x * basis.forward.z + local.y * basis.left.z +
            local.z * basis.up.z,
    };
}

[[nodiscard]] wawvr::xr::Vec3f add(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] wawvr::xr::Vec3f subtract(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] wawvr::xr::Vec3f to_camera_local(
    const wawvr::xr::Vec3f& world,
    const wawvr::xr::Basis3f& camera_axis) noexcept {
    return {
        dot(world, camera_axis.forward),
        dot(world, camera_axis.left),
        dot(world, camera_axis.up),
    };
}

}  // namespace

const PhysicalScopeProfile* find_physical_scope_profile(
    const std::string_view internal_weapon_name) noexcept {
    const PhysicalScopeProfile* found = nullptr;
    for (const auto& profile : kProfiles) {
        if (internal_weapon_name == profile.internal_weapon_name) {
            if (found != nullptr) {
                return nullptr;
            }
            found = &profile;
        }
    }
    return found;
}

const PhysicalScopeProfile* find_physical_scope_profile(
    const std::string_view internal_weapon_name,
    const std::string_view exact_model_name) noexcept {
    if (internal_weapon_name.empty() || exact_model_name.empty()) {
        return nullptr;
    }
    for (const auto& profile : kProfiles) {
        if (profile.exact_model_name != nullptr &&
            internal_weapon_name == profile.internal_weapon_name &&
            exact_model_name == profile.exact_model_name) {
            return &profile;
        }
    }
    return nullptr;
}

bool evaluate_physical_scope_owned_lens(
    const PhysicalScopeProfile* const profile,
    const wawvr::xr::Vec3f& actual_owner_origin_world,
    const wawvr::xr::Basis3f& actual_owner_axis_world,
    wawvr::xr::Vec3f* const output_lens_world) noexcept {
    if (profile == nullptr || output_lens_world == nullptr ||
        profile->exact_model_name == nullptr || profile->exact_model_name[0] == '\0' ||
        profile->lens_owner_tag == nullptr || profile->lens_owner_tag[0] == '\0' ||
        profile->lens_owner_bone >= 128 || profile->lens_surface_index >= 32 ||
        profile->lens_center_vertex != 1 || profile->lens_vertex_count != 21 ||
        profile->lens_triangle_count != 20 || profile->lens_material_name == nullptr ||
        std::string_view{profile->lens_material_name} != "mc/mtl_rus_scope_trans" ||
        !profile->requires_model_tag || !finite(profile->lens_center_model_space) ||
        !finite(actual_owner_origin_world) || !valid_axis(actual_owner_axis_world)) {
        return false;
    }
    const auto& axis = actual_owner_axis_world;
    const wawvr::xr::Vec3f cross{
        axis.forward.y * axis.left.z - axis.forward.z * axis.left.y,
        axis.forward.z * axis.left.x - axis.forward.x * axis.left.z,
        axis.forward.x * axis.left.y - axis.forward.y * axis.left.x};
    constexpr float tolerance = 0.001F;
    if (std::fabs(dot(axis.forward, axis.forward) - 1.0F) > tolerance ||
        std::fabs(dot(axis.left, axis.left) - 1.0F) > tolerance ||
        std::fabs(dot(axis.up, axis.up) - 1.0F) > tolerance ||
        std::fabs(dot(axis.forward, axis.left)) > tolerance ||
        std::fabs(dot(axis.forward, axis.up)) > tolerance ||
        std::fabs(dot(axis.left, axis.up)) > tolerance || dot(cross, axis.up) < 1.0F - tolerance) {
        return false;
    }
    for (const float value : profile->lens_owner_bind) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    const auto& b = profile->lens_owner_bind;
    const float norm_squared = b[0]*b[0] + b[1]*b[1] + b[2]*b[2] + b[3]*b[3];
    if (norm_squared < 0.5F || norm_squared > 1.5F ||
        std::fabs(b[7] * norm_squared - 2.0F) > 0.002F) {
        return false;
    }
    const float inverse_norm = 1.0F / std::sqrt(norm_squared);
    // Normalize and conjugate the exact authored owner quaternion. The point
    // is in model space; adding the evaluated owner translation alone would
    // double-count its bind origin and miss any authored bind rotation.
    const wawvr::xr::Vec3f q{-b[0]*inverse_norm, -b[1]*inverse_norm, -b[2]*inverse_norm};
    const float w = b[3] * inverse_norm;
    const auto v = subtract(profile->lens_center_model_space, {b[4], b[5], b[6]});
    const wawvr::xr::Vec3f t{
        2.0F*(q.y*v.z-q.z*v.y), 2.0F*(q.z*v.x-q.x*v.z), 2.0F*(q.x*v.y-q.y*v.x)};
    const wawvr::xr::Vec3f local{
        v.x+w*t.x+q.y*t.z-q.z*t.y,
        v.y+w*t.y+q.z*t.x-q.x*t.z,
        v.z+w*t.z+q.x*t.y-q.y*t.x};
    const auto world = add(actual_owner_origin_world, compose(axis, local));
    if (!finite(world)) {
        return false;
    }
    *output_lens_world = world;
    return true;
}

bool build_physical_scope_snapshot(
    const PhysicalScopeProfile* const profile,
    const std::int32_t weapon_index,
    const std::uint64_t controller_generation,
    const std::uint64_t publication_milliseconds,
    const bool right_gripping,
    const bool left_gripping,
    const bool support_pose_latched,
    const wawvr::xr::Vec3f* const lens_tag_world,
    const wawvr::xr::Vec3f* const lens_tag_weapon_local_offset,
    const wawvr::xr::Vec3f& grip_world,
    const wawvr::xr::Basis3f& weapon_axis_world,
    const wawvr::xr::Vec3f& camera_origin_world,
    const wawvr::xr::Basis3f& camera_axis_world,
    PhysicalScopeSnapshot* const output) noexcept {
    if (output == nullptr) {
        return false;
    }
    *output = {};
    // Once paired grips have raised a scoped rifle, the left hand deliberately
    // retains that same support pose while the right hand cycles the bolt.
    // Keep the optical camera alive across that handoff. Dropping it for the
    // left-only interval changes the frontend view count from three to two and
    // back during the regrip, which can retire only one eye and produce a
    // visible zero-layer blackout. A direct one-hand pickup still cannot
    // activate the optic because it has no retained support-pose latch.
    const bool physical_scope_pose =
        left_gripping && (right_gripping || support_pose_latched);
    if (profile == nullptr || weapon_index <= 0 ||
        controller_generation == 0 || publication_milliseconds == 0 ||
        !physical_scope_pose || !finite(grip_world) ||
        ((profile->requires_model_tag || profile->lens_owner_tag != nullptr) &&
         lens_tag_world == nullptr) ||
        (profile->lens_owner_tag != nullptr && lens_tag_weapon_local_offset != nullptr &&
         (lens_tag_weapon_local_offset->x != 0.0F || lens_tag_weapon_local_offset->y != 0.0F ||
          lens_tag_weapon_local_offset->z != 0.0F)) ||
        (lens_tag_world != nullptr && !finite(*lens_tag_world)) ||
        (lens_tag_weapon_local_offset != nullptr &&
         !finite(*lens_tag_weapon_local_offset)) ||
        (lens_tag_world == nullptr &&
         lens_tag_weapon_local_offset != nullptr) ||
        !finite(camera_origin_world) || !valid_axis(weapon_axis_world) ||
        !valid_axis(camera_axis_world) ||
        !std::isfinite(profile->zoom_fov_degrees) ||
        profile->zoom_fov_degrees <= 1.0F ||
        profile->zoom_fov_degrees >= 120.0F ||
        !std::isfinite(profile->lens_radius_meters) ||
        profile->lens_radius_meters < 0.005F ||
        profile->lens_radius_meters > 0.10F) {
        return false;
    }

    PhysicalScopeSnapshot result{};
    result.active = true;
    result.anchored_to_model_tag = lens_tag_world != nullptr;
    result.controller_generation = controller_generation;
    result.publication_milliseconds = publication_milliseconds;
    result.weapon_index = weapon_index;
    if (lens_tag_world != nullptr) {
        const wawvr::xr::Vec3f local_offset =
            lens_tag_weapon_local_offset != nullptr
                ? *lens_tag_weapon_local_offset
                : wawvr::xr::Vec3f{};
        result.lens_origin_world = add(
            *lens_tag_world, compose(weapon_axis_world, local_offset));
    } else {
        result.lens_origin_world = add(
              grip_world,
              compose(
                  weapon_axis_world,
                  profile->lens_from_grip_weapon_local));
    }
    result.lens_axis_world = weapon_axis_world;
    result.lens_origin_camera_local = to_camera_local(
        subtract(result.lens_origin_world, camera_origin_world),
        camera_axis_world);
    result.lens_axis_camera_local = {
        to_camera_local(weapon_axis_world.forward, camera_axis_world),
        to_camera_local(weapon_axis_world.left, camera_axis_world),
        to_camera_local(weapon_axis_world.up, camera_axis_world),
    };
    result.zoom_fov_degrees = profile->zoom_fov_degrees;
    result.lens_radius_meters = profile->lens_radius_meters;
    if (!finite(result.lens_origin_world) ||
        !finite(result.lens_origin_camera_local) ||
        !valid_axis(result.lens_axis_camera_local)) {
        return false;
    }
    *output = result;
    return true;
}

bool align_physical_scope_camera_to_projectile(
    const wawvr::xr::Vec3f& muzzle_origin_world,
    const wawvr::xr::Basis3f& projectile_axis_world,
    PhysicalScopeSnapshot* const snapshot) noexcept {
    if (snapshot == nullptr || !snapshot->active ||
        !finite(snapshot->lens_origin_world) || !finite(muzzle_origin_world) ||
        !valid_axis(projectile_axis_world)) {
        return false;
    }
    const auto& forward = projectile_axis_world.forward;
    const auto& left = projectile_axis_world.left;
    const auto& up = projectile_axis_world.up;
    constexpr float kAxisTolerance = 0.001F;
    const wawvr::xr::Vec3f cross_forward_left{
        forward.y * left.z - forward.z * left.y,
        forward.z * left.x - forward.x * left.z,
        forward.x * left.y - forward.y * left.x};
    if (std::fabs(dot(forward, forward) - 1.0F) > kAxisTolerance ||
        std::fabs(dot(left, left) - 1.0F) > kAxisTolerance ||
        std::fabs(dot(up, up) - 1.0F) > kAxisTolerance ||
        std::fabs(dot(forward, left)) > kAxisTolerance ||
        std::fabs(dot(forward, up)) > kAxisTolerance ||
        std::fabs(dot(left, up)) > kAxisTolerance ||
        dot(cross_forward_left, up) < 1.0F - kAxisTolerance) {
        return false;
    }
    const auto lens_from_muzzle =
        subtract(snapshot->lens_origin_world, muzzle_origin_world);
    // Guard against mixing a stale/foreign weapon origin with this optic.
    constexpr float kMaximumLensMuzzleSeparationIw = 128.0F;
    const float separation_squared = dot(lens_from_muzzle, lens_from_muzzle);
    if (!std::isfinite(separation_squared) ||
        separation_squared > kMaximumLensMuzzleSeparationIw *
                                 kMaximumLensMuzzleSeparationIw) {
        return false;
    }
    const float along_ray = dot(lens_from_muzzle, forward);
    const wawvr::xr::Vec3f camera_origin = add(muzzle_origin_world, {
        forward.x * along_ray, forward.y * along_ray, forward.z * along_ray});
    const auto camera_shift = subtract(camera_origin, snapshot->lens_origin_world);
    constexpr float kMaximumCameraShiftIw = 16.0F;
    if (!finite(camera_origin) || dot(camera_shift, camera_shift) >
                                     kMaximumCameraShiftIw * kMaximumCameraShiftIw) {
        return false;
    }
    snapshot->camera_origin_world = camera_origin;
    snapshot->camera_axis_world = projectile_axis_world;
    snapshot->ballistic_camera_aligned = true;
    return true;
}

void publish_physical_scope_snapshot(
    const PhysicalScopeSnapshot& snapshot) noexcept {
    try {
        std::lock_guard<std::mutex> lock(g_scope_mutex);
        g_scope_snapshot = snapshot.active ? snapshot : PhysicalScopeSnapshot{};
    } catch (...) {
    }
}

void invalidate_physical_scope_snapshot() noexcept {
    publish_physical_scope_snapshot({});
}

bool read_fresh_physical_scope_snapshot(
    const std::uint64_t now_milliseconds,
    PhysicalScopeSnapshot* const output) noexcept {
    if (output == nullptr || now_milliseconds == 0) {
        return false;
    }
    PhysicalScopeSnapshot snapshot{};
    try {
        std::lock_guard<std::mutex> lock(g_scope_mutex);
        snapshot = g_scope_snapshot;
    } catch (...) {
        return false;
    }
    constexpr std::uint64_t kMaximumAgeMilliseconds = 150;
    if (!snapshot.active || snapshot.publication_milliseconds == 0 ||
        now_milliseconds < snapshot.publication_milliseconds ||
        now_milliseconds - snapshot.publication_milliseconds >
            kMaximumAgeMilliseconds) {
        return false;
    }
    *output = snapshot;
    return true;
}

}  // namespace wawvr::mod
