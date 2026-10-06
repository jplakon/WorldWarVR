// SPDX-License-Identifier: GPL-3.0-only

#include "input_mapping.hpp"
#include "weapon_placement.hpp"

#include "xr_math.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace wawvr;

const xr::Quaternionf kIdentityTrackingAnchorOrientation{};

void check(const bool condition, const std::string_view message) {
    if (!condition) {
        std::fprintf(stderr, "weapon placement check failed: %.*s\n",
                     static_cast<int>(message.size()), message.data());
        throw std::runtime_error(std::string(message));
    }
}

[[nodiscard]] bool near(
    const float left,
    const float right,
    const float tolerance = 0.001F) noexcept {
    return std::abs(left - right) <= tolerance;
}

void check_vector(
    const xr::Vec3f& actual,
    const xr::Vec3f& expected,
    const std::string_view message) {
    check(
        near(actual.x, expected.x) && near(actual.y, expected.y) &&
            near(actual.z, expected.z),
        message);
}

void check_vector_exact(
    const xr::Vec3f& actual,
    const xr::Vec3f& expected,
    const std::string_view message) {
    check(actual.x == expected.x && actual.y == expected.y &&
              actual.z == expected.z,
          message);
}

void check_basis(
    const xr::Basis3f& actual,
    const xr::Basis3f& expected,
    const std::string_view message) {
    check_vector(actual.forward, expected.forward, message);
    check_vector(actual.left, expected.left, message);
    check_vector(actual.up, expected.up, message);
}

void check_basis_exact(
    const xr::Basis3f& actual,
    const xr::Basis3f& expected,
    const std::string_view message) {
    check_vector_exact(actual.forward, expected.forward, message);
    check_vector_exact(actual.left, expected.left, message);
    check_vector_exact(actual.up, expected.up, message);
}

void check_quaternion_exact(
    const xr::Quaternionf& actual,
    const xr::Quaternionf& expected,
    const std::string_view message) {
    check(actual.x == expected.x && actual.y == expected.y &&
              actual.z == expected.z && actual.w == expected.w,
          message);
}

[[nodiscard]] xr::Basis3f yaw_basis(const float degrees) noexcept {
    constexpr float kDegreesToRadians = 0.0174532925199F;
    const float radians = degrees * kDegreesToRadians;
    const float sine = std::sin(radians);
    const float cosine = std::cos(radians);
    xr::Basis3f result{};
    result.forward = {cosine, sine, 0.0F};
    result.left = {-sine, cosine, 0.0F};
    result.up = {0.0F, 0.0F, 1.0F};
    return result;
}

[[nodiscard]] xr::Basis3f yaw_roll_basis(
    const float yaw_degrees,
    const float roll_degrees) noexcept {
    constexpr float kDegreesToRadians = 0.0174532925199F;
    const xr::Basis3f yaw = yaw_basis(yaw_degrees);
    const float roll = roll_degrees * kDegreesToRadians;
    const float sine = std::sin(roll);
    const float cosine = std::cos(roll);
    xr::Basis3f result{};
    result.forward = yaw.forward;
    result.left = {
        yaw.left.x * cosine + yaw.up.x * sine,
        yaw.left.y * cosine + yaw.up.y * sine,
        yaw.left.z * cosine + yaw.up.z * sine,
    };
    result.up = {
        yaw.up.x * cosine - yaw.left.x * sine,
        yaw.up.y * cosine - yaw.left.y * sine,
        yaw.up.z * cosine - yaw.left.z * sine,
    };
    return result;
}

[[nodiscard]] xr::Quaternionf yaw_orientation(
    const float degrees) noexcept {
    constexpr float kDegreesToHalfRadians = 0.00872664625997F;
    const float half_angle = degrees * kDegreesToHalfRadians;
    xr::Quaternionf result{};
    result.z = std::sin(half_angle);
    result.w = std::cos(half_angle);
    return result;
}

[[nodiscard]] xr::Vec3f compose_direction(
    const xr::Basis3f& parent,
    const xr::Vec3f& local) noexcept {
    return {
        local.x * parent.forward.x + local.y * parent.left.x +
            local.z * parent.up.x,
        local.x * parent.forward.y + local.y * parent.left.y +
            local.z * parent.up.y,
        local.x * parent.forward.z + local.y * parent.left.z +
            local.z * parent.up.z,
    };
}

[[nodiscard]] xr::Basis3f compose_basis(
    const xr::Basis3f& parent,
    const xr::Basis3f& local) noexcept {
    return {
        compose_direction(parent, local.forward),
        compose_direction(parent, local.left),
        compose_direction(parent, local.up),
    };
}

[[nodiscard]] xr::Posef compose_pose(
    const xr::Posef& parent,
    const xr::Posef& local) noexcept {
    const xr::Vec3f rotated = xr::Rotate(parent.orientation, local.position);
    return {
        xr::Normalize(xr::Multiply(parent.orientation, local.orientation)),
        {
            parent.position.x + rotated.x,
            parent.position.y + rotated.y,
            parent.position.z + rotated.z,
        },
    };
}

[[nodiscard]] float basis_yaw_degrees(
    const xr::Basis3f& axis) noexcept {
    constexpr float kRadiansToDegrees = 57.2957795131F;
    return std::atan2(axis.forward.y, axis.forward.x) * kRadiansToDegrees;
}

[[nodiscard]] mod::ControllerFrameSnapshot tracked_snapshot() noexcept {
    mod::ControllerFrameSnapshot snapshot{};
    snapshot.frame.frame_id = 10;
    snapshot.frame.views_valid = true;
    snapshot.frame.head_center.orientation.w = 1.0F;
    snapshot.tracking_anchor.orientation.w = 1.0F;
    snapshot.frame.actions.focused = true;
    snapshot.frame.actions.sequence = 11;
    snapshot.publication_milliseconds = 1'000;
    snapshot.generation = 12;

    auto& right = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    right.grip.active = true;
    right.grip.orientation_valid = true;
    right.grip.position_valid = true;
    right.grip.orientation_tracked = true;
    right.grip.position_tracked = true;
    right.grip.pose.orientation.w = 1.0F;
    right.grip.pose.position = {0.25F, -0.10F, -0.50F};
    right.aim.active = true;
    right.aim.orientation_valid = true;
    right.aim.orientation_tracked = true;
    right.aim.pose.orientation.w = 1.0F;
    auto& left = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Left)];
    left.grip.active = true;
    left.grip.orientation_valid = true;
    left.grip.position_valid = true;
    left.grip.orientation_tracked = true;
    left.grip.position_tracked = true;
    left.grip.pose.orientation.w = 1.0F;
    left.grip.pose.position = {-0.20F, 0.15F, -0.40F};
    left.aim.active = true;
    left.aim.orientation_valid = true;
    left.aim.orientation_tracked = true;
    left.aim.pose.orientation.w = 1.0F;
    return snapshot;
}

void test_pose_uses_grip_position_and_aim_orientation() {
    const auto snapshot = tracked_snapshot();
    mod::RightControllerWeaponPose pose{};
    check(
        mod::right_controller_weapon_pose(snapshot, 1'001, &pose),
        "valid right-controller weapon pose");
    check_vector(
        pose.grip_position,
        {0.50F * xr::kIwUnitsPerMeter,
         -0.25F * xr::kIwUnitsPerMeter,
         -0.10F * xr::kIwUnitsPerMeter},
        "OpenXR grip converts to IW head-local position");
    check_vector(pose.aim_axis.forward, {1.0F, 0.0F, 0.0F},
                 "identity aim points IW forward");

    auto invalid = snapshot;
    invalid.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)]
        .grip.position_valid = false;
    check(!mod::right_controller_weapon_pose(invalid, 1'001, &pose),
          "invalid grip position fails closed");
}

void test_valid_but_inferred_pose_remains_usable() {
    const auto snapshot = tracked_snapshot();
    mod::ControllerWeaponPose pose{};

    auto position_inferred = snapshot;
    auto& position_inferred_right = position_inferred.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    position_inferred_right.grip.position_tracked = false;
    position_inferred_right.grip.pose.position.x += 0.02F;
    check(position_inferred_right.grip.position_valid &&
              mod::controller_weapon_pose(
                  position_inferred, xr::Hand::Right, 1'001, &pose),
          "valid inferred grip position remains usable like COD4");

    auto orientation_inferred = snapshot;
    auto& orientation_inferred_right =
        orientation_inferred.frame.actions.hands[
            static_cast<std::uint32_t>(xr::Hand::Right)];
    orientation_inferred_right.aim.orientation_tracked = false;
    check(orientation_inferred_right.aim.orientation_valid &&
              mod::controller_weapon_pose(
                  orientation_inferred, xr::Hand::Right, 1'001, &pose),
          "valid inferred aim orientation remains usable like COD4");

    mod::ControllerWeaponPoseFilterState state{};
    check(mod::stabilized_controller_weapon_pose(
              snapshot, xr::Hand::Right, 1'001, &state, &pose),
          "tracked baseline initializes the stabilized pose");
    position_inferred.generation += 1;
    position_inferred.frame.frame_id += 1;
    position_inferred.frame.actions.sequence += 1;
    position_inferred.publication_milliseconds += 10;
    check(mod::stabilized_controller_weapon_pose(
              position_inferred, xr::Hand::Right, 1'011, &state, &pose) &&
              state.valid && state.generation == position_inferred.generation,
          "inferred valid position advances the stabilized pose");

    orientation_inferred.generation += 2;
    orientation_inferred.frame.frame_id += 2;
    orientation_inferred.frame.actions.sequence += 2;
    orientation_inferred.publication_milliseconds += 20;
    check(mod::stabilized_controller_weapon_pose(
              orientation_inferred, xr::Hand::Right, 1'021, &state, &pose) &&
              state.generation == orientation_inferred.generation,
          "inferred valid orientation advances the stabilized pose");
    const auto accepted_generation = state.generation;

    auto position_invalid = snapshot;
    position_invalid.generation += 3;
    position_invalid.frame.frame_id += 3;
    position_invalid.frame.actions.sequence += 3;
    position_invalid.publication_milliseconds += 30;
    position_invalid.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)]
        .grip.position_valid = false;
    check(!mod::stabilized_controller_weapon_pose(
              position_invalid, xr::Hand::Right, 1'031, &state, &pose) &&
              state.valid && state.generation == accepted_generation,
          "transient invalid hand pose retains stabilized history");

    auto recovered = snapshot;
    recovered.generation += 4;
    recovered.frame.frame_id += 4;
    recovered.frame.actions.sequence += 4;
    recovered.publication_milliseconds += 40;
    recovered.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)]
        .grip.pose.position.x += 0.05F;
    check(mod::stabilized_controller_weapon_pose(
              recovered, xr::Hand::Right, 1'041, &state, &pose) &&
              state.generation == recovered.generation,
          "valid recovery is checked against and advances retained history");
}

void test_left_controller_can_drive_same_weapon_contract() {
    auto snapshot = tracked_snapshot();
    constexpr float kHalfSqrtTwo = 0.70710678118F;
    snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Left)]
        .grip.pose.orientation = {
            0.0F, kHalfSqrtTwo, 0.0F, kHalfSqrtTwo};
    mod::ControllerWeaponPose pose{};
    check(
        mod::controller_weapon_pose(
            snapshot, xr::Hand::Left, 1'001, &pose),
        "valid left-controller weapon pose");
    check_vector(
        pose.grip_position,
        {0.40F * xr::kIwUnitsPerMeter,
         0.20F * xr::kIwUnitsPerMeter,
         0.15F * xr::kIwUnitsPerMeter},
        "left OpenXR grip converts through the shared IW mapping");
    check_vector(
        pose.aim_axis.forward, {1.0F, 0.0F, 0.0F},
        "left aim remains weapon-forward despite an upright grip orientation");

    xr::Vec3f world{};
    check(mod::controller_grip_world({}, {}, pose, &world),
          "left controller grip composes into world space");
    check_vector(world, pose.grip_position,
                 "identity camera preserves left grip position");

}

void test_controller_pose_is_tracking_anchor_relative() {
    auto snapshot = tracked_snapshot();
    constexpr float kHalfSqrtTwo = 0.70710678118F;
    snapshot.frame.head_center.orientation = {
        0.0F, kHalfSqrtTwo, 0.0F, kHalfSqrtTwo};
    auto& right = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    right.aim.pose.orientation = snapshot.frame.head_center.orientation;
    right.grip.pose.position = snapshot.tracking_anchor.position;

    mod::RightControllerWeaponPose pose{};
    check(mod::right_controller_weapon_pose(snapshot, 1'001, &pose),
          "same head/controller yaw is valid");
    check_vector(pose.aim_axis.forward, {0.0F, 1.0F, 0.0F},
                 "controller yaw is retained relative to frozen anchor");
    check_vector(pose.grip_position, {},
                 "anchor-centered grip has zero local translation");
}

void test_current_head_local_pose_composes_to_established_world_contract() {
    auto snapshot = tracked_snapshot();
    snapshot.tracking_anchor = {
        yaw_orientation(-20.0F), {0.40F, 1.20F, -0.30F}};
    const xr::Posef head_from_anchor{
        yaw_orientation(30.0F), {0.10F, -0.05F, 0.08F}};
    snapshot.frame.head_center =
        compose_pose(snapshot.tracking_anchor, head_from_anchor);

    const xr::Posef controller_from_head{
        yaw_orientation(-12.0F), {0.24F, -0.13F, -0.38F}};
    const xr::Posef controller_reference =
        compose_pose(snapshot.frame.head_center, controller_from_head);
    auto& right = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    right.grip.pose = controller_reference;
    right.aim.pose = controller_reference;

    mod::ControllerWeaponPose anchor_local{};
    mod::ControllerWeaponPose head_local{};
    check(mod::controller_weapon_pose(
              snapshot, xr::Hand::Right, 1'001, &anchor_local),
          "established anchor-local controller pose remains valid");
    check(mod::current_head_local_controller_weapon_pose(
              snapshot, xr::Hand::Right, 1'001, &head_local),
          "raw current-head-local controller pose is valid");

    const xr::EnginePose expected_head_local = xr::OpenXrPoseToIwRelative(
        controller_reference, snapshot.frame.head_center,
        xr::kIwUnitsPerMeter);
    check_vector(head_local.grip_position, expected_head_local.position,
                 "current-head-local wrapper preserves exact H inverse C position");
    check_basis(head_local.aim_axis, expected_head_local.axis,
                "current-head-local wrapper preserves exact H inverse C aim");

    const xr::Vec3f body_origin{120.0F, -45.0F, 18.0F};
    const xr::Basis3f body_axis = yaw_basis(37.0F);
    const xr::EnginePose head_from_body = xr::OpenXrPoseToIwRelative(
        snapshot.frame.head_center, snapshot.tracking_anchor,
        xr::kIwUnitsPerMeter);
    const xr::Vec3f head_offset =
        compose_direction(body_axis, head_from_body.position);
    const xr::Vec3f head_world_origin{
        body_origin.x + head_offset.x,
        body_origin.y + head_offset.y,
        body_origin.z + head_offset.z,
    };
    const xr::Basis3f head_world_axis =
        compose_basis(body_axis, head_from_body.axis);

    xr::Vec3f anchor_world_grip{};
    xr::Vec3f head_world_grip{};
    check(mod::controller_grip_world(
              body_origin, body_axis, anchor_local, &anchor_world_grip) &&
              mod::controller_grip_world(
                  head_world_origin, head_world_axis, head_local,
                  &head_world_grip),
          "both controller reference bases compose to world space");
    check_vector(head_world_grip, anchor_world_grip,
                 "head-local and anchor-local paths reconstruct one grip world point");

    mod::WeaponAttachmentState anchor_attachment{
        true, {2.0F, -1.0F, 0.5F}, yaw_basis(8.0F)};
    auto head_attachment = anchor_attachment;
    xr::Vec3f anchor_weapon_origin{};
    xr::Basis3f anchor_weapon_axis{};
    xr::Vec3f head_weapon_origin{};
    xr::Basis3f head_weapon_axis{};
    check(mod::apply_controller_weapon_placement(
              body_origin, body_axis, anchor_local, &anchor_attachment,
              &anchor_weapon_origin, &anchor_weapon_axis) &&
              mod::apply_controller_weapon_placement(
                  head_world_origin, head_world_axis, head_local,
                  &head_attachment, &head_weapon_origin, &head_weapon_axis),
          "same controller attachment is valid under either coherent base");
    check_vector(head_weapon_origin, anchor_weapon_origin,
                 "head-local path preserves the established attachment origin");
    check_basis(head_weapon_axis, anchor_weapon_axis,
                "head-local path preserves the established attachment axis");

    const auto right_index = static_cast<std::uint32_t>(xr::Hand::Right);
    snapshot.weapon_poses[right_index].current_head_local_valid = true;
    snapshot.weapon_poses[right_index].filtered_current_head_local_pose =
        {head_local.grip_position, head_local.aim_axis};
    mod::ControllerWeaponPose published{};
    check(mod::published_current_head_local_controller_weapon_pose(
              snapshot, xr::Hand::Right, 1'001, &published),
          "published current-head-local pose is readable");
    check_vector_exact(published.grip_position, head_local.grip_position,
                       "published head-local position is immutable");
    check_basis_exact(published.aim_axis, head_local.aim_axis,
                      "published head-local aim is immutable");

    const xr::Posef filtered_from_head{
        yaw_orientation(7.0F), {0.18F, -0.09F, -0.31F}};
    const xr::Posef filtered_reference =
        compose_pose(snapshot.frame.head_center, filtered_from_head);
    snapshot.weapon_poses[right_index].valid = true;
    snapshot.weapon_poses[right_index].filtered_grip_position =
        filtered_reference.position;
    snapshot.weapon_poses[right_index].filtered_aim_orientation =
        filtered_reference.orientation;
    mod::ControllerWeaponPose cod4_ordered{};
    check(mod::published_cod4_current_head_local_controller_weapon_pose(
              snapshot, xr::Hand::Right, 1'001, &cod4_ordered),
          "COD4 filter-before-current-head pose is readable");
    const xr::EnginePose expected_filtered_head_local =
        xr::OpenXrPoseToIwRelative(
            filtered_reference, snapshot.frame.head_center,
            xr::kIwUnitsPerMeter);
    check_vector(cod4_ordered.grip_position,
                 expected_filtered_head_local.position,
                 "COD4 ordering removes the current head after filtering");
    check_basis(cod4_ordered.aim_axis, expected_filtered_head_local.axis,
                "COD4 ordering preserves filtered aim relative to current head");
}

void test_stable_two_hand_pose_rebases_after_filtering() {
    mod::ControllerWeaponPose source{};
    source.grip_position = {17.0F, -6.0F, 4.0F};
    source.aim_axis = yaw_roll_basis(11.0F, 23.0F);

    mod::ControllerWeaponPose identity_result{};
    check(mod::rebase_controller_weapon_pose_to_reference(
              xr::EnginePose{}, source, &identity_result),
          "identity reference accepts a stable two-hand pose");
    check_vector_exact(identity_result.grip_position, source.grip_position,
                       "identity rebase preserves the stable grip exactly");
    check_basis_exact(identity_result.aim_axis, source.aim_axis,
                      "identity rebase preserves the stable sight exactly");

    const std::array<xr::EnginePose, 2> head_references{{
        {{9.0F, -3.0F, 2.0F}, yaw_basis(41.0F)},
        {{-5.0F, 8.0F, -1.0F}, yaw_basis(-33.0F)},
    }};
    for (const auto& head_from_anchor : head_references) {
        mod::ControllerWeaponPose head_local{};
        check(mod::rebase_controller_weapon_pose_to_reference(
                  head_from_anchor, source, &head_local),
              "moving head reference accepts the stable two-hand pose");

        const xr::Vec3f reconstructed_offset = compose_direction(
            head_from_anchor.axis, head_local.grip_position);
        const xr::Vec3f reconstructed_grip{
            head_from_anchor.position.x + reconstructed_offset.x,
            head_from_anchor.position.y + reconstructed_offset.y,
            head_from_anchor.position.z + reconstructed_offset.z,
        };
        check_vector(reconstructed_grip, source.grip_position,
                     "head-local grip reconstructs one stable anchor point");
        check_basis(compose_basis(head_from_anchor.axis, head_local.aim_axis),
                    source.aim_axis,
                    "head-local sight reconstructs one stable anchor direction");
    }

    mod::ControllerWeaponPose sentinel{};
    sentinel.grip_position = {91.0F, -82.0F, 73.0F};
    sentinel.aim_axis = yaw_basis(37.0F);
    const auto preserved_sentinel = sentinel;
    auto invalid_reference = head_references.front();
    invalid_reference.axis.forward = {};
    check(!mod::rebase_controller_weapon_pose_to_reference(
              invalid_reference, source, &sentinel),
          "invalid head reference fails closed");
    check_vector_exact(sentinel.grip_position,
                       preserved_sentinel.grip_position,
                       "invalid head reference preserves caller grip output");
    check_basis_exact(sentinel.aim_axis, preserved_sentinel.aim_axis,
                      "invalid head reference preserves caller sight output");

    auto invalid_source = source;
    invalid_source.grip_position.x =
        std::numeric_limits<float>::quiet_NaN();
    check(!mod::rebase_controller_weapon_pose_to_reference(
              head_references.front(), invalid_source, &sentinel),
          "invalid stable pose fails closed");
    check_vector_exact(sentinel.grip_position,
                       preserved_sentinel.grip_position,
                       "invalid stable pose preserves caller grip output");
    check_basis_exact(sentinel.aim_axis, preserved_sentinel.aim_axis,
                      "invalid stable pose preserves caller sight output");
}

void test_weapon_pose_filter_matches_cod4_response_once_per_generation() {
    check(near(mod::kWeaponPositionResponse, 0.45F),
          "weapon position response matches COD4");
    check(near(mod::kWeaponOrientationResponse, 0.55F),
          "weapon orientation response matches COD4");

    auto snapshot = tracked_snapshot();
    mod::ControllerWeaponPoseFilterState state{};
    mod::ControllerWeaponPose first{};
    check(mod::stabilized_controller_weapon_pose(
              snapshot, xr::Hand::Right, 1'001, &state, &first),
          "first stabilized weapon sample is accepted");

    mod::ControllerWeaponPose raw_first{};
    check(mod::controller_weapon_pose(
              snapshot, xr::Hand::Right, 1'001, &raw_first),
          "first raw weapon sample is accepted");
    check_vector(first.grip_position, raw_first.grip_position,
                 "first stabilized position starts without pickup lag");
    check_basis(first.aim_axis, raw_first.aim_axis,
                "first stabilized orientation starts without pickup lag");

    auto moved = snapshot;
    moved.generation = snapshot.generation + 1;
    moved.frame.frame_id += 1;
    moved.frame.actions.sequence += 1;
    moved.publication_milliseconds = 1'014;
    auto& moved_right = moved.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    moved_right.grip.pose.position = {0.45F, 0.10F, -0.30F};
    constexpr float kHalfSqrtTwo = 0.70710678118F;
    moved_right.aim.pose.orientation = {
        0.0F, kHalfSqrtTwo, 0.0F, kHalfSqrtTwo};

    mod::ControllerWeaponPose filtered{};
    check(mod::stabilized_controller_weapon_pose(
              moved, xr::Hand::Right, 1'015, &state, &filtered),
          "second stabilized weapon sample is accepted");

    auto expected_snapshot = moved;
    auto& expected_right = expected_snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    expected_right.grip.pose.position = {
        0.25F + (0.45F - 0.25F) * mod::kWeaponPositionResponse,
        -0.10F + (0.10F + 0.10F) * mod::kWeaponPositionResponse,
        -0.50F + (-0.30F + 0.50F) * mod::kWeaponPositionResponse,
    };
    xr::Quaternionf expected_orientation{
        0.0F,
        kHalfSqrtTwo * mod::kWeaponOrientationResponse,
        0.0F,
        1.0F + (kHalfSqrtTwo - 1.0F) *
            mod::kWeaponOrientationResponse,
    };
    const float orientation_length = std::sqrt(
        expected_orientation.y * expected_orientation.y +
        expected_orientation.w * expected_orientation.w);
    expected_orientation.y /= orientation_length;
    expected_orientation.w /= orientation_length;
    expected_right.aim.pose.orientation = expected_orientation;
    mod::ControllerWeaponPose expected{};
    check(mod::controller_weapon_pose(
              expected_snapshot, xr::Hand::Right, 1'015, &expected),
          "expected blended pose converts through the raw contract");
    check_vector(filtered.grip_position, expected.grip_position,
                 "position uses the COD4 0.45 response");
    check_basis(filtered.aim_axis, expected.aim_axis,
                "orientation uses normalized COD4 0.55 response");

    // A viewmodel can consume one XR publication more than once. Changing the
    // fixture's raw values without changing its generation must not advance
    // the filter again or make the two render consumers disagree.
    auto repeated = moved;
    auto& repeated_right = repeated.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    repeated_right.grip.pose.position = {5.0F, 4.0F, 3.0F};
    repeated_right.aim.pose.orientation = {0.0F, 0.0F, 1.0F, 0.0F};
    mod::ControllerWeaponPose repeated_pose{};
    check(mod::stabilized_controller_weapon_pose(
              repeated, xr::Hand::Right, 1'016, &state, &repeated_pose),
          "repeated-generation weapon sample is accepted");
    check_vector(repeated_pose.grip_position, filtered.grip_position,
                 "one XR generation advances position only once");
    check_basis(repeated_pose.aim_axis, filtered.aim_axis,
                "one XR generation advances orientation only once");

    auto resumed = moved;
    resumed.generation += 1;
    resumed.frame.frame_id += 1;
    resumed.frame.actions.sequence += 1;
    resumed.publication_milliseconds =
        moved.publication_milliseconds +
        mod::kMaximumControllerFrameAgeMilliseconds + 1;
    auto& resumed_right = resumed.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    resumed_right.grip.pose.position = {-0.30F, 0.20F, -0.70F};
    resumed_right.aim.pose.orientation = {0.0F, 0.0F, 1.0F, 0.0F};
    mod::ControllerWeaponPose after_gap{};
    mod::ControllerWeaponPose raw_after_gap{};
    check(mod::stabilized_controller_weapon_pose(
              resumed, xr::Hand::Right,
              resumed.publication_milliseconds + 1,
              &state, &after_gap) &&
              mod::controller_weapon_pose(
                  resumed, xr::Hand::Right,
                  resumed.publication_milliseconds + 1,
                  &raw_after_gap),
          "tracking resume after a long gap is accepted");
    check_vector(after_gap.grip_position, raw_after_gap.grip_position,
                 "long tracking gap resets position history");
    check_basis(after_gap.aim_axis, raw_after_gap.aim_axis,
                "long tracking gap resets orientation history");

    auto invalid = resumed;
    invalid.frame.actions.focused = false;
    check(!mod::stabilized_controller_weapon_pose(
              invalid, xr::Hand::Right,
              invalid.publication_milliseconds + 1,
              &state, &after_gap) && !state.valid,
          "invalid tracking fails closed and clears filter history");
    mod::reset_controller_weapon_pose_filter(&state);
    check(!state.valid && state.generation == 0,
          "explicit filter reset clears cached generation");
}

void test_weapon_pose_filter_matches_skipped_generation_responses() {
    auto snapshot = tracked_snapshot();
    mod::ControllerWeaponPoseFilterState state{};
    mod::ControllerWeaponPose seeded{};
    check(mod::stabilized_controller_weapon_pose(
              snapshot, xr::Hand::Right, 1'001, &state, &seeded),
          "skipped-generation fixture seeds without lag");

    auto moved = snapshot;
    constexpr std::uint64_t kGenerationDelta = 4;
    moved.generation += kGenerationDelta;
    moved.frame.frame_id += kGenerationDelta;
    moved.frame.actions.sequence += kGenerationDelta;
    moved.publication_milliseconds += 40;
    auto& moved_right = moved.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    moved_right.grip.pose.position = {0.45F, 0.10F, -0.30F};
    constexpr float kHalfSqrtTwo = 0.70710678118F;
    moved_right.aim.pose.orientation = {
        0.0F, kHalfSqrtTwo, 0.0F, kHalfSqrtTwo};

    mod::ControllerWeaponPose filtered{};
    check(mod::stabilized_controller_weapon_pose(
              moved, xr::Hand::Right, 1'041, &state, &filtered),
          "skipped controller generations are accepted");

    const float position_response = mod::kWeaponPositionResponse;
    const float orientation_response = mod::kWeaponOrientationResponse;
    auto expected_snapshot = moved;
    auto& expected_right = expected_snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    expected_right.grip.pose.position = {
        0.25F + (0.45F - 0.25F) * position_response,
        -0.10F + (0.10F + 0.10F) * position_response,
        -0.50F + (-0.30F + 0.50F) * position_response,
    };
    xr::Quaternionf expected_orientation{
        0.0F,
        kHalfSqrtTwo * orientation_response,
        0.0F,
        1.0F + (kHalfSqrtTwo - 1.0F) * orientation_response,
    };
    const float orientation_length = std::sqrt(
        expected_orientation.y * expected_orientation.y +
        expected_orientation.w * expected_orientation.w);
    expected_orientation.y /= orientation_length;
    expected_orientation.w /= orientation_length;
    expected_right.aim.pose.orientation = expected_orientation;
    mod::ControllerWeaponPose expected{};
    check(mod::controller_weapon_pose(
              expected_snapshot, xr::Hand::Right, 1'041, &expected),
          "skipped-generation expected pose converts through raw contract");
    check_vector(filtered.grip_position, expected.grip_position,
                 "one draw applies one COD4 position response");
    check_basis(filtered.aim_axis, expected.aim_axis,
                "one draw applies one COD4 orientation response");

    const auto cached_state = state;
    auto repeated = moved;
    repeated.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)]
        .grip.pose.position = {9.0F, 8.0F, 7.0F};
    mod::ControllerWeaponPose cached{};
    check(mod::stabilized_controller_weapon_pose(
              repeated, xr::Hand::Right, 1'042, &state, &cached),
          "repeated skipped generation returns cached result");
    check(state.generation == cached_state.generation,
          "repeated skipped generation does not advance history");
    check_vector_exact(cached.grip_position, filtered.grip_position,
                       "repeated skipped generation preserves output bits");
    check_basis_exact(cached.aim_axis, filtered.aim_axis,
                      "repeated skipped generation preserves basis bits");
}

void test_weapon_pose_filter_rejects_tracking_discontinuities() {
    const auto snapshot = tracked_snapshot();
    mod::ControllerWeaponPoseFilterState state{};
    mod::ControllerWeaponPose pose{};
    check(mod::stabilized_controller_weapon_pose(
              snapshot, xr::Hand::Right, 1'001, &state, &pose),
          "first sample is exempt from discontinuity rejection");

    auto displaced = snapshot;
    displaced.generation += 1;
    displaced.frame.frame_id += 1;
    displaced.frame.actions.sequence += 1;
    displaced.publication_milliseconds += 10;
    displaced.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)]
        .grip.pose.position.x +=
        mod::kMaximumWeaponPositionDiscontinuityMeters + 0.01F;
    check(!mod::stabilized_controller_weapon_pose(
              displaced, xr::Hand::Right, 1'011, &state, &pose) &&
              state.valid,
          "implausible position jump is rejected while history is retained");
    check(!mod::stabilized_controller_weapon_pose(
              displaced, xr::Hand::Right, 1'012, &state, &pose) &&
              state.valid,
          "persistent relocalized position cannot become a new first sample");

    mod::reset_controller_weapon_pose_filter(&state);
    check(mod::stabilized_controller_weapon_pose(
              displaced, xr::Hand::Right, 1'011, &state, &pose),
          "caller reset accepts a new position as the recovery baseline");

    auto rotated = displaced;
    rotated.generation += 1;
    rotated.frame.frame_id += 1;
    rotated.frame.actions.sequence += 1;
    rotated.publication_milliseconds += 10;
    rotated.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)]
        .aim.pose.orientation = {0.0F, 1.0F, 0.0F, 0.0F};
    check(!mod::stabilized_controller_weapon_pose(
              rotated, xr::Hand::Right, 1'021, &state, &pose) &&
              state.valid,
          "implausible angular jump is rejected while history is retained");
    check(!mod::stabilized_controller_weapon_pose(
              rotated, xr::Hand::Right, 1'022, &state, &pose) &&
              state.valid,
          "persistent relocalized orientation cannot become a new first sample");

    mod::reset_controller_weapon_pose_filter(&state);
    check(mod::stabilized_controller_weapon_pose(
              rotated, xr::Hand::Right, 1'021, &state, &pose),
          "caller reset accepts a new orientation as the recovery baseline");
}

void test_weapon_pose_filter_uses_shortest_quaternion_hemisphere() {
    auto snapshot = tracked_snapshot();
    constexpr float kHalfSqrtTwo = 0.70710678118F;
    auto& right = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    right.aim.pose.orientation = {
        0.0F, kHalfSqrtTwo, 0.0F, kHalfSqrtTwo};

    mod::ControllerWeaponPoseFilterState state{};
    mod::ControllerWeaponPose first{};
    check(mod::stabilized_controller_weapon_pose(
              snapshot, xr::Hand::Right, 1'001, &state, &first),
          "first quaternion-hemisphere sample is accepted");

    auto equivalent = snapshot;
    equivalent.generation += 1;
    equivalent.frame.frame_id += 1;
    equivalent.frame.actions.sequence += 1;
    equivalent.publication_milliseconds += 13;
    auto& equivalent_right = equivalent.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    equivalent_right.aim.pose.orientation = {
        -0.0F, -kHalfSqrtTwo, -0.0F, -kHalfSqrtTwo};

    mod::ControllerWeaponPose filtered{};
    check(mod::stabilized_controller_weapon_pose(
              equivalent, xr::Hand::Right,
              equivalent.publication_milliseconds + 1,
              &state, &filtered),
          "negated equivalent quaternion is accepted");
    check_basis(filtered.aim_axis, first.aim_axis,
                "q and -q do not create a false weapon rotation");
}

void test_two_hand_rebaseline_uses_one_coherent_publication() {
    auto low = tracked_snapshot();
    auto& low_right = low.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    auto& low_left = low.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Left)];
    low_right.grip.pose.position = {0.20F, 0.00F, -0.40F};
    low_left.grip.pose.position = {0.20F, 0.00F, -0.70F};

    std::array<mod::ControllerWeaponPoseFilterState, xr::kHandCount> filters{};
    mod::ControllerWeaponPose incumbent{};
    const auto right_index = static_cast<std::size_t>(xr::Hand::Right);
    check(mod::stabilized_controller_weapon_pose(
              low, xr::Hand::Right, 1'001,
              &filters[right_index], &incumbent),
          "right owner establishes a low filtered pose");

    auto raised = low;
    raised.generation += 1;
    raised.frame.frame_id += 1;
    raised.frame.actions.sequence += 1;
    raised.publication_milliseconds += 14;
    auto& raised_right = raised.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    auto& raised_left = raised.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Left)];
    raised_right.grip.pose.position.y += 0.20F;
    raised_left.grip.pose.position.y += 0.20F;

    // The incumbent advances by the ordinary 0.45 response before the grip
    // edge is processed, mirroring the render hook's outgoing-pose snapshot.
    check(mod::stabilized_controller_weapon_pose(
              raised, xr::Hand::Right,
              raised.publication_milliseconds + 1,
              &filters[right_index], &incumbent),
          "moving incumbent pose advances before pair entry");

    mod::ControllerWeaponPose right{};
    mod::ControllerWeaponPose left{};
    mod::ControllerWeaponPose pair{};
    check(mod::rebaseline_two_hand_controller_weapon_poses(
              raised, raised.publication_milliseconds + 1,
              &filters, &right, &left, &pair),
          "valid pair entry rebaselines both hands transactionally");
    check(near(pair.aim_axis.forward.x, 1.0F, 0.00001F) &&
              near(pair.aim_axis.forward.y, 0.0F, 0.00001F) &&
              near(pair.aim_axis.forward.z, 0.0F, 0.00001F),
          "common eye-level translation cannot create false pair pitch");
    check(near(
              filters[static_cast<std::size_t>(xr::Hand::Right)]
                  .filtered_grip_position.y,
              raised_right.grip.pose.position.y) &&
              near(filters[static_cast<std::size_t>(xr::Hand::Left)]
                       .filtered_grip_position.y,
                   raised_left.grip.pose.position.y),
          "both pair filters share the same current publication age");

    const auto established_filters = filters;
    const auto established_right = right;
    const auto established_left = left;
    const auto established_pair = pair;
    auto rejected = raised;
    rejected.generation += 1;
    rejected.frame.frame_id += 1;
    rejected.frame.actions.sequence += 1;
    rejected.publication_milliseconds += 14;
    rejected.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Left)]
        .grip.position_valid = false;
    check(!mod::rebaseline_two_hand_controller_weapon_poses(
              rejected, rejected.publication_milliseconds + 1,
              &filters, &right, &left, &pair),
          "an incomplete pair cannot replace established histories");
    for (std::size_t index = 0; index < filters.size(); ++index) {
        const auto& actual = filters[index];
        const auto& expected = established_filters[index];
        check(actual.valid == expected.valid &&
                  actual.generation == expected.generation &&
                  actual.publication_milliseconds ==
                      expected.publication_milliseconds,
              "failed pair rebaseline preserves filter metadata");
        check_vector(
            actual.filtered_grip_position,
            expected.filtered_grip_position,
            "failed pair rebaseline preserves filtered position");
        check(near(actual.filtered_aim_orientation.x,
                   expected.filtered_aim_orientation.x) &&
                  near(actual.filtered_aim_orientation.y,
                       expected.filtered_aim_orientation.y) &&
                  near(actual.filtered_aim_orientation.z,
                       expected.filtered_aim_orientation.z) &&
                  near(actual.filtered_aim_orientation.w,
                       expected.filtered_aim_orientation.w),
              "failed pair rebaseline preserves filtered orientation");
    }
    check_vector(right.grip_position, established_right.grip_position,
                 "failed rebaseline preserves right output");
    check_vector(left.grip_position, established_left.grip_position,
                 "failed rebaseline preserves left output");
    check_basis(pair.aim_axis, established_pair.aim_axis,
                "failed rebaseline preserves pair output");
}

void test_two_hand_common_translation_keeps_a_rigid_hand_line() {
    auto snapshot = tracked_snapshot();
    auto& initial_right = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    auto& initial_left = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Left)];
    initial_right.grip.pose.position = {0.00F, 0.00F, -0.35F};
    initial_left.grip.pose.position = {0.00F, 0.00F, -0.65F};

    std::array<mod::ControllerWeaponPoseFilterState, xr::kHandCount> filters{};
    const auto right_index = static_cast<std::size_t>(xr::Hand::Right);
    mod::ControllerWeaponPose right{};
    mod::ControllerWeaponPose raw_right{};
    mod::ControllerWeaponPose raw_left{};
    mod::ControllerWeaponPose initial_pair{};
    check(mod::stabilized_controller_weapon_pose(
              snapshot, xr::Hand::Right, 1'001,
              &filters[right_index], &right) &&
              mod::controller_weapon_pose(
                  snapshot, xr::Hand::Right, 1'001, &raw_right) &&
              mod::controller_weapon_pose(
                  snapshot, xr::Hand::Left, 1'001, &raw_left) &&
              mod::two_hand_controller_weapon_pose_from_raw_grip_delta(
                  right, raw_right, raw_left, &initial_pair),
          "stationary pair establishes a filtered root and raw hand delta");

    auto translated = snapshot;
    translated.generation += 1;
    translated.frame.frame_id += 1;
    translated.frame.actions.sequence += 1;
    translated.publication_milliseconds += 14;
    auto& translated_right = translated.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Right)];
    auto& translated_left = translated.frame.actions.hands[
        static_cast<std::uint32_t>(xr::Hand::Left)];
    constexpr xr::Vec3f kCommonTranslation{0.20F, -0.16F, 0.12F};
    translated_right.grip.pose.position.x += kCommonTranslation.x;
    translated_right.grip.pose.position.y += kCommonTranslation.y;
    translated_right.grip.pose.position.z += kCommonTranslation.z;
    translated_left.grip.pose.position.x += kCommonTranslation.x;
    translated_left.grip.pose.position.y += kCommonTranslation.y;
    translated_left.grip.pose.position.z += kCommonTranslation.z;

    mod::ControllerWeaponPose translated_pair{};
    check(mod::stabilized_controller_weapon_pose(
              translated, xr::Hand::Right,
              translated.publication_milliseconds + 1,
              &filters[right_index], &right) &&
              mod::controller_weapon_pose(
                  translated, xr::Hand::Right,
                  translated.publication_milliseconds + 1, &raw_right) &&
              mod::controller_weapon_pose(
                  translated, xr::Hand::Left,
                  translated.publication_milliseconds + 1, &raw_left) &&
              mod::two_hand_controller_weapon_pose_from_raw_grip_delta(
                  right, raw_right, raw_left, &translated_pair),
          "common pair translation advances the root without skewing its delta");
    check_basis(
        translated_pair.aim_axis, initial_pair.aim_axis,
        "common two-hand motion cannot fabricate a sight-axis rotation");
    check_vector_exact(
        translated_pair.grip_position, right.grip_position,
        "pair translation keeps the filtered right grip as its rigid anchor");
}

void test_raw_pair_delta_tracks_steering_without_filter_lag() {
    mod::ControllerWeaponPose stabilized_right{};
    stabilized_right.grip_position = {4.0F, -2.0F, 1.0F};
    stabilized_right.aim_axis = {};
    mod::ControllerWeaponPose raw_right{};
    raw_right.grip_position = {100.0F, 50.0F, -25.0F};
    raw_right.aim_axis = {};
    mod::ControllerWeaponPose raw_left{};
    raw_left.grip_position = {112.0F, 50.0F, -25.0F};
    raw_left.aim_axis = {};

    mod::ControllerWeaponPose pair{};
    check(mod::two_hand_controller_weapon_pose_from_raw_grip_delta(
              stabilized_right, raw_right, raw_left, &pair),
          "raw pair delta establishes a straight sight line");
    check_basis(pair.aim_axis, {},
                "straight raw hand delta points the sight forward");

    constexpr float kFifteenDegrees = 0.261799387799F;
    raw_left.grip_position = {
        raw_right.grip_position.x + 12.0F * std::cos(kFifteenDegrees),
        raw_right.grip_position.y + 12.0F * std::sin(kFifteenDegrees),
        raw_right.grip_position.z,
    };
    check(mod::two_hand_controller_weapon_pose_from_raw_grip_delta(
              stabilized_right, raw_right, raw_left, &pair),
          "fresh raw hand delta accepts deliberate steering");
    check(near(basis_yaw_degrees(pair.aim_axis), 15.0F, 0.01F),
          "fresh pair steering reaches the current hand line immediately");
    check_vector_exact(pair.grip_position, stabilized_right.grip_position,
                       "raw steering never bypasses the stabilized root");
}

void test_raw_pair_delta_fails_closed_without_mutating_output() {
    mod::ControllerWeaponPose stabilized_right{};
    stabilized_right.grip_position = {4.0F, -2.0F, 1.0F};
    stabilized_right.aim_axis = {};
    mod::ControllerWeaponPose raw_right{};
    raw_right.grip_position = {100.0F, 50.0F, -25.0F};
    raw_right.aim_axis = {};
    mod::ControllerWeaponPose raw_left{};
    raw_left.grip_position = {112.0F, 50.0F, -25.0F};
    raw_left.aim_axis = {};

    mod::ControllerWeaponPose sentinel{};
    sentinel.grip_position = {-7.0F, 8.0F, 9.0F};
    sentinel.aim_axis = yaw_basis(21.0F);
    const auto expected = sentinel;
    const auto check_preserved = [&]() {
        check_vector_exact(sentinel.grip_position, expected.grip_position,
                           "a rejected raw pair preserves output position");
        check_basis(sentinel.aim_axis, expected.aim_axis,
                    "a rejected raw pair preserves output orientation");
    };

    raw_left.grip_position = {101.0F, 50.0F, -25.0F};
    check(!mod::two_hand_controller_weapon_pose_from_raw_grip_delta(
              stabilized_right, raw_right, raw_left, &sentinel),
          "a raw pair below the minimum hand separation is rejected");
    check_preserved();

    raw_left.grip_position = {140.0F, 50.0F, -25.0F};
    check(!mod::two_hand_controller_weapon_pose_from_raw_grip_delta(
              stabilized_right, raw_right, raw_left, &sentinel),
          "a raw pair above the maximum hand separation is rejected");
    check_preserved();

    raw_left.grip_position = {112.0F, 50.0F, -25.0F};
    raw_right.grip_position.x = std::numeric_limits<float>::quiet_NaN();
    check(!mod::two_hand_controller_weapon_pose_from_raw_grip_delta(
              stabilized_right, raw_right, raw_left, &sentinel),
          "a non-finite raw weapon hand is rejected");
    check_preserved();

    check(!mod::two_hand_controller_weapon_pose_from_raw_grip_delta(
              stabilized_right, raw_right, raw_left, nullptr),
          "a raw pair rejects a null output");
}

void test_two_hand_orientation_filter_initializes_and_reuses_sample() {
    mod::ControllerWeaponPose raw{};
    raw.grip_position = {4.25F, -8.50F, 12.75F};
    raw.aim_axis = yaw_basis(7.0F);

    mod::TwoHandWeaponOrientationFilterState state{};
    mod::ControllerWeaponPose pose{};
    check(mod::stabilized_two_hand_controller_weapon_pose(
              40, 2'000, kIdentityTrackingAnchorOrientation,
              raw, &state, &pose),
          "first valid pair initializes the two-hand orientation filter");
    check(state.valid && state.generation == 40 &&
              state.publication_milliseconds == 2'000,
          "first valid pair records its complete XR sample key");
    check_vector_exact(pose.grip_position, raw.grip_position,
                       "pair initialization preserves grip exactly");
    check_basis(pose.aim_axis, raw.aim_axis,
                "pair initialization has no orientation startup lag");
    const xr::Basis3f cached_initial_axis = pose.aim_axis;

    mod::ControllerWeaponPose changed = raw;
    changed.grip_position = {-19.0F, 23.0F, 31.0F};
    changed.aim_axis = yaw_basis(15.0F);
    check(mod::stabilized_two_hand_controller_weapon_pose(
              40, 2'010, kIdentityTrackingAnchorOrientation,
              changed, &state, &pose),
          "a repeated generation reuses the cached orientation");
    check_basis_exact(
        pose.aim_axis, cached_initial_axis,
        "a repeated generation returns the exact cached orientation");
    check_vector_exact(pose.grip_position, changed.grip_position,
                       "a repeated generation still preserves raw grip");
    check(state.generation == 40 &&
              state.publication_milliseconds == 2'000,
          "a repeated generation cannot replace the accepted sample key");

    check(mod::stabilized_two_hand_controller_weapon_pose(
              41, 2'000, kIdentityTrackingAnchorOrientation,
              changed, &state, &pose),
          "a repeated publication reuses the cached orientation");
    check_basis_exact(
        pose.aim_axis, cached_initial_axis,
        "a repeated publication returns the exact cached orientation");
    check(state.generation == 40 &&
              state.publication_milliseconds == 2'000,
          "a repeated publication cannot advance filter history");
    check_vector_exact(pose.grip_position, changed.grip_position,
                       "a repeated publication still preserves raw grip");

    const auto established_state = state;
    const auto established_pose = pose;
    check(!mod::stabilized_two_hand_controller_weapon_pose(
              39, 2'020, kIdentityTrackingAnchorOrientation,
              changed, &state, &pose),
          "a regressed XR generation fails closed");
    check(state.valid == established_state.valid &&
              state.generation == established_state.generation &&
              state.publication_milliseconds ==
                  established_state.publication_milliseconds &&
              state.previous_raw_direction.x ==
                  established_state.previous_raw_direction.x &&
              state.previous_raw_direction.y ==
                  established_state.previous_raw_direction.y &&
              state.previous_raw_direction.z ==
                  established_state.previous_raw_direction.z &&
              state.filtered_direction.x ==
                  established_state.filtered_direction.x &&
              state.filtered_direction.y ==
                  established_state.filtered_direction.y &&
              state.filtered_direction.z ==
                  established_state.filtered_direction.z,
          "a regressed XR key preserves orientation history");
    check_basis_exact(state.cached_axis, established_state.cached_axis,
                      "a regressed XR key preserves cached orientation");
    check_quaternion_exact(
        state.tracking_anchor_orientation,
        established_state.tracking_anchor_orientation,
        "a regressed XR key preserves the tracking anchor key");
    check_vector_exact(pose.grip_position, established_pose.grip_position,
                       "a rejected XR key preserves the output position");
    check_basis(pose.aim_axis, established_pose.aim_axis,
                "a rejected XR key preserves the output orientation");

    auto invalid = changed;
    invalid.aim_axis.forward = {};
    check(!mod::stabilized_two_hand_controller_weapon_pose(
              41, 2'010, kIdentityTrackingAnchorOrientation,
              invalid, &state, &pose),
          "an invalid raw pair fails closed");
    check(state.generation == established_state.generation &&
              state.publication_milliseconds ==
                  established_state.publication_milliseconds,
          "an invalid raw pair preserves filter history");

    mod::reset_two_hand_weapon_orientation_filter(&state);
    check(!state.valid && state.generation == 0 &&
              state.publication_milliseconds == 0 &&
               state.tracking_anchor_orientation.x == 0.0F &&
               state.tracking_anchor_orientation.y == 0.0F &&
               state.tracking_anchor_orientation.z == 0.0F &&
               state.tracking_anchor_orientation.w == 1.0F &&
               state.previous_raw_direction.x == 0.0F &&
               state.previous_raw_direction.y == 0.0F &&
               state.previous_raw_direction.z == 0.0F &&
               state.filtered_direction.x == 0.0F &&
               state.filtered_direction.y == 0.0F &&
               state.filtered_direction.z == 0.0F,
           "explicit two-hand orientation reset clears its sample key");
}

void test_two_hand_direction_filter_reseeds_on_tracking_anchor_rebase() {
    mod::ControllerWeaponPose raw{};
    raw.grip_position = {3.0F, -2.0F, 1.0F};
    raw.aim_axis = yaw_basis(0.0F);

    mod::TwoHandWeaponOrientationFilterState state{};
    mod::ControllerWeaponPose pose{};
    check(mod::stabilized_two_hand_controller_weapon_pose(
              45, 2'200, kIdentityTrackingAnchorOrientation,
              raw, &state, &pose),
          "anchor-rebase direction history initializes");

    // Body-yaw synchronization rotates the engine camera and the local
    // controller basis in opposite directions without necessarily publishing
    // a new XR generation. The filter must treat the anchor as part of its key
    // and publish the new local direction with zero lag.
    const xr::Quaternionf rebased_anchor = yaw_orientation(1.0F);
    const xr::Basis3f rebased_camera = yaw_basis(1.0F);
    raw.grip_position = {8.0F, 9.0F, 10.0F};
    raw.aim_axis = yaw_basis(-1.0F);
    check(mod::stabilized_two_hand_controller_weapon_pose(
              45, 2'200, rebased_anchor, raw, &state, &pose),
          "same-publication tracking-anchor rebase reseeds direction history");
    check_basis_exact(pose.aim_axis, raw.aim_axis,
                      "anchor rebase has zero local-direction filter lag");
    check_vector_exact(pose.grip_position, raw.grip_position,
                       "anchor rebase preserves the exact current right grip");
    check_vector(compose_direction(rebased_camera, pose.aim_axis.forward),
                 {1.0F, 0.0F, 0.0F},
                 "camera and local pair counter-rotation preserve world aim");

    const auto rebased_state = state;
    const xr::Basis3f rebased_axis = pose.aim_axis;
    xr::Quaternionf equivalent_anchor = rebased_anchor;
    equivalent_anchor.x = -equivalent_anchor.x;
    equivalent_anchor.y = -equivalent_anchor.y;
    equivalent_anchor.z = -equivalent_anchor.z;
    equivalent_anchor.w = -equivalent_anchor.w;
    raw.grip_position = {-4.0F, 6.0F, 12.0F};
    raw.aim_axis = yaw_basis(-8.0F);
    check(mod::stabilized_two_hand_controller_weapon_pose(
              45, 2'200, equivalent_anchor, raw, &state, &pose),
          "sign-negated equivalent anchor reuses the coherent publication");
    check_basis_exact(pose.aim_axis, rebased_axis,
                      "equivalent quaternion sign cannot spuriously reseed");
    check_vector_exact(pose.grip_position, raw.grip_position,
                       "equivalent anchor still preserves current right grip");
    check_basis_exact(state.cached_axis, rebased_state.cached_axis,
                      "equivalent anchor preserves cached direction history");

    const xr::Quaternionf stale_rebased_anchor = yaw_orientation(2.0F);
    raw.aim_axis = yaw_basis(-2.0F);
    const auto before_stale_rebase_state = state;
    const auto before_stale_rebase_pose = pose;
    check(!mod::stabilized_two_hand_controller_weapon_pose(
              46,
              2'200 + mod::kMaximumControllerFrameAgeMilliseconds + 1,
              stale_rebased_anchor, raw, &state, &pose),
          "stale publication cannot bypass failure by changing anchor");
    check(state.generation == before_stale_rebase_state.generation &&
              state.publication_milliseconds ==
                  before_stale_rebase_state.publication_milliseconds,
          "stale anchor rebase preserves the accepted sample key");
    check_vector_exact(state.filtered_direction,
                       before_stale_rebase_state.filtered_direction,
                       "stale anchor rebase preserves direction history");
    check_basis_exact(state.cached_axis,
                      before_stale_rebase_state.cached_axis,
                      "stale anchor rebase preserves cached orientation");
    check_quaternion_exact(
        state.tracking_anchor_orientation,
        before_stale_rebase_state.tracking_anchor_orientation,
        "stale anchor rebase preserves the tracking anchor key");
    check_vector_exact(pose.grip_position,
                       before_stale_rebase_pose.grip_position,
                       "stale anchor rebase preserves caller position");
    check_basis_exact(pose.aim_axis, before_stale_rebase_pose.aim_axis,
                      "stale anchor rebase preserves caller orientation");

    mod::reset_two_hand_weapon_orientation_filter(&state);
    check(mod::stabilized_two_hand_controller_weapon_pose(
              46,
              2'200 + mod::kMaximumControllerFrameAgeMilliseconds + 1,
              stale_rebased_anchor, raw, &state, &pose),
          "explicit reset reseeds after a stale anchor rebase");
    check_basis_exact(pose.aim_axis, raw.aim_axis,
                      "post-reset anchor rebase initializes without lag");

    xr::Quaternionf invalid_anchor{};
    invalid_anchor.x = std::numeric_limits<float>::quiet_NaN();
    const auto preserved_state = state;
    const auto preserved_pose = pose;
    check(!mod::stabilized_two_hand_controller_weapon_pose(
              47,
              2'210 + mod::kMaximumControllerFrameAgeMilliseconds,
              invalid_anchor, raw, &state, &pose),
          "invalid tracking anchor fails closed");
    check(state.generation == preserved_state.generation &&
              state.publication_milliseconds ==
                  preserved_state.publication_milliseconds,
          "invalid tracking anchor preserves the accepted sample key");
    check_vector_exact(state.filtered_direction,
                       preserved_state.filtered_direction,
                       "invalid tracking anchor preserves direction history");
    check_basis_exact(state.cached_axis, preserved_state.cached_axis,
                      "invalid tracking anchor preserves cached orientation");
    check_quaternion_exact(
        state.tracking_anchor_orientation,
        preserved_state.tracking_anchor_orientation,
        "invalid tracking anchor preserves its established key");
    check_vector_exact(pose.grip_position, preserved_pose.grip_position,
                       "invalid tracking anchor preserves caller position");
    check_basis_exact(pose.aim_axis, preserved_pose.aim_axis,
                      "invalid tracking anchor preserves caller orientation");
}

void test_two_hand_direction_filter_is_frame_rate_independent() {
    mod::ControllerWeaponPose baseline{};
    baseline.grip_position = {1.0F, 2.0F, 3.0F};
    baseline.aim_axis = yaw_basis(0.0F);
    mod::ControllerWeaponPose target = baseline;
    target.grip_position = {9.0F, -4.0F, 7.0F};
    target.aim_axis = yaw_basis(30.0F);

    mod::TwoHandWeaponOrientationFilterState single_interval_state{};
    mod::TwoHandWeaponOrientationFilterState subdivided_state{};
    mod::ControllerWeaponPose single_interval_pose{};
    mod::ControllerWeaponPose subdivided_pose{};
    check(mod::stabilized_two_hand_controller_weapon_pose(
              50, 2'500, kIdentityTrackingAnchorOrientation,
              baseline, &single_interval_state,
              &single_interval_pose) &&
              mod::stabilized_two_hand_controller_weapon_pose(
                  50, 2'500, kIdentityTrackingAnchorOrientation,
                  baseline, &subdivided_state,
                  &subdivided_pose),
          "both variable-interval direction filters initialize identically");

    check(mod::stabilized_two_hand_controller_weapon_pose(
              51, 2'540, kIdentityTrackingAnchorOrientation,
              target, &single_interval_state,
              &single_interval_pose),
          "one forty-millisecond publication advances the direction filter");
    for (std::uint64_t interval = 1; interval <= 4; ++interval) {
        check(mod::stabilized_two_hand_controller_weapon_pose(
                  50 + interval, 2'500 + interval * 10,
                  kIdentityTrackingAnchorOrientation, target,
                  &subdivided_state, &subdivided_pose),
              "four ten-millisecond publications advance the direction filter");
    }

    check(near(basis_yaw_degrees(single_interval_pose.aim_axis),
               basis_yaw_degrees(subdivided_pose.aim_axis), 0.002F),
          "equal elapsed time has equal response across publication intervals");
    const float expected_response = 1.0F - std::exp2(
        -40.0F / mod::kTwoHandDirectionFilterHalfLifeMilliseconds);
    check(near(basis_yaw_degrees(single_interval_pose.aim_axis),
               30.0F * expected_response, 0.01F),
          "direction response follows the configured exponential half-life");
    check_vector_exact(single_interval_pose.grip_position,
                       target.grip_position,
                       "single interval retains the exact right-grip anchor");
    check_vector_exact(subdivided_pose.grip_position,
                       target.grip_position,
                       "subdivided intervals retain the exact right-grip anchor");

    // Integer millisecond approximations of 72, 90, and 120 Hz ensure the
    // response is derived from publication time rather than frame count.
    constexpr std::array<std::uint64_t, 3> kRuntimeIntervals{14, 11, 8};
    for (const std::uint64_t interval : kRuntimeIntervals) {
        mod::TwoHandWeaponOrientationFilterState runtime_state{};
        mod::ControllerWeaponPose runtime_pose{};
        check(mod::stabilized_two_hand_controller_weapon_pose(
                  70, 2'700, kIdentityTrackingAnchorOrientation,
                  baseline, &runtime_state, &runtime_pose) &&
                  mod::stabilized_two_hand_controller_weapon_pose(
                      71, 2'700 + interval,
                      kIdentityTrackingAnchorOrientation, target,
                      &runtime_state,
                      &runtime_pose),
              "runtime-rate publication interval advances the direction filter");
        const float interval_response = 1.0F - std::exp2(
            -static_cast<float>(interval) /
            mod::kTwoHandDirectionFilterHalfLifeMilliseconds);
        check(near(basis_yaw_degrees(runtime_pose.aim_axis),
                   30.0F * interval_response, 0.01F),
              "72/90/120 Hz response follows elapsed publication time");
        check_vector_exact(runtime_pose.grip_position, target.grip_position,
                           "runtime-rate sample preserves the right anchor");
    }
}

void test_two_hand_direction_filter_attenuates_alternating_pair_noise() {
    mod::ControllerWeaponPose raw{};
    raw.grip_position = {1.0F, 2.0F, 3.0F};
    raw.aim_axis = yaw_basis(0.0F);

    mod::TwoHandWeaponOrientationFilterState state{};
    mod::ControllerWeaponPose pose{};
    check(mod::stabilized_two_hand_controller_weapon_pose(
              100, 3'000, kIdentityTrackingAnchorOrientation,
              raw, &state, &pose),
          "persistent direction filter baseline initializes directly");

    for (std::uint64_t sample = 1; sample <= 240; ++sample) {
        raw.grip_position = {
            static_cast<float>(sample),
            -static_cast<float>(sample) * 0.25F,
            17.0F,
        };
        raw.aim_axis = yaw_basis(sample % 2 == 0 ? -0.40F : 0.40F);
        check(mod::stabilized_two_hand_controller_weapon_pose(
                  100 + sample, 3'000 + sample * 10,
                  kIdentityTrackingAnchorOrientation, raw, &state, &pose),
              "each alternating pair sample advances persistent history");
        check_vector_exact(pose.grip_position, raw.grip_position,
                           "the direction filter never filters grip position");
        if (sample >= 20) {
            check(std::abs(basis_yaw_degrees(pose.aim_axis)) <= 0.13F,
                  "persistent filtering strongly attenuates steady pair jitter");
        }
    }
}

void test_two_hand_direction_filter_attenuates_endpoint_jitter() {
    mod::ControllerWeaponPose right{};
    right.aim_axis = {};
    mod::ControllerWeaponPose left{};
    left.grip_position = {12.0F, 0.0F, 0.0F};
    left.aim_axis = {};

    mod::ControllerWeaponPose raw_pair{};
    check(mod::two_hand_controller_weapon_pose(right, left, &raw_pair),
          "endpoint-jitter baseline pair builds");
    mod::TwoHandWeaponOrientationFilterState state{};
    mod::ControllerWeaponPose filtered_pair{};
    check(mod::stabilized_two_hand_controller_weapon_pose(
              300, 6'000, kIdentityTrackingAnchorOrientation,
              raw_pair, &state, &filtered_pair),
          "endpoint-jitter direction history initializes");

    float raw_squared_sum = 0.0F;
    float filtered_squared_sum = 0.0F;
    float maximum_filtered_yaw = 0.0F;
    std::uint32_t measured_samples = 0;
    for (std::uint64_t sample = 1; sample <= 240; ++sample) {
        const float sign = sample % 2 == 0 ? -1.0F : 1.0F;
        right.grip_position = {0.0F, sign * 0.04F, 0.0F};
        left.grip_position = {12.0F, -sign * 0.04F, 0.0F};
        check(mod::two_hand_controller_weapon_pose(
                  right, left, &raw_pair),
              "anti-phase endpoint jitter produces a valid raw pair");
        check(mod::stabilized_two_hand_controller_weapon_pose(
                  300 + sample, 6'000 + sample * 14,
                  kIdentityTrackingAnchorOrientation, raw_pair,
                  &state, &filtered_pair),
              "anti-phase endpoint jitter advances direction history");
        check_vector_exact(
            filtered_pair.grip_position, right.grip_position,
            "direction filtering preserves the current right endpoint exactly");
        if (sample >= 20) {
            const float raw_yaw =
                basis_yaw_degrees(raw_pair.aim_axis);
            const float filtered_yaw =
                basis_yaw_degrees(filtered_pair.aim_axis);
            raw_squared_sum += raw_yaw * raw_yaw;
            filtered_squared_sum += filtered_yaw * filtered_yaw;
            maximum_filtered_yaw = std::max(
                maximum_filtered_yaw, std::abs(filtered_yaw));
            ++measured_samples;
        }
    }
    const float raw_rms = std::sqrt(
        raw_squared_sum / static_cast<float>(measured_samples));
    const float filtered_rms = std::sqrt(
        filtered_squared_sum / static_cast<float>(measured_samples));
    check(near(raw_rms, 0.381966F, 0.002F),
          "72 Hz endpoint fixture retains its expected raw angular jitter");
    check(filtered_rms <= raw_rms * 0.40F,
          "72 Hz direction filtering removes at least 60 percent of differential endpoint jitter");
    check(maximum_filtered_yaw <= 0.16F,
          "72 Hz filtered endpoint jitter remains below the bounded sight-motion target");
}

void test_two_hand_direction_filter_reseeds_without_regrip_snap() {
    mod::ControllerWeaponPose raw{};
    raw.grip_position = {2.0F, 3.0F, 4.0F};
    raw.aim_axis = yaw_basis(0.0F);

    mod::TwoHandWeaponOrientationFilterState committed{};
    mod::ControllerWeaponPose pose{};
    check(mod::stabilized_two_hand_controller_weapon_pose(
              600, 9'000, kIdentityTrackingAnchorOrientation,
              raw, &committed, &pose),
          "pre-release two-hand direction history initializes");
    raw.aim_axis = yaw_basis(20.0F);
    check(mod::stabilized_two_hand_controller_weapon_pose(
              601, 9'010, kIdentityTrackingAnchorOrientation,
              raw, &committed, &pose),
          "pre-release two-hand direction history becomes nontrivial");
    const float expected_history_yaw = 20.0F *
        (1.0F - std::exp2(
            -10.0F / mod::kTwoHandDirectionFilterHalfLifeMilliseconds));
    check(near(basis_yaw_degrees(committed.cached_axis),
               expected_history_yaw, 0.01F),
          "pre-release cached sight contains nontrivial filter history");
    constexpr float kRadiansToDegrees = 57.2957795131F;
    const float filtered_direction_yaw = std::atan2(
        committed.filtered_direction.y,
        committed.filtered_direction.x) * kRadiansToDegrees;
    check(near(filtered_direction_yaw, expected_history_yaw, 0.01F) &&
              filtered_direction_yaw > 0.0F &&
              filtered_direction_yaw < 20.0F,
          "pre-release direction contains the expected partial response");

    const auto before_rejected_exit = committed;
    auto staged_exit = committed;
    mod::reset_two_hand_weapon_orientation_filter(&staged_exit);
    check(!staged_exit.valid,
          "a staged accepted-exit candidate clears its private history");
    check(committed.valid == before_rejected_exit.valid &&
              committed.generation == before_rejected_exit.generation &&
              committed.publication_milliseconds ==
                  before_rejected_exit.publication_milliseconds,
          "discarding a staged exit leaves committed filter keys untouched");
    check_vector_exact(
        committed.filtered_direction,
        before_rejected_exit.filtered_direction,
        "discarding a staged exit leaves committed direction untouched");
    check_basis_exact(
        committed.cached_axis, before_rejected_exit.cached_axis,
        "discarding a staged exit leaves committed sight axis untouched");

    mod::reset_two_hand_weapon_orientation_filter(&committed);
    raw.grip_position = {-7.0F, 11.0F, 5.0F};
    raw.aim_axis = yaw_basis(-7.0F);
    const xr::Quaternionf regrip_anchor = yaw_orientation(13.0F);
    check(mod::stabilized_two_hand_controller_weapon_pose(
              700, 10'000, regrip_anchor, raw, &committed, &pose),
          "first publication after an accepted exit reseeds on the new pair");
    check_vector_exact(
        pose.grip_position, raw.grip_position,
        "regrip reseed preserves the new right endpoint exactly");
    check_basis_exact(
        pose.aim_axis, raw.aim_axis,
        "regrip reseed publishes the new sight direction without stale lag");

    const auto cached_regrip_axis = pose.aim_axis;
    raw.grip_position = {15.0F, 16.0F, 17.0F};
    raw.aim_axis = yaw_basis(40.0F);
    check(mod::stabilized_two_hand_controller_weapon_pose(
              700, 10'000, regrip_anchor, raw, &committed, &pose),
          "a repeated regrip publication reuses its immutable direction");
    check_basis_exact(
        pose.aim_axis, cached_regrip_axis,
        "repeated regrip consumers cannot introduce a transition snap");
    check_vector_exact(
        pose.grip_position, raw.grip_position,
        "repeated consumers retain the exact current right endpoint contract");
}

void test_two_hand_direction_filter_converges_on_deliberate_motion() {
    mod::ControllerWeaponPose raw{};
    raw.grip_position = {-1.0F, -2.0F, -3.0F};
    raw.aim_axis = yaw_basis(0.0F);

    mod::TwoHandWeaponOrientationFilterState state{};
    mod::ControllerWeaponPose pose{};
    check(mod::stabilized_two_hand_controller_weapon_pose(
              150, 4'000, kIdentityTrackingAnchorOrientation,
              raw, &state, &pose),
          "deliberate-motion direction filter initializes directly");

    raw.aim_axis = yaw_basis(15.0F);
    float previous_filtered_yaw = 0.0F;
    for (std::uint64_t sample = 1; sample <= 12; ++sample) {
        raw.grip_position = {
            500.0F + static_cast<float>(sample),
            -300.0F - static_cast<float>(sample),
            40.0F + static_cast<float>(sample) * 0.125F,
        };
        check(mod::stabilized_two_hand_controller_weapon_pose(
                  150 + sample, 4'000 + sample * 10,
                  kIdentityTrackingAnchorOrientation, raw, &state, &pose),
              "each deliberate-motion sample advances persistent history");
        check_vector_exact(pose.grip_position, raw.grip_position,
                           "deliberate motion preserves the right anchor exactly");
        const float filtered_yaw = basis_yaw_degrees(pose.aim_axis);
        check(filtered_yaw > previous_filtered_yaw && filtered_yaw < 15.01F,
              "deliberate motion converges monotonically without a hard bypass");
        previous_filtered_yaw = filtered_yaw;
    }
    check(near(previous_filtered_yaw, 15.0F, 0.02F),
          "deliberate motion converges on the hand line within ten half-lives");
}

void test_two_hand_direction_filter_preserves_translation_and_roll() {
    mod::ControllerWeaponPose right{};
    right.grip_position = {4.0F, -2.0F, 1.0F};
    right.aim_axis = {};
    mod::ControllerWeaponPose left{};
    left.grip_position = {16.0F, -2.0F, 1.0F};
    left.aim_axis = {};

    mod::ControllerWeaponPose raw_pair{};
    check(mod::two_hand_controller_weapon_pose(right, left, &raw_pair),
          "common-translation direction baseline pair builds");
    mod::TwoHandWeaponOrientationFilterState state{};
    mod::ControllerWeaponPose pose{};
    check(mod::stabilized_two_hand_controller_weapon_pose(
              190, 4'900, kIdentityTrackingAnchorOrientation,
              raw_pair, &state, &pose),
          "common-translation direction baseline initializes");
    const xr::Basis3f baseline_axis = pose.aim_axis;

    constexpr xr::Vec3f kTranslation{17.25F, -8.50F, 3.75F};
    right.grip_position.x += kTranslation.x;
    right.grip_position.y += kTranslation.y;
    right.grip_position.z += kTranslation.z;
    left.grip_position.x += kTranslation.x;
    left.grip_position.y += kTranslation.y;
    left.grip_position.z += kTranslation.z;
    check(mod::two_hand_controller_weapon_pose(right, left, &raw_pair),
          "commonly translated raw pair builds");
    check(mod::stabilized_two_hand_controller_weapon_pose(
              191, 4'910, kIdentityTrackingAnchorOrientation,
              raw_pair, &state, &pose),
          "commonly translated raw pair advances the direction filter");
    check_vector_exact(
        pose.grip_position, right.grip_position,
        "common translation preserves the raw right anchor exactly");
    check_basis(
        pose.aim_axis, baseline_axis,
        "common translation leaves the sight orientation unchanged");

    raw_pair.aim_axis = yaw_roll_basis(0.0F, 35.0F);
    check(mod::stabilized_two_hand_controller_weapon_pose(
              192, 4'920, kIdentityTrackingAnchorOrientation,
              raw_pair, &state, &pose),
          "a rolled dominant-hand hint advances the direction filter");
    check(near(pose.aim_axis.left.z, raw_pair.aim_axis.left.z, 0.001F) &&
              near(pose.aim_axis.up.z, raw_pair.aim_axis.up.z, 0.001F),
          "direction smoothing preserves the current dominant-hand roll");
}

void test_two_hand_direction_filter_rejects_discontinuities() {
    mod::ControllerWeaponPose raw{};
    raw.grip_position = {-3.0F, 5.0F, 9.0F};
    raw.aim_axis = yaw_basis(0.0F);

    mod::TwoHandWeaponOrientationFilterState state{};
    mod::ControllerWeaponPose pose{};
    check(mod::stabilized_two_hand_controller_weapon_pose(
              200, 5'000, kIdentityTrackingAnchorOrientation,
              raw, &state, &pose),
          "discontinuity baseline initializes directly");
    const auto established_state = state;
    const auto established_pose = pose;

    raw.grip_position = {101.0F, -202.0F, 303.0F};
    raw.aim_axis = yaw_basis(150.0F);
    check(!mod::stabilized_two_hand_controller_weapon_pose(
              201, 5'010, kIdentityTrackingAnchorOrientation,
              raw, &state, &pose),
          "an implausible adjacent direction jump fails closed");
    check(state.generation == established_state.generation &&
              state.publication_milliseconds ==
                  established_state.publication_milliseconds,
          "a rejected direction jump preserves the accepted sample key");
    check_vector_exact(state.filtered_direction,
                       established_state.filtered_direction,
                       "a rejected direction jump preserves filter history");
    check_basis_exact(state.cached_axis, established_state.cached_axis,
                      "a rejected direction jump preserves cached orientation");
    check_vector_exact(pose.grip_position, established_pose.grip_position,
                       "a rejected direction jump preserves caller position");
    check_basis_exact(pose.aim_axis, established_pose.aim_axis,
                      "a rejected direction jump preserves caller orientation");

    raw.aim_axis = yaw_basis(0.0F);
    check(!mod::stabilized_two_hand_controller_weapon_pose(
              201,
              5'000 + mod::kMaximumControllerFrameAgeMilliseconds + 1,
              kIdentityTrackingAnchorOrientation, raw, &state, &pose),
          "a stale publication interval fails closed");
    check(state.generation == established_state.generation &&
              state.publication_milliseconds ==
                  established_state.publication_milliseconds,
          "a stale publication interval preserves filter history");

    mod::reset_two_hand_weapon_orientation_filter(&state);
    check(mod::stabilized_two_hand_controller_weapon_pose(
              201, 5'010, kIdentityTrackingAnchorOrientation,
              raw, &state, &pose),
          "an explicit reset starts a fresh coherent direction history");
    check_basis(pose.aim_axis, raw.aim_axis,
                "the first sample after reset has no startup lag");
}

void test_cod4_two_hand_pose_builds_expected_basis() {
    mod::ControllerWeaponPose right{};
    right.grip_position = {2.0F, -3.0F, 4.0F};
    mod::ControllerWeaponPose left{};
    left.grip_position = {14.0F, -3.0F, 4.0F};

    mod::ControllerWeaponPose pair{};
    check(mod::two_hand_controller_weapon_pose(right, left, &pair),
          "plausible COD4 two-hand pose builds");
    check_vector(pair.grip_position, right.grip_position,
                 "two-hand pair remains anchored at the right grip");
    check_basis(pair.aim_axis, {},
                "right-up roll hint produces the canonical IW basis");
}

void test_cod4_two_hand_pose_steers_from_fixed_right_grip() {
    mod::ControllerWeaponPose right{};
    right.grip_position = {1.0F, 2.0F, 3.0F};
    mod::ControllerWeaponPose left{};
    left.grip_position = {13.0F, 2.0F, 3.0F};

    mod::ControllerWeaponPose forward_pair{};
    check(mod::two_hand_controller_weapon_pose(
              right, left, &forward_pair),
          "initial fixed-right two-hand pose builds");

    left.grip_position = {1.0F, 14.0F, 3.0F};
    mod::ControllerWeaponPose steered_pair{};
    check(mod::two_hand_controller_weapon_pose(
              right, left, &steered_pair),
          "moved left grip steers two-hand pose");
    check_vector(steered_pair.grip_position, right.grip_position,
                 "left steering cannot translate the right anchor");
    check_vector(steered_pair.aim_axis.forward, {0.0F, 1.0F, 0.0F},
                 "left grip steers barrel along the hand line");
    check_vector(steered_pair.aim_axis.left, {-1.0F, 0.0F, 0.0F},
                 "steered basis retains IW forward-left-up handedness");
    check_vector(steered_pair.aim_axis.up, {0.0F, 0.0F, 1.0F},
                 "steering preserves the projected right-hand up hint");
}

void test_cod4_live_pair_uses_stabilized_right_and_raw_left_immediately() {
    mod::ControllerWeaponPose stabilized_right{};
    stabilized_right.grip_position = {4.25F, -2.50F, 1.75F};
    stabilized_right.aim_axis = yaw_roll_basis(0.0F, 30.0F);
    mod::ControllerWeaponPose raw_left{};
    raw_left.grip_position = {16.25F, -2.50F, 1.75F};

    mod::ControllerWeaponPose pair{};
    check(mod::validated_cod4_two_hand_controller_weapon_pose(
              true, true, true, stabilized_right, raw_left, &pair),
          "COD4 live pair accepts stabilized-right plus raw-left inputs");
    check_vector_exact(
        pair.grip_position, stabilized_right.grip_position,
        "COD4 live pair keeps the stabilized right anchor bit-exact");
    check_basis(
        pair.aim_axis, stabilized_right.aim_axis,
        "COD4 live pair keeps the stabilized right roll on a collinear hand line");

    // A new raw support-hand sample is consumed directly. There is no
    // persistent pair-direction history that can delay this steering edge.
    raw_left.grip_position = {4.25F, 9.50F, 1.75F};
    check(mod::validated_cod4_two_hand_controller_weapon_pose(
              true, true, true, stabilized_right, raw_left, &pair),
          "fresh raw-left sample immediately rebuilds the COD4 live pair");
    check_vector_exact(
        pair.grip_position, stabilized_right.grip_position,
        "immediate raw-left steering cannot move the stabilized right anchor");
    check_vector(
        pair.aim_axis.forward, {0.0F, 1.0F, 0.0F},
        "fresh raw-left sample steers to its current hand line in one call");
    check_vector(
        pair.aim_axis.left, {-1.0F, 0.0F, 0.0F},
        "steered COD4 pair retains the right-hand roll projection");
    check_vector(
        pair.aim_axis.up, {0.0F, 0.0F, 1.0F},
        "steered COD4 pair reconstructs an exact orthonormal up axis");

    const mod::ControllerWeaponPose accepted = pair;
    check(!mod::validated_cod4_two_hand_controller_weapon_pose(
              true, false, true, stabilized_right, raw_left, &pair),
          "invalid stabilized support tracking fails the live pair closed");
    check_vector_exact(
        pair.grip_position, accepted.grip_position,
        "failed support validation preserves the committed right anchor");
    check_basis_exact(
        pair.aim_axis, accepted.aim_axis,
        "failed support validation preserves the committed sight direction");

    check(!mod::validated_cod4_two_hand_controller_weapon_pose(
              true, true, false, stabilized_right, raw_left, &pair),
          "an incoherent raw publication fails the live pair closed");
    check_basis_exact(
        pair.aim_axis, accepted.aim_axis,
        "raw publication failure cannot overwrite the accepted pose");
}

void test_cod4_two_hand_pose_is_radially_invariant() {
    mod::ControllerWeaponPose right{};
    right.grip_position = {-2.0F, 5.0F, 1.0F};
    mod::ControllerWeaponPose near_left{};
    near_left.grip_position = {4.0F, 13.0F, 1.0F};
    mod::ControllerWeaponPose far_left = near_left;
    far_left.grip_position = {16.0F, 29.0F, 1.0F};

    mod::ControllerWeaponPose near_pair{};
    mod::ControllerWeaponPose far_pair{};
    check(mod::two_hand_controller_weapon_pose(
              right, near_left, &near_pair) &&
              mod::two_hand_controller_weapon_pose(
                  right, far_left, &far_pair),
          "two plausible radial separations build pair poses");
    check_vector(near_pair.grip_position, far_pair.grip_position,
                 "radial support distance does not move pair origin");
    check_basis(near_pair.aim_axis, far_pair.aim_axis,
                "radial support distance does not change pair orientation");
}

void test_cod4_two_hand_pose_preserves_right_roll_hint() {
    mod::ControllerWeaponPose right{};
    right.aim_axis.forward = {1.0F, 0.0F, 0.0F};
    right.aim_axis.left = {0.0F, 0.0F, -1.0F};
    right.aim_axis.up = {0.0F, 1.0F, 0.0F};
    mod::ControllerWeaponPose left{};
    left.grip_position = {10.0F, 0.0F, 0.0F};

    mod::ControllerWeaponPose pair{};
    check(mod::two_hand_controller_weapon_pose(right, left, &pair),
          "rolled right-hand two-hand pose builds");
    check_basis(pair.aim_axis, right.aim_axis,
                "pair pose preserves right-hand roll around the hand line");

    // If right up is parallel to the hand line, COD4 derives a valid roll from
    // right left instead of publishing a degenerate basis.
    right.aim_axis = {};
    left.grip_position = {0.0F, 0.0F, 10.0F};
    check(mod::two_hand_controller_weapon_pose(right, left, &pair),
          "parallel right-up hint uses orthonormal fallback");
    check_vector(pair.aim_axis.forward, {0.0F, 0.0F, 1.0F},
                 "fallback preserves the hand-line forward");
    check_vector(pair.aim_axis.left, {0.0F, -1.0F, 0.0F},
                 "fallback reconstructs a normalized left axis");
    check_vector(pair.aim_axis.up, {1.0F, 0.0F, 0.0F},
                 "fallback reconstructs a normalized up axis");
}

void test_cod4_two_hand_pose_rejects_degenerate_input() {
    mod::ControllerWeaponPose right{};
    mod::ControllerWeaponPose left{};
    left.grip_position = {10.0F, 0.0F, 0.0F};
    mod::ControllerWeaponPose preserved{};
    preserved.grip_position = {7.0F, 8.0F, 9.0F};
    preserved.aim_axis.forward = {0.0F, 1.0F, 0.0F};
    preserved.aim_axis.left = {-1.0F, 0.0F, 0.0F};
    preserved.aim_axis.up = {0.0F, 0.0F, 1.0F};
    const mod::ControllerWeaponPose expected = preserved;

    check(!mod::two_hand_controller_weapon_pose(right, left, nullptr),
          "null pair output fails closed");

    left.grip_position = {0.1F, 0.0F, 0.0F};
    check(!mod::two_hand_controller_weapon_pose(right, left, &preserved),
          "near-coincident grips fail closed");
    check_vector(preserved.grip_position, expected.grip_position,
                 "near-coincident failure preserves output origin");
    check_basis(preserved.aim_axis, expected.aim_axis,
                "near-coincident failure preserves output basis");

    left.grip_position = {
        mod::kCod4TwoHandMaximumSeparationIwUnits + 1.0F, 0.0F, 0.0F};
    check(!mod::two_hand_controller_weapon_pose(right, left, &preserved),
          "implausibly separated grips fail closed");

    left.grip_position = {10.0F, 0.0F, 0.0F};
    left.grip_position.z = std::numeric_limits<float>::infinity();
    check(!mod::two_hand_controller_weapon_pose(right, left, &preserved),
          "non-finite grip input fails closed");

    left.grip_position = {10.0F, 0.0F, 0.0F};
    right.aim_axis.up = right.aim_axis.forward;
    right.aim_axis.left = right.aim_axis.forward;
    check(!mod::two_hand_controller_weapon_pose(right, left, &preserved),
          "invalid right roll basis fails closed");
}

void test_rigid_absolute_weapon_attachment() {
    const xr::Vec3f camera_origin{100.0F, 200.0F, 300.0F};
    const xr::Basis3f camera_axis{};
    mod::RightControllerWeaponPose controller{};
    controller.grip_position = {4.0F, 1.0F, 0.0F};
    controller.aim_axis = {};
    mod::WeaponAttachmentState attachment{};
    xr::Vec3f weapon_origin{108.0F, 202.0F, 297.0F};
    xr::Basis3f weapon_axis = camera_axis;

    check(
        mod::apply_right_controller_weapon_placement(
            camera_origin, camera_axis, controller, &attachment,
            &weapon_origin, &weapon_axis),
        "first tracked placement calibrates");
    check(attachment.valid, "attachment calibration retained");
    check_vector(attachment.position, {4.0F, 1.0F, -3.0F},
                 "stock origin captured relative to grip");
    check_vector(weapon_origin, {108.0F, 202.0F, 297.0F},
                 "identity first placement preserves stock origin");

    controller.grip_position = {6.0F, 1.0F, 0.0F};
    controller.aim_axis.forward = {0.0F, 1.0F, 0.0F};
    controller.aim_axis.left = {-1.0F, 0.0F, 0.0F};
    controller.aim_axis.up = {0.0F, 0.0F, 1.0F};
    check(
        mod::apply_right_controller_weapon_placement(
            camera_origin, camera_axis, controller, &attachment,
            &weapon_origin, &weapon_axis),
        "moved and rotated controller placement applies");
    check_vector(weapon_axis.forward, {0.0F, 1.0F, 0.0F},
                 "viewmodel forward follows controller aim");
    check_vector(weapon_origin, {105.0F, 205.0F, 297.0F},
                 "captured attachment translates and rotates rigidly");
}

void test_chest_pickup_does_not_capture_holster_angle() {
    const xr::Basis3f camera_axis{};
    mod::WeaponAttachmentState attachment{
        true,
        {8.0F, -4.0F, 2.0F},
        {
            {0.226455F, 0.566139F, 0.792594F},
            {-0.928477F, 0.371391F, 0.0F},
            {-0.294346F, -0.735865F, 0.609749F},
        },
    };
    xr::Basis3f weapon_axis = attachment.axis;

    check(mod::prepare_controller_forward_weapon_pickup(
              camera_axis, &attachment, &weapon_axis),
          "fresh chest pickup prepares controller-forward calibration");
    check(!attachment.valid,
          "fresh chest pickup discards stale owner attachment");
    check_vector(weapon_axis.forward, camera_axis.forward,
                 "fresh chest pickup discards diagonal holster forward");
    check_vector(weapon_axis.left, camera_axis.left,
                 "fresh chest pickup restores canonical weapon left");
    check_vector(weapon_axis.up, camera_axis.up,
                 "fresh chest pickup restores canonical weapon up");

    mod::ControllerWeaponPose controller{};
    controller.grip_position = {4.0F, 1.0F, 0.0F};
    controller.aim_axis.forward = {0.0F, 1.0F, 0.0F};
    controller.aim_axis.left = {-1.0F, 0.0F, 0.0F};
    controller.aim_axis.up = {0.0F, 0.0F, 1.0F};
    xr::Vec3f weapon_origin{8.0F, 2.0F, -3.0F};
    check(mod::apply_controller_weapon_placement(
              {}, camera_axis, controller, &attachment,
              &weapon_origin, &weapon_axis),
          "fresh pickup creates a controller-relative attachment");
    check_vector(weapon_origin, {8.0F, 2.0F, -3.0F},
                 "rotated-controller pickup preserves the rifle root");
    check_vector(weapon_axis.forward, controller.aim_axis.forward,
                 "fresh pickup rifle forward follows controller aim");
}

void test_exact_controller_handoff_attachment() {
    const xr::Vec3f camera_origin{100.0F, 200.0F, 300.0F};
    xr::Basis3f camera_axis{};
    camera_axis.forward = {0.0F, 1.0F, 0.0F};
    camera_axis.left = {-1.0F, 0.0F, 0.0F};
    camera_axis.up = {0.0F, 0.0F, 1.0F};

    mod::ControllerWeaponPose left{};
    left.grip_position = {6.0F, -3.0F, 2.0F};
    left.aim_axis.forward = {0.0F, 1.0F, 0.0F};
    left.aim_axis.left = {-1.0F, 0.0F, 0.0F};
    left.aim_axis.up = {0.0F, 0.0F, 1.0F};

    const xr::Vec3f right_owned_origin{112.0F, 207.0F, 296.0F};
    xr::Basis3f right_owned_axis{};
    right_owned_axis.forward = {-1.0F, 0.0F, 0.0F};
    right_owned_axis.left = {0.0F, -1.0F, 0.0F};
    right_owned_axis.up = {0.0F, 0.0F, 1.0F};

    mod::WeaponAttachmentState attachment{};
    check(mod::calibrate_controller_weapon_attachment(
              camera_origin, camera_axis, left,
              right_owned_origin, right_owned_axis, &attachment),
          "left support hand calibrates from the current right-owned pose");

    xr::Vec3f handed_origin = right_owned_origin;
    xr::Basis3f handed_axis = right_owned_axis;
    check(mod::apply_controller_weapon_placement(
              camera_origin, camera_axis, left, &attachment,
              &handed_origin, &handed_axis),
          "calibrated support-hand placement applies");
    check_vector(handed_origin, right_owned_origin,
                 "ownership handoff preserves the exact rifle origin");
    check_vector(handed_axis.forward, right_owned_axis.forward,
                 "ownership handoff preserves rifle forward");
    check_vector(handed_axis.left, right_owned_axis.left,
                 "ownership handoff preserves rifle left");
    check_vector(handed_axis.up, right_owned_axis.up,
                 "ownership handoff preserves rifle up");

    left.grip_position.x += 2.0F;
    check(mod::apply_controller_weapon_placement(
              camera_origin, camera_axis, left, &attachment,
              &handed_origin, &handed_axis),
          "support hand moves the retained rifle after handoff");
    check(!near(handed_origin.x, right_owned_origin.x) ||
              !near(handed_origin.y, right_owned_origin.y),
          "retained rifle follows later support-controller movement");

    // The handoff must remain a rigid controller-relative transform after
    // the calibration frame, not merely reproduce the pose while both axes
    // are unchanged. Rotate the support controller by +90 degrees and verify
    // that the rifle's complete offset and basis rotate by the same delta.
    left.grip_position.x -= 2.0F;
    left.aim_axis.forward = {-1.0F, 0.0F, 0.0F};
    left.aim_axis.left = {0.0F, -1.0F, 0.0F};
    left.aim_axis.up = {0.0F, 0.0F, 1.0F};
    check(mod::apply_controller_weapon_placement(
              camera_origin, camera_axis, left, &attachment,
              &handed_origin, &handed_axis),
          "support-controller rotation applies after handoff calibration");
    check_vector(handed_origin, {102.0F, 215.0F, 296.0F},
                 "support-controller rotation carries the rifle offset");
    check_vector(handed_axis.forward, {0.0F, -1.0F, 0.0F},
                 "support-controller rotation carries rifle forward");
    check_vector(handed_axis.left, {1.0F, 0.0F, 0.0F},
                 "support-controller rotation carries rifle left");
    check_vector(handed_axis.up, {0.0F, 0.0F, 1.0F},
                 "support-controller rotation carries rifle up");
}

void test_controller_attachment_survives_reference_rebase() {
    xr::Vec3f camera_origin{100.0F, 200.0F, 300.0F};
    xr::Basis3f camera_axis{};
    mod::ControllerWeaponPose controller{};
    controller.grip_position = {4.0F, 1.0F, 0.0F};
    controller.aim_axis = {};
    mod::WeaponAttachmentState attachment{};
    xr::Vec3f weapon_origin{108.0F, 202.0F, 297.0F};
    xr::Basis3f weapon_axis{};
    check(mod::apply_controller_weapon_placement(
              camera_origin, camera_axis, controller, &attachment,
              &weapon_origin, &weapon_axis),
          "controller attachment calibrates before reference rebase");

    // A recenter/body-yaw rebase changes the camera reference, not the
    // controller-to-rifle transform. The existing local attachment must be
    // applied in the new reference instead of being replaced by an identity
    // transform from T4's stock viewmodel pose.
    camera_origin = {400.0F, -50.0F, 75.0F};
    camera_axis.forward = {0.0F, 1.0F, 0.0F};
    camera_axis.left = {-1.0F, 0.0F, 0.0F};
    camera_axis.up = {0.0F, 0.0F, 1.0F};
    check(mod::apply_controller_weapon_placement(
              camera_origin, camera_axis, controller, &attachment,
              &weapon_origin, &weapon_axis),
          "controller-local attachment applies after reference rebase");
    check(attachment.valid,
          "reference rebase preserves attachment calibration");
    check_vector(attachment.position, {4.0F, 1.0F, -3.0F},
                 "reference rebase does not replace local attachment");
    check_vector(weapon_origin, {398.0F, -42.0F, 72.0F},
                 "rebased camera composes the preserved rifle offset");
    check_vector(weapon_axis.forward, camera_axis.forward,
                 "rebased camera composes preserved rifle forward");
    check_vector(weapon_axis.left, camera_axis.left,
                 "rebased camera composes preserved rifle left");
    check_vector(weapon_axis.up, camera_axis.up,
                 "rebased camera composes preserved rifle up");
}

void test_handoff_transfer_reconstructs_current_outgoing_pose() {
    const xr::Vec3f old_camera_origin{100.0F, 200.0F, 300.0F};
    const xr::Basis3f old_camera_axis{};
    mod::ControllerWeaponPose paired_left{};
    paired_left.grip_position = {6.0F, -3.0F, 2.0F};
    paired_left.aim_axis = {};

    const xr::Vec3f paired_weapon_origin{112.0F, 207.0F, 296.0F};
    xr::Basis3f paired_weapon_axis{};
    paired_weapon_axis.forward = {0.0F, 1.0F, 0.0F};
    paired_weapon_axis.left = {-1.0F, 0.0F, 0.0F};
    paired_weapon_axis.up = {0.0F, 0.0F, 1.0F};

    mod::WeaponAttachmentState outgoing_left_attachment{};
    check(mod::calibrate_controller_weapon_attachment(
              old_camera_origin, old_camera_axis, paired_left,
              paired_weapon_origin, paired_weapon_axis,
              &outgoing_left_attachment),
          "paired rifle produces an outgoing left attachment");

    // Simulate the body-yaw/tracking-reference change that invalidates the
    // retained world root during a bolt cycle. The left controller also moves
    // and rotates before the right hand rejoins.
    const xr::Vec3f camera_origin{400.0F, -50.0F, 75.0F};
    xr::Basis3f camera_axis{};
    camera_axis.forward = {0.0F, 1.0F, 0.0F};
    camera_axis.left = {-1.0F, 0.0F, 0.0F};
    camera_axis.up = {0.0F, 0.0F, 1.0F};
    mod::ControllerWeaponPose current_left{};
    current_left.grip_position = {8.0F, -1.0F, 4.0F};
    current_left.aim_axis.forward = {-1.0F, 0.0F, 0.0F};
    current_left.aim_axis.left = {0.0F, -1.0F, 0.0F};
    current_left.aim_axis.up = {0.0F, 0.0F, 1.0F};

    auto expected_attachment = outgoing_left_attachment;
    xr::Vec3f expected_origin = camera_origin;
    xr::Basis3f expected_axis = camera_axis;
    check(mod::apply_controller_weapon_placement(
              camera_origin, camera_axis, current_left,
              &expected_attachment, &expected_origin, &expected_axis),
          "outgoing left attachment reconstructs the visible rebased rifle");

    mod::ControllerWeaponPose current_right{};
    current_right.grip_position = {3.0F, 7.0F, -2.0F};
    current_right.aim_axis.forward = {0.0F, -1.0F, 0.0F};
    current_right.aim_axis.left = {1.0F, 0.0F, 0.0F};
    current_right.aim_axis.up = {0.0F, 0.0F, 1.0F};

    // Seed the incoming owner with the kind of valid-but-stale attachment
    // that previously moved the rifle backward on regrip.
    mod::WeaponAttachmentState incoming_right_attachment{};
    incoming_right_attachment.valid = true;
    incoming_right_attachment.position = {-40.0F, 2.0F, 1.0F};
    incoming_right_attachment.axis = {};
    xr::Vec3f transferred_origin{-999.0F, -999.0F, -999.0F};
    xr::Basis3f transferred_axis{};
    check(mod::transfer_controller_weapon_attachment(
              camera_origin, camera_axis, current_left,
              outgoing_left_attachment, current_right,
              &incoming_right_attachment, &transferred_origin,
              &transferred_axis),
          "left-to-right transfer replaces a stale incoming attachment");
    check_vector(transferred_origin, expected_origin,
                 "handoff preserves the current left-owned rifle origin");
    check_basis(transferred_axis, expected_axis,
                "handoff preserves the current left-owned rifle basis");
    check(!near(incoming_right_attachment.position.x, -40.0F),
          "handoff does not reuse the stale right-owner offset");

    auto applied_right_attachment = incoming_right_attachment;
    xr::Vec3f right_owned_origin{};
    xr::Basis3f right_owned_axis{};
    check(mod::apply_controller_weapon_placement(
              camera_origin, camera_axis, current_right,
              &applied_right_attachment, &right_owned_origin,
              &right_owned_axis),
          "transferred incoming attachment applies for the right owner");
    check_vector(right_owned_origin, expected_origin,
                 "right owner round-trips the outgoing visible origin");
    check_basis(right_owned_axis, expected_axis,
                "right owner round-trips the outgoing visible basis");

    // The exact handoff must remain stable after the transfer frame; this
    // catches a stale incoming filter or attachment that appears correct for
    // one frame and then settles backward on subsequent frames.
    for (int frame = 0; frame < 60; ++frame) {
        auto settled_attachment = incoming_right_attachment;
        xr::Vec3f settled_origin{};
        xr::Basis3f settled_axis{};
        check(mod::apply_controller_weapon_placement(
                  camera_origin, camera_axis, current_right,
                  &settled_attachment, &settled_origin, &settled_axis),
              "settled right owner continues applying transferred attachment");
        check_vector(settled_origin, expected_origin,
                     "stationary right owner has no post-handoff drift");
        check_basis(settled_axis, expected_axis,
                    "stationary right owner has no post-handoff rotation");
    }

    // Once continuity is established, the incoming owner must immediately
    // drive the rifle rather than leaving it attached to the outgoing hand.
    mod::ControllerWeaponPose moved_right = current_right;
    moved_right.grip_position.x += 5.0F;
    auto moved_attachment = incoming_right_attachment;
    xr::Vec3f moved_origin{};
    xr::Basis3f moved_axis{};
    check(mod::apply_controller_weapon_placement(
              camera_origin, camera_axis, moved_right, &moved_attachment,
              &moved_origin, &moved_axis),
          "right movement applies immediately after handoff");
    check_vector(
        moved_origin,
        {expected_origin.x + camera_axis.forward.x * 5.0F,
         expected_origin.y + camera_axis.forward.y * 5.0F,
         expected_origin.z + camera_axis.forward.z * 5.0F},
        "transferred rifle follows the new right-owner translation");
    check_basis(moved_axis, expected_axis,
                "pure right-owner translation preserves rifle rotation");

    const mod::WeaponAttachmentState preserved_attachment =
        incoming_right_attachment;
    const xr::Vec3f preserved_origin = transferred_origin;
    const xr::Basis3f preserved_axis = transferred_axis;
    check(!mod::transfer_controller_weapon_attachment(
              camera_origin, camera_axis, current_left, {}, current_right,
              &incoming_right_attachment, &transferred_origin,
              &transferred_axis),
          "invalid outgoing attachment fails closed");
    check(incoming_right_attachment.valid == preserved_attachment.valid,
          "failed transfer preserves incoming validity");
    check_vector(incoming_right_attachment.position,
                 preserved_attachment.position,
                 "failed transfer preserves incoming position");
    check_basis(incoming_right_attachment.axis, preserved_attachment.axis,
                "failed transfer preserves incoming basis");
    check_vector(transferred_origin, preserved_origin,
                 "failed transfer preserves output origin");
    check_basis(transferred_axis, preserved_axis,
                "failed transfer preserves output basis");
}

void test_controller_forward_alignment_removes_transferred_axis_offset() {
    const xr::Vec3f camera_origin{100.0F, 200.0F, 300.0F};
    const xr::Basis3f camera_axis{};

    mod::ControllerWeaponPose outgoing_controller{};
    outgoing_controller.grip_position = {3.0F, -2.0F, 1.0F};
    outgoing_controller.aim_axis = {};
    const xr::Vec3f outgoing_origin{112.0F, 207.0F, 296.0F};
    const xr::Basis3f outgoing_axis = yaw_basis(18.0F);
    mod::WeaponAttachmentState outgoing_attachment{};
    check(mod::calibrate_controller_weapon_attachment(
              camera_origin, camera_axis, outgoing_controller,
              outgoing_origin, outgoing_axis, &outgoing_attachment),
          "offset rifle calibrates before pair ownership transfer");

    mod::ControllerWeaponPose right{};
    right.grip_position = {4.0F, 1.0F, -2.0F};
    right.aim_axis = {};
    mod::ControllerWeaponPose left{};
    left.grip_position = {16.0F, 1.0F, -2.0F};
    left.aim_axis = {};
    mod::ControllerWeaponPose pair{};
    check(mod::two_hand_controller_weapon_pose(right, left, &pair),
          "straight pair pose is available for transferred attachment");

    mod::WeaponAttachmentState pair_attachment{};
    xr::Vec3f transferred_origin{};
    xr::Basis3f transferred_axis{};
    check(mod::transfer_controller_weapon_attachment(
              camera_origin, camera_axis, outgoing_controller,
              outgoing_attachment, pair, &pair_attachment,
              &transferred_origin, &transferred_axis),
          "offset rifle transfers into the pair controller");
    check(near(basis_yaw_degrees(transferred_axis), 18.0F),
          "transfer retains the eighteen-degree rifle mismatch");

    const xr::Vec3f preserved_position = pair_attachment.position;
    const xr::Vec3f preserved_origin = transferred_origin;
    check(mod::align_controller_weapon_attachment_to_controller_forward(
              camera_origin, camera_axis, pair, transferred_origin,
              &pair_attachment),
          "pair attachment aligns to the controller-forward basis");
    check_vector_exact(pair_attachment.position, preserved_position,
                       "alignment preserves transferred attachment position");
    check_vector_exact(transferred_origin, preserved_origin,
                       "alignment never mutates the transferred rifle root");

    xr::Vec3f aligned_origin = transferred_origin;
    xr::Basis3f aligned_axis = transferred_axis;
    check(mod::apply_controller_weapon_placement(
              camera_origin, camera_axis, pair, &pair_attachment,
              &aligned_origin, &aligned_axis),
          "controller-forward pair attachment applies");
    check_vector_exact(aligned_origin, preserved_origin,
                       "axis correction leaves the visible rifle root exact");
    check_basis(aligned_axis, pair.aim_axis,
                "axis correction makes rifle and pair forward collinear");

    left.grip_position = {14.392304F, 7.0F, -2.0F};
    mod::ControllerWeaponPose steered_pair{};
    check(mod::two_hand_controller_weapon_pose(right, left, &steered_pair),
          "steered pair pose remains qualified");
    xr::Vec3f steered_origin = aligned_origin;
    xr::Basis3f steered_axis = aligned_axis;
    check(mod::apply_controller_weapon_placement(
              camera_origin, camera_axis, steered_pair, &pair_attachment,
              &steered_origin, &steered_axis),
          "aligned attachment follows subsequent pair steering");
    check_basis(steered_axis, steered_pair.aim_axis,
                "subsequent pair steering remains controller-forward");
}

void test_controller_forward_alignment_initializes_position_once() {
    const xr::Vec3f camera_origin{100.0F, 200.0F, 300.0F};
    xr::Basis3f camera_axis = yaw_basis(25.0F);
    mod::ControllerWeaponPose controller{};
    controller.grip_position = {4.0F, -2.0F, 1.0F};
    controller.aim_axis = yaw_basis(-15.0F);
    const xr::Vec3f first_origin{108.0F, 202.0F, 297.0F};

    mod::WeaponAttachmentState attachment{};
    check(mod::align_controller_weapon_attachment_to_controller_forward(
              camera_origin, camera_axis, controller, first_origin,
              &attachment),
          "fresh controller-forward attachment captures root position");
    check(attachment.valid,
          "fresh controller-forward attachment becomes valid");
    check_basis(attachment.axis, {},
                "fresh controller-forward attachment uses identity axis");
    const xr::Vec3f initialized_position = attachment.position;

    xr::Vec3f applied_origin = first_origin;
    xr::Basis3f applied_axis = yaw_basis(70.0F);
    check(mod::apply_controller_weapon_placement(
              camera_origin, camera_axis, controller, &attachment,
              &applied_origin, &applied_axis),
          "fresh controller-forward attachment applies");
    check_vector(applied_origin, first_origin,
                 "fresh attachment preserves its first visible root");

    const xr::Vec3f later_seed{-50.0F, 75.0F, 12.0F};
    check(mod::align_controller_weapon_attachment_to_controller_forward(
              camera_origin, camera_axis, controller, later_seed,
              &attachment),
          "valid controller-forward attachment can be realigned");
    check_vector_exact(attachment.position, initialized_position,
                       "valid attachment position initializes only once");

    const mod::WeaponAttachmentState preserved_attachment = attachment;
    const xr::Vec3f nonfinite_origin{
        std::numeric_limits<float>::infinity(), 0.0F, 0.0F};
    check(!mod::align_controller_weapon_attachment_to_controller_forward(
              camera_origin, camera_axis, controller, nonfinite_origin,
              &attachment),
          "non-finite root rejects controller-forward alignment");
    check(attachment.valid == preserved_attachment.valid,
          "failed alignment preserves attachment validity");
    check_vector_exact(attachment.position, preserved_attachment.position,
                       "failed alignment preserves attachment position");
    check_basis(attachment.axis, preserved_attachment.axis,
                "failed alignment preserves attachment axis");
}

void test_post_pose_grip_tag_alignment() {
    const xr::Vec3f camera_origin{100.0F, 200.0F, 300.0F};
    xr::Basis3f camera_axis{};
    camera_axis.forward = {0.0F, 1.0F, 0.0F};
    camera_axis.left = {-1.0F, 0.0F, 0.0F};
    camera_axis.up = {0.0F, 0.0F, 1.0F};
    mod::RightControllerWeaponPose controller{};
    controller.grip_position = {10.0F, 2.0F, -3.0F};

    xr::Vec3f tracked_grip_world{};
    check(
        mod::right_controller_grip_world(
            camera_origin, camera_axis, controller, &tracked_grip_world),
        "controller grip converts to stock-refdef world space");
    check_vector(
        tracked_grip_world, {98.0F, 210.0F, 297.0F},
        "camera basis composes tracked grip position");

    xr::Vec3f viewmodel_origin{90.0F, 190.0F, 290.0F};
    const xr::Vec3f current_tag_world{94.0F, 205.0F, 295.0F};
    check(
        mod::align_viewmodel_origin_to_grip(
            tracked_grip_world, current_tag_world, &viewmodel_origin),
        "finite per-model grip-tag correction applies");
    check_vector(
        viewmodel_origin, {94.0F, 195.0F, 292.0F},
        "viewmodel root moves by tracked grip minus animated grip tag");

    xr::Vec3f preserved = viewmodel_origin;
    check(
        !mod::align_viewmodel_origin_to_grip(
            {10'000.0F, 0.0F, 0.0F}, {}, &preserved),
        "implausible tag correction fails closed");
    check_vector(
        preserved, viewmodel_origin,
        "failed grip correction preserves viewmodel origin");
}

void test_evaluated_viewmodel_skeleton_translation() {
    const xr::Vec3f original_root{90.0F, 190.0F, 290.0F};
    const xr::Vec3f corrected_root{94.0F, 195.0F, 292.0F};
    xr::Vec3f pose_origin{89.5F, 191.0F, 287.0F};
    std::array<mod::EvaluatedViewmodelBoneTransform, 4> transforms{};
    transforms[0].translation = {1.0F, 2.0F, 3.0F};
    transforms[0].quaternion[3] = 1.0F;
    transforms[0].translation_weight = 0.25F;
    transforms[1].translation = {10.0F, 20.0F, 30.0F};
    transforms[2].translation = {-1.0F, -2.0F, -3.0F};
    transforms[2].quaternion[0] = 0.5F;
    transforms[2].translation_weight = 0.75F;
    transforms[3].translation = {40.0F, 50.0F, 60.0F};
    const auto original_transforms = transforms;
    const std::array<std::uint32_t,
                     mod::kViewmodelSkeletonBitWordCount>
        evaluated_bits{0xA0000000U, 0, 0, 0};

    check(
        mod::translate_evaluated_viewmodel_skeleton(
            original_root, corrected_root, evaluated_bits,
            transforms, &pose_origin),
        "finite evaluated skeleton translation applies without a pose rebuild");
    check_vector(
        pose_origin, {93.5F, 196.0F, 289.0F},
        "evaluated pose origin receives the exact viewmodel-root delta");
    check_vector(
        transforms[0].translation, {5.0F, 7.0F, 5.0F},
        "MSB bone zero receives the exact root delta");
    check_vector(
        transforms[2].translation, {3.0F, 3.0F, -1.0F},
        "MSB-first bone two receives the exact root delta");
    check_vector(
        transforms[1].translation, original_transforms[1].translation,
        "unevaluated bone one remains untouched");
    check_vector(
        transforms[3].translation, original_transforms[3].translation,
        "unevaluated bone three remains untouched");
    check(
        near(transforms[0].quaternion[3], 1.0F) &&
            near(transforms[0].translation_weight, 0.25F) &&
            near(transforms[2].quaternion[0], 0.5F) &&
            near(transforms[2].translation_weight, 0.75F),
        "translation preserves rotation and translation weight");

    const xr::Vec3f preserved_pose = pose_origin;
    const auto preserved_transforms = transforms;
    auto out_of_range_bits = evaluated_bits;
    out_of_range_bits[0] |= 0x08000000U;
    check(
        !mod::translate_evaluated_viewmodel_skeleton(
            corrected_root, corrected_root, out_of_range_bits,
            transforms, &pose_origin),
        "evaluated bit outside the matrix range fails closed");
    check_vector(
        pose_origin, preserved_pose,
        "out-of-range bit preserves pose origin");
    check_vector(
        transforms[0].translation, preserved_transforms[0].translation,
        "out-of-range bit preserves matrices");

    transforms[0].translation.x = std::numeric_limits<float>::infinity();
    const xr::Vec3f nonfinite_pose = pose_origin;
    check(
        !mod::translate_evaluated_viewmodel_skeleton(
            corrected_root, corrected_root, evaluated_bits,
            transforms, &pose_origin),
        "non-finite selected matrix fails closed");
    check_vector(
        pose_origin, nonfinite_pose,
        "non-finite selected matrix preserves pose origin");

    transforms = preserved_transforms;
    const auto oversized_transforms = transforms;
    check(
        !mod::translate_evaluated_viewmodel_skeleton(
            corrected_root, {1'000.0F, 195.0F, 292.0F},
            evaluated_bits, transforms, &pose_origin),
        "implausible evaluated root translation fails closed");
    check_vector(
        transforms[2].translation, oversized_transforms[2].translation,
        "implausible evaluated translation preserves matrices");

    std::array<mod::EvaluatedViewmodelBoneTransform, 34> cross_word{};
    cross_word[33].translation = {7.0F, 8.0F, 9.0F};
    std::array<std::uint32_t, mod::kViewmodelSkeletonBitWordCount>
        cross_word_bits{};
    cross_word_bits[1] = 0x40000000U;
    xr::Vec3f cross_word_pose{};
    check(
        mod::translate_evaluated_viewmodel_skeleton(
            {}, {1.0F, 2.0F, 3.0F}, cross_word_bits,
            cross_word, &cross_word_pose),
        "MSB-first skeleton bit ordering extends across words");
    check_vector(
        cross_word[33].translation, {8.0F, 10.0F, 12.0F},
        "second-word bone thirty-three receives the root delta");
}

void test_translated_viewmodel_tag_match_tolerates_float_reassociation() {
    // The native tag path adds view offset after reading the evaluated bone,
    // while the expected path adds the correction after forming world space.
    // At large map coordinates those equivalent float32 expressions can land
    // one ULP apart and must not trigger the expensive pose-rebuild fallback.
    volatile float matrix_component = -198.419342F;
    volatile float view_offset_component = 5222.984375F;
    volatile float correction_component = -0.001263948F;
    const float original_world = matrix_component + view_offset_component;
    const float expected_component = original_world + correction_component;
    const float translated_matrix = matrix_component + correction_component;
    const float observed_component = translated_matrix + view_offset_component;

    check(
        expected_component == 5024.5634765625F &&
            observed_component == 5024.56396484375F &&
            expected_component != observed_component,
        "float32 regression fixture retains the native reassociation gap");
    check(
        mod::translated_viewmodel_tag_matches(
            {expected_component, -17.0F, 0.25F},
            {observed_component, -17.0F, 0.25F}),
        "equivalent native tag results one ULP apart are accepted");

    float beyond_tolerance = expected_component;
    for (int step = 0; step < 3; ++step) {
        beyond_tolerance = std::nextafter(
            beyond_tolerance, std::numeric_limits<float>::infinity());
    }
    check(
        !mod::translated_viewmodel_tag_matches(
            {expected_component, -17.0F, 0.25F},
            {beyond_tolerance, -17.0F, 0.25F}),
        "native tag result beyond two ULPs is rejected");
    check(
        !mod::translated_viewmodel_tag_matches(
            {expected_component, -17.0F, 0.25F},
            {expected_component + 0.1F, -17.0F, 0.25F}),
        "visibly stale native tag result is rejected");
    check(
        !mod::translated_viewmodel_tag_matches(
            {expected_component, -17.0F, 0.25F},
            {std::numeric_limits<float>::infinity(), -17.0F, 0.25F}),
        "non-finite native tag result is rejected");
}

void test_cod4_two_hand_engagement_blend_endpoints() {
    mod::ControllerWeaponPose right{};
    right.grip_position = {2.0F, 3.0F, 4.0F};
    right.aim_axis = yaw_roll_basis(0.0F, 20.0F);
    mod::ControllerWeaponPose left{};
    left.grip_position = {2.0F, 13.0F, 4.0F};
    left.aim_axis = yaw_basis(-45.0F);

    mod::ControllerWeaponPose pair_target{};
    check(mod::two_hand_controller_weapon_pose_from_raw_grip_delta(
              right, right, left, &pair_target),
          "COD4 pair target builds from the same-frame raw hand delta");

    mod::ControllerWeaponPose pose{};
    check(mod::cod4_two_hand_blended_controller_weapon_pose(
              right, right, left, 0.0F, &pose),
          "zero COD4 engagement blend produces a pose");
    check_vector_exact(pose.grip_position, right.grip_position,
                       "zero blend preserves exact stabilized right anchor");
    check_basis_exact(pose.aim_axis, right.aim_axis,
                      "zero blend preserves exact stabilized right basis");

    check(mod::cod4_two_hand_blended_controller_weapon_pose(
              right, right, left, 1.0F, &pose),
          "full COD4 engagement blend produces a pose");
    check_vector_exact(pose.grip_position, right.grip_position,
                       "full blend preserves exact stabilized right anchor");
    check_basis_exact(pose.aim_axis, pair_target.aim_axis,
                      "full blend reaches the unfiltered COD4 hand line");

    check(mod::cod4_two_hand_blended_controller_weapon_pose(
              right, right, left, 0.5F, &pose),
          "partial COD4 engagement blend produces a pose");
    check(near(basis_yaw_degrees(pose.aim_axis), 45.0F, 0.01F),
          "partial blend interpolates right forward toward the hand line");
    check_vector_exact(pose.grip_position, right.grip_position,
                       "partial blend never filters the right anchor");
}

void test_adaptive_weapon_response_damps_quiet_motion_and_releases_fast() {
    check(near(mod::adaptive_weapon_position_response(0.0F),
               mod::kQuietWeaponPositionResponse),
          "stationary position uses the quiet response");
    check(near(mod::adaptive_weapon_orientation_response(0.0F),
               mod::kQuietWeaponOrientationResponse),
          "stationary orientation uses the quiet response");
    const float middle_position = mod::adaptive_weapon_position_response(
        (mod::kQuietWeaponPositionErrorMeters +
         mod::kFastWeaponPositionErrorMeters) * 0.5F);
    const float middle_orientation = mod::adaptive_weapon_orientation_response(
        (mod::kQuietWeaponOrientationErrorDegrees +
         mod::kFastWeaponOrientationErrorDegrees) * 0.5F);
    check(middle_position > mod::kQuietWeaponPositionResponse &&
              middle_position < mod::kWeaponPositionResponse,
          "position response ramps continuously between quiet and fast");
    check(middle_orientation > mod::kQuietWeaponOrientationResponse &&
              middle_orientation < mod::kWeaponOrientationResponse,
          "orientation response ramps continuously between quiet and fast");
    check(near(mod::adaptive_weapon_position_response(
                   mod::kFastWeaponPositionErrorMeters),
               mod::kWeaponPositionResponse) &&
              near(mod::adaptive_weapon_position_response(0.50F),
                   mod::kWeaponPositionResponse),
          "deliberate position movement restores the exact COD4 response");
    check(near(mod::adaptive_weapon_orientation_response(
                   mod::kFastWeaponOrientationErrorDegrees),
               mod::kWeaponOrientationResponse) &&
              near(mod::adaptive_weapon_orientation_response(120.0F),
                   mod::kWeaponOrientationResponse),
          "deliberate orientation movement restores the exact COD4 response");
}

void test_two_hand_engagement_uses_filtered_endpoints() {
    mod::ControllerWeaponPose stabilized_right{};
    stabilized_right.grip_position = {2.0F, 3.0F, 4.0F};
    stabilized_right.aim_axis = yaw_roll_basis(0.0F, 20.0F);

    mod::ControllerWeaponPose raw_right{};
    raw_right.grip_position = {3.0F, 3.0F, 4.0F};
    raw_right.aim_axis = stabilized_right.aim_axis;
    mod::ControllerWeaponPose filtered_left{};
    filtered_left.grip_position = {2.0F, 13.0F, 4.0F};
    filtered_left.aim_axis = yaw_basis(-45.0F);

    mod::ControllerWeaponPose baseline{};
    check(mod::cod4_two_hand_blended_controller_weapon_pose(
              stabilized_right, raw_right, filtered_left, 1.0F, &baseline),
          "full blend accepts filtered right and filtered left hands");
    check_vector_exact(
        baseline.grip_position, stabilized_right.grip_position,
        "same-frame raw pair keeps the stabilized dominant anchor");
    check(near(basis_yaw_degrees(baseline.aim_axis), 90.0F, 0.01F),
          "two-hand line points between filtered endpoints");

    raw_right.grip_position = {25.0F, -20.0F, 14.0F};

    mod::ControllerWeaponPose changed_raw_right{};
    check(mod::cod4_two_hand_blended_controller_weapon_pose(
              stabilized_right, raw_right, filtered_left, 1.0F,
              &changed_raw_right),
          "raw right remains a validity gate without steering");
    check_vector_exact(
        changed_raw_right.grip_position, baseline.grip_position,
        "raw right motion cannot move the filtered dominant anchor");
    check_basis_exact(
        changed_raw_right.aim_axis, baseline.aim_axis,
        "raw right motion cannot steer the COD4 hand line");

    mod::Cod4TwoHandEngagementBlendState state{};
    state.valid = true;
    state.generation = 50;
    state.tracking_anchor_orientation =
        kIdentityTrackingAnchorOrientation;
    state.blend = 1.0F;
    state.cached_pose = baseline;
    mod::ControllerWeaponPose advanced{};
    check(mod::update_cod4_two_hand_engagement_pose(
              51, true, kIdentityTrackingAnchorOrientation,
              stabilized_right, raw_right, filtered_left, &state, &advanced),
          "live engagement consumes both filtered endpoints");
    check_vector_exact(
        advanced.grip_position, baseline.grip_position,
        "live engagement keeps the stabilized dominant anchor");
    check_basis_exact(
        advanced.aim_axis, baseline.aim_axis,
        "live engagement ignores raw-right steering noise");

    filtered_left.grip_position = {15.0F, 3.0F, 4.0F};
    check(mod::update_cod4_two_hand_engagement_pose(
              52, true, kIdentityTrackingAnchorOrientation,
              stabilized_right, raw_right, filtered_left, &state, &advanced),
          "filtered support-hand motion advances the live pair");
    check_vector(
        advanced.aim_axis.forward, {1.0F, 0.0F, 0.0F},
        "filtered support hand steers the stabilized pair");
}

void test_cod4_two_hand_engagement_sequence_and_generation_cache() {
    mod::ControllerWeaponPose right{};
    right.grip_position = {1.0F, -2.0F, 3.0F};
    right.aim_axis = yaw_basis(0.0F);
    mod::ControllerWeaponPose left{};
    left.grip_position = {1.0F, 8.0F, 3.0F};
    left.aim_axis = yaw_basis(0.0F);

    mod::Cod4TwoHandEngagementBlendState state{};
    mod::ControllerWeaponPose first{};
    check(mod::update_cod4_two_hand_engagement_pose(
              100, true, kIdentityTrackingAnchorOrientation,
              right, right, left, &state, &first),
          "first active COD4 publication engages the blend");
    check(near(state.blend, 0.22F, 0.000001F),
          "first engage step uses COD4's 0.22 response");
    check_vector_exact(first.grip_position, right.grip_position,
                       "first engage step preserves exact right anchor");

    mod::ControllerWeaponPose second{};
    check(mod::update_cod4_two_hand_engagement_pose(
              101, true, kIdentityTrackingAnchorOrientation,
              right, right, left, &state, &second),
          "second active COD4 publication advances the blend");
    check(near(state.blend, 0.3916F, 0.000001F),
          "second engage step follows blend += remaining * 0.22");
    const auto cached_state = state;
    const auto cached_pose = second;

    auto changed_right = right;
    changed_right.grip_position = {99.0F, 88.0F, 77.0F};
    changed_right.aim_axis = yaw_basis(-35.0F);
    auto changed_left = left;
    changed_left.grip_position = {
        std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
    mod::ControllerWeaponPose repeated{};
    check(mod::update_cod4_two_hand_engagement_pose(
              101, false, kIdentityTrackingAnchorOrientation,
              changed_right, changed_right, changed_left, &state, &repeated),
          "repeated generation returns the immutable cached result");
    check(state.blend == cached_state.blend &&
              state.generation == cached_state.generation,
          "repeated generation does not advance engagement history");
    check_vector_exact(repeated.grip_position, cached_pose.grip_position,
                       "repeated generation keeps cached right anchor bits");
    check_basis_exact(repeated.aim_axis, cached_pose.aim_axis,
                      "repeated generation keeps cached orientation bits");

    mod::ControllerWeaponPose released{};
    check(mod::update_cod4_two_hand_engagement_pose(
              102, false, kIdentityTrackingAnchorOrientation,
              right, right, left, &state, &released),
          "inactive COD4 publication releases the blend");
    const float released_blend = 0.3916F * 0.82F;
    check(near(state.blend, released_blend, 0.000001F),
          "release step uses COD4's 0.18 response");

    mod::ControllerWeaponPose reengaged{};
    check(mod::update_cod4_two_hand_engagement_pose(
              103, true, kIdentityTrackingAnchorOrientation,
              right, right, left, &state, &reengaged),
          "regrip reverses the existing release blend");
    const float expected_reengaged =
        released_blend + (1.0F - released_blend) * 0.22F;
    check(near(state.blend, expected_reengaged, 0.000001F),
          "regrip continues from retained blend instead of reseeding");
}

void test_cod4_two_hand_engagement_skipped_generation_cadence() {
    mod::ControllerWeaponPose right{};
    right.grip_position = {1.0F, 2.0F, 3.0F};
    right.aim_axis = yaw_basis(0.0F);
    mod::ControllerWeaponPose left{};
    left.grip_position = {1.0F, 12.0F, 3.0F};
    left.aim_axis = yaw_basis(0.0F);

    mod::Cod4TwoHandEngagementBlendState state{};
    mod::ControllerWeaponPose pose{};
    check(mod::update_cod4_two_hand_engagement_pose(
              10, true, kIdentityTrackingAnchorOrientation,
              right, right, left, &state, &pose),
          "initial COD4 cadence publication succeeds");
    check(mod::update_cod4_two_hand_engagement_pose(
              14, true, kIdentityTrackingAnchorOrientation,
              right, right, left, &state, &pose),
          "skipped COD4 engage publications succeed");
    const float expected_engage = 1.0F - std::pow(0.78F, 5.0F);
    check(near(state.blend, expected_engage, 0.000001F),
          "generation gap applies one engage response per publication");

    check(mod::update_cod4_two_hand_engagement_pose(
              17, false, kIdentityTrackingAnchorOrientation,
              right, right, left, &state, &pose),
          "skipped COD4 release publications succeed");
    const float expected_release = expected_engage * std::pow(0.82F, 3.0F);
    check(near(state.blend, expected_release, 0.000001F),
          "generation gap applies one release response per publication");
}

void test_cod4_two_hand_engagement_anchor_cache_key() {
    mod::ControllerWeaponPose right{};
    right.grip_position = {2.0F, 3.0F, 4.0F};
    right.aim_axis = yaw_basis(0.0F);
    mod::ControllerWeaponPose left{};
    left.grip_position = {2.0F, 13.0F, 4.0F};
    left.aim_axis = yaw_basis(0.0F);

    mod::Cod4TwoHandEngagementBlendState state{};
    mod::ControllerWeaponPose initial{};
    check(mod::update_cod4_two_hand_engagement_pose(
              400, true, kIdentityTrackingAnchorOrientation,
              right, right, left, &state, &initial),
          "anchor cache initializes from a valid publication");
    const float accepted_blend = state.blend;

    auto rebased_right = right;
    rebased_right.grip_position = {-5.0F, 6.0F, 7.0F};
    rebased_right.aim_axis = yaw_basis(20.0F);
    auto rebased_left = left;
    rebased_left.grip_position = {3.660254F, 11.0F, 7.0F};
    const xr::Quaternionf rebased_anchor = yaw_orientation(15.0F);
    mod::ControllerWeaponPose expected{};
    check(mod::cod4_two_hand_blended_controller_weapon_pose(
              rebased_right, rebased_right, rebased_left, accepted_blend,
              &expected),
          "expected anchor-rebased pose is valid");
    mod::ControllerWeaponPose rebased{};
    check(mod::update_cod4_two_hand_engagement_pose(
              400, false, rebased_anchor, rebased_right, rebased_right,
              rebased_left, &state, &rebased),
          "same-generation anchor rebase refreshes the cached pose");
    check(state.blend == accepted_blend && state.generation == 400,
          "anchor-only recompute does not advance engagement response");
    check_vector_exact(rebased.grip_position, expected.grip_position,
                       "anchor-only recompute uses current right anchor");
    check_basis_exact(rebased.aim_axis, expected.aim_axis,
                      "anchor-only recompute uses current hand line");
    check_quaternion_exact(state.tracking_anchor_orientation, rebased_anchor,
                           "anchor-only recompute stores normalized anchor");

    xr::Quaternionf equivalent_anchor{
        -rebased_anchor.x, -rebased_anchor.y, -rebased_anchor.z,
        -rebased_anchor.w};
    auto invalid_right = rebased_right;
    invalid_right.grip_position.x =
        std::numeric_limits<float>::quiet_NaN();
    mod::ControllerWeaponPose equivalent{};
    check(mod::update_cod4_two_hand_engagement_pose(
              400, true, equivalent_anchor, invalid_right, invalid_right,
              rebased_left, &state, &equivalent),
          "quaternion sign-equivalent anchor returns cached pose");
    check_basis_exact(equivalent.aim_axis, rebased.aim_axis,
                      "q and negative q share one immutable cache entry");

    const auto accepted_state = state;
    const auto accepted_pose = equivalent;
    xr::Quaternionf invalid_anchor{};
    invalid_anchor.w = std::numeric_limits<float>::quiet_NaN();
    mod::ControllerWeaponPose untouched = accepted_pose;
    check(!mod::update_cod4_two_hand_engagement_pose(
              401, true, invalid_anchor, right, right, left, &state,
              &untouched),
          "invalid tracking anchor fails closed");
    check(state.generation == accepted_state.generation &&
              state.blend == accepted_state.blend,
          "invalid anchor preserves engagement state");
    check_vector_exact(untouched.grip_position, accepted_pose.grip_position,
                       "invalid anchor preserves caller output");
    check_basis_exact(untouched.aim_axis, accepted_pose.aim_axis,
                      "invalid anchor preserves caller output basis");
}

void test_cod4_two_hand_engagement_clamps_and_reset() {
    mod::ControllerWeaponPose right{};
    right.grip_position = {0.0F, 0.0F, 0.0F};
    right.aim_axis = yaw_basis(0.0F);
    mod::ControllerWeaponPose left{};
    left.grip_position = {10.0F, 0.0F, 0.0F};
    left.aim_axis = yaw_basis(0.0F);

    mod::Cod4TwoHandEngagementBlendState release_state{};
    release_state.valid = true;
    release_state.generation = 200;
    release_state.tracking_anchor_orientation =
        kIdentityTrackingAnchorOrientation;
    release_state.blend = 0.0011F;
    release_state.cached_pose = right;
    mod::ControllerWeaponPose pose{};
    check(mod::update_cod4_two_hand_engagement_pose(
              201, false, kIdentityTrackingAnchorOrientation,
              right, right, left, &release_state, &pose),
          "near-zero COD4 release step succeeds");
    check(release_state.blend == 0.0F,
          "blend below the exact 0.001 threshold snaps to zero");
    check_basis_exact(pose.aim_axis, right.aim_axis,
                      "snapped zero blend restores exact right basis");

    mod::Cod4TwoHandEngagementBlendState engage_state{};
    engage_state.valid = true;
    engage_state.generation = 300;
    engage_state.tracking_anchor_orientation =
        kIdentityTrackingAnchorOrientation;
    engage_state.blend = 0.999F;
    engage_state.cached_pose = right;
    check(mod::update_cod4_two_hand_engagement_pose(
              301, true, kIdentityTrackingAnchorOrientation,
              right, right, left, &engage_state, &pose),
          "near-one COD4 engage step succeeds");
    check(engage_state.blend == 1.0F,
          "blend above the exact 0.999 threshold snaps to one");

    const auto accepted_state = engage_state;
    const auto accepted_pose = pose;
    auto invalid_right = right;
    invalid_right.aim_axis.forward.x =
        std::numeric_limits<float>::quiet_NaN();
    check(!mod::update_cod4_two_hand_engagement_pose(
              302, true, kIdentityTrackingAnchorOrientation,
              invalid_right, invalid_right, left, &engage_state, &pose),
          "invalid dominant-hand pose fails closed");
    check(engage_state.generation == accepted_state.generation &&
              engage_state.blend == accepted_state.blend,
          "invalid pose preserves accepted engagement state");
    check_vector_exact(pose.grip_position, accepted_pose.grip_position,
                       "invalid pose preserves caller output position");
    check_basis_exact(pose.aim_axis, accepted_pose.aim_axis,
                      "invalid pose preserves caller output orientation");
    auto invalid_left = left;
    invalid_left.grip_position.x =
        std::numeric_limits<float>::infinity();
    check(!mod::update_cod4_two_hand_engagement_pose(
              302, true, kIdentityTrackingAnchorOrientation,
              right, right, invalid_left, &engage_state, &pose),
          "invalid raw support-hand pose fails closed");
    check(engage_state.generation == accepted_state.generation &&
              engage_state.blend == accepted_state.blend,
          "invalid support pose preserves accepted engagement state");
    check(!mod::update_cod4_two_hand_engagement_pose(
              299, true, kIdentityTrackingAnchorOrientation,
              right, right, left, &engage_state, &pose),
          "regressed generation fails closed");
    check(!mod::update_cod4_two_hand_engagement_pose(
              0, true, kIdentityTrackingAnchorOrientation,
              right, right, left, &engage_state, &pose),
          "zero generation fails closed");

    mod::reset_cod4_two_hand_engagement_blend(&engage_state);
    check(!engage_state.valid && engage_state.generation == 0 &&
              engage_state.blend == 0.0F,
          "explicit engagement reset clears all history");
}

void test_right_ray_two_hand_constraint_latches_without_a_pose_shift() {
    const xr::Basis3f identity = yaw_basis(0.0F);
    mod::ControllerWeaponPose stable_right{
        {4.0F, 5.0F, 6.0F}, identity};
    mod::ControllerWeaponPose raw_right{{0.0F, 0.0F, 0.0F}, identity};
    mod::ControllerWeaponPose raw_left{{20.0F, 0.0F, 0.0F}, identity};
    mod::RightRayTwoHandSteeringState state{};
    mod::ControllerWeaponPose pose{};
    const auto set_support_angle = [&raw_right, &raw_left](
        const float degrees) noexcept {
        constexpr float kDegreesToRadians = 0.0174532925199F;
        const float radians = degrees * kDegreesToRadians;
        raw_left.grip_position = {
            raw_right.grip_position.x + 20.0F * std::cos(radians),
            raw_right.grip_position.y + 20.0F * std::sin(radians),
            raw_right.grip_position.z,
        };
    };
    std::uint64_t generation = 1;
    std::uint64_t publication_milliseconds = 1'000;

    check(mod::update_right_ray_two_hand_steering_pose(
              generation, publication_milliseconds,
              kIdentityTrackingAnchorOrientation, stable_right, raw_right,
              raw_left, &state, &pose),
          "a valid support grip initializes the right-ray constraint");
    check_vector_exact(
        pose.grip_position, stable_right.grip_position,
        "support engagement keeps the established right-hand root exact");
    check_basis_exact(
        pose.aim_axis, stable_right.aim_axis,
        "support engagement cannot shift the established right-hand ray");

    set_support_angle(2.9F);
    ++generation;
    publication_milliseconds += 16;
    check(mod::update_right_ray_two_hand_steering_pose(
              generation, publication_milliseconds,
              kIdentityTrackingAnchorOrientation, stable_right, raw_right,
              raw_left, &state, &pose),
          "sub-intent support shimmer remains a valid pair");
    check_basis_exact(
        pose.aim_axis, stable_right.aim_axis,
        "sub-intent support shimmer leaves the right ray bit-exact");

    const mod::ControllerWeaponPose cached = pose;
    set_support_angle(8.0F);
    check(mod::update_right_ray_two_hand_steering_pose(
              generation, publication_milliseconds,
              kIdentityTrackingAnchorOrientation, stable_right, raw_right,
              raw_left, &state, &pose),
          "a repeated consumer reuses the published generation");
    check_vector_exact(
        pose.grip_position, cached.grip_position,
        "a repeated consumer reuses the exact cached root");
    check_basis_exact(
        pose.aim_axis, cached.aim_axis,
        "a repeated consumer cannot advance hidden steering history");

    // A long alternating sequence below the engage threshold cannot mutate
    // the accepted bore. Common translation changes every sample as well.
    for (std::uint64_t sample = 0; sample < 10'000; ++sample) {
        ++generation;
        publication_milliseconds += 16;
        const float direction = (sample & 1U) == 0 ? 1.0F : -1.0F;
        const float common = static_cast<float>(sample) * 0.0002F;
        stable_right.grip_position = {common, -common, common * 0.5F};
        raw_right.grip_position = {common, common * 0.25F, -common};
        set_support_angle(direction * 2.9F);
        check(mod::update_right_ray_two_hand_steering_pose(
                  generation, publication_milliseconds,
                  kIdentityTrackingAnchorOrientation, stable_right,
                  raw_right, raw_left, &state, &pose),
              "alternating sub-intent pair noise remains valid");
        check_vector_exact(
            pose.grip_position, stable_right.grip_position,
            "common pair translation follows the stable right root exactly");
        check_basis_exact(
            pose.aim_axis, stable_right.aim_axis,
            "alternating differential pair noise cannot accumulate into aim drift");
    }
    check(state.phase == mod::RightRayTwoHandSteeringPhase::Quiet,
          "bounded alternating noise never arms the support clutch");

    // A single large sample may enter Arming, but it cannot move the bore and
    // a return sample cancels it without leaving hidden accepted history.
    ++generation;
    publication_milliseconds += 16;
    set_support_angle(8.0F);
    check(mod::update_right_ray_two_hand_steering_pose(
              generation, publication_milliseconds,
              kIdentityTrackingAnchorOrientation, stable_right, raw_right,
              raw_left, &state, &pose),
          "one supra-threshold spike remains a valid arming sample");
    check_basis_exact(pose.aim_axis, stable_right.aim_axis,
                      "one spike cannot move the clutched right ray");
    ++generation;
    publication_milliseconds += 16;
    set_support_angle(0.0F);
    check(mod::update_right_ray_two_hand_steering_pose(
              generation, publication_milliseconds,
              kIdentityTrackingAnchorOrientation, stable_right, raw_right,
              raw_left, &state, &pose),
          "returning from a spike cancels arming");
    check(state.phase == mod::RightRayTwoHandSteeringPhase::Quiet,
          "a reversed spike returns the clutch to quiet");
    check_basis_exact(pose.aim_axis, stable_right.aim_axis,
                      "a cancelled spike leaves no steering ratchet");

    // Slow natural flex can cross the spatial threshold, but it does not
    // satisfy the outward-velocity intent gate.
    for (std::uint64_t sample = 1; sample <= 100; ++sample) {
        ++generation;
        publication_milliseconds += 16;
        set_support_angle(static_cast<float>(sample) * 0.08F);
        check(mod::update_right_ray_two_hand_steering_pose(
                  generation, publication_milliseconds,
                  kIdentityTrackingAnchorOrientation, stable_right,
                  raw_right, raw_left, &state, &pose),
              "slow support drift remains a valid quiet sample");
        check_basis_exact(pose.aim_axis, stable_right.aim_axis,
                          "slow support drift cannot ratchet the bore");
    }

    // A tracking/cadence pause is not intent and must not poison the stored
    // timestamp.  The first fresh pair after the gap reclutches in place and
    // later normal-cadence samples remain usable without a grip reset.
    ++generation;
    publication_milliseconds +=
        mod::kMaximumControllerFrameAgeMilliseconds + 1;
    set_support_angle(20.0F);
    check(mod::update_right_ray_two_hand_steering_pose(
              generation, publication_milliseconds,
              kIdentityTrackingAnchorOrientation, stable_right, raw_right,
              raw_left, &state, &pose),
          "a long controller-publication gap reclutches on the fresh pair");
    check(state.phase == mod::RightRayTwoHandSteeringPhase::Quiet,
          "a cadence gap cannot synthesize steering intent");
    check_basis_exact(pose.aim_axis, stable_right.aim_axis,
                      "a cadence gap preserves the accepted bore");

    ++generation;
    publication_milliseconds += 16;
    set_support_angle(20.2F);
    check(mod::update_right_ray_two_hand_steering_pose(
              generation, publication_milliseconds,
              kIdentityTrackingAnchorOrientation, stable_right, raw_right,
              raw_left, &state, &pose),
          "normal controller cadence resumes after a long gap");
    check_basis_exact(pose.aim_axis, stable_right.aim_axis,
                      "post-gap support noise remains clutched");
}

void test_right_ray_two_hand_constraint_allows_deliberate_relative_steering() {
    const xr::Basis3f identity = yaw_basis(0.0F);
    mod::ControllerWeaponPose stable_right{
        {0.0F, 0.0F, 0.0F}, identity};
    mod::ControllerWeaponPose raw_right{{0.0F, 0.0F, 0.0F}, identity};
    mod::ControllerWeaponPose raw_left{{20.0F, 0.0F, 0.0F}, identity};
    mod::RightRayTwoHandSteeringState state{};
    mod::ControllerWeaponPose pose{};
    const auto set_support_angle = [&raw_left](const float degrees) noexcept {
        constexpr float kDegreesToRadians = 0.0174532925199F;
        const float radians = degrees * kDegreesToRadians;
        raw_left.grip_position = {
            20.0F * std::cos(radians),
            20.0F * std::sin(radians),
            0.0F,
        };
    };
    std::uint64_t generation = 1;
    std::uint64_t publication_milliseconds = 1'000;
    check(mod::update_right_ray_two_hand_steering_pose(
              generation, publication_milliseconds,
              kIdentityTrackingAnchorOrientation, stable_right, raw_right,
              raw_left, &state, &pose),
          "relative steering captures a neutral support direction");

    // Four coherent outward samples after the engage crossing satisfy the
    // 50-ms dwell. Arming is sample-and-hold, including the transition sample.
    for (const float degrees : {3.5F, 4.0F, 4.5F, 5.0F, 5.5F}) {
        ++generation;
        publication_milliseconds += 16;
        set_support_angle(degrees);
        check(mod::update_right_ray_two_hand_steering_pose(
                  generation, publication_milliseconds,
                  kIdentityTrackingAnchorOrientation, stable_right,
                  raw_right, raw_left, &state, &pose),
              "coherent support intent advances the clutch dwell");
        check_basis_exact(
            pose.aim_axis, stable_right.aim_axis,
            "arming cannot move the accepted right ray");
    }
    check(state.phase == mod::RightRayTwoHandSteeringPhase::Steering,
          "sustained coherent support motion opens the steering clutch");

    ++generation;
    publication_milliseconds += 16;
    set_support_angle(7.5F);
    check(mod::update_right_ray_two_hand_steering_pose(
              generation, publication_milliseconds,
              kIdentityTrackingAnchorOrientation, stable_right, raw_right,
              raw_left, &state, &pose),
          "an armed support gesture steers the bore");
    const float first_steering_yaw = basis_yaw_degrees(pose.aim_axis);
    check(first_steering_yaw > 1.80F && first_steering_yaw < 2.05F,
          "support steering obeys the time-based 120-degree-per-second slew");
    check_vector_exact(pose.grip_position, stable_right.grip_position,
                       "support steering never moves the right-hand root");

    // Let the current target converge and reclutch, then resume the same
    // physical sweep.  The fixed settle anchor must accumulate ordinary
    // sub-3-degree steps until they can pass the full intent/coherence gate;
    // moving that anchor every sample would leave the support hand frozen.
    for (int sample = 0;
         sample < 8 &&
         state.phase != mod::RightRayTwoHandSteeringPhase::Settling;
         ++sample) {
        ++generation;
        publication_milliseconds += 16;
        check(mod::update_right_ray_two_hand_steering_pose(
                  generation, publication_milliseconds,
                  kIdentityTrackingAnchorOrientation, stable_right,
                  raw_right, raw_left, &state, &pose),
              "a held steering target reaches the settling phase");
    }
    check(state.phase == mod::RightRayTwoHandSteeringPhase::Settling,
          "the deliberate gesture enters settling before its dwell completes");
    const float paused_yaw = basis_yaw_degrees(pose.aim_axis);

    for (const float degrees : {8.0F, 8.5F, 9.0F, 9.5F, 10.0F}) {
        ++generation;
        publication_milliseconds += 16;
        set_support_angle(degrees);
        check(mod::update_right_ray_two_hand_steering_pose(
                  generation, publication_milliseconds,
                  kIdentityTrackingAnchorOrientation, stable_right,
                  raw_right, raw_left, &state, &pose),
              "resumed support motion accumulates against a fixed settle anchor");
        check(
            std::fabs(basis_yaw_degrees(pose.aim_axis) - paused_yaw) < 0.01F,
            "sub-threshold resumed motion cannot jump the accepted bore");
    }
    check(state.phase == mod::RightRayTwoHandSteeringPhase::Settling,
          "resumed motion below three degrees remains clutched");
    check(
        std::fabs(basis_yaw_degrees(pose.aim_axis) - paused_yaw) < 0.01F,
        "settling remains jump-free while resumed intent accumulates");

    for (const float degrees : {10.5F, 11.0F, 11.5F, 12.0F, 12.5F}) {
        ++generation;
        publication_milliseconds += 16;
        set_support_angle(degrees);
        check(mod::update_right_ray_two_hand_steering_pose(
                  generation, publication_milliseconds,
                  kIdentityTrackingAnchorOrientation, stable_right,
                  raw_right, raw_left, &state, &pose),
              "coherent resumed motion re-arms after the settle pause");
    }
    check(state.phase == mod::RightRayTwoHandSteeringPhase::Steering,
          "a resumed deliberate sweep reopens steering after coherence dwell");

    ++generation;
    publication_milliseconds += 16;
    set_support_angle(14.5F);
    check(mod::update_right_ray_two_hand_steering_pose(
              generation, publication_milliseconds,
              kIdentityTrackingAnchorOrientation, stable_right, raw_right,
              raw_left, &state, &pose),
          "re-armed support motion steers after a pause");
    check(basis_yaw_degrees(pose.aim_axis) > paused_yaw + 1.0F,
          "resumed coherent support motion advances the bore");

    // Endpoint shimmer can remain fast even though it repeatedly reverses and
    // expresses no continued steering intent.  One same-direction sample may
    // extend the authorized sweep; the first reversal must capture that last
    // coherent endpoint, converge to it, and then hold bit-exact while all
    // later raw support noise is ignored.
    xr::Basis3f frozen_noisy_endpoint{};
    for (int sample = 0; sample < 16; ++sample) {
        ++generation;
        publication_milliseconds += 16;
        set_support_angle(
            14.5F + ((sample & 1) == 0 ? 0.8F : -0.8F));
        check(mod::update_right_ray_two_hand_steering_pose(
                  generation, publication_milliseconds,
                  kIdentityTrackingAnchorOrientation, stable_right,
                  raw_right, raw_left, &state, &pose),
              "reversing endpoint shimmer remains a valid support sample");
        if (sample == 7) {
            frozen_noisy_endpoint = pose.aim_axis;
        } else if (sample > 7) {
            check_basis_exact(
                pose.aim_axis, frozen_noisy_endpoint,
                "endpoint shimmer cannot keep moving the converged bore");
        }
    }
    check(state.phase != mod::RightRayTwoHandSteeringPhase::Steering &&
              state.phase != mod::RightRayTwoHandSteeringPhase::Converging,
          "reversing endpoint shimmer closes the live steering connection");

    set_support_angle(14.5F);
    for (int sample = 0; sample < 6; ++sample) {
        ++generation;
        publication_milliseconds += 16;
        check(mod::update_right_ray_two_hand_steering_pose(
                  generation, publication_milliseconds,
                  kIdentityTrackingAnchorOrientation, stable_right,
                  raw_right, raw_left, &state, &pose),
              "a centered endpoint completes the quiet dwell");
    }
    check(state.phase == mod::RightRayTwoHandSteeringPhase::Quiet,
          "a centered endpoint finishes reclutching");
    const float settled_yaw = basis_yaw_degrees(pose.aim_axis);
    check(settled_yaw > 8.50F && settled_yaw < 9.10F,
          "the last coherent endpoint produces one bounded absolute correction");

    const xr::Basis3f held_axis = pose.aim_axis;
    for (const float noise : {2.9F, -2.9F, 2.5F, -2.5F}) {
        ++generation;
        publication_milliseconds += 16;
        set_support_angle(14.5F + noise);
        check(mod::update_right_ray_two_hand_steering_pose(
                  generation, publication_milliseconds,
                  kIdentityTrackingAnchorOrientation, stable_right,
                  raw_right, raw_left, &state, &pose),
              "post-settle support noise remains valid");
        check_basis_exact(
            pose.aim_axis, held_axis,
            "post-settle support noise cannot ratchet the accepted correction");
    }

    mod::reset_right_ray_two_hand_steering(&state);
    raw_left.grip_position = {30.0F, 0.0F, 0.0F};
    ++generation;
    publication_milliseconds += 16;
    check(mod::update_right_ray_two_hand_steering_pose(
              generation, publication_milliseconds,
              kIdentityTrackingAnchorOrientation, stable_right, raw_right,
              raw_left, &state, &pose),
          "a radial support regrip captures a fresh constraint");
    check_basis_exact(
        pose.aim_axis, stable_right.aim_axis,
        "radial support placement cannot steer the bore");
}

void test_right_ray_two_hand_constraint_cancels_common_rigid_motion() {
    const xr::Basis3f identity = yaw_basis(0.0F);
    mod::ControllerWeaponPose stable_right{
        {0.0F, 0.0F, 0.0F}, identity};
    mod::ControllerWeaponPose raw_right{{0.0F, 0.0F, 0.0F}, identity};
    mod::ControllerWeaponPose raw_left{{20.0F, 0.0F, 0.0F}, identity};
    mod::RightRayTwoHandSteeringState state{};
    mod::ControllerWeaponPose pose{};
    check(mod::update_right_ray_two_hand_steering_pose(
              1, 1'000, kIdentityTrackingAnchorOrientation, stable_right,
              raw_right, raw_left, &state, &pose),
          "rigid-motion test captures a neutral support constraint");

    stable_right.grip_position = {7.0F, -3.0F, 2.0F};
    stable_right.aim_axis = yaw_basis(90.0F);
    raw_right.grip_position = {10.0F, 20.0F, 30.0F};
    raw_right.aim_axis = yaw_basis(90.0F);
    raw_left.grip_position = {10.0F, 40.0F, 30.0F};
    check(mod::update_right_ray_two_hand_steering_pose(
              2, 1'016, kIdentityTrackingAnchorOrientation, stable_right,
              raw_right, raw_left, &state, &pose),
          "a common rigid hand translation and rotation remains valid");
    check_vector_exact(
        pose.grip_position, stable_right.grip_position,
        "common rigid hand motion follows the current right root exactly");
    check_basis_exact(
        pose.aim_axis, stable_right.aim_axis,
        "a constant controller-local support constraint follows the right ray exactly");

    // The stabilized visible right ray intentionally trails the current raw
    // controller by 90 degrees.  Both raw hands have moved rigidly together,
    // so that filter phase difference must not be interpreted as support-hand
    // steering around the otherwise stable ray.
    stable_right.aim_axis = identity;
    raw_right.aim_axis = yaw_basis(90.0F);
    raw_right.grip_position = {30.0F, 10.0F, -5.0F};
    raw_left.grip_position = {30.0F, 30.0F, -5.0F};
    check(mod::update_right_ray_two_hand_steering_pose(
              3, 1'032, kIdentityTrackingAnchorOrientation, stable_right,
              raw_right, raw_left, &state, &pose),
          "raw pair rotation remains coherent while the stable right ray lags");
    check_vector_exact(
        pose.grip_position, stable_right.grip_position,
        "raw/stable filter phase cannot move the visible right-hand root");
    check_basis_exact(
        pose.aim_axis, stable_right.aim_axis,
        "raw/stable filter phase cannot masquerade as support steering");

    const auto state_before_rejection = state;
    const mod::ControllerWeaponPose preserved{
        {99.0F, 98.0F, 97.0F}, yaw_basis(25.0F)};
    pose = preserved;
    raw_left.grip_position = {
        raw_right.grip_position.x + 0.1F,
        raw_right.grip_position.y,
        raw_right.grip_position.z,
    };
    check(!mod::update_right_ray_two_hand_steering_pose(
               4, 1'048, kIdentityTrackingAnchorOrientation, stable_right,
               raw_right, raw_left, &state, &pose),
          "an implausibly close controller pair fails closed");
    check(state.generation == state_before_rejection.generation,
          "a rejected pair cannot advance steering history");
    check_vector_exact(
        pose.grip_position, preserved.grip_position,
        "a rejected pair cannot mutate the caller's pose");

    const auto state_before_bad_basis = state;
    raw_right.grip_position = {0.0F, 0.0F, 0.0F};
    raw_right.aim_axis.forward = {1.0F, 0.0F, 0.0F};
    raw_right.aim_axis.left = {1.0F, 0.0F, 0.0F};
    raw_right.aim_axis.up = {1.0F, 0.0F, 0.0F};
    raw_left.grip_position = {20.0F, 0.0F, 0.0F};
    pose = preserved;
    check(!mod::update_right_ray_two_hand_steering_pose(
               5, 1'064, kIdentityTrackingAnchorOrientation, stable_right,
               raw_right, raw_left, &state, &pose),
          "a malformed raw right-controller basis fails closed");
    check(state.generation == state_before_bad_basis.generation,
          "a malformed raw basis cannot advance steering history");
    check_basis_exact(
        pose.aim_axis, preserved.aim_axis,
        "a malformed raw basis cannot mutate the caller's pose");

    mod::reset_right_ray_two_hand_steering(&state);
    check(!state.valid && state.generation == 0 &&
              state.publication_milliseconds == 0 &&
              state.phase == mod::RightRayTwoHandSteeringPhase::Quiet,
          "explicit right-ray constraint reset clears all history");
}

void test_pistol_close_palms_keep_live_right_ray() {
    mod::RightRayTwoHandSteeringState state{};
    mod::ControllerWeaponPose pose{};
    mod::ControllerWeaponPose right{{4, 5, 6}, yaw_basis(0.2F)};
    mod::ControllerWeaponPose left = right;
    for (std::uint64_t frame = 1; frame <= 240; ++frame) {
        const float step = static_cast<float>(frame);
        right.grip_position = {4 + step * 0.03F, 5 - step * 0.02F, 6};
        right.aim_axis = yaw_basis(step * 0.001F);
        left = right;
        switch (frame % 4) {
        case 0: break;  // Coincident controller positions are legal for support.
        case 1: left.grip_position.y += 1.1F; break;
        case 2: left.grip_position.x -= 0.7F; break;
        case 3: left.grip_position.z += 2.9F; break;
        }
        const mod::ControllerWeaponPose stable{
            {right.grip_position.x - 0.04F, right.grip_position.y, 6},
            yaw_basis(step * 0.001F - 0.002F)};
        check(mod::update_pistol_two_hand_support_pose(
                  frame, 1'000 + frame * 16, kIdentityTrackingAnchorOrientation,
                  stable, right, left, &state, &pose),
              "close and coincident pistol support remains live for over three seconds");
        check_vector_exact(pose.grip_position, stable.grip_position,
                           "pistol support never moves the stable firing-hand root");
        check_basis_exact(pose.aim_axis, stable.aim_axis,
                          "pistol support motion cannot steer or twist the firing ray");
        check(state.valid && state.generation == frame &&
                  state.phase == mod::RightRayTwoHandSteeringPhase::Quiet,
              "pistol pair publishes a coherent live transition receipt every frame");
    }

    mod::RightRayTwoHandSteeringState rifle_state{};
    const auto preserved = pose;
    left = right;
    left.grip_position.y += 1.1F;
    check(!mod::update_right_ray_two_hand_steering_pose(
               241, 5'000, kIdentityTrackingAnchorOrientation,
               right, right, left, &rifle_state, &pose),
          "the existing rifle path still rejects a too-short support lever");
    check(!rifle_state.valid,
          "pistol support does not relax or initialize rifle state");
    check_vector_exact(pose.grip_position, preserved.grip_position,
                       "rejected rifle pair preserves the output");
}

void test_pistol_support_rejects_bad_tracking_and_clears_rifle_history() {
    const mod::ControllerWeaponPose right{{1, 2, 3}, yaw_basis(0.3F)};
    mod::ControllerWeaponPose left = right;
    mod::RightRayTwoHandSteeringState state{};
    state.valid = true;
    state.generation = 5;
    state.publication_milliseconds = 1'000;
    state.phase = mod::RightRayTwoHandSteeringPhase::Steering;
    state.accepted_forward_right_local = {0, 1, 0};
    state.arming_elapsed_milliseconds = 99;
    mod::ControllerWeaponPose pose{};
    check(mod::update_pistol_two_hand_support_pose(
              6, 1'016, kIdentityTrackingAnchorOrientation,
              right, right, left, &state, &pose),
          "pistol support can replace a previously published rifle constraint");
    check_basis_exact(pose.aim_axis, right.aim_axis,
                      "no stale rifle steering reaches pistol aim");
    check(state.phase == mod::RightRayTwoHandSteeringPhase::Quiet &&
              state.arming_elapsed_milliseconds == 0 &&
              state.accepted_forward_right_local.x == 1.0F &&
              state.accepted_forward_right_local.y == 0.0F,
          "pistol support clears the previous steering phase and correction");
    const auto preserved_state = state;
    const auto preserved_pose = pose;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (int failure = 0; failure < 8; ++failure) {
        auto stable = right;
        auto raw = right;
        auto raw_left = left;
        auto anchor = kIdentityTrackingAnchorOrientation;
        std::uint64_t generation = 7;
        std::uint64_t timestamp = 1'032;
        switch (failure) {
        case 0: stable.grip_position.x = nan; break;
        case 1: stable.aim_axis.forward = {}; break;
        case 2: raw.aim_axis.left = raw.aim_axis.forward; break;
        case 3: raw.grip_position.z = nan; break;
        case 4: raw_left.grip_position.y = nan; break;
        case 5: anchor.w = nan; break;
        case 6: generation = 5; break;
        case 7: timestamp = 1'000; break;
        }
        check(!mod::update_pistol_two_hand_support_pose(
                   generation, timestamp, anchor, stable, raw, raw_left,
                   &state, &pose),
              "invalid tracking, basis, anchor or regressed publication fails closed");
        check(state.generation == preserved_state.generation &&
                  state.publication_milliseconds ==
                      preserved_state.publication_milliseconds,
              "rejected pistol pose does not advance the state");
        check_vector_exact(pose.grip_position, preserved_pose.grip_position,
                           "rejected pistol input cannot move the caller output");
        check_basis_exact(pose.aim_axis, preserved_pose.aim_axis,
                          "rejected pistol input cannot rotate the caller output");
    }
    check(!mod::update_pistol_two_hand_support_pose(
               7, 1'032, {}, right, right, left, nullptr, &pose) &&
              !mod::update_pistol_two_hand_support_pose(
                  7, 1'032, {}, right, right, left, &state, nullptr),
          "missing pistol state/output is rejected");
}

void test_pistol_support_release_and_regrip_preserves_right_attachment() {
    const xr::Vec3f camera_origin{};
    const xr::Basis3f camera_axis{};
    mod::ControllerWeaponPose right{{7, -3, 5}, yaw_basis(0.4F)};
    mod::ControllerWeaponPose left = right;
    left.grip_position.y += 1.0F;
    const xr::Vec3f original_origin{12, -1, 6};
    const xr::Basis3f original_axis = right.aim_axis;
    mod::WeaponAttachmentState right_attachment{};
    check(mod::calibrate_controller_weapon_attachment(
              camera_origin, camera_axis, right, original_origin, original_axis,
              &right_attachment),
          "pistol one-hand attachment establishes before support grip");
    const auto saved_attachment = right_attachment;
    mod::RightRayTwoHandSteeringState support_state{};
    mod::ControllerWeaponPose pair{};
    for (std::uint64_t cycle = 0; cycle < 8; ++cycle) {
        check(mod::update_pistol_two_hand_support_pose(
                  1 + cycle, 1'000 + cycle * 100, {}, right, right, left,
                  &support_state, &pair),
              "close-palm regrip publishes a valid pair");
        mod::WeaponAttachmentState paired_attachment{};
        xr::Vec3f origin{};
        xr::Basis3f axis{};
        check(mod::transfer_controller_weapon_attachment(
                  camera_origin, camera_axis, right, right_attachment, pair,
                  &paired_attachment, &origin, &axis),
              "right-to-pistol-pair transfer remains valid at close separation");
        check_vector(origin, original_origin,
                          "adding pistol support preserves the existing weapon position");
        check_basis(axis, original_axis,
                         "adding pistol support preserves the firing direction");
        mod::reset_right_ray_two_hand_steering(&support_state);
        check(mod::apply_controller_weapon_placement(
                  camera_origin, camera_axis, right, &right_attachment,
                  &origin, &axis),
              "left release restores the unchanged right attachment");
        check_vector(origin, original_origin,
                          "support release cannot shift pistol position");
        check_basis(axis, original_axis,
                         "support release cannot rotate pistol aim");
        check_vector_exact(right_attachment.position, saved_attachment.position,
                           "repeated pistol regrips never rewrite the saved right offset");
        check_basis_exact(right_attachment.axis, saved_attachment.axis,
                          "repeated pistol regrips never rewrite the saved right basis");
    }
}

void test_axis_to_engine_quaternion() {
    xr::Basis3f yaw_left{};
    yaw_left.forward = {0.0F, 1.0F, 0.0F};
    yaw_left.left = {-1.0F, 0.0F, 0.0F};
    yaw_left.up = {0.0F, 0.0F, 1.0F};
    xr::Quaternionf quaternion{};
    check(mod::iw_axis_to_unit_quaternion(yaw_left, &quaternion),
          "valid IW axis converts to unit quaternion");

    // For the engine row convention a +90-degree yaw is +Z quaternion.
    constexpr float kHalfSqrtTwo = 0.70710678118F;
    check(near(std::abs(quaternion.z), kHalfSqrtTwo) &&
              near(std::abs(quaternion.w), kHalfSqrtTwo),
          "engine quaternion represents 90-degree yaw");
    check(near(quaternion.x, 0.0F) && near(quaternion.y, 0.0F),
          "yaw quaternion has no pitch/roll components");
}

}  // namespace

int main() {
    test_pose_uses_grip_position_and_aim_orientation();
    test_valid_but_inferred_pose_remains_usable();
    test_left_controller_can_drive_same_weapon_contract();
    test_controller_pose_is_tracking_anchor_relative();
    test_current_head_local_pose_composes_to_established_world_contract();
    test_stable_two_hand_pose_rebases_after_filtering();
    test_weapon_pose_filter_matches_cod4_response_once_per_generation();
    test_weapon_pose_filter_matches_skipped_generation_responses();
    test_weapon_pose_filter_rejects_tracking_discontinuities();
    test_weapon_pose_filter_uses_shortest_quaternion_hemisphere();
    test_two_hand_rebaseline_uses_one_coherent_publication();
    test_two_hand_common_translation_keeps_a_rigid_hand_line();
    test_raw_pair_delta_tracks_steering_without_filter_lag();
    test_raw_pair_delta_fails_closed_without_mutating_output();
    test_two_hand_orientation_filter_initializes_and_reuses_sample();
    test_two_hand_direction_filter_reseeds_on_tracking_anchor_rebase();
    test_two_hand_direction_filter_is_frame_rate_independent();
    test_two_hand_direction_filter_attenuates_alternating_pair_noise();
    test_two_hand_direction_filter_attenuates_endpoint_jitter();
    test_two_hand_direction_filter_reseeds_without_regrip_snap();
    test_two_hand_direction_filter_converges_on_deliberate_motion();
    test_two_hand_direction_filter_preserves_translation_and_roll();
    test_two_hand_direction_filter_rejects_discontinuities();
    test_cod4_two_hand_pose_builds_expected_basis();
    test_cod4_two_hand_pose_steers_from_fixed_right_grip();
    test_cod4_live_pair_uses_stabilized_right_and_raw_left_immediately();
    test_cod4_two_hand_pose_is_radially_invariant();
    test_cod4_two_hand_pose_preserves_right_roll_hint();
    test_cod4_two_hand_pose_rejects_degenerate_input();
    test_cod4_two_hand_engagement_blend_endpoints();
    test_adaptive_weapon_response_damps_quiet_motion_and_releases_fast();
    test_two_hand_engagement_uses_filtered_endpoints();
    test_cod4_two_hand_engagement_sequence_and_generation_cache();
    test_cod4_two_hand_engagement_skipped_generation_cadence();
    test_cod4_two_hand_engagement_anchor_cache_key();
    test_cod4_two_hand_engagement_clamps_and_reset();
    test_right_ray_two_hand_constraint_latches_without_a_pose_shift();
    test_right_ray_two_hand_constraint_allows_deliberate_relative_steering();
    test_right_ray_two_hand_constraint_cancels_common_rigid_motion();
    test_pistol_close_palms_keep_live_right_ray();
    test_pistol_support_rejects_bad_tracking_and_clears_rifle_history();
    test_pistol_support_release_and_regrip_preserves_right_attachment();
    test_rigid_absolute_weapon_attachment();
    test_chest_pickup_does_not_capture_holster_angle();
    test_exact_controller_handoff_attachment();
    test_controller_attachment_survives_reference_rebase();
    test_handoff_transfer_reconstructs_current_outgoing_pose();
    test_controller_forward_alignment_removes_transferred_axis_offset();
    test_controller_forward_alignment_initializes_position_once();
    test_post_pose_grip_tag_alignment();
    test_evaluated_viewmodel_skeleton_translation();
    test_translated_viewmodel_tag_match_tolerates_float_reassociation();
    test_axis_to_engine_quaternion();
    return 0;
}
