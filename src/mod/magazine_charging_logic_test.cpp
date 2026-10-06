// SPDX-License-Identifier: GPL-3.0-only
#include "magazine_charging_logic.hpp"

#include <array>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

using wawvr::mod::MagazineChargingCalibration;
using wawvr::mod::MagazineChargingCompletion;
using wawvr::mod::MagazineChargingControlPolicy;
using wawvr::mod::MagazineChargingEvent;
using wawvr::mod::MagazineChargingFrame;
using wawvr::mod::MagazineChargingState;
using wawvr::mod::kMaximumMagazineChargingReturnSamples;
using wawvr::mod::update_magazine_charging;

constexpr std::array<float, kMaximumMagazineChargingReturnSamples>
    kM1GoldenReturnSamples{
        1.0F,
        228.0F / 255.0F,
        165.0F / 255.0F,
        90.0F / 255.0F,
        27.0F / 255.0F,
        0.0F,
    };

constexpr MagazineChargingCalibration kCalibration{
    .control_policy = MagazineChargingControlPolicy::ManualPullRelease,
    .travel_units = 2.961060F,
    .locked_open_offset_units = 2.384359F,
    .open_threshold = 0.98F,
    .spring_return_seconds = 1.0F / 6.0F,
    .trigger_engage = 0.65F,
    .trigger_release = 0.35F,
    .completion = MagazineChargingCompletion::SpringClosed,
    .return_sample_count = 6,
    .return_samples = kM1GoldenReturnSamples,
};

constexpr MagazineChargingCalibration kOpenBoltCalibration{
    .control_policy = MagazineChargingControlPolicy::ManualPullRelease,
    .travel_units = 3.5F,
    .locked_open_offset_units = 0.0F,
    .open_threshold = 0.98F,
    .spring_return_seconds = 0.0F,
    .trigger_engage = 0.65F,
    .trigger_release = 0.35F,
    .completion = MagazineChargingCompletion::LatchOpen,
};

constexpr std::array<float, kMaximumMagazineChargingReturnSamples>
    kEnBlocReturnSamples{
        1.0F,
        0.75F,
        0.25F,
        0.0F,
    };

constexpr MagazineChargingCalibration kEnBlocCalibration{
    .control_policy = MagazineChargingControlPolicy::EnBlocAutomatic,
    .travel_units = 3.0F,
    .locked_open_offset_units = 3.0F,
    .open_threshold = 0.98F,
    .spring_return_seconds = 0.12F,
    .trigger_engage = 0.0F,
    .trigger_release = 0.0F,
    .completion = MagazineChargingCompletion::SpringClosed,
    .return_sample_count = 4,
    .return_samples = kEnBlocReturnSamples,
};

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

MagazineChargingFrame baseline() {
    return {
        .enabled = true,
        .focused = true,
        .weapon_supported = true,
        .action_sequence = 1,
        .cartridge_available = true,
        .left_rifle_gripped = true,
        .right_hand_pose_valid = true,
        .right_hand_near_handle = true,
        .right_trigger_active = true,
        .left_trigger_active = true,
    };
}

MagazineChargingFrame left_hand_baseline() {
    return {
        .enabled = true,
        .focused = true,
        .weapon_supported = true,
        .action_sequence = 1,
        .cartridge_available = true,
        .right_trigger_active = true,
        .right_rifle_gripped = true,
        .left_hand_pose_valid = true,
        .left_hand_near_handle = true,
        .left_trigger_active = true,
    };
}

void rearm_after_empty_reload(
    MagazineChargingFrame* const frame,
    MagazineChargingState* const state) {
    frame->arm_charge = true;
    auto update = update_magazine_charging(kCalibration, *frame, state);
    expect(update.event == MagazineChargingEvent::ChargeRequired &&
               update.charge_required && update.block_attack &&
               update.handle_fraction > 0.80F &&
               !update.reserve_right_grip &&
               !update.reserve_left_grip,
           "empty reload arms an unresolved chamber lock without reserving either hand");
    frame->arm_charge = false;
    ++frame->action_sequence;
    update = update_magazine_charging(kCalibration, *frame, state);
    expect(update.event == MagazineChargingEvent::ControlsRearmed ||
               update.event == MagazineChargingEvent::None,
           "neutral controls leave the empty-reload lock ready");
}

void arm_en_bloc_after_empty_reload(
    MagazineChargingFrame* const frame,
    MagazineChargingState* const state) {
    frame->arm_charge = true;
    auto update = update_magazine_charging(
        kEnBlocCalibration, *frame, state);
    expect(update.event == MagazineChargingEvent::ChargeRequired &&
               update.charge_required && update.block_attack &&
               !update.reserve_right_grip &&
               !update.reserve_left_grip && !update.handle_grabbed &&
               update.handle_fraction == 1.0F,
           "empty Garand reload holds the action fully open without reserving a hand");
    frame->arm_charge = false;
    ++frame->action_sequence;
    update = update_magazine_charging(kEnBlocCalibration, *frame, state);
    expect(update.event == MagazineChargingEvent::None &&
               update.charge_required && update.block_attack &&
               !update.reserve_right_grip &&
               !update.reserve_left_grip && !update.handle_grabbed &&
               update.handle_fraction == 1.0F,
           "neutral controls preserve the automatic en-bloc action lock");
}

void test_tactical_reload_never_arms() {
    MagazineChargingState state{};
    auto frame = baseline();
    const auto update = update_magazine_charging(kCalibration, frame, &state);
    expect(!update.charge_required && !update.block_attack,
           "a reload without an empty-mag arm pulse remains immediately usable");
}

void test_en_bloc_automatic_spring_release_chambers_loaded_action() {
    MagazineChargingState state{};
    auto frame = baseline();
    arm_en_bloc_after_empty_reload(&frame, &state);

    frame.left_rifle_gripped = true;
    frame.right_hand_pose_valid = true;
    frame.right_hand_near_handle = true;
    frame.right_grip_held = true;
    frame.right_trigger_active = true;
    frame.right_trigger_value = 1.0F;
    frame.right_hand_forward_coordinate = -100.0F;
    ++frame.action_sequence;
    auto update = update_magazine_charging(
        kEnBlocCalibration, frame, &state);
    expect(update.event == MagazineChargingEvent::None &&
               update.charge_required && update.block_attack &&
               !update.reserve_right_grip &&
               !update.reserve_left_grip && !update.handle_grabbed &&
               !update.trigger_pressed_edge &&
               !update.trigger_released_edge &&
               update.handle_fraction == 1.0F &&
               !state.spring_returning,
           "en-bloc policy ignores grip, trigger, proximity, and manual pull input");

    frame.automatic_spring_release = true;
    ++frame.action_sequence;
    update = update_magazine_charging(kEnBlocCalibration, frame, &state);
    expect(update.event == MagazineChargingEvent::SpringReleased &&
               update.charge_required && update.block_attack &&
               !update.reserve_right_grip &&
               !update.reserve_left_grip && !update.handle_grabbed &&
               !update.trigger_pressed_edge &&
               !update.trigger_released_edge &&
               state.spring_returning,
           "a loaded en-bloc insertion starts spring return without reserving either hand");

    frame.automatic_spring_release = false;
    frame.delta_seconds = kEnBlocCalibration.spring_return_seconds;
    ++frame.action_sequence;
    update = update_magazine_charging(kEnBlocCalibration, frame, &state);
    expect(update.event == MagazineChargingEvent::Charged &&
               !update.charge_required && !update.block_attack &&
               !update.reserve_right_grip &&
               !update.reserve_left_grip && !update.handle_grabbed &&
               update.handle_fraction == 0.0F &&
               !state.spring_returning,
           "the automatic spring return closes, chambers, and restores firing");
}

void test_en_bloc_automatic_spring_release_requires_a_cartridge() {
    MagazineChargingState state{};
    auto frame = baseline();
    arm_en_bloc_after_empty_reload(&frame, &state);

    frame.cartridge_available = false;
    frame.automatic_spring_release = true;
    frame.right_grip_held = true;
    frame.right_trigger_value = 1.0F;
    frame.right_hand_forward_coordinate = -100.0F;
    ++frame.action_sequence;
    const auto update = update_magazine_charging(
        kEnBlocCalibration, frame, &state);
    expect(update.event == MagazineChargingEvent::None &&
               update.charge_required && update.block_attack &&
               !update.reserve_right_grip &&
               !update.reserve_left_grip && !update.handle_grabbed &&
               update.handle_fraction == 1.0F &&
               !state.spring_returning,
           "automatic release cannot chamber without a cartridge");
}

void test_en_bloc_automatic_spring_release_requires_prior_charge() {
    MagazineChargingState state{};
    auto frame = baseline();
    frame.automatic_spring_release = true;
    frame.right_grip_held = true;
    frame.right_trigger_value = 1.0F;
    frame.right_hand_forward_coordinate = -100.0F;

    const auto update = update_magazine_charging(
        kEnBlocCalibration, frame, &state);
    expect(update.event == MagazineChargingEvent::None &&
               !update.charge_required && !update.block_attack &&
               !update.reserve_right_grip &&
               !update.reserve_left_grip && !update.handle_grabbed &&
               !state.spring_returning && update.handle_fraction == 0.0F,
           "automatic release cannot create a charge transaction on its own");
}

void test_manual_policy_ignores_automatic_release_signal() {
    MagazineChargingState state{};
    auto frame = baseline();
    rearm_after_empty_reload(&frame, &state);
    frame.automatic_spring_release = true;
    ++frame.action_sequence;

    const auto update = update_magazine_charging(kCalibration, frame, &state);
    expect(update.event == MagazineChargingEvent::None &&
               update.charge_required && update.block_attack &&
               !update.reserve_right_grip &&
               !update.reserve_left_grip && !state.spring_returning,
           "automatic insertion signal cannot bypass or choose a hand for a manual charging profile");
}

void test_full_pull_and_release_chambers() {
    MagazineChargingState state{};
    auto frame = baseline();
    rearm_after_empty_reload(&frame, &state);

    frame.right_trigger_value = 1.0F;
    ++frame.action_sequence;
    auto update = update_magazine_charging(kCalibration, frame, &state);
    expect(update.event == MagazineChargingEvent::Grabbed &&
               update.handle_grabbed,
           "right index trigger grabs the handle while left hand retains rifle");

    frame.right_hand_forward_coordinate =
        -(kCalibration.travel_units -
          kCalibration.locked_open_offset_units);
    ++frame.action_sequence;
    update = update_magazine_charging(kCalibration, frame, &state);
    expect(update.event == MagazineChargingEvent::FullyOpened &&
               update.handle_fraction == 1.0F && update.charge_required,
           "rearward travel reaches the full-open endpoint");

    frame.right_trigger_value = 0.0F;
    ++frame.action_sequence;
    update = update_magazine_charging(kCalibration, frame, &state);
    expect(update.event == MagazineChargingEvent::SpringReleased &&
               update.charge_required && update.handle_fraction == 1.0F &&
               update.block_attack,
           "trigger release starts the authentic spring return");

    frame.delta_seconds = kCalibration.spring_return_seconds /
        static_cast<float>(kCalibration.return_sample_count - 1U);
    for (std::size_t sample_index = 1; sample_index + 1U <
         kCalibration.return_sample_count; ++sample_index) {
        ++frame.action_sequence;
        update = update_magazine_charging(kCalibration, frame, &state);
        expect(std::abs(
                   update.handle_fraction -
                   kM1GoldenReturnSamples[sample_index]) < 0.0001F &&
                   update.event ==
                       (sample_index == 1U
                            ? MagazineChargingEvent::ControlsRearmed
                            : MagazineChargingEvent::None) &&
                   update.charge_required && update.block_attack,
               "every intermediate 30 Hz sample matches the decoded native M1 curve");
    }

    frame.delta_seconds = kCalibration.spring_return_seconds;
    ++frame.action_sequence;
    update = update_magazine_charging(kCalibration, frame, &state);
    expect(update.event == MagazineChargingEvent::Charged &&
               !update.charge_required && update.handle_fraction ==
                   kM1GoldenReturnSamples[
                       kCalibration.return_sample_count - 1U],
           "the golden M1 curve reaches its exact closed endpoint and chambers once");
    frame.delta_seconds = 0.0F;
    ++frame.action_sequence;
    update = update_magazine_charging(kCalibration, frame, &state);
    expect(!update.block_attack,
           "neutral controls rearm firing after charging");
}

void test_left_hand_full_pull_waits_for_both_triggers() {
    MagazineChargingState state{};
    auto frame = left_hand_baseline();
    // Keep the retaining right index trigger held throughout the left-hand
    // charge. The action may chamber, but attack cannot rearm until this
    // firing trigger is positively released as well.
    frame.right_trigger_active = true;
    frame.right_trigger_value = 1.0F;
    frame.arm_charge = true;
    auto update = update_magazine_charging(kCalibration, frame, &state);
    expect(update.event == MagazineChargingEvent::ChargeRequired &&
               update.charge_required && update.block_attack &&
               !update.reserve_right_grip &&
               !update.reserve_left_grip,
           "a left-capable empty reload begins without preselecting a hand");

    frame.arm_charge = false;
    ++frame.action_sequence;
    update = update_magazine_charging(kCalibration, frame, &state);
    expect(update.charge_required && !update.handle_grabbed &&
               !update.reserve_right_grip &&
               !update.reserve_left_grip,
           "the unresolved chamber lock reserves neither grip");

    frame.left_trigger_value = 1.0F;
    ++frame.action_sequence;
    update = update_magazine_charging(kCalibration, frame, &state);
    expect(update.event == MagazineChargingEvent::Grabbed &&
               update.handle_grabbed &&
               update.manipulating_left_hand &&
               update.reserve_left_grip &&
               !update.reserve_right_grip,
           "left index trigger grabs the handle while the right hand retains the firearm");

    frame.left_hand_forward_coordinate =
        -(kCalibration.travel_units -
          kCalibration.locked_open_offset_units);
    ++frame.action_sequence;
    update = update_magazine_charging(kCalibration, frame, &state);
    expect(update.event == MagazineChargingEvent::FullyOpened &&
               update.handle_fraction == 1.0F &&
               update.manipulating_left_hand,
           "left-hand travel reaches the exact full-rear endpoint");

    frame.left_trigger_value = 0.0F;
    ++frame.action_sequence;
    update = update_magazine_charging(kCalibration, frame, &state);
    expect(update.event == MagazineChargingEvent::SpringReleased &&
               state.spring_returning && update.block_attack &&
               update.reserve_left_grip,
           "left-trigger release starts spring return while retaining selected-hand ownership");

    frame.delta_seconds = kCalibration.spring_return_seconds;
    ++frame.action_sequence;
    update = update_magazine_charging(kCalibration, frame, &state);
    expect(update.event == MagazineChargingEvent::Charged &&
               !update.charge_required && update.block_attack &&
               update.reserve_left_grip,
           "spring closure chambers the rifle but a held right firing trigger keeps attack locked");

    frame.delta_seconds = 0.0F;
    ++frame.action_sequence;
    update = update_magazine_charging(kCalibration, frame, &state);
    expect(update.event == MagazineChargingEvent::None &&
               update.block_attack && update.reserve_left_grip,
           "releasing only the manipulating trigger cannot rearm charging controls");

    frame.right_trigger_active = false;
    frame.right_trigger_value = 0.0F;
    ++frame.action_sequence;
    update = update_magazine_charging(kCalibration, frame, &state);
    expect(update.event != MagazineChargingEvent::ControlsRearmed &&
               update.block_attack,
           "an inactive opposite-trigger sample cannot rearm a completed left-hand charge");

    frame.right_trigger_active = true;
    frame.left_trigger_value =
        (std::numeric_limits<float>::quiet_NaN)();
    ++frame.action_sequence;
    update = update_magazine_charging(kCalibration, frame, &state);
    expect(update.event != MagazineChargingEvent::ControlsRearmed &&
               update.block_attack,
           "a non-finite selected-trigger sample cannot rearm a completed left-hand charge");

    frame.right_trigger_active = true;
    frame.left_trigger_active = true;
    frame.right_trigger_value = 0.0F;
    frame.left_trigger_value = 0.0F;
    frame.left_hand_pose_valid = true;
    frame.left_grip_held = false;
    ++frame.action_sequence;
    update = update_magazine_charging(kCalibration, frame, &state);
    expect(update.event == MagazineChargingEvent::ControlsRearmed &&
               !update.block_attack &&
               !update.reserve_right_grip &&
               !update.reserve_left_grip,
           "both index triggers must be released before a left-hand charge rearms firing");
}

void test_selected_left_charging_hand_cannot_switch_mid_gesture() {
    MagazineChargingState state{};
    auto frame = left_hand_baseline();
    frame.arm_charge = true;
    static_cast<void>(update_magazine_charging(kCalibration, frame, &state));
    frame.arm_charge = false;
    ++frame.action_sequence;
    static_cast<void>(update_magazine_charging(kCalibration, frame, &state));

    frame.left_trigger_value = 1.0F;
    ++frame.action_sequence;
    auto update = update_magazine_charging(kCalibration, frame, &state);
    expect(update.handle_grabbed && update.manipulating_left_hand,
           "left hand establishes the active charging gesture");

    frame.left_hand_forward_coordinate = 0.0F;
    frame.right_trigger_active = true;
    frame.right_trigger_value = 1.0F;
    frame.right_hand_pose_valid = true;
    frame.right_hand_near_handle = true;
    frame.right_hand_forward_coordinate = -kCalibration.travel_units;
    ++frame.action_sequence;
    update = update_magazine_charging(kCalibration, frame, &state);
    expect(update.handle_grabbed && update.manipulating_left_hand &&
               update.handle_fraction > 0.80F &&
               update.handle_fraction < 1.0F &&
               update.reserve_left_grip &&
               !update.reserve_right_grip,
           "another trigger edge and controller movement cannot steal or drive an active left-hand charge");
}

void test_partial_pull_does_not_chamber() {
    MagazineChargingState state{};
    auto frame = baseline();
    rearm_after_empty_reload(&frame, &state);

    frame.right_trigger_value = 1.0F;
    ++frame.action_sequence;
    static_cast<void>(update_magazine_charging(kCalibration, frame, &state));
    frame.right_hand_forward_coordinate = -0.5F *
        (kCalibration.travel_units -
         kCalibration.locked_open_offset_units);
    ++frame.action_sequence;
    static_cast<void>(update_magazine_charging(kCalibration, frame, &state));
    frame.right_trigger_value = 0.0F;
    ++frame.action_sequence;
    const auto update = update_magazine_charging(kCalibration, frame, &state);
    expect(update.event == MagazineChargingEvent::Released &&
               update.charge_required && update.handle_fraction > 0.80F,
           "partial pull returns to locked-open without clearing the chamber lock");
}

void test_wrong_hand_context_cannot_charge() {
    MagazineChargingState state{};
    auto frame = baseline();
    rearm_after_empty_reload(&frame, &state);
    frame.left_rifle_gripped = false;
    frame.right_trigger_value = 1.0F;
    ++frame.action_sequence;
    auto update = update_magazine_charging(kCalibration, frame, &state);
    expect(!update.handle_grabbed && update.charge_required,
           "right trigger cannot grab unless the left hand retains the rifle");
    frame.left_rifle_gripped = true;
    frame.right_hand_near_handle = false;
    frame.right_trigger_value = 0.0F;
    ++frame.action_sequence;
    static_cast<void>(update_magazine_charging(kCalibration, frame, &state));
    frame.right_trigger_value = 1.0F;
    ++frame.action_sequence;
    update = update_magazine_charging(kCalibration, frame, &state);
    expect(!update.handle_grabbed && update.charge_required,
           "a trigger press outside the exact handle radius cannot charge");
}

void test_focus_gap_preserves_requirement() {
    MagazineChargingState state{};
    auto frame = baseline();
    rearm_after_empty_reload(&frame, &state);
    frame.right_trigger_value = 1.0F;
    ++frame.action_sequence;
    static_cast<void>(update_magazine_charging(kCalibration, frame, &state));
    frame.right_hand_forward_coordinate =
        -(kCalibration.travel_units -
          kCalibration.locked_open_offset_units);
    ++frame.action_sequence;
    static_cast<void>(update_magazine_charging(kCalibration, frame, &state));
    frame.focused = false;
    ++frame.action_sequence;
    const auto update = update_magazine_charging(kCalibration, frame, &state);
    expect(update.event == MagazineChargingEvent::Released &&
               update.charge_required && update.handle_fraction > 0.80F,
           "tracking loss releases the handle but cannot chamber the rifle");
}

void test_empty_charge_cannot_clear_requirement() {
    MagazineChargingState state{};
    auto frame = baseline();
    rearm_after_empty_reload(&frame, &state);
    frame.cartridge_available = false;
    frame.right_trigger_value = 1.0F;
    ++frame.action_sequence;
    static_cast<void>(update_magazine_charging(kCalibration, frame, &state));
    frame.right_hand_forward_coordinate =
        -(kCalibration.travel_units -
          kCalibration.locked_open_offset_units);
    ++frame.action_sequence;
    static_cast<void>(update_magazine_charging(kCalibration, frame, &state));
    frame.right_trigger_value = 0.0F;
    ++frame.action_sequence;
    const auto update = update_magazine_charging(kCalibration, frame, &state);
    expect(update.event == MagazineChargingEvent::Released &&
               update.charge_required,
           "a full pull with an empty clip cannot clear the chamber lock");
}

void test_focus_gap_cannot_finish_spring_return() {
    MagazineChargingState state{};
    auto frame = baseline();
    rearm_after_empty_reload(&frame, &state);
    frame.right_trigger_value = 1.0F;
    ++frame.action_sequence;
    static_cast<void>(update_magazine_charging(kCalibration, frame, &state));
    frame.right_hand_forward_coordinate =
        -(kCalibration.travel_units -
          kCalibration.locked_open_offset_units);
    ++frame.action_sequence;
    static_cast<void>(update_magazine_charging(kCalibration, frame, &state));
    frame.right_trigger_value = 0.0F;
    ++frame.action_sequence;
    static_cast<void>(update_magazine_charging(kCalibration, frame, &state));

    frame.focused = false;
    frame.delta_seconds = kCalibration.spring_return_seconds * 2.0F;
    ++frame.action_sequence;
    const auto update = update_magazine_charging(kCalibration, frame, &state);
    expect(update.charge_required && update.block_attack &&
               update.handle_fraction > 0.80F,
           "an unfocused frame cannot complete a spring return or clear the chamber lock");
}

void test_inactive_trigger_action_cannot_chamber() {
    MagazineChargingState state{};
    auto frame = baseline();
    rearm_after_empty_reload(&frame, &state);
    frame.right_trigger_value = 1.0F;
    ++frame.action_sequence;
    static_cast<void>(update_magazine_charging(kCalibration, frame, &state));
    frame.right_hand_forward_coordinate =
        -(kCalibration.travel_units -
          kCalibration.locked_open_offset_units);
    ++frame.action_sequence;
    static_cast<void>(update_magazine_charging(kCalibration, frame, &state));

    frame.right_trigger_active = false;
    ++frame.action_sequence;
    const auto update = update_magazine_charging(kCalibration, frame, &state);
    expect(update.event == MagazineChargingEvent::Released &&
               update.charge_required && !state.spring_returning,
           "an unavailable trigger sample releases safely without manufacturing a chambered round");
}

void test_open_bolt_action_latches_rear_and_unblocks() {
    MagazineChargingState state{};
    auto frame = baseline();
    frame.arm_charge = true;
    auto update = update_magazine_charging(
        kOpenBoltCalibration, frame, &state);
    expect(update.event == MagazineChargingEvent::ChargeRequired &&
               update.handle_fraction == 0.0F && update.block_attack,
           "an empty open-bolt reload starts forward and locked");

    frame.arm_charge = false;
    ++frame.action_sequence;
    static_cast<void>(update_magazine_charging(
        kOpenBoltCalibration, frame, &state));
    frame.right_trigger_value = 1.0F;
    ++frame.action_sequence;
    update = update_magazine_charging(
        kOpenBoltCalibration, frame, &state);
    expect(update.event == MagazineChargingEvent::Grabbed,
           "the open-bolt handle can be grabbed after controls rearm");

    frame.right_hand_forward_coordinate =
        -kOpenBoltCalibration.travel_units;
    ++frame.action_sequence;
    update = update_magazine_charging(
        kOpenBoltCalibration, frame, &state);
    expect(update.event == MagazineChargingEvent::FullyOpened &&
               update.handle_fraction == 1.0F,
           "the open-bolt action reaches its full-rear catch");

    frame.right_trigger_value = 0.0F;
    ++frame.action_sequence;
    update = update_magazine_charging(
        kOpenBoltCalibration, frame, &state);
    expect(update.event == MagazineChargingEvent::Charged &&
               !update.charge_required && update.handle_fraction == 1.0F &&
               update.block_attack && state.awaiting_controls_release &&
               !state.spring_returning,
            "release latches the ready open-bolt action rear while controls settle");
    ++frame.action_sequence;
    update = update_magazine_charging(
        kOpenBoltCalibration, frame, &state);
    expect(update.event == MagazineChargingEvent::ControlsRearmed &&
               !update.block_attack && update.handle_fraction == 1.0F &&
               !state.awaiting_controls_release && !state.spring_returning,
           "neutral controls rearm firing without spring-closing the latched action");

    frame.delta_seconds = 10.0F;
    ++frame.action_sequence;
    update = update_magazine_charging(
        kOpenBoltCalibration, frame, &state);
    expect(update.event == MagazineChargingEvent::None &&
               !update.charge_required && !update.block_attack &&
               update.handle_fraction == 1.0F && !state.spring_returning,
           "elapsed time cannot move a completed open-bolt latch off its rear catch");
}

void test_return_curve_validation_is_profile_specific() {
    expect(wawvr::mod::validate_magazine_charging_calibration(kCalibration) &&
               wawvr::mod::validate_magazine_charging_calibration(
                   kOpenBoltCalibration) &&
               wawvr::mod::validate_magazine_charging_calibration(
                   kEnBlocCalibration),
           "manual spring, manual open-bolt, and automatic en-bloc policies validate independently");

    MagazineChargingCalibration malformed = kEnBlocCalibration;
    malformed.locked_open_offset_units =
        kEnBlocCalibration.travel_units - 0.01F;
    expect(!wawvr::mod::validate_magazine_charging_calibration(malformed),
           "an automatic en-bloc action must begin exactly fully open");

    malformed = kEnBlocCalibration;
    malformed.trigger_engage = 0.01F;
    expect(!wawvr::mod::validate_magazine_charging_calibration(malformed),
           "an automatic en-bloc profile cannot retain a trigger-engage threshold");

    malformed = kEnBlocCalibration;
    malformed.trigger_release = 0.01F;
    expect(!wawvr::mod::validate_magazine_charging_calibration(malformed),
           "an automatic en-bloc profile cannot retain a trigger-release threshold");

    malformed = kEnBlocCalibration;
    malformed.completion = MagazineChargingCompletion::LatchOpen;
    malformed.spring_return_seconds = 0.0F;
    malformed.return_sample_count = 0;
    malformed.return_samples = {};
    expect(!wawvr::mod::validate_magazine_charging_calibration(malformed),
           "an automatic en-bloc profile must spring closed instead of latching open");

    malformed = kCalibration;
    malformed.locked_open_offset_units = malformed.travel_units;
    expect(!wawvr::mod::validate_magazine_charging_calibration(malformed),
           "a manual pull profile retains rearward travel beyond its locked-open rest");

    malformed = kCalibration;
    malformed.control_policy =
        static_cast<MagazineChargingControlPolicy>(255);
    expect(!wawvr::mod::validate_magazine_charging_calibration(malformed),
           "an unknown charging control policy fails closed");

    malformed = kCalibration;
    malformed.return_sample_count = 1;
    expect(!wawvr::mod::validate_magazine_charging_calibration(malformed),
           "a spring curve needs at least an open and a closed endpoint");

    malformed = kCalibration;
    malformed.return_sample_count =
        static_cast<std::uint8_t>(
            kMaximumMagazineChargingReturnSamples + 1U);
    expect(!wawvr::mod::validate_magazine_charging_calibration(malformed),
           "a spring curve cannot exceed the fixed sample capacity");

    malformed = kCalibration;
    malformed.return_samples[0] = 0.99F;
    expect(!wawvr::mod::validate_magazine_charging_calibration(malformed),
           "a spring curve must start at the fully open endpoint");

    malformed = kCalibration;
    malformed.return_samples[kCalibration.return_sample_count - 1U] = 0.01F;
    expect(!wawvr::mod::validate_magazine_charging_calibration(malformed),
           "a spring curve must end at the fully closed endpoint");

    malformed = kCalibration;
    malformed.return_samples[2] = 0.95F;
    expect(!wawvr::mod::validate_magazine_charging_calibration(malformed),
           "a non-monotonic return curve fails closed");

    malformed = kCalibration;
    malformed.return_samples[2] =
        (std::numeric_limits<float>::quiet_NaN)();
    expect(!wawvr::mod::validate_magazine_charging_calibration(malformed),
           "a non-finite authored return sample fails closed");

    malformed = kCalibration;
    malformed.return_samples[2] = -0.01F;
    expect(!wawvr::mod::validate_magazine_charging_calibration(malformed),
           "a return sample below the closed endpoint fails closed");

    malformed = kCalibration;
    malformed.return_samples[2] = 1.01F;
    expect(!wawvr::mod::validate_magazine_charging_calibration(malformed),
           "a return sample above the open endpoint fails closed");

    malformed = kCalibration;
    malformed.return_samples[kCalibration.return_sample_count] = 0.01F;
    expect(!wawvr::mod::validate_magazine_charging_calibration(malformed),
           "unused curve capacity must remain zero instead of hiding drift");

    malformed = kOpenBoltCalibration;
    malformed.return_sample_count = 2;
    malformed.return_samples = {1.0F, 0.0F};
    expect(!wawvr::mod::validate_magazine_charging_calibration(malformed),
           "an open-bolt latch cannot silently acquire an active spring curve");

    malformed = kOpenBoltCalibration;
    malformed.return_samples[0] = 1.0F;
    expect(!wawvr::mod::validate_magazine_charging_calibration(malformed),
           "an open-bolt latch rejects hidden samples even when its active count is zero");

    malformed = kCalibration;
    malformed.completion =
        static_cast<MagazineChargingCompletion>(255);
    expect(!wawvr::mod::validate_magazine_charging_calibration(malformed),
           "an unknown charging completion policy fails closed");
}

}  // namespace

int main() {
    test_tactical_reload_never_arms();
    test_en_bloc_automatic_spring_release_chambers_loaded_action();
    test_en_bloc_automatic_spring_release_requires_a_cartridge();
    test_en_bloc_automatic_spring_release_requires_prior_charge();
    test_manual_policy_ignores_automatic_release_signal();
    test_full_pull_and_release_chambers();
    test_left_hand_full_pull_waits_for_both_triggers();
    test_selected_left_charging_hand_cannot_switch_mid_gesture();
    test_partial_pull_does_not_chamber();
    test_wrong_hand_context_cannot_charge();
    test_focus_gap_preserves_requirement();
    test_empty_charge_cannot_clear_requirement();
    test_focus_gap_cannot_finish_spring_return();
    test_inactive_trigger_action_cannot_chamber();
    test_open_bolt_action_latches_rear_and_unblocks();
    test_return_curve_validation_is_profile_specific();
    std::cout << "Magazine charging logic tests passed\n";
    return EXIT_SUCCESS;
}
