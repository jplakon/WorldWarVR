// SPDX-License-Identifier: GPL-3.0-only
#include "weapon_frame_base_phase_logic.hpp"

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

bool near(const float left, const float right, const float tolerance = 0.001F) {
    return std::abs(left - right) <= tolerance;
}

wawvr::xr::Basis3f yaw_basis(const float degrees) {
    constexpr float kDegreesToRadians =
        3.14159265358979323846F / 180.0F;
    const float radians = degrees * kDegreesToRadians;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return {{cosine, sine, 0.0F}, {-sine, cosine, 0.0F},
            {0.0F, 0.0F, 1.0F}};
}

wawvr::mod::WeaponFrameBaseReceipt receipt_fixture() {
    wawvr::mod::WeaponFrameBaseReceipt receipt{};
    receipt.valid = true;
    receipt.controller_generation = 9;
    receipt.frame_id = 100;
    receipt.action_sequence = 200;
    receipt.predicted_display_time = 300;
    receipt.controller_publication_milliseconds = 400;
    receipt.weapon_sample_milliseconds = 405;
    receipt.same_frame_sample_count = 1;
    receipt.scene_base_lock_eligible = true;
    receipt.tracking_anchor.position = {1.0F, 2.0F, 3.0F};
    receipt.head_center.position = {4.0F, 5.0F, 6.0F};
    receipt.camera_origin = {10.0F, 20.0F, 30.0F};
    receipt.body_axis = {};
    receipt.weapon_origin = {11.0F, 22.0F, 33.0F};
    receipt.weapon_axis = {};
    return receipt;
}

wawvr::xr::FrameState frame_fixture(
    const wawvr::mod::WeaponFrameBaseReceipt& receipt) {
    wawvr::xr::FrameState frame{};
    frame.frame_id = receipt.frame_id;
    frame.predicted_display_time = receipt.predicted_display_time;
    frame.actions.sequence = receipt.action_sequence;
    frame.head_center = receipt.head_center;
    return frame;
}

void test_exact_frame_measures_and_selects_weapon_base() {
    const auto receipt = receipt_fixture();
    const auto frame = frame_fixture(receipt);
    const wawvr::xr::Vec3f scene_origin{13.0F, 24.0F, 30.0F};
    const auto scene_axis = yaw_basis(30.0F);
    wawvr::xr::Vec3f selected_origin{};
    wawvr::xr::Basis3f selected_axis{};
    wawvr::mod::WeaponFrameBasePhaseComparison comparison{};
    check(wawvr::mod::select_weapon_aligned_scene_base(
              receipt, frame, receipt.tracking_anchor, scene_origin,
              scene_axis, &selected_origin, &selected_axis, &comparison),
          "exact eligible prediction selects the weapon base");
    check(comparison.valid && near(comparison.origin_delta_iw_units, 5.0F) &&
              near(comparison.forward_delta_degrees, 30.0F) &&
              near(comparison.left_delta_degrees, 30.0F) &&
              near(comparison.up_delta_degrees, 0.0F),
          "exact prediction reports origin and full-basis drift");
    check(selected_origin.x == receipt.camera_origin.x &&
              selected_origin.y == receipt.camera_origin.y &&
              selected_origin.z == receipt.camera_origin.z &&
              selected_axis.forward.x == receipt.body_axis.forward.x &&
              selected_axis.forward.y == receipt.body_axis.forward.y &&
              selected_axis.left.x == receipt.body_axis.left.x &&
              selected_axis.left.y == receipt.body_axis.left.y &&
              selected_axis.up.x == receipt.body_axis.up.x &&
              selected_axis.up.z == receipt.body_axis.up.z,
          "A/B output is the exact weapon-sampled base");
}

void test_ineligible_receipt_measures_without_overriding() {
    auto receipt = receipt_fixture();
    receipt.scene_base_lock_eligible = false;
    const auto frame = frame_fixture(receipt);
    const wawvr::xr::Vec3f scene_origin{13.0F, 24.0F, 30.0F};
    const auto scene_axis = yaw_basis(10.0F);
    wawvr::xr::Vec3f selected_origin{};
    wawvr::xr::Basis3f selected_axis{};
    wawvr::mod::WeaponFrameBasePhaseComparison comparison{};
    check(!wawvr::mod::select_weapon_aligned_scene_base(
              receipt, frame, receipt.tracking_anchor, scene_origin,
              scene_axis, &selected_origin, &selected_axis, &comparison),
          "ineligible receipt does not override the scene");
    check(comparison.valid && selected_origin.x == scene_origin.x &&
              selected_origin.y == scene_origin.y &&
              selected_origin.z == scene_origin.z &&
              selected_axis.forward.x == scene_axis.forward.x &&
              selected_axis.forward.y == scene_axis.forward.y,
          "ineligible receipt still measures while preserving stock base");
}

void test_identity_mismatch_and_invalid_data_fail_closed() {
    auto receipt = receipt_fixture();
    auto frame = frame_fixture(receipt);
    wawvr::xr::Vec3f selected_origin{};
    wawvr::xr::Basis3f selected_axis{};
    wawvr::mod::WeaponFrameBasePhaseComparison comparison{};
    const wawvr::xr::Vec3f stock_origin{7.0F, 8.0F, 9.0F};
    const wawvr::xr::Basis3f stock_axis{};
    const auto reject = [&](const char* const message) {
        check(!wawvr::mod::select_weapon_aligned_scene_base(
                  receipt, frame, receipt.tracking_anchor,
                  stock_origin, stock_axis, &selected_origin,
                  &selected_axis, &comparison), message);
        check(!comparison.valid && selected_origin.x == stock_origin.x &&
                  selected_origin.y == stock_origin.y &&
                  selected_origin.z == stock_origin.z &&
                  selected_axis.forward.x == stock_axis.forward.x &&
                  selected_axis.left.y == stock_axis.left.y &&
                  selected_axis.up.z == stock_axis.up.z,
              "rejection preserves the complete stock base");
    };

    frame.frame_id += 1;
    reject("wrong frame id is rejected");
    frame = frame_fixture(receipt);
    frame.predicted_display_time += 1;
    reject("wrong predicted display time is rejected");
    frame = frame_fixture(receipt);
    frame.actions.sequence += 1;
    reject("wrong action sequence is rejected");
    frame = frame_fixture(receipt);
    frame.head_center.position.x += 0.01F;
    reject("different head sample is rejected");
    frame = frame_fixture(receipt);
    auto anchor = receipt.tracking_anchor;
    anchor.position.y += 0.01F;
    check(!wawvr::mod::select_weapon_aligned_scene_base(
              receipt, frame, anchor, stock_origin, stock_axis,
              &selected_origin, &selected_axis, &comparison),
          "different tracking anchor is rejected");
    check(!comparison.valid && selected_origin.x == stock_origin.x &&
              selected_axis.forward.x == stock_axis.forward.x,
          "anchor rejection preserves the stock base");
    receipt.camera_origin.x = std::numeric_limits<float>::infinity();
    reject("non-finite receipt base is rejected");
}

void test_invalid_basis_and_orientation_are_rejected() {
    auto receipt = receipt_fixture();
    auto frame = frame_fixture(receipt);
    wawvr::xr::Vec3f selected_origin{};
    wawvr::xr::Basis3f selected_axis{};
    wawvr::mod::WeaponFrameBasePhaseComparison comparison{};
    receipt.body_axis.left = receipt.body_axis.forward;
    check(!wawvr::mod::select_weapon_aligned_scene_base(
              receipt, frame, receipt.tracking_anchor,
              {7.0F, 8.0F, 9.0F}, {}, &selected_origin,
              &selected_axis, &comparison),
          "collinear receipt basis is rejected");

    receipt = receipt_fixture();
    frame = frame_fixture(receipt);
    receipt.head_center.orientation = {};
    receipt.head_center.orientation.w = 0.0F;
    frame.head_center = receipt.head_center;
    check(!wawvr::mod::select_weapon_aligned_scene_base(
              receipt, frame, receipt.tracking_anchor,
              {7.0F, 8.0F, 9.0F}, {}, &selected_origin,
              &selected_axis, &comparison),
          "matching zero quaternions are rejected");
}

void test_quaternion_sign_equivalence_is_accepted() {
    auto receipt = receipt_fixture();
    receipt.tracking_anchor.orientation = {0.0F, 0.0F, 0.5F, 0.8660254F};
    auto frame = frame_fixture(receipt);
    auto anchor = receipt.tracking_anchor;
    anchor.orientation.x = -anchor.orientation.x;
    anchor.orientation.y = -anchor.orientation.y;
    anchor.orientation.z = -anchor.orientation.z;
    anchor.orientation.w = -anchor.orientation.w;
    frame.head_center.orientation.w = -1.0F;
    receipt.head_center.orientation.w = 1.0F;
    wawvr::mod::WeaponFrameBasePhaseComparison comparison{};
    check(wawvr::mod::compare_weapon_frame_base_to_scene(
              receipt, frame, anchor, receipt.camera_origin,
              receipt.body_axis, &comparison) && comparison.valid,
          "q and negative q identify the same immutable poses");
}

}  // namespace

int main() {
    test_exact_frame_measures_and_selects_weapon_base();
    test_ineligible_receipt_measures_without_overriding();
    test_identity_mismatch_and_invalid_data_fail_closed();
    test_invalid_basis_and_orientation_are_rejected();
    test_quaternion_sign_equivalence_is_accepted();
    return 0;
}
