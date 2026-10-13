// SPDX-License-Identifier: GPL-3.0-only
#include "tracked_attachment_math.hpp"

#include <cmath>

namespace wawvr::gameplay {
namespace {

constexpr float kQuaternionNormFloor = 1.0e-12F;

[[nodiscard]] bool finite(const float value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool finite(const TrackedAttachmentVector3& value) noexcept {
    return finite(value.x) && finite(value.y) && finite(value.z);
}

[[nodiscard]] bool normalize(
    const TrackedAttachmentQuaternion& value,
    TrackedAttachmentQuaternion* const normalized) noexcept {
    if (normalized == nullptr || !finite(value.x) || !finite(value.y) ||
        !finite(value.z) || !finite(value.w)) {
        return false;
    }
    const float norm_squared = value.x * value.x + value.y * value.y +
                               value.z * value.z + value.w * value.w;
    if (!finite(norm_squared) || norm_squared < kQuaternionNormFloor) {
        return false;
    }
    const float inverse_norm = 1.0F / std::sqrt(norm_squared);
    *normalized = {
        value.x * inverse_norm,
        value.y * inverse_norm,
        value.z * inverse_norm,
        value.w * inverse_norm,
    };
    return finite(normalized->x) && finite(normalized->y) &&
           finite(normalized->z) && finite(normalized->w);
}

[[nodiscard]] TrackedAttachmentQuaternion conjugate(
    const TrackedAttachmentQuaternion& value) noexcept {
    return {-value.x, -value.y, -value.z, value.w};
}

[[nodiscard]] TrackedAttachmentQuaternion multiply(
    const TrackedAttachmentQuaternion& left,
    const TrackedAttachmentQuaternion& right) noexcept {
    return {
        left.w * right.x + left.x * right.w + left.y * right.z -
            left.z * right.y,
        left.w * right.y - left.x * right.z + left.y * right.w +
            left.z * right.x,
        left.w * right.z + left.x * right.y - left.y * right.x +
            left.z * right.w,
        left.w * right.w - left.x * right.x - left.y * right.y -
            left.z * right.z,
    };
}

[[nodiscard]] TrackedAttachmentVector3 subtract(
    const TrackedAttachmentVector3& left,
    const TrackedAttachmentVector3& right) noexcept {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] TrackedAttachmentVector3 add(
    const TrackedAttachmentVector3& left,
    const TrackedAttachmentVector3& right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] TrackedAttachmentVector3 cross(
    const TrackedAttachmentVector3& left,
    const TrackedAttachmentVector3& right) noexcept {
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] TrackedAttachmentVector3 scale(
    const TrackedAttachmentVector3& value,
    const float factor) noexcept {
    return {value.x * factor, value.y * factor, value.z * factor};
}

[[nodiscard]] TrackedAttachmentVector3 rotate(
    const TrackedAttachmentQuaternion& orientation,
    const TrackedAttachmentVector3& value) noexcept {
    const TrackedAttachmentVector3 quaternion_vector{
        orientation.x,
        orientation.y,
        orientation.z,
    };
    const auto first_cross = cross(quaternion_vector, value);
    const auto second_cross = cross(quaternion_vector, first_cross);
    return add(
        value,
        add(scale(first_cross, 2.0F * orientation.w),
            scale(second_cross, 2.0F)));
}

[[nodiscard]] bool normalize_pose(
    const TrackedAttachmentPose& value,
    TrackedAttachmentPose* const normalized) noexcept {
    if (normalized == nullptr || !finite(value.position)) {
        return false;
    }
    normalized->position = value.position;
    return normalize(value.orientation, &normalized->orientation);
}

}  // namespace

bool calculate_tracked_hand_attachment(
    const TrackedAttachmentPose& tracked_hand_world,
    const TrackedAttachmentPose& device_world,
    TrackedAttachmentPose* const hand_to_device) noexcept {
    if (hand_to_device == nullptr) {
        return false;
    }

    TrackedAttachmentPose hand{};
    TrackedAttachmentPose device{};
    if (!normalize_pose(tracked_hand_world, &hand) ||
        !normalize_pose(device_world, &device)) {
        return false;
    }

    const auto inverse_hand = conjugate(hand.orientation);
    TrackedAttachmentPose result{};
    result.position =
        rotate(inverse_hand, subtract(device.position, hand.position));
    const auto relative_orientation =
        multiply(inverse_hand, device.orientation);
    if (!finite(result.position) ||
        !normalize(relative_orientation, &result.orientation)) {
        return false;
    }
    *hand_to_device = result;
    return true;
}

bool resolve_tracked_attachment_pose(
    const TrackedAttachmentPose& current_tracked_hand_world,
    const TrackedAttachmentPose& hand_to_device,
    const TrackedAttachmentQuaternion* const
        insertion_guide_world_orientation,
    TrackedAttachmentPose* const device_world) noexcept {
    if (device_world == nullptr) {
        return false;
    }

    TrackedAttachmentPose hand{};
    TrackedAttachmentPose attachment{};
    if (!normalize_pose(current_tracked_hand_world, &hand) ||
        !normalize_pose(hand_to_device, &attachment)) {
        return false;
    }

    TrackedAttachmentPose result{};
    result.position = add(
        hand.position,
        rotate(hand.orientation, attachment.position));
    const auto world_orientation =
        multiply(hand.orientation, attachment.orientation);
    if (!finite(result.position) ||
        !normalize(world_orientation, &result.orientation)) {
        return false;
    }

    if (insertion_guide_world_orientation != nullptr &&
        !normalize(*insertion_guide_world_orientation,
                   &result.orientation)) {
        return false;
    }

    *device_world = result;
    return true;
}

}  // namespace wawvr::gameplay
