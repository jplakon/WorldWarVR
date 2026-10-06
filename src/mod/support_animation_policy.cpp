// SPDX-License-Identifier: GPL-3.0-only
#include "support_animation_policy.hpp"

namespace wawvr::mod {

bool native_support_animation_allowed_for_weapon_name(
    const std::string_view internal_weapon_name) noexcept {
    if (internal_weapon_name.empty()) {
        return true;
    }

    // Runtime WeaponDef names use a bounded alphanumeric/underscore identity.
    // Fail safe to native behavior for a malformed diagnostic/test value rather
    // than applying controller-only animation policy to an uncertain weapon.
    for (const char value : internal_weapon_name) {
        const bool ascii_letter =
            (value >= 'a' && value <= 'z') ||
            (value >= 'A' && value <= 'Z');
        const bool ascii_digit = value >= '0' && value <= '9';
        if (!ascii_letter && !ascii_digit && value != '_') {
            return true;
        }
    }

    return false;
}

}  // namespace wawvr::mod
