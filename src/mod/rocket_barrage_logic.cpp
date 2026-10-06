// SPDX-License-Identifier: GPL-3.0-only
#include "rocket_barrage_logic.hpp"

#include <cmath>

namespace wawvr::mod {

namespace {

[[nodiscard]] bool campaign_targeting_weapon_name(
    const std::string_view weapon_name) noexcept {
    return weapon_name == kRocketBarrageWeaponName ||
           weapon_name == kAirSupportWeaponName;
}

}  // namespace

std::string_view resolve_campaign_targeting_weapon_name(
    const std::string_view terminated_bytes) noexcept {
    const auto bounded_bytes =
        terminated_bytes.substr(0, kCampaignTargetingWeaponNameBytes);
    const auto terminator = bounded_bytes.find('\0');
    if (terminator == std::string_view::npos) {
        return {};
    }
    const auto weapon_name = bounded_bytes.substr(0, terminator);
    return campaign_targeting_weapon_name(weapon_name)
        ? weapon_name
        : std::string_view{};
}

RocketBarrageAngleSelection select_rocket_barrage_angles(
    const RocketBarrageAngleGate& gate) noexcept {
    const std::uint32_t effective_weapon_id =
        gate.primary_weapon_id != 0 ? gate.primary_weapon_id
                                    : gate.fallback_weapon_id;

    if (!gate.hook_enabled || !gate.single_player || gate.entity == 0 ||
        gate.local_player_entity == 0 ||
        gate.entity != gate.local_player_entity || !gate.client_readable ||
        gate.weapon_count == 0 ||
        gate.weapon_count > kMaximumSerializedT4WeaponId ||
        effective_weapon_id == 0 ||
        effective_weapon_id > kMaximumSerializedT4WeaponId ||
        effective_weapon_id > gate.weapon_count ||
        !gate.weapon_definition_readable ||
        !campaign_targeting_weapon_name(gate.weapon_name) ||
        !gate.controller_focused || !gate.controller_frame_current ||
        !std::isfinite(gate.controller_pitch_degrees) ||
        !std::isfinite(gate.controller_yaw_degrees)) {
        return {};
    }

    return {
        true,
        effective_weapon_id,
        {
            gate.controller_pitch_degrees,
            gate.controller_yaw_degrees,
            0.0F,
        },
    };
}

}  // namespace wawvr::mod
