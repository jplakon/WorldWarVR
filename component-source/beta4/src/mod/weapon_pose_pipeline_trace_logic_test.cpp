// SPDX-License-Identifier: GPL-3.0-only
#include "weapon_pose_pipeline_trace_logic.hpp"

#include "xr_math.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

constexpr float kPi = 3.14159265358979323846F;

void expect(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

[[nodiscard]] bool near(
    const float left,
    const float right,
    const float tolerance = 0.001F) noexcept {
    return std::abs(left - right) <= tolerance;
}

[[nodiscard]] wawvr::xr::Vec3f yaw_direction(
    const float degrees) noexcept {
    const float radians = degrees * kPi / 180.0F;
    return {std::cos(radians), std::sin(radians), 0.0F};
}

[[nodiscard]] wawvr::xr::Basis3f yaw_axis(
    const float degrees) noexcept {
    const auto forward = yaw_direction(degrees);
    return {
        forward,
        {-forward.y, forward.x, 0.0F},
        {0.0F, 0.0F, 1.0F},
    };
}

[[nodiscard]] wawvr::mod::WeaponPosePipelineTracePose pose(
    const float x_iw,
    const float yaw_degrees) noexcept {
    return {true, {x_iw, 0.0F, 0.0F}, yaw_axis(yaw_degrees)};
}

[[nodiscard]] wawvr::mod::WeaponPosePipelineTraceDirection direction(
    const float yaw_degrees) noexcept {
    return {true, yaw_direction(yaw_degrees)};
}

[[nodiscard]] wawvr::mod::WeaponPosePipelineTraceInput input(
    const wawvr::mod::WeaponGripMode mode,
    const std::uint64_t generation,
    const std::uint64_t publication_milliseconds,
    const std::uint64_t render_milliseconds) noexcept {
    wawvr::mod::WeaponPosePipelineTraceInput result{};
    result.grip_mode = mode;
    result.weapon_identity = 16;
    result.generation = generation;
    result.frame_id = 100 + generation;
    result.publication_milliseconds = publication_milliseconds;
    result.render_milliseconds = render_milliseconds;
    result.predicted_display_time_nanoseconds =
        1'000'000'000LL +
        static_cast<std::int64_t>(generation - 1) * 28'000'000LL;
    result.predicted_display_period_nanoseconds = 14'000'000LL;
    result.raw_right = pose(0.0F, 0.0F);
    result.filtered_right = pose(0.0F, 0.0F);
    result.applied_weapon = pose(0.0F, 0.0F);
    result.visible_weapon_direction = direction(0.0F);
    if (mode == wawvr::mod::WeaponGripMode::TwoHand) {
        result.raw_left = pose(10.0F, 0.0F);
        result.filtered_left = pose(10.0F, 0.0F);
        result.pre_final_two_hand_direction = direction(0.0F);
        result.post_final_two_hand_direction = direction(0.0F);
    }
    return result;
}

void test_publication_and_render_cadence_are_independent() {
    wawvr::mod::WeaponPosePipelineTraceState state{};
    wawvr::mod::WeaponPosePipelineTraceReport report{};
    const auto first = input(
        wawvr::mod::WeaponGripMode::RightHand, 1, 1'000, 1'000);
    expect(
        wawvr::mod::observe_weapon_pose_pipeline_trace(
            first, &state, &report) && !report.valid,
        "first cadence sample seeds");

    auto duplicate = first;
    duplicate.render_milliseconds = 1'014;
    expect(
        wawvr::mod::observe_weapon_pose_pipeline_trace(
            duplicate, &state, &report) && !report.valid,
        "duplicate renderer consumer is accepted");
    expect(
        state.window.unique_publications == 1 &&
            state.window.render_calls == 2 &&
            state.window.duplicate_render_calls == 1,
        "duplicate increments only render counters");

    const auto second = input(
        wawvr::mod::WeaponGripMode::RightHand, 2, 1'028, 1'028);
    expect(
        wawvr::mod::observe_weapon_pose_pipeline_trace(
            second, &state, &report),
        "second publication is accepted");
    expect(
        state.window.unique_publications == 2 &&
            state.window.render_calls == 3 &&
            state.window.duplicate_render_calls == 1 &&
            state.window.render_delta_milliseconds.count == 2 &&
            near(
                wawvr::mod::weapon_pose_pipeline_trace_metric_mean(
                    state.window.render_delta_milliseconds),
                14.0F) &&
            state.window.publication_delta_milliseconds.count == 1 &&
            near(
                wawvr::mod::weapon_pose_pipeline_trace_metric_mean(
                    state.window.publication_delta_milliseconds),
                28.0F) &&
            near(
                wawvr::mod::weapon_pose_pipeline_trace_metric_mean(
                    state.window.predicted_display_period_milliseconds),
                14.0F),
        "28 ms publication and 14 ms render/display cadences remain distinct");
}

void test_mode_change_reseeds_without_bridging() {
    wawvr::mod::WeaponPosePipelineTraceState state{};
    wawvr::mod::WeaponPosePipelineTraceReport report{};
    auto right = input(
        wawvr::mod::WeaponGripMode::RightHand, 1, 1'000, 1'000);
    right.applied_weapon = pose(0.0F, 0.0F);
    expect(
        wawvr::mod::observe_weapon_pose_pipeline_trace(
            right, &state, &report),
        "right-only segment seeds");

    auto two_hand = input(
        wawvr::mod::WeaponGripMode::TwoHand, 2, 1'028, 1'028);
    two_hand.applied_weapon = pose(20.0F, 45.0F);
    expect(
        wawvr::mod::observe_weapon_pose_pipeline_trace(
            two_hand, &state, &report),
        "two-hand mode starts a new segment");
    expect(
        state.valid &&
            state.grip_mode == wawvr::mod::WeaponGripMode::TwoHand &&
            state.window.unique_publications == 1 &&
            state.window.applied_weapon.samples == 1 &&
            state.window.applied_weapon.position_step_millimeters.count == 0 &&
            state.window.applied_weapon.orientation_step_degrees.count == 0,
        "mode boundary never bridges pose-step history");

    two_hand.transition_pending = true;
    two_hand.generation = 3;
    two_hand.frame_id = 103;
    two_hand.publication_milliseconds = 1'056;
    two_hand.render_milliseconds = 1'056;
    expect(
        !wawvr::mod::observe_weapon_pose_pipeline_trace(
            two_hand, &state, &report) && !state.valid,
        "ownership transition clears the diagnostic segment");
}

void test_missing_hand_and_pair_stages_preserve_one_hand_metrics() {
    wawvr::mod::WeaponPosePipelineTraceState state{};
    wawvr::mod::WeaponPosePipelineTraceReport report{};
    auto first = input(
        wawvr::mod::WeaponGripMode::RightHand, 1, 1'000, 1'000);
    first.raw_right = pose(0.0F, 0.0F);
    first.filtered_right = pose(0.0F, 0.0F);
    expect(
        wawvr::mod::observe_weapon_pose_pipeline_trace(
            first, &state, &report),
        "one-hand sample with absent left/pair stages seeds");

    auto second = input(
        wawvr::mod::WeaponGripMode::RightHand, 2, 1'028, 1'028);
    second.raw_right = pose(
        wawvr::xr::kIwUnitsPerMeter * 0.001F, 1.0F);
    second.filtered_right = pose(0.0F, 0.25F);
    expect(
        wawvr::mod::observe_weapon_pose_pipeline_trace(
            second, &state, &report),
        "second one-hand sample remains valid without left/pair stages");
    expect(
        state.window.raw_right.samples == 2 &&
            state.window.raw_right.position_step_millimeters.count == 1 &&
            near(
                state.window.raw_right.position_step_millimeters.maximum,
                1.0F, 0.01F) &&
            near(
                state.window.raw_right.orientation_step_degrees.maximum,
                1.0F, 0.01F) &&
            state.window.filtered_right.samples == 2 &&
            state.window.right_raw_to_filtered.samples == 2 &&
            state.window.raw_left.samples == 0 &&
            state.window.filtered_left.samples == 0 &&
            state.window.pre_final_two_hand_samples == 0 &&
            state.window.post_final_two_hand_samples == 0,
        "active-hand metrics advance independently of missing optional stages");
}

void test_regression_and_nonfinite_inputs_fail_closed() {
    wawvr::mod::WeaponPosePipelineTraceState state{};
    wawvr::mod::WeaponPosePipelineTraceReport report{};
    expect(
        wawvr::mod::observe_weapon_pose_pipeline_trace(
            input(
                wawvr::mod::WeaponGripMode::RightHand,
                2, 1'028, 1'028),
            &state, &report),
        "regression test seeds");
    expect(
        !wawvr::mod::observe_weapon_pose_pipeline_trace(
            input(
                wawvr::mod::WeaponGripMode::RightHand,
                1, 1'000, 1'040),
            &state, &report) && !state.valid,
        "generation regression fails closed");

    auto invalid = input(
        wawvr::mod::WeaponGripMode::RightHand, 3, 2'000, 2'000);
    invalid.raw_right.position.x =
        (std::numeric_limits<float>::quiet_NaN)();
    expect(
        !wawvr::mod::observe_weapon_pose_pipeline_trace(
            invalid, &state, &report) && !state.valid,
        "nonfinite valid pose fails closed");
}

void test_report_is_ready_at_120_unique_publications() {
    wawvr::mod::WeaponPosePipelineTraceState state{};
    wawvr::mod::WeaponPosePipelineTraceReport report{};
    for (std::uint64_t generation = 1;
         generation <=
             wawvr::mod::kWeaponPosePipelineTracePublicationsPerReport;
         ++generation) {
        const std::uint64_t milliseconds = 10'000 + (generation - 1) * 14;
        auto sample = input(
            wawvr::mod::WeaponGripMode::TwoHand,
            generation, milliseconds, milliseconds);
        sample.frame_id = 1'000 + generation;
        sample.predicted_display_time_nanoseconds =
            5'000'000'000LL +
            static_cast<std::int64_t>(generation - 1) * 14'000'000LL;
        expect(
            wawvr::mod::observe_weapon_pose_pipeline_trace(
                sample, &state, &report),
            "bounded report sample is accepted");
        expect(
            report.valid ==
                (generation ==
                 wawvr::mod::kWeaponPosePipelineTracePublicationsPerReport),
            "report readiness occurs only on the 120th publication");
    }
    expect(
        report.valid &&
            report.window.unique_publications == 120 &&
            report.window.first_generation == 1 &&
            report.window.last_generation == 120 &&
            report.window.pre_final_two_hand_samples == 120 &&
            report.window.post_final_two_hand_samples == 120 &&
            state.valid && state.window.unique_publications == 0,
        "completed report is copied and the next bounded window starts empty");
}

}  // namespace

int main() {
    test_publication_and_render_cadence_are_independent();
    test_mode_change_reseeds_without_bridging();
    test_missing_hand_and_pair_stages_preserve_one_hand_metrics();
    test_regression_and_nonfinite_inputs_fail_closed();
    test_report_is_ready_at_120_unique_publications();
    std::cout << "weapon pose pipeline trace logic tests passed\n";
    return EXIT_SUCCESS;
}
