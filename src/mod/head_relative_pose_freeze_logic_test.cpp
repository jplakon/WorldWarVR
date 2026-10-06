// SPDX-License-Identifier: GPL-3.0-only
#include "head_relative_pose_freeze_logic.hpp"
#include "xr_math.h"

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
    const float tolerance = 1.0e-4F) noexcept {
    return std::abs(left - right) <= tolerance;
}

[[nodiscard]] bool near(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right,
    const float tolerance = 1.0e-4F) noexcept {
    return near(left.x, right.x, tolerance) &&
        near(left.y, right.y, tolerance) &&
        near(left.z, right.z, tolerance);
}

[[nodiscard]] bool near(
    const wawvr::xr::Basis3f& left,
    const wawvr::xr::Basis3f& right,
    const float tolerance = 1.0e-4F) noexcept {
    return near(left.forward, right.forward, tolerance) &&
        near(left.left, right.left, tolerance) &&
        near(left.up, right.up, tolerance);
}

[[nodiscard]] wawvr::xr::Basis3f yaw_basis(const float degrees) noexcept {
    constexpr float kDegreesToRadians =
        3.14159265358979323846F / 180.0F;
    const float radians = degrees * kDegreesToRadians;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return {
        {cosine, sine, 0.0F},
        {-sine, cosine, 0.0F},
        {0.0F, 0.0F, 1.0F},
    };
}

void test_identity_capture_is_exact() {
    const wawvr::xr::EnginePose head{};
    const wawvr::xr::EnginePose world{
        {2.0F, -3.0F, 4.0F}, yaw_basis(30.0F)};
    wawvr::mod::HeadRelativePoseFreeze freeze{};
    check(wawvr::mod::capture_head_relative_pose_freeze(
              head, world, &freeze),
          "identity head captures a finite world pose");
    check(freeze.valid && near(freeze.head_local_pose.position, world.position) &&
              near(freeze.head_local_pose.axis, world.axis),
          "identity capture preserves the exact pose coordinates");

    wawvr::xr::EnginePose reconstructed{};
    check(wawvr::mod::reconstruct_head_relative_pose_freeze(
              freeze, head, &reconstructed),
          "identity head reconstructs a captured pose");
    check(near(reconstructed.position, world.position) &&
              near(reconstructed.axis, world.axis),
          "identity reconstruction round-trips the world pose");
}

void test_head_world_composition_matches_reference_relative_pose() {
    const wawvr::xr::EnginePose body{
        {40.0F, -20.0F, 5.0F}, yaw_basis(35.0F)};
    wawvr::xr::Posef anchor{};
    anchor.position = {1.0F, 2.0F, 3.0F};
    anchor.orientation.w = 1.0F;
    wawvr::xr::Posef head = anchor;
    head.position = {1.25F, 1.90F, 3.40F};

    const auto relative = wawvr::xr::OpenXrPoseToIwRelative(
        head, anchor, wawvr::xr::kIwUnitsPerMeter);
    wawvr::xr::EnginePose composed{};
    check(wawvr::mod::compose_head_world_pose(
              body, head, anchor, wawvr::xr::kIwUnitsPerMeter, &composed),
          "head-world composition accepts an exact predicted head sample");

    const wawvr::xr::Vec3f expected_offset{
        body.axis.forward.x * relative.position.x +
            body.axis.left.x * relative.position.y +
            body.axis.up.x * relative.position.z,
        body.axis.forward.y * relative.position.x +
            body.axis.left.y * relative.position.y +
            body.axis.up.y * relative.position.z,
        body.axis.forward.z * relative.position.x +
            body.axis.left.z * relative.position.y +
            body.axis.up.z * relative.position.z,
    };
    check(near(
              composed.position,
              {body.position.x + expected_offset.x,
               body.position.y + expected_offset.y,
               body.position.z + expected_offset.z}) &&
              near(composed.axis, body.axis),
          "head-world composition uses the same anchor-relative transform as stereo");

    check(!wawvr::mod::compose_head_world_pose(
              body, head, anchor, 0.0F, &composed) &&
              near(composed.position, {}) && near(composed.axis, {}),
          "invalid head-world scale fails closed and clears output");
    check(!wawvr::mod::compose_head_world_pose(
              body, head, anchor, wawvr::xr::kIwUnitsPerMeter, nullptr),
          "null head-world output is rejected");
}

void test_translation_and_rotation_reconstruct_under_new_head() {
    const wawvr::xr::EnginePose initial_head{
        {10.0F, -4.0F, 3.0F}, yaw_basis(90.0F)};
    // In initial-head coordinates this is position (2,-1,0.5) and yaw +45.
    const wawvr::xr::EnginePose initial_world{
        {11.0F, -2.0F, 3.5F}, yaw_basis(135.0F)};
    wawvr::mod::HeadRelativePoseFreeze freeze{};
    check(wawvr::mod::capture_head_relative_pose_freeze(
              initial_head, initial_world, &freeze),
          "rotated and translated head captures the world pose");
    check(near(freeze.head_local_pose.position, {2.0F, -1.0F, 0.5F}) &&
              near(freeze.head_local_pose.axis, yaw_basis(45.0F)),
          "capture expresses position and orientation in head coordinates");

    wawvr::xr::EnginePose original_round_trip{};
    check(wawvr::mod::reconstruct_head_relative_pose_freeze(
              freeze, initial_head, &original_round_trip),
          "captured pose reconstructs under its original head pose");
    check(near(original_round_trip.position, initial_world.position) &&
              near(original_round_trip.axis, initial_world.axis),
          "original head pose produces the original world pose");

    const wawvr::xr::EnginePose moved_head{
        {-3.0F, 8.0F, 1.0F}, yaw_basis(-90.0F)};
    wawvr::xr::EnginePose moved_world{};
    check(wawvr::mod::reconstruct_head_relative_pose_freeze(
              freeze, moved_head, &moved_world),
          "captured pose reconstructs under a moved head pose");
    check(near(moved_world.position, {-4.0F, 6.0F, 1.5F}) &&
              near(moved_world.axis, yaw_basis(-45.0F)),
          "head translation and rotation preserve the frozen local pose");
}

void test_three_dimensional_basis_round_trip() {
    const wawvr::xr::Basis3f vertical_head{
        {0.0F, 0.0F, 1.0F},
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
    };
    const wawvr::xr::EnginePose head{{4.0F, 5.0F, 6.0F}, vertical_head};
    const wawvr::xr::EnginePose world{
        {6.0F, 8.0F, 7.0F},
        {
            {0.0F, 1.0F, 0.0F},
            {0.0F, 0.0F, 1.0F},
            {1.0F, 0.0F, 0.0F},
        },
    };
    wawvr::mod::HeadRelativePoseFreeze freeze{};
    wawvr::xr::EnginePose reconstructed{};
    check(wawvr::mod::capture_head_relative_pose_freeze(
              head, world, &freeze) &&
              wawvr::mod::reconstruct_head_relative_pose_freeze(
                  freeze, head, &reconstructed),
          "non-yaw orthonormal poses capture and reconstruct");
    check(near(reconstructed.position, world.position) &&
              near(reconstructed.axis, world.axis),
          "full three-dimensional basis round-trips without axis loss");
}

void test_invalid_inputs_fail_closed() {
    const wawvr::xr::EnginePose valid_head{};
    const wawvr::xr::EnginePose valid_world{
        {1.0F, 2.0F, 3.0F}, yaw_basis(20.0F)};
    wawvr::mod::HeadRelativePoseFreeze freeze{};

    auto invalid_head = valid_head;
    invalid_head.position.x = std::numeric_limits<float>::infinity();
    check(!wawvr::mod::capture_head_relative_pose_freeze(
              invalid_head, valid_world, &freeze) && !freeze.valid,
          "non-finite head position is rejected and clears capture");

    invalid_head = valid_head;
    invalid_head.axis.left = invalid_head.axis.forward;
    check(!wawvr::mod::capture_head_relative_pose_freeze(
              invalid_head, valid_world, &freeze) && !freeze.valid,
          "non-orthogonal head basis is rejected");

    auto reflected_world = valid_world;
    reflected_world.axis.up.z = -1.0F;
    check(!wawvr::mod::capture_head_relative_pose_freeze(
              valid_head, reflected_world, &freeze) && !freeze.valid,
          "left-handed world basis is rejected");

    auto scaled_world = valid_world;
    scaled_world.axis.forward.x *= 2.0F;
    scaled_world.axis.forward.y *= 2.0F;
    check(!wawvr::mod::capture_head_relative_pose_freeze(
              valid_head, scaled_world, &freeze) && !freeze.valid,
          "non-unit world basis is rejected");

    check(wawvr::mod::capture_head_relative_pose_freeze(
              valid_head, valid_world, &freeze),
          "valid fixture captures before reconstruction rejection tests");
    wawvr::xr::EnginePose output{
        {9.0F, 9.0F, 9.0F}, yaw_basis(90.0F)};
    auto invalid_freeze = freeze;
    invalid_freeze.head_local_pose.position.y =
        std::numeric_limits<float>::quiet_NaN();
    check(!wawvr::mod::reconstruct_head_relative_pose_freeze(
              invalid_freeze, valid_head, &output) &&
              near(output.position, {}) && near(output.axis, {}),
          "invalid retained pose is rejected and clears reconstruction");

    invalid_freeze = freeze;
    invalid_freeze.valid = false;
    check(!wawvr::mod::reconstruct_head_relative_pose_freeze(
              invalid_freeze, valid_head, &output),
          "unarmed freeze is rejected");
    check(!wawvr::mod::capture_head_relative_pose_freeze(
              valid_head, valid_world, nullptr),
          "null capture output is rejected");
    check(!wawvr::mod::reconstruct_head_relative_pose_freeze(
              freeze, valid_head, nullptr),
          "null reconstruction output is rejected");
}

}  // namespace

int main() {
    test_identity_capture_is_exact();
    test_head_world_composition_matches_reference_relative_pose();
    test_translation_and_rotation_reconstruct_under_new_head();
    test_three_dimensional_basis_round_trip();
    test_invalid_inputs_fail_closed();
    return 0;
}
