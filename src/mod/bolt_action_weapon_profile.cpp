// SPDX-License-Identifier: GPL-3.0-only
#include "bolt_action_weapon_profile.hpp"

#include <array>

namespace wawvr::mod {
namespace {

constexpr std::array<const BoltActionWeaponProfile*, 8> kProfiles{
    &kKar98BoltActionWeaponProfile,
    &kKar98ScopedZombieBoltActionWeaponProfile,
    &kSpringfieldBoltActionWeaponProfile,
    &kMosinBoltActionWeaponProfile,
    &kMosinScopedBoltActionWeaponProfile,
    &kType99BoltActionWeaponProfile,
    &kType99BayonetBoltActionWeaponProfile,
    &kType99ScopedBoltActionWeaponProfile,
};

[[nodiscard]] constexpr std::string_view profile_string(
    const char* const value) noexcept {
    return value == nullptr ? std::string_view{} : std::string_view{value};
}

}  // namespace

std::span<const BoltActionWeaponProfile* const>
bolt_action_weapon_profiles() noexcept {
    return kProfiles;
}

const BoltActionWeaponProfile* find_bolt_action_weapon_profile(
    const BoltActionWeaponProfileId id) noexcept {
    if (id == BoltActionWeaponProfileId::None) {
        return nullptr;
    }
    for (const BoltActionWeaponProfile* const profile : kProfiles) {
        if (profile != nullptr && profile->id == id) {
            return profile;
        }
    }
    return nullptr;
}

const BoltActionWeaponProfile*
find_bolt_action_weapon_profile_by_internal_name(
    const std::string_view internal_weapon_name) noexcept {
    if (internal_weapon_name.empty()) {
        return nullptr;
    }
    for (const BoltActionWeaponProfile* const profile : kProfiles) {
        if (profile != nullptr &&
            profile_string(profile->internal_weapon_name) ==
                internal_weapon_name) {
            return profile;
        }
    }
    return nullptr;
}

const BoltActionWeaponProfile*
find_bolt_action_weapon_profile_by_viewmodel_model_name(
    const std::string_view viewmodel_model_name) noexcept {
    if (viewmodel_model_name.empty()) {
        return nullptr;
    }
    for (const BoltActionWeaponProfile* const profile : kProfiles) {
        if (profile != nullptr &&
            profile_string(profile->viewmodel_model_name) ==
                viewmodel_model_name) {
            return profile;
        }
    }
    return nullptr;
}

bool validate_bolt_action_weapon_profile_registry() noexcept {
    for (std::size_t index = 0; index < kProfiles.size(); ++index) {
        const BoltActionWeaponProfile* const profile = kProfiles[index];
        if (profile == nullptr ||
            !validate_bolt_action_weapon_profile(*profile)) {
            return false;
        }
        for (std::size_t peer_index = index + 1;
             peer_index < kProfiles.size(); ++peer_index) {
            const BoltActionWeaponProfile* const peer =
                kProfiles[peer_index];
            if (peer == nullptr || profile->id == peer->id ||
                profile_string(profile->internal_weapon_name) ==
                    profile_string(peer->internal_weapon_name) ||
                profile_string(profile->viewmodel_model_name) ==
                    profile_string(peer->viewmodel_model_name)) {
                return false;
            }
        }
    }
    return !kProfiles.empty();
}

}  // namespace wawvr::mod
