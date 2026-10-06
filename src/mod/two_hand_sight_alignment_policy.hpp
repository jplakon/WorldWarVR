// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <string_view>

namespace wawvr::mod {

// Returns true only for exact weapon identities whose controller-owned
// two-hand pose must put the rifle bore on the constrained controller axis.
// Grip ownership and one-hand placement remain separate policies.
[[nodiscard]] bool controller_owned_two_hand_sight_axis_for_weapon_name(
    std::string_view internal_weapon_name) noexcept;

}  // namespace wawvr::mod
