// SPDX-License-Identifier: GPL-3.0-only
#include "detachable_magazine_weapon_profile.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

using wawvr::gameplay::ReloadProfileKind;
using wawvr::mod::DetachableMagazineWeaponProfile;
using wawvr::mod::DetachableMagazineWeaponProfileId;
using wawvr::mod::MagazineChargingCompletion;
using wawvr::mod::MagazineChargingControlPolicy;
using wawvr::mod::MagazineChargingHand;
using wawvr::mod::detachable_magazine_weapon_binding_matches;
using wawvr::mod::detachable_magazine_mesh_piece;
using wawvr::mod::detachable_magazine_mesh_piece_count;
using wawvr::mod::detachable_magazine_charging_mesh_piece;
using wawvr::mod::detachable_magazine_charging_mesh_piece_count;
using wawvr::mod::detachable_magazine_weapon_profiles;
using wawvr::mod::find_detachable_magazine_weapon_profile;
using wawvr::mod::find_detachable_magazine_weapon_profile_by_identity;
using wawvr::mod::find_detachable_magazine_weapon_profile_by_internal_name;
using wawvr::mod::find_detachable_magazine_weapon_profile_by_viewmodel_model_name;
using wawvr::mod::has_detachable_magazine_weapon_profile_for_internal_name;
using wawvr::mod::kBarDetachableMagazineWeaponProfile;
using wawvr::mod::kBarBipodDetachableMagazineWeaponProfile;
using wawvr::mod::kColtDetachableMagazineWeaponProfile;
using wawvr::mod::kColtWetDetachableMagazineWeaponProfile;
using wawvr::mod::kFg42BipodDetachableMagazineWeaponProfile;
using wawvr::mod::kGewehr43DetachableMagazineWeaponProfile;
using wawvr::mod::kM1CarbineDetachableMagazineWeaponProfile;
using wawvr::mod::kM1GarandDetachableMagazineWeaponProfile;
using wawvr::mod::kM1GarandBayonetDetachableMagazineWeaponProfile;
using wawvr::mod::kM1GarandGrenadeLauncherDetachableMagazineWeaponProfile;
using wawvr::mod::kPtrs41DetachableMagazineWeaponProfile;
using wawvr::mod::kMp40DetachableMagazineWeaponProfile;
using wawvr::mod::kNambuDetachableMagazineWeaponProfile;
using wawvr::mod::kPpshDetachableMagazineWeaponProfile;
using wawvr::mod::kStg44DetachableMagazineWeaponProfile;
using wawvr::mod::kSvt40DetachableMagazineWeaponProfile;
using wawvr::mod::kThompsonDetachableMagazineWeaponProfile;
using wawvr::mod::kThompsonWetDetachableMagazineWeaponProfile;
using wawvr::mod::kTokarevDetachableMagazineWeaponProfile;
using wawvr::mod::kType100DetachableMagazineWeaponProfile;
using wawvr::mod::kType100NoSoundDetachableMagazineWeaponProfile;
using wawvr::mod::kType99LmgDetachableMagazineWeaponProfile;
using wawvr::mod::kType99LmgBipodDetachableMagazineWeaponProfile;
using wawvr::mod::kWaltherDetachableMagazineWeaponProfile;
using wawvr::mod::kZombieColtDetachableMagazineWeaponProfile;
using wawvr::mod::kZombieBarDetachableMagazineWeaponProfile;
using wawvr::mod::kZombieBarUpgradedDetachableMagazineWeaponProfile;
using wawvr::mod::kZombieColtDedicatedDetachableMagazineWeaponProfile;
using wawvr::mod::kZombieColtUpgradedDetachableMagazineWeaponProfile;
using wawvr::mod::kZombieFg42DetachableMagazineWeaponProfile;
using wawvr::mod::kZombieFg42UpgradedDetachableMagazineWeaponProfile;
using wawvr::mod::kZombieGewehr43DetachableMagazineWeaponProfile;
using wawvr::mod::kZombieGewehr43UpgradedDetachableMagazineWeaponProfile;
using wawvr::mod::kZombieM1CarbineDetachableMagazineWeaponProfile;
using wawvr::mod::kZombieM1CarbineUpgradedDetachableMagazineWeaponProfile;
using wawvr::mod::kZombieMp40DetachableMagazineWeaponProfile;
using wawvr::mod::kZombieMp40UpgradedDetachableMagazineWeaponProfile;
using wawvr::mod::kZombiePpshDetachableMagazineWeaponProfile;
using wawvr::mod::kZombiePpshUpgradedDetachableMagazineWeaponProfile;
using wawvr::mod::kZombieStg44DetachableMagazineWeaponProfile;
using wawvr::mod::kZombieStg44UpgradedDetachableMagazineWeaponProfile;
using wawvr::mod::kZombieThompsonDetachableMagazineWeaponProfile;
using wawvr::mod::kZombieThompsonUpgradedDetachableMagazineWeaponProfile;
using wawvr::mod::kZombieType100DetachableMagazineWeaponProfile;
using wawvr::mod::kZombieType100UpgradedDetachableMagazineWeaponProfile;
using wawvr::mod::pack_detachable_magazine_weapon_binding;
using wawvr::mod::unpack_detachable_magazine_weapon_binding;
using wawvr::mod::validate_detachable_magazine_weapon_profile;
using wawvr::mod::validate_detachable_magazine_weapon_profile_registry;
using wawvr::mod::validate_exact_m1carbine_profile;
using wawvr::mod::validate_exact_m1garand_profile;
using wawvr::mod::validate_exact_m1garand_bayonet_profile;
using wawvr::mod::validate_exact_zombie_colt_dedicated_profile;
using wawvr::mod::validate_exact_zombie_colt_profile;
using wawvr::mod::validate_exact_zombie_gewehr43_profile;
using wawvr::mod::validate_exact_zombie_gewehr43_upgraded_profile;
using wawvr::mod::validate_exact_zombie_stg44_profile;
using wawvr::mod::validate_exact_zombie_stg44_upgraded_profile;
using wawvr::mod::validate_exact_zombie_thompson_profile;
using wawvr::mod::validate_exact_zombie_thompson_upgraded_profile;
using wawvr::mod::validate_exact_zombie_mp40_profile;
using wawvr::mod::validate_exact_zombie_mp40_upgraded_profile;
using wawvr::mod::validate_exact_zombie_type100_profile;
using wawvr::mod::validate_exact_zombie_bar_profile;
using wawvr::mod::validate_exact_zombie_fg42_profile;
using wawvr::mod::validate_exact_zombie_m1carbine_profile;
using wawvr::mod::validate_exact_zombie_ppsh_profile;
using wawvr::mod::validate_exact_m1garand_grenade_launcher_profile;

void expect(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

struct PhaseOneProfileExpectation final {
    DetachableMagazineWeaponProfileId id;
    const DetachableMagazineWeaponProfile* canonical;
    const char* diagnostic_name;
    const char* internal_weapon_name;
    const char* viewmodel_model_name;
    const char* magazine_material_name;
    const char* charging_material_name;
    std::int32_t clip_size;
    MagazineChargingHand charging_hand;
    MagazineChargingCompletion charging_completion;
    bool suppress_native_pose_always;
};

constexpr std::array<PhaseOneProfileExpectation, 45>
    kPhaseOneProfileExpectations{{
        {
            DetachableMagazineWeaponProfileId::ZombieColt,
            &kZombieColtDetachableMagazineWeaponProfile,
            "Nacht zombie Colt M1911",
            "zombie_colt",
            "viewmodel_usa_colt45_pistol",
            "mc/mtl_weapon_colt45",
            "mc/mtl_weapon_colt45",
            8,
            MagazineChargingHand::Left,
            MagazineChargingCompletion::SpringClosed,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::M1Carbine,
            &kM1CarbineDetachableMagazineWeaponProfile,
            "Nacht M1A1 Carbine",
            "m1carbine",
            "viewmodel_usa_m1carbine_rifle",
            "mc/mtl_weapon_carbine",
            "mc/mtl_weapon_carbine",
            15,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::SpringClosed,
            true,
        },
        {
            DetachableMagazineWeaponProfileId::M1Garand,
            &kM1GarandDetachableMagazineWeaponProfile,
            "M1 Garand",
            "m1garand",
            "viewmodel_usa_m1garand_rifle",
            "mc/mtl_brass_shells",
            "mc/mtl_weapon_m1garand",
            8,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::SpringClosed,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::M1GarandBayonet,
            &kM1GarandBayonetDetachableMagazineWeaponProfile,
            "M1 Garand bayonet",
            "m1garand_bayonet",
            "viewmodel_usa_m1garand_rifle_bayonet",
            "mc/mtl_brass_shells",
            "mc/mtl_weapon_m1garand",
            8,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::SpringClosed,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::M1GarandGrenadeLauncher,
            &kM1GarandGrenadeLauncherDetachableMagazineWeaponProfile,
            "M1 Garand rifle-grenade launcher",
            "m1garand_gl",
            "viewmodel_usa_m1garand_rifle_grenade_mount",
            "mc/mtl_brass_shells",
            "mc/mtl_weapon_m1garand",
            8,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::SpringClosed,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::Ptrs41,
            &kPtrs41DetachableMagazineWeaponProfile,
            "PTRS-41", "ptrs41", "viewmodel_mp_ptrs41",
            "mc/mtl_weapon_ptrs", "mc/mtl_weapon_ptrs", 5,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::SpringClosed, false,
        },
        {
            DetachableMagazineWeaponProfileId::Gewehr43,
            &kGewehr43DetachableMagazineWeaponProfile,
            "Nacht Gewehr 43",
            "gewehr43",
            "viewmodel_ger_g43_rifle",
            "mc/mtl_weapon_g43",
            "mc/mtl_weapon_g43",
            10,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::SpringClosed,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::Stg44,
            &kStg44DetachableMagazineWeaponProfile,
            "Nacht StG 44",
            "stg44",
            "viewmodel_ger_mp44_lmg",
            "mc/mtl_weapon_mp44",
            "mc/mtl_weapon_mp44",
            30,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::SpringClosed,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::Mp40,
            &kMp40DetachableMagazineWeaponProfile,
            "Nacht MP40",
            "mp40",
            "viewmodel_ger_mp40_smg",
            "mc/mtl_weapon_mp40",
            "mc/mtl_weapon_mp40",
            32,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::LatchOpen,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::Thompson,
            &kThompsonDetachableMagazineWeaponProfile,
            "Nacht Thompson",
            "thompson",
            "viewmodel_usa_thompson_smg",
            "mc/mtl_usa_smg_thompson",
            "mc/mtl_usa_smg_thompson",
            20,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::LatchOpen,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::Bar,
            &kBarDetachableMagazineWeaponProfile,
            "Nacht BAR",
            "bar",
            "viewmodel_usa_bar_lmg",
            "mc/mtl_weapon_bar_wood",
            "mc/mtl_weapon_bar",
            20,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::LatchOpen,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::Fg42Bipod,
            &kFg42BipodDetachableMagazineWeaponProfile,
            "Nacht FG42 bipod",
            "fg42_bipod",
            "viewmodel_ger_fg42_bipod_lmg",
            "mc/mtl_weapon_fg42",
            "mc/mtl_weapon_fg42",
            32,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::LatchOpen,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::Walther,
            &kWaltherDetachableMagazineWeaponProfile,
            "Walther P38",
            "walther",
            "viewmodel_ger_walther_pistol",
            "mc/mtl_weapon_walther",
            "mc/mtl_weapon_walther",
            8,
            MagazineChargingHand::Left,
            MagazineChargingCompletion::SpringClosed,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::Colt,
            &kColtDetachableMagazineWeaponProfile,
            "Colt M1911",
            "colt",
            "viewmodel_usa_colt45_pistol",
            "mc/mtl_weapon_colt45",
            "mc/mtl_weapon_colt45",
            8,
            MagazineChargingHand::Left,
            MagazineChargingCompletion::SpringClosed,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::Tokarev,
            &kTokarevDetachableMagazineWeaponProfile,
            "Tokarev TT-33",
            "tokarev",
            "viewmodel_rus_tt30_pistol",
            "mc/mtl_tokarevtt30_pistol",
            "mc/mtl_tokarevtt30_pistol",
            8,
            MagazineChargingHand::Left,
            MagazineChargingCompletion::SpringClosed,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::Nambu,
            &kNambuDetachableMagazineWeaponProfile,
            "Nambu pistol",
            "nambu",
            "viewmodel_jap_nambu_pistol",
            "mc/mtl_weapon_nambu",
            "mc/mtl_weapon_nambu",
            8,
            MagazineChargingHand::Left,
            MagazineChargingCompletion::SpringClosed,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::Svt40,
            &kSvt40DetachableMagazineWeaponProfile,
            "SVT-40",
            "svt40",
            "viewmodel_rus_svt40_rifle",
            "mc/mtl_weapon_svt40_clip",
            "mc/mtl_weapon_svt40",
            10,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::SpringClosed,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::Ppsh,
            &kPpshDetachableMagazineWeaponProfile,
            "PPSh-41",
            "ppsh",
            "viewmodel_rus_ppsh_smg",
            "mc/mtl_rus_smg_ppsh41",
            "mc/mtl_rus_smg_ppsh41",
            71,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::LatchOpen,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::Type100,
            &kType100DetachableMagazineWeaponProfile,
            "Type 100 SMG",
            "type100_smg",
            "viewmodel_jap_type100_smg",
            "mc/mtl_weapon_type100_metal",
            "mc/mtl_weapon_type100_metal",
            30,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::LatchOpen,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::Type99Lmg,
            &kType99LmgDetachableMagazineWeaponProfile,
            "Type 99 LMG",
            "type99_lmg",
            "viewmodel_jap_type99_lmg",
            "mc/mtl_weapon_type99_lmg",
            "mc/mtl_weapon_type99_lmg",
            32,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::LatchOpen,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::Type99LmgBipod,
            &kType99LmgBipodDetachableMagazineWeaponProfile,
            "Type 99 LMG bipod",
            "type99_lmg_bipod",
            "viewmodel_jap_type99_bipod_lmg",
            "mc/mtl_weapon_type99_lmg",
            "mc/mtl_weapon_type99_lmg",
            32,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::LatchOpen,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::Type100NoSound,
            &kType100NoSoundDetachableMagazineWeaponProfile,
            "Type 100 SMG no-sound variant",
            "type100_smg_nosound",
            "viewmodel_jap_type100_smg",
            "mc/mtl_weapon_type100_metal",
            "mc/mtl_weapon_type100_metal",
            30,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::LatchOpen,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::ThompsonWet,
            &kThompsonWetDetachableMagazineWeaponProfile,
            "Wet Thompson",
            "thompson_wet",
            "viewmodel_usa_thompson_smg_wet",
            "mc/mtl_usa_smg_thompson_wet",
            "mc/mtl_usa_smg_thompson_wet",
            20,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::LatchOpen,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::ColtWet,
            &kColtWetDetachableMagazineWeaponProfile,
            "Wet Colt M1911",
            "colt_wet",
            "viewmodel_usa_colt45_pistol_wet",
            "mc/mtl_weapon_colt45_wet",
            "mc/mtl_weapon_colt45_wet",
            8,
            MagazineChargingHand::Left,
            MagazineChargingCompletion::SpringClosed,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::BarBipod,
            &kBarBipodDetachableMagazineWeaponProfile,
            "BAR bipod",
            "bar_bipod",
            "viewmodel_usa_bar_bipod_lmg",
            "mc/mtl_weapon_bar_wood",
            "mc/mtl_weapon_bar",
            20,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::LatchOpen,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::ZombieColtDedicated,
            &kZombieColtDedicatedDetachableMagazineWeaponProfile,
            "Zombie Colt M1911",
            "zombie_colt",
            "viewmodel_zombie_colt45_pistol",
            "mc/mtl_weapon_colt45",
            "mc/mtl_weapon_colt45",
            8,
            MagazineChargingHand::Left,
            MagazineChargingCompletion::SpringClosed,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::ZombieColtUpgraded,
            &kZombieColtUpgradedDetachableMagazineWeaponProfile,
            "Upgraded zombie Colt M1911",
            "zombie_colt_upgraded",
            "viewmodel_zombie_colt45_pistol_up",
            "mc/mtl_weapon_colt45_zombie_up",
            "mc/mtl_weapon_colt45_zombie_up",
            6,
            MagazineChargingHand::Left,
            MagazineChargingCompletion::SpringClosed,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::ZombieM1Carbine,
            &kZombieM1CarbineDetachableMagazineWeaponProfile,
            "Zombie M1A1 Carbine",
            "zombie_m1carbine",
            "viewmodel_zombie_m1carbine_rifle",
            "mc/mtl_weapon_mp_carbine",
            "mc/mtl_weapon_mp_carbine",
            15,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::SpringClosed,
            true,
        },
        {
            DetachableMagazineWeaponProfileId::ZombieM1CarbineUpgraded,
            &kZombieM1CarbineUpgradedDetachableMagazineWeaponProfile,
            "Upgraded zombie M1A1 Carbine",
            "zombie_m1carbine_upgraded",
            "viewmodel_zombie_m1carbine_rifle_up",
            "mc/mtl_weapon_carbine_gold",
            "mc/mtl_weapon_carbine_gold",
            15,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::SpringClosed,
            true,
        },
        {
            DetachableMagazineWeaponProfileId::ZombieGewehr43,
            &kZombieGewehr43DetachableMagazineWeaponProfile,
            "Zombie Gewehr 43",
            "zombie_gewehr43",
            "viewmodel_zombie_g43_rifle",
            "mc/mtl_weapon_mp_g43",
            "mc/mtl_weapon_mp_g43",
            10,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::SpringClosed,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::ZombieGewehr43Upgraded,
            &kZombieGewehr43UpgradedDetachableMagazineWeaponProfile,
            "Upgraded zombie Gewehr 43",
            "zombie_gewehr43_upgraded",
            "viewmodel_zombie_g43_rifle_up",
            "mc/mtl_weapon_g43_gold",
            "mc/mtl_weapon_g43_gold",
            12,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::SpringClosed,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::ZombieStg44,
            &kZombieStg44DetachableMagazineWeaponProfile,
            "Zombie StG 44",
            "zombie_stg44",
            "viewmodel_zombie_mp44_lmg",
            "mc/mtl_weapon_mp_mp44",
            "mc/mtl_weapon_mp_mp44",
            30,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::SpringClosed,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::ZombieStg44Upgraded,
            &kZombieStg44UpgradedDetachableMagazineWeaponProfile,
            "Upgraded zombie StG 44",
            "zombie_stg44_upgraded",
            "viewmodel_zombie_mp44_lmg_up",
            "mc/mtl_weapon_mp_mp44",
            "mc/mtl_weapon_mp_mp44",
            60,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::SpringClosed,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::ZombieThompson,
            &kZombieThompsonDetachableMagazineWeaponProfile,
            "Zombie Thompson",
            "zombie_thompson",
            "viewmodel_zombie_thompson_smg",
            "mc/mtl_weapon_mp_thompson",
            "mc/mtl_weapon_mp_thompson",
            20,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::LatchOpen,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::ZombieThompsonUpgraded,
            &kZombieThompsonUpgradedDetachableMagazineWeaponProfile,
            "Upgraded zombie Thompson",
            "zombie_thompson_upgraded",
            "viewmodel_zombie_thompson_smg_up",
            "mc/mtl_weapon_thompson_gold",
            "mc/mtl_weapon_thompson_gold",
            40,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::LatchOpen,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::ZombieMp40,
            &kZombieMp40DetachableMagazineWeaponProfile,
            "Zombie MP40",
            "zombie_mp40",
            "viewmodel_zombie_mp40_smg",
            "mc/mtl_weapon_mp_mp40",
            "mc/mtl_weapon_mp_mp40",
            32,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::LatchOpen,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::ZombieMp40Upgraded,
            &kZombieMp40UpgradedDetachableMagazineWeaponProfile,
            "Upgraded zombie MP40",
            "zombie_mp40_upgraded",
            "viewmodel_zombie_mp40_smg_up",
            "mc/mtl_weapon_mp40_gold",
            "mc/mtl_weapon_mp40_gold",
            64,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::LatchOpen,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::ZombieType100,
            &kZombieType100DetachableMagazineWeaponProfile,
            "Zombie Type 100 SMG",
            "zombie_type100_smg",
            "viewmodel_zombie_type100_smg",
            "mc/mtl_weapon_mp_type100_metal",
            "mc/mtl_weapon_mp_type100_metal",
            30,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::LatchOpen,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::ZombieType100Upgraded,
            &kZombieType100UpgradedDetachableMagazineWeaponProfile,
            "Upgraded zombie Type 100 SMG",
            "zombie_type100_smg_upgraded",
            "viewmodel_zombie_type100_smg_up",
            "mc/mtl_weapon_type100_gold",
            "mc/mtl_weapon_type100_gold",
            60,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::LatchOpen,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::ZombieBar,
            &kZombieBarDetachableMagazineWeaponProfile,
            "Zombie BAR",
            "zombie_bar",
            "viewmodel_zombie_bar_lmg",
            "mc/mtl_weapon_mp_bar",
            "mc/mtl_weapon_mp_bar",
            20,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::LatchOpen,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::ZombieBarUpgraded,
            &kZombieBarUpgradedDetachableMagazineWeaponProfile,
            "Upgraded zombie BAR",
            "zombie_bar_upgraded",
            "viewmodel_zombie_bar_lmg_up",
            "mc/mtl_weapon_bar_gold",
            "mc/mtl_weapon_bar_gold",
            30,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::LatchOpen,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::ZombieFg42,
            &kZombieFg42DetachableMagazineWeaponProfile,
            "Zombie FG42",
            "zombie_fg42",
            "viewmodel_zombie_fg42_lmg",
            "mc/mtl_weapon_mp_fg42",
            "mc/mtl_weapon_mp_fg42",
            32,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::LatchOpen,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::ZombieFg42Upgraded,
            &kZombieFg42UpgradedDetachableMagazineWeaponProfile,
            "Upgraded zombie FG42",
            "zombie_fg42_upgraded",
            "viewmodel_zombie_fg42_lmg_up",
            "mc/mtl_weapon_mp_fg42",
            "mc/mtl_weapon_mp_fg42",
            64,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::LatchOpen,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::ZombiePpsh,
            &kZombiePpshDetachableMagazineWeaponProfile,
            "Zombie PPSh-41",
            "zombie_ppsh",
            "viewmodel_zombie_ppsh_smg",
            "mc/mtl_weapon_mp_ppsh41",
            "mc/mtl_weapon_mp_ppsh41",
            71,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::LatchOpen,
            false,
        },
        {
            DetachableMagazineWeaponProfileId::ZombiePpshUpgraded,
            &kZombiePpshUpgradedDetachableMagazineWeaponProfile,
            "Upgraded zombie PPSh-41",
            "zombie_ppsh_upgraded",
            "viewmodel_zombie_ppsh_smg_up",
            "mc/mtl_weapon_ppsh41_gold",
            "mc/mtl_weapon_ppsh41_gold",
            115,
            MagazineChargingHand::Right,
            MagazineChargingCompletion::LatchOpen,
            false,
        },
    }};

void test_phase_one_registry_covers_each_profile_exactly_once() {
    const auto profiles = detachable_magazine_weapon_profiles();
    expect(profiles.size() == kPhaseOneProfileExpectations.size() &&
               validate_detachable_magazine_weapon_profile_registry(),
           "the Phase-1 registry has the exact expected cardinality and validates");

    for (const auto& expected : kPhaseOneProfileExpectations) {
        std::size_t canonical_occurrences = 0;
        std::size_t id_occurrences = 0;
        std::size_t internal_name_occurrences = 0;
        std::size_t identity_occurrences = 0;
        for (const DetachableMagazineWeaponProfile* const registered :
             profiles) {
            expect(registered != nullptr,
                   "the Phase-1 registry never contains a null profile");
            canonical_occurrences += registered == expected.canonical ? 1U : 0U;
            id_occurrences += registered->id == expected.id ? 1U : 0U;
            internal_name_occurrences +=
                std::string_view(registered->internal_weapon_name) ==
                        expected.internal_weapon_name
                    ? 1U
                    : 0U;
            identity_occurrences +=
                std::string_view(registered->internal_weapon_name) ==
                        expected.internal_weapon_name &&
                    std::string_view(registered->viewmodel_model_name) ==
                        expected.viewmodel_model_name
                ? 1U
                : 0U;
        }
        expect(canonical_occurrences == 1 && id_occurrences == 1 &&
                   identity_occurrences == 1,
               "each canonical profile, id, and exact WeaponDef/viewmodel identity occurs exactly once");

        const DetachableMagazineWeaponProfile* const profile =
            find_detachable_magazine_weapon_profile(expected.id);
        expect(profile == expected.canonical &&
                   (internal_name_occurrences == 1
                        ? find_detachable_magazine_weapon_profile_by_internal_name(
                              expected.internal_weapon_name) ==
                              expected.canonical
                        : find_detachable_magazine_weapon_profile_by_internal_name(
                              expected.internal_weapon_name) == nullptr) &&
                   find_detachable_magazine_weapon_profile_by_identity(
                       expected.internal_weapon_name,
                       expected.viewmodel_model_name) == expected.canonical,
               "every Phase-1 identity resolves to its one canonical profile");
        expect(std::string_view(profile->diagnostic_name) ==
                       expected.diagnostic_name &&
                   std::string_view(profile->internal_weapon_name) ==
                       expected.internal_weapon_name &&
                   std::string_view(profile->viewmodel_model_name) ==
                       expected.viewmodel_model_name &&
                   std::string_view(profile->magazine_material_name) ==
                       expected.magazine_material_name &&
                   profile->reload_kind ==
                       ReloadProfileKind::DetachableMagazine &&
                   profile->expected_clip_size == expected.clip_size &&
                   profile->requires_embedded_viewmodel_magazine &&
                   profile->charging.enabled &&
                   std::string_view(profile->charging.handle_bone_tag_name) ==
                       "j_bolt" &&
                   std::string_view(profile->charging.surface_material_name) ==
                       expected.charging_material_name &&
                   profile->charging.manipulating_hand ==
                       expected.charging_hand &&
                   profile->charging.interaction.completion ==
                       expected.charging_completion &&
                   profile->charging.interaction.control_policy ==
                        (expected.id ==
                                     DetachableMagazineWeaponProfileId::M1Garand ||
                                 expected.id ==
                                     DetachableMagazineWeaponProfileId::M1GarandBayonet ||
                                 expected.id ==
                                     DetachableMagazineWeaponProfileId::M1GarandGrenadeLauncher
                            ? MagazineChargingControlPolicy::EnBlocAutomatic
                            : MagazineChargingControlPolicy::ManualPullRelease) &&
                   profile->charging.suppress_native_pose_always ==
                       expected.suppress_native_pose_always,
               "each Phase-1 profile retains its exact identity and charging policy");
    }
}

void test_registry_contains_exact_zombie_colt_profile() {

    const DetachableMagazineWeaponProfile* const profile =
        find_detachable_magazine_weapon_profile(
            DetachableMagazineWeaponProfileId::ZombieColt);
    expect(profile == &kZombieColtDetachableMagazineWeaponProfile,
           "the profile identifier resolves to the canonical Colt entry");
    expect(profile->id ==
                   DetachableMagazineWeaponProfileId::ZombieColt &&
               std::string_view(profile->diagnostic_name) ==
                   "Nacht zombie Colt M1911" &&
               std::string_view(profile->internal_weapon_name) ==
                   "zombie_colt" &&
               std::string_view(profile->viewmodel_model_name) ==
                   "viewmodel_usa_colt45_pistol" &&
               std::string_view(profile->magazine_material_name) ==
                   "mc/mtl_weapon_colt45" &&
               std::string_view(profile->magazine_bone_tag_name) ==
                   "j_clip" &&
               profile->reload_kind ==
                   ReloadProfileKind::DetachableMagazine &&
               profile->expected_clip_size == 8 &&
               profile->requires_embedded_viewmodel_magazine,
           "the Colt identity, asset names, and reload policy are exact");
}

void test_zombie_colt_dedicated_profile_is_exact() {
    const DetachableMagazineWeaponProfile* const profile =
        find_detachable_magazine_weapon_profile(
            DetachableMagazineWeaponProfileId::ZombieColtDedicated);
    expect(profile ==
                   &kZombieColtDedicatedDetachableMagazineWeaponProfile &&
               validate_exact_zombie_colt_dedicated_profile(*profile) &&
               detachable_magazine_mesh_piece_count(profile->mesh) == 1 &&
               detachable_magazine_charging_mesh_piece_count(
                   profile->charging) == 1,
           "the dedicated base Zombie Colt resolves to its exact accepted-family profile");
}

void test_m1carbine_profile_is_exact_and_bounded() {
    const DetachableMagazineWeaponProfile* const profile =
        find_detachable_magazine_weapon_profile(
            DetachableMagazineWeaponProfileId::M1Carbine);
    expect(profile == &kM1CarbineDetachableMagazineWeaponProfile &&
               validate_exact_m1carbine_profile(*profile),
           "the M1A1 profile identifier resolves to its exact canonical entry");
    expect(std::string_view(profile->diagnostic_name) ==
                   "Nacht M1A1 Carbine" &&
               std::string_view(profile->internal_weapon_name) ==
                   "m1carbine" &&
               std::string_view(profile->viewmodel_model_name) ==
                   "viewmodel_usa_m1carbine_rifle" &&
               std::string_view(profile->magazine_material_name) ==
                   "mc/mtl_weapon_carbine" &&
               std::string_view(profile->magazine_bone_tag_name) ==
                   "j_clip" &&
               profile->reload_kind ==
                   ReloadProfileKind::DetachableMagazine &&
               profile->expected_clip_size == 15 &&
               profile->requires_embedded_viewmodel_magazine,
           "the M1A1 identity, asset names, and reload policy are exact");

    const auto& mesh = profile->mesh;
    expect(mesh.expected_model_bone_count == 5 &&
               mesh.expected_model_surface_count == 4 &&
               mesh.magazine_bone_index == 2 &&
               mesh.source_surface_index == 0 &&
               mesh.source_surface_rigid_subrange_count == 1 &&
               mesh.source_rigid_subrange_index == 0 &&
               mesh.source_surface_vertex_count == 74 &&
               mesh.source_surface_triangle_count == 58 &&
               mesh.vertex_offset == 0 && mesh.vertex_count == 74 &&
               mesh.triangle_offset == 0 && mesh.triangle_count == 58,
           "the M1A1 recipe isolates the complete audited j_clip surface");
    expect(mesh.bind_pose.translation.x == -2.436279F &&
               mesh.bind_pose.translation.y == 0.012866F &&
               mesh.bind_pose.translation.z == -0.551744F &&
               mesh.bind_pose.orientation.x == 0.0F &&
               mesh.bind_pose.orientation.y == 0.0F &&
               mesh.bind_pose.orientation.z == 0.0F &&
               mesh.bind_pose.orientation.w == 1.0F,
           "the detached M1A1 magazine retains its audited rigid bind pose");
    const auto& charging = profile->charging;
    expect(charging.enabled &&
               std::string_view(charging.handle_bone_tag_name) == "j_bolt" &&
               std::string_view(charging.surface_material_name) ==
                   "mc/mtl_weapon_carbine" &&
               charging.manipulating_hand == MagazineChargingHand::Right &&
               charging.suppress_native_pose_always &&
               charging.handle_bone_index == 1 &&
               charging.parent_bone_index == 0 &&
               charging.source_surface_index == 2 &&
               charging.source_surface_rigid_subrange_count == 1 &&
               charging.source_rigid_subrange_index == 0 &&
               charging.source_surface_vertex_count == 328 &&
               charging.source_surface_triangle_count == 316 &&
               charging.rigid_vertex_count == 328 &&
               charging.rigid_triangle_count == 315,
           "the M1A1 profile isolates only the audited rigid j_bolt surface");
    expect(charging.bind_pose.translation.x == -1.946244F &&
               charging.bind_pose.translation.y == -1.782576F &&
               charging.bind_pose.translation.z == 0.924210F &&
               charging.bind_pose.orientation.y == -0.010224F &&
               charging.bind_pose.orientation.w == 0.999939F &&
               charging.interaction.travel_units == 2.961060F &&
               charging.interaction.locked_open_offset_units == 2.384359F &&
               charging.interaction.open_threshold == 0.98F &&
               charging.interaction.spring_return_seconds == 1.0F / 6.0F,
            "the M1A1 handle uses the decoded native bind, empty-open, rear, and spring-return calibration");
    constexpr std::array<float, 8> expected_return_samples{
        1.0F,
        228.0F / 255.0F,
        165.0F / 255.0F,
        90.0F / 255.0F,
        27.0F / 255.0F,
        0.0F,
    };
    expect(charging.interaction.trigger_engage == 0.65F &&
               charging.interaction.trigger_release == 0.35F &&
               charging.interaction.completion ==
                   MagazineChargingCompletion::SpringClosed &&
               charging.interaction.return_sample_count == 6 &&
               charging.interaction.return_samples == expected_return_samples &&
               charging.grab_radius_units == 10.0F,
           "the M1A1 identity owns the exact six-sample golden return curve");
    expect(profile->held_pose_from_controller.translation.x == 3.25F &&
               profile->held_pose_from_controller.translation.y == 0.0F &&
               profile->held_pose_from_controller.translation.z == 1.25F &&
               profile->held_pose_from_controller.orientation.w == 1.0F &&
               profile->insertion_radius_units == 9.0F,
           "the M1A1 interaction calibration is explicit and bounded");
}

void test_m1garand_en_bloc_profile_is_exact_and_bounded() {
    const DetachableMagazineWeaponProfile* const profile =
        find_detachable_magazine_weapon_profile(
            DetachableMagazineWeaponProfileId::M1Garand);
    expect(profile == &kM1GarandDetachableMagazineWeaponProfile &&
               validate_exact_m1garand_profile(*profile) &&
               detachable_magazine_mesh_piece_count(profile->mesh) == 1 &&
               detachable_magazine_charging_mesh_piece_count(
                   profile->charging) == 1,
           "the Garand identifier resolves to its exact en-bloc profile");
    expect(std::string_view(profile->diagnostic_name) == "M1 Garand" &&
               std::string_view(profile->internal_weapon_name) ==
                   "m1garand" &&
               std::string_view(profile->viewmodel_model_name) ==
                   "viewmodel_usa_m1garand_rifle" &&
               std::string_view(profile->magazine_material_name) ==
                   "mc/mtl_brass_shells" &&
               std::string_view(profile->magazine_bone_tag_name) ==
                   "j_clip" &&
               profile->reload_kind ==
                   ReloadProfileKind::DetachableMagazine &&
               profile->expected_clip_size == 8 &&
               profile->requires_embedded_viewmodel_magazine,
           "the Garand identity, eight-round clip, and reload policy are exact");

    const auto& mesh = profile->mesh;
    expect(mesh.expected_model_bone_count == 7 &&
               mesh.expected_model_surface_count == 6 &&
               mesh.magazine_bone_index == 2 &&
               mesh.source_surface_index == 4 &&
               mesh.source_surface_rigid_subrange_count == 1 &&
               mesh.source_rigid_subrange_index == 0 &&
               mesh.source_surface_vertex_count == 204 &&
               mesh.source_surface_triangle_count == 130 &&
               mesh.vertex_offset == 0 && mesh.vertex_count == 204 &&
               mesh.triangle_offset == 0 && mesh.triangle_count == 130 &&
               mesh.bind_pose.translation.x == -1.888771F &&
               mesh.bind_pose.translation.y == -0.006035F &&
               mesh.bind_pose.translation.z == -7.283907F &&
               mesh.bind_pose.orientation.w == 1.0F,
           "the Garand feed-device recipe isolates the complete audited j_clip surface");

    const auto& charging = profile->charging;
    expect(charging.enabled &&
               std::string_view(charging.handle_bone_tag_name) == "j_bolt" &&
               std::string_view(charging.surface_material_name) ==
                   "mc/mtl_weapon_m1garand" &&
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
               charging.rigid_vertex_count == 534 &&
               charging.rigid_triangle_count == 485 &&
               charging.bind_pose.translation.x == -0.273143F &&
               charging.bind_pose.translation.y == -1.810324F &&
               charging.bind_pose.translation.z == 1.101824F &&
               charging.bind_pose.orientation.y == -0.010224F &&
               charging.bind_pose.orientation.w == 0.999939F,
           "the Garand profile isolates the audited rigid operating-rod surface");
    constexpr std::array<float, 8> expected_return_samples{
        1.0F,
        228.0F / 255.0F,
        165.0F / 255.0F,
        90.0F / 255.0F,
        27.0F / 255.0F,
        0.0F,
    };
    expect(charging.interaction.control_policy ==
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
               charging.interaction.return_samples == expected_return_samples &&
               charging.grab_radius_units == 0.0F,
           "the Garand clip owns an automatic action close without a controller grab gesture");
    expect(profile->held_pose_from_controller.translation.x == 3.25F &&
               profile->held_pose_from_controller.translation.y == 0.0F &&
               profile->held_pose_from_controller.translation.z == 1.25F &&
               profile->held_pose_from_controller.orientation.w == 1.0F &&
               profile->insertion_radius_units == 9.0F,
           "the Garand clip interaction calibration is explicit and bounded");
}

void test_m1garand_bayonet_profile_is_exact_and_fail_closed() {
    const DetachableMagazineWeaponProfile* const profile =
        find_detachable_magazine_weapon_profile(
            DetachableMagazineWeaponProfileId::M1GarandBayonet);
    expect(profile == &kM1GarandBayonetDetachableMagazineWeaponProfile &&
               validate_exact_m1garand_bayonet_profile(*profile) &&
               detachable_magazine_mesh_piece_count(profile->mesh) == 1 &&
               detachable_magazine_charging_mesh_piece_count(
                   profile->charging) == 1,
           "the bayonetted Garand resolves to its exact en-bloc profile");
    expect(std::string_view(profile->internal_weapon_name) ==
                   "m1garand_bayonet" &&
               std::string_view(profile->viewmodel_model_name) ==
                   "viewmodel_usa_m1garand_rifle_bayonet" &&
               profile->expected_clip_size == 8 &&
               profile->charging.interaction.control_policy ==
                   MagazineChargingControlPolicy::EnBlocAutomatic &&
               profile->charging.interaction.completion ==
                   MagazineChargingCompletion::SpringClosed &&
               profile->charging.interaction.trigger_engage == 0.0F &&
               profile->charging.interaction.trigger_release == 0.0F &&
               profile->charging.grab_radius_units == 0.0F &&
               profile->mesh.expected_model_bone_count == 8 &&
               profile->mesh.expected_model_surface_count == 7 &&
               profile->mesh.magazine_bone_index == 2 &&
               profile->mesh.source_surface_index == 5 &&
               profile->charging.expected_model_bone_count == 8 &&
               profile->charging.expected_model_surface_count == 7 &&
               profile->charging.handle_bone_index == 1 &&
               profile->charging.source_surface_index == 3,
           "the bayonetted Garand retains automatic close and chamber behavior");
    expect(find_detachable_magazine_weapon_profile_by_identity(
               "m1garand_bayonet", "viewmodel_usa_m1garand_rifle") ==
                   nullptr &&
               find_detachable_magazine_weapon_profile_by_identity(
                   "m1garand",
                   "viewmodel_usa_m1garand_rifle_bayonet") == nullptr,
           "base and bayonetted Garand identities cannot be cross-paired");
}

void test_m1garand_grenade_launcher_profile_is_exact_and_fail_closed() {
    const DetachableMagazineWeaponProfile* const profile =
        find_detachable_magazine_weapon_profile(
            DetachableMagazineWeaponProfileId::M1GarandGrenadeLauncher);
    expect(
        profile ==
                &kM1GarandGrenadeLauncherDetachableMagazineWeaponProfile &&
            validate_exact_m1garand_grenade_launcher_profile(*profile) &&
            detachable_magazine_mesh_piece_count(profile->mesh) == 1 &&
            detachable_magazine_charging_mesh_piece_count(
                profile->charging) == 1,
        "the rifle-grenade Garand resolves to its exact en-bloc profile");
    expect(std::string_view(profile->internal_weapon_name) ==
                   "m1garand_gl" &&
               std::string_view(profile->viewmodel_model_name) ==
                   "viewmodel_usa_m1garand_rifle_grenade_mount" &&
               profile->expected_clip_size == 8 &&
               profile->charging.interaction.control_policy ==
                   MagazineChargingControlPolicy::EnBlocAutomatic &&
               profile->mesh.expected_model_bone_count == 8 &&
               profile->mesh.expected_model_surface_count == 7 &&
               profile->mesh.source_surface_index == 4 &&
               profile->charging.expected_model_bone_count == 8 &&
               profile->charging.expected_model_surface_count == 7 &&
               profile->charging.source_surface_index == 2,
           "the rifle-grenade mount cannot widen the Garand reload identity");
    expect(find_detachable_magazine_weapon_profile_by_identity(
               "m1garand_gl", "viewmodel_usa_m1garand_rifle") == nullptr &&
               find_detachable_magazine_weapon_profile_by_identity(
                   "m1garand",
                   "viewmodel_usa_m1garand_rifle_grenade_mount") == nullptr &&
               find_detachable_magazine_weapon_profile_by_identity(
                   "m7_launcher",
                   "viewmodel_usa_m1garand_rifle_grenade_mount") == nullptr,
           "rifle mode, launcher mode, and base Garand cannot cross-pair");
}

void test_zombie_m1carbine_profile_is_exact() {
    const DetachableMagazineWeaponProfile* const profile =
        find_detachable_magazine_weapon_profile(
            DetachableMagazineWeaponProfileId::ZombieM1Carbine);
    expect(profile == &kZombieM1CarbineDetachableMagazineWeaponProfile &&
               validate_exact_zombie_m1carbine_profile(*profile) &&
               detachable_magazine_mesh_piece_count(profile->mesh) == 1 &&
               detachable_magazine_charging_mesh_piece_count(
                   profile->charging) == 1,
           "the dedicated base Zombie M1A1 resolves to its exact accepted-family profile");
}

void test_zombie_gewehr43_profile_is_exact() {
    const DetachableMagazineWeaponProfile* const profile =
        find_detachable_magazine_weapon_profile(
            DetachableMagazineWeaponProfileId::ZombieGewehr43);
    expect(profile == &kZombieGewehr43DetachableMagazineWeaponProfile &&
               validate_exact_zombie_gewehr43_profile(*profile),
           "the base Zombie Gewehr 43 resolves to its physically accepted exact profile");
}

void test_zombie_gewehr43_upgraded_profile_is_exact() {
    const DetachableMagazineWeaponProfile* const profile =
        find_detachable_magazine_weapon_profile(
            DetachableMagazineWeaponProfileId::ZombieGewehr43Upgraded);
    expect(
        profile ==
                &kZombieGewehr43UpgradedDetachableMagazineWeaponProfile &&
            validate_exact_zombie_gewehr43_upgraded_profile(*profile) &&
            detachable_magazine_mesh_piece_count(profile->mesh) == 2 &&
            detachable_magazine_charging_mesh_piece_count(
                profile->charging) == 2,
        "the upgraded Zombie Gewehr 43 resolves to its accepted exact two-piece profile");
}

void test_zombie_stg44_profile_is_exact() {
    const DetachableMagazineWeaponProfile* const profile =
        find_detachable_magazine_weapon_profile(
            DetachableMagazineWeaponProfileId::ZombieStg44);
    expect(profile == &kZombieStg44DetachableMagazineWeaponProfile &&
               validate_exact_zombie_stg44_profile(*profile) &&
               detachable_magazine_mesh_piece_count(profile->mesh) == 1 &&
               detachable_magazine_charging_mesh_piece_count(
                   profile->charging) == 1,
           "the base Zombie StG 44 resolves to its accepted exact single-piece profile");
}

void test_zombie_stg44_upgraded_profile_is_exact() {
    const DetachableMagazineWeaponProfile* const profile =
        find_detachable_magazine_weapon_profile(
            DetachableMagazineWeaponProfileId::ZombieStg44Upgraded);
    expect(
        profile ==
                &kZombieStg44UpgradedDetachableMagazineWeaponProfile &&
            validate_exact_zombie_stg44_upgraded_profile(*profile) &&
            detachable_magazine_mesh_piece_count(profile->mesh) == 1 &&
            detachable_magazine_charging_mesh_piece_count(
                profile->charging) == 1,
        "the upgraded Zombie StG 44 resolves to its accepted exact single-piece profile");
}

void test_zombie_thompson_profile_is_exact() {
    const DetachableMagazineWeaponProfile* const profile =
        find_detachable_magazine_weapon_profile(
            DetachableMagazineWeaponProfileId::ZombieThompson);
    expect(
        profile == &kZombieThompsonDetachableMagazineWeaponProfile &&
            validate_exact_zombie_thompson_profile(*profile) &&
            detachable_magazine_mesh_piece_count(profile->mesh) == 1 &&
            detachable_magazine_charging_mesh_piece_count(
                profile->charging) == 1,
        "the base Zombie Thompson resolves to its physically accepted exact open-bolt profile");
}

void test_zombie_thompson_upgraded_profile_is_exact() {
    const DetachableMagazineWeaponProfile* const profile =
        find_detachable_magazine_weapon_profile(
            DetachableMagazineWeaponProfileId::ZombieThompsonUpgraded);
    expect(
        profile ==
                &kZombieThompsonUpgradedDetachableMagazineWeaponProfile &&
            validate_exact_zombie_thompson_upgraded_profile(*profile) &&
            detachable_magazine_mesh_piece_count(profile->mesh) == 1 &&
            detachable_magazine_charging_mesh_piece_count(
                profile->charging) == 1,
        "the upgraded Zombie Thompson resolves to its physically accepted exact 40-round open-bolt profile");
}

void test_zombie_mp40_profile_is_exact() {
    const DetachableMagazineWeaponProfile* const profile =
        find_detachable_magazine_weapon_profile(
            DetachableMagazineWeaponProfileId::ZombieMp40);
    expect(
        profile == &kZombieMp40DetachableMagazineWeaponProfile &&
            validate_exact_zombie_mp40_profile(*profile) &&
            detachable_magazine_mesh_piece_count(profile->mesh) == 1 &&
            detachable_magazine_charging_mesh_piece_count(
                profile->charging) == 1,
        "the base Zombie MP40 resolves to its physically accepted exact 32-round open-bolt profile");
}

void test_zombie_mp40_upgraded_candidate_profile_is_exact() {
    const DetachableMagazineWeaponProfile* const profile =
        find_detachable_magazine_weapon_profile(
            DetachableMagazineWeaponProfileId::ZombieMp40Upgraded);
    expect(
        profile ==
                &kZombieMp40UpgradedDetachableMagazineWeaponProfile &&
            validate_exact_zombie_mp40_upgraded_profile(*profile) &&
            detachable_magazine_mesh_piece_count(profile->mesh) == 1 &&
            detachable_magazine_charging_mesh_piece_count(
                profile->charging) == 2,
        "the upgraded Zombie MP40 resolves to its exact unaccepted 64-round two-piece open-bolt candidate profile");
}

void test_zombie_type100_profile_is_exact() {
    const DetachableMagazineWeaponProfile* const profile =
        find_detachable_magazine_weapon_profile(
            DetachableMagazineWeaponProfileId::ZombieType100);
    expect(
        profile == &kZombieType100DetachableMagazineWeaponProfile &&
            validate_exact_zombie_type100_profile(*profile) &&
            detachable_magazine_mesh_piece_count(profile->mesh) == 1 &&
            detachable_magazine_charging_mesh_piece_count(
                profile->charging) == 1,
        "the base Zombie Type 100 resolves to its physically accepted exact 30-round single-piece open-bolt profile");
}

void test_zombie_bar_profile_is_exact() {
    const DetachableMagazineWeaponProfile* const profile =
        find_detachable_magazine_weapon_profile(
            DetachableMagazineWeaponProfileId::ZombieBar);
    expect(
        profile == &kZombieBarDetachableMagazineWeaponProfile &&
            validate_exact_zombie_bar_profile(*profile) &&
            detachable_magazine_mesh_piece_count(profile->mesh) == 1 &&
            detachable_magazine_charging_mesh_piece_count(
                profile->charging) == 1,
        "the base Zombie BAR resolves to its physically accepted exact 20-round single-piece open-bolt profile");
}

void test_zombie_fg42_profile_is_exact() {
    const DetachableMagazineWeaponProfile* const profile =
        find_detachable_magazine_weapon_profile(
            DetachableMagazineWeaponProfileId::ZombieFg42);
    expect(
        profile == &kZombieFg42DetachableMagazineWeaponProfile &&
            validate_exact_zombie_fg42_profile(*profile) &&
            detachable_magazine_mesh_piece_count(profile->mesh) == 1 &&
            detachable_magazine_charging_mesh_piece_count(
                profile->charging) == 1,
        "the base Zombie FG42 resolves to its physically accepted exact 32-round single-piece open-bolt profile");
}

void test_zombie_ppsh_profile_is_exact() {
    const DetachableMagazineWeaponProfile* const profile =
        find_detachable_magazine_weapon_profile(
            DetachableMagazineWeaponProfileId::ZombiePpsh);
    expect(
        profile == &kZombiePpshDetachableMagazineWeaponProfile &&
            validate_exact_zombie_ppsh_profile(*profile) &&
            detachable_magazine_mesh_piece_count(profile->mesh) == 1 &&
            detachable_magazine_charging_mesh_piece_count(
                profile->charging) == 1,
        "the base Zombie PPSh-41 resolves to its physically accepted exact 71-round single-piece open-bolt profile");
}

void test_colt_submesh_recipe_is_exact_and_bounded() {
    const auto& profile = kZombieColtDetachableMagazineWeaponProfile;
    const auto& mesh = profile.mesh;
    expect(mesh.expected_model_bone_count == 7 &&
               mesh.expected_model_surface_count == 5 &&
               mesh.magazine_bone_index == 2 &&
               mesh.source_surface_index == 0 &&
               mesh.source_surface_rigid_subrange_count == 3 &&
               mesh.source_rigid_subrange_index == 1,
           "the Colt recipe selects j_clip's exact rigid range in surface zero");
    expect(mesh.source_surface_vertex_count == 252 &&
               mesh.source_surface_triangle_count == 202 &&
               mesh.vertex_offset == 87 && mesh.vertex_count == 75 &&
               mesh.triangle_offset == 75 && mesh.triangle_count == 55,
           "the Colt recipe pins the audited source topology and submesh slice");
    expect(mesh.bind_pose.translation.x == -1.340145F &&
               mesh.bind_pose.translation.y == 0.071632F &&
               mesh.bind_pose.translation.z == -1.237288F &&
               mesh.bind_pose.orientation.x == 0.0F &&
               mesh.bind_pose.orientation.y == 0.0F &&
               mesh.bind_pose.orientation.z == 0.0F &&
               mesh.bind_pose.orientation.w == 1.0F,
           "the detached Colt magazine retains its audited rigid bind pose");
}

void test_interaction_calibration_is_complete() {
    const auto& profile = kZombieColtDetachableMagazineWeaponProfile;
    expect(profile.held_pose_from_controller.translation.x == 3.25F &&
               profile.held_pose_from_controller.translation.y == 0.0F &&
               profile.held_pose_from_controller.translation.z == 1.25F &&
               profile.held_pose_from_controller.orientation.w == 1.0F,
           "the held Colt magazine has an explicit controller-relative pose");
    expect(profile.insertion_guide_pose_from_anchor.translation.x == 0.0F &&
               profile.insertion_guide_pose_from_anchor.translation.y == 0.0F &&
               profile.insertion_guide_pose_from_anchor.translation.z == 0.0F &&
               profile.insertion_guide_pose_from_anchor.orientation.x == 0.0F &&
               profile.insertion_guide_pose_from_anchor.orientation.y == 0.0F &&
               profile.insertion_guide_pose_from_anchor.orientation.z == 0.0F &&
               profile.insertion_guide_pose_from_anchor.orientation.w == 1.0F &&
               profile.insertion_radius_units == 9.0F,
           "the j_clip insertion guide and bounded contact radius are explicit");
}

void test_bar_charging_surface_is_distinct_from_its_magazine_material() {
    const auto& profile = kBarDetachableMagazineWeaponProfile;
    expect(std::string_view(profile.magazine_material_name) ==
                   "mc/mtl_weapon_bar_wood" &&
               std::string_view(profile.charging.surface_material_name) ==
                   "mc/mtl_weapon_bar" &&
               std::string_view(profile.magazine_material_name) !=
                   profile.charging.surface_material_name,
           "the BAR charging handle selects its metal material instead of the wooden magazine material");
}

void test_pistol_charging_profiles_use_the_left_hand() {
    constexpr std::array pistol_ids{
        DetachableMagazineWeaponProfileId::ZombieColt,
        DetachableMagazineWeaponProfileId::Walther,
        DetachableMagazineWeaponProfileId::Colt,
        DetachableMagazineWeaponProfileId::ColtWet,
        DetachableMagazineWeaponProfileId::Tokarev,
        DetachableMagazineWeaponProfileId::Nambu,
        DetachableMagazineWeaponProfileId::ZombieColtDedicated,
        DetachableMagazineWeaponProfileId::ZombieColtUpgraded,
    };
    std::size_t left_hand_profile_count = 0;
    for (const DetachableMagazineWeaponProfile* const profile :
         detachable_magazine_weapon_profiles()) {
        left_hand_profile_count +=
            profile->charging.manipulating_hand == MagazineChargingHand::Left
            ? 1U
            : 0U;
    }
    expect(left_hand_profile_count == pistol_ids.size(),
           "only pistol identities select left-hand charging");
    for (const DetachableMagazineWeaponProfileId id : pistol_ids) {
        const DetachableMagazineWeaponProfile* const profile =
            find_detachable_magazine_weapon_profile(id);
        expect(profile != nullptr &&
                   profile->charging.manipulating_hand ==
                       MagazineChargingHand::Left &&
                   profile->charging.interaction.completion ==
                       MagazineChargingCompletion::SpringClosed,
               "each Phase-1 pistol maps its spring-closed slide to the left hand");
    }
}

void test_registered_names_do_not_require_a_unique_profile() {
    for (const DetachableMagazineWeaponProfile* const profile :
         detachable_magazine_weapon_profiles()) {
        expect(has_detachable_magazine_weapon_profile_for_internal_name(
                   profile->internal_weapon_name),
               "every registered weapon name reaches exact viewmodel validation");
    }
    expect(has_detachable_magazine_weapon_profile_for_internal_name(
               "zombie_colt") &&
               find_detachable_magazine_weapon_profile_by_internal_name(
                   "zombie_colt") == nullptr,
           "Nacht zombie_colt is registered even when its two exact viewmodels make name-only selection ambiguous");

    using namespace std::string_view_literals;
    constexpr std::array unsupported_names{
        ""sv, "Zombie_Colt"sv, "zombie_col"sv, "zombie_colt_extra"sv,
        "zombie_colt "sv, " zombie_colt"sv, "zombie_colt\0extra"sv,
        "colt_mp"sv, "viewmodel_usa_colt45_pistol"sv,
    };
    for (const std::string_view name : unsupported_names) {
        expect(!has_detachable_magazine_weapon_profile_for_internal_name(name),
               "membership requires an exact complete registered weapon name");
    }
}

void test_lookups_require_exact_full_identity() {
    std::size_t shared_colt_viewmodel_count = 0;
    for (const DetachableMagazineWeaponProfile* const profile :
         detachable_magazine_weapon_profiles()) {
        shared_colt_viewmodel_count +=
            std::string_view(profile->viewmodel_model_name) ==
                    "viewmodel_usa_colt45_pistol"
                ? 1U
                : 0U;
    }
    expect(find_detachable_magazine_weapon_profile_by_internal_name(
               "zombie_colt") == nullptr &&
               find_detachable_magazine_weapon_profile_by_internal_name(
                   "colt") == &kColtDetachableMagazineWeaponProfile &&
               find_detachable_magazine_weapon_profile_by_identity(
                   "zombie_colt", "viewmodel_usa_colt45_pistol") ==
                   &kZombieColtDetachableMagazineWeaponProfile &&
               find_detachable_magazine_weapon_profile_by_identity(
                   "zombie_colt", "viewmodel_zombie_colt45_pistol") ==
                   &kZombieColtDedicatedDetachableMagazineWeaponProfile &&
               find_detachable_magazine_weapon_profile_by_identity(
                   "zombie_colt_upgraded",
                   "viewmodel_zombie_colt45_pistol_up") ==
                   &kZombieColtUpgradedDetachableMagazineWeaponProfile &&
               find_detachable_magazine_weapon_profile_by_identity(
                   "colt", "viewmodel_usa_colt45_pistol") ==
                   &kColtDetachableMagazineWeaponProfile &&
               shared_colt_viewmodel_count == 2 &&
               find_detachable_magazine_weapon_profile_by_viewmodel_model_name(
                   "viewmodel_usa_colt45_pistol") == nullptr,
           "all exact Colt identities resolve while reused names and viewmodels remain ambiguous");
    expect(find_detachable_magazine_weapon_profile_by_internal_name(
               "m1carbine") ==
                   &kM1CarbineDetachableMagazineWeaponProfile &&
               find_detachable_magazine_weapon_profile_by_viewmodel_model_name(
                   "viewmodel_usa_m1carbine_rifle") ==
                   &kM1CarbineDetachableMagazineWeaponProfile,
           "the exact M1A1 internal and viewmodel identities resolve");
    expect(find_detachable_magazine_weapon_profile(
               DetachableMagazineWeaponProfileId::None) == nullptr &&
               find_detachable_magazine_weapon_profile(
                   static_cast<DetachableMagazineWeaponProfileId>(255)) ==
                   nullptr &&
               find_detachable_magazine_weapon_profile_by_internal_name({}) ==
                   nullptr &&
               find_detachable_magazine_weapon_profile_by_internal_name(
                   "Zombie_Colt") == nullptr &&
               find_detachable_magazine_weapon_profile_by_internal_name(
                   "zombie_colt_upgraded_missing") == nullptr,
            "unknown, empty, partial, case-changed, and variant names fail closed");
    expect(find_detachable_magazine_weapon_profile_by_identity(
               "zombie_colt", "viewmodel_usa_m1carbine_rifle") == nullptr &&
               find_detachable_magazine_weapon_profile_by_identity(
                   "m1carbine", "viewmodel_usa_colt45_pistol") == nullptr &&
               find_detachable_magazine_weapon_profile_by_identity(
                   {}, "viewmodel_usa_colt45_pistol") == nullptr &&
               find_detachable_magazine_weapon_profile_by_identity(
                   "colt", {}) == nullptr,
           "composite lookup rejects crossed, incomplete, and ambiguous identities");
    expect(find_detachable_magazine_weapon_profile_by_viewmodel_model_name(
               "viewmodel_usa_colt45") == nullptr &&
               find_detachable_magazine_weapon_profile_by_viewmodel_model_name(
                   "viewmodel_usa_colt45_pistol_extra") == nullptr &&
               find_detachable_magazine_weapon_profile_by_viewmodel_model_name(
                   "VIEWMODEL_USA_COLT45_PISTOL") == nullptr,
           "partial, suffixed, and case-changed viewmodel names fail closed");
}

void test_exact_m1carbine_validator_rejects_structurally_valid_drift() {
    DetachableMagazineWeaponProfile drifted =
        kM1CarbineDetachableMagazineWeaponProfile;
    drifted.internal_weapon_name = "m1_carbine";
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_m1carbine_profile(drifted),
           "the M1A1 exact check rejects a structurally valid weapon rename");

    drifted = kM1CarbineDetachableMagazineWeaponProfile;
    drifted.mesh.vertex_count = 73;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_m1carbine_profile(drifted),
           "the M1A1 exact check rejects a bounded but incomplete mesh slice");
}

void test_exact_m1garand_validator_rejects_structurally_valid_drift() {
    DetachableMagazineWeaponProfile drifted =
        kM1GarandDetachableMagazineWeaponProfile;
    drifted.internal_weapon_name = "m1_garand";
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_m1garand_profile(drifted),
           "the Garand exact check rejects a structurally valid weapon rename");

    drifted = kM1GarandDetachableMagazineWeaponProfile;
    drifted.mesh.vertex_count = 203;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_m1garand_profile(drifted),
           "the Garand exact check rejects a bounded but incomplete clip mesh slice");

    drifted = kM1GarandDetachableMagazineWeaponProfile;
    drifted.charging.interaction.travel_units = 3.0F;
    drifted.charging.interaction.locked_open_offset_units = 3.0F;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_m1garand_profile(drifted),
           "the Garand exact check rejects provisional action-travel drift");

    drifted = kM1GarandDetachableMagazineWeaponProfile;
    drifted.charging.interaction.control_policy =
        MagazineChargingControlPolicy::ManualPullRelease;
    drifted.charging.interaction.locked_open_offset_units = 2.384359F;
    drifted.charging.interaction.trigger_engage = 0.65F;
    drifted.charging.interaction.trigger_release = 0.35F;
    drifted.charging.grab_radius_units = 10.0F;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_m1garand_profile(drifted),
           "the Garand exact check rejects replacement with a manual charging gesture");
}

void test_exact_m1garand_bayonet_validator_rejects_drift() {
    DetachableMagazineWeaponProfile drifted =
        kM1GarandBayonetDetachableMagazineWeaponProfile;
    drifted.viewmodel_model_name = "viewmodel_usa_m1garand_rifle";
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_m1garand_bayonet_profile(drifted),
           "the bayonetted Garand exact check rejects base-viewmodel aliasing");

    drifted = kM1GarandBayonetDetachableMagazineWeaponProfile;
    drifted.mesh.vertex_count = 203;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_m1garand_bayonet_profile(drifted),
           "the bayonetted Garand exact check rejects unverified clip topology drift");

    drifted = kM1GarandBayonetDetachableMagazineWeaponProfile;
    drifted.mesh.source_surface_index = 4;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_m1garand_bayonet_profile(drifted),
           "the bayonetted Garand exact check rejects the unshifted base clip surface");

    drifted = kM1GarandBayonetDetachableMagazineWeaponProfile;
    drifted.charging.source_surface_index = 2;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_m1garand_bayonet_profile(drifted),
           "the bayonetted Garand exact check rejects the unshifted base operating-rod surface");
}

void test_exact_m1garand_grenade_launcher_validator_rejects_drift() {
    DetachableMagazineWeaponProfile drifted =
        kM1GarandGrenadeLauncherDetachableMagazineWeaponProfile;
    drifted.viewmodel_model_name = "viewmodel_usa_m1garand_rifle";
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_m1garand_grenade_launcher_profile(drifted),
           "the rifle-grenade Garand exact check rejects base-viewmodel aliasing");

    drifted = kM1GarandGrenadeLauncherDetachableMagazineWeaponProfile;
    drifted.mesh.source_surface_index = 5;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_m1garand_grenade_launcher_profile(drifted),
           "the rifle-grenade Garand rejects a shifted clip surface");

    drifted = kM1GarandGrenadeLauncherDetachableMagazineWeaponProfile;
    drifted.charging.source_surface_index = 3;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_m1garand_grenade_launcher_profile(drifted),
           "the rifle-grenade Garand rejects a shifted operating rod");
}

void test_exact_zombie_colt_dedicated_validator_rejects_structurally_valid_drift() {
    DetachableMagazineWeaponProfile drifted =
        kZombieColtDedicatedDetachableMagazineWeaponProfile;
    drifted.viewmodel_model_name = "viewmodel_usa_colt45_pistol";
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_colt_dedicated_profile(drifted),
           "the dedicated Zombie Colt exact check rejects viewmodel identity drift");

    drifted = kZombieColtDedicatedDetachableMagazineWeaponProfile;
    drifted.mesh.vertex_count = 74;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_colt_dedicated_profile(drifted),
           "the dedicated Zombie Colt exact check rejects bounded magazine topology drift");

    drifted = kZombieColtDedicatedDetachableMagazineWeaponProfile;
    drifted.charging.manipulating_hand = MagazineChargingHand::Right;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_colt_dedicated_profile(drifted),
           "the dedicated Zombie Colt exact check rejects charging-hand drift");
}

void test_exact_zombie_m1carbine_validator_rejects_structurally_valid_drift() {
    DetachableMagazineWeaponProfile drifted =
        kZombieM1CarbineDetachableMagazineWeaponProfile;
    drifted.internal_weapon_name = "m1carbine";
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_m1carbine_profile(drifted),
           "the dedicated Zombie M1A1 exact check rejects weapon identity drift");

    drifted = kZombieM1CarbineDetachableMagazineWeaponProfile;
    drifted.magazine_material_name = "mc/mtl_weapon_carbine";
    drifted.charging.surface_material_name = "mc/mtl_weapon_carbine";
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_m1carbine_profile(drifted),
           "the dedicated Zombie M1A1 exact check rejects material identity drift");

    drifted = kZombieM1CarbineDetachableMagazineWeaponProfile;
    drifted.charging.interaction.travel_units = 3.0F;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_m1carbine_profile(drifted),
           "the dedicated Zombie M1A1 exact check rejects charging-travel drift");
}

void test_exact_zombie_gewehr43_validator_rejects_structurally_valid_drift() {
    DetachableMagazineWeaponProfile drifted =
        kZombieGewehr43DetachableMagazineWeaponProfile;
    drifted.internal_weapon_name = "zombie_g43";
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_gewehr43_profile(drifted),
           "the Zombie Gewehr 43 exact check rejects a structurally valid weapon rename");

    drifted = kZombieGewehr43DetachableMagazineWeaponProfile;
    drifted.mesh.vertex_count = 476;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_gewehr43_profile(drifted),
           "the Zombie Gewehr 43 exact check rejects a bounded but incomplete magazine slice");

    drifted = kZombieGewehr43DetachableMagazineWeaponProfile;
    drifted.charging.interaction.spring_return_seconds = 0.16F;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_gewehr43_profile(drifted),
           "the Zombie Gewehr 43 exact check rejects silent charging calibration drift");
}

void test_exact_zombie_gewehr43_upgraded_validator_rejects_structurally_valid_drift() {
    DetachableMagazineWeaponProfile drifted =
        kZombieGewehr43UpgradedDetachableMagazineWeaponProfile;
    drifted.expected_clip_size = 11;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_gewehr43_upgraded_profile(drifted),
           "the upgraded Zombie Gewehr 43 exact check rejects clip-capacity drift");

    drifted = kZombieGewehr43UpgradedDetachableMagazineWeaponProfile;
    drifted.mesh.additional_pieces[0].vertex_count = 101;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_gewehr43_upgraded_profile(drifted),
           "the upgraded Zombie Gewehr 43 exact check rejects second magazine-piece drift");

    drifted = kZombieGewehr43UpgradedDetachableMagazineWeaponProfile;
    drifted.charging.additional_pieces[0].triangle_count = 332;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_gewehr43_upgraded_profile(drifted),
           "the upgraded Zombie Gewehr 43 exact check rejects second charging-piece drift");

    drifted = kZombieGewehr43UpgradedDetachableMagazineWeaponProfile;
    drifted.held_pose_from_controller.translation.x = 3.0F;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_gewehr43_upgraded_profile(drifted),
           "the upgraded Zombie Gewehr 43 exact check rejects accepted-pose drift");
}

void test_exact_zombie_stg44_validator_rejects_structurally_valid_drift() {
    DetachableMagazineWeaponProfile drifted =
        kZombieStg44DetachableMagazineWeaponProfile;
    drifted.internal_weapon_name = "zombie_mp44";
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_stg44_profile(drifted),
           "the Zombie StG 44 exact check rejects a structurally valid identity drift");

    drifted = kZombieStg44DetachableMagazineWeaponProfile;
    drifted.expected_clip_size = 29;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_stg44_profile(drifted),
           "the Zombie StG 44 exact check rejects clip-capacity drift");

    drifted = kZombieStg44DetachableMagazineWeaponProfile;
    drifted.mesh.additional_piece_count = 1;
    drifted.mesh.additional_pieces[0] = {
        .material_name = "mc/mtl_weapon_mp_mp44",
        .source_surface_index = 3,
        .source_surface_rigid_subrange_count = 1,
        .source_rigid_subrange_index = 0,
        .source_surface_vertex_count = 1,
        .source_surface_triangle_count = 1,
        .vertex_offset = 0,
        .vertex_count = 1,
        .triangle_offset = 0,
        .triangle_count = 1,
    };
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_stg44_profile(drifted),
           "the Zombie StG 44 exact check rejects a structurally valid second magazine piece");

    drifted = kZombieStg44DetachableMagazineWeaponProfile;
    drifted.charging.additional_piece_count = 1;
    drifted.charging.additional_pieces[0] = {
        .material_name = "mc/mtl_weapon_mp_mp44",
        .source_surface_index = 3,
        .source_surface_rigid_subrange_count = 1,
        .source_rigid_subrange_index = 0,
        .source_surface_vertex_count = 1,
        .source_surface_triangle_count = 1,
        .vertex_offset = 0,
        .vertex_count = 1,
        .triangle_offset = 0,
        .triangle_count = 1,
    };
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_stg44_profile(drifted),
           "the Zombie StG 44 exact check rejects a structurally valid second charging piece");

    drifted = kZombieStg44DetachableMagazineWeaponProfile;
    drifted.charging.interaction.travel_units = 3.0F;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_stg44_profile(drifted),
           "the Zombie StG 44 exact check rejects charging calibration drift");

    drifted = kZombieStg44DetachableMagazineWeaponProfile;
    drifted.insertion_guide_pose_from_anchor.translation.z = 0.25F;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_stg44_profile(drifted),
           "the Zombie StG 44 exact check rejects accepted insertion-pose drift");
}

void test_exact_zombie_stg44_upgraded_validator_rejects_structurally_valid_drift() {
    DetachableMagazineWeaponProfile drifted =
        kZombieStg44UpgradedDetachableMagazineWeaponProfile;
    drifted.expected_clip_size = 59;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_stg44_upgraded_profile(drifted),
           "the upgraded Zombie StG 44 exact check rejects clip-capacity drift");

    drifted = kZombieStg44UpgradedDetachableMagazineWeaponProfile;
    drifted.mesh.expected_model_surface_count = 4;
    drifted.charging.expected_model_surface_count = 4;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_stg44_upgraded_profile(drifted),
           "the upgraded Zombie StG 44 exact check rejects accepted body-surface drift");

    drifted = kZombieStg44UpgradedDetachableMagazineWeaponProfile;
    drifted.charging.source_surface_index = 3;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_stg44_upgraded_profile(drifted),
           "the upgraded Zombie StG 44 exact check rejects charging surface-selection drift");

    drifted = kZombieStg44UpgradedDetachableMagazineWeaponProfile;
    drifted.charging.interaction.travel_units = 3.0F;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_stg44_upgraded_profile(drifted),
           "the upgraded Zombie StG 44 exact check rejects charging calibration drift");

    drifted = kZombieStg44UpgradedDetachableMagazineWeaponProfile;
    drifted.held_pose_from_controller.translation.x = 3.0F;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_stg44_upgraded_profile(drifted),
           "the upgraded Zombie StG 44 exact check rejects accepted-pose drift");
}

void test_exact_zombie_thompson_validator_rejects_structurally_valid_drift() {
    DetachableMagazineWeaponProfile drifted =
        kZombieThompsonDetachableMagazineWeaponProfile;
    drifted.internal_weapon_name = "zombie_thompson_alt";
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_thompson_profile(drifted),
           "the Zombie Thompson exact check rejects a structurally valid identity drift");

    drifted = kZombieThompsonDetachableMagazineWeaponProfile;
    drifted.expected_clip_size = 21;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_thompson_profile(drifted),
           "the Zombie Thompson exact check rejects clip-capacity drift");

    drifted = kZombieThompsonDetachableMagazineWeaponProfile;
    drifted.mesh.additional_piece_count = 1;
    drifted.mesh.additional_pieces[0] = {
        .material_name = "mc/mtl_weapon_mp_thompson",
        .source_surface_index = 3,
        .source_surface_rigid_subrange_count = 1,
        .source_rigid_subrange_index = 0,
        .source_surface_vertex_count = 1,
        .source_surface_triangle_count = 1,
        .vertex_offset = 0,
        .vertex_count = 1,
        .triangle_offset = 0,
        .triangle_count = 1,
    };
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_thompson_profile(drifted),
           "the Zombie Thompson exact check rejects a structurally valid second magazine piece");

    drifted = kZombieThompsonDetachableMagazineWeaponProfile;
    drifted.charging.source_surface_index = 3;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_thompson_profile(drifted),
           "the Zombie Thompson exact check rejects charging surface-selection drift");

    drifted = kZombieThompsonDetachableMagazineWeaponProfile;
    drifted.charging.interaction.travel_units = 3.25F;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_thompson_profile(drifted),
           "the Zombie Thompson exact check rejects open-bolt travel drift");
}

void test_exact_zombie_thompson_upgraded_validator_rejects_structurally_valid_drift() {
    DetachableMagazineWeaponProfile drifted =
        kZombieThompsonUpgradedDetachableMagazineWeaponProfile;
    drifted.expected_clip_size = 39;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_thompson_upgraded_profile(drifted),
           "the upgraded Zombie Thompson exact check rejects clip-capacity drift");

    drifted = kZombieThompsonUpgradedDetachableMagazineWeaponProfile;
    drifted.magazine_material_name = "mc/mtl_weapon_mp_thompson";
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_thompson_upgraded_profile(drifted),
           "the upgraded Zombie Thompson exact check rejects gold-material drift");

    drifted = kZombieThompsonUpgradedDetachableMagazineWeaponProfile;
    drifted.mesh.expected_model_surface_count = 4;
    drifted.charging.expected_model_surface_count = 4;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_thompson_upgraded_profile(drifted),
           "the upgraded Zombie Thompson exact check rejects fifth-body-surface drift");

    drifted = kZombieThompsonUpgradedDetachableMagazineWeaponProfile;
    drifted.charging.interaction.completion =
        MagazineChargingCompletion::SpringClosed;
    drifted.charging.interaction.spring_return_seconds = 0.15F;
    drifted.charging.interaction.return_sample_count = 2;
    drifted.charging.interaction.return_samples[0] = 1.0F;
    drifted.charging.interaction.return_samples[1] = 0.0F;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_thompson_upgraded_profile(drifted),
           "the upgraded Zombie Thompson exact check rejects open-bolt policy drift");
}

void test_exact_zombie_mp40_validator_rejects_structurally_valid_drift() {
    DetachableMagazineWeaponProfile drifted =
        kZombieMp40DetachableMagazineWeaponProfile;
    drifted.internal_weapon_name = "zombie_mp40_alt";
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_mp40_profile(drifted),
           "the Zombie MP40 exact check rejects a structurally valid identity drift");

    drifted = kZombieMp40DetachableMagazineWeaponProfile;
    drifted.expected_clip_size = 33;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_mp40_profile(drifted),
           "the Zombie MP40 exact check rejects clip-capacity drift");

    drifted = kZombieMp40DetachableMagazineWeaponProfile;
    drifted.mesh.source_surface_index = 0;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_mp40_profile(drifted),
           "the Zombie MP40 exact check rejects magazine surface-selection drift");

    drifted = kZombieMp40DetachableMagazineWeaponProfile;
    drifted.charging.interaction.travel_units = 3.25F;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_mp40_profile(drifted),
           "the Zombie MP40 exact check rejects open-bolt travel drift");
}

void test_exact_zombie_mp40_upgraded_candidate_validator_rejects_structurally_valid_drift() {
    DetachableMagazineWeaponProfile drifted =
        kZombieMp40UpgradedDetachableMagazineWeaponProfile;
    drifted.expected_clip_size = 63;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_mp40_upgraded_profile(drifted),
           "the upgraded Zombie MP40 candidate exact check rejects clip-capacity drift");

    drifted = kZombieMp40UpgradedDetachableMagazineWeaponProfile;
    drifted.magazine_material_name = "mc/mtl_weapon_mp_mp40";
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_mp40_upgraded_profile(drifted),
           "the upgraded Zombie MP40 candidate exact check rejects gold-material drift");

    drifted = kZombieMp40UpgradedDetachableMagazineWeaponProfile;
    drifted.mesh.expected_model_surface_count = 4;
    drifted.charging.expected_model_surface_count = 4;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_mp40_upgraded_profile(drifted),
           "the upgraded Zombie MP40 candidate exact check rejects fifth-body-surface drift");

    drifted = kZombieMp40UpgradedDetachableMagazineWeaponProfile;
    drifted.charging.additional_pieces[0].triangle_count = 25;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_mp40_upgraded_profile(drifted),
           "the upgraded Zombie MP40 candidate exact check rejects silver-etching topology drift");

    drifted = kZombieMp40UpgradedDetachableMagazineWeaponProfile;
    drifted.charging.additional_pieces[0].source_surface_index = 4;
    drifted.charging.additional_pieces[0].source_surface_vertex_count = 265;
    drifted.charging.additional_pieces[0].source_surface_triangle_count = 268;
    drifted.charging.additional_pieces[0].vertex_count = 265;
    drifted.charging.additional_pieces[0].triangle_count = 268;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_mp40_upgraded_profile(drifted),
           "the upgraded Zombie MP40 candidate exact check rejects the silver body-trim surface");

    drifted = kZombieMp40UpgradedDetachableMagazineWeaponProfile;
    drifted.charging.interaction.completion =
        MagazineChargingCompletion::SpringClosed;
    drifted.charging.interaction.spring_return_seconds = 0.15F;
    drifted.charging.interaction.return_sample_count = 2;
    drifted.charging.interaction.return_samples[0] = 1.0F;
    drifted.charging.interaction.return_samples[1] = 0.0F;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_mp40_upgraded_profile(drifted),
           "the upgraded Zombie MP40 candidate exact check rejects open-bolt policy drift");
}

void test_exact_zombie_type100_validator_rejects_structurally_valid_drift() {
    DetachableMagazineWeaponProfile drifted =
        kZombieType100DetachableMagazineWeaponProfile;
    drifted.internal_weapon_name = "zombie_type100_smg_alt";
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_type100_profile(drifted),
           "the Zombie Type 100 exact check rejects a structurally valid identity drift");

    drifted = kZombieType100DetachableMagazineWeaponProfile;
    drifted.expected_clip_size = 31;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_type100_profile(drifted),
           "the Zombie Type 100 exact check rejects clip-capacity drift");

    drifted = kZombieType100DetachableMagazineWeaponProfile;
    drifted.mesh.source_surface_index = 0;
    drifted.mesh.source_surface_vertex_count = 4280;
    drifted.mesh.source_surface_triangle_count = 4602;
    drifted.mesh.vertex_count = 4280;
    drifted.mesh.triangle_count = 4602;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_type100_profile(drifted),
           "the Zombie Type 100 exact check rejects same-material body geometry as its magazine");

    drifted = kZombieType100DetachableMagazineWeaponProfile;
    drifted.charging.source_surface_index = 0;
    drifted.charging.source_surface_vertex_count = 4280;
    drifted.charging.source_surface_triangle_count = 4602;
    drifted.charging.rigid_vertex_count = 4280;
    drifted.charging.rigid_triangle_count = 4602;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_type100_profile(drifted),
           "the Zombie Type 100 exact check rejects same-material body geometry as its charging handle");

    drifted = kZombieType100DetachableMagazineWeaponProfile;
    drifted.charging.interaction.completion =
        MagazineChargingCompletion::SpringClosed;
    drifted.charging.interaction.spring_return_seconds = 0.15F;
    drifted.charging.interaction.return_sample_count = 2;
    drifted.charging.interaction.return_samples[0] = 1.0F;
    drifted.charging.interaction.return_samples[1] = 0.0F;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_type100_profile(drifted),
           "the Zombie Type 100 exact check rejects spring-closed policy drift");
}

void test_exact_zombie_bar_validator_rejects_structurally_valid_drift() {
    DetachableMagazineWeaponProfile drifted =
        kZombieBarDetachableMagazineWeaponProfile;
    drifted.internal_weapon_name = "zombie_bar_alt";
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_bar_profile(drifted),
           "the Zombie BAR exact check rejects a structurally valid identity drift");

    drifted = kZombieBarDetachableMagazineWeaponProfile;
    drifted.expected_clip_size = 21;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_bar_profile(drifted),
           "the Zombie BAR exact check rejects clip-capacity drift");

    drifted = kZombieBarDetachableMagazineWeaponProfile;
    drifted.mesh.source_rigid_subrange_index = 1;
    drifted.mesh.vertex_offset = 24;
    drifted.mesh.vertex_count = 52;
    drifted.mesh.triangle_offset = 12;
    drifted.mesh.triangle_count = 28;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_bar_profile(drifted),
           "the Zombie BAR exact check rejects surface-zero rigid one as its magazine");

    drifted = kZombieBarDetachableMagazineWeaponProfile;
    drifted.mesh.source_surface_index = 1;
    drifted.mesh.source_surface_rigid_subrange_count = 1;
    drifted.mesh.source_rigid_subrange_index = 0;
    drifted.mesh.source_surface_vertex_count = 4190;
    drifted.mesh.source_surface_triangle_count = 4312;
    drifted.mesh.vertex_offset = 0;
    drifted.mesh.vertex_count = 4190;
    drifted.mesh.triangle_offset = 0;
    drifted.mesh.triangle_count = 4311;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_bar_profile(drifted),
           "the Zombie BAR exact check rejects same-material body geometry as its magazine");

    drifted = kZombieBarDetachableMagazineWeaponProfile;
    drifted.charging.rigid_triangle_count = 574;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_bar_profile(drifted),
           "the Zombie BAR exact check rejects the source surface's extra charging triangle");

    drifted = kZombieBarDetachableMagazineWeaponProfile;
    drifted.charging.manipulating_hand = MagazineChargingHand::Left;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_bar_profile(drifted),
           "the Zombie BAR exact check rejects charging-hand drift");

    drifted = kZombieBarDetachableMagazineWeaponProfile;
    drifted.charging.interaction.travel_units = 3.50F;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_bar_profile(drifted),
           "the Zombie BAR exact check rejects open-bolt travel drift");

    drifted = kZombieBarDetachableMagazineWeaponProfile;
    drifted.charging.interaction.completion =
        MagazineChargingCompletion::SpringClosed;
    drifted.charging.interaction.spring_return_seconds = 0.15F;
    drifted.charging.interaction.return_sample_count = 2;
    drifted.charging.interaction.return_samples[0] = 1.0F;
    drifted.charging.interaction.return_samples[1] = 0.0F;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_bar_profile(drifted),
           "the Zombie BAR exact check rejects spring-closed policy drift");
}

void test_exact_zombie_fg42_validator_rejects_structurally_valid_drift() {
    DetachableMagazineWeaponProfile drifted =
        kZombieFg42DetachableMagazineWeaponProfile;
    drifted.internal_weapon_name = "zombie_fg42_alt";
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_fg42_profile(drifted),
           "the Zombie FG42 exact check rejects a structurally valid identity drift");

    drifted = kZombieFg42DetachableMagazineWeaponProfile;
    drifted.expected_clip_size = 33;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_fg42_profile(drifted),
           "the Zombie FG42 exact check rejects clip-capacity drift");

    drifted = kZombieFg42DetachableMagazineWeaponProfile;
    drifted.charging.source_rigid_subrange_index = 0;
    drifted.charging.rigid_vertex_offset = 0;
    drifted.charging.rigid_vertex_count = 4;
    drifted.charging.rigid_triangle_offset = 0;
    drifted.charging.rigid_triangle_count = 2;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_fg42_profile(drifted),
           "the Zombie FG42 exact check rejects surface-zero rigid zero's body sliver as its charging handle");

    drifted = kZombieFg42DetachableMagazineWeaponProfile;
    drifted.mesh.source_surface_index = 1;
    drifted.mesh.source_surface_rigid_subrange_count = 1;
    drifted.mesh.source_rigid_subrange_index = 0;
    drifted.mesh.source_surface_vertex_count = 3080;
    drifted.mesh.source_surface_triangle_count = 2552;
    drifted.mesh.vertex_offset = 0;
    drifted.mesh.vertex_count = 3080;
    drifted.mesh.triangle_offset = 0;
    drifted.mesh.triangle_count = 2552;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_fg42_profile(drifted),
           "the Zombie FG42 exact check rejects same-material body geometry as its magazine");

    drifted = kZombieFg42DetachableMagazineWeaponProfile;
    drifted.charging.source_surface_index = 1;
    drifted.charging.source_surface_rigid_subrange_count = 1;
    drifted.charging.source_rigid_subrange_index = 0;
    drifted.charging.source_surface_vertex_count = 3080;
    drifted.charging.source_surface_triangle_count = 2552;
    drifted.charging.rigid_vertex_offset = 0;
    drifted.charging.rigid_vertex_count = 3080;
    drifted.charging.rigid_triangle_offset = 0;
    drifted.charging.rigid_triangle_count = 2552;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_fg42_profile(drifted),
           "the Zombie FG42 exact check rejects same-material body geometry as its charging handle");

    drifted = kZombieFg42DetachableMagazineWeaponProfile;
    drifted.charging.manipulating_hand = MagazineChargingHand::Left;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_fg42_profile(drifted),
           "the Zombie FG42 exact check rejects charging-hand drift");

    drifted = kZombieFg42DetachableMagazineWeaponProfile;
    drifted.charging.interaction.travel_units = 3.20F;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_fg42_profile(drifted),
           "the Zombie FG42 exact check rejects open-bolt travel drift");

    drifted = kZombieFg42DetachableMagazineWeaponProfile;
    drifted.charging.interaction.completion =
        MagazineChargingCompletion::SpringClosed;
    drifted.charging.interaction.spring_return_seconds = 0.15F;
    drifted.charging.interaction.return_sample_count = 2;
    drifted.charging.interaction.return_samples[0] = 1.0F;
    drifted.charging.interaction.return_samples[1] = 0.0F;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_fg42_profile(drifted),
           "the Zombie FG42 exact check rejects spring-closed policy drift");
}

void test_exact_zombie_ppsh_validator_rejects_structurally_valid_drift() {
    DetachableMagazineWeaponProfile drifted =
        kZombiePpshDetachableMagazineWeaponProfile;
    drifted.internal_weapon_name = "zombie_ppsh_alt";
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_ppsh_profile(drifted),
           "the Zombie PPSh-41 exact check rejects a structurally valid identity drift");

    drifted = kZombiePpshDetachableMagazineWeaponProfile;
    drifted.expected_clip_size = 72;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_ppsh_profile(drifted),
           "the Zombie PPSh-41 exact check rejects clip-capacity drift");

    drifted = kZombiePpshDetachableMagazineWeaponProfile;
    drifted.mesh.source_surface_index = 0;
    drifted.mesh.source_surface_rigid_subrange_count = 1;
    drifted.mesh.source_rigid_subrange_index = 0;
    drifted.mesh.source_surface_vertex_count = 4221;
    drifted.mesh.source_surface_triangle_count = 4366;
    drifted.mesh.vertex_offset = 0;
    drifted.mesh.vertex_count = 4221;
    drifted.mesh.triangle_offset = 0;
    drifted.mesh.triangle_count = 4366;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_ppsh_profile(drifted),
           "the Zombie PPSh-41 exact check rejects same-material body geometry as its magazine");

    drifted = kZombiePpshDetachableMagazineWeaponProfile;
    drifted.charging.source_surface_index = 0;
    drifted.charging.source_surface_rigid_subrange_count = 1;
    drifted.charging.source_rigid_subrange_index = 0;
    drifted.charging.source_surface_vertex_count = 4221;
    drifted.charging.source_surface_triangle_count = 4366;
    drifted.charging.rigid_vertex_offset = 0;
    drifted.charging.rigid_vertex_count = 4221;
    drifted.charging.rigid_triangle_offset = 0;
    drifted.charging.rigid_triangle_count = 4366;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_ppsh_profile(drifted),
           "the Zombie PPSh-41 exact check rejects same-material body geometry as its charging handle");

    drifted = kZombiePpshDetachableMagazineWeaponProfile;
    drifted.charging.manipulating_hand = MagazineChargingHand::Left;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_ppsh_profile(drifted),
           "the Zombie PPSh-41 exact check rejects charging-hand drift");

    drifted = kZombiePpshDetachableMagazineWeaponProfile;
    drifted.charging.interaction.travel_units = 3.10F;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_ppsh_profile(drifted),
           "the Zombie PPSh-41 exact check rejects open-bolt travel drift");

    drifted = kZombiePpshDetachableMagazineWeaponProfile;
    drifted.charging.interaction.completion =
        MagazineChargingCompletion::SpringClosed;
    drifted.charging.interaction.spring_return_seconds = 0.15F;
    drifted.charging.interaction.return_sample_count = 2;
    drifted.charging.interaction.return_samples[0] = 1.0F;
    drifted.charging.interaction.return_samples[1] = 0.0F;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_ppsh_profile(drifted),
           "the Zombie PPSh-41 exact check rejects spring-closed policy drift");
}

void test_validator_rejects_incomplete_identity_and_policy() {
    DetachableMagazineWeaponProfile malformed =
        kZombieColtDetachableMagazineWeaponProfile;
    malformed.id = DetachableMagazineWeaponProfileId::None;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "the sentinel profile identifier is rejected");

    malformed = kZombieColtDetachableMagazineWeaponProfile;
    malformed.id =
        static_cast<DetachableMagazineWeaponProfileId>(255);
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "an unknown profile identifier is rejected");

    malformed = kZombieColtDetachableMagazineWeaponProfile;
    malformed.internal_weapon_name = "";
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "an empty exact weapon identity is rejected");

    malformed = kZombieColtDetachableMagazineWeaponProfile;
    malformed.magazine_material_name = nullptr;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "a missing exact source material is rejected");

    malformed = kZombieColtDetachableMagazineWeaponProfile;
    malformed.magazine_bone_tag_name = "";
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "an empty rigid-bone identity is rejected");

    malformed = kZombieColtDetachableMagazineWeaponProfile;
    malformed.reload_kind = ReloadProfileKind::NativeOnly;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "a profile without detachable-magazine policy is rejected");

    malformed = kZombieColtDetachableMagazineWeaponProfile;
    malformed.expected_clip_size = 0;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "a profile without an exact clip capacity is rejected");

    malformed = kZombieColtDetachableMagazineWeaponProfile;
    malformed.requires_embedded_viewmodel_magazine = false;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "the Colt profile cannot silently switch to an absent world clip model");
}

void test_exact_colt_validator_rejects_structurally_valid_drift() {
    DetachableMagazineWeaponProfile drifted =
        kZombieColtDetachableMagazineWeaponProfile;
    drifted.internal_weapon_name = "colt";
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_colt_profile(drifted),
           "the shipping exact check rejects a structurally valid weapon rename");

    drifted = kZombieColtDetachableMagazineWeaponProfile;
    drifted.mesh.vertex_count = 74;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_colt_profile(drifted),
           "the shipping exact check rejects a bounded but unaudited mesh slice");

    drifted = kZombieColtDetachableMagazineWeaponProfile;
    drifted.held_pose_from_controller.translation.x = 3.0F;
    expect(validate_detachable_magazine_weapon_profile(drifted) &&
               !validate_exact_zombie_colt_profile(drifted),
           "the shipping exact check rejects silent controller-pose drift");
}

void test_validator_rejects_malformed_mesh_recipes() {
    DetachableMagazineWeaponProfile malformed =
        kZombieColtDetachableMagazineWeaponProfile;
    malformed.mesh.expected_model_bone_count = 0;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "a recipe without an expected skeleton is rejected");

    malformed = kZombieColtDetachableMagazineWeaponProfile;
    malformed.mesh.magazine_bone_index = 7;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "a magazine bone outside the expected model is rejected");

    malformed = kZombieColtDetachableMagazineWeaponProfile;
    malformed.mesh.source_surface_index = 5;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "a source surface outside the expected model is rejected");

    malformed = kZombieColtDetachableMagazineWeaponProfile;
    malformed.mesh.source_rigid_subrange_index = 3;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "a rigid range outside the source surface is rejected");

    malformed = kZombieColtDetachableMagazineWeaponProfile;
    malformed.mesh.vertex_count = 0;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "an empty detached vertex slice is rejected");

    malformed = kZombieColtDetachableMagazineWeaponProfile;
    malformed.mesh.vertex_offset = 200;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "a detached vertex slice past the source surface is rejected");

    malformed = kZombieColtDetachableMagazineWeaponProfile;
    malformed.mesh.triangle_offset = 180;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "a detached triangle slice past the source surface is rejected");

    malformed = kZombieColtDetachableMagazineWeaponProfile;
    malformed.mesh.bind_pose.orientation.w = 0.0F;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "a non-normalized rigid bind orientation is rejected");
}

void test_multi_piece_mesh_recipe_is_exact_and_fail_closed() {
    DetachableMagazineWeaponProfile multi_piece =
        kZombieColtDetachableMagazineWeaponProfile;
    multi_piece.mesh.additional_piece_count = 1;
    multi_piece.mesh.additional_pieces[0] = {
        .material_name = "mc/mtl_weapon_colt45_detail",
        .source_surface_index = 1,
        .source_surface_rigid_subrange_count = 2,
        .source_rigid_subrange_index = 0,
        .source_surface_vertex_count = 24,
        .source_surface_triangle_count = 16,
        .vertex_offset = 0,
        .vertex_count = 10,
        .triangle_offset = 0,
        .triangle_count = 6,
    };
    const auto primary = detachable_magazine_mesh_piece(multi_piece, 0);
    const auto detail = detachable_magazine_mesh_piece(multi_piece, 1);
    expect(validate_detachable_magazine_weapon_profile(multi_piece) &&
               detachable_magazine_mesh_piece_count(multi_piece.mesh) == 2 &&
               primary.source_surface_index == 0 &&
               primary.source_rigid_subrange_index == 1 &&
               primary.vertex_offset == 87 && primary.vertex_count == 75 &&
               detail.source_surface_index == 1 &&
               detail.source_rigid_subrange_index == 0 &&
               detail.vertex_count == 10 && detail.triangle_count == 6,
           "a two-surface magazine preserves the legacy primary extraction and publishes its exact additional piece");
    expect(!validate_exact_zombie_colt_profile(multi_piece),
           "the audited single-piece Colt cannot silently acquire another rigid piece");

    DetachableMagazineWeaponProfile malformed = multi_piece;
    malformed.mesh.additional_pieces[0].material_name = "";
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "each additional magazine piece requires an exact material identity");

    malformed = multi_piece;
    malformed.mesh.additional_pieces[0].source_surface_index = 5;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "an additional piece outside the exact model surface table is rejected");

    malformed = multi_piece;
    malformed.mesh.additional_pieces[0].source_surface_index =
        malformed.mesh.source_surface_index;
    malformed.mesh.additional_pieces[0].source_rigid_subrange_index =
        malformed.mesh.source_rigid_subrange_index;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "the same surface and rigid range cannot be extracted twice");

    malformed = multi_piece;
    malformed.mesh.additional_pieces[0].vertex_offset = 20;
    malformed.mesh.additional_pieces[0].vertex_count = 10;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "an additional piece cannot escape its audited source vertex span");

    malformed = kZombieColtDetachableMagazineWeaponProfile;
    malformed.mesh.additional_pieces[1].material_name =
        "mc/mtl_hidden_unaudited_piece";
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "unused additional-piece slots cannot carry hidden unaudited data");

    malformed = multi_piece;
    malformed.mesh.additional_piece_count = 4;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "a recipe cannot exceed the fixed detached-magazine piece budget");
}

void test_multi_piece_charging_recipe_is_exact_and_fail_closed() {
    const auto& upgraded =
        kZombieM1CarbineUpgradedDetachableMagazineWeaponProfile;
    const auto primary =
        detachable_magazine_charging_mesh_piece(upgraded.charging, 0);
    const auto etching =
        detachable_magazine_charging_mesh_piece(upgraded.charging, 1);
    expect(validate_detachable_magazine_weapon_profile(upgraded) &&
               detachable_magazine_charging_mesh_piece_count(
                   upgraded.charging) == 2 &&
               primary.source_surface_index == 2 &&
               primary.source_rigid_subrange_index == 0 &&
               primary.vertex_count == 328 &&
               primary.triangle_count == 315 &&
               etching.source_surface_index == 4 &&
               etching.source_rigid_subrange_index == 1 &&
               etching.vertex_offset == 121 &&
               etching.vertex_count == 71 &&
               etching.triangle_offset == 120 &&
               etching.triangle_count == 57,
           "the upgraded M1 publishes both exact j_bolt rigid pieces");

    DetachableMagazineWeaponProfile malformed = upgraded;
    malformed.charging.additional_pieces[0].material_name = "";
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "an additional charging piece requires an exact material identity");

    malformed = upgraded;
    malformed.charging.additional_pieces[0].source_surface_index = 6;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "an additional charging piece cannot escape the model surface table");

    malformed = upgraded;
    malformed.charging.additional_pieces[0].source_rigid_subrange_index = 3;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "an additional charging piece requires an in-range rigid owner");

    malformed = upgraded;
    malformed.charging.additional_pieces[0].vertex_offset = 200;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "an additional charging piece cannot escape its vertex span");

    malformed = upgraded;
    malformed.charging.additional_pieces[0] = primary;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "the primary charging range cannot be published twice");

    malformed = upgraded;
    malformed.charging.additional_pieces[0] =
        malformed.mesh.additional_pieces[0];
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "one rigid range cannot be both a detached magazine piece and a charging piece");

    malformed = kM1CarbineDetachableMagazineWeaponProfile;
    malformed.charging.additional_pieces[0].material_name =
        "mc/mtl_hidden_unaudited_bolt_piece";
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "unused charging-piece slots cannot carry hidden data");

    malformed = upgraded;
    malformed.charging.additional_piece_count = 2;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "a charging recipe cannot exceed its fixed action-piece budget");

    malformed = kM1CarbineDetachableMagazineWeaponProfile;
    malformed.charging.additional_piece_count = 1;
    malformed.charging.additional_pieces[0] = {
        .material_name = "mc/mtl_structural_test_detail",
        .source_surface_index = 3,
        .source_surface_rigid_subrange_count = 1,
        .source_rigid_subrange_index = 0,
        .source_surface_vertex_count = 1,
        .source_surface_triangle_count = 1,
        .vertex_offset = 0,
        .vertex_count = 1,
        .triangle_offset = 0,
        .triangle_count = 1,
    };
    expect(validate_detachable_magazine_weapon_profile(malformed) &&
               !validate_exact_m1carbine_profile(malformed),
           "the accepted original M1 lock rejects a structurally valid unaudited charging piece");
}

void test_validator_rejects_unsafe_interaction_calibration() {
    DetachableMagazineWeaponProfile malformed =
        kZombieColtDetachableMagazineWeaponProfile;
    malformed.held_pose_from_controller.translation.x =
        (std::numeric_limits<float>::quiet_NaN)();
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "a non-finite held-magazine offset is rejected");

    malformed = kZombieColtDetachableMagazineWeaponProfile;
    malformed.held_pose_from_controller.orientation =
        {0.0F, 0.0F, 0.0F, 0.5F};
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "a non-unit held-magazine orientation is rejected");

    malformed = kZombieColtDetachableMagazineWeaponProfile;
    malformed.insertion_guide_pose_from_anchor.orientation.x =
        (std::numeric_limits<float>::infinity)();
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "a non-finite insertion-guide orientation is rejected");

    malformed = kZombieColtDetachableMagazineWeaponProfile;
    malformed.insertion_radius_units = 0.0F;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "a non-positive insertion radius is rejected");

    malformed = kZombieColtDetachableMagazineWeaponProfile;
    malformed.insertion_radius_units = 65.0F;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "an unbounded insertion radius is rejected");
}

void test_validator_rejects_malformed_charging_recipe() {
    DetachableMagazineWeaponProfile malformed =
        kM1CarbineDetachableMagazineWeaponProfile;
    malformed.charging.handle_bone_index = 2;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "the charging handle cannot alias the magazine bone");

    malformed = kM1CarbineDetachableMagazineWeaponProfile;
    malformed.charging.source_surface_index = 4;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "a charging surface outside the audited M1 model is rejected");

    malformed = kM1CarbineDetachableMagazineWeaponProfile;
    malformed.charging.rigid_triangle_count = 317;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "a charging rigid range larger than its source surface is rejected");

    malformed = kM1CarbineDetachableMagazineWeaponProfile;
    malformed.charging.interaction.locked_open_offset_units =
        malformed.charging.interaction.travel_units;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "a locked-open position must leave finite rearward tug travel");

    malformed = kM1CarbineDetachableMagazineWeaponProfile;
    malformed.charging.interaction.spring_return_seconds = 0.0F;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "a non-positive charging-handle spring duration is rejected");

    malformed = kM1CarbineDetachableMagazineWeaponProfile;
    malformed.charging.interaction.return_samples[2] = 0.95F;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "a rising sample invalidates an otherwise bounded spring curve");

    malformed = kM1CarbineDetachableMagazineWeaponProfile;
    malformed.charging.interaction.return_samples[3] =
        (std::numeric_limits<float>::quiet_NaN)();
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "a non-finite sample invalidates a spring curve");

    malformed = kBarDetachableMagazineWeaponProfile;
    malformed.charging.interaction.return_samples[0] = 1.0F;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "an open-bolt latch rejects hidden spring-curve samples");

    malformed = kM1GarandDetachableMagazineWeaponProfile;
    malformed.charging.interaction.locked_open_offset_units = 2.384359F;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "an automatic en-bloc action must remain fully open before insertion");

    malformed = kM1GarandDetachableMagazineWeaponProfile;
    malformed.charging.interaction.trigger_engage = 0.65F;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "an automatic en-bloc action rejects an unused trigger gesture");

    malformed = kM1GarandDetachableMagazineWeaponProfile;
    malformed.charging.grab_radius_units = 10.0F;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "an automatic en-bloc action rejects an unused controller grab radius");

    malformed = kM1GarandDetachableMagazineWeaponProfile;
    malformed.charging.interaction.control_policy =
        static_cast<MagazineChargingControlPolicy>(255);
    expect(!validate_detachable_magazine_weapon_profile(malformed),
           "an unknown charging-control policy fails closed");

    malformed = kZombieColtDetachableMagazineWeaponProfile;
    malformed.charging.enabled = false;
    expect(!validate_detachable_magazine_weapon_profile(malformed),
            "a disabled charging recipe cannot carry unvalidated hidden data");
}

void test_registry_rejects_empty_null_duplicate_and_invalid_entries() {
    const std::span<const DetachableMagazineWeaponProfile* const> empty{};
    expect(!validate_detachable_magazine_weapon_profile_registry(empty),
           "an empty shipping registry is rejected");

    const std::array<const DetachableMagazineWeaponProfile*, 1> with_null{
        nullptr};
    expect(!validate_detachable_magazine_weapon_profile_registry(with_null),
           "a null registry entry is rejected");

    const std::array<const DetachableMagazineWeaponProfile*, 2>
        duplicate_pointer{
        &kZombieColtDetachableMagazineWeaponProfile,
        &kZombieColtDetachableMagazineWeaponProfile,
    };
    expect(!validate_detachable_magazine_weapon_profile_registry(
               duplicate_pointer),
           "a duplicate canonical profile pointer is rejected");

    DetachableMagazineWeaponProfile same_name_distinct_model =
        kZombieColtDetachableMagazineWeaponProfile;
    same_name_distinct_model.id = DetachableMagazineWeaponProfileId::Gewehr43;
    same_name_distinct_model.viewmodel_model_name =
        "viewmodel_usa_colt45_pistol_zombie_variant";
    const std::array<const DetachableMagazineWeaponProfile*, 2>
        valid_shared_weapon_name{
            &kZombieColtDetachableMagazineWeaponProfile,
            &same_name_distinct_model,
        };
    expect(validate_detachable_magazine_weapon_profile_registry(
               valid_shared_weapon_name),
           "a reused internal weapon name is valid when the exact viewmodel identity differs");

    DetachableMagazineWeaponProfile duplicate_identity_pair =
        kZombieColtDetachableMagazineWeaponProfile;
    duplicate_identity_pair.id = DetachableMagazineWeaponProfileId::Gewehr43;
    const std::array<const DetachableMagazineWeaponProfile*, 2>
        invalid_duplicate_identity_pair{
            &kZombieColtDetachableMagazineWeaponProfile,
            &duplicate_identity_pair,
        };
    expect(!validate_detachable_magazine_weapon_profile_registry(
               invalid_duplicate_identity_pair),
           "two profile ids cannot publish the same internal-name and viewmodel identity pair");

    DetachableMagazineWeaponProfile duplicate_id =
        kZombieColtDetachableMagazineWeaponProfile;
    duplicate_id.internal_weapon_name = "zombie_colt_variant";
    duplicate_id.viewmodel_model_name =
        "viewmodel_usa_colt45_pistol_zombie_variant";
    const std::array<const DetachableMagazineWeaponProfile*, 2>
        invalid_duplicate_id{
            &kZombieColtDetachableMagazineWeaponProfile,
            &duplicate_id,
        };
    expect(!validate_detachable_magazine_weapon_profile_registry(
               invalid_duplicate_id),
           "a profile id remains unique even when the composite asset identity differs");

    DetachableMagazineWeaponProfile malformed =
        kZombieColtDetachableMagazineWeaponProfile;
    malformed.mesh.triangle_count = 0;
    const std::array<const DetachableMagazineWeaponProfile*, 1> invalid{
        &malformed};
    expect(!validate_detachable_magazine_weapon_profile_registry(invalid),
           "a structurally invalid registry entry is rejected");
}

void test_map_local_binding_is_coherent_and_fail_closed() {
    const std::uint64_t packed = pack_detachable_magazine_weapon_binding(
        {DetachableMagazineWeaponProfileId::ZombieColt, 7});
    const auto binding = unpack_detachable_magazine_weapon_binding(packed);
    expect(binding.profile_id ==
                   DetachableMagazineWeaponProfileId::ZombieColt &&
               binding.weapon_index == 7 &&
               detachable_magazine_weapon_binding_matches(
                   packed,
                   DetachableMagazineWeaponProfileId::ZombieColt,
                   7),
           "the exact Colt profile binds coherently to its map-local index");
    expect(!detachable_magazine_weapon_binding_matches(
               packed,
               DetachableMagazineWeaponProfileId::ZombieColt,
               2) &&
               !detachable_magazine_weapon_binding_matches(
                   packed,
                   DetachableMagazineWeaponProfileId::None,
                   7),
           "a stale index and unsupported profile fail the binding gate");
    const std::uint64_t m1carbine_packed =
        pack_detachable_magazine_weapon_binding(
            {DetachableMagazineWeaponProfileId::M1Carbine, 16});
    const auto m1carbine_binding =
        unpack_detachable_magazine_weapon_binding(m1carbine_packed);
    expect(m1carbine_binding.profile_id ==
                   DetachableMagazineWeaponProfileId::M1Carbine &&
               m1carbine_binding.weapon_index == 16 &&
               detachable_magazine_weapon_binding_matches(
                   m1carbine_packed,
                   DetachableMagazineWeaponProfileId::M1Carbine,
                   16) &&
               !detachable_magazine_weapon_binding_matches(
                   m1carbine_packed,
                   DetachableMagazineWeaponProfileId::ZombieColt,
                   16),
           "the M1A1 profile binds independently to its map-local index");
    expect(pack_detachable_magazine_weapon_binding(
               {DetachableMagazineWeaponProfileId::ZombieColt, 0}) == 0 &&
               pack_detachable_magazine_weapon_binding(
                   {DetachableMagazineWeaponProfileId::ZombieColt, -1}) ==
                   0 &&
               pack_detachable_magazine_weapon_binding(
                   {DetachableMagazineWeaponProfileId::None, 7}) == 0 &&
               pack_detachable_magazine_weapon_binding(
                   {static_cast<DetachableMagazineWeaponProfileId>(255),
                    7}) == 0,
           "non-positive indices and unknown profiles publish the empty value");
}

}  // namespace

int main() {
    const auto& ptrs = kPtrs41DetachableMagazineWeaponProfile;
    expect(validate_detachable_magazine_weapon_profile(ptrs) &&
               find_detachable_magazine_weapon_profile_by_identity(
                   "ptrs41", "viewmodel_mp_ptrs41") == &ptrs &&
               find_detachable_magazine_weapon_profile_by_internal_name(
                   "ptrs41_zombie") == nullptr,
           "PTRS support requires its audited exact campaign identity");
    expect(detachable_magazine_mesh_piece_count(ptrs.mesh) == 2 &&
               detachable_magazine_mesh_piece(ptrs, 0).vertex_count == 108 &&
               detachable_magazine_mesh_piece(ptrs, 1).vertex_count == 395 &&
               detachable_magazine_mesh_piece(ptrs, 1).source_surface_index == 8 &&
               detachable_magazine_charging_mesh_piece_count(ptrs.charging) == 2 &&
               detachable_magazine_charging_mesh_piece(ptrs.charging, 1)
                   .source_surface_index == 7,
           "PTRS carries the clip metal and five cartridges, never the chamber round");
    expect(ptrs.hide_authored_feed_device_always &&
               std::string_view(ptrs.insertion_anchor_bone_tag_name) == "j_gun" &&
               ptrs.insertion_anchor_bone_index == 0 &&
               ptrs.mesh.bind_pose.translation.y > 60.0F &&
               ptrs.insertion_guide_pose_from_anchor.translation.y < 1.0F &&
               !ptrs.charging.suppress_native_pose_always,
           "PTRS insertion never follows the parked clip and preserves semiauto animation");
    auto malformed_ptrs = ptrs;
    malformed_ptrs.insertion_anchor_bone_index = 8;
    expect(!validate_detachable_magazine_weapon_profile(malformed_ptrs),
           "PTRS insertion anchor cannot escape the audited model skeleton");
    malformed_ptrs = ptrs;
    malformed_ptrs.insertion_anchor_bone_tag_name = nullptr;
    expect(!validate_detachable_magazine_weapon_profile(malformed_ptrs),
           "an explicit insertion index cannot silently fall back to parked j_clip");
    expect(ptrs.feed_door.enabled && ptrs.feed_door.bone_index == 3 &&
               std::string_view(ptrs.feed_door.bone_tag_name) == "j_clip_release" &&
               ptrs.feed_door.piece.source_surface_index == 6 &&
               ptrs.feed_door.piece.vertex_count == 146 &&
               ptrs.feed_door.piece.triangle_count == 172,
           "PTRS loading hatch has a separate exact rigid surface and hinge");
    expect(ptrs.charging.closed_translation_offset.x == 0.329079F &&
               ptrs.charging.interaction.travel_units == 9.090631F &&
               ptrs.charging.interaction.locked_open_offset_units == 9.089901F &&
               ptrs.charging.interaction.spring_return_seconds == 2.0F / 30.0F &&
               ptrs.insertion_guide_pose_from_anchor.translation.x == 7.703720F &&
               ptrs.feed_door.open_pose.orientation.y == -0.538377F,
           "PTRS calibrations come from native idle, empty idle and reload keyframes");
    malformed_ptrs = ptrs;
    malformed_ptrs.feed_door.piece.source_surface_index = 5;
    expect(!validate_detachable_magazine_weapon_profile(malformed_ptrs),
           "loading hatch cannot steal the held clip surface");
    malformed_ptrs = ptrs;
    malformed_ptrs.feed_door.piece.source_surface_index = 4;
    expect(!validate_detachable_magazine_weapon_profile(malformed_ptrs),
           "loading hatch cannot steal the charging handle surface");
    malformed_ptrs = ptrs;
    malformed_ptrs.feed_door.bone_index = 1;
    expect(!validate_detachable_magazine_weapon_profile(malformed_ptrs),
           "loading hatch must not move the charging handle bone");
    test_phase_one_registry_covers_each_profile_exactly_once();
    test_registry_contains_exact_zombie_colt_profile();
    test_zombie_colt_dedicated_profile_is_exact();
    test_m1carbine_profile_is_exact_and_bounded();
    test_m1garand_en_bloc_profile_is_exact_and_bounded();
    test_m1garand_bayonet_profile_is_exact_and_fail_closed();
    test_m1garand_grenade_launcher_profile_is_exact_and_fail_closed();
    test_zombie_m1carbine_profile_is_exact();
    test_zombie_gewehr43_profile_is_exact();
    test_zombie_gewehr43_upgraded_profile_is_exact();
    test_zombie_stg44_profile_is_exact();
    test_zombie_stg44_upgraded_profile_is_exact();
    test_zombie_thompson_profile_is_exact();
    test_zombie_thompson_upgraded_profile_is_exact();
    test_zombie_mp40_profile_is_exact();
    test_zombie_mp40_upgraded_candidate_profile_is_exact();
    test_zombie_type100_profile_is_exact();
    test_zombie_bar_profile_is_exact();
    test_zombie_fg42_profile_is_exact();
    test_zombie_ppsh_profile_is_exact();
    test_colt_submesh_recipe_is_exact_and_bounded();
    test_interaction_calibration_is_complete();
    test_bar_charging_surface_is_distinct_from_its_magazine_material();
    test_pistol_charging_profiles_use_the_left_hand();
    test_registered_names_do_not_require_a_unique_profile();
    test_lookups_require_exact_full_identity();
    test_validator_rejects_incomplete_identity_and_policy();
    test_exact_colt_validator_rejects_structurally_valid_drift();
    test_exact_m1carbine_validator_rejects_structurally_valid_drift();
    test_exact_m1garand_validator_rejects_structurally_valid_drift();
    test_exact_m1garand_bayonet_validator_rejects_drift();
    test_exact_m1garand_grenade_launcher_validator_rejects_drift();
    test_exact_zombie_colt_dedicated_validator_rejects_structurally_valid_drift();
    test_exact_zombie_m1carbine_validator_rejects_structurally_valid_drift();
    test_exact_zombie_gewehr43_validator_rejects_structurally_valid_drift();
    test_exact_zombie_gewehr43_upgraded_validator_rejects_structurally_valid_drift();
    test_exact_zombie_stg44_validator_rejects_structurally_valid_drift();
    test_exact_zombie_stg44_upgraded_validator_rejects_structurally_valid_drift();
    test_exact_zombie_thompson_validator_rejects_structurally_valid_drift();
    test_exact_zombie_thompson_upgraded_validator_rejects_structurally_valid_drift();
    test_exact_zombie_mp40_validator_rejects_structurally_valid_drift();
    test_exact_zombie_mp40_upgraded_candidate_validator_rejects_structurally_valid_drift();
    test_exact_zombie_type100_validator_rejects_structurally_valid_drift();
    test_exact_zombie_bar_validator_rejects_structurally_valid_drift();
    test_exact_zombie_fg42_validator_rejects_structurally_valid_drift();
    test_exact_zombie_ppsh_validator_rejects_structurally_valid_drift();
    test_validator_rejects_malformed_mesh_recipes();
    test_multi_piece_mesh_recipe_is_exact_and_fail_closed();
    test_multi_piece_charging_recipe_is_exact_and_fail_closed();
    test_validator_rejects_unsafe_interaction_calibration();
    test_validator_rejects_malformed_charging_recipe();
    test_registry_rejects_empty_null_duplicate_and_invalid_entries();
    test_map_local_binding_is_coherent_and_fail_closed();
    std::cout << "detachable magazine weapon profile tests passed\n";
    return EXIT_SUCCESS;
}
