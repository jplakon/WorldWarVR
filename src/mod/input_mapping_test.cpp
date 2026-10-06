// SPDX-License-Identifier: GPL-3.0-only

#include "controller_state.hpp"
#include "current_head_local_controller_pose_logic.hpp"
#include "input_mapping.hpp"
#include "weapon_placement.hpp"

#include "t4/usercmd.hpp"

#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {

using namespace wawvr;

void check(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

[[nodiscard]] bool near(
    const float left,
    const float right,
    const float tolerance = 0.001F) noexcept {
    return std::abs(left - right) <= tolerance;
}

[[nodiscard]] xr::Basis3f identity_axis() noexcept {
    return {};
}

[[nodiscard]] mod::ControllerFrameSnapshot valid_snapshot() noexcept {
    mod::ControllerFrameSnapshot snapshot{};
    snapshot.frame.frame_id = 4;
    snapshot.frame.views_valid = true;
    snapshot.frame.head_center.orientation.w = 1.0F;
    snapshot.frame.actions.focused = true;
    snapshot.frame.actions.sequence = 8;
    // Model the normal runtime contract. Individual tests clear this to cover
    // the SteamVR Quest fallback where B is reserved for pause/back.
    snapshot.frame.actions.menu.active = true;
    snapshot.tracking_anchor.orientation.w = 1.0F;
    snapshot.publication_milliseconds = 1'000;

    auto& right_aim = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)].aim;
    right_aim.active = true;
    right_aim.orientation_valid = true;
    right_aim.pose.orientation.w = 1.0F;
    return snapshot;
}

void test_identity_aim_and_trigger() {
    auto snapshot = valid_snapshot();
    auto& trigger = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)].trigger;
    trigger.active = true;
    trigger.current = 0.8F;
    snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)].squeeze =
        {true, 0.8F, false};

    t4::UsercmdSp command{};
    t4::add_button(command, t4::UsercmdButton::reload);
    const auto result = mod::apply_controller_input(
        command, snapshot, identity_axis(), 1'001);
    check(result.frame_accepted, "fresh focused frame accepted");
    check(result.weapon_aim_applied, "identity controller aim applied");
    check(result.weapon_trigger_applied, "right trigger applied");
    check(command.gun_pitch_short == 0, "identity aim has zero pitch");
    check(command.gun_yaw_short == 0, "identity aim has zero yaw");
    check(t4::has_button(command, t4::UsercmdButton::attack),
          "right trigger injects attack");
    check(t4::has_button(command, t4::UsercmdButton::reload),
          "native buttons are preserved");
}

void test_camera_axis_composition() {
    const auto snapshot = valid_snapshot();
    xr::Basis3f camera{};
    camera.forward = {0.0F, 1.0F, 0.0F};
    camera.left = {-1.0F, 0.0F, 0.0F};
    camera.up = {0.0F, 0.0F, 1.0F};
    float pitch = 99.0F;
    float yaw = 99.0F;
    check(mod::controller_aim_degrees(
              snapshot, camera, &pitch, &yaw),
          "camera-composed aim is valid");
    check(std::abs(pitch) < 0.001F, "camera-composed pitch");
    check(std::abs(yaw - 90.0F) < 0.001F, "camera-composed yaw");
}

void test_controller_aim_is_relative_to_tracking_anchor() {
    auto snapshot = valid_snapshot();
    constexpr float kHalfSqrtTwo = 0.70710678118F;
    const xr::Quaternionf head_yaw_left = {
        0.0F, kHalfSqrtTwo, 0.0F, kHalfSqrtTwo};
    snapshot.frame.head_center.orientation = head_yaw_left;
    snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)]
        .aim.pose.orientation = head_yaw_left;

    // T4's global refdef remains stock outside the temporary stereo scene
    // calls. A controller pointing ahead with a +90-degree HMD yaw must retain
    // that anchor-relative yaw when composed through this stock camera.
    xr::Basis3f camera{};
    float pitch = 99.0F;
    float yaw = 99.0F;
    check(mod::controller_aim_degrees(
              snapshot, camera, &pitch, &yaw),
          "tracking-anchor-relative controller aim is valid");
    check(std::abs(pitch) < 0.001F,
          "tracking-anchor-relative controller pitch");
    check(std::abs(yaw - 90.0F) < 0.001F,
          "stock refdef does not cancel tracked HMD/controller yaw");
}

void test_hmd_oriented_movement_and_buttons() {
    auto snapshot = valid_snapshot();
    auto& left = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Left)];
    auto& right = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    left.stick.active = true;
    left.stick.current = {0.0F, 1.0F};
    left.trigger = {true, 0.8F, false};
    left.squeeze = {true, 0.8F, false};
    left.primary = {true, true, true};
    left.stick_click = {true, true, true};
    right.primary = {true, true, true};
    right.secondary = {true, true, true};
    right.stick_click = {true, true, true};
    right.squeeze = {true, 0.8F, false};

    t4::UsercmdSp command{};
    command.melee_charge_yaw = 73.0F;
    command.melee_charge_distance = 91;
    const auto result = mod::apply_controller_input(
        command, snapshot, identity_axis(), 1'010);
    check(result.movement_applied && command.forward_move == 127 &&
              command.right_move == 0,
          "identity HMD maps stick-up to forward movement");
    check(t4::has_button(command, t4::UsercmdButton::reload),
          "right A maps to COD4-default reload");
    check(t4::has_button(command, t4::UsercmdButton::use), "use map");
    check(t4::has_button(command, t4::UsercmdButton::sprint), "sprint map");
    check(t4::has_button(command, t4::UsercmdButton::aim_down_sights),
          "holding both weapon grips enters T4's native ADS pose");
    check(!t4::has_button(command, t4::UsercmdButton::jump),
          "left trigger is reserved for manual grenade interaction");
    check(t4::has_button(command, t4::UsercmdButton::crouch), "crouch map");
    check(t4::has_button(command, t4::UsercmdButton::melee) &&
              result.melee_button_held,
          "right-stick click maps to melee and reports its exact input source");
    check(result.melee_comfort_applied &&
              std::abs(command.melee_charge_yaw) < 0.001F &&
              command.melee_charge_distance == 0,
          "VR melee keeps the knife button but removes target yaw and lunge");
    check(!t4::has_button(command, t4::UsercmdButton::frag_grenade) &&
              !t4::has_button(command, t4::UsercmdButton::smoke_grenade),
          "COD4 default keeps both grip actions out of native grenade bits");

    // A +90 degree OpenXR yaw points HMD forward toward IW left. Stick-up
    // therefore becomes negative right-move in the unchanged body basis.
    constexpr float kHalfSqrtTwo = 0.70710678118F;
    snapshot.frame.head_center.orientation =
        {0.0F, kHalfSqrtTwo, 0.0F, kHalfSqrtTwo};
    command = {};
    const auto rotated = mod::apply_controller_input(
        command, snapshot, identity_axis(), 1'011);
    check(rotated.movement_applied, "rotated HMD movement applied");
    check(std::abs(static_cast<int>(command.forward_move)) <= 1 &&
              command.right_move == -127,
          "left-facing HMD rotates stick-up into body-left movement");

    // Looking 60 degrees upward leaves only half of the raw head-forward
    // vector in the horizontal plane. Normalizing that projection keeps full
    // stick travel at full movement speed.
    constexpr float kSinThirty = 0.5F;
    constexpr float kCosThirty = 0.86602540378F;
    snapshot.frame.head_center.orientation = {
        kSinThirty, 0.0F, 0.0F, kCosThirty};
    command = {};
    const auto pitched = mod::apply_controller_input(
        command, snapshot, identity_axis(), 1'012);
    check(pitched.movement_applied && command.forward_move == 127 &&
              command.right_move == 0,
          "HMD pitch does not reduce horizontal movement speed");
}

void test_campaign_controls_never_suspend_locomotion() {
    auto snapshot = valid_snapshot();
    auto& left = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Left)];
    auto& right = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    left.stick = {true, {0.0F, 1.0F}, true};
    right.thumbrest = {true, true, true};

    t4::UsercmdSp sp_command{};
    const auto sp_result = mod::apply_controller_input(
        sp_command, snapshot, identity_axis(), 1'001);
    check(sp_result.movement_applied && sp_command.forward_move == 127 &&
              sp_command.right_move == 0,
          "passive Touch thumbrest contact cannot stop SP/Zombies locomotion");

    t4::UsercmdMp mp_command{};
    const auto mp_result = mod::apply_controller_input(
        mp_command, snapshot, identity_axis(), 1'001);
    check(mp_result.movement_applied && mp_command.forward_move == 127 &&
              mp_command.right_move == 0,
          "passive Touch thumbrest contact does not alter MP locomotion");

    right.thumbrest = {};
    left.secondary = {true, true, true};
    sp_command = {};
    const auto secondary_sp_result = mod::apply_controller_input(
        sp_command, snapshot, identity_axis(), 1'002);
    check(secondary_sp_result.movement_applied &&
              sp_command.forward_move == 127 && sp_command.right_move == 0,
          "deliberate campaign chord cannot stop SP/Zombies locomotion");

    mp_command = {};
    const auto secondary_mp_result = mod::apply_controller_input(
        mp_command, snapshot, identity_axis(), 1'002);
    check(secondary_mp_result.movement_applied &&
              mp_command.forward_move == 127 && mp_command.right_move == 0,
          "left-secondary mission modifier does not alter MP locomotion");
}

void test_grips_are_reserved_for_physical_interactions() {
    auto snapshot = valid_snapshot();
    auto& left = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Left)];
    left.squeeze = {true, 0.8F, true};

    t4::UsercmdSp command{};
    t4::add_button(command, t4::UsercmdButton::aim_down_sights);
    const auto held_result = mod::apply_controller_input(
        command, snapshot, identity_axis(), 1'001);
    check(!held_result.gameplay_buttons_applied &&
              t4::has_button(command, t4::UsercmdButton::aim_down_sights) &&
              !t4::has_button(command, t4::UsercmdButton::smoke_grenade),
          "left grip is reserved for support/manual reload and preserves native ADS");

    left.squeeze.current = 0.0F;
    command = {};
    const auto released_result = mod::apply_controller_input(
        command, snapshot, identity_axis(), 1'002);
    check(!released_result.gameplay_buttons_applied &&
              !t4::has_button(command, t4::UsercmdButton::smoke_grenade) &&
              !t4::has_button(command, t4::UsercmdButton::aim_down_sights),
          "released left grip injects neither tactical grenade nor ADS");
}

void test_melee_charge_is_unconditionally_suppressed() {
    t4::UsercmdSp command{};
    command.melee_charge_yaw = -132.5F;
    command.melee_charge_distance = 255;
    t4::add_button(command, t4::UsercmdButton::melee);

    mod::suppress_t4_melee_charge(command);

    check(t4::has_button(command, t4::UsercmdButton::melee),
          "melee charge suppression preserves the ordinary knife button");
    check(std::abs(command.melee_charge_yaw) < 0.001F &&
              command.melee_charge_distance == 0,
          "melee charge suppression uses T4's native no-charge sentinel");
}

void test_right_grip_does_not_guess_a_grenade_binding() {
    auto snapshot = valid_snapshot();
    auto& right = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    constexpr float kHalfSqrtTwo = 0.70710678118F;
    right.aim.pose.orientation =
        {0.0F, kHalfSqrtTwo, 0.0F, kHalfSqrtTwo};

    t4::UsercmdSp command{};
    command.view_angles = {111, 222, 333};
    right.squeeze = {true, mod::kControllerButtonThreshold - 0.01F, false};
    auto result = mod::apply_controller_input(
        command, snapshot, identity_axis(), 1'001);
    check(result.weapon_aim_applied &&
              !t4::has_button(command, t4::UsercmdButton::frag_grenade),
          "sub-threshold grip keeps controller aim but does not prime a frag");
    check(command.view_angles == std::array<std::int32_t, 3>{111, 222, 333},
          "controller frag aiming never turns the body or HMD camera");

    command = {};
    command.view_angles = {111, 222, 333};
    right.squeeze.current = mod::kControllerButtonThreshold;
    result = mod::apply_controller_input(
        command, snapshot, identity_axis(), 1'002);
    check(!t4::has_button(command, t4::UsercmdButton::frag_grenade) &&
              result.weapon_aim_applied,
          "held right grip remains available to physical interaction logic");
    check(command.gun_yaw_short == static_cast<std::int16_t>(16384),
          "right-controller weapon aim remains independent of grip binding");
    check(command.view_angles == std::array<std::int32_t, 3>{111, 222, 333},
          "held grip leaves native view angles unchanged");

    command = {};
    right.squeeze = {true, 0.0F, true};
    result = mod::apply_controller_input(
        command, snapshot, identity_axis(), 1'003);
    check(!t4::has_button(command, t4::UsercmdButton::frag_grenade),
          "releasing grip still injects no guessed grenade bit");

    command = {};
    t4::add_button(command, t4::UsercmdButton::frag_grenade);
    result = mod::apply_controller_input(
        command, snapshot, identity_axis(), 1'004);
    check(t4::has_button(command, t4::UsercmdButton::frag_grenade),
          "VR release never erases a native keyboard/mouse frag bit");
}

void test_focus_staleness_and_invalid_pose_fail_closed() {
    const t4::UsercmdSp original = [] {
        t4::UsercmdSp command{};
        command.buttons = 0x12340000U;
        command.forward_move = 11;
        command.gun_pitch_short = 321;
        return command;
    }();

    auto snapshot = valid_snapshot();
    t4::UsercmdSp command = original;
    auto result = mod::apply_controller_input(
        command, snapshot, identity_axis(),
        snapshot.publication_milliseconds +
            mod::kMaximumControllerFrameAgeMilliseconds + 1);
    check(!result.frame_accepted &&
              std::memcmp(&command, &original, sizeof(command)) == 0,
          "stale controller frame cannot mutate command");

    snapshot.frame.actions.focused = false;
    command = original;
    result = mod::apply_controller_input(command, snapshot, identity_axis(), 1'001);
    check(!result.frame_accepted &&
              std::memcmp(&command, &original, sizeof(command)) == 0,
          "unfocused session cannot mutate command");

    snapshot = valid_snapshot();
    auto& right = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    right.trigger = {true, 1.0F, false};
    right.squeeze = {true, 1.0F, false};
    right.aim.pose.orientation.x =
        std::numeric_limits<float>::quiet_NaN();
    command = original;
    result = mod::apply_controller_input(command, snapshot, identity_axis(), 1'001);
    check(result.frame_accepted && !result.weapon_aim_applied &&
              !t4::has_button(command, t4::UsercmdButton::attack),
          "non-finite aim suppresses VR aim and trigger atomically");
    check(command.gun_pitch_short == original.gun_pitch_short,
          "invalid aim preserves native gun angle");
}

void test_high_level_actions_do_not_guess_usercmd_fields() {
    auto snapshot = valid_snapshot();
    snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)].aim.active = false;
    auto& left = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Left)];
    left.secondary = {true, true, true};
    snapshot.frame.actions.menu = {true, true, true};

    t4::UsercmdSp command{};
    command.weapon = 7;
    static_cast<void>(mod::apply_controller_input(
        command, snapshot, identity_axis(), 1'001));
    check(command.weapon == 7 && command.buttons == 0,
          "weapon-next/menu actions never guess at raw usercmd fields");
}

void test_multiplayer_command_prefix_and_active_state() {
    auto snapshot = valid_snapshot();
    auto& right = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    right.trigger = {true, 0.9F, false};
    right.squeeze = {true, 0.9F, false};
    right.stick_click = {true, true, true};

    t4::UsercmdMp command{};
    command.opaque_after_angles.fill(std::byte{0xA5});
    const auto result = mod::apply_controller_input(
        command, snapshot, identity_axis(), 1'001);
    check(result.weapon_aim_applied && result.weapon_trigger_applied,
          "MP command accepts the shared controller aim/button prefix");
    check(t4::has_button(command, t4::UsercmdButton::attack) &&
              t4::has_button(command, t4::UsercmdButton::melee),
          "MP command receives attack and ordinary melee buttons");
    for (const auto value : command.opaque_after_angles) {
        check(value == std::byte{0xA5},
              "MP opaque tail is never treated as SP melee-charge state");
    }
    check(!result.melee_comfort_applied,
          "MP does not claim an unverified melee-charge write");
    check(mod::controller_gameplay_input_allowed(
              0, 10, mod::kT4MpActiveConnectionState) &&
              !mod::controller_gameplay_input_allowed(
                  0, 9, mod::kT4MpActiveConnectionState),
          "MP controller ownership uses live-verified active connection state 10");
    check(mod::snap_turn_gameplay_allowed(
              0, 10, mod::kT4MpActiveConnectionState),
          "MP snap turn uses live-verified active connection state 10");
}

void test_horizontal_snap_turn_hysteresis() {
    mod::SnapTurnState state{};
    xr::Vec2ActionState stick{true, {0.80F, 0.0F}, true};
    check(near(mod::consume_snap_turn_degrees(stick, &state), -45.0F),
          "right stick engages one rightward snap");
    check(near(mod::consume_snap_turn_degrees(stick, &state), 0.0F),
          "held stick cannot repeat snap");

    stick.current.x = 0.40F;
    check(near(mod::consume_snap_turn_degrees(stick, &state), 0.0F),
          "stick above release threshold stays latched");
    stick.current.x = 0.20F;
    check(near(mod::consume_snap_turn_degrees(stick, &state), 0.0F) &&
              state.armed,
          "centered stick rearms snap");
    stick.current.x = -0.90F;
    check(near(mod::consume_snap_turn_degrees(stick, &state), 45.0F),
          "left stick engages one leftward snap");

    mod::reset_snap_turn(&state);
    stick.current = {0.85F, 0.90F};
    check(near(mod::consume_snap_turn_degrees(stick, &state), 0.0F) &&
              state.armed,
          "vertically dominant right stick never turns");
    stick.active = false;
    state.armed = false;
    check(near(mod::consume_snap_turn_degrees(stick, &state), 0.0F) &&
              state.armed,
          "inactive stick safely rearms snap");
}

void test_b_is_not_crouch_when_it_is_the_pause_fallback() {
    auto snapshot = valid_snapshot();
    snapshot.frame.actions.menu = {};
    auto& right = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    right.secondary = {true, true, true};

    t4::UsercmdSp command{};
    const auto result = mod::apply_controller_input(
        command, snapshot, identity_axis(), 1'001);
    check(result.frame_accepted,
          "SteamVR fallback frame remains valid gameplay input");
    check(!t4::has_button(command, t4::UsercmdButton::crouch),
          "B pause fallback cannot simultaneously crouch the player");
}

bool mounted_probe_false(
    const mod::ControllerFrameSnapshot&, std::uint64_t) noexcept {
    return false;
}

bool mounted_probe_true(
    const mod::ControllerFrameSnapshot&, std::uint64_t) noexcept {
    return true;
}

void test_mounted_trigger_route_without_handheld_grip() {
    auto snapshot = valid_snapshot();
    auto& trigger = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)].trigger;
    trigger = {true, 1.0F, false};

    t4::UsercmdSp command{};
    mod::bind_mounted_gun_route_probe(&mounted_probe_false);
    auto result = mod::apply_controller_input(
        command, snapshot, identity_axis(), 1'001);
    check(!result.weapon_trigger_applied &&
              !t4::has_button(command, t4::UsercmdButton::attack),
          "right trigger without grip stays native when mounted probe rejects");

    command = {};
    mod::bind_mounted_gun_route_probe(&mounted_probe_true);
    result = mod::apply_controller_input(
        command, snapshot, identity_axis(), 1'001);
    check(result.weapon_trigger_applied &&
              t4::has_button(command, t4::UsercmdButton::attack),
          "accepted mounted context fires without a handheld right grip");
    mod::bind_mounted_gun_route_probe(nullptr);
}

void test_smooth_turn_setting_is_opt_in() {
    using mod::TurnMode;
    check(mod::turn_mode_from_setting(L"smooth") == TurnMode::Smooth,
          "exact smooth setting opts into continuous turning");
    check(mod::turn_mode_from_setting(L"") == TurnMode::Snap &&
              mod::turn_mode_from_setting(L"snap") == TurnMode::Snap &&
              mod::turn_mode_from_setting(L"1") == TurnMode::Snap &&
              mod::turn_mode_from_setting(L"Smooth") == TurnMode::Snap,
          "missing or non-exact turn settings preserve snap default");
}

void test_smooth_turn_rate_deadzone_dominance_and_hitch_cap() {
    mod::SmoothTurnState state{};
    xr::Vec2ActionState stick{true, {1.0F, 0.0F}, true};

    check(near(mod::consume_smooth_turn_degrees(
                   true, 1, 1'000, stick, &state), 0.0F),
          "first owned smooth-turn frame establishes a time baseline");
    check(near(mod::consume_smooth_turn_degrees(
                   true, 2, 1'016, stick, &state), -1.92F, 0.001F),
          "full right stick turns right at COD4-parity 120 degrees per second");
    check(near(mod::consume_smooth_turn_degrees(
                   true, 2, 1'020, stick, &state), 0.0F),
          "duplicate native commands for one XR action sample cannot turn twice");

    stick.current = {-1.0F, 0.0F};
    check(near(mod::consume_smooth_turn_degrees(
                   true, 3, 1'032, stick, &state), 1.92F, 0.001F),
          "full left stick turns left at the same fixed rate");

    stick.current = {0.625F, 0.0F};
    check(near(mod::consume_smooth_turn_degrees(
                   true, 4, 1'048, stick, &state), -0.96F, 0.001F),
          "usable range is linearly remapped from deadzone to full tilt");

    stick.current = {mod::kSmoothTurnDeadzone, 0.0F};
    check(near(mod::consume_smooth_turn_degrees(
                   true, 5, 1'064, stick, &state), 0.0F),
          "stick at the smooth-turn deadzone produces no yaw");

    stick.current = {1.0F, 0.90F};
    check(near(mod::consume_smooth_turn_degrees(
                   true, 6, 1'080, stick, &state), 0.0F),
          "vertical or diagonal stance intent is not consumed as turning");

    stick.current = {1.0F, 0.0F};
    check(near(mod::consume_smooth_turn_degrees(
                   true, 7, 2'080, stick, &state), -6.0F, 0.001F),
          "a long hitch is capped to 50 ms of smooth turning");
}

void test_smooth_turn_resets_across_input_ownership() {
    mod::SmoothTurnState state{};
    xr::Vec2ActionState stick{true, {1.0F, 0.0F}, true};
    static_cast<void>(mod::consume_smooth_turn_degrees(
        true, 1, 1'000, stick, &state));
    check(near(mod::consume_smooth_turn_degrees(
                   false, 2, 1'016, stick, &state), 0.0F) &&
              !state.input_was_owned &&
              state.previous_update_milliseconds == 0 &&
              state.last_action_sequence == 0,
          "focus, UI, or gameplay ownership loss clears smooth-turn timing");
    check(near(mod::consume_smooth_turn_degrees(
                   true, 3, 5'000, stick, &state), 0.0F),
          "focus regain baselines instead of applying accumulated yaw");

    stick.active = false;
    check(near(mod::consume_smooth_turn_degrees(
                   true, 4, 5'016, stick, &state), 0.0F) &&
              !state.input_was_owned,
          "inactive stick also clears smooth-turn timing");

    check(near(mod::consume_smooth_turn_degrees(
                   true, 5, 5'032, stick, nullptr), 0.0F),
          "null smooth-turn state fails closed");
}

void test_left_support_grip_enters_native_ads() {
    auto snapshot = valid_snapshot();
    auto& right = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    auto& left = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Left)];
    right.trigger = {true, 1.0F, false};
    right.squeeze = {true, 1.0F, false};
    left.squeeze = {true, 1.0F, false};

    t4::UsercmdSp command{};
    auto result = mod::apply_controller_input(
        command, snapshot, identity_axis(), 1'001);
    check(result.weapon_trigger_applied &&
              t4::has_button(command, t4::UsercmdButton::attack) &&
              t4::has_button(command, t4::UsercmdButton::aim_down_sights),
          "left support grip enters native ADS while right retains firing ownership");

    right.squeeze.current = 0.0F;
    command = {};
    result = mod::apply_controller_input(
        command, snapshot, identity_axis(), 1'001);
    check(!result.weapon_trigger_applied &&
              !t4::has_button(command, t4::UsercmdButton::attack) &&
              !t4::has_button(command, t4::UsercmdButton::aim_down_sights),
          "left support alone neither fires nor enters ADS");
}

void test_right_stick_stance_ladder_and_jump() {
    mod::DirectionalActionState state{};
    xr::Vec2ActionState stick{true, {0.0F, 0.0F}, true};
    using Action = mod::DirectionalAction;
    using Stance = mod::DirectionalActionState::Stance;

    auto update = mod::update_directional_actions(
        true, 1, stick, &state);
    check(update.action == Action::none && state.stance == Stance::standing,
          "neutral ownership frame arms directional actions");

    stick.current = {0.05F, 0.80F};
    update = mod::update_directional_actions(true, 2, stick, &state);
    check(update.action == Action::jump && state.stance == Stance::standing,
          "right-stick up jumps only while already standing");
    update = mod::update_directional_actions(true, 2, stick, &state);
    check(update.action == Action::none,
          "repeated native command for one XR sample cannot repeat jump");
    update = mod::update_directional_actions(true, 3, stick, &state);
    check(update.action == Action::none,
          "held right-stick up cannot repeat jump");

    stick.current = {0.0F, 0.0F};
    update = mod::update_directional_actions(true, 4, stick, &state);
    check(update.action == Action::none,
          "neutral right stick rearms directional actions");
    stick.current = {-0.05F, -0.82F};
    update = mod::update_directional_actions(true, 5, stick, &state);
    check(update.action == Action::crouch &&
              state.stance == Stance::crouched,
          "first right-stick down press goes from standing to crouched");
    update = mod::update_directional_actions(true, 6, stick, &state);
    check(update.action == Action::none && state.stance == Stance::crouched,
          "held right-stick down cannot continue from crouched to prone");

    stick.current = {0.0F, 0.0F};
    static_cast<void>(mod::update_directional_actions(
        true, 7, stick, &state));
    stick.current = {0.02F, -0.90F};
    update = mod::update_directional_actions(true, 8, stick, &state);
    check(update.action == Action::prone && state.stance == Stance::prone,
          "second rearmed right-stick down press goes crouched to prone");

    stick.current = {0.0F, 0.0F};
    static_cast<void>(mod::update_directional_actions(
        true, 9, stick, &state));
    stick.current = {-0.03F, 0.88F};
    update = mod::update_directional_actions(true, 10, stick, &state);
    check(update.action == Action::crouch &&
              state.stance == Stance::crouched,
          "first right-stick up press goes prone to crouched without jumping");

    stick.current = {0.0F, 0.0F};
    static_cast<void>(mod::update_directional_actions(
        true, 11, stick, &state));
    stick.current = {0.03F, 0.91F};
    update = mod::update_directional_actions(true, 12, stick, &state);
    check(update.action == Action::stand && state.stance == Stance::standing,
          "second right-stick up press goes crouched to standing without jumping");

    stick.current = {0.0F, 0.0F};
    static_cast<void>(mod::update_directional_actions(
        true, 13, stick, &state));
    stick.current = {0.0F, 0.92F};
    update = mod::update_directional_actions(true, 14, stick, &state);
    check(update.action == Action::jump && state.stance == Stance::standing,
          "a further right-stick up press jumps from standing");

    stick.current = {0.0F, 0.0F};
    static_cast<void>(mod::update_directional_actions(
        true, 15, stick, &state));
    stick.current = {0.85F, 0.85F};
    update = mod::update_directional_actions(true, 16, stick, &state);
    check(update.action == Action::none && state.stance == Stance::standing,
          "diagonal stick input does not steal horizontal turning");

    check(mod::directional_action_console_command(Action::crouch) ==
              "gocrouch\n" &&
              mod::directional_action_console_command(Action::prone) ==
                  "goprone\n",
          "downward stance rungs use T4's exact absolute crouch/prone commands");
    check(mod::directional_action_console_command(Action::stand) ==
              "+gostand\n-gostand\n" &&
              mod::directional_action_console_command(Action::jump) ==
                  "+gostand\n-gostand\n" &&
              mod::directional_action_console_command(Action::none).empty(),
          "stand and jump use one balanced native gostand pulse");

    mod::reset_directional_actions(&state);
    check(!state.input_was_owned && !state.armed &&
              state.last_action_sequence == 0 &&
              state.stance == Stance::standing,
          "directional action reset clears every latch");
}

void test_stance_ladder_survives_menu_or_focus_ownership() {
    using Action = mod::DirectionalAction;
    using Stance = mod::DirectionalActionState::Stance;
    mod::DirectionalActionState state{};
    xr::Vec2ActionState stick{true, {0.0F, 0.0F}, true};

    static_cast<void>(mod::update_directional_actions(
        true, 1, stick, &state));
    stick.current = {0.0F, -1.0F};
    static_cast<void>(mod::update_directional_actions(
        true, 2, stick, &state));
    stick.current = {0.0F, 0.0F};
    static_cast<void>(mod::update_directional_actions(
        true, 3, stick, &state));
    stick.current = {0.0F, -1.0F};
    auto update = mod::update_directional_actions(
        true, 4, stick, &state);
    check(update.action == Action::prone && state.stance == Stance::prone,
          "setup reaches prone before ownership loss");

    update = mod::update_directional_actions(false, 5, stick, &state);
    check(update.action == Action::none && state.stance == Stance::prone,
          "menu or focus ownership loss preserves the actual stance rung");
    stick.current = {0.0F, 1.0F};
    update = mod::update_directional_actions(true, 6, stick, &state);
    check(update.action == Action::none && state.stance == Stance::prone,
          "regaining ownership with the stick held cannot leak a stance action");
    stick.current = {0.0F, 0.0F};
    static_cast<void>(mod::update_directional_actions(
        true, 7, stick, &state));
    stick.current = {0.0F, 1.0F};
    update = mod::update_directional_actions(true, 8, stick, &state);
    check(update.action == Action::crouch &&
              state.stance == Stance::crouched,
          "a fresh upward press after focus regain raises prone to crouched");
}

void test_click_to_sprint_latch() {
    mod::SprintLatchState state{};
    xr::Vec2ActionState movement_stick{
        true, {0.0F, 1.0F}, true};
    xr::BoolActionState stick_click{true, false, true};

    check(!mod::update_sprint_latch(
              true, 1, movement_stick, stick_click, &state),
          "first owned sprint frame baselines a released L3");
    stick_click.current = true;
    check(mod::update_sprint_latch(
              true, 2, movement_stick, stick_click, &state),
          "fresh L3 edge latches sprint while locomoting");
    check(mod::update_sprint_latch(
              true, 2, movement_stick, stick_click, &state),
          "repeated usercmd for one XR sequence preserves one latch");

    stick_click.current = false;
    check(mod::update_sprint_latch(
              true, 3, movement_stick, stick_click, &state),
          "released L3 keeps sprint latched while the stick is moving");
    movement_stick.current = {};
    check(!mod::update_sprint_latch(
              true, 4, movement_stick, stick_click, &state),
          "returning locomotion stick to neutral clears sprint");

    movement_stick.current = {0.0F, 1.0F};
    stick_click.current = true;
    check(mod::update_sprint_latch(
              true, 5, movement_stick, stick_click, &state),
          "a later L3 edge can latch sprint again");
    check(!mod::update_sprint_latch(
              false, 6, movement_stick, stick_click, &state),
          "UI, focus, connection, or stale-frame ownership loss clears sprint");
    check(!mod::update_sprint_latch(
              true, 7, movement_stick, stick_click, &state),
          "held L3 on gameplay recovery is baselined without a synthetic edge");
    stick_click.current = false;
    check(!mod::update_sprint_latch(
              true, 8, movement_stick, stick_click, &state),
          "release after recovery rearms a future sprint click");
    stick_click.current = true;
    check(mod::update_sprint_latch(
              true, 9, movement_stick, stick_click, &state),
          "fresh post-recovery click latches normally");

    mod::reset_sprint_latch(&state);
    check(!state.input_was_owned && !state.click_was_held &&
              !state.latched && state.last_action_sequence == 0,
          "explicit XR/input reset clears all sprint latch state");
    check(!mod::update_sprint_latch(
              true, 10, movement_stick, stick_click, nullptr),
          "null sprint latch state fails closed");
}

void test_snap_turn_requires_exact_gameplay_state_and_no_ui_catcher() {
    check(mod::snap_turn_gameplay_allowed(0, 10),
          "exact T4 connection state 10 permits gameplay snap turn");
    check(!mod::snap_turn_gameplay_allowed(0, 9) &&
              !mod::snap_turn_gameplay_allowed(0, 11),
          "neighboring T4 connection states cannot receive snap turn");
    check(!mod::snap_turn_gameplay_allowed(0x08U, 10),
          "T4 key catcher 0x08 owns input and blocks snap turn");
    check(!mod::snap_turn_gameplay_allowed(0x10U, 10),
          "T4 key catcher 0x10 owns input and blocks snap turn");
    check(!mod::snap_turn_gameplay_allowed(0x18U, 10),
          "combined UI catcher bits block snap turn");
    check(mod::snap_turn_gameplay_allowed(0x04U, 10),
          "unrelated catcher bits do not broaden the verified gate");
}

void test_controller_gameplay_input_is_owned_by_gameplay_not_ui() {
    check(mod::controller_gameplay_input_allowed(0, 10),
          "active T4 gameplay without UI permits controller usercmd input");
    check(!mod::controller_gameplay_input_allowed(0x10U, 10),
          "native T4 UI catcher blocks controller usercmd input");
    check(!mod::controller_gameplay_input_allowed(0, 9) &&
              !mod::controller_gameplay_input_allowed(0, 11),
          "non-gameplay connection states cannot receive controller usercmd input");
    check(!mod::controller_gameplay_input_allowed(0x08U, 10) &&
              !mod::controller_gameplay_input_allowed(0x01U, 10),
          "other native input owners also block controller usercmd input");
}

void test_snap_turn_updates_t4_yaw_and_same_command_camera() {
    t4::UsercmdSp command{};
    command.view_angles[1] = 1'000;
    float client_yaw = 10.0F;
    xr::Basis3f camera{};
    check(mod::apply_snap_turn_to_t4_command(
              command, -45.0F, &client_yaw, &camera),
          "valid rightward snap applies to exact T4 state");
    check(command.view_angles[1] == ((1'000 - 8'192) & 0xFFFF),
          "post-serialization command yaw receives -8192 units");
    check(near(client_yaw, 325.0F),
          "live float yaw wraps into 0..360 degrees");
    constexpr float kHalfSqrtTwo = 0.70710678118F;
    check(near(camera.forward.x, kHalfSqrtTwo) &&
              near(camera.forward.y, -kHalfSqrtTwo),
          "sampled stock camera rotates right for same-command gun aim");

    const t4::UsercmdSp before = command;
    const float yaw_before = client_yaw;
    camera.forward.x = std::numeric_limits<float>::quiet_NaN();
    check(!mod::apply_snap_turn_to_t4_command(
              command, 45.0F, &client_yaw, &camera) &&
              std::memcmp(&command, &before, sizeof(command)) == 0 &&
              client_yaw == yaw_before,
          "invalid camera fails closed without partial yaw mutation");
}

void test_body_yaw_delta_tracks_physical_heading() {
    auto snapshot = valid_snapshot();
    constexpr float kHalfSqrtTwo = 0.70710678118F;
    snapshot.frame.head_center.orientation =
        {0.0F, kHalfSqrtTwo, 0.0F, kHalfSqrtTwo};
    float yaw = 0.0F;
    check(mod::controller_body_yaw_delta_degrees(snapshot, &yaw) &&
              near(yaw, 90.0F),
          "physical left turn produces matching positive T4 body yaw");

    auto& right_aim = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)].aim;
    right_aim.pose.orientation = snapshot.frame.head_center.orientation;
    float pitch_before = 0.0F;
    float weapon_yaw_before = 0.0F;
    xr::Basis3f camera{};
    check(mod::controller_aim_degrees(
              snapshot, camera, &pitch_before, &weapon_yaw_before),
          "pre-rebase controller world aim is valid");

    t4::UsercmdSp command{};
    float client_yaw = 0.0F;
    check(mod::apply_snap_turn_to_t4_command(
              command, yaw, &client_yaw, &camera),
          "body catch-up applies the extracted yaw to native state");

    snapshot.tracking_anchor.orientation =
        snapshot.frame.head_center.orientation;
    check(mod::controller_body_yaw_delta_degrees(snapshot, &yaw) &&
              near(yaw, 0.0F),
          "rebasing the tracking anchor consumes physical body yaw once");
    float pitch_after = 0.0F;
    float weapon_yaw_after = 0.0F;
    check(mod::controller_aim_degrees(
              snapshot, camera, &pitch_after, &weapon_yaw_after) &&
              near(weapon_yaw_after, weapon_yaw_before),
          "equal body and anchor yaw transfers preserve visible controller aim");

    auto& left = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Left)];
    left.stick = {true, {0.0F, 1.0F}, true};
    command = {};
    const auto movement = mod::apply_controller_input(
        command, snapshot, camera, 1'001);
    check(movement.movement_applied && command.forward_move == 127 &&
              command.right_move == 0,
          "stick-up is native forward after body and HMD heading synchronize");

    command = {};
    check(mod::apply_snap_turn_to_t4_command(
              command, -45.0F, &client_yaw, &camera),
          "snap turn remains independently applicable after body catch-up");
    float combined_pitch = 0.0F;
    float combined_yaw = 0.0F;
    check(mod::controller_aim_degrees(
              snapshot, camera, &combined_pitch, &combined_yaw) &&
              near(combined_yaw, 45.0F),
          "physical +90 and virtual -45 compose once instead of double-rotating");
}

void test_physical_melee_gesture() {
    using Gesture = mod::PhysicalMeleeGesture;
    auto snapshot = valid_snapshot();
    auto& right = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    right.grip.active = true;
    right.grip.position_valid = true;
    right.grip.pose.position = {0.20F, 0.0F, 0.0F};
    mod::PhysicalMeleeState state{};

    snapshot.frame.actions.sequence = 10;
    snapshot.publication_milliseconds = 1'000;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 1'001, &state) == Gesture::none,
          "first tracked hand pose only establishes the melee baseline");

    snapshot.frame.actions.sequence = 11;
    snapshot.publication_milliseconds = 1'080;
    right.grip.pose.position.x += 0.10F;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 1'081, &state) ==
              Gesture::right_hand_swing,
          "fast ten-centimetre right-hand swing triggers native melee once");
    check(near(state.last_trigger_relative_travel_meters, 0.10F) &&
              near(state.last_trigger_world_travel_meters, 0.10F) &&
              near(state.last_trigger_speed_meters_per_second, 1.25F) &&
              near(state.last_trigger_outward_travel_meters, 0.10F),
          "physical melee records enough motion evidence to identify its source");
    check(mod::update_physical_melee_gesture(
              true, snapshot, 1'081, &state) == Gesture::none,
          "several commands consuming one OpenXR sample cannot duplicate melee");

    snapshot.frame.actions.sequence = 12;
    snapshot.publication_milliseconds = 1'160;
    right.grip.pose.position.x += 0.10F;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 1'161, &state) == Gesture::none,
          "physical melee cooldown blocks animation-spam swings");

    snapshot.frame.actions.sequence = 13;
    snapshot.publication_milliseconds = 1'600;
    right.trigger = {true, 1.0F, true};
    right.grip.pose.position.x += 0.40F;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 1'601, &state) == Gesture::none,
          "firing blocks accidental physical melee");

    snapshot.frame.actions.sequence = 14;
    snapshot.publication_milliseconds = 2'100;
    right.trigger = {};
    snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Left)].squeeze =
            {true, 1.0F, true};
    right.grip.pose.position.x += 0.40F;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 2'101, &state) == Gesture::none,
          "a one-handed grip transition cannot become physical melee");

    mod::reset_physical_melee_gesture(&state);
    auto& left = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Left)];
    right.squeeze = {true, 1.0F, true};
    left.squeeze = {true, 1.0F, true};
    left.grip.active = true;
    left.grip.position_valid = true;
    right.grip.pose.position = {0.20F, 0.0F, 0.0F};
    left.grip.pose.position = {0.55F, 0.0F, 0.0F};
    snapshot.frame.actions.sequence = 18;
    snapshot.publication_milliseconds = 2'800;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 2'801, &state, false) == Gesture::none,
          "two squeezed empty hands cannot claim two-hand weapon melee");
    right.grip.pose.position.x += 0.10F;
    left.grip.pose.position.x += 0.10F;
    snapshot.frame.actions.sequence = 19;
    snapshot.publication_milliseconds = 2'880;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 2'881, &state, false) == Gesture::none,
          "coherent empty-hand movement remains blocked without weapon ownership");

    mod::reset_physical_melee_gesture(&state);
    right.grip.pose.position = {0.20F, 0.0F, 0.0F};
    left.grip.pose.position = {0.55F, 0.0F, 0.0F};
    snapshot.frame.actions.sequence = 20;
    snapshot.publication_milliseconds = 3'000;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 3'001, &state, true) == Gesture::none,
          "engaging both rifle grips establishes a fresh thrust baseline");

    for (std::uint64_t step = 1; step <= 4; ++step) {
        snapshot.frame.actions.sequence = 20 + step;
        snapshot.publication_milliseconds = 3'000 + step * 11;
        right.grip.pose.position.x += 0.012F;
        left.grip.pose.position.x += 0.012F;
        const Gesture gesture = mod::update_physical_melee_gesture(
            true, snapshot, snapshot.publication_milliseconds + 1, &state,
            true);
        check(
            gesture == (step == 4
                ? Gesture::two_hand_thrust : Gesture::none),
            step == 4
                ? "a natural two-hand thrust accumulated across 90 Hz frames triggers melee"
                : "sub-threshold 90 Hz motion accumulates without triggering early");
    }

    mod::reset_physical_melee_gesture(&state);
    right.grip.pose.position = {0.20F, 0.0F, 0.0F};
    left.grip.pose.position = {0.55F, 0.0F, 0.0F};
    right.squeeze.current = 1.0F;
    left.squeeze.current = 1.0F;
    snapshot.frame.actions.sequence = 25;
    snapshot.publication_milliseconds = 3'500;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 3'501, &state, true) == Gesture::none,
          "two-hand grip hysteresis fixture establishes a baseline");
    right.squeeze.current = 0.50F;
    left.squeeze.current = 0.50F;
    right.grip.pose.position.x += 0.05F;
    left.grip.pose.position.x += 0.05F;
    snapshot.frame.actions.sequence = 26;
    snapshot.publication_milliseconds = 3'550;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 3'551, &state, true) ==
              Gesture::two_hand_thrust,
          "mid-pressure grips remain latched while the weapon is still two-handed");

    mod::reset_physical_melee_gesture(&state);
    right.squeeze.current = 1.0F;
    left.squeeze.current = 1.0F;
    right.grip.pose.position = {0.20F, 0.0F, 0.0F};
    left.grip.pose.position = {0.25F, 0.0F, 0.0F};
    snapshot.frame.actions.sequence = 27;
    snapshot.publication_milliseconds = 3'700;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 3'701, &state, true) == Gesture::none &&
              !state.pose_was_valid,
          "overlapping hands cannot define a stable rifle thrust axis");

    mod::reset_physical_melee_gesture(&state);
    right.grip.pose.position = {0.20F, 0.0F, 0.0F};
    left.grip.pose.position = {1.20F, 0.0F, 0.0F};
    snapshot.frame.actions.sequence = 28;
    snapshot.publication_milliseconds = 3'750;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 3'751, &state, true) == Gesture::none &&
              !state.pose_was_valid,
          "implausibly separated hands cannot define a rifle thrust axis");

    mod::reset_physical_melee_gesture(&state);
    right.grip.pose.position = {0.20F, 0.0F, 0.0F};
    left.grip.pose.position = {0.55F, 0.0F, 0.0F};
    snapshot.frame.actions.sequence = 29;
    snapshot.publication_milliseconds = 3'800;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 3'801, &state, true) == Gesture::none,
          "moderate two-hand thrust fixture establishes a baseline");
    right.grip.pose.position.x += 0.08F;
    left.grip.pose.position.x += 0.08F;
    snapshot.frame.actions.sequence = 30;
    snapshot.publication_milliseconds = 3'920;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 3'921, &state, true) ==
              Gesture::two_hand_thrust,
          "an eight-centimetre 120 ms rifle thrust does not require violent speed");

    mod::reset_physical_melee_gesture(&state);
    snapshot.frame.actions.sequence = 31;
    snapshot.publication_milliseconds = 4'000;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 4'001, &state, true) == Gesture::none,
          "two-hand support-only fixture establishes a fresh baseline");
    snapshot.frame.actions.sequence = 32;
    snapshot.publication_milliseconds = 4'080;
    left.grip.pose.position.x += 0.20F;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 4'081, &state, true) == Gesture::none,
          "moving only the support hand cannot synthesize a rifle thrust");

    mod::reset_physical_melee_gesture(&state);
    right.grip.pose.position = {0.20F, 0.0F, 0.0F};
    left.grip.pose.position = {0.55F, 0.0F, 0.0F};
    snapshot.frame.actions.sequence = 33;
    snapshot.publication_milliseconds = 4'200;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 4'201, &state, true) == Gesture::none,
          "asymmetric steering fixture establishes a fresh baseline");
    right.grip.pose.position.x += 0.025F;
    left.grip.pose.position.x += 0.10F;
    snapshot.frame.actions.sequence = 34;
    snapshot.publication_milliseconds = 4'280;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 4'281, &state, true) == Gesture::none,
          "asymmetric support-hand steering is not a rigid rifle thrust");

    mod::reset_physical_melee_gesture(&state);
    snapshot.frame.actions.sequence = 40;
    snapshot.publication_milliseconds = 5'000;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 5'001, &state, true) == Gesture::none,
          "two-hand reverse-motion fixture establishes a fresh baseline");
    snapshot.frame.actions.sequence = 41;
    snapshot.publication_milliseconds = 5'080;
    right.grip.pose.position.x -= 0.10F;
    left.grip.pose.position.x -= 0.10F;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 5'081, &state, true) == Gesture::none,
          "pulling a double-gripped rifle backward cannot trigger melee");

    mod::reset_physical_melee_gesture(&state);
    snapshot.frame.actions.sequence = 50;
    snapshot.publication_milliseconds = 6'000;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 6'001, &state, true) == Gesture::none,
          "two-hand lateral-motion fixture establishes a fresh baseline");
    snapshot.frame.actions.sequence = 51;
    snapshot.publication_milliseconds = 6'080;
    right.grip.pose.position.y += 0.10F;
    left.grip.pose.position.y += 0.10F;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 6'081, &state, true) == Gesture::none,
          "a rigid sideways rifle movement is not mistaken for a thrust");

    mod::reset_physical_melee_gesture(&state);
    right.trigger = {true, 1.0F, true};
    snapshot.frame.actions.sequence = 60;
    snapshot.publication_milliseconds = 7'000;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 7'001, &state, true) == Gesture::none,
          "two-hand firing fixture establishes a fresh baseline");
    snapshot.frame.actions.sequence = 61;
    snapshot.publication_milliseconds = 7'080;
    right.grip.pose.position.x += 0.10F;
    left.grip.pose.position.x += 0.10F;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 7'081, &state, true) == Gesture::none,
          "firing still blocks an otherwise valid two-hand thrust");

    mod::reset_physical_melee_gesture(&state);
    right.trigger = {};
    right.squeeze.current = 1.0F;
    left.squeeze.current = 1.0F;
    right.grip.pose.position = {0.20F, 0.0F, 0.0F};
    left.grip.pose.position = {0.55F, 0.0F, 0.0F};
    snapshot.frame.actions.sequence = 70;
    snapshot.publication_milliseconds = 8'000;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 8'001, &state, true) == Gesture::none,
          "grip reacquisition fixture establishes a baseline");
    left.squeeze.current = 0.10F;
    snapshot.frame.actions.sequence = 71;
    snapshot.publication_milliseconds = 8'020;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 8'021, &state, true) == Gesture::none &&
              !state.pose_was_valid,
          "losing either rifle grip cancels an in-progress thrust");
    left.squeeze.current = 0.80F;
    snapshot.frame.actions.sequence = 72;
    snapshot.publication_milliseconds = 8'040;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 8'041, &state, true) == Gesture::none,
          "reacquiring the support grip rebaselines instead of attacking");
    right.grip.pose.position.x += 0.08F;
    left.grip.pose.position.x += 0.08F;
    snapshot.frame.actions.sequence = 73;
    snapshot.publication_milliseconds = 8'120;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 8'121, &state, true) ==
              Gesture::two_hand_thrust,
          "a fresh deliberate thrust works after grip reacquisition");

    mod::reset_physical_melee_gesture(&state);
    right.squeeze = {};
    left.squeeze = {};
    right.grip.pose.position = {0.20F, 0.0F, 0.0F};
    snapshot.frame.head_center.position = {};
    snapshot.frame.actions.sequence = 80;
    snapshot.publication_milliseconds = 9'000;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 9'001, &state) == Gesture::none,
          "head-only free-hand fixture establishes a baseline");
    snapshot.frame.head_center.position.x -= 0.10F;
    snapshot.frame.actions.sequence = 81;
    snapshot.publication_milliseconds = 9'080;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 9'081, &state) == Gesture::none,
          "leaning the head away from a stationary free hand cannot trigger melee");

    mod::reset_physical_melee_gesture(&state);
    right.squeeze = {true, 1.0F, true};
    left.squeeze = {true, 1.0F, true};
    right.grip.pose.position = {0.20F, 0.0F, 0.0F};
    left.grip.pose.position = {0.55F, 0.0F, 0.0F};
    snapshot.frame.head_center.position = {};
    snapshot.frame.actions.sequence = 82;
    snapshot.publication_milliseconds = 9'200;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 9'201, &state, true) == Gesture::none,
          "head-only two-hand fixture establishes a baseline");
    snapshot.frame.head_center.position.x -= 0.10F;
    snapshot.frame.actions.sequence = 83;
    snapshot.publication_milliseconds = 9'280;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 9'281, &state, true) == Gesture::none,
          "leaning away from a stationary double-gripped rifle cannot synthesize a thrust");

    mod::reset_physical_melee_gesture(&state);
    right.squeeze = {};
    left.squeeze = {};
    right.grip.pose.position = {0.20F, 0.0F, 0.0F};
    snapshot.frame.head_center.position = {};
    snapshot.frame.actions.sequence = 84;
    snapshot.publication_milliseconds = 9'400;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 9'401, &state) == Gesture::none,
          "tracking relocation fixture establishes a baseline");
    right.grip.pose.position.x += 0.45F;
    snapshot.frame.actions.sequence = 85;
    snapshot.publication_milliseconds = 9'480;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 9'481, &state) == Gesture::none,
          "an implausible one-frame controller relocation rebaselines instead of attacking");

    mod::reset_physical_melee_gesture(&state);
    right.grip.pose.position = {0.20F, 0.0F, 0.0F};
    snapshot.frame.actions.sequence = 100;
    snapshot.publication_milliseconds = 10'000;
    check(mod::update_physical_melee_gesture(
              true, snapshot, 10'001, &state) == Gesture::none,
          "long jitter soak establishes a baseline");
    for (std::uint64_t sample = 1; sample <= 900; ++sample) {
        snapshot.frame.actions.sequence = 100 + sample;
        snapshot.publication_milliseconds = 10'000 + sample * 11;
        const float direction = sample % 2 == 0 ? 1.0F : -1.0F;
        right.grip.pose.position = {
            0.20F + direction * 0.009F,
            direction * 0.006F,
            direction * 0.004F,
        };
        check(mod::update_physical_melee_gesture(
                  true, snapshot,
                  snapshot.publication_milliseconds + 1, &state) ==
                  Gesture::none,
              "ten seconds of controller jitter cannot randomly activate melee");
    }

    mod::reset_physical_melee_gesture(&state);
    check(!state.input_was_owned && !state.pose_was_valid &&
              !state.right_grip_latched && !state.left_grip_latched &&
              !state.two_hand_weapon_was_held &&
              state.gesture_origin_milliseconds == 0 &&
              state.last_action_sequence == 0 &&
              state.cooldown_until_milliseconds == 0 &&
              state.last_trigger_relative_travel_meters == 0.0F &&
              state.last_trigger_world_travel_meters == 0.0F &&
              state.last_trigger_speed_meters_per_second == 0.0F &&
              state.last_trigger_outward_travel_meters == 0.0F,
          "explicit physical-melee reset clears tracking history");
}

void test_held_pistol_melee_gesture() {
    using Gesture = mod::PhysicalMeleeGesture;
    constexpr std::uint64_t pistol_identity = 0x123400000007ULL;
    auto snapshot = valid_snapshot();
    auto& right = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    auto& left = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Left)];
    mod::PhysicalMeleeState state{};
    const auto reset = [&] {
        snapshot = valid_snapshot();
        snapshot.frame.actions.sequence = 1;
        right.grip.active = true;
        right.grip.position_valid = true;
        right.grip.pose.position = {0.20F, -0.25F, -0.45F};
        right.squeeze = {true, 1.0F, true};
        left.squeeze = {true, 0.0F, false};
        mod::reset_physical_melee_gesture(&state);
        check(mod::update_physical_melee_gesture(
                  true, snapshot, 1'001, &state, false, pistol_identity) ==
                  Gesture::none,
              "a freshly gripped pistol establishes a motion baseline");
    };
    const auto sample = [&](const std::uint64_t elapsed,
                            const std::uint64_t identity = 0x123400000007ULL,
                            const bool owned = true) {
        ++snapshot.frame.actions.sequence;
        snapshot.publication_milliseconds += elapsed;
        return mod::update_physical_melee_gesture(
            owned, snapshot, snapshot.publication_milliseconds + 1,
            &state, false, identity);
    };

    reset();
    right.grip.pose.position.z -= 0.10F;
    check(sample(80) == Gesture::right_hand_pistol_strike,
          "a forward stab works while the right hand keeps the pistol gripped");
    check(mod::update_physical_melee_gesture(
              true, snapshot, snapshot.publication_milliseconds + 1,
              &state, false, pistol_identity) == Gesture::none,
          "repeated usercmds do not duplicate a pistol strike");
    right.grip.pose.position.z -= 0.10F;
    check(sample(80) == Gesture::none,
          "held-pistol strikes retain the existing melee cooldown");

    reset();
    check(sample(120) == Gesture::none,
          "a stationary pistol does not trigger during the original sample window");
    right.grip.pose.position.z -= 0.07F;
    check(sample(20) == Gesture::right_hand_pistol_strike,
          "idle time cannot dilute a fast jab near the old window boundary");
    right.grip.pose.position.z -= 0.08F;
    check(sample(20) == Gesture::none,
          "the rest of a boundary-crossing pistol jab does not duplicate the attack");

    // Exercise different idle phases and rates with a sampled 1.2 m/s thrust,
    // rather than placing the entire gesture directly after a fresh baseline.
    // A 150 ms fixed origin used to miss some of these otherwise identical jabs.
    for (const std::uint64_t frame_ms : {8ULL, 11ULL, 16ULL, 22ULL, 33ULL}) {
        for (std::uint64_t phase = 0; phase < 150; ++phase) {
            reset();
            for (std::uint64_t idle = 0; idle < 300 + phase; idle += frame_ms) {
                check(sample(frame_ms) == Gesture::none,
                      "stationary held-pistol idle remains quiet at every phase");
            }
            std::uint32_t attacks = 0;
            for (std::uint64_t movement = 0; movement < 75; movement += frame_ms) {
                right.grip.pose.position.z -=
                    static_cast<float>(frame_ms) * 0.0012F;
                const auto gesture = sample(frame_ms);
                attacks += gesture == Gesture::right_hand_pistol_strike ? 1 : 0;
                check(gesture == Gesture::none ||
                          gesture == Gesture::right_hand_pistol_strike,
                      "a held-pistol phase test never changes melee ownership");
            }
            check(attacks == 1,
                  "a sampled pistol jab triggers exactly once independent of idle phase and frame rate");
            check(sample(frame_ms) == Gesture::none,
                  "holding the pistol after the phase-swept jab remains quiet");
        }
    }

    // GetTickCount64 commonly advances in ~16 ms ticks even when distinct
    // OpenXR publications arrive at 90 or 120 Hz. Equal clock values are not
    // backwards time, a lost pose, or permission to duplicate an attack.
    for (const std::uint64_t frame_us : {11'111ULL, 8'333ULL}) {
        for (std::uint64_t phase = 0; phase < 16; ++phase) {
            reset();
            std::uint64_t real_time_us = phase * 1'000;
            const auto quantized_sample = [&]() {
                real_time_us += frame_us;
                const auto clock_ms = 1'000 + (real_time_us / 16'000) * 16;
                return sample(clock_ms - snapshot.publication_milliseconds);
            };
            for (std::uint64_t idle = 0; idle < 36; ++idle) {
                check(quantized_sample() == Gesture::none,
                      "quantized-clock pistol idle cannot synthesize melee");
            }
            std::uint32_t attacks = 0;
            for (std::uint64_t movement = 0; movement < 9; ++movement) {
                right.grip.pose.position.z -=
                    static_cast<float>(frame_us) * 0.0000012F;
                const auto gesture = quantized_sample();
                attacks += gesture == Gesture::right_hand_pistol_strike ? 1 : 0;
                check(gesture == Gesture::none ||
                          gesture == Gesture::right_hand_pistol_strike,
                      "quantized-clock stab keeps the held-pistol gesture type");
            }
            check(attacks == 1,
                  "90 and 120 Hz pistol jabs work once across all 16 ms clock phases");
        }
    }

    reset();
    right.grip.pose.position.z -= 0.02F;
    check(sample(0) == Gesture::none,
          "a fresh action on the initial pistol clock tick defers without resetting origin");
    right.grip.pose.position.z -= 0.02F;
    check(sample(16) == Gesture::right_hand_pistol_strike,
          "the next clock tick retains movement from the equal-time initial action");
    right.grip.pose.position.z -= 0.10F;
    check(sample(0) == Gesture::none,
          "an equal-clock action after a pistol strike cannot duplicate melee");

    reset();
    check(sample(32) == Gesture::none,
          "backwards-clock pistol fixture establishes later history");
    snapshot.publication_milliseconds -= 16;
    right.grip.pose.position.z -= 0.10F;
    check(sample(0) == Gesture::none,
          "genuinely backwards pistol time clears history instead of striking");
    check(sample(16) == Gesture::none,
          "recovering the clock cannot replay discarded backwards-time motion");

    // Long, high-rate input fills and wraps the bounded history without
    // allocating, weakening the speed threshold, or accumulating pose noise.
    for (const std::uint64_t frame_ms : {1ULL, 8ULL, 16ULL, 33ULL}) {
        reset();
        for (std::uint64_t index = 0; index < 500; ++index) {
            right.grip.pose.position.z -=
                static_cast<float>(frame_ms) * 0.0004F;
            check(sample(frame_ms) == Gesture::none,
                  "sliding history never turns a slow pistol extension into a stab");
        }
    }

    reset();
    // Turning to the right rotates the pointing ray towards positive X.
    right.aim.pose.orientation = {0.0F, -0.70710678F, 0.0F, 0.70710678F};
    check(sample(120) == Gesture::none,
          "a rotated idle pistol starts without melee");
    right.grip.pose.position.x += 0.08F;
    check(sample(40) == Gesture::right_hand_pistol_strike,
          "a phase-independent held-pistol stab follows the rotated pointing ray");

    reset();
    right.grip.pose.position.z -= 0.10F;
    check(sample(151) == Gesture::none,
          "a gap beyond the motion window cannot be bridged into a pistol strike");
    check(sample(11) == Gesture::none,
          "a stale input gap leaves no pre-gap history to replay");

    for (const bool sideways : {false, true}) {
        for (std::uint64_t phase = 0; phase < 150; ++phase) {
            reset();
            for (std::uint64_t idle = 0; idle < 300 + phase; idle += 11) {
                check(sample(11) == Gesture::none,
                      "negative pistol phase tests remain quiet before movement");
            }
            for (std::uint64_t movement = 0; movement < 8; ++movement) {
                if (sideways) {
                    right.grip.pose.position.x += 0.02F;
                } else {
                    right.grip.pose.position.y += 0.02F;
                }
                check(sample(11) == Gesture::none,
                      "raising or sweeping a pistol never becomes a stab at a window boundary");
            }
        }
    }

    reset();
    right.squeeze.current = 0.50F;
    right.grip.pose.position.z -= 0.10F;
    check(sample(80) == Gesture::right_hand_pistol_strike,
          "a held pistol accepts a deliberate stab through grip hysteresis");

    reset();
    right.squeeze.current = 0.0F;
    right.grip.pose.position.z -= 0.10F;
    check(sample(80) == Gesture::none,
          "dropping a pistol cannot synthesize a melee strike");
    right.squeeze.current = 1.0F;
    right.grip.pose.position.z -= 0.10F;
    check(sample(80) == Gesture::none,
          "regripping a moving pistol rebaselines instead of striking");
    right.grip.pose.position.z -= 0.10F;
    check(sample(80) == Gesture::right_hand_pistol_strike,
          "a subsequent deliberate stab works after reacquisition");

    reset();
    right.grip.pose.position.z -= 0.10F;
    check(sample(80, 0) == Gesture::none,
          "rifles, unknown weapons and reserved reloads retain single-grip suppression");
    right.grip.pose.position.z -= 0.10F;
    check(sample(80) == Gesture::none,
          "returning from reload establishes a fresh pistol baseline");
    right.grip.pose.position.z -= 0.10F;
    check(sample(80, pistol_identity + 1) == Gesture::none,
          "switching pistol identity cannot inherit an in-progress stab");
    right.grip.pose.position.z -= 0.06F;
    check(sample(40, pistol_identity + 1) == Gesture::right_hand_pistol_strike,
          "a fresh pistol stab works after eligibility and weapon identity recovery");

    reset();
    right.trigger = {true, 1.0F, true};
    right.grip.pose.position.z -= 0.10F;
    check(sample(80) == Gesture::none,
          "firing a held pistol blocks melee");
    right.trigger.current = 0.0F;
    check(sample(20) == Gesture::none,
          "releasing the trigger cannot replay motion accumulated while firing");

    reset();
    left.squeeze.current = 1.0F;
    right.grip.pose.position.z -= 0.10F;
    check(sample(80) == Gesture::none,
          "adding a second grip cannot trigger a single-hand pistol stab");
    left.squeeze.current = 0.0F;
    check(sample(20) == Gesture::none,
          "releasing pistol support establishes a fresh single-hand baseline");

    reset();
    left.squeeze.active = false;
    right.grip.pose.position.z -= 0.10F;
    check(sample(80) == Gesture::none,
          "unknown left-grip state cannot enable the held-pistol exception");
    reset();
    right.aim.orientation_valid = false;
    right.grip.pose.position.z -= 0.10F;
    check(sample(80) == Gesture::none,
          "a held pistol needs a valid pointing ray to recognize a forward stab");

    reset();
    right.grip.pose.position.x += 0.10F;
    check(sample(80) == Gesture::none,
          "sweeping a gripped pistol sideways is not a forward stab");
    reset();
    right.grip.pose.position.y += 0.10F;
    check(sample(80) == Gesture::none,
          "raising a pistol toward the sights does not trigger melee");
    reset();
    right.grip.pose.position.z -= 0.08F;
    check(sample(140) == Gesture::none,
          "slowly extending a held pistol does not trigger melee");

    reset();
    snapshot.frame.head_center.position.z += 0.10F;
    check(sample(80) == Gesture::none,
          "leaning away from a stationary gripped pistol cannot trigger melee");
    reset();
    right.grip.pose.position.z -= 0.45F;
    check(sample(40) == Gesture::none,
          "a held-pistol tracking relocation is not a strike");
    reset();
    right.grip.position_valid = false;
    right.grip.pose.position.z -= 0.20F;
    check(sample(20) == Gesture::none,
          "lost pistol tracking cancels motion history");
    right.grip.position_valid = true;
    check(sample(20) == Gesture::none,
          "recovered pistol tracking rebaselines without melee");
    right.grip.pose.position.z -= 0.06F;
    check(sample(40) == Gesture::right_hand_pistol_strike,
          "a fresh deliberate pistol stab works after tracking recovery");
    reset();
    right.grip.pose.position.z -= 0.10F;
    check(sample(80, pistol_identity, false) == Gesture::none,
          "menus or focus loss cannot trigger a held-pistol strike");
    check(sample(20) == Gesture::none,
          "regaining gameplay cannot replay a pending pistol strike");

    reset();
    for (std::uint64_t index = 0; index < 900; ++index) {
        const float sign = index % 2 == 0 ? 1.0F : -1.0F;
        right.grip.pose.position = {
            0.20F + sign * 0.009F, -0.25F + sign * 0.006F,
            -0.45F + sign * 0.004F};
        check(sample(11) == Gesture::none,
              "held-pistol jitter does not accumulate into random melee");
    }
    mod::reset_physical_melee_gesture(&state);
    check(state.held_pistol_identity == 0 &&
              state.held_pistol_sample_count == 0 &&
              state.held_pistol_next_sample == 0,
          "reset retires the held-pistol identity and bounded motion history");
}

void test_automatic_body_yaw_sync_requires_sprint_movement() {
    check(!mod::automatic_body_yaw_sync_allowed(true, true, false),
          "ordinary gameplay never discharges hidden body yaw on a grip edge");
    check(mod::automatic_body_yaw_sync_allowed(true, true, true),
          "active sprint movement may synchronize hidden body yaw");
    check(!mod::automatic_body_yaw_sync_allowed(false, true, false),
          "menus and other non-gameplay ownership block automatic body yaw");
    check(!mod::automatic_body_yaw_sync_allowed(true, false, true),
          "stale controller frames block automatic body yaw");
}

void test_controller_frame_broker() {
    mod::clear_controller_frame();
    mod::ControllerFrameSnapshot read{};
    check(!mod::read_controller_frame(&read), "cleared broker has no frame");

    xr::FrameState frame{};
    frame.frame_id = 44;
    frame.actions.sequence = 45;
    xr::Posef anchor{};
    anchor.orientation = {0.0F, 0.0F, 0.0F, 1.0F};
    mod::publish_controller_frame(frame, anchor);
    check(mod::read_controller_frame(&read), "published broker frame readable");
    check(read.frame.frame_id == 44 && read.frame.actions.sequence == 45 &&
              read.generation != 0 && read.publication_milliseconds != 0,
          "broker returns one consistent snapshot");

    const xr::Quaternionf body_aligned{
        0.0F, 0.70710678F, 0.0F, 0.70710678F};
    check(mod::rebase_controller_frame_tracking_anchor(
              read.generation, read.frame.frame_id,
              read.tracking_anchor.orientation, body_aligned),
          "exact controller generation accepts body-yaw anchor rebase");
    check(mod::read_controller_frame(&read) &&
              near(read.tracking_anchor.orientation.y, body_aligned.y),
          "controller consumers observe body-aligned anchor immediately");
    xr::Quaternionf consumed{};
    check(mod::consume_controller_tracking_anchor_rebase(&consumed) &&
              near(consumed.y, body_aligned.y) &&
              !mod::consume_controller_tracking_anchor_rebase(&consumed),
          "Present consumes one queued long-lived anchor update");
    check(!mod::rebase_controller_frame_tracking_anchor(
              read.generation - 1, read.frame.frame_id,
              read.tracking_anchor.orientation, body_aligned),
          "stale controller generation cannot rewrite the anchor");

    const xr::Quaternionf recovered_alignment{
        0.0F, -0.38268343F, 0.0F, 0.92387953F};
    check(mod::rebase_controller_frame_tracking_anchor(
              read.generation, read.frame.frame_id,
              read.tracking_anchor.orientation, recovered_alignment),
          "exact controller generation queues a recovery-path anchor rebase");
    mod::clear_controller_frame();
    check(!mod::read_controller_frame(&read), "broker clear invalidates frame");
    check(mod::consume_controller_tracking_anchor_rebase(&consumed) &&
              near(consumed.y, recovered_alignment.y),
          "temporary frame clear preserves the queued body-yaw transfer");

    mod::publish_controller_frame(frame, anchor);
    check(mod::read_controller_frame(&read) &&
              mod::rebase_controller_frame_tracking_anchor(
                  read.generation, read.frame.frame_id,
                  read.tracking_anchor.orientation, body_aligned),
          "full-teardown fixture queues an anchor rebase");
    mod::discard_controller_tracking_anchor_rebase();
    check(!mod::consume_controller_tracking_anchor_rebase(&consumed),
          "full XR teardown discards a stale queued anchor transfer");
    mod::clear_controller_frame();
    check(!mod::read_controller_frame(nullptr), "broker rejects null reader");
}

void test_controller_publication_owns_weapon_filter() {
    mod::clear_controller_frame();
    xr::Posef anchor{};
    anchor.orientation.w = 1.0F;
    xr::FrameState frame{};
    frame.frame_id = 100;
    frame.views_valid = true;
    frame.actions.focused = true;
    frame.actions.sequence = 100;
    auto& right = frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    right.grip.active = true;
    right.grip.position_valid = true;
    right.grip.position_tracked = true;
    right.grip.pose.position = {0.0F, 0.0F, 0.0F};
    right.aim.active = true;
    right.aim.orientation_valid = true;
    right.aim.orientation_tracked = true;
    right.aim.pose.orientation.w = 1.0F;
    mod::publish_controller_frame(frame, anchor);

    constexpr float kHalfSqrtTwo = 0.70710678118F;
    right.grip.pose.position = {0.20F, -0.10F, 0.30F};
    right.aim.pose.orientation = {
        0.0F, kHalfSqrtTwo, 0.0F, kHalfSqrtTwo};
    for (int publication = 1; publication <= 4; ++publication) {
        frame.frame_id += 1;
        frame.actions.sequence += 1;
        mod::publish_controller_frame(frame, anchor);
    }
    mod::ControllerFrameSnapshot filtered{};
    check(mod::read_controller_frame(&filtered),
          "publication-owned filter snapshot is readable");
    const auto& pose = filtered.weapon_poses[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    const float expected_position_response = 1.0F - std::pow(0.55F, 4.0F);
    check(pose.valid &&
              near(pose.filtered_grip_position.x,
                   0.20F * expected_position_response) &&
              near(pose.filtered_grip_position.y,
                   -0.10F * expected_position_response) &&
              near(pose.filtered_grip_position.z,
                   0.30F * expected_position_response),
          "N through N+4 publications apply four fixed COD4 position steps");
    xr::Quaternionf expected_orientation{};
    expected_orientation.w = 1.0F;
    for (int publication = 0; publication < 4; ++publication) {
        expected_orientation.y +=
            (kHalfSqrtTwo - expected_orientation.y) * 0.55F;
        expected_orientation.w +=
            (kHalfSqrtTwo - expected_orientation.w) * 0.55F;
        const float length = std::sqrt(
            expected_orientation.y * expected_orientation.y +
            expected_orientation.w * expected_orientation.w);
        expected_orientation.y /= length;
        expected_orientation.w /= length;
    }
    check(near(pose.filtered_aim_orientation.y,
               expected_orientation.y) &&
              near(pose.filtered_aim_orientation.w,
                   expected_orientation.w),
          "N through N+4 publications match sequential COD4 orientation steps");

    const auto immutable = pose;
    mod::ControllerFrameSnapshot repeated{};
    check(mod::read_controller_frame(&repeated),
          "repeated filtered publication read succeeds");
    const auto& repeated_pose = repeated.weapon_poses[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    check(std::memcmp(&immutable, &repeated_pose, sizeof(immutable)) == 0,
          "repeated consumers receive bit-identical filtered pose");

    auto invalid = frame;
    invalid.frame_id += 1;
    invalid.actions.sequence += 1;
    invalid.actions.hands[static_cast<std::uint32_t>(xr::Hand::Right)]
        .grip.pose.position.x = 5.0F;
    mod::publish_controller_frame(invalid, anchor);
    mod::ControllerFrameSnapshot rejected{};
    check(mod::read_controller_frame(&rejected),
          "raw relocalized publication remains available to non-weapon input");
    check(!rejected.weapon_poses[
               static_cast<std::uint32_t>(xr::Hand::Right)].valid &&
              rejected.frame.actions.hands[
                  static_cast<std::uint32_t>(xr::Hand::Right)]
                  .grip.pose.position.x == 5.0F,
          "relocalized weapon pose fails closed without changing raw frame");
    invalid.frame_id += 1;
    invalid.actions.sequence += 1;
    mod::publish_controller_frame(invalid, anchor);
    check(mod::read_controller_frame(&rejected) &&
              !rejected.weapon_poses[
                   static_cast<std::uint32_t>(xr::Hand::Right)].valid,
          "sustained relocalization remains invalid without retirement reset");
    frame.frame_id = invalid.frame_id + 1;
    frame.actions.sequence = invalid.actions.sequence + 1;
    mod::publish_controller_frame(frame, anchor);
    mod::ControllerFrameSnapshot recovered{};
    check(mod::read_controller_frame(&recovered),
          "plausible publication after relocalization is readable");
    const auto& recovered_pose = recovered.weapon_poses[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    const float expected_recovered_x =
        immutable.filtered_grip_position.x +
        (0.20F - immutable.filtered_grip_position.x) * 0.45F;
    check(recovered_pose.valid &&
              near(recovered_pose.filtered_grip_position.x,
                   expected_recovered_x),
          "normal recovery after one bad sample preserves accepted history");

    mod::clear_controller_frame();
    frame.frame_id += 1;
    frame.actions.sequence += 1;
    right.grip.pose.position.x = 0.10F;
    mod::publish_controller_frame(frame, anchor);
    mod::ControllerFrameSnapshot after_transient_clear{};
    check(mod::read_controller_frame(&after_transient_clear),
          "publication after transient clear is readable");
    const auto& transient_pose = after_transient_clear.weapon_poses[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    check(transient_pose.valid &&
              near(transient_pose.filtered_grip_position.x,
                   expected_recovered_x +
                       (0.10F - expected_recovered_x) * 0.45F),
          "transient broker clear preserves producer filter history");

    mod::reset_controller_weapon_publication_filters();
    frame.frame_id += 1;
    frame.actions.sequence += 1;
    right.grip.pose.position.x = 5.0F;
    mod::publish_controller_frame(frame, anchor);
    mod::ControllerFrameSnapshot after_retirement_reset{};
    check(mod::read_controller_frame(&after_retirement_reset) &&
              near(after_retirement_reset.weapon_poses[
                       static_cast<std::uint32_t>(xr::Hand::Right)]
                       .filtered_grip_position.x, 5.0F),
          "explicit held-weapon retirement reset authorizes raw reseed");

    mod::discard_controller_tracking_anchor_rebase();
    frame.frame_id += 1;
    frame.actions.sequence += 1;
    right.grip.pose.position.x = 4.0F;
    mod::publish_controller_frame(frame, anchor);
    mod::ControllerFrameSnapshot after_teardown{};
    check(mod::read_controller_frame(&after_teardown) &&
              near(after_teardown.weapon_poses[
                       static_cast<std::uint32_t>(xr::Hand::Right)]
                       .filtered_grip_position.x, 4.0F),
          "full XR teardown resets producer filter history");

    frame.frame_id += 1;
    frame.actions.sequence += 1;
    right.grip.position_tracked = false;
    right.aim.orientation_tracked = false;
    right.grip.pose.position.x = 4.05F;
    mod::publish_controller_frame(frame, anchor);
    mod::ControllerFrameSnapshot inferred{};
    check(mod::read_controller_frame(&inferred) &&
              inferred.weapon_poses[
                  static_cast<std::uint32_t>(xr::Hand::Right)].valid,
          "valid inferred OpenXR pose remains available to the weapon like COD4");
    mod::clear_controller_frame();
}

void test_controller_publication_adaptive_quiet_aim() {
    mod::reset_controller_weapon_publication_filters();
    mod::clear_controller_frame();

    xr::Posef anchor{};
    anchor.orientation.w = 1.0F;
    xr::FrameState frame{};
    frame.frame_id = 700;
    frame.views_valid = true;
    frame.actions.focused = true;
    frame.actions.sequence = 700;
    auto& right = frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    right.grip.active = true;
    right.grip.position_valid = true;
    right.grip.position_tracked = true;
    right.aim.active = true;
    right.aim.orientation_valid = true;
    right.aim.orientation_tracked = true;
    right.aim.pose.orientation.w = 1.0F;
    mod::publish_controller_frame(frame, anchor);

    constexpr float kDegreesToRadians = 0.0174532925199F;
    const auto set_yaw = [&](const float degrees) {
        const float half_radians = degrees * kDegreesToRadians * 0.5F;
        right.aim.pose.orientation = {
            0.0F, std::sin(half_radians), 0.0F, std::cos(half_radians)};
    };

    // A stationary Touch controller commonly alternates by about a millimetre
    // and a fraction of a degree. The quiet path must attenuate that motion
    // substantially instead of reproducing it at the COD4 fast response.
    for (int publication = 0; publication < 60; ++publication) {
        const float sign = (publication & 1) == 0 ? 1.0F : -1.0F;
        right.grip.pose.position.x = sign * 0.0010F;
        set_yaw(sign * 0.15F);
        frame.frame_id += 1;
        frame.actions.sequence += 1;
        mod::publish_controller_frame(frame, anchor);
    }
    mod::ControllerFrameSnapshot jittered{};
    check(mod::read_controller_frame(&jittered),
          "adaptive quiet-aim jitter publication is readable");
    const auto right_index =
        static_cast<std::uint32_t>(xr::Hand::Right);
    const auto& jittered_pose = jittered.weapon_poses[right_index];
    constexpr float kRadiansToDegrees = 57.2957795131F;
    const float jittered_yaw = 2.0F * std::atan2(
        jittered_pose.filtered_aim_orientation.y,
        jittered_pose.filtered_aim_orientation.w) * kRadiansToDegrees;
    check(jittered_pose.valid &&
              std::abs(jittered_pose.filtered_grip_position.x) < 0.00020F,
          "alternating one-millimetre noise is attenuated below 0.2 mm");
    check(std::abs(jittered_yaw) < 0.03F,
          "alternating 0.15-degree noise is attenuated below 0.03 degrees");

    // A slow, deliberate sight sweep must escape the quiet response instead
    // of sticking behind the hands. At 90 publications per second, both the
    // positional and angular lag stay well below a visible aiming correction.
    constexpr int kSweepPublications = 90;
    constexpr float kSweepPositionMeters = 0.010F;
    constexpr float kSweepYawDegrees = 10.0F;
    for (int publication = 1; publication <= kSweepPublications;
         ++publication) {
        const float fraction = static_cast<float>(publication) /
            static_cast<float>(kSweepPublications);
        right.grip.pose.position.x = kSweepPositionMeters * fraction;
        set_yaw(kSweepYawDegrees * fraction);
        frame.frame_id += 1;
        frame.actions.sequence += 1;
        mod::publish_controller_frame(frame, anchor);
    }
    mod::ControllerFrameSnapshot swept{};
    check(mod::read_controller_frame(&swept),
          "adaptive quiet-aim slow sweep publication is readable");
    const auto& swept_pose = swept.weapon_poses[right_index];
    const float swept_yaw = 2.0F * std::atan2(
        swept_pose.filtered_aim_orientation.y,
        swept_pose.filtered_aim_orientation.w) * kRadiansToDegrees;
    check(kSweepPositionMeters - swept_pose.filtered_grip_position.x <
              0.0010F,
          "ten-millimetre slow sweep keeps positional lag below one millimetre");
    check(kSweepYawDegrees - swept_yaw < 0.75F,
          "ten-degree slow sweep keeps angular lag below 0.75 degrees");

    mod::clear_controller_frame();
    mod::reset_controller_weapon_publication_filters();
}

void test_current_head_local_publication_lifecycle() {
    mod::clear_controller_frame();
    xr::Posef anchor{};
    xr::FrameState frame{};
    frame.frame_id = 500;
    frame.views_valid = true;
    frame.actions.focused = true;
    frame.actions.sequence = 501;
    frame.head_center.position = {0.10F, 1.65F, -0.05F};
    auto& right = frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    right.grip.active = true;
    right.grip.position_valid = true;
    right.grip.pose.position = {0.28F, 1.34F, -0.48F};
    right.aim.active = true;
    right.aim.orientation_valid = true;

    mod::publish_controller_frame(frame, anchor);
    mod::ControllerFrameSnapshot first{};
    const auto right_index =
        static_cast<std::uint32_t>(xr::Hand::Right);
    check(mod::read_controller_frame(&first) &&
              first.weapon_poses[right_index].current_head_local_valid,
          "complete focused frame seeds current-head-local publication");
    const xr::EnginePose accepted =
        first.weapon_poses[right_index].filtered_current_head_local_pose;

    auto unfocused = frame;
    unfocused.frame_id += 1;
    unfocused.actions.sequence += 1;
    unfocused.actions.focused = false;
    unfocused.head_center.position.x += 0.05F;
    mod::publish_controller_frame(unfocused, anchor);
    mod::ControllerFrameSnapshot rejected{};
    check(mod::read_controller_frame(&rejected) &&
              !rejected.weapon_poses[right_index].current_head_local_valid,
          "unfocused frame cannot publish or advance head-local weapon pose");

    frame.frame_id += 2;
    frame.actions.sequence += 2;
    mod::publish_controller_frame(frame, anchor);
    mod::ControllerFrameSnapshot recovered{};
    check(mod::read_controller_frame(&recovered) &&
              recovered.weapon_poses[right_index].current_head_local_valid,
          "focused frame recovers after rejected publication");
    const auto& recovered_pose =
        recovered.weapon_poses[right_index].filtered_current_head_local_pose;
    check(near(recovered_pose.position.x, accepted.position.x) &&
              near(recovered_pose.position.y, accepted.position.y) &&
              near(recovered_pose.position.z, accepted.position.z),
          "rejected frame leaves head-local position history unchanged");

    std::this_thread::sleep_for(std::chrono::milliseconds(
        mod::kCurrentHeadLocalMaximumFrameAgeMilliseconds + 25));
    frame.frame_id += 1;
    frame.actions.sequence += 1;
    right.grip.pose.position.x += 0.12F;
    right.aim.pose.orientation = {
        0.0F, 0.2588190451F, 0.0F, 0.9659258263F};
    xr::EnginePose expected_after_gap{};
    check(mod::current_head_local_controller_pose(
              frame.head_center, right, &expected_after_gap),
          "raw head-local pose after publication gap is valid");
    mod::publish_controller_frame(frame, anchor);
    mod::ControllerFrameSnapshot after_gap{};
    check(mod::read_controller_frame(&after_gap) &&
              after_gap.weapon_poses[right_index].current_head_local_valid,
          "first complete frame after a long gap reseeds immediately");
    const auto& gap_pose =
        after_gap.weapon_poses[right_index].filtered_current_head_local_pose;
    check(near(gap_pose.position.x, expected_after_gap.position.x) &&
              near(gap_pose.position.y, expected_after_gap.position.y) &&
              near(gap_pose.position.z, expected_after_gap.position.z),
          "long publication gap does not leave the filter permanently stale");

    mod::clear_controller_frame();
    frame.frame_id += 1;
    frame.actions.sequence += 1;
    right.grip.pose.position = {-0.22F, 1.10F, -0.70F};
    right.aim.pose.orientation = {};
    xr::EnginePose expected_after_clear{};
    check(mod::current_head_local_controller_pose(
              frame.head_center, right, &expected_after_clear),
          "raw head-local pose after broker clear is valid");
    mod::publish_controller_frame(frame, anchor);
    mod::ControllerFrameSnapshot after_clear{};
    check(mod::read_controller_frame(&after_clear) &&
              after_clear.weapon_poses[right_index].current_head_local_valid,
          "first complete frame after broker clear reseeds immediately");
    const auto& clear_pose =
        after_clear.weapon_poses[right_index].filtered_current_head_local_pose;
    check(near(clear_pose.position.x, expected_after_clear.position.x) &&
              near(clear_pose.position.y, expected_after_clear.position.y) &&
              near(clear_pose.position.z, expected_after_clear.position.z),
          "broker clear discards stale current-head-local filter history");
    mod::clear_controller_frame();
}

}  // namespace

int main() {
    test_identity_aim_and_trigger();
    test_mounted_trigger_route_without_handheld_grip();
    test_camera_axis_composition();
    test_controller_aim_is_relative_to_tracking_anchor();
    test_hmd_oriented_movement_and_buttons();
    test_b_is_not_crouch_when_it_is_the_pause_fallback();
    test_campaign_controls_never_suspend_locomotion();
    test_grips_are_reserved_for_physical_interactions();
    test_melee_charge_is_unconditionally_suppressed();
    test_right_grip_does_not_guess_a_grenade_binding();
    test_focus_staleness_and_invalid_pose_fail_closed();
    test_high_level_actions_do_not_guess_usercmd_fields();
    test_left_support_grip_enters_native_ads();
    test_multiplayer_command_prefix_and_active_state();
    test_horizontal_snap_turn_hysteresis();
    test_smooth_turn_setting_is_opt_in();
    test_smooth_turn_rate_deadzone_dominance_and_hitch_cap();
    test_smooth_turn_resets_across_input_ownership();
    test_right_stick_stance_ladder_and_jump();
    test_stance_ladder_survives_menu_or_focus_ownership();
    test_click_to_sprint_latch();
    test_snap_turn_requires_exact_gameplay_state_and_no_ui_catcher();
    test_controller_gameplay_input_is_owned_by_gameplay_not_ui();
    test_snap_turn_updates_t4_yaw_and_same_command_camera();
    test_body_yaw_delta_tracks_physical_heading();
    test_automatic_body_yaw_sync_requires_sprint_movement();
    test_physical_melee_gesture();
    test_held_pistol_melee_gesture();
    test_controller_frame_broker();
    test_controller_publication_owns_weapon_filter();
    test_controller_publication_adaptive_quiet_aim();
    test_current_head_local_publication_lifecycle();
    return 0;
}
