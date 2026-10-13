// SPDX-License-Identifier: GPL-3.0-only
#include "bolt_action_weapon_profile.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

using wawvr::gameplay::ReloadProfileKind;
using wawvr::mod::BoltActionWeaponProfile;
using wawvr::mod::BoltActionWeaponProfileId;
using wawvr::mod::bolt_action_weapon_binding_matches;
using wawvr::mod::bolt_action_weapon_profiles;
using wawvr::mod::find_bolt_action_weapon_profile;
using wawvr::mod::find_bolt_action_weapon_profile_by_internal_name;
using wawvr::mod::find_bolt_action_weapon_profile_by_viewmodel_model_name;
using wawvr::mod::kKar98BoltActionWeaponProfile;
using wawvr::mod::kKar98ScopedZombieBoltActionWeaponProfile;
using wawvr::mod::kMaximumAuthoredFeedRoundSurfaces;
using wawvr::mod::kMaximumExpectedMultiRigidRoundBoneTags;
using wawvr::mod::kMaximumMovingBoltTags;
using wawvr::mod::kMosinBoltActionWeaponProfile;
using wawvr::mod::kMosinScopedBoltActionWeaponProfile;
using wawvr::mod::kSpringfieldBoltActionWeaponProfile;
using wawvr::mod::kType99BayonetBoltActionWeaponProfile;
using wawvr::mod::kType99BoltActionWeaponProfile;
using wawvr::mod::kType99ScopedBoltActionWeaponProfile;
using wawvr::mod::pack_bolt_action_weapon_binding;
using wawvr::mod::unpack_bolt_action_weapon_binding;
using wawvr::mod::validate_bolt_action_weapon_profile;
using wawvr::mod::validate_bolt_action_weapon_profile_registry;

void expect(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

[[nodiscard]] bool exact_float(
    const float actual,
    const float expected) noexcept {
    return actual == expected;
}

void test_registry_contains_exact_accepted_kar98_profile() {
    const BoltActionWeaponProfile* const profile =
        find_bolt_action_weapon_profile(BoltActionWeaponProfileId::Kar98);
    expect(profile == &kKar98BoltActionWeaponProfile,
           "the immutable registry exposes the canonical Kar98 profile");
    expect(!profile->direct_ballistic_basis,
           "the accepted Kar98 keeps its native SP direction path");
    expect(validate_bolt_action_weapon_profile_registry() &&
                bolt_action_weapon_profiles().size() == 8,
            "the shipping Kar98, scoped Zombies Kar98, Springfield, Mosin family, and exact Type 99 family registry validates");
    expect(profile->id == BoltActionWeaponProfileId::Kar98 &&
               std::string_view(profile->diagnostic_name) == "Kar98" &&
               std::string_view(profile->internal_weapon_name) == "kar98k" &&
               std::string_view(profile->viewmodel_model_name) ==
                   "viewmodel_ger_kar98_rifle" &&
               std::string_view(profile->bolt_tag_name) == "j_bolt" &&
               profile->moving_bolt_tag_count == 1 &&
               std::string_view(profile->moving_bolt_tag_names[0]) ==
                   "j_bolt" &&
               profile->moving_bolt_tag_names[1] == nullptr &&
               profile->reload_kind ==
                   ReloadProfileKind::InternalStripperClip,
           "Kar98 identity, moving bolt, and reload policy remain exact");
    expect(exact_float(profile->bolt_travel_units, 3.75F) &&
               exact_float(profile->bolt_open_threshold, 0.95F) &&
               exact_float(profile->bolt_closed_threshold, 0.05F) &&
               exact_float(profile->trigger_engage, 0.65F) &&
               exact_float(profile->trigger_release, 0.35F) &&
               exact_float(profile->bolt_grab_radius_units, 10.0F),
           "Kar98 physical bolt calibration remains exact");
    expect(profile->idle_anim == 0 && profile->rechamber_hip_anim == 4 &&
               profile->rechamber_ads_anim == 7,
           "Kar98 automatic-rechamber animation policy remains exact");
    expect(std::string_view(profile->feed_device_material_name) ==
                    "mc/mtl_stripper_clip" &&
               profile->feed_device_bone_tag_name == nullptr &&
               std::string_view(profile->round_material_name) ==
                   "mc/mtl_k98round" &&
               profile->detached_round_bone_tag_name == nullptr &&
               !profile->allow_multi_rigid_round_surfaces &&
               profile->expected_multi_rigid_round_bone_tag_count == 0 &&
               profile->expected_multi_rigid_round_bone_tag_names[0] ==
                   nullptr &&
               profile->authored_round_surface_count == 2,
           "Kar98 clip and round surface recipe remains exact");
    expect(exact_float(profile->feed_device_hand_offset.x, 3.25F) &&
               exact_float(profile->feed_device_hand_offset.y, 0.0F) &&
               exact_float(profile->feed_device_hand_offset.z, 1.25F) &&
               exact_float(profile->insertion_radius_units, 9.0F) &&
               exact_float(profile->receiver_segment_minimum, 0.05F) &&
               exact_float(profile->receiver_segment_maximum, 0.75F) &&
               exact_float(profile->receiver_top_offset, 3.0F),
           "Kar98 held-clip and top-feed receiver calibration remains exact");
}

void test_registry_contains_exact_springfield_profile() {
    const BoltActionWeaponProfile* const profile =
        find_bolt_action_weapon_profile(
            BoltActionWeaponProfileId::Springfield);
    expect(profile == &kSpringfieldBoltActionWeaponProfile,
           "the immutable registry exposes the canonical Springfield profile");
    expect(!profile->direct_ballistic_basis,
           "the accepted Springfield keeps its native SP direction path");
    expect(profile->id == BoltActionWeaponProfileId::Springfield &&
               std::string_view(profile->diagnostic_name) == "Springfield" &&
               std::string_view(profile->internal_weapon_name) ==
                   "springfield" &&
               std::string_view(profile->viewmodel_model_name) ==
                   "viewmodel_usa_springfield_rifle" &&
               std::string_view(profile->bolt_tag_name) == "j_bolt" &&
               profile->moving_bolt_tag_count == 1 &&
               std::string_view(profile->moving_bolt_tag_names[0]) ==
                   "j_bolt" &&
               profile->moving_bolt_tag_names[1] == nullptr &&
               profile->reload_kind ==
                   ReloadProfileKind::InternalStripperClip,
           "Springfield identity, moving bolt, and reload policy are exact");
    expect(exact_float(profile->bolt_travel_units, 4.225902390F) &&
               exact_float(profile->bolt_open_threshold, 0.95F) &&
               exact_float(profile->bolt_closed_threshold, 0.05F) &&
               exact_float(profile->trigger_engage, 0.65F) &&
               exact_float(profile->trigger_release, 0.35F) &&
               exact_float(profile->bolt_grab_radius_units, 10.0F),
           "Springfield physical bolt calibration is exact");
    expect(profile->idle_anim == 0 && profile->rechamber_hip_anim == 4 &&
               profile->rechamber_ads_anim == 7,
           "Springfield automatic-rechamber animation policy is exact");
    expect(std::string_view(profile->feed_device_material_name) ==
                    "mc/mtl_stripper_clip" &&
               profile->feed_device_bone_tag_name == nullptr &&
               std::string_view(profile->round_material_name) ==
                   "mc/mtl_ammo_belt" &&
               profile->detached_round_bone_tag_name == nullptr &&
               !profile->allow_multi_rigid_round_surfaces &&
               profile->expected_multi_rigid_round_bone_tag_count == 0 &&
               profile->expected_multi_rigid_round_bone_tag_names[0] ==
                   nullptr &&
               profile->authored_round_surface_count == 1,
           "Springfield clip and authored round-cluster recipe is exact");
}

void test_registry_contains_exact_mosin_profile() {
    const BoltActionWeaponProfile* const profile =
        find_bolt_action_weapon_profile(BoltActionWeaponProfileId::Mosin);
    expect(profile == &kMosinBoltActionWeaponProfile,
           "the immutable registry exposes the canonical Mosin profile");
    expect(profile->direct_ballistic_basis,
           "the Mosin opts into the physically required direct barrel basis");
    expect(profile->id == BoltActionWeaponProfileId::Mosin &&
               std::string_view(profile->diagnostic_name) ==
                   "Mosin-Nagant" &&
               std::string_view(profile->internal_weapon_name) ==
                   "mosin_rifle" &&
               std::string_view(profile->viewmodel_model_name) ==
                   "viewmodel_rus_mosinnagant_rifle" &&
               std::string_view(profile->bolt_tag_name) == "j_bolt" &&
               profile->moving_bolt_tag_count == 2 &&
               std::string_view(profile->moving_bolt_tag_names[0]) ==
                   "j_bolt" &&
               std::string_view(profile->moving_bolt_tag_names[1]) ==
                   "j_bolt1" &&
               profile->reload_kind ==
                   ReloadProfileKind::InternalStripperClip,
           "Mosin identity, both moving bolt pieces, and reload policy are exact");
    expect(exact_float(profile->bolt_travel_units, 1.889505966F) &&
               exact_float(profile->bolt_open_threshold, 0.95F) &&
               exact_float(profile->bolt_closed_threshold, 0.05F) &&
               exact_float(profile->trigger_engage, 0.65F) &&
               exact_float(profile->trigger_release, 0.35F) &&
               exact_float(profile->bolt_grab_radius_units, 10.0F),
           "Mosin physical bolt calibration is exact");
    expect(profile->idle_anim == 0 && profile->rechamber_hip_anim == 4 &&
               profile->rechamber_ads_anim == 7,
           "Mosin automatic-rechamber animation policy is exact");
    expect(std::string_view(profile->feed_device_material_name) ==
                    "mc/mtl_stripper_clip" &&
               std::string_view(profile->feed_device_bone_tag_name) ==
                   "tag_stripper" &&
               std::string_view(profile->round_material_name) ==
                   "mc/mtl_ammo_belt" &&
               std::string_view(profile->detached_round_bone_tag_name) ==
                   "j_clip" &&
               profile->allow_multi_rigid_round_surfaces &&
               profile->expected_multi_rigid_round_bone_tag_count == 2 &&
               std::string_view(
                   profile->expected_multi_rigid_round_bone_tag_names[0]) ==
                   "tag_round1" &&
               std::string_view(
                   profile->expected_multi_rigid_round_bone_tag_names[1]) ==
                   "tag_round2" &&
               profile->expected_multi_rigid_round_bone_tag_names[2] ==
                   nullptr &&
               profile->authored_round_surface_count == 2,
           "Mosin clip recipe selects j_clip and exactly pins both loose-round bones");
}

void test_registry_contains_exact_scoped_mosin_profile() {
    const BoltActionWeaponProfile* const profile =
        find_bolt_action_weapon_profile(
            BoltActionWeaponProfileId::MosinScoped);
    expect(profile == &kMosinScopedBoltActionWeaponProfile,
           "the immutable registry exposes the campaign scoped Mosin profile");
    expect(profile->id == BoltActionWeaponProfileId::MosinScoped &&
               std::string_view(profile->diagnostic_name) ==
                   "Mosin-Nagant Scoped" &&
               std::string_view(profile->internal_weapon_name) ==
                   "mosin_rifle_scoped" &&
               std::string_view(profile->viewmodel_model_name) ==
                   "viewmodel_rus_mosinnagant_scoped_rifle",
           "the scoped Mosin identity exactly matches the player-held Vendetta weapon and viewmodel");
    expect(std::string_view(profile->bolt_tag_name) == "j_bolt" &&
               profile->moving_bolt_tag_count == 2 &&
               std::string_view(profile->moving_bolt_tag_names[0]) ==
                   "j_bolt" &&
               std::string_view(profile->moving_bolt_tag_names[1]) ==
                   "j_bolt1" &&
               exact_float(profile->bolt_travel_units, 1.889505966F) &&
               exact_float(profile->bolt_open_threshold, 0.95F) &&
               exact_float(profile->bolt_closed_threshold, 0.05F) &&
               exact_float(profile->trigger_engage, 0.65F) &&
               exact_float(profile->trigger_release, 0.35F) &&
               exact_float(profile->bolt_grab_radius_units, 10.0F) &&
               profile->reload_kind ==
                   ReloadProfileKind::InternalStripperClip &&
               profile->idle_anim == 0 &&
               profile->rechamber_hip_anim == 4 &&
               profile->rechamber_ads_anim == 7 &&
               profile->direct_ballistic_basis,
           "the scoped Mosin pins its inventoried two-piece bolt, input, animation, reload, and ballistic calibration");
    expect(std::string_view(profile->feed_device_material_name) ==
                   "mc/mtl_stripper_clip" &&
               std::string_view(profile->feed_device_bone_tag_name) ==
                   "tag_stripper" &&
               std::string_view(profile->round_material_name) ==
                   "mc/mtl_ammo_belt" &&
               std::string_view(profile->detached_round_bone_tag_name) ==
                   "j_clip" &&
               profile->allow_multi_rigid_round_surfaces &&
               profile->expected_multi_rigid_round_bone_tag_count == 2 &&
               std::string_view(
                   profile->expected_multi_rigid_round_bone_tag_names[0]) ==
                   "tag_round1" &&
               std::string_view(
                   profile->expected_multi_rigid_round_bone_tag_names[1]) ==
                   "tag_round2" &&
               profile->expected_multi_rigid_round_bone_tag_names[2] ==
                   nullptr &&
               profile->expected_multi_rigid_round_bone_tag_names[3] ==
                   nullptr &&
               profile->authored_round_surface_count == 2 &&
               exact_float(profile->feed_device_hand_offset.x, 3.25F) &&
               exact_float(profile->feed_device_hand_offset.y, 0.0F) &&
               exact_float(profile->feed_device_hand_offset.z, 1.25F) &&
               exact_float(profile->insertion_radius_units, 9.0F) &&
               exact_float(profile->receiver_segment_minimum, 0.05F) &&
               exact_float(profile->receiver_segment_maximum, 0.75F) &&
               exact_float(profile->receiver_top_offset, 3.0F),
           "the scoped Mosin pins its inventoried stripper clip, two round surfaces, and receiver calibration");
}

void test_registry_contains_exact_type99_profile() {
    const BoltActionWeaponProfile* const profile =
        find_bolt_action_weapon_profile(BoltActionWeaponProfileId::Type99);
    expect(profile == &kType99BoltActionWeaponProfile,
           "the immutable registry exposes the canonical Type 99 profile");
    expect(!profile->direct_ballistic_basis,
           "the Type 99 retains the native SP direction path until independently required otherwise");
    expect(profile->id == BoltActionWeaponProfileId::Type99 &&
               std::string_view(profile->diagnostic_name) ==
                   "Type 99 Arisaka" &&
               std::string_view(profile->internal_weapon_name) ==
                   "type99_rifle" &&
               std::string_view(profile->viewmodel_model_name) ==
                   "viewmodel_jap_type99_rifle" &&
               std::string_view(profile->bolt_tag_name) == "j_bolt" &&
               profile->moving_bolt_tag_count == 1 &&
               std::string_view(profile->moving_bolt_tag_names[0]) ==
                   "j_bolt" &&
               profile->moving_bolt_tag_names[1] == nullptr &&
               profile->reload_kind ==
                   ReloadProfileKind::InternalStripperClip,
           "Type 99 identity, moving bolt, and reload policy are exact");
    expect(exact_float(profile->bolt_travel_units, 2.364536F) &&
               exact_float(profile->bolt_open_threshold, 0.95F) &&
               exact_float(profile->bolt_closed_threshold, 0.05F) &&
               exact_float(profile->trigger_engage, 0.65F) &&
               exact_float(profile->trigger_release, 0.35F) &&
               exact_float(profile->bolt_grab_radius_units, 10.0F),
           "Type 99 physical bolt calibration matches the native retail stroke");
    expect(profile->idle_anim == 0 && profile->rechamber_hip_anim == 4 &&
               profile->rechamber_ads_anim == 7,
           "Type 99 automatic-rechamber animation policy is exact");
    expect(std::string_view(profile->feed_device_material_name) ==
                    "mc/mtl_stripper_clip" &&
               std::string_view(profile->feed_device_bone_tag_name) ==
                   "j_stripper" &&
               std::string_view(profile->round_material_name) ==
                   "mc/mtl_ammo_belt" &&
               std::string_view(profile->detached_round_bone_tag_name) ==
                   "j_clip" &&
               profile->allow_multi_rigid_round_surfaces &&
               profile->expected_multi_rigid_round_bone_tag_count == 2 &&
               std::string_view(
                   profile->expected_multi_rigid_round_bone_tag_names[0]) ==
                   "j_round" &&
               std::string_view(
                   profile->expected_multi_rigid_round_bone_tag_names[1]) ==
                   "j_round1" &&
               profile->expected_multi_rigid_round_bone_tag_names[2] ==
                   nullptr &&
               profile->authored_round_surface_count == 2,
           "Type 99 clip recipe pins the exact stripper, carried rounds, and loose rounds");
}

void test_registry_contains_exact_scoped_zombie_kar98_profile() {
    const BoltActionWeaponProfile* const profile =
        find_bolt_action_weapon_profile(
            BoltActionWeaponProfileId::Kar98ScopedZombie);
    expect(profile == &kKar98ScopedZombieBoltActionWeaponProfile,
           "the immutable registry exposes the exact scoped Zombies Kar98 profile");
    expect(std::string_view(profile->internal_weapon_name) ==
                    "kar98k_scoped_zombie" &&
               std::string_view(profile->viewmodel_model_name) ==
                    "viewmodel_ger_kar98_scoped_rifle" &&
               std::string_view(profile->bolt_tag_name) == "j_bolt" &&
               exact_float(profile->bolt_travel_units, 3.75F) &&
               std::string_view(profile->feed_device_material_name) ==
                    "mc/mtl_stripper_clip" &&
               std::string_view(profile->round_material_name) ==
                    "mc/mtl_k98round",
           "the scoped Zombies Kar98 retains the exact audited Kar98 bolt and feed recipe");
}

void test_registry_contains_exact_type99_variants() {
    const BoltActionWeaponProfile* const bayonet =
        find_bolt_action_weapon_profile(
            BoltActionWeaponProfileId::Type99Bayonet);
    const BoltActionWeaponProfile* const scoped =
        find_bolt_action_weapon_profile(
            BoltActionWeaponProfileId::Type99Scoped);
    expect(bayonet == &kType99BayonetBoltActionWeaponProfile &&
               scoped == &kType99ScopedBoltActionWeaponProfile,
           "the immutable registry exposes both audited Type 99 variants");
    expect(std::string_view(bayonet->internal_weapon_name) ==
                   "type99_rifle_bayonet" &&
               std::string_view(bayonet->viewmodel_model_name) ==
                   "viewmodel_jap_type99_rifle_bayonet" &&
               std::string_view(scoped->internal_weapon_name) ==
                   "type99_rifle_scoped" &&
               std::string_view(scoped->viewmodel_model_name) ==
                   "viewmodel_jap_type99_rifle_scoped",
           "Type 99 variant identities exactly match the live retail WeaponDefs and viewmodels");
    for (const BoltActionWeaponProfile* const profile : {bayonet, scoped}) {
        expect(profile->moving_bolt_tag_count == 1 &&
                   std::string_view(profile->bolt_tag_name) == "j_bolt" &&
                   exact_float(profile->bolt_travel_units, 2.364536F) &&
                   std::string_view(profile->feed_device_bone_tag_name) ==
                       "j_stripper" &&
                   std::string_view(profile->detached_round_bone_tag_name) ==
                       "j_clip" &&
                   profile->expected_multi_rigid_round_bone_tag_count == 2 &&
                   std::string_view(
                       profile->expected_multi_rigid_round_bone_tag_names[0]) ==
                       "j_round" &&
                   std::string_view(
                       profile->expected_multi_rigid_round_bone_tag_names[1]) ==
                       "j_round1" &&
                   profile->authored_round_surface_count == 2,
               "each Type 99 variant retains the exact audited bolt and feed topology");
    }
}

void test_lookup_requires_exact_full_names() {
    expect(find_bolt_action_weapon_profile_by_internal_name("kar98k") ==
               &kKar98BoltActionWeaponProfile,
           "the exact Kar98 internal weapon name resolves");
    expect(find_bolt_action_weapon_profile_by_viewmodel_model_name(
               "viewmodel_ger_kar98_rifle") ==
               &kKar98BoltActionWeaponProfile,
            "the exact Kar98 viewmodel name resolves");
    expect(find_bolt_action_weapon_profile_by_internal_name(
               "kar98k_scoped_zombie") ==
                    &kKar98ScopedZombieBoltActionWeaponProfile &&
               find_bolt_action_weapon_profile_by_viewmodel_model_name(
                   "viewmodel_ger_kar98_scoped_rifle") ==
                    &kKar98ScopedZombieBoltActionWeaponProfile,
           "the exact scoped Zombies Kar98 internal and viewmodel names resolve");
    expect(find_bolt_action_weapon_profile_by_internal_name("springfield") ==
                   &kSpringfieldBoltActionWeaponProfile &&
               find_bolt_action_weapon_profile_by_viewmodel_model_name(
                   "viewmodel_usa_springfield_rifle") ==
                   &kSpringfieldBoltActionWeaponProfile,
           "the exact Springfield internal and viewmodel names resolve");
    expect(find_bolt_action_weapon_profile_by_internal_name("mosin_rifle") ==
                   &kMosinBoltActionWeaponProfile &&
               find_bolt_action_weapon_profile_by_viewmodel_model_name(
                   "viewmodel_rus_mosinnagant_rifle") ==
                   &kMosinBoltActionWeaponProfile,
            "the exact Mosin internal and viewmodel names resolve");
    expect(find_bolt_action_weapon_profile_by_internal_name(
               "mosin_rifle_scoped") ==
                   &kMosinScopedBoltActionWeaponProfile &&
               find_bolt_action_weapon_profile_by_viewmodel_model_name(
                   "viewmodel_rus_mosinnagant_scoped_rifle") ==
                   &kMosinScopedBoltActionWeaponProfile,
           "the exact player-held campaign scoped Mosin identity resolves");
    expect(find_bolt_action_weapon_profile_by_internal_name("type99_rifle") ==
                    &kType99BoltActionWeaponProfile &&
               find_bolt_action_weapon_profile_by_viewmodel_model_name(
                   "viewmodel_jap_type99_rifle") ==
                   &kType99BoltActionWeaponProfile,
           "the exact Type 99 internal and viewmodel names resolve");
    expect(find_bolt_action_weapon_profile_by_internal_name(
               "type99_rifle_bayonet") ==
                   &kType99BayonetBoltActionWeaponProfile &&
               find_bolt_action_weapon_profile_by_viewmodel_model_name(
                   "viewmodel_jap_type99_rifle_bayonet") ==
                   &kType99BayonetBoltActionWeaponProfile &&
               find_bolt_action_weapon_profile_by_internal_name(
                   "type99_rifle_scoped") ==
                   &kType99ScopedBoltActionWeaponProfile &&
               find_bolt_action_weapon_profile_by_viewmodel_model_name(
                   "viewmodel_jap_type99_rifle_scoped") ==
                   &kType99ScopedBoltActionWeaponProfile,
           "the exact audited Type 99 bayonet and scoped names resolve");

    expect(find_bolt_action_weapon_profile(BoltActionWeaponProfileId::None) ==
                   nullptr &&
               find_bolt_action_weapon_profile_by_internal_name({}) ==
                   nullptr &&
               find_bolt_action_weapon_profile_by_internal_name("kar98") ==
                   nullptr &&
               find_bolt_action_weapon_profile_by_internal_name(
                   "kar98k_scoped") == nullptr &&
               find_bolt_action_weapon_profile_by_internal_name("Kar98k") ==
                   nullptr,
           "empty, substring, suffixed, and case-changed weapon names fail closed");
    expect(find_bolt_action_weapon_profile_by_viewmodel_model_name(
               "viewmodel_ger_kar98") == nullptr &&
               find_bolt_action_weapon_profile_by_viewmodel_model_name(
                   "viewmodel_ger_kar98_rifle_extra") == nullptr &&
               find_bolt_action_weapon_profile_by_viewmodel_model_name(
                   "viewmodel_usa_springfield_rifle_scoped") == nullptr &&
               find_bolt_action_weapon_profile_by_internal_name(
                   "springfield_bayonet") == nullptr &&
               find_bolt_action_weapon_profile_by_internal_name(
                   "Springfield") == nullptr &&
                find_bolt_action_weapon_profile_by_internal_name(
                    "mosin_rifle_bayonet") == nullptr &&
                find_bolt_action_weapon_profile_by_viewmodel_model_name(
                   "viewmodel_rus_mosinnagant_scoped_rifle_extra") ==
                    nullptr &&
                find_bolt_action_weapon_profile_by_internal_name(
                    "mosin_rifle_scoped_noflash") == nullptr &&
                find_bolt_action_weapon_profile_by_internal_name(
                    "mosin_rifle_scoped_bayonet") == nullptr &&
                find_bolt_action_weapon_profile_by_internal_name(
                    "mosin_rifle_scoped_zombie") == nullptr &&
                find_bolt_action_weapon_profile_by_internal_name(
                    "mosin_rifle_scoped_bayonet_zombie") == nullptr &&
                find_bolt_action_weapon_profile_by_viewmodel_model_name(
                    "viewmodel_rus_mosinnagant_cloth_rifle_bayonet") ==
                    nullptr,
           "unaudited partial, scripted, bayonet, Zombies, and case-changed names fail closed");
}

void test_profile_validation_rejects_malformed_values() {
    BoltActionWeaponProfile profile = kKar98BoltActionWeaponProfile;
    profile.internal_weapon_name = "synthetic_bolt";
    profile.viewmodel_model_name = "viewmodel_synthetic_bolt";
    expect(validate_bolt_action_weapon_profile(profile),
           "a structurally complete synthetic profile validates");

    auto malformed = profile;
    malformed.internal_weapon_name = "";
    expect(!validate_bolt_action_weapon_profile(malformed),
           "an empty exact identity is rejected");

    malformed = profile;
    malformed.reload_kind = ReloadProfileKind::NativeOnly;
    expect(!validate_bolt_action_weapon_profile(malformed),
           "a profile without the implemented stripper-clip policy is rejected");

    malformed = profile;
    malformed.bolt_travel_units = 0.0F;
    expect(!validate_bolt_action_weapon_profile(malformed),
           "non-positive bolt travel is rejected");

    malformed = profile;
    malformed.bolt_closed_threshold = malformed.bolt_open_threshold;
    expect(!validate_bolt_action_weapon_profile(malformed),
           "overlapping bolt endpoints are rejected");

    malformed = profile;
    malformed.trigger_release = malformed.trigger_engage;
    expect(!validate_bolt_action_weapon_profile(malformed),
           "invalid trigger hysteresis is rejected");

    malformed = profile;
    malformed.moving_bolt_tag_count = 0;
    expect(!validate_bolt_action_weapon_profile(malformed),
           "a missing moving-bolt recipe is rejected");

    malformed = profile;
    malformed.moving_bolt_tag_count =
        static_cast<std::uint8_t>(kMaximumMovingBoltTags + 1U);
    expect(!validate_bolt_action_weapon_profile(malformed),
           "an unbounded moving-bolt recipe is rejected");

    malformed = profile;
    malformed.moving_bolt_tag_names[0] = "j_bolt1";
    expect(!validate_bolt_action_weapon_profile(malformed),
           "the primary grab anchor must be the first moving bolt tag");

    malformed = profile;
    malformed.moving_bolt_tag_count = 2;
    malformed.moving_bolt_tag_names[1] = "j_bolt";
    expect(!validate_bolt_action_weapon_profile(malformed),
           "duplicate moving bolt tags are rejected");

    malformed = profile;
    malformed.moving_bolt_tag_names[1] = "j_bolt1";
    expect(!validate_bolt_action_weapon_profile(malformed),
           "a populated moving-bolt tag outside the declared count is rejected");

    malformed = profile;
    malformed.authored_round_surface_count = 0;
    expect(!validate_bolt_action_weapon_profile(malformed),
           "a missing authored-round recipe is rejected");

    malformed = profile;
    malformed.authored_round_surface_count =
        static_cast<std::uint8_t>(
            kMaximumAuthoredFeedRoundSurfaces + 1U);
    expect(!validate_bolt_action_weapon_profile(malformed),
           "an unbounded authored-round recipe is rejected");

    malformed = profile;
    malformed.detached_round_bone_tag_name = "";
    expect(!validate_bolt_action_weapon_profile(malformed),
           "an empty detached-round anchor is rejected");

    malformed = profile;
    malformed.allow_multi_rigid_round_surfaces = true;
    malformed.detached_round_bone_tag_name = nullptr;
    malformed.expected_multi_rigid_round_bone_tag_names = {
        "tag_round1", "tag_round2"};
    malformed.expected_multi_rigid_round_bone_tag_count = 2;
    expect(!validate_bolt_action_weapon_profile(malformed),
           "multi-rigid round props require an exact detached-round anchor");

    malformed = profile;
    malformed.allow_multi_rigid_round_surfaces = true;
    malformed.detached_round_bone_tag_name = "j_clip";
    expect(!validate_bolt_action_weapon_profile(malformed),
           "multi-rigid admission without an expected bone set is rejected");

    malformed = kMosinBoltActionWeaponProfile;
    malformed.expected_multi_rigid_round_bone_tag_count =
        static_cast<std::uint8_t>(
            kMaximumExpectedMultiRigidRoundBoneTags + 1U);
    expect(!validate_bolt_action_weapon_profile(malformed),
           "an unbounded multi-rigid round bone set is rejected");

    malformed = kMosinBoltActionWeaponProfile;
    malformed.expected_multi_rigid_round_bone_tag_names[1] = "tag_round1";
    expect(!validate_bolt_action_weapon_profile(malformed),
           "duplicate multi-rigid round bone tags are rejected");

    malformed = kMosinBoltActionWeaponProfile;
    malformed.expected_multi_rigid_round_bone_tag_count = 1;
    expect(!validate_bolt_action_weapon_profile(malformed),
           "a populated multi-rigid round tag outside the declared count is rejected");

    malformed = kMosinBoltActionWeaponProfile;
    malformed.expected_multi_rigid_round_bone_tag_names[1] = "j_clip";
    expect(!validate_bolt_action_weapon_profile(malformed),
           "the detached round cluster cannot also be a native multi-rigid round bone");

    malformed = kMosinBoltActionWeaponProfile;
    malformed.authored_round_surface_count = 1;
    expect(!validate_bolt_action_weapon_profile(malformed),
           "a multi-rigid native round prop requires a separate detached round surface");

    malformed = profile;
    malformed.feed_device_bone_tag_name = "";
    expect(!validate_bolt_action_weapon_profile(malformed),
           "an empty optional feed-device anchor is rejected");

    malformed = kMosinBoltActionWeaponProfile;
    malformed.feed_device_bone_tag_name = "j_clip";
    expect(!validate_bolt_action_weapon_profile(malformed),
           "the feed device and detached round cluster require distinct anchors");

    malformed = kMosinBoltActionWeaponProfile;
    malformed.feed_device_bone_tag_name = "j_bolt1";
    expect(!validate_bolt_action_weapon_profile(malformed),
           "a feed-device anchor cannot also be a moving bolt tag");

    malformed = kMosinBoltActionWeaponProfile;
    malformed.feed_device_bone_tag_name = "tag_round1";
    expect(!validate_bolt_action_weapon_profile(malformed),
           "a feed-device anchor cannot also be a native loose-round tag");

    malformed = profile;
    malformed.round_material_name = profile.feed_device_material_name;
    expect(!validate_bolt_action_weapon_profile(malformed),
           "feed-device and round materials must be distinct exact identities");

    malformed = profile;
    malformed.feed_device_hand_offset.x =
        (std::numeric_limits<float>::quiet_NaN)();
    expect(!validate_bolt_action_weapon_profile(malformed),
           "a non-finite held-device calibration is rejected");

    malformed = profile;
    malformed.receiver_segment_maximum = 1.01F;
    expect(!validate_bolt_action_weapon_profile(malformed),
           "a receiver projection outside the rifle segment is rejected");
}

void test_map_local_binding_is_coherent_and_not_index_two_specific() {
    const std::uint64_t packed = pack_bolt_action_weapon_binding(
        {BoltActionWeaponProfileId::Kar98, 19});
    const auto binding = unpack_bolt_action_weapon_binding(packed);
    expect(binding.profile_id == BoltActionWeaponProfileId::Kar98 &&
               binding.weapon_index == 19,
           "a validated profile can bind coherently to a map-local index above two");
    expect(bolt_action_weapon_binding_matches(
               packed, BoltActionWeaponProfileId::Kar98, 19) &&
               !bolt_action_weapon_binding_matches(
                   packed, BoltActionWeaponProfileId::Kar98, 2) &&
               !bolt_action_weapon_binding_matches(
                   packed, BoltActionWeaponProfileId::None, 19),
           "stale indices and unsupported profiles fail the atomic binding gate");
    expect(pack_bolt_action_weapon_binding(
               {BoltActionWeaponProfileId::Kar98, 0}) == 0 &&
               pack_bolt_action_weapon_binding(
                   {BoltActionWeaponProfileId::None, 19}) == 0,
           "invalid bindings publish the empty fail-closed value");

    const std::uint64_t springfield = pack_bolt_action_weapon_binding(
        {BoltActionWeaponProfileId::Springfield, 14});
    expect(bolt_action_weapon_binding_matches(
               springfield, BoltActionWeaponProfileId::Springfield, 14) &&
               !bolt_action_weapon_binding_matches(
                   springfield, BoltActionWeaponProfileId::Kar98, 14) &&
               !bolt_action_weapon_binding_matches(
                   springfield, BoltActionWeaponProfileId::Springfield, 2),
           "Springfield profile identity and map-local index stay coherent");

    const std::uint64_t mosin = pack_bolt_action_weapon_binding(
        {BoltActionWeaponProfileId::Mosin, 20});
    expect(bolt_action_weapon_binding_matches(
               mosin, BoltActionWeaponProfileId::Mosin, 20) &&
               !bolt_action_weapon_binding_matches(
                   mosin, BoltActionWeaponProfileId::Springfield, 20),
            "Mosin profile identity remains coherent at its campaign-local index");

    const std::uint64_t scoped_mosin = pack_bolt_action_weapon_binding(
        {BoltActionWeaponProfileId::MosinScoped, 4});
    expect(bolt_action_weapon_binding_matches(
               scoped_mosin, BoltActionWeaponProfileId::MosinScoped, 4) &&
               !bolt_action_weapon_binding_matches(
                   scoped_mosin, BoltActionWeaponProfileId::Mosin, 4) &&
               !bolt_action_weapon_binding_matches(
                   scoped_mosin, BoltActionWeaponProfileId::MosinScoped, 20),
           "the scoped Mosin profile remains coherent at its Vendetta map-local index");

    const std::uint64_t type99 = pack_bolt_action_weapon_binding(
        {BoltActionWeaponProfileId::Type99, 5});
    expect(bolt_action_weapon_binding_matches(
               type99, BoltActionWeaponProfileId::Type99, 5) &&
               !bolt_action_weapon_binding_matches(
                   type99, BoltActionWeaponProfileId::Mosin, 5),
           "Type 99 profile identity remains coherent at its Pacific campaign-local index");
}

}  // namespace

int main() {
    test_registry_contains_exact_accepted_kar98_profile();
    test_registry_contains_exact_springfield_profile();
    test_registry_contains_exact_mosin_profile();
    test_registry_contains_exact_scoped_mosin_profile();
    test_registry_contains_exact_type99_profile();
    test_registry_contains_exact_scoped_zombie_kar98_profile();
    test_registry_contains_exact_type99_variants();
    test_lookup_requires_exact_full_names();
    test_profile_validation_rejects_malformed_values();
    test_map_local_binding_is_coherent_and_not_index_two_specific();
    std::cout << "bolt action weapon profile tests passed\n";
    return EXIT_SUCCESS;
}
