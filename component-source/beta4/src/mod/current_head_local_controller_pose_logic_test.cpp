// SPDX-License-Identifier: GPL-3.0-only
#include "current_head_local_controller_pose_logic.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void check(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

[[nodiscard]] bool near(
    const float left,
    const float right,
    const float tolerance = 0.001F) {
    return std::abs(left - right) <= tolerance;
}

[[nodiscard]] bool near(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right,
    const float tolerance = 0.001F) {
    return near(left.x, right.x, tolerance) &&
        near(left.y, right.y, tolerance) &&
        near(left.z, right.z, tolerance);
}

[[nodiscard]] bool near(
    const wawvr::xr::Basis3f& left,
    const wawvr::xr::Basis3f& right,
    const float tolerance = 0.001F) {
    return near(left.forward, right.forward, tolerance) &&
        near(left.left, right.left, tolerance) &&
        near(left.up, right.up, tolerance);
}

[[nodiscard]] bool near(
    const wawvr::xr::EnginePose& left,
    const wawvr::xr::EnginePose& right,
    const float tolerance = 0.001F) {
    return near(left.position, right.position, tolerance) &&
        near(left.axis, right.axis, tolerance);
}

[[nodiscard]] wawvr::xr::Quaternionf axis_angle(
    wawvr::xr::Vec3f axis,
    const float degrees) {
    const float length = std::sqrt(
        axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    axis.x /= length;
    axis.y /= length;
    axis.z /= length;
    constexpr float kDegreesToRadians =
        3.14159265358979323846F / 180.0F;
    const float half_angle = degrees * kDegreesToRadians * 0.5F;
    const float sine = std::sin(half_angle);
    return wawvr::xr::Normalize({
        axis.x * sine,
        axis.y * sine,
        axis.z * sine,
        std::cos(half_angle),
    });
}

[[nodiscard]] wawvr::xr::Posef compose(
    const wawvr::xr::Posef& parent,
    const wawvr::xr::Posef& local) {
    const auto rotated = wawvr::xr::Rotate(
        parent.orientation, local.position);
    return {
        wawvr::xr::Normalize(wawvr::xr::Multiply(
            parent.orientation, local.orientation)),
        {
            parent.position.x + rotated.x,
            parent.position.y + rotated.y,
            parent.position.z + rotated.z,
        },
    };
}

[[nodiscard]] wawvr::xr::HandActionState tracked_hand(
    const wawvr::xr::Posef& head,
    const wawvr::xr::Vec3f& local_grip_position,
    const wawvr::xr::Quaternionf& local_aim_orientation) {
    wawvr::xr::Posef local_grip{};
    local_grip.position = local_grip_position;
    wawvr::xr::Posef local_aim{};
    local_aim.orientation = local_aim_orientation;
    const auto world_grip = compose(head, local_grip);
    const auto world_aim = compose(head, local_aim);

    wawvr::xr::HandActionState hand{};
    hand.grip.active = true;
    hand.grip.position_valid = true;
    hand.grip.position_tracked = true;
    hand.grip.pose.position = world_grip.position;
    hand.aim.active = true;
    hand.aim.orientation_valid = true;
    hand.aim.orientation_tracked = true;
    hand.aim.pose.orientation = world_aim.orientation;
    return hand;
}

[[nodiscard]] wawvr::xr::Posef head_fixture() {
    wawvr::xr::Posef head{};
    head.position = {1.15F, 1.72F, -0.45F};
    head.orientation = wawvr::xr::Normalize(wawvr::xr::Multiply(
        axis_angle({0.0F, 1.0F, 0.0F}, 37.0F),
        wawvr::xr::Multiply(
            axis_angle({1.0F, 0.0F, 0.0F}, -19.0F),
            axis_angle({0.0F, 0.0F, 1.0F}, 13.0F))));
    return head;
}

[[nodiscard]] wawvr::xr::HandActionState hand_fixture(
    const wawvr::xr::Posef& head) {
    return tracked_hand(
        head, {0.24F, -0.31F, -0.58F},
        wawvr::xr::Normalize(wawvr::xr::Multiply(
            axis_angle({0.0F, 1.0F, 0.0F}, -11.0F),
            axis_angle({0.0F, 0.0F, 1.0F}, 23.0F))));
}

[[nodiscard]] bool exact(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) {
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

[[nodiscard]] bool exact(
    const wawvr::xr::Quaternionf& left,
    const wawvr::xr::Quaternionf& right) {
    return left.x == right.x && left.y == right.y && left.z == right.z &&
        left.w == right.w;
}

[[nodiscard]] bool exact(
    const wawvr::xr::Basis3f& left,
    const wawvr::xr::Basis3f& right) {
    return exact(left.forward, right.forward) &&
        exact(left.left, right.left) && exact(left.up, right.up);
}

[[nodiscard]] bool exact(
    const wawvr::xr::EnginePose& left,
    const wawvr::xr::EnginePose& right) {
    return exact(left.position, right.position) && exact(left.axis, right.axis);
}

[[nodiscard]] bool exact(
    const wawvr::mod::CurrentHeadLocalControllerPoseFilterConfig& left,
    const wawvr::mod::CurrentHeadLocalControllerPoseFilterConfig& right) {
    return left.position_response == right.position_response &&
        left.orientation_response == right.orientation_response &&
        left.maximum_position_discontinuity_meters ==
            right.maximum_position_discontinuity_meters &&
        left.maximum_orientation_discontinuity_degrees ==
            right.maximum_orientation_discontinuity_degrees &&
        left.maximum_frame_age_milliseconds ==
            right.maximum_frame_age_milliseconds;
}

[[nodiscard]] bool exact(
    const wawvr::mod::CurrentHeadLocalControllerPoseFilterState& left,
    const wawvr::mod::CurrentHeadLocalControllerPoseFilterState& right) {
    return left.valid == right.valid &&
        left.generation == right.generation &&
        left.publication_milliseconds == right.publication_milliseconds &&
        left.engine_units_per_meter == right.engine_units_per_meter &&
        exact(left.config, right.config) &&
        exact(left.filtered_grip_position, right.filtered_grip_position) &&
        exact(
            left.filtered_aim_orientation,
            right.filtered_aim_orientation) &&
        exact(left.cached_pose, right.cached_pose);
}

void test_raw_pose_is_invariant_under_common_six_dof_motion() {
    const auto head = head_fixture();
    const auto hand = hand_fixture(head);
    wawvr::xr::EnginePose baseline{};
    check(wawvr::mod::current_head_local_controller_pose(
              head, hand, &baseline),
          "baseline current-head-local pose is valid");

    wawvr::xr::Posef common_transform{};
    common_transform.position = {-2.30F, 0.75F, 1.10F};
    common_transform.orientation = wawvr::xr::Normalize(
        wawvr::xr::Multiply(
            axis_angle({0.3F, 0.8F, -0.5F}, 71.0F),
            axis_angle({-0.7F, 0.1F, 0.6F}, -29.0F)));
    const auto moved_head = compose(common_transform, head);
    auto moved_hand = hand;
    moved_hand.grip.pose = compose(common_transform, hand.grip.pose);
    moved_hand.aim.pose = compose(common_transform, hand.aim.pose);

    wawvr::xr::EnginePose moved{};
    check(wawvr::mod::current_head_local_controller_pose(
              moved_head, moved_hand, &moved),
          "common six-DoF transformed pose is valid");
    check(near(moved, baseline, 0.002F),
          "common head/controller rigid motion changed current-head-local pose");
}

void test_raw_pose_preserves_exact_controller_only_delta() {
    const auto head = head_fixture();
    const auto hand = hand_fixture(head);
    wawvr::xr::EnginePose baseline{};
    check(wawvr::mod::current_head_local_controller_pose(
              head, hand, &baseline),
          "controller-only baseline is valid");

    const wawvr::xr::Vec3f local_delta{0.08F, -0.03F, 0.05F};
    const auto world_delta = wawvr::xr::Rotate(
        head.orientation, local_delta);
    auto moved_hand = hand;
    moved_hand.grip.pose.position.x += world_delta.x;
    moved_hand.grip.pose.position.y += world_delta.y;
    moved_hand.grip.pose.position.z += world_delta.z;
    const auto relative_aim = wawvr::xr::Normalize(wawvr::xr::Multiply(
        wawvr::xr::Conjugate(head.orientation),
        hand.aim.pose.orientation));
    const auto aim_delta = axis_angle({0.4F, -0.2F, 0.9F}, 31.0F);
    moved_hand.aim.pose.orientation = wawvr::xr::Normalize(
        wawvr::xr::Multiply(
            head.orientation,
            wawvr::xr::Multiply(aim_delta, relative_aim)));

    wawvr::xr::EnginePose moved{};
    check(wawvr::mod::current_head_local_controller_pose(
              head, moved_hand, &moved),
          "controller-only moved pose is valid");
    const auto expected_iw_delta = wawvr::xr::OpenXrVectorToIw(local_delta);
    const wawvr::xr::Vec3f expected_position{
        baseline.position.x +
            expected_iw_delta.x * wawvr::xr::kIwUnitsPerMeter,
        baseline.position.y +
            expected_iw_delta.y * wawvr::xr::kIwUnitsPerMeter,
        baseline.position.z +
            expected_iw_delta.z * wawvr::xr::kIwUnitsPerMeter,
    };
    wawvr::xr::Posef expected_aim{};
    expected_aim.orientation = wawvr::xr::Multiply(aim_delta, relative_aim);
    const wawvr::xr::Posef identity{};
    const auto expected_axis = wawvr::xr::OpenXrPoseToIwRelative(
        expected_aim, identity, 1.0F).axis;
    check(near(moved.position, expected_position, 0.002F) &&
              near(moved.axis, expected_axis, 0.002F),
          "controller-only raw delta was not preserved exactly");
}

void test_filter_is_invariant_under_common_six_dof_motion() {
    const auto head = head_fixture();
    const auto hand = hand_fixture(head);
    wawvr::mod::CurrentHeadLocalControllerPoseFilterState state{};
    wawvr::xr::EnginePose baseline{};
    check(wawvr::mod::filtered_current_head_local_controller_pose(
              head, hand, 1, 1'000, 1'001, &state, &baseline),
          "head-local filter baseline is accepted");

    wawvr::xr::Posef common_transform{};
    common_transform.position = {0.42F, -0.19F, 0.73F};
    common_transform.orientation = axis_angle(
        {-0.4F, 0.7F, 0.3F}, 54.0F);
    const auto moved_head = compose(common_transform, head);
    auto moved_hand = hand;
    moved_hand.grip.pose = compose(common_transform, hand.grip.pose);
    moved_hand.aim.pose = compose(common_transform, hand.aim.pose);
    wawvr::xr::EnginePose filtered{};
    check(wawvr::mod::filtered_current_head_local_controller_pose(
              moved_head, moved_hand, 2, 1'016, 1'017,
              &state, &filtered),
          "common-motion generation advances the local filter");
    check(near(filtered, baseline, 0.002F),
          "head-local EMA turned common motion into inverse weapon lag");
}

void test_filter_uses_configured_controller_responses() {
    check(near(
              wawvr::mod::kCurrentHeadLocalPositionResponse, 0.45F) &&
              near(
                  wawvr::mod::kCurrentHeadLocalOrientationResponse,
                  0.55F),
          "head-local filter defaults match the established responses");

    const wawvr::xr::Posef head{};
    auto hand = tracked_hand(
        head, {0.0F, 0.0F, -0.40F}, {});
    wawvr::mod::CurrentHeadLocalControllerPoseFilterState state{};
    wawvr::xr::EnginePose baseline{};
    check(wawvr::mod::filtered_current_head_local_controller_pose(
              head, hand, 10, 2'000, 2'001, &state, &baseline),
          "configured-response baseline is valid");

    hand.grip.pose.position.x += 0.20F;
    const auto target_orientation = axis_angle(
        {0.0F, 1.0F, 0.0F}, 60.0F);
    hand.aim.pose.orientation = target_orientation;
    wawvr::xr::EnginePose filtered{};
    check(wawvr::mod::filtered_current_head_local_controller_pose(
              head, hand, 11, 2'016, 2'017, &state, &filtered),
          "configured-response controller step is accepted");

    const auto raw_target = wawvr::xr::OpenXrPoseToIwRelative(
        {{}, hand.grip.pose.position}, head,
        wawvr::xr::kIwUnitsPerMeter).position;
    const wawvr::xr::Vec3f expected_position{
        baseline.position.x +
            (raw_target.x - baseline.position.x) * 0.45F,
        baseline.position.y +
            (raw_target.y - baseline.position.y) * 0.45F,
        baseline.position.z +
            (raw_target.z - baseline.position.z) * 0.45F,
    };
    wawvr::xr::Quaternionf expected_orientation{
        target_orientation.x * 0.55F,
        target_orientation.y * 0.55F,
        target_orientation.z * 0.55F,
        1.0F + (target_orientation.w - 1.0F) * 0.55F,
    };
    expected_orientation = wawvr::xr::Normalize(expected_orientation);
    wawvr::xr::Posef expected_pose{};
    expected_pose.orientation = expected_orientation;
    const wawvr::xr::Posef identity{};
    const auto expected_axis = wawvr::xr::OpenXrPoseToIwRelative(
        expected_pose, identity, 1.0F).axis;
    check(near(filtered.position, expected_position, 0.002F) &&
              near(filtered.axis, expected_axis, 0.002F),
          "filtered controller response did not use 0.45/0.55");
}

void test_pose_contract_has_no_tracking_anchor_dependency() {
    const auto head = head_fixture();
    const auto hand = hand_fixture(head);
    // Deliberately different would-be anchors exist only in the caller. The
    // production API has no anchor parameter, so neither can affect output.
    wawvr::xr::Posef anchor_a{};
    wawvr::xr::Posef anchor_b{};
    anchor_b.position = {99.0F, -42.0F, 17.0F};
    anchor_b.orientation = axis_angle({0.2F, 0.9F, -0.1F}, 123.0F);
    check(!near(anchor_a.position, anchor_b.position),
          "anchor-independence fixture uses distinct anchors");

    wawvr::xr::EnginePose first{};
    wawvr::xr::EnginePose second{};
    check(wawvr::mod::current_head_local_controller_pose(
              head, hand, &first) &&
              wawvr::mod::current_head_local_controller_pose(
                  head, hand, &second),
          "anchor-free pose calls are valid");
    check(exact(first, second),
          "an external tracking anchor affected the anchor-free contract");
}

void test_repeated_generation_returns_identical_cached_pose() {
    const auto head = head_fixture();
    auto hand = hand_fixture(head);
    wawvr::mod::CurrentHeadLocalControllerPoseFilterState state{};
    wawvr::xr::EnginePose first{};
    check(wawvr::mod::filtered_current_head_local_controller_pose(
              head, hand, 50, 5'000, 5'001, &state, &first),
          "cached-generation baseline is accepted");
    const auto established_state = state;

    hand.grip.pose.position.x += 0.15F;
    hand.aim.pose.orientation = wawvr::xr::Normalize(
        wawvr::xr::Multiply(
            hand.aim.pose.orientation,
            axis_angle({0.0F, 0.0F, 1.0F}, 45.0F)));
    wawvr::xr::EnginePose repeated{
        {91.0F, 92.0F, 93.0F},
        wawvr::xr::Basis3f{},
    };
    check(wawvr::mod::filtered_current_head_local_controller_pose(
              head, hand, 50, 5'000, 5'015, &state, &repeated),
          "exact repeated generation returns its cached result");
    check(exact(repeated, first) && exact(state, established_state),
          "repeated consumer changed pose within one generation");
}

void test_invalid_stale_and_regressed_inputs_preserve_state() {
    const auto head = head_fixture();
    const auto hand = hand_fixture(head);
    wawvr::mod::CurrentHeadLocalControllerPoseFilterState state{};
    wawvr::xr::EnginePose accepted{};
    check(wawvr::mod::filtered_current_head_local_controller_pose(
              head, hand, 100, 10'000, 10'001, &state, &accepted),
          "failure-preservation baseline is accepted");
    const auto established_state = state;
    const wawvr::xr::EnginePose sentinel{
        {7.0F, 8.0F, 9.0F},
        {
            {0.0F, 1.0F, 0.0F},
            {-1.0F, 0.0F, 0.0F},
            {0.0F, 0.0F, 1.0F},
        },
    };

    const auto rejects_without_mutation =
        [&](const wawvr::xr::Posef& candidate_head,
            const wawvr::xr::HandActionState& candidate_hand,
            const std::uint64_t generation,
            const std::uint64_t publication,
            const std::uint64_t now,
            const char* const message) {
            wawvr::xr::EnginePose output = sentinel;
            check(!wawvr::mod::filtered_current_head_local_controller_pose(
                      candidate_head, candidate_hand, generation,
                      publication, now, &state, &output),
                  message);
            check(exact(state, established_state) && exact(output, sentinel),
                  "rejected sample mutated filter state or caller output");
        };

    auto invalid_hand = hand;
    invalid_hand.aim.pose.orientation = {0.0F, 0.0F, 0.0F, 0.0F};
    rejects_without_mutation(
        head, invalid_hand, 101, 10'016, 10'017,
        "invalid orientation is rejected");
    rejects_without_mutation(
        head, hand, 101, 10'016,
        10'016 +
            wawvr::mod::kCurrentHeadLocalMaximumFrameAgeMilliseconds + 1,
        "stale publication is rejected");
    rejects_without_mutation(
        head, hand, 99, 10'016, 10'017,
        "regressed generation is rejected");
    rejects_without_mutation(
        head, hand, 101, 9'999, 10'000,
        "regressed publication timestamp is rejected");

    auto discontinuous_hand = hand;
    discontinuous_hand.grip.pose.position.x += 1.0F;
    rejects_without_mutation(
        head, discontinuous_hand, 101, 10'016, 10'017,
        "implausible local position discontinuity is rejected");
    rejects_without_mutation(
        head, hand, 101,
        10'000 +
            wawvr::mod::kCurrentHeadLocalMaximumFrameAgeMilliseconds + 1,
        10'000 +
            wawvr::mod::kCurrentHeadLocalMaximumFrameAgeMilliseconds + 2,
        "stale filter-history gap is rejected");

    auto invalid_head = head;
    invalid_head.position.x = std::numeric_limits<float>::infinity();
    rejects_without_mutation(
        invalid_head, hand, 101, 10'016, 10'017,
        "non-finite frame head is rejected");
}

}  // namespace

int main() {
    test_raw_pose_is_invariant_under_common_six_dof_motion();
    test_raw_pose_preserves_exact_controller_only_delta();
    test_filter_is_invariant_under_common_six_dof_motion();
    test_filter_uses_configured_controller_responses();
    test_pose_contract_has_no_tracking_anchor_dependency();
    test_repeated_generation_returns_identical_cached_pose();
    test_invalid_stale_and_regressed_inputs_preserve_state();
    return 0;
}
