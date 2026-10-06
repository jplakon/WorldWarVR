// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "gameplay/manual_reload_logic.hpp"

#include "xr_types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace wawvr::mod {

enum class BoltActionWeaponProfileId : std::uint8_t {
    None,
    Kar98,
    Kar98ScopedZombie,
    Springfield,
    Mosin,
    Type99,
    Type99Bayonet,
    Type99Scoped,
    MosinScoped,
};

struct BoltActionWeaponBinding final {
    BoltActionWeaponProfileId profile_id{BoltActionWeaponProfileId::None};
    std::int32_t weapon_index{};
};

// One atomic word keeps the validated profile and map-local T4 index coherent
// across render, gameplay, and PM hook threads.
[[nodiscard]] constexpr std::uint64_t pack_bolt_action_weapon_binding(
    const BoltActionWeaponBinding binding) noexcept {
    if (binding.profile_id == BoltActionWeaponProfileId::None ||
        binding.weapon_index <= 0) {
        return 0;
    }
    return (static_cast<std::uint64_t>(binding.profile_id) << 32U) |
        static_cast<std::uint32_t>(binding.weapon_index);
}

[[nodiscard]] constexpr BoltActionWeaponBinding
unpack_bolt_action_weapon_binding(const std::uint64_t packed) noexcept {
    return {
        .profile_id = static_cast<BoltActionWeaponProfileId>(
            (packed >> 32U) & 0xFFU),
        .weapon_index = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(packed & 0xFFFFFFFFU)),
    };
}

[[nodiscard]] constexpr bool bolt_action_weapon_binding_matches(
    const std::uint64_t packed,
    const BoltActionWeaponProfileId profile_id,
    const std::int32_t weapon_index) noexcept {
    return packed != 0 &&
        pack_bolt_action_weapon_binding({profile_id, weapon_index}) == packed;
}

inline constexpr std::uint8_t kMaximumAuthoredFeedRoundSurfaces = 8;
inline constexpr std::uint8_t kMaximumMovingBoltTags = 2;
inline constexpr std::uint8_t
    kMaximumExpectedMultiRigidRoundBoneTags = 4;

// Immutable calibration and asset identity for one physical bolt-action path.
// The T4 weapon index is intentionally absent: retail assigns indices in load
// order, so the runtime must bind a validated profile to the current index.
struct BoltActionWeaponProfile final {
    BoltActionWeaponProfileId id{BoltActionWeaponProfileId::None};
    const char* diagnostic_name{};
    const char* internal_weapon_name{};
    const char* viewmodel_model_name{};
    // The first entry is the physical grab anchor named by bolt_tag_name.
    // Additional entries are independently rigid pieces that must receive
    // the same manual travel without copying another piece's closed pose.
    const char* bolt_tag_name{};
    std::array<const char*, kMaximumMovingBoltTags> moving_bolt_tag_names{};
    std::uint8_t moving_bolt_tag_count{};
    gameplay::ReloadProfileKind reload_kind{
        gameplay::ReloadProfileKind::NativeOnly};

    float bolt_travel_units{};
    float bolt_open_threshold{};
    float bolt_closed_threshold{};
    float trigger_engage{};
    float trigger_release{};
    float bolt_grab_radius_units{};

    std::int32_t idle_anim{};
    std::int32_t rechamber_hip_anim{};
    std::int32_t rechamber_ads_anim{};

    const char* feed_device_material_name{};
    // Optional exact rigid-bone anchor for the detachable feed device. The
    // accepted Mosin surface is authored on tag_stripper; older accepted
    // profiles remain material/topology pinned until their bone names are
    // independently inventoried.
    const char* feed_device_bone_tag_name{};
    const char* round_material_name{};
    // Most accepted rifles have one rigid list per round surface. A profile
    // may explicitly admit additional multi-rigid authored round props only
    // when it also names the exact one-rigid-list cluster to detach.
    const char* detached_round_bone_tag_name{};
    bool allow_multi_rigid_round_surfaces{};
    // A multi-rigid round surface is admissible only when its complete rigid
    // bone set exactly matches these tags. Empty for the accepted one-rigid
    // Kar98 and Springfield recipes.
    std::array<const char*, kMaximumExpectedMultiRigidRoundBoneTags>
        expected_multi_rigid_round_bone_tag_names{};
    std::uint8_t expected_multi_rigid_round_bone_tag_count{};
    std::uint8_t authored_round_surface_count{};
    wawvr::xr::Vec3f feed_device_hand_offset{};
    float insertion_radius_units{};
    float receiver_segment_minimum{};
    float receiver_segment_maximum{};
    float receiver_top_offset{};
    // Opt-in for a physically observed native-angle handoff mismatch. Profiles
    // remain on the accepted SP path unless separately validated for the
    // direct rendered forward/right/up WeaponParms handoff.
    bool direct_ballistic_basis{};
};

namespace detail {

[[nodiscard]] constexpr bool finite_profile_float(const float value) noexcept {
    constexpr float maximum = (std::numeric_limits<float>::max)();
    return value == value && value >= -maximum && value <= maximum;
}

[[nodiscard]] constexpr bool nonempty_profile_string(
    const char* const value) noexcept {
    return value != nullptr && value[0] != '\0';
}

[[nodiscard]] constexpr bool equal_profile_strings(
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

}  // namespace detail

// Structural validation is constexpr so every shipping registry entry can be
// rejected at build time as well as checked at runtime/test seams.
[[nodiscard]] constexpr bool validate_bolt_action_weapon_profile(
    const BoltActionWeaponProfile& profile) noexcept {
    if (profile.id == BoltActionWeaponProfileId::None ||
        !detail::nonempty_profile_string(profile.diagnostic_name) ||
        !detail::nonempty_profile_string(profile.internal_weapon_name) ||
        !detail::nonempty_profile_string(profile.viewmodel_model_name) ||
        !detail::nonempty_profile_string(profile.bolt_tag_name) ||
        !detail::nonempty_profile_string(
            profile.feed_device_material_name) ||
        !detail::nonempty_profile_string(profile.round_material_name) ||
        profile.reload_kind !=
            gameplay::ReloadProfileKind::InternalStripperClip) {
        return false;
    }

    if (profile.moving_bolt_tag_count == 0 ||
        profile.moving_bolt_tag_count > kMaximumMovingBoltTags ||
        !detail::equal_profile_strings(
            profile.moving_bolt_tag_names[0], profile.bolt_tag_name)) {
        return false;
    }
    for (std::size_t index = 0;
         index < profile.moving_bolt_tag_names.size(); ++index) {
        const char* const tag = profile.moving_bolt_tag_names[index];
        if (index >= profile.moving_bolt_tag_count) {
            if (tag != nullptr) {
                return false;
            }
            continue;
        }
        if (!detail::nonempty_profile_string(tag) ||
            (profile.feed_device_bone_tag_name != nullptr &&
             detail::equal_profile_strings(
                 tag, profile.feed_device_bone_tag_name))) {
            return false;
        }
        for (std::size_t peer = 0; peer < index; ++peer) {
            if (detail::equal_profile_strings(
                    tag, profile.moving_bolt_tag_names[peer])) {
                return false;
            }
        }
    }

    if (!detail::finite_profile_float(profile.bolt_travel_units) ||
        profile.bolt_travel_units <= 0.0F ||
        !detail::finite_profile_float(profile.bolt_closed_threshold) ||
        !detail::finite_profile_float(profile.bolt_open_threshold) ||
        profile.bolt_closed_threshold < 0.0F ||
        profile.bolt_closed_threshold >= profile.bolt_open_threshold ||
        profile.bolt_open_threshold > 1.0F ||
        !detail::finite_profile_float(profile.trigger_release) ||
        !detail::finite_profile_float(profile.trigger_engage) ||
        profile.trigger_release < 0.0F ||
        profile.trigger_release >= profile.trigger_engage ||
        profile.trigger_engage > 1.0F ||
        !detail::finite_profile_float(profile.bolt_grab_radius_units) ||
        profile.bolt_grab_radius_units <= 0.0F) {
        return false;
    }

    if (profile.idle_anim < 0 || profile.rechamber_hip_anim < 0 ||
        profile.rechamber_ads_anim < 0 ||
        profile.idle_anim == profile.rechamber_hip_anim ||
        profile.idle_anim == profile.rechamber_ads_anim ||
        profile.rechamber_hip_anim == profile.rechamber_ads_anim ||
        profile.authored_round_surface_count == 0 ||
        profile.authored_round_surface_count >
            kMaximumAuthoredFeedRoundSurfaces) {
        return false;
    }
    if ((profile.feed_device_bone_tag_name != nullptr &&
         profile.feed_device_bone_tag_name[0] == '\0') ||
        (profile.detached_round_bone_tag_name != nullptr &&
         profile.detached_round_bone_tag_name[0] == '\0') ||
        (profile.allow_multi_rigid_round_surfaces &&
         !detail::nonempty_profile_string(
             profile.detached_round_bone_tag_name))) {
        return false;
    }
    if (detail::equal_profile_strings(
            profile.feed_device_material_name,
            profile.round_material_name) ||
        (profile.feed_device_bone_tag_name != nullptr &&
         (detail::equal_profile_strings(
              profile.feed_device_bone_tag_name,
              profile.detached_round_bone_tag_name) ||
          detail::equal_profile_strings(
              profile.feed_device_bone_tag_name,
              profile.bolt_tag_name)))) {
        return false;
    }
    if (profile.expected_multi_rigid_round_bone_tag_count >
            kMaximumExpectedMultiRigidRoundBoneTags ||
        profile.allow_multi_rigid_round_surfaces !=
            (profile.expected_multi_rigid_round_bone_tag_count != 0) ||
        (profile.allow_multi_rigid_round_surfaces &&
         profile.authored_round_surface_count < 2)) {
        return false;
    }
    for (std::size_t index = 0;
         index <
             profile.expected_multi_rigid_round_bone_tag_names.size();
         ++index) {
        const char* const tag =
            profile.expected_multi_rigid_round_bone_tag_names[index];
        if (index >=
            profile.expected_multi_rigid_round_bone_tag_count) {
            if (tag != nullptr) {
                return false;
            }
            continue;
        }
        if (!detail::nonempty_profile_string(tag) ||
            detail::equal_profile_strings(
                tag, profile.detached_round_bone_tag_name) ||
            (profile.feed_device_bone_tag_name != nullptr &&
             detail::equal_profile_strings(
                 tag, profile.feed_device_bone_tag_name))) {
            return false;
        }
        for (std::size_t peer = 0; peer < index; ++peer) {
            if (detail::equal_profile_strings(
                    tag,
                    profile
                        .expected_multi_rigid_round_bone_tag_names[peer])) {
                return false;
            }
        }
    }

    if (!detail::finite_profile_float(profile.feed_device_hand_offset.x) ||
        !detail::finite_profile_float(profile.feed_device_hand_offset.y) ||
        !detail::finite_profile_float(profile.feed_device_hand_offset.z) ||
        !detail::finite_profile_float(profile.insertion_radius_units) ||
        profile.insertion_radius_units <= 0.0F ||
        !detail::finite_profile_float(profile.receiver_segment_minimum) ||
        !detail::finite_profile_float(profile.receiver_segment_maximum) ||
        profile.receiver_segment_minimum < 0.0F ||
        profile.receiver_segment_minimum >=
            profile.receiver_segment_maximum ||
        profile.receiver_segment_maximum > 1.0F ||
        !detail::finite_profile_float(profile.receiver_top_offset)) {
        return false;
    }

    return true;
}

inline constexpr BoltActionWeaponProfile kKar98BoltActionWeaponProfile{
    .id = BoltActionWeaponProfileId::Kar98,
    .diagnostic_name = "Kar98",
    .internal_weapon_name = "kar98k",
    .viewmodel_model_name = "viewmodel_ger_kar98_rifle",
    .bolt_tag_name = "j_bolt",
    .moving_bolt_tag_names = {"j_bolt"},
    .moving_bolt_tag_count = 1,
    .reload_kind = gameplay::ReloadProfileKind::InternalStripperClip,
    .bolt_travel_units = 3.75F,
    .bolt_open_threshold = 0.95F,
    .bolt_closed_threshold = 0.05F,
    .trigger_engage = 0.65F,
    .trigger_release = 0.35F,
    .bolt_grab_radius_units = 10.0F,
    .idle_anim = 0,
    .rechamber_hip_anim = 4,
    .rechamber_ads_anim = 7,
    .feed_device_material_name = "mc/mtl_stripper_clip",
    .round_material_name = "mc/mtl_k98round",
    .authored_round_surface_count = 2,
    .feed_device_hand_offset = {3.25F, 0.0F, 1.25F},
    .insertion_radius_units = 9.0F,
    .receiver_segment_minimum = 0.05F,
    .receiver_segment_maximum = 0.75F,
    .receiver_top_offset = 3.0F,
};

static_assert(validate_bolt_action_weapon_profile(
    kKar98BoltActionWeaponProfile));

inline constexpr BoltActionWeaponProfile
    kKar98ScopedZombieBoltActionWeaponProfile = [] {
        BoltActionWeaponProfile profile = kKar98BoltActionWeaponProfile;
        profile.id = BoltActionWeaponProfileId::Kar98ScopedZombie;
        profile.diagnostic_name = "Scoped Kar98 Zombies";
        profile.internal_weapon_name = "kar98k_scoped_zombie";
        profile.viewmodel_model_name = "viewmodel_ger_kar98_scoped_rifle";
        return profile;
    }();

static_assert(validate_bolt_action_weapon_profile(
    kKar98ScopedZombieBoltActionWeaponProfile));

inline constexpr BoltActionWeaponProfile kSpringfieldBoltActionWeaponProfile{
    .id = BoltActionWeaponProfileId::Springfield,
    .diagnostic_name = "Springfield",
    .internal_weapon_name = "springfield",
    .viewmodel_model_name = "viewmodel_usa_springfield_rifle",
    .bolt_tag_name = "j_bolt",
    .moving_bolt_tag_names = {"j_bolt"},
    .moving_bolt_tag_count = 1,
    .reload_kind = gameplay::ReloadProfileKind::InternalStripperClip,
    .bolt_travel_units = 4.225902390F,
    .bolt_open_threshold = 0.95F,
    .bolt_closed_threshold = 0.05F,
    .trigger_engage = 0.65F,
    .trigger_release = 0.35F,
    .bolt_grab_radius_units = 10.0F,
    .idle_anim = 0,
    .rechamber_hip_anim = 4,
    .rechamber_ads_anim = 7,
    .feed_device_material_name = "mc/mtl_stripper_clip",
    .round_material_name = "mc/mtl_ammo_belt",
    .authored_round_surface_count = 1,
    .feed_device_hand_offset = {3.25F, 0.0F, 1.25F},
    .insertion_radius_units = 9.0F,
    .receiver_segment_minimum = 0.05F,
    .receiver_segment_maximum = 0.75F,
    .receiver_top_offset = 3.0F,
};

static_assert(validate_bolt_action_weapon_profile(
    kSpringfieldBoltActionWeaponProfile));

inline constexpr BoltActionWeaponProfile kMosinBoltActionWeaponProfile{
    .id = BoltActionWeaponProfileId::Mosin,
    .diagnostic_name = "Mosin-Nagant",
    .internal_weapon_name = "mosin_rifle",
    .viewmodel_model_name = "viewmodel_rus_mosinnagant_rifle",
    .bolt_tag_name = "j_bolt",
    .moving_bolt_tag_names = {"j_bolt", "j_bolt1"},
    .moving_bolt_tag_count = 2,
    .reload_kind = gameplay::ReloadProfileKind::InternalStripperClip,
    .bolt_travel_units = 1.889505966F,
    .bolt_open_threshold = 0.95F,
    .bolt_closed_threshold = 0.05F,
    .trigger_engage = 0.65F,
    .trigger_release = 0.35F,
    .bolt_grab_radius_units = 10.0F,
    .idle_anim = 0,
    .rechamber_hip_anim = 4,
    .rechamber_ads_anim = 7,
    .feed_device_material_name = "mc/mtl_stripper_clip",
    .feed_device_bone_tag_name = "tag_stripper",
    .round_material_name = "mc/mtl_ammo_belt",
    .detached_round_bone_tag_name = "j_clip",
    .allow_multi_rigid_round_surfaces = true,
    .expected_multi_rigid_round_bone_tag_names = {
        "tag_round1", "tag_round2"},
    .expected_multi_rigid_round_bone_tag_count = 2,
    .authored_round_surface_count = 2,
    .feed_device_hand_offset = {3.25F, 0.0F, 1.25F},
    .insertion_radius_units = 9.0F,
    .receiver_segment_minimum = 0.05F,
    .receiver_segment_maximum = 0.75F,
    .receiver_top_offset = 3.0F,
    .direct_ballistic_basis = true,
};

static_assert(validate_bolt_action_weapon_profile(
    kMosinBoltActionWeaponProfile));

inline constexpr BoltActionWeaponProfile
    kMosinScopedBoltActionWeaponProfile = [] {
        BoltActionWeaponProfile profile = kMosinBoltActionWeaponProfile;
        profile.id = BoltActionWeaponProfileId::MosinScoped;
        profile.diagnostic_name = "Mosin-Nagant Scoped";
        profile.internal_weapon_name = "mosin_rifle_scoped";
        profile.viewmodel_model_name =
            "viewmodel_rus_mosinnagant_scoped_rifle";
        return profile;
    }();

static_assert(validate_bolt_action_weapon_profile(
    kMosinScopedBoltActionWeaponProfile));

inline constexpr BoltActionWeaponProfile kType99BoltActionWeaponProfile{
    .id = BoltActionWeaponProfileId::Type99,
    .diagnostic_name = "Type 99 Arisaka",
    .internal_weapon_name = "type99_rifle",
    .viewmodel_model_name = "viewmodel_jap_type99_rifle",
    .bolt_tag_name = "j_bolt",
    .moving_bolt_tag_names = {"j_bolt"},
    .moving_bolt_tag_count = 1,
    .reload_kind = gameplay::ReloadProfileKind::InternalStripperClip,
    .bolt_travel_units = 2.364536F,
    .bolt_open_threshold = 0.95F,
    .bolt_closed_threshold = 0.05F,
    .trigger_engage = 0.65F,
    .trigger_release = 0.35F,
    .bolt_grab_radius_units = 10.0F,
    .idle_anim = 0,
    .rechamber_hip_anim = 4,
    .rechamber_ads_anim = 7,
    .feed_device_material_name = "mc/mtl_stripper_clip",
    .feed_device_bone_tag_name = "j_stripper",
    .round_material_name = "mc/mtl_ammo_belt",
    .detached_round_bone_tag_name = "j_clip",
    .allow_multi_rigid_round_surfaces = true,
    .expected_multi_rigid_round_bone_tag_names = {"j_round", "j_round1"},
    .expected_multi_rigid_round_bone_tag_count = 2,
    .authored_round_surface_count = 2,
    .feed_device_hand_offset = {3.25F, 0.0F, 1.25F},
    .insertion_radius_units = 9.0F,
    .receiver_segment_minimum = 0.05F,
    .receiver_segment_maximum = 0.75F,
    .receiver_top_offset = 3.0F,
};

static_assert(validate_bolt_action_weapon_profile(
    kType99BoltActionWeaponProfile));

inline constexpr BoltActionWeaponProfile
    kType99BayonetBoltActionWeaponProfile = [] {
        BoltActionWeaponProfile profile = kType99BoltActionWeaponProfile;
        profile.id = BoltActionWeaponProfileId::Type99Bayonet;
        profile.diagnostic_name = "Type 99 Arisaka Bayonet";
        profile.internal_weapon_name = "type99_rifle_bayonet";
        profile.viewmodel_model_name = "viewmodel_jap_type99_rifle_bayonet";
        return profile;
    }();

static_assert(validate_bolt_action_weapon_profile(
    kType99BayonetBoltActionWeaponProfile));

inline constexpr BoltActionWeaponProfile
    kType99ScopedBoltActionWeaponProfile = [] {
        BoltActionWeaponProfile profile = kType99BoltActionWeaponProfile;
        profile.id = BoltActionWeaponProfileId::Type99Scoped;
        profile.diagnostic_name = "Type 99 Arisaka Scoped";
        profile.internal_weapon_name = "type99_rifle_scoped";
        profile.viewmodel_model_name = "viewmodel_jap_type99_rifle_scoped";
        return profile;
    }();

static_assert(validate_bolt_action_weapon_profile(
    kType99ScopedBoltActionWeaponProfile));

[[nodiscard]] std::span<const BoltActionWeaponProfile* const>
bolt_action_weapon_profiles() noexcept;

[[nodiscard]] const BoltActionWeaponProfile*
find_bolt_action_weapon_profile(BoltActionWeaponProfileId id) noexcept;

[[nodiscard]] const BoltActionWeaponProfile*
find_bolt_action_weapon_profile_by_internal_name(
    std::string_view internal_weapon_name) noexcept;

[[nodiscard]] const BoltActionWeaponProfile*
find_bolt_action_weapon_profile_by_viewmodel_model_name(
    std::string_view viewmodel_model_name) noexcept;

[[nodiscard]] bool validate_bolt_action_weapon_profile_registry() noexcept;

}  // namespace wawvr::mod
