// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace wawvr::mod {

inline constexpr std::uint32_t kMaximumSerializedT4WeaponId = 255;
inline constexpr std::string_view kRocketBarrageWeaponName =
    "rocket_barrage";
inline constexpr std::string_view kAirSupportWeaponName = "air_support";
inline constexpr std::size_t kCampaignTargetingWeaponNameBytes =
    (kRocketBarrageWeaponName.size() > kAirSupportWeaponName.size()
         ? kRocketBarrageWeaponName.size()
         : kAirSupportWeaponName.size()) + 1U;

// Resolves only an exact supported name with a NUL terminator inside the
// bounded readable span. Bytes after the first NUL belong to other storage
// and do not participate in identity. The returned view borrows the input.
[[nodiscard]] std::string_view resolve_campaign_targeting_weapon_name(
    std::string_view terminated_bytes) noexcept;

// Fully resolved, platform-neutral inputs for the narrow campaign targeting
// substitution. The runtime hook is responsible for proving that any game
// memory used to populate this structure is readable before calling here.
struct RocketBarrageAngleGate final {
    bool hook_enabled{};
    bool single_player{};
    std::uintptr_t entity{};
    std::uintptr_t local_player_entity{};
    bool client_readable{};
    std::uint32_t primary_weapon_id{};
    std::uint32_t fallback_weapon_id{};
    std::uint32_t weapon_count{};
    bool weapon_definition_readable{};
    std::string_view weapon_name{};
    bool controller_focused{};
    bool controller_frame_current{};
    float controller_pitch_degrees{};
    float controller_yaw_degrees{};
};

struct RocketBarrageAngleSelection final {
    bool substitute{};
    std::uint32_t effective_weapon_id{};
    std::array<float, 3> angles{};
};

// Selects controller pitch/yaw only for the exact local SP rocket-barrage or
// air-support script path. A nonzero primary weapon ID takes precedence; fallback
// is consulted only when primary is zero. On rejection the result is entirely
// zero-initialized, so the hook must preserve its native angle pointer/value.
[[nodiscard]] RocketBarrageAngleSelection select_rocket_barrage_angles(
    const RocketBarrageAngleGate& gate) noexcept;

}  // namespace wawvr::mod
