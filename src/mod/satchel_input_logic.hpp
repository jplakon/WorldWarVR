// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <string_view>

namespace wawvr::mod {

// Retail T4's selected-satchel placement button. This is not IW3's secondary
// fire bit. Native attack (0x1) remains the independently mapped detonator.
inline constexpr std::uint32_t kSatchelNativeThrowButton = 0x00400000U;

enum class SatchelWeaponKind : std::uint8_t {
    none,
    satchel_charge,
    satchel_charge_new,
};

struct SatchelInput final {
    bool gameplay_session_inactive{};
    // Set only after validating the selected SP weapon index, definition and
    // exact internal name. An unavailable identity must never inherit a hold.
    bool weapon_context_valid{};
    bool multiplayer{};
    std::uint32_t weapon_index{};
    std::uintptr_t weapon_definition_identity{};
    std::string_view weapon_name{};
    // Includes gameplay focus and a fresh, valid left-trigger action sample.
    // This is independent of weapon identity so a temporary focus/input gap
    // can preserve an already-owned native hold for the same selected satchel.
    bool input_valid{};
    bool right_grip_held{};
    // Existing manual grenade/reload ownership blocks only a new throw hold.
    bool new_press_blocked{};
    float left_trigger_value{};
};

struct SatchelInputState final {
    std::uint32_t weapon_index{};
    std::uintptr_t weapon_definition_identity{};
    SatchelWeaponKind weapon_kind{SatchelWeaponKind::none};
    bool trigger_held{};
    bool new_press_rearm_required{};
};

struct SatchelInputResult final {
    bool hold_native_throw{};
    // The selected satchel owns new left-trigger actions, even before the
    // right grip is held. Use this to block NEW belt/ADS interactions, never
    // to cancel an already-owned manual grenade or reload interaction.
    bool reserve_left_trigger{};
};

[[nodiscard]] SatchelWeaponKind satchel_weapon_kind(
    bool multiplayer, std::string_view weapon_name) noexcept;

// Selection, changed identity and focus recovery require a released trigger
// before a new press. A valid physical release always ends an existing hold,
// including after the right grip is released or another action reserves the
// hand. Unknown identity, another weapon, MP and positive inactivity reset
// without forwarding a satchel bit into an unrelated weapon context.
[[nodiscard]] SatchelInputResult update_satchel_input(
    const SatchelInput& input, SatchelInputState* state) noexcept;

// Adds only the controller-owned satchel placement bit. Never clears native
// keyboard buttons, changes native attack/detonation, or remaps offhand bits.
[[nodiscard]] std::uint32_t merge_satchel_native_throw(
    std::uint32_t native_buttons, bool hold_native_throw) noexcept;

}  // namespace wawvr::mod
