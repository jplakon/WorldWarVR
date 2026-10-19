// SPDX-License-Identifier: GPL-3.0-only
#include "manual_grenade_logic.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

using namespace wawvr::mod;

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// Unit-model witnesses only: the runtime has no verified native event/queue
// ownership adapter. These helpers must never be mistaken for native proof.
[[nodiscard]] bool retire_with_simulated_native_drain(
    const bool trigger, const bool authoritative, const bool predicted,
    ContextualThrowbackTransaction* const transaction) {
    const ContextualThrowbackNativeDrainWitness drain{
        transaction != nullptr ? transaction->generation : 0,
        true, true};
    return retire_contextual_throwback_tombstone(
        trigger, authoritative, predicted, transaction, &drain);
}

[[nodiscard]] bool complete_one_simulated_owned_dispatch(
    const std::uint32_t weapon,
    ContextualThrowbackTransaction* const transaction) {
    const ContextualThrowbackNativeEventReceipt receipt{
        true, transaction != nullptr ? transaction->generation : 0, 5000};
    return consume_contextual_throwback_spawn(weapon, transaction, &receipt) &&
        complete_contextual_throwback_dispatch(receipt, transaction);
}

[[nodiscard]] bool near(
    const float left,
    const float right,
    const float tolerance = 1.0e-3F) noexcept {
    return std::abs(left - right) <= tolerance;
}

void expect_vector(
    const ManualGrenadePoint& actual,
    const ManualGrenadePoint& expected,
    const std::string_view message,
    const float tolerance = 1.0e-3F) {
    expect(near(actual.x, expected.x, tolerance) &&
               near(actual.y, expected.y, tolerance) &&
               near(actual.z, expected.z, tolerance),
           message);
}

// Most gesture tests exercise the normal unblocked path. Keep that path
// concise while the dedicated arbitration test below calls the full API.
[[nodiscard]] bool update_manual_grenade_gesture(
    const bool input_valid,
    const bool trigger_active,
    const float trigger_value,
    const ManualGrenadePoint& head_local,
    ManualGrenadeGestureState* const state,
    ManualGrenadeEvent* const event) noexcept {
    return wawvr::mod::update_manual_grenade_gesture(
        input_valid, trigger_active, trigger_value, head_local, false,
        state, event);
}

void test_belt_slots_are_mirrored_and_finite() {
    expect(manual_grenade_slot_at({0.0F, 13.0F, -28.0F}) ==
               ManualGrenadeSlot::frag,
           "left hip selects frag");
    expect(manual_grenade_slot_at({0.0F, -13.0F, -28.0F}) ==
               ManualGrenadeSlot::tactical,
           "right hip selects tactical");
    expect(manual_grenade_left_hip_contains({-18.0F, 2.0F, -42.0F}) &&
               manual_grenade_left_hip_contains({18.0F, 24.0F, -14.0F}),
           "left hip includes every documented boundary");
    expect(manual_grenade_right_hip_contains({-18.0F, -2.0F, -42.0F}) &&
               manual_grenade_right_hip_contains({18.0F, -24.0F, -14.0F}),
           "right hip is the exact mirrored volume");
    expect(manual_grenade_slot_at({19.0F, 13.0F, -28.0F}) ==
               ManualGrenadeSlot::none &&
               manual_grenade_slot_at({0.0F, 0.0F, -28.0F}) ==
                   ManualGrenadeSlot::none &&
               manual_grenade_slot_at({0.0F, 13.0F, -13.9F}) ==
                   ManualGrenadeSlot::none,
           "outside and center positions select no grenade");
    expect(manual_grenade_slot_at(
               {(std::numeric_limits<float>::quiet_NaN)(), 13.0F,
                -28.0F}) == ManualGrenadeSlot::none,
           "non-finite belt position fails closed");
}

void test_throwback_override_is_frag_only() {
    expect(manual_grenade_has_frag_throwback_override(
               ManualGrenadeSlot::frag, 1) &&
               manual_grenade_has_frag_throwback_override(
                   ManualGrenadeSlot::frag, 5000),
           "a live native throwback makes the frag slot available");
    expect(!manual_grenade_has_frag_throwback_override(
               ManualGrenadeSlot::frag, 0) &&
               !manual_grenade_has_frag_throwback_override(
                   ManualGrenadeSlot::frag, -1) &&
               !manual_grenade_has_frag_throwback_override(
                   ManualGrenadeSlot::tactical, 5000) &&
               !manual_grenade_has_frag_throwback_override(
                   ManualGrenadeSlot::none, 5000),
           "throwback cannot manufacture tactical or invalid inventory");
}

void test_contextual_throwback_requires_a_fresh_edge_and_never_uses_belt_slot() {
    using namespace wawvr::mod;
    ManualGrenadeGestureState state{};
    ManualGrenadeEvent event{};
    const ManualGrenadePoint aiming_hand{12.0F, -4.0F, -6.0F};
    static_cast<void>(wawvr::mod::update_manual_grenade_gesture(
        true, true, 0.0F, aiming_hand, false, &state, &event, false));
    static_cast<void>(wawvr::mod::update_manual_grenade_gesture(
        true, true, 1.0F, aiming_hand, false, &state, &event, true));
    expect(event == ManualGrenadeEvent::grab_frag &&
               state.held_slot == ManualGrenadeSlot::frag && state.contextual_throwback,
           "native live-grenade context allows a trigger grab at aiming height without hip reach");
    for (int sample = 0; sample < 10'000; ++sample) {
        static_cast<void>(wawvr::mod::update_manual_grenade_gesture(
            true, true, 1.0F, aiming_hand, false, &state, &event, true));
        expect(event == ManualGrenadeEvent::none && state.contextual_throwback,
               "held contextual trigger cannot duplicate the pickup transaction");
    }
    static_cast<void>(wawvr::mod::update_manual_grenade_gesture(
        true, true, 0.0F, aiming_hand, true, &state, &event, false));
    expect(event == ManualGrenadeEvent::release && state.contextual_throwback,
           "release remains available if context expires or support/reload arbitration changes");
    expect(rearm_manual_grenade_gesture(&state) && !state.contextual_throwback,
           "spawn retirement clears contextual ownership");
    static_cast<void>(wawvr::mod::update_manual_grenade_gesture(
        true, true, 1.0F, aiming_hand, false, &state, &event, false));
    expect(event == ManualGrenadeEvent::none && state.stage == ManualGrenadeStage::ready,
           "without a native enemy grenade, aiming-height trigger cannot throw a fresh grenade");

    reset_manual_grenade_gesture(&state);
    static_cast<void>(wawvr::mod::update_manual_grenade_gesture(
        true, true, 1.0F, aiming_hand, false, &state, &event, true));
    expect(event == ManualGrenadeEvent::none,
           "a trigger already held when a grenade prompt appears cannot auto-pickup");
    static_cast<void>(wawvr::mod::update_manual_grenade_gesture(
        true, true, 0.0F, aiming_hand, false, &state, &event, true));
    static_cast<void>(wawvr::mod::update_manual_grenade_gesture(
        true, true, 1.0F, aiming_hand, true, &state, &event, true));
    expect(event == ManualGrenadeEvent::none && !state.contextual_throwback,
           "an active reload consumes a contextual press instead of stealing its hand");
}

void test_startup_requires_a_fresh_trigger_edge() {
    ManualGrenadeGestureState state{};
    ManualGrenadeEvent event{ManualGrenadeEvent::reset};
    const ManualGrenadePoint left_hip{0.0F, 13.0F, -28.0F};

    expect(update_manual_grenade_gesture(
               true, true, 1.0F, left_hip, &state, &event) &&
               event == ManualGrenadeEvent::none &&
               state.stage == ManualGrenadeStage::ready,
           "trigger held at startup is only baselined");
    expect(update_manual_grenade_gesture(
               true, true, 1.0F, left_hip, &state, &event) &&
               event == ManualGrenadeEvent::none,
           "continued startup hold never manufactures a grab");
    expect(update_manual_grenade_gesture(
               true, true, 0.0F, left_hip, &state, &event) &&
               event == ManualGrenadeEvent::none,
           "release after startup arms a fresh edge");
    expect(update_manual_grenade_gesture(
               true, true, 0.70F, left_hip, &state, &event) &&
               event == ManualGrenadeEvent::grab_frag &&
               state.stage == ManualGrenadeStage::holding &&
               state.held_slot == ManualGrenadeSlot::frag,
           "left-trigger engage edge grabs frag at left hip");
}

void test_trigger_hysteresis_releases_once() {
    ManualGrenadeGestureState state{};
    ManualGrenadeEvent event{};
    const ManualGrenadePoint left_hip{0.0F, 13.0F, -28.0F};
    expect(update_manual_grenade_gesture(
               true, true, 0.0F, left_hip, &state, &event),
           "unheld trigger baseline succeeds");
    expect(update_manual_grenade_gesture(
               true, true, 2.0F, left_hip, &state, &event) &&
               event == ManualGrenadeEvent::grab_frag,
           "trigger values are clamped before engage");
    expect(update_manual_grenade_gesture(
               true, true, 0.36F, {0.0F, 0.0F, 0.0F}, &state,
               &event) &&
               event == ManualGrenadeEvent::none &&
               state.stage == ManualGrenadeStage::holding,
           "held trigger stays latched above release threshold");
    expect(update_manual_grenade_gesture(
               true, true, 0.35F, {0.0F, 0.0F, 0.0F}, &state,
               &event) &&
               event == ManualGrenadeEvent::release &&
               state.stage == ManualGrenadeStage::released_pending &&
               state.held_slot == ManualGrenadeSlot::frag,
           "release threshold produces exactly one release event");
    expect(update_manual_grenade_gesture(
               true, true, -1.0F, left_hip, &state, &event) &&
               event == ManualGrenadeEvent::none,
           "continued release does not repeat the event");
    expect(rearm_manual_grenade_gesture(&state) &&
               state.stage == ManualGrenadeStage::ready &&
               state.held_slot == ManualGrenadeSlot::none &&
               state.input_initialized && !state.trigger_was_held,
           "native consumption rearms while preserving trigger baseline");
    expect(update_manual_grenade_gesture(
               true, true, 0.7F, left_hip, &state, &event) &&
               event == ManualGrenadeEvent::grab_frag,
           "a fresh press can grab immediately after native consumption");
    expect(!rearm_manual_grenade_gesture(&state),
           "an actively held grenade cannot be silently rearmed");
}

void test_tactical_slot_and_outside_edge_policy() {
    ManualGrenadeGestureState state{};
    ManualGrenadeEvent event{};
    const ManualGrenadePoint right_hip{0.0F, -13.0F, -28.0F};
    const ManualGrenadePoint chest{0.0F, 0.0F, 0.0F};
    expect(update_manual_grenade_gesture(
               true, true, 0.0F, chest, &state, &event),
           "outside baseline succeeds");
    expect(update_manual_grenade_gesture(
               true, true, 1.0F, chest, &state, &event) &&
               event == ManualGrenadeEvent::none,
           "press outside the belt does not grab");
    expect(update_manual_grenade_gesture(
               true, true, 1.0F, right_hip, &state, &event) &&
               event == ManualGrenadeEvent::none,
           "moving into the belt while held does not invent an edge");
    expect(update_manual_grenade_gesture(
               true, true, 0.0F, right_hip, &state, &event) &&
               update_manual_grenade_gesture(
                   true, true, 0.70F, right_hip, &state, &event) &&
               event == ManualGrenadeEvent::grab_tactical &&
               state.held_slot == ManualGrenadeSlot::tactical,
           "fresh left-trigger edge at right hip grabs tactical");
}

void test_new_grab_arbitration_consumes_edge_but_never_blocks_release() {
    ManualGrenadeGestureState state{};
    ManualGrenadeEvent event{};
    const ManualGrenadePoint hip{0.0F, 13.0F, -28.0F};
    expect(wawvr::mod::update_manual_grenade_gesture(
               true, true, 0.0F, hip, false, &state, &event),
           "arbitrated gesture baseline succeeds");
    expect(wawvr::mod::update_manual_grenade_gesture(
               true, true, 1.0F, hip, true, &state, &event) &&
               event == ManualGrenadeEvent::none &&
               state.stage == ManualGrenadeStage::ready,
           "reload or support ownership blocks a new grenade grab");
    expect(wawvr::mod::update_manual_grenade_gesture(
               true, true, 1.0F, hip, false, &state, &event) &&
               event == ManualGrenadeEvent::none,
           "lifting arbitration while held cannot manufacture a grab");
    expect(wawvr::mod::update_manual_grenade_gesture(
               true, true, 0.0F, hip, false, &state, &event) &&
               wawvr::mod::update_manual_grenade_gesture(
                   true, true, 1.0F, hip, false, &state, &event) &&
               event == ManualGrenadeEvent::grab_frag,
           "release and fresh press grab after arbitration clears");
    expect(wawvr::mod::update_manual_grenade_gesture(
               true, true, 0.0F, hip, true, &state, &event) &&
               event == ManualGrenadeEvent::release &&
               state.stage == ManualGrenadeStage::released_pending,
           "arbitration never traps a grenade already being held");
}

void test_focus_and_tracking_loss_cancel_without_throw() {
    ManualGrenadeGestureState state{};
    ManualGrenadeEvent event{};
    const ManualGrenadePoint hip{0.0F, 13.0F, -28.0F};
    expect(update_manual_grenade_gesture(
               true, true, 0.0F, hip, &state, &event) &&
               update_manual_grenade_gesture(
                   true, true, 1.0F, hip, &state, &event) &&
               event == ManualGrenadeEvent::grab_frag,
           "precondition grenade is held");
    expect(update_manual_grenade_gesture(
               false, true, 1.0F, hip, &state, &event) &&
               event == ManualGrenadeEvent::reset &&
               state.stage == ManualGrenadeStage::ready &&
               state.held_slot == ManualGrenadeSlot::none &&
               !state.input_initialized,
           "focus loss resets instead of releasing");
    expect(update_manual_grenade_gesture(
               true, true, 1.0F, hip, &state, &event) &&
               event == ManualGrenadeEvent::none,
           "held trigger on focus recovery is baselined");
    expect(update_manual_grenade_gesture(
               true, true, 0.0F, hip, &state, &event) &&
               update_manual_grenade_gesture(
                   true, true, 1.0F, hip, &state, &event) &&
               event == ManualGrenadeEvent::grab_frag,
           "release and repress after recovery grabs safely");
    expect(update_manual_grenade_gesture(
               true, false, 1.0F, hip, &state, &event) &&
               event == ManualGrenadeEvent::reset,
           "inactive trigger action also cancels without release");
}

void test_invalid_gesture_inputs_fail_closed() {
    ManualGrenadeGestureState state{};
    ManualGrenadeEvent event{ManualGrenadeEvent::release};
    const ManualGrenadePoint hip{0.0F, 13.0F, -28.0F};
    expect(!update_manual_grenade_gesture(
               true, true, 0.0F, hip, nullptr, &event),
           "null gesture state is rejected");
    expect(!update_manual_grenade_gesture(
               true, true, 0.0F, hip, &state, nullptr),
           "null gesture event is rejected");
    expect(update_manual_grenade_gesture(
               true, true, 0.0F, hip, &state, &event),
           "gesture initialized before non-finite input");
    expect(update_manual_grenade_gesture(
               true, true,
               (std::numeric_limits<float>::quiet_NaN)(), hip,
               &state, &event) &&
               event == ManualGrenadeEvent::reset &&
               !state.input_initialized,
           "non-finite trigger resets fail closed");
    state.stage = static_cast<ManualGrenadeStage>(255);
    expect(update_manual_grenade_gesture(
               true, true, 0.0F, hip, &state, &event) &&
               event == ManualGrenadeEvent::reset &&
               state.stage == ManualGrenadeStage::ready,
           "corrupt state is reset fail closed");
    reset_manual_grenade_gesture(&state);
    reset_manual_grenade_gesture(nullptr);
    expect(state.stage == ManualGrenadeStage::ready,
           "explicit reset is null-safe and deterministic");
}

void test_velocity_history_selects_recent_strongest() {
    ManualGrenadeVelocityHistory history{};
    constexpr std::uint64_t now = 1'000'000'000ULL;
    expect(record_manual_grenade_velocity(
               now - 200'000'000ULL, {500.0F, 0.0F, 0.0F}, &history),
           "stale sample may be recorded");
    expect(record_manual_grenade_velocity(
               now - 100'000'000ULL, {300.0F, 0.0F, 0.0F}, &history),
           "older strong sample recorded");
    expect(record_manual_grenade_velocity(
               now - 10'000'000ULL, {250.0F, 25.0F, 0.0F}, &history),
           "recent throw sample recorded");
    ManualGrenadePoint selected{};
    std::uint64_t age{};
    expect(select_manual_grenade_release_velocity(
               now, history, &selected, &age) &&
               age == 10'000'000ULL && near(selected.x, 250.0F) &&
               near(selected.y, 25.0F),
           "age penalty selects the strongest useful release sample");

    clear_manual_grenade_velocity_history(&history);
    expect(!select_manual_grenade_release_velocity(
               now, history, &selected, &age) &&
               age == 0U && near(selected.x, 0.0F),
           "cleared history returns a zeroed failure");
    clear_manual_grenade_velocity_history(nullptr);
}

void test_velocity_ring_has_twelve_samples_and_overwrites_oldest() {
    ManualGrenadeVelocityHistory history{};
    constexpr std::uint64_t base = 2'000'000'000ULL;
    for (std::size_t index = 0;
         index < kManualGrenadeVelocitySampleCapacity;
         ++index) {
        expect(record_manual_grenade_velocity(
                   base + index, {static_cast<float>(index + 1U), 0.0F,
                                  0.0F}, &history),
               "all twelve ring entries accept a sample");
    }
    expect(history.write_index == 0U && history.samples[0].valid,
           "twelve samples wrap exactly to ring start");
    expect(record_manual_grenade_velocity(
               base + 20U, {20.0F, 0.0F, 0.0F}, &history) &&
               history.write_index == 1U &&
               history.samples[0].sampled_monotonic_nanoseconds ==
                   base + 20U,
           "thirteenth sample overwrites only the oldest slot");
}

void test_velocity_cap_preserves_direction_and_rejects_bad_samples() {
    ManualGrenadeVelocityHistory history{};
    expect(record_manual_grenade_velocity(
               10U, {600.0F, 800.0F, 0.0F}, &history),
           "overspeed sample is accepted after bounding");
    ManualGrenadePoint selected{};
    std::uint64_t age{};
    expect(select_manual_grenade_release_velocity(
               10U, history, &selected, &age),
           "bounded sample remains selectable");
    expect_vector(selected, {300.0F, 400.0F, 0.0F},
                  "speed cap preserves the 3-4-5 direction");
    expect(!record_manual_grenade_velocity(
               0U, {1.0F, 2.0F, 3.0F}, &history) &&
               !record_manual_grenade_velocity(
                   11U,
                   {(std::numeric_limits<float>::infinity)(), 0.0F, 0.0F},
                   &history) &&
               !record_manual_grenade_velocity(
                   11U, {1.0F, 2.0F, 3.0F}, nullptr),
           "zero-time, non-finite, and null history samples are rejected");

    history.samples[0].valid = true;
    history.samples[0].sampled_monotonic_nanoseconds = 20U;
    history.samples[0].velocity_game_units_per_second.x =
        (std::numeric_limits<float>::quiet_NaN)();
    expect(!select_manual_grenade_release_velocity(
               15U, history, &selected, &age),
           "future and corrupted samples cannot be selected");
    expect(!select_manual_grenade_release_velocity(
               0U, history, &selected, &age) &&
               !select_manual_grenade_release_velocity(
                   15U, history, nullptr, &age) &&
               !select_manual_grenade_release_velocity(
                   15U, history, &selected, nullptr),
           "invalid selection arguments fail closed");
}

void test_launch_calibration_preserves_throw_and_drop() {
    const ManualGrenadeLaunchCalibration calibration{700.0F, 0.0F, 200.0F};
    ManualGrenadeLaunchResult result{};
    expect(build_manual_grenade_launch_velocity(
               {10.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, calibration,
               &result) &&
               result.deliberate_drop &&
               !result.used_fallback_direction &&
               near(result.normalized_strength, 0.0F),
           "still release remains a deliberate drop");
    expect_vector(result.velocity_game_units_per_second,
                  {10.0F, 0.0F, 0.0F},
                  "deliberate drop retains physical velocity");

    expect(build_manual_grenade_launch_velocity(
               {260.0F, 0.0F, 100.0F}, {0.0F, 1.0F, 0.0F}, calibration,
               &result) &&
               !result.deliberate_drop &&
               !result.used_fallback_direction &&
               near(result.normalized_strength, 1.0F) &&
               near(result.velocity_game_units_per_second.x, 805.0F) &&
               near(result.velocity_game_units_per_second.y, 0.0F) &&
               near(result.velocity_game_units_per_second.z, 265.0F),
           "full physical throw preserves direction and COD4 strength");

    expect(build_manual_grenade_launch_velocity(
               {0.0F, 0.0F, 100.0F}, {0.0F, 2.0F, 7.0F}, calibration,
               &result) &&
               result.used_fallback_direction &&
               near(result.velocity_game_units_per_second.x, 0.0F) &&
               result.velocity_game_units_per_second.y > 0.0F,
           "vertical throw uses normalized fallback horizontal direction");
}

void test_launch_strength_and_arc_are_bounded() {
    ManualGrenadeLaunchResult result{};
    const ManualGrenadeLaunchCalibration calibration{700.0F, 100.0F,
                                                       200.0F};
    expect(build_manual_grenade_launch_velocity(
               {35.0F, 0.0F, -1000.0F}, {0.0F, 1.0F, 0.0F}, calibration,
               &result) && !result.deliberate_drop &&
               near(result.normalized_strength, 1.0F),
           "large vertical component saturates strength safely");
    expect(near(result.velocity_game_units_per_second.z, 80.0F),
           "downward hand motion cannot remove minimum native arc");

    expect(build_manual_grenade_launch_velocity(
               {500.0F, 0.0F, 5000.0F}, {0.0F, 1.0F, 0.0F}, calibration,
               &result) &&
               near(result.normalized_strength, 1.0F) &&
               near(result.velocity_game_units_per_second.x, 920.0F) &&
               near(result.velocity_game_units_per_second.z, 640.0F),
           "strength and high upward arc clamp at their documented maxima");

    expect(build_manual_grenade_launch_velocity(
               {0.0F, 0.0F, 100.0F}, {0.0F, 0.0F, 1.0F}, {}, &result) &&
               result.used_fallback_direction &&
               result.velocity_game_units_per_second.x > 0.0F &&
               near(result.velocity_game_units_per_second.y, 0.0F) &&
               near(result.velocity_game_units_per_second.z, 80.0F),
           "missing horizontal native tuning and fallback use safe defaults");
}

void test_contextual_native_pickup_race_and_tombstone() {
    using Stage = ContextualThrowbackStage;
    using Ps = ContextualThrowbackPlayerState;
    using Decision = ContextualThrowbackFireDecision;
    ContextualThrowbackTransaction transaction{};
    expect(begin_contextual_throwback(1000, 7, 4, 300, &transaction),
           "positive contextual window begins a guarded transaction");
    expect(evaluate_contextual_throwback_fire(1100, Ps::predicted, 4, 0x3FF,
               &transaction) == Decision::hold &&
               evaluate_contextual_throwback_fire(1100, Ps::authoritative,
                   4, 0x3FF, &transaction) == Decision::hold,
           "both simulations wait before native event or own-ammo changes");
    expect(!consume_contextual_throwback_spawn(4, &transaction),
           "preflight alone cannot authorize a missile spawn");
    expect(evaluate_contextual_throwback_fire(1301, Ps::authoritative,
               4, 0x3FF, &transaction) == Decision::hold &&
               transaction.stage == Stage::aborted,
           "expired pickup stays held without clearing an unresolved native queue");
    expect(!begin_contextual_throwback(1400, 8, 4, 1000, &transaction) &&
               !retire_with_simulated_native_drain(true, true, false,
                   &transaction) &&
               !retire_with_simulated_native_drain(false, true, true,
                   &transaction),
           "abort survives gesture retirement and one-copy prediction lag");
    expect(evaluate_contextual_throwback_fire(1400, Ps::predicted, 4, 0x3FF,
               &transaction) == Decision::hold &&
               retire_with_simulated_native_drain(true, true, true,
                   &transaction) && transaction.stage == Stage::inactive,
           "idle states and release require an additional simulated drain witness");
}

void test_contextual_pickup_requires_real_source_consumption() {
    ContextualThrowbackTransaction transaction{};
    expect(begin_contextual_throwback(1000, 9, 4, 1500, &transaction),
           "fresh generation starts for acquisition witness");
    expect(!witness_contextual_throwback_pickup(1010, 8, 4, 90, 1490,
               true, true, &transaction) &&
               transaction.stage == ContextualThrowbackStage::waiting_for_pickup,
           "an old callback cannot consume a new generation");
    expect(!witness_contextual_throwback_pickup(1020, 9, 4, 90, 1480,
               true, false, &transaction) &&
               transaction.stage == ContextualThrowbackStage::aborted,
           "owner and copied fuse do not suffice when native pickup refused");
    expect(retire_with_simulated_native_drain(true, true, true,
               &transaction) &&
               begin_contextual_throwback(2000, 10, 4, 1500, &transaction) &&
               !witness_contextual_throwback_pickup(2010, 10, 4, 0x3FF,
                   1490, true, true, &transaction),
           "fresh-own-frag owner sentinel cannot become an enemy witness");
}

void test_contextual_zero_inventory_native_replay_and_one_spawn() {
    using Ps = ContextualThrowbackPlayerState;
    using Decision = ContextualThrowbackFireDecision;
    ContextualThrowbackTransaction transaction{};
    // There is intentionally no player-ammo count in this transaction. Only
    // the actual native enemy pickup grants permission, even at zero ammo.
    expect(begin_contextual_throwback(2000, 11, 4, 1500, &transaction) &&
               witness_contextual_throwback_pickup(2010, 11, 4, 112,
                   1490, true, true, &transaction),
           "real enemy pickup authorizes throwback without inventory dependency");
    expect(evaluate_contextual_throwback_fire(2020, Ps::predicted, 4, 0x3FF,
               &transaction) == Decision::hold &&
               evaluate_contextual_throwback_fire(2020, Ps::authoritative,
                   4, 112, &transaction) == Decision::allow_enemy,
           "server can proceed while prediction awaits the same owner witness");
    expect(evaluate_contextual_throwback_fire(2021, Ps::predicted, 4, 112,
               &transaction) == Decision::hold &&
               evaluate_contextual_throwback_fire(2021, Ps::predicted, 4, 112,
                   &transaction) == Decision::hold,
           "prediction replays hold after authoritative dispatch is permitted");
    expect(evaluate_contextual_throwback_fire(2022, Ps::authoritative,
               4, 112, &transaction) == Decision::hold,
           "one generation cannot enqueue a second authoritative fire event");
    expect(!consume_contextual_throwback_spawn(5, &transaction) &&
               complete_one_simulated_owned_dispatch(4, &transaction) &&
               !consume_contextual_throwback_spawn(4, &transaction),
           "native spawn permission is weapon-specific and consumed once");
    expect(evaluate_contextual_throwback_fire(2023, Ps::predicted, 4, 112,
               &transaction) == Decision::hold &&
               evaluate_contextual_throwback_fire(2023, Ps::unrelated, 4,
                   0x3FF, &transaction) == Decision::passthrough,
           "spent local copies remain blocked without touching other entities");
    abort_contextual_throwback(&transaction);
    expect(transaction.stage == ContextualThrowbackStage::spent,
           "focus or visual cleanup cannot erase a spent tombstone");
}

void test_contextual_pickup_is_rejected_before_native_side_effects() {
    using Stage = ContextualThrowbackStage;
    ContextualThrowbackTransaction transaction{};
    expect(begin_contextual_throwback(1000, 90, 4, 300, &transaction),
           "pre-consumption guard gets a bounded native window");
    expect(!validate_contextual_throwback_pickup_attempt(1100, 89,
               &transaction) && transaction.stage == Stage::waiting_for_pickup &&
               transaction.last_observed_milliseconds == 1000,
           "stale generation cannot consume or advance the current acquisition");
    expect(validate_contextual_throwback_pickup_attempt(1299, 90,
               &transaction), "native pickup is eligible immediately before deadline");
    int native_consumption_count = 0;
    if (validate_contextual_throwback_pickup_attempt(1300, 90, &transaction))
        ++native_consumption_count;
    expect(native_consumption_count == 0 && transaction.stage == Stage::aborted,
           "deadline equality blocks the callback before a native missile is consumed");
    expect(!witness_contextual_throwback_pickup(1300, 90, 4, 112, 1,
               true, true, &transaction),
           "post-return evidence cannot revive a pre-consumption expiration");
    expect(retire_with_simulated_native_drain(true, true, true,
               &transaction) && begin_contextual_throwback(2000, 91, 4,
                   1500, &transaction) &&
               validate_contextual_throwback_pickup_attempt(2020, 91,
                   &transaction) &&
               !validate_contextual_throwback_pickup_attempt(2019, 91,
                   &transaction) && transaction.stage == Stage::aborted,
           "a clock regression fails closed instead of extending pickup eligibility");
    expect(!validate_contextual_throwback_pickup_attempt(2050, 0, nullptr),
           "invalid preflight arguments never allow native consumption");
}

void test_contextual_fuse_and_clock_cannot_be_refreshed() {
    using Ps = ContextualThrowbackPlayerState;
    using Decision = ContextualThrowbackFireDecision;
    ContextualThrowbackTransaction transaction{};
    expect(begin_contextual_throwback(1000, 92, 4, 300, &transaction) &&
               !witness_contextual_throwback_pickup(1001, 92, 4, 112, 301,
                   true, true, &transaction),
           "acquisition cannot replace the observed enemy fuse with a longer fuse");
    expect(retire_with_simulated_native_drain(true, true, true,
               &transaction) && begin_contextual_throwback(2000, 93, 4, 900,
                   &transaction) &&
               witness_contextual_throwback_pickup(2020, 93, 4, 112, 880,
                   true, true, &transaction),
           "native decreasing fuse is kept as an unchanged acquisition witness");
    expect(evaluate_contextual_throwback_fire(2021, Ps::predicted, 4, 112,
               &transaction) == Decision::allow_enemy &&
               evaluate_contextual_throwback_fire(2019, Ps::authoritative,
                   4, 112, &transaction) == Decision::hold &&
               !consume_contextual_throwback_spawn(4, &transaction),
           "clock regression after acquisition cannot authorize a later spawn");
    expect(retire_with_simulated_native_drain(true, true, true,
               &transaction) && begin_contextual_throwback(3000, 94, 4, 900,
                   &transaction) &&
               evaluate_contextual_throwback_fire(3001,
                   static_cast<Ps>(255), 4, 112, &transaction) == Decision::hold,
           "unknown player-state roles cannot inherit a local enemy permission");
}

void test_contextual_native_dispatch_requires_owned_receipt_and_completion() {
    using Ps = ContextualThrowbackPlayerState;
    using Decision = ContextualThrowbackFireDecision;
    using Stage = ContextualThrowbackStage;
    ContextualThrowbackTransaction transaction{};
    expect(begin_contextual_throwback(1000, 200, 4, 1000, &transaction) &&
        witness_contextual_throwback_pickup(1010, 200, 4, 112, 990,
            true, true, &transaction) &&
        evaluate_contextual_throwback_fire(1020, Ps::authoritative, 4, 112,
            &transaction) == Decision::allow_enemy,
        "unit model reserves one authoritative permission after real pickup");
    ContextualThrowbackNativeEventReceipt receipt{false, 200, 39};
    expect(!consume_contextual_throwback_spawn(4, &transaction) &&
        !consume_contextual_throwback_spawn(4, &transaction, &receipt) &&
        transaction.stage == Stage::acquired && transaction.dispatch_receipt == 0,
        "no adapter or unverified low ordinal cannot become native event identity");
    receipt = {true, 199, 10000};
    expect(!consume_contextual_throwback_spawn(4, &transaction, &receipt) &&
        transaction.generation == 200 && transaction.stage == Stage::acquired,
        "stale owned event cannot spend a newer same-weapon transaction");
    receipt = {true, 200, 0};
    expect(!consume_contextual_throwback_spawn(4, &transaction, &receipt),
        "an empty receipt is unknown even when generation matches");
    receipt = {true, 200, 10001};
    expect(consume_contextual_throwback_spawn(4, &transaction, &receipt) &&
        transaction.stage == Stage::dispatch_in_flight &&
        !consume_contextual_throwback_spawn(4, &transaction, &receipt) &&
        !retire_with_simulated_native_drain(true, true, true, &transaction),
        "handler permission stays in-flight, non-repeatable and non-retirable");
    auto wrong = receipt;
    ++wrong.dispatch_receipt;
    expect(!complete_contextual_throwback_dispatch(wrong, &transaction) &&
        transaction.stage == Stage::dispatch_in_flight,
        "different event receipt cannot acknowledge native handler completion");
    wrong = receipt;
    --wrong.generation;
    expect(!complete_contextual_throwback_dispatch(wrong, &transaction) &&
        complete_contextual_throwback_dispatch(receipt, &transaction) &&
        transaction.stage == Stage::spent &&
        !complete_contextual_throwback_dispatch(receipt, &transaction),
        "only exact generation and event receipt complete once after native return");
    expect(!retire_contextual_throwback_tombstone(true, true, true, &transaction),
        "both idle native copies still do not prove their old queues drained");
    ContextualThrowbackNativeDrainWitness drain{199, true, true};
    expect(!retire_contextual_throwback_tombstone(true, true, true,
        &transaction, &drain), "stale drain cannot erase a current tombstone");
    drain = {200, true, false};
    expect(!retire_contextual_throwback_tombstone(true, true, true,
        &transaction, &drain), "one drained queue cannot authorize rearm");
    drain.predicted_queue_drained = true;
    expect(retire_contextual_throwback_tombstone(true, true, true,
        &transaction, &drain) &&
        begin_contextual_throwback(2000, 201, 4, 1000, &transaction) &&
        !consume_contextual_throwback_spawn(4, &transaction, &receipt),
        "old same-weapon receipt remains unusable after verified model retirement");
}

void test_contextual_queued_handler_never_cancels_owner_or_fuse() {
    using Ps = ContextualThrowbackPlayerState;
    using Decision = ContextualThrowbackFireDecision;
    for (std::uint64_t generation = 1; generation <= 1024; ++generation) {
        ContextualThrowbackTransaction transaction{};
        expect(begin_contextual_throwback(1000, generation, 4, 1000,
            &transaction) && witness_contextual_throwback_pickup(1010,
            generation, 4, 112, 990, true, true, &transaction) &&
            evaluate_contextual_throwback_fire(1020, Ps::authoritative,
                4, 112, &transaction) == Decision::allow_enemy,
            "stress model queues one owned enemy permission");
        const auto owner = transaction.native_owner;
        const auto fuse = transaction.fuse_at_pickup;
        expect(evaluate_contextual_throwback_fire(1019, Ps::authoritative,
            5, 0x3FF, &transaction) == Decision::hold &&
            evaluate_contextual_throwback_fire(0, Ps::predicted,
                4, 0x3FF, &transaction) == Decision::hold,
            "queued-event mismatch/regressed clock never takes native cancel");
        abort_contextual_throwback(&transaction);
        const ContextualThrowbackNativeEventReceipt receipt{
            true, generation, 10000 + generation};
        expect(consume_contextual_throwback_spawn(4, &transaction, &receipt) &&
            evaluate_contextual_throwback_fire(0, Ps::authoritative,
                0, 0x3FF, &transaction) == Decision::hold &&
            !retire_contextual_throwback_tombstone(true, true, true,
                &transaction) &&
            transaction.native_owner == owner &&
            transaction.fuse_at_pickup == fuse,
            "visual abort and apparent idle cannot clear an in-flight owner/fuse");
        expect(complete_contextual_throwback_dispatch(receipt, &transaction) &&
            evaluate_contextual_throwback_fire(2000, Ps::predicted, 4, 112,
                &transaction) == Decision::hold &&
            retire_with_simulated_native_drain(true, true, true, &transaction),
            "completion alone holds until separate generation-bound model drain");
    }
}

void test_contextual_scheduled_expiry_is_not_default_fuse_argument_equality() {
    ContextualThrowbackExpiryWitness witness{
        true, true, true, 5000, 3000, 3500, 5000, 1500};
    expect(contextual_throwback_scheduled_expiry_preserved(witness),
        "delayed native processing preserves absolute expiry not original duration");
    witness.spawned_scheduled_expiry = 8500;
    expect(!contextual_throwback_scheduled_expiry_preserved(witness),
        "default fuse argument equality cannot disguise refreshed expiry");
    witness.spawned_scheduled_expiry = 5000;
    witness.spawn_native_time = 5100;
    witness.remaining_fuse_at_spawn = -100;
    expect(contextual_throwback_scheduled_expiry_preserved(witness),
        "past scheduled expiry retains native negative cook time without refresh");
    witness.spawn_native_time = 5000;
    witness.remaining_fuse_at_spawn = 0;
    expect(contextual_throwback_scheduled_expiry_preserved(witness),
        "zero remaining fuse is an expiry boundary not a fresh default");
    witness.remaining_fuse_at_spawn = 5000;
    expect(!contextual_throwback_scheduled_expiry_preserved(witness),
        "remaining fuse must agree with the witnessed scheduled native expiry");
    witness.remaining_fuse_at_spawn = 0;
    auto missing = witness;
    missing.same_native_clock_verified = false;
    expect(!contextual_throwback_scheduled_expiry_preserved(missing),
        "unverified native clock cannot be compared with mod monotonic time");
    missing = witness;
    missing.spawned_missile_identity_verified = false;
    expect(!contextual_throwback_scheduled_expiry_preserved(missing),
        "unrelated or missing replacement missile cannot prove fuse preservation");
    missing = witness;
    missing.source_identity_verified = false;
    expect(!contextual_throwback_scheduled_expiry_preserved(missing),
        "missing source identity cannot prove fuse preservation");
    witness.spawn_native_time = 2999;
    expect(!contextual_throwback_scheduled_expiry_preserved(witness),
        "native time regression is rejected");
    const auto maximum = (std::numeric_limits<std::int64_t>::max)();
    witness = {true, true, true, maximum, maximum - 100, maximum,
        maximum, 0};
    expect(contextual_throwback_scheduled_expiry_preserved(witness),
        "comparison at native-clock bound does not add or overflow expiry");
    witness.spawned_scheduled_expiry = maximum - 1;
    expect(!contextual_throwback_scheduled_expiry_preserved(witness),
        "one-tick scheduled-expiry mismatch is not silently accepted");
    witness = {true, true, true, 1, 0, maximum, 1, -1};
    expect(!contextual_throwback_scheduled_expiry_preserved(witness),
        "extreme expired duration cannot wrap into a small negative fuse");
    witness = {true, true, true, 1, 0,
        1LL - (std::numeric_limits<std::int32_t>::min)(), 1,
        (std::numeric_limits<std::int32_t>::min)()};
    expect(contextual_throwback_scheduled_expiry_preserved(witness),
        "signed minimum native cook time is compared without negating int32");
    witness = {};
    expect(!contextual_throwback_scheduled_expiry_preserved(witness),
        "unknown runtime expiry observations fail closed by default");
}

void test_contextual_pickup_boundary_stress_keeps_one_spawn() {
    using Ps = ContextualThrowbackPlayerState;
    using Decision = ContextualThrowbackFireDecision;
    for (std::uint64_t index = 1; index <= 1024; ++index) {
        ContextualThrowbackTransaction transaction{};
        const auto now = index * 10000;
        const auto window = static_cast<std::int32_t>(1 + index % 1500);
        const auto wait = (std::min)(window, 1000);
        expect(begin_contextual_throwback(now, index, 4, window, &transaction) &&
                   validate_contextual_throwback_pickup_attempt(
                       now + wait - 1, index, &transaction) &&
                   witness_contextual_throwback_pickup(now + wait - 1,
                       index, 4, 112, 1, true, true, &transaction) &&
                   evaluate_contextual_throwback_fire(now + wait,
                       Ps::authoritative, 4, 112, &transaction) ==
                       Decision::allow_enemy &&
                   complete_one_simulated_owned_dispatch(4, &transaction) &&
                   !consume_contextual_throwback_spawn(4, &transaction),
               "bounded acquisition gives one server spawn permission only");
        expect(retire_with_simulated_native_drain(true, true, true,
                   &transaction) &&
                   begin_contextual_throwback(now + 2000, index + 2048,
                       4, window, &transaction) &&
                   !validate_contextual_throwback_pickup_attempt(
                       now + 2000 + wait, index + 2048, &transaction),
               "every bounded deadline rejects before consumption at equality");
    }
}

void test_launch_invalid_inputs_clear_result() {
    ManualGrenadeLaunchResult result{
        .velocity_game_units_per_second = {1.0F, 2.0F, 3.0F},
        .deliberate_drop = true,
        .used_fallback_direction = true,
        .normalized_strength = 1.0F,
    };
    const auto nan = (std::numeric_limits<float>::quiet_NaN)();
    expect(!build_manual_grenade_launch_velocity(
               {nan, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {}, &result) &&
               near(result.velocity_game_units_per_second.x, 0.0F) &&
               !result.deliberate_drop && !result.used_fallback_direction,
           "non-finite physical velocity clears output");
    expect(!build_manual_grenade_launch_velocity(
               {}, {nan, 0.0F, 0.0F}, {}, &result),
           "non-finite fallback fails closed even for a drop");
    expect(!build_manual_grenade_launch_velocity(
               {}, {1.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F}, &result) &&
               !build_manual_grenade_launch_velocity(
                   {}, {1.0F, 0.0F, 0.0F},
                   {kManualGrenadeMaximumNativeProjectileSpeed + 1.0F,
                    0.0F, 0.0F},
                   &result),
           "negative and implausible native calibration fail closed");
    expect(!build_manual_grenade_launch_velocity(
               {}, {1.0F, 0.0F, 0.0F}, {}, nullptr),
           "null launch output is rejected");
}

}  // namespace

int main() {
    test_belt_slots_are_mirrored_and_finite();
    test_throwback_override_is_frag_only();
    test_contextual_throwback_requires_a_fresh_edge_and_never_uses_belt_slot();
    test_startup_requires_a_fresh_trigger_edge();
    test_trigger_hysteresis_releases_once();
    test_tactical_slot_and_outside_edge_policy();
    test_new_grab_arbitration_consumes_edge_but_never_blocks_release();
    test_focus_and_tracking_loss_cancel_without_throw();
    test_invalid_gesture_inputs_fail_closed();
    test_velocity_history_selects_recent_strongest();
    test_velocity_ring_has_twelve_samples_and_overwrites_oldest();
    test_velocity_cap_preserves_direction_and_rejects_bad_samples();
    test_launch_calibration_preserves_throw_and_drop();
    test_launch_strength_and_arc_are_bounded();
    test_contextual_native_pickup_race_and_tombstone();
    test_contextual_pickup_requires_real_source_consumption();
    test_contextual_zero_inventory_native_replay_and_one_spawn();
    test_contextual_pickup_is_rejected_before_native_side_effects();
    test_contextual_fuse_and_clock_cannot_be_refreshed();
    test_contextual_pickup_boundary_stress_keeps_one_spawn();
    test_contextual_native_dispatch_requires_owned_receipt_and_completion();
    test_contextual_queued_handler_never_cancels_owner_or_fuse();
    test_contextual_scheduled_expiry_is_not_default_fuse_argument_equality();
    test_launch_invalid_inputs_clear_result();

    if (failures != 0) {
        std::cerr << failures << " manual-grenade logic test(s) failed\n";
        return 1;
    }
    std::cout << "Manual-grenade logic tests passed\n";
    return 0;
}
