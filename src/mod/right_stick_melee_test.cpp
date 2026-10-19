// SPDX-License-Identifier: GPL-3.0-only
#include "input_mapping.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace {
using namespace wawvr;

void require(bool condition, const char* label) {
    if (!condition) throw std::runtime_error(label);
}

mod::ControllerFrameSnapshot fixture() {
    mod::ControllerFrameSnapshot frame{};
    frame.frame.frame_id = 1;
    frame.frame.actions.sequence = 1;
    frame.frame.actions.focused = true;
    frame.frame.actions.menu.active = true;
    frame.frame.views_valid = true;
    frame.frame.head_center.orientation.w = 1.0F;
    frame.tracking_anchor.orientation.w = 1.0F;
    frame.publication_milliseconds = 1'000;
    for (auto& hand : frame.frame.actions.hands) {
        hand.grip.active = true;
        hand.grip.position_valid = true;
        hand.grip.pose.orientation.w = 1.0F;
        hand.aim.active = true;
        hand.aim.orientation_valid = true;
        hand.aim.pose.orientation.w = 1.0F;
        hand.squeeze = {true, 0.0F, false};
        hand.trigger = {true, 0.0F, false};
        hand.stick_click = {true, false, false};
    }
    frame.frame.actions.hands[1].grip.pose.position =
        {0.20F, -0.25F, -0.45F};
    frame.frame.actions.hands[0].grip.pose.position =
        {0.20F, -0.25F, -0.75F};
    return frame;
}

void exact_opt_in_and_rearm() {
    require(!mod::right_stick_melee_from_setting(L""), "missing setting defaults off");
    for (const auto* value : {L"0", L"true", L"yes", L"on", L" 1", L"1 ", L"01", L"11"}) {
        require(!mod::right_stick_melee_from_setting(value), "nonexact setting fails closed");
    }
    require(mod::right_stick_melee_from_setting(L"1"), "exact setting opts in");
    mod::RightStickMeleeState state{};
    xr::BoolActionState click{true, true, true};
    require(!mod::update_right_stick_melee(true, true, false, 1, click, &state),
            "held click at launch does not attack");
    require(!mod::update_right_stick_melee(true, true, false, 2, click, &state),
            "held click cannot repeat");
    click.current = false;
    require(!mod::update_right_stick_melee(true, true, false, 3, click, &state),
            "release arms without attack");
    click.current = true;
    require(mod::update_right_stick_melee(true, true, false, 4, click, &state),
            "fresh deliberate optional click attacks");
    require(!mod::update_right_stick_melee(true, true, false, 4, click, &state),
            "duplicate OpenXR action never repeats attack");
    require(!mod::update_right_stick_melee(true, true, false, 5, click, &state),
            "holding optional binding across frames never repeats");
    require(!mod::update_right_stick_melee(true, false, false, 6, click, &state),
            "ownership loss suppresses click");
    require(!mod::update_right_stick_melee(true, true, false, 7, click, &state),
            "held click on ownership return cannot replay");
    click.current = false;
    require(!mod::update_right_stick_melee(true, true, false, 8, click, &state),
            "release after ownership return rearms");
    click.current = true;
    require(!mod::update_right_stick_melee(true, true, true, 9, click, &state),
            "reload reservation consumes attempted optional click");
    require(!mod::update_right_stick_melee(true, true, false, 10, click, &state),
            "finishing reload with click held cannot replay");
    click.current = false;
    require(!mod::update_right_stick_melee(true, true, false, 11, click, &state),
            "reload exit release rearms");
    click.current = true;
    require(mod::update_right_stick_melee(true, true, false, 12, click, &state),
            "fresh press after reload is available");
    require(!mod::update_right_stick_melee(true, true, false, 1, click, &state),
            "sequence rewind cannot replay held click");
    click.active = false;
    require(!mod::update_right_stick_melee(true, true, false, 2, click, &state),
            "inactive action clears state");
    click.active = true;
    require(!mod::update_right_stick_melee(true, true, false, 3, click, &state),
            "action recovery while held requires a new release");
    require(!mod::update_right_stick_melee(true, true, false, 4, click, nullptr),
            "missing state fails closed");
}

template <typename Command>
void command_routing() {
    auto frame = fixture();
    auto& right = frame.frame.actions.hands[1];
    right.stick_click.current = true;
    Command command{};
    if constexpr (std::is_same_v<Command, t4::UsercmdMp>) {
        command.opaque_after_angles.fill(std::byte{0xA5});
    }
    const auto initial = mod::apply_controller_input(command, frame, {}, 1'001);
    require(!initial.melee_button_held &&
                !t4::has_button(command, t4::UsercmdButton::melee),
            "default mapping emits zero R3 melee in SP and MP");
    command = {};
    const auto explicit_press = mod::apply_controller_input(command, frame, {}, 1'001, true);
    require(explicit_press.melee_button_held &&
                t4::has_button(command, t4::UsercmdButton::melee),
            "explicit verified pulse maps to native melee in SP and MP");
    frame.frame.actions.focused = false;
    command = {};
    require(!mod::apply_controller_input(command, frame, {}, 1'001, true).melee_button_held &&
                !t4::has_button(command, t4::UsercmdButton::melee),
            "even explicit pulse cannot bypass stale or unfocused frame gate");
    frame.frame.actions.focused = true;
    right.stick_click.current = false;
    command = {};
    require(!mod::apply_controller_input(command, frame, {}, 1'001, true).melee_button_held,
            "unheld click cannot receive requested pulse");
    // Native keyboard/mouse bits are not attributable to XR R3 and must not
    // be erased by this option. Physical gestures are added after this stage.
    t4::add_button(command, t4::UsercmdButton::melee);
    static_cast<void>(mod::apply_controller_input(command, frame, {}, 1'001));
    require(t4::has_button(command, t4::UsercmdButton::melee),
            "unrelated native deliberate melee is preserved");
}

template <typename Command>
void mixed_controls_stress(bool smooth) {
    auto frame = fixture();
    auto& right = frame.frame.actions.hands[1];
    auto& left = frame.frame.actions.hands[0];
    mod::RightStickMeleeState melee{};
    mod::PhysicalMeleeState physical{};
    mod::SnapTurnState snap{};
    mod::SmoothTurnState turn{};
    std::uint32_t unexpected = 0;
    std::uint32_t attack_commands = 0;
    std::uint32_t reload_commands = 0;
    std::uint32_t turn_samples = 0;
    for (std::uint64_t step = 0; step < 12'000; ++step) {
        ++frame.frame.frame_id;
        ++frame.frame.actions.sequence;
        frame.publication_milliseconds += 11;
        frame.frame.actions.focused = step % 173 != 0;
        frame.frame.views_valid = step % 181 != 0;
        const bool gameplay = step % 167 != 0;
        const bool stale = step % 191 == 0;
        const auto now = frame.publication_milliseconds + (stale ? 251 : 1);
        right.grip.position_valid = step % 179 != 0;
        right.stick.active = true;
        right.stick.current = {step % 8 < 4 ? 0.95F : 0.0F, 0.0F};
        right.stick_click = {step % 199 != 0, step % 6 < 3, true};
        right.squeeze.current = step % 23 < 17 ? 0.9F : 0.0F;
        left.squeeze.current = step % 29 < 17 ? 0.9F : 0.0F;
        right.trigger.current = step % 13 < 5 ? 0.9F : 0.0F;
        right.primary = {true, step % 37 < 3, true};
        left.stick = {true, {0.0F, step % 17 < 9 ? 0.9F : 0.0F}, true};
        // A bounded aiming noise trajectory (not an intentional thrust).
        const float noise = step % 2 == 0 ? 0.004F : -0.004F;
        right.grip.pose.position = {0.20F + noise, -0.25F, -0.45F};
        left.grip.pose.position = {0.20F - noise, -0.25F, -0.75F};
        const bool owned = gameplay && mod::controller_frame_is_current(frame, now);
        const bool pulse = mod::update_right_stick_melee(
            false, owned && frame.frame.views_valid && right.grip.position_valid,
            right.primary.current, frame.frame.actions.sequence, right.stick_click, &melee);
        Command command{};
        if constexpr (std::is_same_v<Command, t4::UsercmdMp>) {
            command.opaque_after_angles.fill(std::byte{0xA5});
        }
        if (owned) {
            static_cast<void>(mod::apply_controller_input(command, frame, {}, now, pulse));
        }
        const auto gesture = mod::update_physical_melee_gesture(
            owned, frame, now, &physical, false, 0);
        if (gesture != mod::PhysicalMeleeGesture::none) ++unexpected;
        if (t4::has_button(command, t4::UsercmdButton::melee)) ++unexpected;
        if (t4::has_button(command, t4::UsercmdButton::attack)) ++attack_commands;
        if (t4::has_button(command, t4::UsercmdButton::reload)) ++reload_commands;
        const float degrees = smooth
            ? mod::consume_smooth_turn_degrees(owned, frame.frame.actions.sequence,
                  now, right.stick, &turn)
            : (owned ? mod::consume_snap_turn_degrees(right.stick, &snap) : 0.0F);
        if (degrees != 0.0F) ++turn_samples;
        // A second native command consuming the same action is not another click.
        require(!mod::update_right_stick_melee(false, owned, false,
                    frame.frame.actions.sequence, right.stick_click, &melee),
                "default remains off for duplicate commands");
        if constexpr (std::is_same_v<Command, t4::UsercmdMp>) {
            for (const auto value : command.opaque_after_angles) {
                require(value == std::byte{0xA5}, "MP opaque state stays unchanged");
            }
        }
    }
    require(unexpected == 0, "mixed controls must produce zero unintended melee");
    require(attack_commands > 0 && reload_commands > 0 && turn_samples > 0,
            "stress run actually exercises firing, reload and turning");
    std::printf("%s %s default-off: 12000 samples, 0 unintended melee; attack=%u reload=%u turn=%u\n",
        std::is_same_v<Command, t4::UsercmdSp> ? "SP" : "MP",
        smooth ? "smooth" : "snap", attack_commands, reload_commands, turn_samples);
}
} // namespace

int main() {
    exact_opt_in_and_rearm();
    command_routing<t4::UsercmdSp>();
    command_routing<t4::UsercmdMp>();
    mixed_controls_stress<t4::UsercmdSp>(false);
    mixed_controls_stress<t4::UsercmdSp>(true);
    mixed_controls_stress<t4::UsercmdMp>(false);
    mixed_controls_stress<t4::UsercmdMp>(true);
    std::puts("Right-stick melee default/off/on, rearm and SP/MP controls checks passed");
    return 0;
}
