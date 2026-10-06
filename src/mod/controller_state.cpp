// SPDX-License-Identifier: GPL-3.0-only
#include "controller_state.hpp"
#include "current_head_local_controller_pose_logic.hpp"
#include "tracking_anchor_sync.hpp"
#include "weapon_placement.hpp"

#include <windows.h>

#include <cstring>
#include <algorithm>
#include <array>
#include <cmath>
#include <type_traits>

namespace wawvr::mod {
namespace {

static_assert(std::is_trivially_copyable_v<ControllerFrameSnapshot>);

SRWLOCK g_controller_frame_lock = SRWLOCK_INIT;
ControllerFrameSnapshot g_controller_frame{};
bool g_controller_frame_available = false;
std::uint64_t g_controller_frame_generation = 0;
wawvr::xr::Quaternionf g_pending_tracking_anchor_orientation{};
bool g_tracking_anchor_rebase_pending = false;
std::array<ControllerWeaponPoseFilterState, wawvr::xr::kHandCount>
    g_publication_weapon_filters{};
std::array<CurrentHeadLocalControllerPoseFilterState, wawvr::xr::kHandCount>
    g_current_head_local_publication_weapon_filters{};

bool finite(const float value) noexcept { return std::isfinite(value); }

bool normalize(wawvr::xr::Quaternionf* const value) noexcept {
    if (value == nullptr || !finite(value->x) || !finite(value->y) ||
        !finite(value->z) || !finite(value->w)) return false;
    const float length_squared = value->x * value->x + value->y * value->y +
        value->z * value->z + value->w * value->w;
    if (!finite(length_squared) || length_squared <= 1.0e-8F) return false;
    const float inverse_length = 1.0F / std::sqrt(length_squared);
    value->x *= inverse_length; value->y *= inverse_length;
    value->z *= inverse_length; value->w *= inverse_length;
    return true;
}

[[nodiscard]] bool current_head_local_frame_contract_valid(
    const wawvr::xr::FrameState& frame) noexcept {
    auto head_orientation = frame.head_center.orientation;
    return frame.frame_id != 0 && frame.actions.sequence != 0 &&
        frame.actions.focused && frame.views_valid &&
        finite(frame.head_center.position.x) &&
        finite(frame.head_center.position.y) &&
        finite(frame.head_center.position.z) &&
        normalize(&head_orientation);
}

bool update_publication_weapon_pose(
    const wawvr::xr::HandActionState& hand, const std::uint64_t generation,
    const std::uint64_t publication_milliseconds,
    ControllerWeaponPoseFilterState* const state,
    PublishedControllerWeaponPose* const output) noexcept {
    if (state == nullptr || output == nullptr || generation == 0 ||
        !hand.grip.active || !hand.grip.position_valid ||
        !hand.aim.active || !hand.aim.orientation_valid ||
        !finite(hand.grip.pose.position.x) ||
        !finite(hand.grip.pose.position.y) ||
        !finite(hand.grip.pose.position.z)) return false;
    auto target = hand.aim.pose.orientation;
    if (!normalize(&target)) return false;

    auto next = *state;
    if (!next.valid) {
        next.valid = true; next.generation = generation;
        next.publication_milliseconds = publication_milliseconds;
        next.filtered_grip_position = hand.grip.pose.position;
        next.filtered_aim_orientation = target;
    } else {
        const auto delta = wawvr::xr::Vec3f{
            hand.grip.pose.position.x - next.filtered_grip_position.x,
            hand.grip.pose.position.y - next.filtered_grip_position.y,
            hand.grip.pose.position.z - next.filtered_grip_position.z};
        const float distance_squared =
            delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
        const float raw_dot = std::abs(
            next.filtered_aim_orientation.x * target.x +
            next.filtered_aim_orientation.y * target.y +
            next.filtered_aim_orientation.z * target.z +
            next.filtered_aim_orientation.w * target.w);
        constexpr float kRadiansToDegrees = 57.2957795131F;
        const float angle = 2.0F *
            std::acos(std::clamp(raw_dot, 0.0F, 1.0F)) * kRadiansToDegrees;
        if (!finite(distance_squared) || distance_squared >
                kMaximumWeaponPositionDiscontinuityMeters *
                kMaximumWeaponPositionDiscontinuityMeters ||
            !finite(angle) ||
            angle > kMaximumWeaponOrientationDiscontinuityDegrees) return false;
        const float position_response = adaptive_weapon_position_response(
            std::sqrt(distance_squared));
        const float orientation_response =
            adaptive_weapon_orientation_response(angle);
        next.filtered_grip_position.x += delta.x * position_response;
        next.filtered_grip_position.y += delta.y * position_response;
        next.filtered_grip_position.z += delta.z * position_response;
        const float dot = next.filtered_aim_orientation.x * target.x +
            next.filtered_aim_orientation.y * target.y +
            next.filtered_aim_orientation.z * target.z +
            next.filtered_aim_orientation.w * target.w;
        if (dot < 0.0F) { target.x=-target.x; target.y=-target.y;
            target.z=-target.z; target.w=-target.w; }
        next.filtered_aim_orientation.x +=
            (target.x-next.filtered_aim_orientation.x)*orientation_response;
        next.filtered_aim_orientation.y +=
            (target.y-next.filtered_aim_orientation.y)*orientation_response;
        next.filtered_aim_orientation.z +=
            (target.z-next.filtered_aim_orientation.z)*orientation_response;
        next.filtered_aim_orientation.w +=
            (target.w-next.filtered_aim_orientation.w)*orientation_response;
        if (!normalize(&next.filtered_aim_orientation)) return false;
        next.generation = generation;
        next.publication_milliseconds = publication_milliseconds;
    }
    *state = next;
    output->valid = true;
    output->filtered_grip_position = next.filtered_grip_position;
    output->filtered_aim_orientation = next.filtered_aim_orientation;
    return true;
}

}  // namespace

void publish_controller_frame(
    const wawvr::xr::FrameState& frame,
    const wawvr::xr::Posef& tracking_anchor) noexcept {
    ControllerFrameSnapshot next{};
    next.frame = frame;
    next.tracking_anchor = tracking_anchor;
    next.publication_milliseconds = GetTickCount64();

    TrackingAnchorSyncLock sync(tracking_anchor_sync_mutex());
    AcquireSRWLockExclusive(&g_controller_frame_lock);
    next.generation = ++g_controller_frame_generation;
    const bool current_head_local_frame_ready =
        current_head_local_frame_contract_valid(frame);
    for (std::uint32_t index = 0; index < wawvr::xr::kHandCount; ++index) {
        static_cast<void>(update_publication_weapon_pose(
            frame.actions.hands[index], next.generation,
            next.publication_milliseconds, &g_publication_weapon_filters[index],
            &next.weapon_poses[index]));
        if (current_head_local_frame_ready) {
            auto& current_head_local_filter =
                g_current_head_local_publication_weapon_filters[index];
            if (current_head_local_filter.valid &&
                next.publication_milliseconds >=
                    current_head_local_filter.publication_milliseconds &&
                next.publication_milliseconds -
                        current_head_local_filter.publication_milliseconds >
                    kCurrentHeadLocalMaximumFrameAgeMilliseconds) {
                // A paused/lost XR session has no continuous pose history.
                // Explicitly reseed on the first complete frame instead of
                // permanently rejecting every later generation as stale.
                reset_current_head_local_controller_pose_filter(
                    &current_head_local_filter);
            }
            wawvr::xr::EnginePose current_head_local{};
            if (filtered_current_head_local_controller_pose(
                    frame.head_center, frame.actions.hands[index],
                    next.generation, next.publication_milliseconds,
                    next.publication_milliseconds,
                    &current_head_local_filter, &current_head_local)) {
                next.weapon_poses[index].current_head_local_valid = true;
                next.weapon_poses[index].filtered_current_head_local_pose =
                    current_head_local;
            }
        }
    }
    g_controller_frame = next;
    g_controller_frame_available = true;
    ReleaseSRWLockExclusive(&g_controller_frame_lock);
}

void clear_controller_frame() noexcept {
    TrackingAnchorSyncLock sync(tracking_anchor_sync_mutex());
    AcquireSRWLockExclusive(&g_controller_frame_lock);
    ++g_controller_frame_generation;
    g_controller_frame = {};
    g_controller_frame.generation = g_controller_frame_generation;
    g_controller_frame_available = false;
    g_current_head_local_publication_weapon_filters = {};
    ReleaseSRWLockExclusive(&g_controller_frame_lock);
}

void reset_controller_weapon_publication_filters() noexcept {
    TrackingAnchorSyncLock sync(tracking_anchor_sync_mutex());
    AcquireSRWLockExclusive(&g_controller_frame_lock);
    g_publication_weapon_filters = {};
    g_current_head_local_publication_weapon_filters = {};
    ReleaseSRWLockExclusive(&g_controller_frame_lock);
}

void discard_controller_tracking_anchor_rebase() noexcept {
    TrackingAnchorSyncLock sync(tracking_anchor_sync_mutex());
    AcquireSRWLockExclusive(&g_controller_frame_lock);
    g_pending_tracking_anchor_orientation = {};
    g_tracking_anchor_rebase_pending = false;
    g_publication_weapon_filters = {};
    g_current_head_local_publication_weapon_filters = {};
    ReleaseSRWLockExclusive(&g_controller_frame_lock);
}

bool read_controller_frame(ControllerFrameSnapshot* const snapshot) noexcept {
    if (snapshot == nullptr) {
        return false;
    }

    TrackingAnchorSyncLock sync(tracking_anchor_sync_mutex());
    AcquireSRWLockShared(&g_controller_frame_lock);
    const bool available = g_controller_frame_available;
    if (available) {
        *snapshot = g_controller_frame;
    }
    ReleaseSRWLockShared(&g_controller_frame_lock);
    return available;
}

bool rebase_controller_frame_tracking_anchor(
    const std::uint64_t expected_generation,
    const std::uint64_t expected_frame_id,
    const wawvr::xr::Quaternionf& expected_orientation,
    const wawvr::xr::Quaternionf& desired_orientation) noexcept {
    TrackingAnchorSyncLock sync(tracking_anchor_sync_mutex());
    AcquireSRWLockExclusive(&g_controller_frame_lock);
    const bool matches = g_controller_frame_available &&
        g_controller_frame.generation == expected_generation &&
        g_controller_frame.frame.frame_id == expected_frame_id &&
        std::memcmp(
            &g_controller_frame.tracking_anchor.orientation,
            &expected_orientation, sizeof(expected_orientation)) == 0;
    if (matches) {
        g_controller_frame.tracking_anchor.orientation = desired_orientation;
        g_pending_tracking_anchor_orientation = desired_orientation;
        g_tracking_anchor_rebase_pending = true;
    }
    ReleaseSRWLockExclusive(&g_controller_frame_lock);
    return matches;
}

bool consume_controller_tracking_anchor_rebase(
    wawvr::xr::Quaternionf* const desired_orientation) noexcept {
    if (desired_orientation == nullptr) {
        return false;
    }
    TrackingAnchorSyncLock sync(tracking_anchor_sync_mutex());
    AcquireSRWLockExclusive(&g_controller_frame_lock);
    const bool pending = g_tracking_anchor_rebase_pending;
    if (pending) {
        *desired_orientation = g_pending_tracking_anchor_orientation;
        g_pending_tracking_anchor_orientation = {};
        g_tracking_anchor_rebase_pending = false;
    }
    ReleaseSRWLockExclusive(&g_controller_frame_lock);
    return pending;
}

}  // namespace wawvr::mod
