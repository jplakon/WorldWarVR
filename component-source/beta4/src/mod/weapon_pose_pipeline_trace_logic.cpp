// SPDX-License-Identifier: GPL-3.0-only
#include "weapon_pose_pipeline_trace_logic.hpp"

#include "xr_math.h"

#include <algorithm>
#include <cmath>

namespace wawvr::mod {
namespace {

constexpr double kNanosecondsToMilliseconds = 1.0 / 1'000'000.0;
constexpr float kRadiansToDegrees =
    180.0F / 3.14159265358979323846F;
constexpr float kIwToMillimeters =
    1000.0F / wawvr::xr::kIwUnitsPerMeter;

[[nodiscard]] bool finite_vector(
    const wawvr::xr::Vec3f& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] float dot(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] float length_squared(
    const wawvr::xr::Vec3f& value) noexcept {
    return dot(value, value);
}

[[nodiscard]] bool normalize(
    wawvr::xr::Vec3f value,
    wawvr::xr::Vec3f* const normalized) noexcept {
    if (normalized == nullptr || !finite_vector(value)) {
        return false;
    }
    const float magnitude_squared = length_squared(value);
    if (!std::isfinite(magnitude_squared) || magnitude_squared <= 1.0e-8F) {
        return false;
    }
    const float inverse_magnitude = 1.0F / std::sqrt(magnitude_squared);
    value.x *= inverse_magnitude;
    value.y *= inverse_magnitude;
    value.z *= inverse_magnitude;
    if (!finite_vector(value)) {
        return false;
    }
    *normalized = value;
    return true;
}

[[nodiscard]] bool valid_basis(
    const wawvr::xr::Basis3f& axis) noexcept {
    if (!finite_vector(axis.forward) || !finite_vector(axis.left) ||
        !finite_vector(axis.up)) {
        return false;
    }
    constexpr float kMinimumLengthSquared = 0.80F;
    constexpr float kMaximumLengthSquared = 1.20F;
    constexpr float kMaximumAxisDot = 0.20F;
    const float forward_length = length_squared(axis.forward);
    const float left_length = length_squared(axis.left);
    const float up_length = length_squared(axis.up);
    return forward_length >= kMinimumLengthSquared &&
        forward_length <= kMaximumLengthSquared &&
        left_length >= kMinimumLengthSquared &&
        left_length <= kMaximumLengthSquared &&
        up_length >= kMinimumLengthSquared &&
        up_length <= kMaximumLengthSquared &&
        std::abs(dot(axis.forward, axis.left)) <= kMaximumAxisDot &&
        std::abs(dot(axis.forward, axis.up)) <= kMaximumAxisDot &&
        std::abs(dot(axis.left, axis.up)) <= kMaximumAxisDot;
}

[[nodiscard]] bool valid_pose(
    const WeaponPosePipelineTracePose& pose) noexcept {
    return !pose.valid ||
        (finite_vector(pose.position) && valid_basis(pose.axis));
}

[[nodiscard]] bool valid_direction(
    const WeaponPosePipelineTraceDirection& direction) noexcept {
    if (!direction.valid) {
        return true;
    }
    wawvr::xr::Vec3f ignored{};
    return normalize(direction.direction, &ignored);
}

[[nodiscard]] float vector_distance(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    const wawvr::xr::Vec3f delta{
        right.x - left.x, right.y - left.y, right.z - left.z};
    return std::sqrt(length_squared(delta));
}

[[nodiscard]] float direction_angle_degrees(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    wawvr::xr::Vec3f normalized_left{};
    wawvr::xr::Vec3f normalized_right{};
    if (!normalize(left, &normalized_left) ||
        !normalize(right, &normalized_right)) {
        return (std::numeric_limits<float>::quiet_NaN)();
    }
    return std::acos(std::clamp(
        dot(normalized_left, normalized_right), -1.0F, 1.0F)) *
        kRadiansToDegrees;
}

[[nodiscard]] float basis_angle_degrees(
    const wawvr::xr::Basis3f& left,
    const wawvr::xr::Basis3f& right) noexcept {
    if (!valid_basis(left) || !valid_basis(right)) {
        return (std::numeric_limits<float>::quiet_NaN)();
    }
    const float relative_trace = dot(left.forward, right.forward) +
        dot(left.left, right.left) + dot(left.up, right.up);
    return std::acos(std::clamp(
        (relative_trace - 1.0F) * 0.5F, -1.0F, 1.0F)) *
        kRadiansToDegrees;
}

void add_metric(
    WeaponPosePipelineTraceMetric* const metric,
    const float value) noexcept {
    if (metric == nullptr || !std::isfinite(value) || value < 0.0F) {
        return;
    }
    ++metric->count;
    metric->sum += static_cast<double>(value);
    metric->sum_squared +=
        static_cast<double>(value) * static_cast<double>(value);
    metric->minimum = (std::min)(metric->minimum, value);
    metric->maximum = (std::max)(metric->maximum, value);
}

void add_pose_sample(
    const WeaponPosePipelineTracePose& previous,
    const WeaponPosePipelineTracePose& current,
    WeaponPosePipelineTracePoseMetrics* const metrics) noexcept {
    if (metrics == nullptr || !current.valid) {
        return;
    }
    ++metrics->samples;
    if (!previous.valid) {
        return;
    }
    add_metric(
        &metrics->position_step_millimeters,
        vector_distance(previous.position, current.position) *
            kIwToMillimeters);
    add_metric(
        &metrics->orientation_step_degrees,
        basis_angle_degrees(previous.axis, current.axis));
}

void add_pose_error_sample(
    const WeaponPosePipelineTracePose& raw,
    const WeaponPosePipelineTracePose& filtered,
    WeaponPosePipelineTracePoseErrorMetrics* const metrics) noexcept {
    if (metrics == nullptr || !raw.valid || !filtered.valid) {
        return;
    }
    ++metrics->samples;
    add_metric(
        &metrics->position_error_millimeters,
        vector_distance(raw.position, filtered.position) *
            kIwToMillimeters);
    add_metric(
        &metrics->orientation_error_degrees,
        basis_angle_degrees(raw.axis, filtered.axis));
}

void add_direction_sample(
    const WeaponPosePipelineTraceDirection& previous,
    const WeaponPosePipelineTraceDirection& current,
    std::uint32_t* const samples,
    WeaponPosePipelineTraceMetric* const step_metric) noexcept {
    if (samples == nullptr || step_metric == nullptr || !current.valid) {
        return;
    }
    ++*samples;
    if (previous.valid) {
        add_metric(
            step_metric,
            direction_angle_degrees(
                previous.direction, current.direction));
    }
}

[[nodiscard]] bool held_mode(const WeaponGripMode mode) noexcept {
    return mode == WeaponGripMode::RightHand ||
        mode == WeaponGripMode::LeftHand ||
        mode == WeaponGripMode::TwoHand;
}

[[nodiscard]] bool valid_input(
    const WeaponPosePipelineTraceInput& input) noexcept {
    return held_mode(input.grip_mode) && input.weapon_identity != 0 &&
        input.generation != 0 && input.frame_id != 0 &&
        input.publication_milliseconds != 0 &&
        input.render_milliseconds != 0 &&
        input.render_milliseconds >= input.publication_milliseconds &&
        input.predicted_display_time_nanoseconds >= 0 &&
        input.predicted_display_period_nanoseconds >= 0 &&
        valid_pose(input.raw_right) &&
        valid_pose(input.filtered_right) && valid_pose(input.raw_left) &&
        valid_pose(input.filtered_left) &&
        valid_direction(input.pre_final_two_hand_direction) &&
        valid_direction(input.post_final_two_hand_direction) &&
        valid_pose(input.applied_weapon) &&
        valid_direction(input.visible_weapon_direction);
}

void initialize_window(
    const WeaponPosePipelineTraceInput& input,
    WeaponPosePipelineTraceWindow* const window) noexcept {
    if (window == nullptr) {
        return;
    }
    *window = {};
    window->grip_mode = input.grip_mode;
    window->weapon_identity = input.weapon_identity;
}

void set_previous_publication(
    const WeaponPosePipelineTraceInput& input,
    WeaponPosePipelineTraceState* const state) noexcept {
    state->last_generation = input.generation;
    state->last_frame_id = input.frame_id;
    state->last_publication_milliseconds =
        input.publication_milliseconds;
    state->last_predicted_display_time_nanoseconds =
        input.predicted_display_time_nanoseconds;
    state->previous_raw_right = input.raw_right;
    state->previous_filtered_right = input.filtered_right;
    state->previous_raw_left = input.raw_left;
    state->previous_filtered_left = input.filtered_left;
    state->previous_pre_final_two_hand_direction =
        input.pre_final_two_hand_direction;
    state->previous_post_final_two_hand_direction =
        input.post_final_two_hand_direction;
    state->previous_applied_weapon = input.applied_weapon;
    state->previous_visible_weapon_direction =
        input.visible_weapon_direction;
    state->last_render_visible_weapon_direction =
        input.visible_weapon_direction;
}

void add_unique_publication(
    const WeaponPosePipelineTraceInput& input,
    const bool first_in_segment,
    WeaponPosePipelineTraceState* const state) noexcept {
    auto& window = state->window;
    if (window.unique_publications == 0) {
        window.first_generation = input.generation;
        window.first_frame_id = input.frame_id;
    }
    ++window.unique_publications;
    ++window.render_calls;
    window.last_generation = input.generation;
    window.last_frame_id = input.frame_id;

    if (!first_in_segment) {
        add_metric(
            &window.publication_delta_milliseconds,
            static_cast<float>(
                input.publication_milliseconds -
                state->last_publication_milliseconds));
        if (input.predicted_display_time_nanoseconds > 0 &&
            state->last_predicted_display_time_nanoseconds > 0) {
            add_metric(
                &window.predicted_display_step_milliseconds,
                static_cast<float>(
                    static_cast<double>(
                        input.predicted_display_time_nanoseconds -
                        state->last_predicted_display_time_nanoseconds) *
                    kNanosecondsToMilliseconds));
        }
    }
    if (input.predicted_display_period_nanoseconds > 0) {
        add_metric(
            &window.predicted_display_period_milliseconds,
            static_cast<float>(
                static_cast<double>(
                    input.predicted_display_period_nanoseconds) *
                kNanosecondsToMilliseconds));
    }
    add_metric(
        &window.publication_to_render_age_milliseconds,
        static_cast<float>(
            input.render_milliseconds - input.publication_milliseconds));

    add_pose_sample(
        state->previous_raw_right, input.raw_right, &window.raw_right);
    add_pose_sample(
        state->previous_filtered_right, input.filtered_right,
        &window.filtered_right);
    add_pose_error_sample(
        input.raw_right, input.filtered_right,
        &window.right_raw_to_filtered);
    add_pose_sample(
        state->previous_raw_left, input.raw_left, &window.raw_left);
    add_pose_sample(
        state->previous_filtered_left, input.filtered_left,
        &window.filtered_left);
    add_pose_error_sample(
        input.raw_left, input.filtered_left,
        &window.left_raw_to_filtered);

    add_direction_sample(
        state->previous_pre_final_two_hand_direction,
        input.pre_final_two_hand_direction,
        &window.pre_final_two_hand_samples,
        &window.pre_final_two_hand_direction_step_degrees);
    add_direction_sample(
        state->previous_post_final_two_hand_direction,
        input.post_final_two_hand_direction,
        &window.post_final_two_hand_samples,
        &window.post_final_two_hand_direction_step_degrees);
    if (input.pre_final_two_hand_direction.valid &&
        input.post_final_two_hand_direction.valid) {
        ++window.paired_two_hand_samples;
        add_metric(
            &window.pre_to_post_two_hand_error_degrees,
            direction_angle_degrees(
                input.pre_final_two_hand_direction.direction,
                input.post_final_two_hand_direction.direction));
    }

    add_pose_sample(
        state->previous_applied_weapon, input.applied_weapon,
        &window.applied_weapon);
    add_direction_sample(
        state->previous_visible_weapon_direction,
        input.visible_weapon_direction,
        &window.visible_weapon_samples,
        &window.visible_weapon_direction_step_degrees);
}

void seed_segment(
    const WeaponPosePipelineTraceInput& input,
    WeaponPosePipelineTraceState* const state) noexcept {
    *state = {};
    state->valid = true;
    state->grip_mode = input.grip_mode;
    state->weapon_identity = input.weapon_identity;
    state->last_render_milliseconds = input.render_milliseconds;
    initialize_window(input, &state->window);
    add_unique_publication(input, true, state);
    set_previous_publication(input, state);
}

}  // namespace

bool observe_weapon_pose_pipeline_trace(
    const WeaponPosePipelineTraceInput& input,
    WeaponPosePipelineTraceState* const state,
    WeaponPosePipelineTraceReport* const report) noexcept {
    if (state == nullptr || report == nullptr) {
        return false;
    }
    *report = {};

    if (input.transition_pending || !held_mode(input.grip_mode)) {
        *state = {};
        return false;
    }
    if (!valid_input(input)) {
        *state = {};
        return false;
    }

    if (!state->valid || state->grip_mode != input.grip_mode ||
        state->weapon_identity != input.weapon_identity) {
        seed_segment(input, state);
    } else {
        if (input.render_milliseconds < state->last_render_milliseconds) {
            *state = {};
            return false;
        }
        add_metric(
            &state->window.render_delta_milliseconds,
            static_cast<float>(
                input.render_milliseconds - state->last_render_milliseconds));
        state->last_render_milliseconds = input.render_milliseconds;

        if (input.generation == state->last_generation) {
            if (input.frame_id != state->last_frame_id ||
                input.publication_milliseconds !=
                    state->last_publication_milliseconds ||
                input.predicted_display_time_nanoseconds !=
                    state->last_predicted_display_time_nanoseconds) {
                *state = {};
                return false;
            }
            ++state->window.render_calls;
            ++state->window.duplicate_render_calls;
            if (state->last_render_visible_weapon_direction.valid &&
                input.visible_weapon_direction.valid) {
                add_metric(
                    &state->window
                         .same_publication_visible_direction_step_degrees,
                    direction_angle_degrees(
                        state->last_render_visible_weapon_direction.direction,
                        input.visible_weapon_direction.direction));
            }
            state->last_render_visible_weapon_direction =
                input.visible_weapon_direction;
            return true;
        }

        if (input.generation < state->last_generation ||
            input.frame_id <= state->last_frame_id ||
            input.publication_milliseconds <=
                state->last_publication_milliseconds ||
            (input.predicted_display_time_nanoseconds > 0 &&
             state->last_predicted_display_time_nanoseconds > 0 &&
             input.predicted_display_time_nanoseconds <=
                 state->last_predicted_display_time_nanoseconds)) {
            *state = {};
            return false;
        }

        const bool forward_gap =
            input.generation - state->last_generation != 1 ||
            input.frame_id - state->last_frame_id != 1;
        if (forward_gap) {
            seed_segment(input, state);
        } else {
            add_unique_publication(input, false, state);
            set_previous_publication(input, state);
        }
    }

    if (state->window.unique_publications >=
        kWeaponPosePipelineTracePublicationsPerReport) {
        report->valid = true;
        report->window = state->window;
        initialize_window(input, &state->window);
    }
    return true;
}

void reset_weapon_pose_pipeline_trace(
    WeaponPosePipelineTraceState* const state) noexcept {
    if (state != nullptr) {
        *state = {};
    }
}

float weapon_pose_pipeline_trace_metric_mean(
    const WeaponPosePipelineTraceMetric& metric) noexcept {
    return metric.count == 0
        ? 0.0F
        : static_cast<float>(metric.sum / metric.count);
}

float weapon_pose_pipeline_trace_metric_rms(
    const WeaponPosePipelineTraceMetric& metric) noexcept {
    return metric.count == 0
        ? 0.0F
        : static_cast<float>(
              std::sqrt(metric.sum_squared / metric.count));
}

}  // namespace wawvr::mod
