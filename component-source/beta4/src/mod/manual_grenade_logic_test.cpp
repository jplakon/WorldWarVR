// SPDX-License-Identifier: GPL-3.0-only
#include "manual_grenade_logic.hpp"

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
    test_launch_invalid_inputs_clear_result();

    if (failures != 0) {
        std::cerr << failures << " manual-grenade logic test(s) failed\n";
        return 1;
    }
    std::cout << "Manual-grenade logic tests passed\n";
    return 0;
}
