// SPDX-License-Identifier: GPL-3.0-only
#include "tracked_attachment_math.hpp"

#include <cmath>
#include <iostream>
#include <limits>

namespace {

using wawvr::gameplay::TrackedAttachmentPose;
using wawvr::gameplay::TrackedAttachmentQuaternion;
using wawvr::gameplay::calculate_tracked_hand_attachment;
using wawvr::gameplay::resolve_tracked_attachment_pose;

constexpr float kPi = 3.14159265358979323846F;
int failures = 0;

void expect(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] bool near(
    const float left,
    const float right,
    const float epsilon = 1.0e-4F) {
    return std::fabs(left - right) <= epsilon;
}

[[nodiscard]] TrackedAttachmentQuaternion yaw_degrees(
    const float degrees) {
    const float half_radians = degrees * kPi / 360.0F;
    return {0.0F, 0.0F, std::sin(half_radians),
            std::cos(half_radians)};
}

void test_calibrated_attachment_follows_current_hand_translation() {
    const TrackedAttachmentPose calibration_hand{
        {10.0F, 2.0F, 3.0F},
        {},
    };
    const TrackedAttachmentPose calibration_device{
        {11.0F, 4.0F, 6.0F},
        {},
    };
    TrackedAttachmentPose attachment{};
    expect(calculate_tracked_hand_attachment(
               calibration_hand, calibration_device, &attachment),
           "finite calibration produces a hand-local attachment");
    expect(near(attachment.position.x, 1.0F) &&
               near(attachment.position.y, 2.0F) &&
               near(attachment.position.z, 3.0F),
           "identity calibration preserves the device offset");

    const TrackedAttachmentPose current_hand{
        {20.0F, 7.0F, -4.0F},
        {},
    };
    TrackedAttachmentPose resolved{};
    expect(resolve_tracked_attachment_pose(
               current_hand, attachment, nullptr, &resolved),
           "current tracked hand resolves the held device");
    expect(near(resolved.position.x, 21.0F) &&
               near(resolved.position.y, 9.0F) &&
               near(resolved.position.z, -1.0F),
           "device translates in the same direction and amount as the hand");
}

void test_attachment_rotates_in_hand_local_space() {
    TrackedAttachmentPose hand{};
    hand.orientation = yaw_degrees(90.0F);
    TrackedAttachmentPose attachment{};
    attachment.position = {1.0F, 0.0F, 0.0F};
    TrackedAttachmentPose resolved{};
    expect(resolve_tracked_attachment_pose(
               hand, attachment, nullptr, &resolved),
           "rotated tracked hand resolves a finite pose");
    expect(near(resolved.position.x, 0.0F) &&
               near(resolved.position.y, 1.0F) &&
               near(resolved.position.z, 0.0F),
           "hand rotation rotates the calibrated local device offset");
}

void test_calibration_round_trip_restores_device_pose() {
    TrackedAttachmentPose hand{};
    hand.position = {4.0F, -2.0F, 7.0F};
    hand.orientation = yaw_degrees(-70.0F);
    TrackedAttachmentPose device{};
    device.position = {-3.0F, 8.0F, 2.0F};
    device.orientation = yaw_degrees(25.0F);
    TrackedAttachmentPose attachment{};
    TrackedAttachmentPose restored{};
    expect(calculate_tracked_hand_attachment(hand, device, &attachment) &&
               resolve_tracked_attachment_pose(
                   hand, attachment, nullptr, &restored),
           "calibration and resolution form a valid round trip");
    expect(near(restored.position.x, device.position.x) &&
               near(restored.position.y, device.position.y) &&
               near(restored.position.z, device.position.z),
           "round trip restores calibrated device position");
    const float orientation_dot =
        std::fabs(restored.orientation.x * device.orientation.x +
                  restored.orientation.y * device.orientation.y +
                  restored.orientation.z * device.orientation.z +
                  restored.orientation.w * device.orientation.w);
    expect(near(orientation_dot, 1.0F),
           "round trip restores calibrated device orientation");
}

void test_insertion_guide_changes_only_orientation() {
    TrackedAttachmentPose hand{};
    hand.position = {5.0F, 6.0F, 7.0F};
    hand.orientation = yaw_degrees(30.0F);
    TrackedAttachmentPose attachment{};
    attachment.position = {0.2F, -0.4F, 0.6F};
    attachment.orientation = yaw_degrees(-10.0F);
    TrackedAttachmentPose free_pose{};
    TrackedAttachmentPose guided_pose{};
    const auto guide = yaw_degrees(120.0F);
    expect(resolve_tracked_attachment_pose(
               hand, attachment, nullptr, &free_pose) &&
               resolve_tracked_attachment_pose(
                   hand, attachment, &guide, &guided_pose),
           "free and insertion-guided poses both resolve");
    expect(near(free_pose.position.x, guided_pose.position.x) &&
               near(free_pose.position.y, guided_pose.position.y) &&
               near(free_pose.position.z, guided_pose.position.z),
           "insertion guide cannot snap or mirror device position");
    const float guide_dot =
        std::fabs(guided_pose.orientation.x * guide.x +
                  guided_pose.orientation.y * guide.y +
                  guided_pose.orientation.z * guide.z +
                  guided_pose.orientation.w * guide.w);
    expect(near(guide_dot, 1.0F),
           "insertion guide supplies only the resolved orientation");
}

void test_invalid_inputs_fail_without_overwriting_output() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    TrackedAttachmentPose hand{};
    TrackedAttachmentPose attachment{};
    TrackedAttachmentPose output{};
    output.position = {9.0F, 8.0F, 7.0F};

    hand.position.x = nan;
    expect(!resolve_tracked_attachment_pose(
               hand, attachment, nullptr, &output) &&
               near(output.position.x, 9.0F),
           "nonfinite hand pose fails closed without partial output");

    hand = {};
    attachment.orientation = {0.0F, 0.0F, 0.0F, 0.0F};
    expect(!resolve_tracked_attachment_pose(
               hand, attachment, nullptr, &output) &&
               near(output.position.x, 9.0F),
           "degenerate attachment orientation fails closed");

    attachment = {};
    TrackedAttachmentQuaternion guide{};
    guide.w = nan;
    expect(!resolve_tracked_attachment_pose(
               hand, attachment, &guide, &output) &&
               near(output.position.x, 9.0F),
           "invalid insertion guide cannot corrupt a valid free pose");

    expect(!calculate_tracked_hand_attachment(
               hand, attachment, nullptr) &&
               !resolve_tracked_attachment_pose(
                   hand, attachment, nullptr, nullptr),
           "null outputs fail closed");
}

}  // namespace

int main() {
    test_calibrated_attachment_follows_current_hand_translation();
    test_attachment_rotates_in_hand_local_space();
    test_calibration_round_trip_restores_device_pose();
    test_insertion_guide_changes_only_orientation();
    test_invalid_inputs_fail_without_overwriting_output();

    if (failures != 0) {
        std::cerr << failures << " tracked-attachment math test(s) failed\n";
        return 1;
    }
    std::cout << "tracked-attachment math tests passed\n";
    return 0;
}
