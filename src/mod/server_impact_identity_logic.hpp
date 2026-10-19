// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <limits>

namespace wawvr::mod {
// Never label a discharge merely from the currently selected player weapon.
// The native WeaponParms definition must resolve to that same exact registered
// definition. A different render/selected/old definition is not a fallback.
[[nodiscard]] constexpr bool server_impact_definition_identity_matches(
    std::uintptr_t native_definition,std::uint32_t resolved_definition,
    std::int32_t resolved_weapon_index) noexcept {
    return native_definition!=0 &&
        native_definition<=(std::numeric_limits<std::uint32_t>::max)() &&
        native_definition==resolved_definition && resolved_weapon_index>0;
}
} // namespace wawvr::mod
