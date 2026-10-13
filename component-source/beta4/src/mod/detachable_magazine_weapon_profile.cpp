// SPDX-License-Identifier: GPL-3.0-only
#include "detachable_magazine_weapon_profile.hpp"

#include <array>

namespace wawvr::mod {
namespace {

constexpr std::array<const DetachableMagazineWeaponProfile*, 45> kProfiles{
    &kZombieColtDetachableMagazineWeaponProfile,
    &kM1CarbineDetachableMagazineWeaponProfile,
    &kM1GarandDetachableMagazineWeaponProfile,
    &kM1GarandBayonetDetachableMagazineWeaponProfile,
    &kM1GarandGrenadeLauncherDetachableMagazineWeaponProfile,
    &kPtrs41DetachableMagazineWeaponProfile,
    &kGewehr43DetachableMagazineWeaponProfile,
    &kStg44DetachableMagazineWeaponProfile,
    &kMp40DetachableMagazineWeaponProfile,
    &kThompsonDetachableMagazineWeaponProfile,
    &kBarDetachableMagazineWeaponProfile,
    &kFg42BipodDetachableMagazineWeaponProfile,
    &kWaltherDetachableMagazineWeaponProfile,
    &kColtDetachableMagazineWeaponProfile,
    &kTokarevDetachableMagazineWeaponProfile,
    &kNambuDetachableMagazineWeaponProfile,
    &kSvt40DetachableMagazineWeaponProfile,
    &kPpshDetachableMagazineWeaponProfile,
    &kType100DetachableMagazineWeaponProfile,
    &kType99LmgDetachableMagazineWeaponProfile,
    &kType99LmgBipodDetachableMagazineWeaponProfile,
    &kType100NoSoundDetachableMagazineWeaponProfile,
    &kThompsonWetDetachableMagazineWeaponProfile,
    &kColtWetDetachableMagazineWeaponProfile,
    &kBarBipodDetachableMagazineWeaponProfile,
    &kZombieColtDedicatedDetachableMagazineWeaponProfile,
    &kZombieColtUpgradedDetachableMagazineWeaponProfile,
    &kZombieM1CarbineDetachableMagazineWeaponProfile,
    &kZombieM1CarbineUpgradedDetachableMagazineWeaponProfile,
    &kZombieGewehr43DetachableMagazineWeaponProfile,
    &kZombieGewehr43UpgradedDetachableMagazineWeaponProfile,
    &kZombieStg44DetachableMagazineWeaponProfile,
    &kZombieStg44UpgradedDetachableMagazineWeaponProfile,
    &kZombieThompsonDetachableMagazineWeaponProfile,
    &kZombieThompsonUpgradedDetachableMagazineWeaponProfile,
    &kZombieMp40DetachableMagazineWeaponProfile,
    &kZombieMp40UpgradedDetachableMagazineWeaponProfile,
    &kZombieType100DetachableMagazineWeaponProfile,
    &kZombieType100UpgradedDetachableMagazineWeaponProfile,
    &kZombieBarDetachableMagazineWeaponProfile,
    &kZombieBarUpgradedDetachableMagazineWeaponProfile,
    &kZombieFg42DetachableMagazineWeaponProfile,
    &kZombieFg42UpgradedDetachableMagazineWeaponProfile,
    &kZombiePpshDetachableMagazineWeaponProfile,
    &kZombiePpshUpgradedDetachableMagazineWeaponProfile,
};

[[nodiscard]] constexpr std::string_view profile_string(
    const char* const value) noexcept {
    return value == nullptr ? std::string_view{} : std::string_view{value};
}

}  // namespace

std::span<const DetachableMagazineWeaponProfile* const>
detachable_magazine_weapon_profiles() noexcept {
    return kProfiles;
}

const DetachableMagazineWeaponProfile*
find_detachable_magazine_weapon_profile(
    const DetachableMagazineWeaponProfileId id) noexcept {
    if (!is_known_detachable_magazine_weapon_profile_id(id)) {
        return nullptr;
    }
    for (const DetachableMagazineWeaponProfile* const profile : kProfiles) {
        if (profile != nullptr && profile->id == id) {
            return profile;
        }
    }
    return nullptr;
}

bool has_detachable_magazine_weapon_profile_for_internal_name(
    const std::string_view internal_weapon_name) noexcept {
    if (internal_weapon_name.empty()) {
        return false;
    }
    for (const DetachableMagazineWeaponProfile* const profile : kProfiles) {
        if (profile != nullptr &&
            profile_string(profile->internal_weapon_name) ==
                internal_weapon_name) {
            return true;
        }
    }
    return false;
}

const DetachableMagazineWeaponProfile*
find_detachable_magazine_weapon_profile_by_internal_name(
    const std::string_view internal_weapon_name) noexcept {
    if (internal_weapon_name.empty()) {
        return nullptr;
    }
    const DetachableMagazineWeaponProfile* match = nullptr;
    for (const DetachableMagazineWeaponProfile* const profile : kProfiles) {
        if (profile != nullptr &&
            profile_string(profile->internal_weapon_name) ==
                internal_weapon_name) {
            if (match != nullptr) {
                return nullptr;
            }
            match = profile;
        }
    }
    return match;
}

const DetachableMagazineWeaponProfile*
find_detachable_magazine_weapon_profile_by_identity(
    const std::string_view internal_weapon_name,
    const std::string_view viewmodel_model_name) noexcept {
    if (internal_weapon_name.empty() || viewmodel_model_name.empty()) {
        return nullptr;
    }
    for (const DetachableMagazineWeaponProfile* const profile : kProfiles) {
        if (profile != nullptr &&
            profile_string(profile->internal_weapon_name) ==
                internal_weapon_name &&
            profile_string(profile->viewmodel_model_name) ==
                viewmodel_model_name) {
            return profile;
        }
    }
    return nullptr;
}

const DetachableMagazineWeaponProfile*
find_detachable_magazine_weapon_profile_by_viewmodel_model_name(
    const std::string_view viewmodel_model_name) noexcept {
    if (viewmodel_model_name.empty()) {
        return nullptr;
    }
    const DetachableMagazineWeaponProfile* match = nullptr;
    for (const DetachableMagazineWeaponProfile* const profile : kProfiles) {
        if (profile != nullptr &&
            profile_string(profile->viewmodel_model_name) ==
                viewmodel_model_name) {
            if (match != nullptr) {
                // Shared viewmodels are legal, but model-only lookup is not
                // allowed to guess between exact WeaponDef identities.
                return nullptr;
            }
            match = profile;
        }
    }
    return match;
}

bool validate_detachable_magazine_weapon_profile_registry(
    const std::span<const DetachableMagazineWeaponProfile* const>
        profiles) noexcept {
    if (profiles.empty()) {
        return false;
    }
    for (std::size_t index = 0; index < profiles.size(); ++index) {
        const DetachableMagazineWeaponProfile* const profile =
            profiles[index];
        if (profile == nullptr ||
            !validate_detachable_magazine_weapon_profile(*profile)) {
            return false;
        }
        for (std::size_t peer_index = index + 1;
             peer_index < profiles.size(); ++peer_index) {
            const DetachableMagazineWeaponProfile* const peer =
                profiles[peer_index];
            if (peer == nullptr || profile->id == peer->id ||
                (profile_string(profile->internal_weapon_name) ==
                     profile_string(peer->internal_weapon_name) &&
                 profile_string(profile->viewmodel_model_name) ==
                     profile_string(peer->viewmodel_model_name))) {
                return false;
            }
        }
    }
    return true;
}

bool validate_detachable_magazine_weapon_profile_registry() noexcept {
    return validate_detachable_magazine_weapon_profile_registry(kProfiles);
}

}  // namespace wawvr::mod
