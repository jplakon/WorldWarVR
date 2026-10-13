// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <string_view>

namespace wawvr::mod {

struct NativeGameplayCommandFrame final {
    bool input_owned{};
    bool single_player_profile{};
    bool weapon_next_held{};
    bool mission_stick_valid{};
    float mission_stick_x{};
    float mission_stick_y{};
};

struct NativeGameplayCommandState final {
    bool input_owned{};
    bool weapon_next_was_held{};
    bool weapon_next_consumed_by_mission_chord{};
    bool mission_dpad_armed{};
};

struct NativeGameplayCommandUpdate final {
    bool weapon_next_tap{};
    std::int32_t mission_key_tap{};
};

struct PezBotAutofillFrame final {
    bool enabled{};
    bool multiplayer_profile{};
    bool presentation_state_valid{};
    std::int32_t connection_state{};
    std::int32_t active_connection_state{10};
};

struct PezBotAutofillState final {
    bool disconnected_frontend_latched{};
    bool active_server_observed{};
    bool active_departure_latched{};
};

inline constexpr float kMissionDpadEngageThreshold = 0.75F;
inline constexpr float kMissionDpadReleaseThreshold = 0.35F;
inline constexpr float kMissionDpadDominanceMargin = 0.12F;

inline constexpr std::int32_t kMissionDpadUpKey = '5';
inline constexpr std::int32_t kMissionDpadDownKey = 'n';
inline constexpr std::int32_t kMissionDpadLeftKey = '6';
inline constexpr std::int32_t kMissionDpadRightKey = '7';

// Converts controller state into one-shot high-level engine commands. A held
// button or deflected modifier stick is baselined whenever gameplay gains
// ownership so closing a menu or regaining OpenXR focus cannot leak that input
// into an unintended command. In an exact SP executable profile, the mission
// D-pad maps deliberately held weapon-next (Y/B) plus the left stick to keys
// 5/N/6/7; left is WaW's native key 6 rocket-barrage selector in Little
// Resistance. Passive capacitive touch is intentionally excluded. A plain
// weapon-next tap is emitted on release. A mission chord arms only after Y/B
// is held with the stick centred; its emitted direction consumes that release
// so it cannot also cycle the weapon. Pressing Y/B while already walking stays
// an ordinary weapon swap. MP can never enter the mission path.
[[nodiscard]] NativeGameplayCommandUpdate update_native_gameplay_commands(
    const NativeGameplayCommandFrame& frame,
    NativeGameplayCommandState* state) noexcept;

// PeZBOT consumes `svr_pezbots` once and immediately resets it to zero. Queue
// one refill either in the fully disconnected MP frontend or on the first
// post-Com_Frame observation that a previously active server has entered its
// next non-active load. The assignment is idempotent with the launcher's
// first-map value and with PeZBOT's team-count restoration during automatic
// rotation. Invalid telemetry keeps every latch unchanged; a transient read
// failure must not duplicate commands.
[[nodiscard]] bool should_queue_pezbot_autofill(
    const PezBotAutofillFrame& frame,
    PezBotAutofillState* state) noexcept;

// The launcher-only authorization marker must be a complete command-line token
// sequence. A longer value such as `90`, or marker text embedded in another
// token/path, must fail closed.
[[nodiscard]] bool command_line_has_exact_marker(
    std::wstring_view command_line,
    std::wstring_view marker) noexcept;

}  // namespace wawvr::mod
