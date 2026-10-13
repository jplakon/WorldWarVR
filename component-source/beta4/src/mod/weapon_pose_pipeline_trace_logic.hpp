// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "weapon_grip_logic.hpp"

#include "xr_types.h"

#include <cstdint>
#include <limits>

namespace wawvr::mod {

// This module is diagnostic-only. It accepts immutable copies of the pose
// stages that production placement already calculated and cannot modify any
// controller, attachment, or viewmodel state.
inline constexpr std::uint32_t
    kWeaponPosePipelineTracePublicationsPerReport = 120;

struct WeaponPosePipelineTraceMetric final {
    std::uint32_t count{};
    double sum{};
    double sum_squared{};
    float minimum{(std::numeric_limits<float>::max)()};
    float maximum{};
};

struct WeaponPosePipelineTracePose final {
    bool valid{};
    wawvr::xr::Vec3f position{};
    wawvr::xr::Basis3f axis{};
};

struct WeaponPosePipelineTraceDirection final {
    bool valid{};
    wawvr::xr::Vec3f direction{};
};

struct WeaponPosePipelineTraceInput final {
    WeaponGripMode grip_mode{WeaponGripMode::Chest};
    std::uint64_t weapon_identity{};
    bool transition_pending{};

    std::uint64_t generation{};
    std::uint64_t frame_id{};
    std::uint64_t publication_milliseconds{};
    std::uint64_t render_milliseconds{};
    std::int64_t predicted_display_time_nanoseconds{};
    std::int64_t predicted_display_period_nanoseconds{};

    WeaponPosePipelineTracePose raw_right{};
    WeaponPosePipelineTracePose filtered_right{};
    WeaponPosePipelineTracePose raw_left{};
    WeaponPosePipelineTracePose filtered_left{};
    WeaponPosePipelineTraceDirection pre_final_two_hand_direction{};
    WeaponPosePipelineTraceDirection post_final_two_hand_direction{};
    WeaponPosePipelineTracePose applied_weapon{};
    WeaponPosePipelineTraceDirection visible_weapon_direction{};
};

struct WeaponPosePipelineTracePoseMetrics final {
    std::uint32_t samples{};
    WeaponPosePipelineTraceMetric position_step_millimeters{};
    WeaponPosePipelineTraceMetric orientation_step_degrees{};
};

struct WeaponPosePipelineTracePoseErrorMetrics final {
    std::uint32_t samples{};
    WeaponPosePipelineTraceMetric position_error_millimeters{};
    WeaponPosePipelineTraceMetric orientation_error_degrees{};
};

struct WeaponPosePipelineTraceWindow final {
    WeaponGripMode grip_mode{WeaponGripMode::Chest};
    std::uint64_t weapon_identity{};
    std::uint64_t first_generation{};
    std::uint64_t last_generation{};
    std::uint64_t first_frame_id{};
    std::uint64_t last_frame_id{};

    std::uint32_t unique_publications{};
    std::uint32_t render_calls{};
    std::uint32_t duplicate_render_calls{};

    WeaponPosePipelineTraceMetric render_delta_milliseconds{};
    WeaponPosePipelineTraceMetric publication_delta_milliseconds{};
    WeaponPosePipelineTraceMetric predicted_display_step_milliseconds{};
    WeaponPosePipelineTraceMetric predicted_display_period_milliseconds{};
    WeaponPosePipelineTraceMetric publication_to_render_age_milliseconds{};

    WeaponPosePipelineTracePoseMetrics raw_right{};
    WeaponPosePipelineTracePoseMetrics filtered_right{};
    WeaponPosePipelineTracePoseErrorMetrics right_raw_to_filtered{};
    WeaponPosePipelineTracePoseMetrics raw_left{};
    WeaponPosePipelineTracePoseMetrics filtered_left{};
    WeaponPosePipelineTracePoseErrorMetrics left_raw_to_filtered{};

    std::uint32_t pre_final_two_hand_samples{};
    std::uint32_t post_final_two_hand_samples{};
    std::uint32_t paired_two_hand_samples{};
    WeaponPosePipelineTraceMetric
        pre_final_two_hand_direction_step_degrees{};
    WeaponPosePipelineTraceMetric
        post_final_two_hand_direction_step_degrees{};
    WeaponPosePipelineTraceMetric
        pre_to_post_two_hand_error_degrees{};

    WeaponPosePipelineTracePoseMetrics applied_weapon{};
    std::uint32_t visible_weapon_samples{};
    WeaponPosePipelineTraceMetric visible_weapon_direction_step_degrees{};
    // When T4 consumes one immutable publication more than once, this metric
    // detects render-time movement that publication-only metrics would hide.
    WeaponPosePipelineTraceMetric
        same_publication_visible_direction_step_degrees{};
};

struct WeaponPosePipelineTraceReport final {
    bool valid{};
    WeaponPosePipelineTraceWindow window{};
};

struct WeaponPosePipelineTraceState final {
    bool valid{};
    WeaponGripMode grip_mode{WeaponGripMode::Chest};
    std::uint64_t weapon_identity{};
    std::uint64_t last_generation{};
    std::uint64_t last_frame_id{};
    std::uint64_t last_publication_milliseconds{};
    std::uint64_t last_render_milliseconds{};
    std::int64_t last_predicted_display_time_nanoseconds{};

    WeaponPosePipelineTracePose previous_raw_right{};
    WeaponPosePipelineTracePose previous_filtered_right{};
    WeaponPosePipelineTracePose previous_raw_left{};
    WeaponPosePipelineTracePose previous_filtered_left{};
    WeaponPosePipelineTraceDirection
        previous_pre_final_two_hand_direction{};
    WeaponPosePipelineTraceDirection
        previous_post_final_two_hand_direction{};
    WeaponPosePipelineTracePose previous_applied_weapon{};
    WeaponPosePipelineTraceDirection previous_visible_weapon_direction{};
    WeaponPosePipelineTraceDirection last_render_visible_weapon_direction{};

    WeaponPosePipelineTraceWindow window{};
};

// Returns false and clears state for malformed/regressed input. Chest,
// ownership-transition, weapon-change, mode-change, or forward-gap boundaries
// start a fresh segment and never bridge pose-step history across the boundary.
// A same-generation call contributes only render cadence and the optional
// same-publication visible residual. The report output becomes valid exactly
// once per 120 accepted unique publications; the caller owns any report cap.
[[nodiscard]] bool observe_weapon_pose_pipeline_trace(
    const WeaponPosePipelineTraceInput& input,
    WeaponPosePipelineTraceState* state,
    WeaponPosePipelineTraceReport* report) noexcept;

void reset_weapon_pose_pipeline_trace(
    WeaponPosePipelineTraceState* state) noexcept;

[[nodiscard]] float weapon_pose_pipeline_trace_metric_mean(
    const WeaponPosePipelineTraceMetric& metric) noexcept;

[[nodiscard]] float weapon_pose_pipeline_trace_metric_rms(
    const WeaponPosePipelineTraceMetric& metric) noexcept;

}  // namespace wawvr::mod
