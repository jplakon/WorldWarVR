// SPDX-License-Identifier: GPL-3.0-only
#include "post_t4_aim_phase_logic.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

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

[[nodiscard]] wawvr::xr::Quaternionf openxr_yaw(
    const float degrees) noexcept {
    const float half_radians = degrees * kPi / 360.0F;
    return {0.0F, std::sin(half_radians), 0.0F,
            std::cos(half_radians)};
}

[[nodiscard]] wawvr::xr::Vec3f iw_yaw_direction(
    const float degrees) noexcept {
    const float radians = degrees * kPi / 180.0F;
    return {std::cos(radians), std::sin(radians), 0.0F};
}

[[nodiscard]] wawvr::xr::Basis3f iw_yaw_axis(
    const float degrees) noexcept {
    const wawvr::xr::Vec3f forward = iw_yaw_direction(degrees);
    return {forward, {-forward.y, forward.x, 0.0F}, {0.0F, 0.0F, 1.0F}};
}

[[nodiscard]] wawvr::mod::PostT4AimPhaseInput input(
    const std::uint64_t generation,
    const std::uint64_t frame_id,
    const std::uint64_t publication_milliseconds,
    const float production_controller_yaw_degrees,
    const float attachment_yaw_degrees,
    const float visible_yaw_degrees) noexcept {
    wawvr::mod::PostT4AimPhaseInput result{};
    result.generation = generation;
    result.frame_id = frame_id;
    result.publication_milliseconds = publication_milliseconds;
    result.head_pose.orientation.w = 1.0F;
    result.tracking_anchor.orientation.w = 1.0F;
    result.body_axis = {};
    result.production_controller_world_axis =
        iw_yaw_axis(production_controller_yaw_degrees);
    result.weapon_attachment_axis = iw_yaw_axis(attachment_yaw_degrees);
    result.visible_world_direction = iw_yaw_direction(visible_yaw_degrees);
    return result;
}

void test_head_local_coordinate_builder() {
    wawvr::xr::Posef identity{};
    identity.orientation.w = 1.0F;
    wawvr::xr::Basis3f body{};
    wawvr::xr::Vec3f local{};
    expect(
        wawvr::mod::post_t4_world_direction_to_head_local(
            body, identity, identity, {1.0F, 0.0F, 0.0F}, &local) &&
            near(local.x, 1.0F) && near(local.y, 0.0F) &&
            near(local.z, 0.0F),
        "identity head maps world forward into head-local forward");

    body.forward = {0.0F, 1.0F, 0.0F};
    body.left = {-1.0F, 0.0F, 0.0F};
    body.up = {0.0F, 0.0F, 1.0F};
    expect(
        wawvr::mod::post_t4_world_direction_to_head_local(
            body, identity, identity, body.forward, &local) &&
            near(local.x, 1.0F) && near(local.y, 0.0F),
        "body yaw is composed before the world-to-head transpose");

    body = {};
    wawvr::xr::Posef yawed_head = identity;
    yawed_head.orientation = openxr_yaw(90.0F);
    expect(
        wawvr::mod::post_t4_world_direction_to_head_local(
            body, yawed_head, identity, {0.0F, 1.0F, 0.0F}, &local) &&
            near(local.x, 1.0F) && near(local.y, 0.0F),
        "OpenXR head yaw maps its world forward into head-local forward");

    wawvr::xr::Posef yawed_anchor = yawed_head;
    expect(
        wawvr::mod::post_t4_world_direction_to_head_local(
            body, yawed_head, yawed_anchor, {1.0F, 0.0F, 0.0F}, &local) &&
            near(local.x, 1.0F) && near(local.y, 0.0F),
        "equal head and tracking-anchor yaw cancel exactly");
}

void test_current_publication_wins_without_lag() {
    wawvr::mod::PostT4AimPhaseState state{};
    wawvr::mod::PostT4AimPhaseObservation observation{};
    expect(
        wawvr::mod::update_post_t4_aim_phase(
            input(1, 101, 1'000, 0.0F, 30.0F, 30.0F),
            &state, &observation) && !observation.compared,
        "first phase publication seeds without comparing");
    expect(
        wawvr::mod::update_post_t4_aim_phase(
            input(2, 102, 1'014, 10.0F, 30.0F, 40.0F),
            &state, &observation) && observation.compared &&
            near(observation.lag0_error_degrees, 0.0F, 0.01F) &&
            near(observation.lag1_error_degrees, 10.0F, 0.01F) &&
            near(observation.publication_delta_milliseconds, 14.0F) &&
            !observation.lag1_better,
        "no-lag applied direction matches current raw publication");
}

void test_lagged_stream_is_not_absorbed_at_segment_start() {
    wawvr::mod::PostT4AimPhaseState state{};
    wawvr::mod::PostT4AimPhaseObservation observation{};
    expect(
        wawvr::mod::update_post_t4_aim_phase(
            // The observed weapon is already one publication behind when this
            // diagnostic segment begins: controller 10 + attachment 30 would
            // predict 40, while post-T4 still shows the preceding 30 degrees.
            input(8, 208, 2'000, 10.0F, 30.0F, 30.0F),
            &state, &observation),
        "already-lagged sequence seeds without deriving calibration");
    expect(
        wawvr::mod::update_post_t4_aim_phase(
            input(9, 209, 2'014, 20.0F, 30.0F, 40.0F),
            &state, &observation) && observation.compared &&
            near(observation.lag0_error_degrees, 10.0F, 0.01F) &&
            near(observation.lag1_error_degrees, 0.0F, 0.01F) &&
            observation.lag1_better,
        "one-publication-late visible direction matches prior prediction");
}

void test_previous_world_direction_uses_current_head_basis() {
    wawvr::mod::PostT4AimPhaseState state{};
    wawvr::mod::PostT4AimPhaseObservation observation{};
    auto first = input(
        20, 320, 3'000, 10.0F, 30.0F, 30.0F);
    expect(
        wawvr::mod::update_post_t4_aim_phase(
            first, &state, &observation),
        "independent-head-motion lag sequence seeds");
    auto second = input(
        21, 321, 3'014, 20.0F, 30.0F, 40.0F);
    second.body_axis = iw_yaw_axis(8.0F);
    second.head_pose.orientation = openxr_yaw(12.0F);
    expect(
        wawvr::mod::update_post_t4_aim_phase(
            second, &state, &observation) && observation.compared &&
            near(observation.lag0_error_degrees, 10.0F, 0.01F) &&
            near(observation.lag1_error_degrees, 0.0F, 0.01F),
        "prior world prediction is re-expressed through current body and HMD");
}

void test_duplicate_and_discontinuity_semantics() {
    wawvr::mod::PostT4AimPhaseState state{};
    wawvr::mod::PostT4AimPhaseObservation observation{};
    const auto first = input(
        40, 440, 4'000, 0.0F, 0.0F, 0.0F);
    expect(
        wawvr::mod::update_post_t4_aim_phase(
            first, &state, &observation),
        "discontinuity sequence seeds");
    expect(
        wawvr::mod::update_post_t4_aim_phase(
            first, &state, &observation) && !observation.compared &&
            state.generation == 40,
        "duplicate publication is ignored without advancing history");

    expect(
        wawvr::mod::update_post_t4_aim_phase(
            input(42, 442, 4'028, 20.0F, 0.0F, 20.0F),
            &state, &observation) && !observation.compared &&
            state.generation == 42,
        "skipped generation reseeds without a lag comparison");
    expect(
        wawvr::mod::update_post_t4_aim_phase(
            input(43, 443, 4'042, 22.0F, 0.0F, 22.0F),
            &state, &observation) && observation.compared,
        "comparison resumes after a discontinuity reseed");

    expect(
        wawvr::mod::update_post_t4_aim_phase(
            input(44, 444, 4'400, 24.0F, 0.0F, 24.0F),
            &state, &observation) && !observation.compared,
        "publication gap over the bound reseeds");
    expect(
        wawvr::mod::update_post_t4_aim_phase(
            input(45, 443, 4'414, 26.0F, 0.0F, 26.0F),
            &state, &observation) && !observation.compared,
        "non-increasing OpenXR frame id reseeds");
    expect(
        wawvr::mod::update_post_t4_aim_phase(
            input(46, 446, 4'300, 28.0F, 0.0F, 28.0F),
            &state, &observation) && !observation.compared,
        "publication timestamp regression reseeds");
}

void test_anchor_rebase_and_invalid_sample_reset() {
    wawvr::mod::PostT4AimPhaseState state{};
    wawvr::mod::PostT4AimPhaseObservation observation{};
    expect(
        wawvr::mod::update_post_t4_aim_phase(
            input(60, 560, 5'000, 0.0F, 0.0F, 0.0F),
            &state, &observation),
        "anchor sequence seeds");
    auto rebased = input(
        61, 561, 5'014, 0.0F, 0.0F, 0.0F);
    rebased.tracking_anchor.orientation = openxr_yaw(5.0F);
    rebased.head_pose.orientation = rebased.tracking_anchor.orientation;
    expect(
        wawvr::mod::update_post_t4_aim_phase(
            rebased, &state, &observation) && !observation.compared &&
            state.generation == 61,
        "tracking-anchor rebase reseeds phase history");

    auto invalid = input(62, 562, 5'028, 0.0F, 0.0F, 0.0F);
    invalid.production_controller_world_axis.forward = {};
    invalid.production_controller_world_axis.left = {};
    const bool invalid_result = wawvr::mod::update_post_t4_aim_phase(
        invalid, &state, &observation);
    expect(
        !invalid_result && !state.valid,
        "invalid direction fails closed and clears phase history");
}

}  // namespace

int main() {
    test_head_local_coordinate_builder();
    test_current_publication_wins_without_lag();
    test_lagged_stream_is_not_absorbed_at_segment_start();
    test_previous_world_direction_uses_current_head_basis();
    test_duplicate_and_discontinuity_semantics();
    test_anchor_rebase_and_invalid_sample_reset();
    std::cout << "post-T4 aim phase logic tests passed\n";
    return EXIT_SUCCESS;
}
