#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace wawvr::t4 {

// Minimal SP command prefix independently established from the exact supported
// executable. Bytes whose meaning is not required by the VR input path remain
// opaque on purpose.
struct UsercmdSp final {
    std::int32_t server_time{};                         // +0x00
    std::uint32_t buttons{};                           // +0x04
    std::array<std::int32_t, 3> view_angles{};          // +0x08
    std::uint8_t weapon{};                             // +0x14
    std::uint8_t offhand_index{};                      // +0x15
    std::int8_t forward_move{};                        // +0x16
    std::int8_t right_move{};                          // +0x17
    std::array<std::byte, 4> opaque_motion_18_1b{};     // +0x18
    std::int16_t gun_pitch_short{};                    // +0x1C
    std::int16_t gun_yaw_short{};                      // +0x1E
    std::array<std::byte, 0x08> opaque_after_angles{};  // +0x20
    float melee_charge_yaw{};                          // +0x28
    std::uint8_t melee_charge_distance{};              // +0x2C
    std::array<std::byte, 0x0B> opaque_after_melee{};   // +0x2D
};

// Multiplayer retains the independently verified command prefix through the
// controller gun-angle shorts, but the complete command is only 0x2C bytes.
// The final twelve bytes are intentionally opaque: unlike SP, MP has no
// verified melee-charge yaw/distance fields and they must never be touched by
// the comfort patch.
struct UsercmdMp final {
    std::int32_t server_time{};                         // +0x00
    std::uint32_t buttons{};                           // +0x04
    std::array<std::int32_t, 3> view_angles{};          // +0x08
    std::uint8_t weapon{};                             // +0x14
    std::uint8_t offhand_index{};                      // +0x15
    std::int8_t forward_move{};                        // +0x16
    std::int8_t right_move{};                          // +0x17
    std::array<std::byte, 4> opaque_motion_18_1b{};     // +0x18
    std::int16_t gun_pitch_short{};                    // +0x1C
    std::int16_t gun_yaw_short{};                      // +0x1E
    std::array<std::byte, 0x0C> opaque_after_angles{};  // +0x20
};

static_assert(std::is_standard_layout_v<UsercmdSp>);
static_assert(std::is_trivially_copyable_v<UsercmdSp>);
static_assert(sizeof(UsercmdSp) == 0x38);
static_assert(offsetof(UsercmdSp, buttons) == 0x04);
static_assert(offsetof(UsercmdSp, view_angles) == 0x08);
static_assert(offsetof(UsercmdSp, weapon) == 0x14);
static_assert(offsetof(UsercmdSp, forward_move) == 0x16);
static_assert(offsetof(UsercmdSp, right_move) == 0x17);
static_assert(offsetof(UsercmdSp, gun_pitch_short) == 0x1C);
static_assert(offsetof(UsercmdSp, gun_yaw_short) == 0x1E);
static_assert(offsetof(UsercmdSp, melee_charge_yaw) == 0x28);
static_assert(offsetof(UsercmdSp, melee_charge_distance) == 0x2C);

static_assert(std::is_standard_layout_v<UsercmdMp>);
static_assert(std::is_trivially_copyable_v<UsercmdMp>);
static_assert(sizeof(UsercmdMp) == 0x2C);
static_assert(offsetof(UsercmdMp, buttons) == 0x04);
static_assert(offsetof(UsercmdMp, view_angles) == 0x08);
static_assert(offsetof(UsercmdMp, weapon) == 0x14);
static_assert(offsetof(UsercmdMp, forward_move) == 0x16);
static_assert(offsetof(UsercmdMp, right_move) == 0x17);
static_assert(offsetof(UsercmdMp, gun_pitch_short) == 0x1C);
static_assert(offsetof(UsercmdMp, gun_yaw_short) == 0x1E);

enum class UsercmdButton : std::uint32_t {
    attack = 0x00000001,
    sprint = 0x00000002,
    melee = 0x00000004,
    use = 0x00000008,
    reload = 0x00000010,
    use_reload = 0x00000020,
    lean_left = 0x00000040,
    lean_right = 0x00000080,
    prone = 0x00000100,
    crouch = 0x00000200,
    jump = 0x00000400,
    aim_down_sights = 0x00000800,
    hold_breath = 0x00002000,
    frag_grenade = 0x00004000,
    smoke_grenade = 0x00008000,
    // Verified retail SP +toggleads_throw / PM_GetWeaponFireButton. Not IW3.
    throw_remote_charge = 0x00400000,
};

[[nodiscard]] constexpr std::uint32_t button_mask(const UsercmdButton button) noexcept {
    return static_cast<std::uint32_t>(button);
}

constexpr void add_button(UsercmdSp& command, const UsercmdButton button) noexcept {
    command.buttons |= button_mask(button);
}

constexpr void add_button(UsercmdMp& command, const UsercmdButton button) noexcept {
    command.buttons |= button_mask(button);
}

[[nodiscard]] constexpr bool has_button(const UsercmdSp& command,
                                        const UsercmdButton button) noexcept {
    return (command.buttons & button_mask(button)) != 0;
}


[[nodiscard]] constexpr bool has_button(const UsercmdMp& command,
                                        const UsercmdButton button) noexcept {
    return (command.buttons & button_mask(button)) != 0;
}

// Matches T4's fixed-angle scale (65536 units per turn) and truncation toward
// zero while making non-finite inputs an explicit failure instead of invoking
// undefined float-to-integer behavior.
[[nodiscard]] std::optional<std::int16_t> encode_short_angle_degrees(
    float degrees) noexcept;

// Applies independent controller weapon aim to a command after CL_CreateCmd.
// Native keyboard/mouse state is preserved: trigger input adds ATTACK, and a
// released VR trigger does not erase an attack already produced by the engine.
// Returns false without mutating the command if either angle is non-finite.
[[nodiscard]] bool apply_vr_weapon_aim(UsercmdSp& command, float pitch_degrees,
                                      float yaw_degrees, bool trigger_held) noexcept;

[[nodiscard]] bool apply_vr_weapon_aim(UsercmdMp& command, float pitch_degrees,
                                      float yaw_degrees, bool trigger_held) noexcept;

}  // namespace wawvr::t4
