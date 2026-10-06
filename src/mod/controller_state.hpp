// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "xr_types.h"

#include <cstdint>
#include <array>

namespace wawvr::mod {

// One immutable hand-off from the OpenXR render thread to T4's command-build
// thread. The tracking anchor is the same frozen pose used by stereo, so both
// systems evaluate H_anchor^-1 * tracked_pose in the same coordinate frame.
struct PublishedControllerWeaponPose final {
    bool valid{};
    wawvr::xr::Vec3f filtered_grip_position{};
    wawvr::xr::Quaternionf filtered_aim_orientation{};
    // This companion publication is filtered only after the exact frame's
    // head pose has been removed. It is intentionally kept alongside the
    // established tracking-anchor publication so the diagnostic path can be
    // selected without changing hands, reloads, or stereo consumers.
    bool current_head_local_valid{};
    wawvr::xr::EnginePose filtered_current_head_local_pose{};
};

struct ControllerFrameSnapshot final {
    wawvr::xr::FrameState frame{};
    wawvr::xr::Posef tracking_anchor{};
    std::uint64_t publication_milliseconds{};
    std::uint64_t generation{};
    std::array<PublishedControllerWeaponPose, wawvr::xr::kHandCount>
        weapon_poses{};
};

// Publishes the predicted frame that will be consumed by the corresponding
// gameplay render. Safe to call from the OpenXR/Present thread.
void publish_controller_frame(
    const wawvr::xr::FrameState& frame,
    const wawvr::xr::Posef& tracking_anchor) noexcept;

// Invalidates the current frame during XR loss or before a tracking anchor
// exists. Readers fail closed until a newer frame is published. A queued body
// yaw transfer survives temporary frame loss so Present can still apply it to
// its long-lived anchor after D3D/OpenXR recovery.
void clear_controller_frame() noexcept;

// Authorizes the next valid publication to seed a fresh controller weapon
// baseline. Gameplay calls this only after its bounded held-pose recovery has
// retired the weapon; ordinary invalid publications preserve accepted history.
void reset_controller_weapon_publication_filters() noexcept;

// Full XR teardown discards any queued transfer because a later session will
// capture a new tracking anchor from the then-current HMD pose.
void discard_controller_tracking_anchor_rebase() noexcept;

// Takes a consistent copy for the game thread. The caller still owns focus,
// pose-validity, finite-value, and staleness checks.
[[nodiscard]] bool read_controller_frame(
    ControllerFrameSnapshot* snapshot) noexcept;

// Replaces only the yaw-only tracking-anchor orientation for the exact frame
// generation consumed by the game thread. This lets a physical HMD turn be
// transferred into T4's hidden body yaw without changing room-scale position.
// The expected orientation makes stale publications fail closed.
[[nodiscard]] bool rebase_controller_frame_tracking_anchor(
    std::uint64_t expected_generation,
    std::uint64_t expected_frame_id,
    const wawvr::xr::Quaternionf& expected_orientation,
    const wawvr::xr::Quaternionf& desired_orientation) noexcept;

// Present consumes the same body-yaw transfer before publishing the following
// predicted frame, keeping its private long-lived tracking anchor in sync.
[[nodiscard]] bool consume_controller_tracking_anchor_rebase(
    wawvr::xr::Quaternionf* desired_orientation) noexcept;

}  // namespace wawvr::mod
