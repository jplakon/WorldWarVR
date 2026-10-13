// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace wawvr::gameplay {

struct TrackedAttachmentVector3 final {
    float x{};
    float y{};
    float z{};
};

struct TrackedAttachmentQuaternion final {
    float x{};
    float y{};
    float z{};
    float w{1.0F};
};

struct TrackedAttachmentPose final {
    TrackedAttachmentVector3 position{};
    TrackedAttachmentQuaternion orientation{};
};

// Captures a fixed device transform in tracked-hand-local space. This is a
// calibration operation, not a frame-to-frame tracking operation.
[[nodiscard]] bool calculate_tracked_hand_attachment(
    const TrackedAttachmentPose& tracked_hand_world,
    const TrackedAttachmentPose& device_world,
    TrackedAttachmentPose* hand_to_device) noexcept;

// Resolves the device from the CURRENT tracked hand every call. A non-null
// insertion-guide orientation may replace device orientation, but position
// always remains the result of current_hand * calibrated_attachment.
[[nodiscard]] bool resolve_tracked_attachment_pose(
    const TrackedAttachmentPose& current_tracked_hand_world,
    const TrackedAttachmentPose& hand_to_device,
    const TrackedAttachmentQuaternion* insertion_guide_world_orientation,
    TrackedAttachmentPose* device_world) noexcept;

}  // namespace wawvr::gameplay
