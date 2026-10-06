// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <string_view>

namespace wawvr::mod {

// Returns false for every well-formed SP weapon identity. Once both physical
// grips own a weapon, the controller pose is authoritative and T4's authored
// ADS animation must not translate or rotate the rifle underneath that pose.
// An unavailable or malformed identity preserves native behavior. This policy
// does not alter grip ownership, the support-pose latch, controller-driven ADS
// spread, scopes, or manual weapon actions.
[[nodiscard]] bool native_support_animation_allowed_for_weapon_name(
    std::string_view internal_weapon_name) noexcept;

}  // namespace wawvr::mod
