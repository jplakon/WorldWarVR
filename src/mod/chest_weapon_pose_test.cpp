// SPDX-License-Identifier: GPL-3.0-only
#include "chest_weapon_pose.hpp"
#include "xr_math.h"

#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <limits>

namespace {
namespace xr = wawvr::xr;
using wawvr::mod::build_chest_weapon_pose;
using wawvr::mod::ChestWeaponPoseState;
using wawvr::mod::kChestGripLocal;
using wawvr::mod::kChestWeaponAxisLocal;

void check(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

[[nodiscard]] bool near(const float a, const float b,
                        const float epsilon = 2.0e-4F) noexcept {
    return std::abs(a - b) <= epsilon;
}

[[nodiscard]] bool near(const xr::Vec3f& a, const xr::Vec3f& b) noexcept {
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

[[nodiscard]] bool near(const xr::Basis3f& a, const xr::Basis3f& b) noexcept {
    return near(a.forward, b.forward) && near(a.left, b.left) &&
        near(a.up, b.up);
}

[[nodiscard]] bool near(const xr::EnginePose& a,
                        const xr::EnginePose& b) noexcept {
    return near(a.position, b.position) && near(a.axis, b.axis);
}

[[nodiscard]] xr::Quaternionf rotation(const xr::Vec3f& axis,
                                      const float degrees) noexcept {
    constexpr float kHalfRadiansPerDegree =
        3.14159265358979323846F / 360.0F;
    const float half_angle = degrees * kHalfRadiansPerDegree;
    const float sine = std::sin(half_angle);
    return {axis.x * sine, axis.y * sine, axis.z * sine,
            std::cos(half_angle)};
}

[[nodiscard]] xr::Quaternionf yaw(const float degrees) noexcept {
    return rotation({0.0F, 1.0F, 0.0F}, degrees);
}

[[nodiscard]] xr::Basis3f yaw_basis(const float degrees) noexcept {
    constexpr float kRadiansPerDegree = 3.14159265358979323846F / 180.0F;
    const float cosine = std::cos(degrees * kRadiansPerDegree);
    const float sine = std::sin(degrees * kRadiansPerDegree);
    return {{cosine, sine, 0}, {-sine, cosine, 0}, {0, 0, 1}};
}

[[nodiscard]] xr::Vec3f compose(const xr::Basis3f& basis,
                               const xr::Vec3f& local) noexcept {
    return {basis.forward.x * local.x + basis.left.x * local.y +
                basis.up.x * local.z,
            basis.forward.y * local.x + basis.left.y * local.y +
                basis.up.y * local.z,
            basis.forward.z * local.x + basis.left.z * local.y +
                basis.up.z * local.z};
}

[[nodiscard]] xr::Basis3f expected_axis(const float degrees) noexcept {
    const auto basis = yaw_basis(degrees);
    return {compose(basis, kChestWeaponAxisLocal.forward),
            compose(basis, kChestWeaponAxisLocal.left),
            compose(basis, kChestWeaponAxisLocal.up)};
}

void test_head_yaw_tracks_without_body_turn() {
    const xr::EnginePose body{{100, -200, 70}, {}};
    for (const float degrees : {0.0F, 90.0F, -90.0F, 180.0F}) {
        const xr::Posef head{yaw(degrees), {}};
        xr::EnginePose chest{};
        check(build_chest_weapon_pose(body, head, {}, &chest),
              "head yaw yields a valid chest pose");
        const auto offset = compose(yaw_basis(degrees), kChestGripLocal);
        check(near(chest.position, {body.position.x + offset.x,
                                    body.position.y + offset.y,
                                    body.position.z + offset.z}),
              "visible grip remains at the sternum as the head yaws");
        check(near(chest.axis, expected_axis(degrees)),
              "chest diagonal rotates once with HMD yaw");
    }
}

void test_anchor_and_body_turn_are_composed_once() {
    const xr::EnginePose body{{100, -200, 70}, yaw_basis(30)};
    const xr::Posef anchor{yaw(60), {2, 1.7F, -4}};
    xr::Posef head{yaw(110), anchor.position};
    xr::EnginePose chest{};
    check(build_chest_weapon_pose(body, head, anchor, &chest),
          "nonidentity tracking anchor accepted");
    const auto offset = compose(yaw_basis(80), kChestGripLocal);
    check(near(chest.position, {100 + offset.x, -200 + offset.y, 53}) &&
              near(chest.axis, expected_axis(80)),
          "30 body plus 110 head minus 60 anchor equals 80, not double yaw");

    // A local XR translation is first rotated into the anchored reference
    // space. The independent expected IW offset then gets body yaw once.
    const xr::Vec3f local_xr{0.25F, -0.4F, -0.3F};
    const auto reference_delta = xr::Rotate(anchor.orientation, local_xr);
    head.position = {anchor.position.x + reference_delta.x,
                     anchor.position.y + reference_delta.y,
                     anchor.position.z + reference_delta.z};
    xr::EnginePose moved{};
    check(build_chest_weapon_pose(body, head, anchor, &moved),
          "anchored room-scale translation accepted");
    const auto expected_delta = compose(body.axis,
        {0.3F * xr::kIwUnitsPerMeter, -0.25F * xr::kIwUnitsPerMeter,
         -0.4F * xr::kIwUnitsPerMeter});
    check(near(moved.position, {chest.position.x + expected_delta.x,
                               chest.position.y + expected_delta.y,
                               chest.position.z + expected_delta.z}) &&
              near(moved.axis, chest.axis),
          "physical crouch and translation follow the tracked head exactly");
}

void test_pitch_and_roll_do_not_tilt_chest() {
    const xr::EnginePose body{{-10, 30, 50}, yaw_basis(-20)};
    for (const float pitch : {-80.0F, -40.0F, 0.0F, 40.0F, 80.0F}) {
        for (const float roll : {-75.0F, 0.0F, 75.0F}) {
            const auto tilt = xr::Multiply(rotation({1, 0, 0}, pitch),
                                            rotation({0, 0, -1}, roll));
            const xr::Posef head{xr::Multiply(yaw(60), tilt), {}};
            xr::EnginePose chest{};
            check(build_chest_weapon_pose(body, head, {}, &chest),
                  "pitched and rolled tracked head accepted");
            const auto offset = compose(yaw_basis(40), kChestGripLocal);
            check(near(chest.position, {-10 + offset.x, 30 + offset.y, 33}) &&
                      near(chest.axis, expected_axis(40)),
                  "pitch and roll never swing the chest around the eyes");
        }
    }
}

[[nodiscard]] float dot(const xr::Vec3f& a, const xr::Vec3f& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

void test_near_vertical_fallback_is_bounded_and_orthonormal() {
    const xr::EnginePose body{{10, 20, 60}, {}};
    for (const float pitch : {-90.0F, -89.999F, 89.999F, 90.0F}) {
        for (const float roll : {-65.0F, 0.0F, 65.0F}) {
            const auto tilt = xr::Multiply(rotation({1, 0, 0}, pitch),
                                            rotation({0, 0, -1}, roll));
            const xr::Posef head{xr::Multiply(yaw(120), tilt), {}};
            xr::EnginePose chest{};
            check(build_chest_weapon_pose(body, head, {}, &chest),
                  "vertical head without history gets a finite body fallback");
            check(near(chest.axis, expected_axis(0)),
                  "no-history fallback ignores head roll and uses body yaw");
            const xr::Vec3f offset{chest.position.x - body.position.x,
                                    chest.position.y - body.position.y,
                                    chest.position.z - body.position.z};
            check(near(offset.z, -17) &&
                      near(offset.x * offset.x + offset.y * offset.y, 16),
                  "fallback holds fixed chest height and forward distance");
            const auto& a = chest.axis;
            check(near(dot(a.forward, a.forward), 1) &&
                      near(dot(a.left, a.left), 1) && near(dot(a.up, a.up), 1) &&
                      near(dot(a.forward, a.left), 0) &&
                      near(dot(a.forward, a.up), 0) &&
                      near(dot(a.left, a.up), 0),
                  "fallback weapon basis stays finite and orthonormal");
            check(near(a.forward.z, kChestWeaponAxisLocal.forward.z) &&
                      near(a.left.z, kChestWeaponAxisLocal.left.z) &&
                      near(a.up.z, kChestWeaponAxisLocal.up.z),
                  "fallback never adopts vertical head pitch or roll");
        }
    }
}

void test_sequential_vertical_motion_preserves_heading_without_roll_jump() {
    const xr::EnginePose body{{10, 20, 60}, {}};
    for (const float sign : {-1.0F, 1.0F}) {
        ChestWeaponPoseState state{};
        xr::EnginePose previous{};
        for (const float magnitude : {80.0F, 84.0F, 85.0F, 89.0F, 89.99F,
                                       89.999F, 90.0F, 90.001F, 89.999F,
                                       89.99F, 85.0F, 84.0F, 80.0F}) {
            const auto tilt = xr::Multiply(
                rotation({1, 0, 0}, sign * magnitude),
                rotation({0, 0, -1}, 65));
            const xr::Posef head{xr::Multiply(yaw(120), tilt), {}};
            xr::EnginePose chest{};
            check(build_chest_weapon_pose(body, head, {}, &chest, &state),
                  "sequential vertical samples produce a chest pose");
            check(state.heading_valid && near(chest.axis, expected_axis(120)),
                  "89.99 to 89.999 pitch never changes yaw120 into yaw55");
            if (magnitude != 80.0F) {
                check(near(chest, previous),
                      "continuous approach and return from vertical never jumps");
            }
            previous = chest;
        }
    }
}

void test_retained_heading_respects_body_turn_and_anchor_rebase() {
    xr::EnginePose body{{10, 20, 60}, yaw_basis(30)};
    xr::Posef anchor{yaw(60), {}};
    ChestWeaponPoseState state{};
    xr::EnginePose chest{};
    check(build_chest_weapon_pose(body, {yaw(110), {}}, anchor,
                                 &chest, &state) &&
              near(chest.axis, expected_axis(80)),
          "seed a reliable anchor-relative heading with body yaw");
    const xr::Posef vertical{
        xr::Multiply(yaw(110), xr::Multiply(rotation({1, 0, 0}, -90),
                                           rotation({0, 0, -1}, 65))), {}};
    check(build_chest_weapon_pose(body, vertical, anchor, &chest, &state) &&
              near(chest.axis, expected_axis(80)),
          "vertical pose retains preceding composed heading");
    body.axis = yaw_basis(65);
    check(build_chest_weapon_pose(body, vertical, anchor, &chest, &state) &&
              near(chest.axis, expected_axis(115)),
          "body snap turn rotates retained local heading exactly once");
    // A +20 anchor recenter compensated by +20 body yaw leaves world yaw
    // unchanged, including while head-forward has no horizontal heading.
    body.axis = yaw_basis(85);
    anchor.orientation = yaw(80);
    check(build_chest_weapon_pose(body, vertical, anchor, &chest, &state) &&
              near(chest.axis, expected_axis(115)),
          "anchor rebase and compensating body yaw do not double-turn cache");
    check(build_chest_weapon_pose(body, {yaw(125), {}}, anchor,
                                 &chest, &state) &&
              near(chest.axis, expected_axis(130)),
          "leaving vertical resumes current head yaw and refreshes heading");
}

void test_heading_state_is_transactional_on_invalid_input() {
    ChestWeaponPoseState state{};
    xr::EnginePose output{};
    check(build_chest_weapon_pose({}, {yaw(75), {}}, {}, &output, &state),
          "seed state before invalid input");
    const auto before = state;
    const auto output_before = output;
    xr::Posef invalid{};
    invalid.orientation.w = std::numeric_limits<float>::quiet_NaN();
    check(!build_chest_weapon_pose({}, invalid, {}, &output, &state) &&
              near(output, output_before) &&
              state.heading_valid == before.heading_valid &&
              near(state.heading_body_local, before.heading_body_local) &&
              near(state.heading_tracking_anchor_orientation.w,
                   before.heading_tracking_anchor_orientation.w),
          "bad tracking cannot overwrite the retained trustworthy heading");
    state = {};
    const xr::Posef vertical{
        xr::Multiply(yaw(110), rotation({1, 0, 0}, -90)), {}};
    check(build_chest_weapon_pose({{}, yaw_basis(-35)}, vertical, {},
                                 &output, &state) &&
              near(output.axis, expected_axis(-35)) && !state.heading_valid,
          "cleared-state vertical fallback uses body without claiming history");
}

void test_invalid_input_does_not_change_output() {
    const xr::EnginePose body{};
    const xr::Posef pose{};
    const xr::EnginePose sentinel{{8, 9, 10}, yaw_basis(33)};
    auto output = sentinel;
    const auto reject = [&](const xr::EnginePose& invalid_body,
                            const xr::Posef& head,
                            const xr::Posef& anchor) {
        check(!build_chest_weapon_pose(invalid_body, head, anchor, &output),
              "invalid chest input rejected");
        check(near(output, sentinel), "failure leaves caller output unchanged");
    };
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    xr::EnginePose bad_body = body;
    bad_body.position.x = nan;
    reject(bad_body, pose, pose);
    bad_body = body;
    bad_body.axis.forward = {};
    reject(bad_body, pose, pose);
    bad_body = body;
    bad_body.axis.left = body.axis.forward;
    reject(bad_body, pose, pose);
    bad_body = body;
    bad_body.axis.up.z = -1;
    reject(bad_body, pose, pose);
    xr::Posef bad_pose = pose;
    bad_pose.position.y = infinity;
    reject(body, bad_pose, pose);
    reject(body, pose, bad_pose);
    bad_pose = pose;
    bad_pose.orientation.x = nan;
    reject(body, bad_pose, pose);
    reject(body, pose, bad_pose);
    bad_pose = pose;
    bad_pose.orientation = {0, 0, 0, 0};
    reject(body, bad_pose, pose);
    reject(body, pose, bad_pose);
    bad_pose.orientation = {0, 0, 0, 1.0e-8F};
    reject(body, bad_pose, pose);
    reject(body, pose, bad_pose);
    bad_pose.orientation = {0, 0, 0, std::numeric_limits<float>::max()};
    reject(body, bad_pose, pose);
    reject(body, pose, bad_pose);
    check(!build_chest_weapon_pose(body, pose, pose, nullptr),
          "null output rejected");
}

void test_nonunit_quaternion_is_normalized_and_output_may_alias_body() {
    xr::Posef head{yaw(90), {}};
    head.orientation.y *= 2;
    head.orientation.w *= 2;
    xr::EnginePose body{{1, 2, 3}, {}};
    check(build_chest_weapon_pose(body, head, {}, &body),
          "finite nonunit quaternion and aliased output accepted");
    check(near(body.position, {1, 6, -14}) &&
              near(body.axis, expected_axis(90)),
          "quaternion normalization and transactional composition are correct");
}

}  // namespace

int main() {
    test_head_yaw_tracks_without_body_turn();
    test_anchor_and_body_turn_are_composed_once();
    test_pitch_and_roll_do_not_tilt_chest();
    test_near_vertical_fallback_is_bounded_and_orthonormal();
    test_sequential_vertical_motion_preserves_heading_without_roll_jump();
    test_retained_heading_respects_body_turn_and_anchor_rebase();
    test_heading_state_is_transactional_on_invalid_input();
    test_invalid_input_does_not_change_output();
    test_nonunit_quaternion_is_normalized_and_output_may_alias_body();
    return 0;
}
