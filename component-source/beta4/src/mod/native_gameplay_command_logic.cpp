// SPDX-License-Identifier: GPL-3.0-only
#include "native_gameplay_command_logic.hpp"

#include <cmath>
#include <cstddef>

namespace wawvr::mod {

NativeGameplayCommandUpdate update_native_gameplay_commands(
    const NativeGameplayCommandFrame& frame,
    NativeGameplayCommandState* const state) noexcept {
    NativeGameplayCommandUpdate update{};
    if (state == nullptr) {
        return update;
    }

    if (!frame.input_owned) {
        state->input_owned = false;
        state->weapon_next_was_held = frame.weapon_next_held;
        state->weapon_next_consumed_by_mission_chord =
            frame.weapon_next_held;
        state->mission_dpad_armed = false;
        return update;
    }

    if (!state->input_owned) {
        state->input_owned = true;
        state->weapon_next_was_held = frame.weapon_next_held;
        state->weapon_next_consumed_by_mission_chord =
            frame.weapon_next_held;
        state->mission_dpad_armed = false;
        return update;
    }

    const bool weapon_next_released =
        !frame.weapon_next_held && state->weapon_next_was_held;
    update.weapon_next_tap = weapon_next_released &&
        !state->weapon_next_consumed_by_mission_chord;
    if (weapon_next_released) {
        state->weapon_next_consumed_by_mission_chord = false;
    }
    state->weapon_next_was_held = frame.weapon_next_held;

    const bool stick_valid = frame.mission_stick_valid &&
        std::isfinite(frame.mission_stick_x) &&
        std::isfinite(frame.mission_stick_y);
    const float absolute_x =
        stick_valid ? std::abs(frame.mission_stick_x) : 0.0F;
    const float absolute_y =
        stick_valid ? std::abs(frame.mission_stick_y) : 0.0F;
    if (!frame.single_player_profile) {
        state->mission_dpad_armed = false;
        return update;
    }

    const bool weapon_next_modifier = frame.weapon_next_held;
    if (!weapon_next_modifier || !stick_valid) {
        state->mission_dpad_armed = false;
        return update;
    }
    if (absolute_x <= kMissionDpadReleaseThreshold &&
        absolute_y <= kMissionDpadReleaseThreshold) {
        state->mission_dpad_armed = true;
        return update;
    }
    if (!state->mission_dpad_armed) {
        return update;
    }

    std::int32_t selected_key = 0;
    if (absolute_y >= kMissionDpadEngageThreshold &&
        absolute_y >= absolute_x + kMissionDpadDominanceMargin) {
        selected_key = frame.mission_stick_y > 0.0F
            ? kMissionDpadUpKey
            : kMissionDpadDownKey;
    } else if (absolute_x >= kMissionDpadEngageThreshold &&
               absolute_x >= absolute_y + kMissionDpadDominanceMargin) {
        selected_key = frame.mission_stick_x < 0.0F
            ? kMissionDpadLeftKey
            : kMissionDpadRightKey;
    }
    if (selected_key != 0) {
        // Only an explicitly armed, emitted direction consumes Y/B. Pressing
        // weapon-next while already walking must remain a normal weapon swap.
        state->weapon_next_consumed_by_mission_chord = true;
        update.mission_key_tap = selected_key;
        state->mission_dpad_armed = false;
    }
    return update;
}

bool should_queue_pezbot_autofill(
    const PezBotAutofillFrame& frame,
    PezBotAutofillState* const state) noexcept {
    if (state == nullptr) {
        return false;
    }
    if (!frame.enabled || !frame.multiplayer_profile) {
        *state = {};
        return false;
    }
    if (!frame.presentation_state_valid ||
        frame.connection_state < 0 ||
        frame.active_connection_state <= 0) {
        return false;
    }

    if (frame.connection_state == frame.active_connection_state) {
        state->disconnected_frontend_latched = false;
        state->active_server_observed = true;
        state->active_departure_latched = false;
        return false;
    }

    if (frame.connection_state == 0) {
        state->active_server_observed = false;
        state->active_departure_latched = false;
        if (state->disconnected_frontend_latched) {
            return false;
        }
        state->disconnected_frontend_latched = true;
        return true;
    }

    // This service runs after Com_Frame. Once the client has left CA_ACTIVE,
    // the old server's frame and shutdown work have completed; the queued dvar
    // assignment executes on the next frame, before the incoming map's
    // PeZBOT StartNormal poll. Latch across 6 -> 7 -> 6 -> 8 so one map load
    // cannot receive more than one assignment.
    state->disconnected_frontend_latched = false;
    if (state->active_server_observed &&
        !state->active_departure_latched) {
        state->active_server_observed = false;
        state->active_departure_latched = true;
        return true;
    }
    return false;
}

bool command_line_has_exact_marker(
    const std::wstring_view command_line,
    const std::wstring_view marker) noexcept {
    if (command_line.empty() || marker.empty()) {
        return false;
    }

    const auto is_separator = [](const wchar_t value) noexcept {
        return value == L' ' || value == L'\t' ||
            value == L'\r' || value == L'\n';
    };
    std::size_t search_from = 0;
    while (search_from < command_line.size()) {
        const std::size_t position = command_line.find(marker, search_from);
        if (position == std::wstring_view::npos) {
            return false;
        }
        const std::size_t after = position + marker.size();
        const bool begins_at_boundary =
            position == 0 || is_separator(command_line[position - 1]);
        const bool ends_at_boundary =
            after == command_line.size() ||
            is_separator(command_line[after]);
        if (begins_at_boundary && ends_at_boundary) {
            return true;
        }
        search_from = position + 1;
    }
    return false;
}

}  // namespace wawvr::mod
