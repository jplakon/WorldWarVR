// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "xr_types.h"

#include <cstdint>
#include <string_view>

namespace wawvr::mod {

inline constexpr std::uint64_t kMaximumPublishedMuzzleAgeMilliseconds = 150;
inline constexpr std::uint64_t kMaximumPublishedMuzzleGenerationLag = 4;

struct PublishedWeaponMuzzleSnapshot final {
    bool valid{};
    std::uint64_t controller_generation{};
    std::uint64_t publication_milliseconds{};
    wawvr::xr::Vec3f origin{};
    bool projectile_basis_valid{};
    wawvr::xr::Basis3f projectile_basis{};
    bool weapon_root_basis_valid{};
    wawvr::xr::Basis3f weapon_root_basis{};
};

// Pure fail-closed freshness check shared by the game-thread hook and tests.
// T4 normally consumes the last rendered weapon pose a few command generations
// later, so a small bounded lag is accepted while future/stale publications are
// rejected.
[[nodiscard]] bool published_weapon_muzzle_is_fresh(
    const PublishedWeaponMuzzleSnapshot& muzzle,
    std::uint64_t controller_generation,
    std::uint64_t now_milliseconds) noexcept;

struct PhysicalMuzzleGate final {
    bool hook_enabled{};
    std::uintptr_t firing_entity{};
    std::uintptr_t local_player_entity{};
    std::int32_t entity_number{};
    bool has_client{};
    std::int32_t weapon_type{};
    bool controller_frame_current{};
    bool published_muzzle_fresh{};
};

// Weapon type zero is T4's ordinary bullet path and type two is its projectile
// path (including the Zombies Ray Gun) at the audited FireWeapon callsite.
// Both consume the WeaponParms filled by CalcMuzzlePoints; other weapon types
// retain the native origin.
[[nodiscard]] bool physical_muzzle_gate_allows(
    const PhysicalMuzzleGate& gate) noexcept;

struct AuthoritativeWeaponBasisGate final {
    bool override_requested{};
    bool physical_muzzle_allowed{};
    bool final_visible_basis_fresh{};
};

struct ExactBazookaWeaponRootLaunchGate final {
    bool exact_local_bazooka_route{};
    bool physical_muzzle_allowed{};
    bool published_weapon_root_fresh{};
};

struct BazookaViewmodelAlignment final {
    wawvr::xr::Vec3f corrected_root_origin{};
    wawvr::xr::Basis3f corrected_root_axis{};
    float correction_degrees{};
};

// The retail Bazooka's animated tube can be tilted relative to the rigid
// controller-owned viewmodel root. Build the shortest-arc visual-only root
// correction that puts the evaluated bore on the tracked launch axis while
// pivoting around the gripping hand. The correction is deliberately bounded
// and does not mutate controller attachments, retained poses, or ballistics.
// Rejected inputs leave the caller-owned output unchanged.
[[nodiscard]] bool calculate_bazooka_viewmodel_alignment(
    const wawvr::xr::Vec3f& root_origin,
    const wawvr::xr::Basis3f& root_axis,
    const wawvr::xr::Vec3f& grip_anchor_world,
    const wawvr::xr::Basis3f& evaluated_bore_basis,
    const wawvr::xr::Basis3f& tracked_launch_axis,
    BazookaViewmodelAlignment* alignment) noexcept;

// Select the final evaluated root of the Bazooka's weapon XModel. Unlike the
// outer viewmodel placement and animated muzzle tags, this bone rigidly owns
// the visible launch tube. A rejected selection leaves the output unchanged.
[[nodiscard]] bool select_exact_bazooka_weapon_root_launch_basis(
    const ExactBazookaWeaponRootLaunchGate& gate,
    const wawvr::xr::Basis3f& weapon_root_basis,
    wawvr::xr::Basis3f* launch_basis) noexcept;

// COD4 and T4 route ordinary bullets and type-two projectiles through the
// WeaponParms basis produced by CalcMuzzlePoints. Multiplayer already opts the
// complete basis into the tracked override. Single-player must do the same for
// every ordinary hitscan weapon: moving only the muzzle origin while retaining
// the native view/head direction produces the left/right trajectory error seen
// on the Mosin, M1 Garand and SVT-40. Type-two projectiles also opt in so a
// rocket leaves along the visible launcher barrel.
[[nodiscard]] bool authoritative_weapon_basis_is_requested(
    bool layout_requests_override,
    bool direct_ballistic_profile,
    std::int32_t weapon_type) noexcept;

// T4's evaluated viewmodel root is not guaranteed to point along the barrel.
// Build a roll-preserving orthonormal IW basis from an evaluated model/tag
// forward direction. Projectile weapons use this basis so asset-specific
// model rotations cannot send a rocket along the viewmodel root axis.
[[nodiscard]] bool build_evaluated_projectile_basis(
    const wawvr::xr::Vec3f& evaluated_barrel_direction,
    const wawvr::xr::Basis3f& roll_reference,
    wawvr::xr::Basis3f* projectile_basis) noexcept;

// A rear grip tag is not a bore reference: PPSh and SVT-40 can acquire an
// upward/lateral error from the grip-to-muzzle segment. Use their evaluated
// tag_flash forward instead. Keep this allowlist exact so other accepted
// rifles, unknown weapons and untested variants retain their existing path.
[[nodiscard]] bool hitscan_uses_tag_flash_forward(
    std::string_view internal_weapon_name) noexcept;

inline constexpr float kMinimumBazookaBoreLengthWorldUnits = 50.0F;
inline constexpr float kMaximumBazookaBoreLengthWorldUnits = 80.0F;
inline constexpr float kMinimumBazookaBoreFlashAlignment = 0.98F;
inline constexpr std::uint32_t kStableMissilesEntityFlag = 0x00020000U;

struct PhysicalRocketLauncherProfile final {
    std::string_view weapon_name;
    std::string_view required_viewmodel_name;
    float minimum_bore_length_world_units{};
    float maximum_bore_length_world_units{};
    bool use_rigid_tube_axis{};
};

inline constexpr PhysicalRocketLauncherProfile kBazookaPhysicalLauncher{
    "bazooka", {}, kMinimumBazookaBoreLengthWorldUnits,
    kMaximumBazookaBoreLengthWorldUnits, false};
// The native name deliberately has only one 'c'. The captured retail model's
// two tube rings share a Y/Z center and are rigidly owned by j_gun: its +X is
// the actual bore. tag_flash is slightly below the rear tag, so the tag segment
// validates the 65.239-unit tube but must not tilt the real bore by 0.28 degrees.
inline constexpr PhysicalRocketLauncherProfile kPanzerschreckPhysicalLauncher{
    "panzerschrek", "viewmodel_ger_panzerschreck_at", 64.0F, 66.0F, true};

[[nodiscard]] constexpr const PhysicalRocketLauncherProfile*
find_physical_rocket_launcher_profile(std::string_view name) noexcept {
    if (name == kBazookaPhysicalLauncher.weapon_name) {
        return &kBazookaPhysicalLauncher;
    }
    if (name == kPanzerschreckPhysicalLauncher.weapon_name) {
        return &kPanzerschreckPhysicalLauncher;
    }
    return nullptr;
}

[[nodiscard]] constexpr bool physical_launcher_model_matches(
    const PhysicalRocketLauncherProfile* profile,
    std::string_view viewmodel_name) noexcept {
    return (profile == &kBazookaPhysicalLauncher ||
            profile == &kPanzerschreckPhysicalLauncher) &&
           (profile->required_viewmodel_name.empty() ||
            profile->required_viewmodel_name == viewmodel_name);
}

// Exact-profile visual bounds: the accepted Bazooka keeps 25 degrees, while
// the Panzerschreck permits its measured 44.6-degree held idle under a bounded
// 60-degree limit. The native caller must validate the model and bore first.
[[nodiscard]] bool calculate_physical_launcher_viewmodel_alignment(
    const PhysicalRocketLauncherProfile* profile,
    const wawvr::xr::Vec3f& root_origin,
    const wawvr::xr::Basis3f& root_axis,
    const wawvr::xr::Vec3f& grip_anchor_world,
    const wawvr::xr::Basis3f& evaluated_bore_basis,
    const wawvr::xr::Basis3f& tracked_launch_axis,
    BazookaViewmodelAlignment* alignment) noexcept;

// The Bazooka viewmodel authors tag_brass at the rear of the launch tube and
// tag_flash at its mouth. Accept that measured segment only when its length is
// plausible and it points with tag_flash row zero. The complete flash basis is
// retained as the roll reference. Rejected inputs leave all outputs unchanged,
// allowing the caller to fall back atomically to tag_flash row zero.
[[nodiscard]] bool build_validated_bazooka_bore_basis(
    const wawvr::xr::Vec3f& tag_brass_world,
    const wawvr::xr::Vec3f& tag_flash_world,
    const wawvr::xr::Basis3f& tag_flash_basis,
    wawvr::xr::Basis3f* projectile_basis,
    float* bore_length_world_units = nullptr,
    float* flash_forward_alignment = nullptr) noexcept;

// Keeps the accepted Bazooka calculation unchanged. Other explicitly audited
// profiles may validate their measured tag span and select the rigid visible
// tube axis. Unknown profiles or inconsistent geometry fail atomically.
[[nodiscard]] bool build_validated_physical_launcher_bore_basis(
    const PhysicalRocketLauncherProfile* profile,
    const wawvr::xr::Vec3f& tag_brass_world,
    const wawvr::xr::Vec3f& tag_flash_world,
    const wawvr::xr::Basis3f& tag_flash_basis,
    const wawvr::xr::Basis3f* rigid_tube_basis,
    wawvr::xr::Basis3f* projectile_basis,
    float* bore_length_world_units = nullptr,
    float* flash_forward_alignment = nullptr) noexcept;

// Returns the exact normalized tracked forward used by the final local
// Bazooka G_FireRocket bridge. Native projectile direction is deliberately not
// an input: stock hip spread must not move the rocket away from the rigid
// controller-derived launch line. Rejected inputs do not modify the output.
[[nodiscard]] bool exact_bazooka_projectile_direction(
    const wawvr::xr::Basis3f& tracked_launch_basis,
    wawvr::xr::Vec3f* projectile_direction) noexcept;

// Stock WaW may deliberately curve Bazooka rockets after their configured
// destabilization distance. The exact local physical-Bazooka route marks only
// its returned missile stable so its impact remains on the launch-tube line.
// Rejected calls leave the caller-owned flags unchanged.
[[nodiscard]] bool apply_exact_bazooka_stable_missile_flag(
    bool exact_local_bazooka_route,
    std::uint32_t* entity_flags) noexcept;

// Converts IW's rendered forward/left/up basis to WeaponParms'
// forward/right/up contract. The three outputs are committed together only
// for an accepted fresh physical shot; rejected decisions preserve every
// caller-owned value bit-for-bit.
[[nodiscard]] bool apply_authoritative_weapon_basis_override(
    const AuthoritativeWeaponBasisGate& gate,
    const wawvr::xr::Basis3f& final_visible_basis,
    wawvr::xr::Vec3f* forward,
    wawvr::xr::Vec3f* right,
    wawvr::xr::Vec3f* up) noexcept;

inline constexpr float kMaximumReasonableAdsSpreadDegrees = 180.0F;

struct FixedAdsSpreadGate final {
    bool hook_enabled{};
    std::uintptr_t attacker{};
    std::uintptr_t local_player_entity{};
    std::int32_t entity_number{};
    bool has_client{};
    std::int32_t weapon_type{};
    float ads_spread_degrees{};
};

// Replaces only the authoritative per-shot cone scalar passed from
// FireWeapon to Bullet_Fire. There is deliberately no ADS/player-state input:
// locomotion, FOV, sensitivity, animation, recoil and ammo remain native.
// A nonzero per-weapon ADS value is retained, so shotgun pellets do not
// collapse onto a single ray. Rejected decisions leave `spread_degrees`
// bit-for-bit unchanged.
[[nodiscard]] bool apply_fixed_ads_spread_override(
    const FixedAdsSpreadGate& gate,
    float* spread_degrees) noexcept;

struct FixedAdsVisualSpreadGate final {
    bool hook_enabled{};
    std::int32_t weapon_type{};
    float ads_spread_degrees{};
};

// CG_DrawBulletImpacts obtains local client min/max spread independently of
// the authoritative Bullet_Fire call. Collapse both visual bounds to the
// weapon's authored ADS cone so tracers and predicted impacts follow the same
// tracked shot without changing pellet count or native weapon state.
[[nodiscard]] bool apply_fixed_ads_visual_spread_override(
    const FixedAdsVisualSpreadGate& gate,
    float* minimum_spread_degrees,
    float* maximum_spread_degrees) noexcept;

}  // namespace wawvr::mod
