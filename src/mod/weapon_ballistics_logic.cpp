// SPDX-License-Identifier: GPL-3.0-only
#include "weapon_ballistics_logic.hpp"

#include <algorithm>
#include <cmath>

namespace wawvr::mod {

bool hitscan_uses_tag_flash_forward(
    const std::string_view internal_weapon_name) noexcept {
    return internal_weapon_name == "ppsh" || internal_weapon_name == "svt40";
}

namespace {

[[nodiscard]] bool finite_vector(
    const wawvr::xr::Vec3f& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] bool normalize_vector(
    wawvr::xr::Vec3f value,
    wawvr::xr::Vec3f* const output) noexcept {
    if (output == nullptr || !finite_vector(value)) {
        return false;
    }
    const float length_squared = value.x * value.x + value.y * value.y +
        value.z * value.z;
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-8F) {
        return false;
    }
    const float inverse_length = 1.0F / std::sqrt(length_squared);
    value.x *= inverse_length;
    value.y *= inverse_length;
    value.z *= inverse_length;
    if (!finite_vector(value)) {
        return false;
    }
    *output = value;
    return true;
}

[[nodiscard]] wawvr::xr::Vec3f cross(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] float dot(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
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

[[nodiscard]] wawvr::xr::Vec3f scale(
    const wawvr::xr::Vec3f& value,
    const float factor) noexcept {
    return {value.x * factor, value.y * factor, value.z * factor};
}

[[nodiscard]] bool rotate_shortest_arc(
    const wawvr::xr::Vec3f& rotation_cross,
    const float source_target_dot,
    const wawvr::xr::Vec3f& value,
    wawvr::xr::Vec3f* const rotated) noexcept {
    if (rotated == nullptr || !finite_vector(rotation_cross) ||
        !std::isfinite(source_target_dot) || !finite_vector(value) ||
        source_target_dot <= -0.9999F) {
        return false;
    }

    // Rodrigues' shortest-arc form with v=cross(source,target):
    // R(x) = x + v*x + v*(v*x)/(1+dot(source,target)).
    const wawvr::xr::Vec3f first_cross = cross(rotation_cross, value);
    const wawvr::xr::Vec3f second_cross =
        cross(rotation_cross, first_cross);
    const float inverse_one_plus_dot =
        1.0F / (1.0F + source_target_dot);
    const wawvr::xr::Vec3f result = add(
        add(value, first_cross),
        scale(second_cross, inverse_one_plus_dot));
    if (!finite_vector(result)) {
        return false;
    }
    *rotated = result;
    return true;
}

bool calculate_bounded_launcher_viewmodel_alignment(
    const wawvr::xr::Vec3f& root_origin,
    const wawvr::xr::Basis3f& root_axis,
    const wawvr::xr::Vec3f& grip_anchor_world,
    const wawvr::xr::Basis3f& evaluated_bore_basis,
    const wawvr::xr::Basis3f& tracked_launch_axis,
    BazookaViewmodelAlignment* const alignment,
    const float maximum_correction_degrees,
    const float minimum_correction_dot) noexcept {
    if (alignment == nullptr || !finite_vector(root_origin) ||
        !finite_vector(grip_anchor_world) ||
        !finite_vector(root_axis.forward) ||
        !finite_vector(root_axis.left) || !finite_vector(root_axis.up)) {
        return false;
    }

    wawvr::xr::Vec3f bore_forward{};
    wawvr::xr::Vec3f tracked_forward{};
    if (!normalize_vector(evaluated_bore_basis.forward, &bore_forward) ||
        !normalize_vector(tracked_launch_axis.forward, &tracked_forward)) {
        return false;
    }

    const float source_target_dot = std::clamp(
        dot(bore_forward, tracked_forward), -1.0F, 1.0F);
    constexpr float kDegreesToRadians =
        3.14159265358979323846F / 180.0F;
    if (!std::isfinite(source_target_dot) ||
        source_target_dot < minimum_correction_dot) {
        return false;
    }

    const wawvr::xr::Vec3f rotation_cross =
        cross(bore_forward, tracked_forward);
    BazookaViewmodelAlignment calculated{};
    if (!rotate_shortest_arc(
            rotation_cross, source_target_dot, root_axis.forward,
            &calculated.corrected_root_axis.forward) ||
        !rotate_shortest_arc(
            rotation_cross, source_target_dot, root_axis.left,
            &calculated.corrected_root_axis.left) ||
        !rotate_shortest_arc(
            rotation_cross, source_target_dot, root_axis.up,
            &calculated.corrected_root_axis.up)) {
        return false;
    }

    wawvr::xr::Vec3f pivot_to_root = subtract(root_origin, grip_anchor_world);
    wawvr::xr::Vec3f corrected_pivot_to_root{};
    if (!rotate_shortest_arc(
            rotation_cross, source_target_dot, pivot_to_root,
            &corrected_pivot_to_root)) {
        return false;
    }
    calculated.corrected_root_origin =
        add(grip_anchor_world, corrected_pivot_to_root);
    calculated.correction_degrees =
        std::acos(source_target_dot) / kDegreesToRadians;

    wawvr::xr::Basis3f validated_root{};
    wawvr::xr::Basis3f corrected_bore{};
    if (!build_evaluated_projectile_basis(
            calculated.corrected_root_axis.forward,
            calculated.corrected_root_axis, &validated_root) ||
        !rotate_shortest_arc(
            rotation_cross, source_target_dot, bore_forward,
            &corrected_bore.forward) ||
        !finite_vector(calculated.corrected_root_origin) ||
        !std::isfinite(calculated.correction_degrees) ||
        calculated.correction_degrees > maximum_correction_degrees ||
        dot(corrected_bore.forward, tracked_forward) < 0.9999F) {
        return false;
    }

    calculated.corrected_root_axis = validated_root;
    *alignment = calculated;
    return true;
}

}  // namespace

bool calculate_bazooka_viewmodel_alignment(
    const wawvr::xr::Vec3f& root_origin,
    const wawvr::xr::Basis3f& root_axis,
    const wawvr::xr::Vec3f& grip_anchor_world,
    const wawvr::xr::Basis3f& evaluated_bore_basis,
    const wawvr::xr::Basis3f& tracked_launch_axis,
    BazookaViewmodelAlignment* const alignment) noexcept {
    // Preserve both original Bazooka constants and the exact calculation.
    return calculate_bounded_launcher_viewmodel_alignment(
        root_origin, root_axis, grip_anchor_world, evaluated_bore_basis,
        tracked_launch_axis, alignment, 25.0F, 0.90630778703664996324F);
}

bool calculate_physical_launcher_viewmodel_alignment(
    const PhysicalRocketLauncherProfile* const profile,
    const wawvr::xr::Vec3f& root_origin,
    const wawvr::xr::Basis3f& root_axis,
    const wawvr::xr::Vec3f& grip_anchor_world,
    const wawvr::xr::Basis3f& evaluated_bore_basis,
    const wawvr::xr::Basis3f& tracked_launch_axis,
    BazookaViewmodelAlignment* const alignment) noexcept {
    if (profile == &kBazookaPhysicalLauncher) {
        return calculate_bazooka_viewmodel_alignment(
            root_origin, root_axis, grip_anchor_world, evaluated_bore_basis,
            tracked_launch_axis, alignment);
    }
    if (profile != &kPanzerschreckPhysicalLauncher) {
        return false;
    }
    // The exact retail Panzerschreck's held idle reaches 44.6 degrees, whereas
    // its firing pose is about 23.8 degrees. A Bazooka-only 25-degree bound
    // switches this correction on/off between those poses. Keep a bounded
    // 60-degree allowance only after the exact model/root/bore validation.
    return calculate_bounded_launcher_viewmodel_alignment(
        root_origin, root_axis, grip_anchor_world, evaluated_bore_basis,
        tracked_launch_axis, alignment, 60.0F, 0.5F);
}

bool published_weapon_muzzle_is_fresh(
    const PublishedWeaponMuzzleSnapshot& muzzle,
    const std::uint64_t controller_generation,
    const std::uint64_t now_milliseconds) noexcept {
    return muzzle.valid && muzzle.controller_generation != 0 &&
           controller_generation != 0 && now_milliseconds != 0 &&
           muzzle.controller_generation <= controller_generation &&
           controller_generation - muzzle.controller_generation <=
               kMaximumPublishedMuzzleGenerationLag &&
           muzzle.publication_milliseconds != 0 &&
           muzzle.publication_milliseconds <= now_milliseconds &&
           now_milliseconds - muzzle.publication_milliseconds <=
               kMaximumPublishedMuzzleAgeMilliseconds &&
           std::isfinite(muzzle.origin.x) &&
           std::isfinite(muzzle.origin.y) &&
           std::isfinite(muzzle.origin.z);
}

bool physical_muzzle_gate_allows(
    const PhysicalMuzzleGate& gate) noexcept {
    return gate.hook_enabled && gate.firing_entity != 0 &&
           gate.firing_entity == gate.local_player_entity &&
           gate.entity_number == 0 && gate.has_client &&
           (gate.weapon_type == 0 || gate.weapon_type == 2) &&
           gate.controller_frame_current &&
           gate.published_muzzle_fresh;
}

bool authoritative_weapon_basis_is_requested(
    const bool layout_requests_override,
    const bool direct_ballistic_profile,
    const std::int32_t weapon_type) noexcept {
    return layout_requests_override || direct_ballistic_profile ||
           weapon_type == 0 || weapon_type == 2;
}

bool select_exact_bazooka_weapon_root_launch_basis(
    const ExactBazookaWeaponRootLaunchGate& gate,
    const wawvr::xr::Basis3f& weapon_root_basis,
    wawvr::xr::Basis3f* const launch_basis) noexcept {
    if (!gate.exact_local_bazooka_route ||
        !gate.physical_muzzle_allowed ||
        !gate.published_weapon_root_fresh || launch_basis == nullptr) {
        return false;
    }
    return build_evaluated_projectile_basis(
        weapon_root_basis.forward, weapon_root_basis, launch_basis);
}

bool build_evaluated_projectile_basis(
    const wawvr::xr::Vec3f& evaluated_barrel_direction,
    const wawvr::xr::Basis3f& roll_reference,
    wawvr::xr::Basis3f* const projectile_basis) noexcept {
    if (projectile_basis == nullptr) {
        return false;
    }

    const auto finite_vector = [](const wawvr::xr::Vec3f& value) noexcept {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
               std::isfinite(value.z);
    };
    const auto normalize = [&](wawvr::xr::Vec3f value,
                               wawvr::xr::Vec3f* const output) noexcept {
        if (output == nullptr || !finite_vector(value)) {
            return false;
        }
        const float length_squared = value.x * value.x +
            value.y * value.y + value.z * value.z;
        if (!std::isfinite(length_squared) || length_squared <= 1.0e-8F) {
            return false;
        }
        const float inverse_length = 1.0F / std::sqrt(length_squared);
        value.x *= inverse_length;
        value.y *= inverse_length;
        value.z *= inverse_length;
        if (!finite_vector(value)) {
            return false;
        }
        *output = value;
        return true;
    };

    wawvr::xr::Vec3f forward{};
    if (!normalize(evaluated_barrel_direction, &forward) ||
        !finite_vector(roll_reference.left) ||
        !finite_vector(roll_reference.up)) {
        return false;
    }

    // Preserve weapon roll by projecting its evaluated left vector onto the
    // plane perpendicular to the real barrel. If that axis is degenerate,
    // derive left from the evaluated up vector instead.
    const float left_forward_dot =
        roll_reference.left.x * forward.x +
        roll_reference.left.y * forward.y +
        roll_reference.left.z * forward.z;
    wawvr::xr::Vec3f left_candidate{
        roll_reference.left.x - forward.x * left_forward_dot,
        roll_reference.left.y - forward.y * left_forward_dot,
        roll_reference.left.z - forward.z * left_forward_dot,
    };
    wawvr::xr::Vec3f left{};
    if (!normalize(left_candidate, &left)) {
        left_candidate = {
            roll_reference.up.y * forward.z -
                roll_reference.up.z * forward.y,
            roll_reference.up.z * forward.x -
                roll_reference.up.x * forward.z,
            roll_reference.up.x * forward.y -
                roll_reference.up.y * forward.x,
        };
        if (!normalize(left_candidate, &left)) {
            return false;
        }
    }

    wawvr::xr::Vec3f up_candidate{
        forward.y * left.z - forward.z * left.y,
        forward.z * left.x - forward.x * left.z,
        forward.x * left.y - forward.y * left.x,
    };
    wawvr::xr::Vec3f up{};
    if (!normalize(up_candidate, &up)) {
        return false;
    }

    *projectile_basis = {forward, left, up};
    return true;
}

bool build_validated_bazooka_bore_basis(
    const wawvr::xr::Vec3f& tag_brass_world,
    const wawvr::xr::Vec3f& tag_flash_world,
    const wawvr::xr::Basis3f& tag_flash_basis,
    wawvr::xr::Basis3f* const projectile_basis,
    float* const bore_length_world_units,
    float* const flash_forward_alignment) noexcept {
    if (projectile_basis == nullptr) {
        return false;
    }

    const auto finite_vector = [](const wawvr::xr::Vec3f& value) noexcept {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
               std::isfinite(value.z);
    };
    if (!finite_vector(tag_brass_world) ||
        !finite_vector(tag_flash_world)) {
        return false;
    }

    const wawvr::xr::Vec3f rear_to_muzzle{
        tag_flash_world.x - tag_brass_world.x,
        tag_flash_world.y - tag_brass_world.y,
        tag_flash_world.z - tag_brass_world.z,
    };
    const float length_squared =
        rear_to_muzzle.x * rear_to_muzzle.x +
        rear_to_muzzle.y * rear_to_muzzle.y +
        rear_to_muzzle.z * rear_to_muzzle.z;
    if (!std::isfinite(length_squared)) {
        return false;
    }
    const float length = std::sqrt(length_squared);
    if (!std::isfinite(length) ||
        length < kMinimumBazookaBoreLengthWorldUnits ||
        length > kMaximumBazookaBoreLengthWorldUnits) {
        return false;
    }

    wawvr::xr::Basis3f measured_basis{};
    wawvr::xr::Basis3f normalized_flash_basis{};
    if (!build_evaluated_projectile_basis(
            rear_to_muzzle, tag_flash_basis, &measured_basis) ||
        !build_evaluated_projectile_basis(
            tag_flash_basis.forward, tag_flash_basis,
            &normalized_flash_basis)) {
        return false;
    }
    const float alignment =
        measured_basis.forward.x * normalized_flash_basis.forward.x +
        measured_basis.forward.y * normalized_flash_basis.forward.y +
        measured_basis.forward.z * normalized_flash_basis.forward.z;
    if (!std::isfinite(alignment) ||
        alignment < kMinimumBazookaBoreFlashAlignment) {
        return false;
    }

    *projectile_basis = measured_basis;
    if (bore_length_world_units != nullptr) {
        *bore_length_world_units = length;
    }
    if (flash_forward_alignment != nullptr) {
        *flash_forward_alignment = alignment;
    }
    return true;
}

bool build_validated_physical_launcher_bore_basis(
    const PhysicalRocketLauncherProfile* const profile,
    const wawvr::xr::Vec3f& tag_brass_world,
    const wawvr::xr::Vec3f& tag_flash_world,
    const wawvr::xr::Basis3f& tag_flash_basis,
    const wawvr::xr::Basis3f* const rigid_tube_basis,
    wawvr::xr::Basis3f* const projectile_basis,
    float* const bore_length_world_units,
    float* const flash_forward_alignment) noexcept {
    if (profile == &kBazookaPhysicalLauncher) {
        return build_validated_bazooka_bore_basis(
            tag_brass_world, tag_flash_world, tag_flash_basis,
            projectile_basis, bore_length_world_units,
            flash_forward_alignment);
    }
    if (profile != &kPanzerschreckPhysicalLauncher ||
        projectile_basis == nullptr || rigid_tube_basis == nullptr) {
        return false;
    }
    wawvr::xr::Basis3f measured_basis{};
    wawvr::xr::Basis3f normalized_root{};
    wawvr::xr::Basis3f normalized_flash{};
    float length = 0.0F;
    float alignment = 0.0F;
    if (!build_validated_bazooka_bore_basis(
            tag_brass_world, tag_flash_world, tag_flash_basis,
            &measured_basis, &length, &alignment) ||
        length < profile->minimum_bore_length_world_units ||
        length > profile->maximum_bore_length_world_units ||
        !build_evaluated_projectile_basis(
            rigid_tube_basis->forward, *rigid_tube_basis,
            &normalized_root) ||
        !build_evaluated_projectile_basis(
            tag_flash_basis.forward, tag_flash_basis, &normalized_flash)) {
        return false;
    }
    const float root_flash_dot =
        normalized_root.forward.x * normalized_flash.forward.x +
        normalized_root.forward.y * normalized_flash.forward.y +
        normalized_root.forward.z * normalized_flash.forward.z;
    if (!std::isfinite(root_flash_dot) || root_flash_dot < 0.9995F) {
        return false;
    }
    *projectile_basis = normalized_root;
    if (bore_length_world_units != nullptr) {
        *bore_length_world_units = length;
    }
    if (flash_forward_alignment != nullptr) {
        *flash_forward_alignment = alignment;
    }
    return true;
}

bool exact_bazooka_projectile_direction(
    const wawvr::xr::Basis3f& tracked_launch_basis,
    wawvr::xr::Vec3f* const projectile_direction) noexcept {
    if (projectile_direction == nullptr) {
        return false;
    }
    wawvr::xr::Basis3f normalized_basis{};
    if (!build_evaluated_projectile_basis(
            tracked_launch_basis.forward, tracked_launch_basis,
            &normalized_basis)) {
        return false;
    }

    *projectile_direction = normalized_basis.forward;
    return true;
}

bool apply_exact_bazooka_stable_missile_flag(
    const bool exact_local_bazooka_route,
    std::uint32_t* const entity_flags) noexcept {
    if (!exact_local_bazooka_route || entity_flags == nullptr) {
        return false;
    }
    *entity_flags |= kStableMissilesEntityFlag;
    return true;
}

bool apply_authoritative_weapon_basis_override(
    const AuthoritativeWeaponBasisGate& gate,
    const wawvr::xr::Basis3f& final_visible_basis,
    wawvr::xr::Vec3f* const forward,
    wawvr::xr::Vec3f* const right,
    wawvr::xr::Vec3f* const up) noexcept {
    const bool finite =
        std::isfinite(final_visible_basis.forward.x) &&
        std::isfinite(final_visible_basis.forward.y) &&
        std::isfinite(final_visible_basis.forward.z) &&
        std::isfinite(final_visible_basis.left.x) &&
        std::isfinite(final_visible_basis.left.y) &&
        std::isfinite(final_visible_basis.left.z) &&
        std::isfinite(final_visible_basis.up.x) &&
        std::isfinite(final_visible_basis.up.y) &&
        std::isfinite(final_visible_basis.up.z);
    if (!gate.override_requested || !gate.physical_muzzle_allowed ||
        !gate.final_visible_basis_fresh || !finite || forward == nullptr ||
        right == nullptr || up == nullptr) {
        return false;
    }

    const wawvr::xr::Vec3f converted_forward =
        final_visible_basis.forward;
    const wawvr::xr::Vec3f converted_right{
        -final_visible_basis.left.x,
        -final_visible_basis.left.y,
        -final_visible_basis.left.z,
    };
    const wawvr::xr::Vec3f converted_up = final_visible_basis.up;
    *forward = converted_forward;
    *right = converted_right;
    *up = converted_up;
    return true;
}

bool apply_fixed_ads_spread_override(
    const FixedAdsSpreadGate& gate,
    float* const spread_degrees) noexcept {
    if (spread_degrees == nullptr || !gate.hook_enabled ||
        gate.attacker == 0 || gate.local_player_entity == 0 ||
        gate.attacker != gate.local_player_entity ||
        gate.entity_number != 0 || !gate.has_client ||
        gate.weapon_type != 0 ||
        !std::isfinite(gate.ads_spread_degrees) ||
        gate.ads_spread_degrees < 0.0F ||
        gate.ads_spread_degrees > kMaximumReasonableAdsSpreadDegrees) {
        return false;
    }

    *spread_degrees = gate.ads_spread_degrees;
    return true;
}

bool apply_fixed_ads_visual_spread_override(
    const FixedAdsVisualSpreadGate& gate,
    float* const minimum_spread_degrees,
    float* const maximum_spread_degrees) noexcept {
    if (minimum_spread_degrees == nullptr ||
        maximum_spread_degrees == nullptr || !gate.hook_enabled ||
        gate.weapon_type != 0 ||
        !std::isfinite(gate.ads_spread_degrees) ||
        gate.ads_spread_degrees < 0.0F ||
        gate.ads_spread_degrees > kMaximumReasonableAdsSpreadDegrees) {
        return false;
    }
    *minimum_spread_degrees = gate.ads_spread_degrees;
    *maximum_spread_degrees = gate.ads_spread_degrees;
    return true;
}

}  // namespace wawvr::mod
