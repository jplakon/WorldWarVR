// SPDX-License-Identifier: GPL-3.0-only

#include "native_gameplay_command_logic.hpp"

#include <limits>
#include <iostream>

namespace {

int failures = 0;

void expect(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_weapon_next_is_one_release_per_press() {
    wawvr::mod::NativeGameplayCommandState state{};
    auto update = wawvr::mod::update_native_gameplay_commands(
        {.input_owned = true, .weapon_next_held = false}, &state);
    expect(!update.weapon_next_tap,
           "initial neutral gameplay frame only establishes ownership");

    update = wawvr::mod::update_native_gameplay_commands(
        {.input_owned = true, .weapon_next_held = true}, &state);
    expect(!update.weapon_next_tap,
           "fresh Y press waits for a possible mission chord");
    update = wawvr::mod::update_native_gameplay_commands(
        {.input_owned = true, .weapon_next_held = true}, &state);
    expect(!update.weapon_next_tap,
           "held Y cannot cycle weapons every frame");
    update = wawvr::mod::update_native_gameplay_commands(
        {.input_owned = true, .weapon_next_held = false}, &state);
    expect(update.weapon_next_tap,
           "plain Y release emits one weapon-next command");
    update = wawvr::mod::update_native_gameplay_commands(
        {.input_owned = true, .weapon_next_held = true}, &state);
    expect(!update.weapon_next_tap,
           "a later fresh press again waits for release");
    update = wawvr::mod::update_native_gameplay_commands(
        {.input_owned = true, .weapon_next_held = false}, &state);
    expect(update.weapon_next_tap,
           "a second plain release emits exactly one later command");
}

void test_held_button_is_suppressed_when_gameplay_gains_ownership() {
    wawvr::mod::NativeGameplayCommandState state{};
    auto update = wawvr::mod::update_native_gameplay_commands(
        {.input_owned = false, .weapon_next_held = true}, &state);
    expect(!update.weapon_next_tap,
           "Y is inert while UI or another key catcher owns input");
    update = wawvr::mod::update_native_gameplay_commands(
        {.input_owned = true, .weapon_next_held = true}, &state);
    expect(!update.weapon_next_tap,
           "closing UI while Y remains held cannot switch weapons");
    static_cast<void>(wawvr::mod::update_native_gameplay_commands(
        {.input_owned = true, .weapon_next_held = false}, &state));
    update = wawvr::mod::update_native_gameplay_commands(
        {.input_owned = true, .weapon_next_held = true}, &state);
    expect(!update.weapon_next_tap,
           "fresh Y press after UI closes waits for release");
    update = wawvr::mod::update_native_gameplay_commands(
        {.input_owned = true, .weapon_next_held = false}, &state);
    expect(update.weapon_next_tap,
           "fresh Y release after UI closes switches once");
}

void test_focus_loss_requires_a_fresh_press() {
    wawvr::mod::NativeGameplayCommandState state{};
    static_cast<void>(wawvr::mod::update_native_gameplay_commands(
        {.input_owned = true, .weapon_next_held = false}, &state));
    static_cast<void>(wawvr::mod::update_native_gameplay_commands(
        {.input_owned = false, .weapon_next_held = false}, &state));
    auto update = wawvr::mod::update_native_gameplay_commands(
        {.input_owned = true, .weapon_next_held = true}, &state);
    expect(!update.weapon_next_tap,
           "focus regain while Y is held baselines instead of firing");
    static_cast<void>(wawvr::mod::update_native_gameplay_commands(
        {.input_owned = true, .weapon_next_held = false}, &state));
    update = wawvr::mod::update_native_gameplay_commands(
        {.input_owned = true, .weapon_next_held = true}, &state);
    expect(!update.weapon_next_tap,
           "focus-regained fresh Y press waits for release");
    update = wawvr::mod::update_native_gameplay_commands(
        {.input_owned = true, .weapon_next_held = false}, &state);
    expect(update.weapon_next_tap,
           "focus-regained Y works after a fresh press and release");
    expect(!wawvr::mod::update_native_gameplay_commands(
                {.input_owned = true, .weapon_next_held = true}, nullptr)
                .weapon_next_tap,
           "null command state fails closed");
}

void test_campaign_mission_dpad_is_latched_and_directional() {
    using namespace wawvr::mod;
    NativeGameplayCommandState state{};

    // First focused frame establishes ownership; the following neutral frame
    // arms the modifier exactly as a real centred stick does.
    static_cast<void>(update_native_gameplay_commands(
        {.input_owned = true, .single_player_profile = true}, &state));
    static_cast<void>(update_native_gameplay_commands(
        {.input_owned = true,
         .single_player_profile = true,
         .weapon_next_held = true,
         .mission_stick_valid = true},
        &state));

    auto update = update_native_gameplay_commands(
        {.input_owned = true,
         .single_player_profile = true,
         .weapon_next_held = true,
         .mission_stick_valid = true,
         .mission_stick_x = -0.90F},
        &state);
    expect(update.mission_key_tap == kMissionDpadLeftKey,
           "held left-secondary + left stick left emits native key 6 once");
    update = update_native_gameplay_commands(
        {.input_owned = true,
         .single_player_profile = true,
         .weapon_next_held = true,
         .mission_stick_valid = true,
         .mission_stick_x = -0.90F},
        &state);
    expect(update.mission_key_tap == 0,
           "held campaign direction cannot repeat every frame");

    update = update_native_gameplay_commands(
        {.input_owned = true,
         .single_player_profile = true,
         .weapon_next_held = true,
         .mission_stick_valid = true,
         .mission_stick_x = -0.40F},
        &state);
    expect(update.mission_key_tap == 0 && !state.mission_dpad_armed,
           "stick above release threshold remains latched");
    static_cast<void>(update_native_gameplay_commands(
        {.input_owned = true,
         .single_player_profile = true,
         .weapon_next_held = true,
         .mission_stick_valid = true,
         .mission_stick_x = -0.20F},
        &state));
    expect(state.mission_dpad_armed,
           "centred modifier stick rearms the campaign D-pad");

    update = update_native_gameplay_commands(
        {.input_owned = true,
         .single_player_profile = true,
         .weapon_next_held = true,
         .mission_stick_valid = true,
         .mission_stick_y = 0.90F},
        &state);
    expect(update.mission_key_tap == kMissionDpadUpKey,
           "campaign D-pad up emits native key 5");
    static_cast<void>(update_native_gameplay_commands(
        {.input_owned = true,
         .single_player_profile = true,
         .weapon_next_held = true,
         .mission_stick_valid = true},
        &state));
    update = update_native_gameplay_commands(
        {.input_owned = true,
         .single_player_profile = true,
         .weapon_next_held = true,
         .mission_stick_valid = true,
         .mission_stick_y = -0.90F},
        &state);
    expect(update.mission_key_tap == kMissionDpadDownKey,
           "campaign D-pad down emits native N key");
    static_cast<void>(update_native_gameplay_commands(
        {.input_owned = true,
         .single_player_profile = true,
         .weapon_next_held = true,
         .mission_stick_valid = true},
        &state));
    update = update_native_gameplay_commands(
        {.input_owned = true,
         .single_player_profile = true,
         .weapon_next_held = true,
         .mission_stick_valid = true,
         .mission_stick_x = 0.90F},
        &state);
    expect(update.mission_key_tap == kMissionDpadRightKey,
           "campaign D-pad right emits native key 7");
}

void test_campaign_mission_dpad_fails_closed_across_ownership() {
    using namespace wawvr::mod;
    NativeGameplayCommandState state{};
    auto update = update_native_gameplay_commands(
        {.input_owned = false,
         .single_player_profile = true,
         .weapon_next_held = true,
         .mission_stick_valid = true,
         .mission_stick_x = -1.0F},
        &state);
    expect(update.mission_key_tap == 0,
           "UI ownership blocks the campaign mission D-pad");
    update = update_native_gameplay_commands(
        {.input_owned = true,
         .single_player_profile = true,
         .weapon_next_held = true,
         .mission_stick_valid = true,
         .mission_stick_x = -1.0F},
        &state);
    expect(update.mission_key_tap == 0,
           "focus or gameplay regain baselines a held campaign direction");
    update = update_native_gameplay_commands(
        {.input_owned = true,
         .single_player_profile = true,
         .weapon_next_held = true,
         .mission_stick_valid = true,
         .mission_stick_x =
             std::numeric_limits<float>::quiet_NaN()},
        &state);
    expect(update.mission_key_tap == 0 && !state.mission_dpad_armed,
           "non-finite campaign stick input emits no key and disarms");
}

void test_walking_weapon_next_does_not_trigger_mission_dpad() {
    using namespace wawvr::mod;
    NativeGameplayCommandState state{};

    static_cast<void>(update_native_gameplay_commands(
        {.input_owned = true, .single_player_profile = true}, &state));
    static_cast<void>(update_native_gameplay_commands(
        {.input_owned = true,
         .single_player_profile = true,
         .mission_stick_valid = true,
         .mission_stick_x = -1.0F},
        &state));

    auto update = update_native_gameplay_commands(
        {.input_owned = true,
         .single_player_profile = true,
         .weapon_next_held = true,
         .mission_stick_valid = true,
         .mission_stick_x = -1.0F},
        &state);
    expect(!update.weapon_next_tap && update.mission_key_tap == 0 &&
               !state.mission_dpad_armed,
           "pressing Y while walking cannot enter the campaign D-pad");

    update = update_native_gameplay_commands(
        {.input_owned = true,
         .single_player_profile = true,
         .weapon_next_held = false,
         .mission_stick_valid = true,
         .mission_stick_x = -1.0F},
        &state);
    expect(update.weapon_next_tap && update.mission_key_tap == 0,
           "releasing Y while walking performs one normal weapon switch");
}

void test_weapon_next_mission_chord_suppresses_release() {
    using namespace wawvr::mod;
    NativeGameplayCommandState state{};

    static_cast<void>(update_native_gameplay_commands(
        {.input_owned = true, .single_player_profile = true}, &state));
    auto update = update_native_gameplay_commands(
        {.input_owned = true,
         .single_player_profile = true,
         .weapon_next_held = true,
         .mission_stick_valid = true},
        &state);
    expect(!update.weapon_next_tap && update.mission_key_tap == 0,
           "held left-secondary begins a chord without cycling weapons");

    update = update_native_gameplay_commands(
        {.input_owned = true,
         .single_player_profile = true,
         .weapon_next_held = true,
         .mission_stick_valid = true,
         .mission_stick_x = -0.90F},
        &state);
    expect(update.mission_key_tap == kMissionDpadLeftKey,
           "held left-secondary plus left emits the rocket action slot");

    update = update_native_gameplay_commands(
        {.input_owned = true,
         .single_player_profile = true,
         .weapon_next_held = false,
         .mission_stick_valid = true},
        &state);
    expect(!update.weapon_next_tap,
           "completed mission chord consumes the queued weapon-next release");

    static_cast<void>(update_native_gameplay_commands(
        {.input_owned = true,
         .single_player_profile = true,
         .weapon_next_held = true,
         .mission_stick_valid = true},
        &state));
    update = update_native_gameplay_commands(
        {.input_owned = true,
         .single_player_profile = true,
         .weapon_next_held = false,
         .mission_stick_valid = true},
        &state);
    expect(update.weapon_next_tap,
           "a later plain left-secondary tap still cycles on release");
}

void test_mission_chord_requires_exact_single_player_profile() {
    using namespace wawvr::mod;
    NativeGameplayCommandState state{};

    static_cast<void>(update_native_gameplay_commands(
        {.input_owned = true}, &state));
    static_cast<void>(update_native_gameplay_commands(
        {.input_owned = true,
         .weapon_next_held = true,
         .mission_stick_valid = true},
        &state));
    auto update = update_native_gameplay_commands(
        {.input_owned = true,
         .weapon_next_held = true,
         .mission_stick_valid = true,
         .mission_stick_x = -1.0F},
        &state);
    expect(update.mission_key_tap == 0,
           "MP profile cannot emit a mission action-slot key");
    update = update_native_gameplay_commands(
        {.input_owned = true,
         .weapon_next_held = false,
         .mission_stick_valid = true,
         .mission_stick_x = -1.0F},
        &state);
    expect(update.weapon_next_tap && update.mission_key_tap == 0,
           "MP keeps ordinary weapon-next release behavior");
}

void test_pezbot_autofill_runs_once_per_frontend_visit() {
    using namespace wawvr::mod;
    PezBotAutofillState state{};
    PezBotAutofillFrame frame{
        .enabled = true,
        .multiplayer_profile = true,
        .presentation_state_valid = true,
        .connection_state = 0,
        .active_connection_state = 10,
    };
    expect(should_queue_pezbot_autofill(frame, &state),
           "disconnected MP frontend queues one PeZBOT refill");
    expect(!should_queue_pezbot_autofill(frame, &state),
           "remaining in the frontend cannot repeat the refill command");

    frame.presentation_state_valid = false;
    expect(!should_queue_pezbot_autofill(frame, &state) &&
               state.disconnected_frontend_latched,
           "transient invalid telemetry preserves the frontend latch");
    frame.presentation_state_valid = true;
    frame.connection_state = 5;
    expect(!should_queue_pezbot_autofill(frame, &state) &&
               !state.disconnected_frontend_latched,
           "initial loading does not repeat the frontend refill");
    frame.connection_state = 7;
    expect(!should_queue_pezbot_autofill(frame, &state),
           "later initial-map loading states still cannot repeat it");
    frame.connection_state = 10;
    expect(!should_queue_pezbot_autofill(frame, &state) &&
               state.active_server_observed,
           "active team/class/gameplay states never add a second roster");
    frame.connection_state = 0;
    expect(should_queue_pezbot_autofill(frame, &state),
           "returning to choose another map or mode queues a fresh refill");
}

void test_pezbot_autofill_handles_connected_map_changes() {
    using namespace wawvr::mod;
    PezBotAutofillState state{};
    PezBotAutofillFrame frame{
        .enabled = true,
        .multiplayer_profile = true,
        .presentation_state_valid = true,
        .connection_state = 10,
        .active_connection_state = 10,
    };

    expect(!should_queue_pezbot_autofill(frame, &state),
           "observing the active server only arms its departure edge");
    std::int32_t queued = 0;
    for (const std::int32_t connection : {6, 7, 6, 8, 10}) {
        frame.connection_state = connection;
        queued += should_queue_pezbot_autofill(frame, &state) ? 1 : 0;
    }
    expect(queued == 1,
           "observed 10-6-7-6-8-10 map change queues exactly one refill");

    frame.connection_state = 10;
    expect(!should_queue_pezbot_autofill(frame, &state),
           "remaining active for team, class, pause, or play cannot refill");
    frame.connection_state = 6;
    expect(should_queue_pezbot_autofill(frame, &state),
           "the following connected map change gets its own refill");
    frame.connection_state = 7;
    expect(!should_queue_pezbot_autofill(frame, &state),
           "the departure latch spans the following load sequence");
}

void test_pezbot_autofill_is_mp_and_launcher_gated() {
    using namespace wawvr::mod;
    PezBotAutofillState state{};
    PezBotAutofillFrame frame{
        .enabled = false,
        .multiplayer_profile = true,
        .presentation_state_valid = true,
        .connection_state = 0,
        .active_connection_state = 10,
    };
    expect(!should_queue_pezbot_autofill(frame, &state),
           "ordinary MP without the verified PeZBOT marker is untouched");
    frame.enabled = true;
    frame.multiplayer_profile = false;
    expect(!should_queue_pezbot_autofill(frame, &state),
           "single-player and Zombies can never receive PeZBOT commands");
    expect(!should_queue_pezbot_autofill(frame, nullptr),
           "null autofill state fails closed");

    state = {
        .disconnected_frontend_latched = true,
        .active_server_observed = true,
        .active_departure_latched = true,
    };
    frame.enabled = false;
    frame.multiplayer_profile = true;
    expect(!should_queue_pezbot_autofill(frame, &state) &&
               !state.disconnected_frontend_latched &&
               !state.active_server_observed &&
               !state.active_departure_latched,
           "disabling autofill clears all lifecycle state");
}

void test_pezbot_autofill_marker_requires_exact_boundaries() {
    using wawvr::mod::command_line_has_exact_marker;
    constexpr std::wstring_view marker =
        L"+set wawvr_pezbot_autofill 9";
    expect(command_line_has_exact_marker(
               L"CoDWaWmp.exe +set wawvr_pezbot_autofill 9", marker),
           "exact launcher marker at command-line end is accepted");
    expect(command_line_has_exact_marker(
               L"CoDWaWmp.exe +set wawvr_pezbot_autofill 9 +set x 1",
               marker),
           "exact launcher marker followed by another token is accepted");
    expect(!command_line_has_exact_marker(
               L"CoDWaWmp.exe +set wawvr_pezbot_autofill 90", marker),
           "a longer marker value fails closed");
    expect(!command_line_has_exact_marker(
               L"CoDWaWmp.exe x+set wawvr_pezbot_autofill 9", marker),
           "marker text embedded in another token fails closed");
    expect(!command_line_has_exact_marker(
               L"C:\\fake +set wawvr_pezbot_autofill 9\\CoDWaWmp.exe",
               marker),
           "marker text embedded in an executable path fails closed");
    expect(!command_line_has_exact_marker(L"CoDWaWmp.exe", marker) &&
               !command_line_has_exact_marker(L"CoDWaWmp.exe", L""),
           "missing or empty authorization markers fail closed");
}

}  // namespace

int main() {
    test_weapon_next_is_one_release_per_press();
    test_held_button_is_suppressed_when_gameplay_gains_ownership();
    test_focus_loss_requires_a_fresh_press();
    test_campaign_mission_dpad_is_latched_and_directional();
    test_campaign_mission_dpad_fails_closed_across_ownership();
    test_walking_weapon_next_does_not_trigger_mission_dpad();
    test_weapon_next_mission_chord_suppresses_release();
    test_mission_chord_requires_exact_single_player_profile();
    test_pezbot_autofill_runs_once_per_frontend_visit();
    test_pezbot_autofill_handles_connected_map_changes();
    test_pezbot_autofill_is_mp_and_launcher_gated();
    test_pezbot_autofill_marker_requires_exact_boundaries();
    if (failures != 0) {
        std::cerr << failures << " native gameplay command test(s) failed\n";
        return 1;
    }
    std::cout << "native gameplay command tests passed\n";
    return 0;
}
