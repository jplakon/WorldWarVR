// SPDX-License-Identifier: GPL-3.0-only
#include "two_hand_sight_alignment_policy.hpp"

#include <array>

namespace wawvr::mod {

bool controller_owned_two_hand_sight_axis_for_weapon_name(
    const std::string_view internal_weapon_name) noexcept {
    constexpr std::array<std::string_view, 5>
        kControllerOwnedSightIdentities{
            "m1carbine",
            "m1carbine_bayonet",
            "zombie_m1carbine",
            "zombie_m1carbine_upgraded",
            "kar98k",
        };

    for (const std::string_view identity :
         kControllerOwnedSightIdentities) {
        if (internal_weapon_name == identity) {
            return true;
        }
    }
    return false;
}

}  // namespace wawvr::mod
