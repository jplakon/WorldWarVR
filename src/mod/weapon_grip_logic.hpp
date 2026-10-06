// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>

namespace wawvr::mod {

enum class WeaponGripMode : std::uint8_t {
    Chest,
    RightHand,
    LeftHand,
    // Both physical grips contribute to one synthetic rifle frame. The right
    // hand is the rear anchor while the left hand steers the barrel, matching
    // COD4 VR's accepted two-hand contract without giving either controller
    // exclusive ownership.
    TwoHand,
};

struct WeaponGripInput final {
    bool enabled{};
    bool focused{};
    // Opaque identity for the weapon currently being placed. Zero means no
    // weapon is available and clears all retained ownership/support state.
    std::uint64_t weapon_identity{};
    // Monotonic host time used only to bridge brief OpenXR action/pose
    // publication gaps. It never delays a readable physical release.
    std::uint64_t now_milliseconds{};
    bool right_squeeze_active{};
    float right_squeeze{};
    bool right_pose_usable{};
    bool left_squeeze_active{};
    float left_squeeze{};
    bool left_pose_usable{};
    // A physical bolt/clip interaction may consume right squeeze while the
    // left hand retains the rifle. It never masks the only hand holding it.
    bool right_interaction_reserved{};
    // A detachable-magazine transaction owns left squeeze from belt draw
    // through insertion. Suppress support/pickup latching while another hand
    // owns the weapon, but never make an existing left-only owner drop it.
    bool left_interaction_reserved{};
};

struct WeaponGripState final {
    WeaponGripMode mode{WeaponGripMode::Chest};
    std::uint64_t weapon_identity{};
    bool right_latched{};
    bool left_latched{};
    bool support_pose_latched{};
    // Once a direct left-to-right handoff leaves a single right owner, the
    // exact calibrated root remains authoritative until holster/reset. A
    // pair-to-right support release instead restores native right aim.
    bool preserve_right_handoff_root{};
    bool right_tracking_unavailable{};
    bool left_tracking_unavailable{};
    std::uint64_t right_last_usable_milliseconds{};
    std::uint64_t left_last_usable_milliseconds{};
    std::uint64_t right_tracking_unavailable_since{};
    std::uint64_t left_tracking_unavailable_since{};
    // A hand whose tracking disappeared for longer than the recovery window
    // must be physically released before it may acquire the rifle again. This
    // prevents a relocalized held controller from snapping the weapon across
    // the player's view on the first recovered sample.
    bool right_rearm_required{};
    bool left_rearm_required{};
    bool initialized{};
};

struct WeaponGripUpdate final {
    WeaponGripMode mode{WeaponGripMode::Chest};
    bool changed{};
    bool right_latched{};
    bool left_latched{};
    bool support_pose{};
    bool preserve_right_handoff_root{};
};

// T4 evaluates authored viewmodel animation after the controller placement
// hook. Match COD4's accepted contract: align the evaluated grip tag and
// recapture the persistent attachment only while establishing a validated
// ownership transition. An exact controller-to-controller handoff may instead
// preserve its already-transferred visible root while recapturing the incoming
// attachment. Every steady held frame reapplies the immutable controller root
// so authored tag animation cannot become whole-rifle motion.
struct PostT4HeldPosePolicy final {
    bool align_visible_grip{};
    bool reapply_controller_root{};
    bool recapture_attachment{};
};

[[nodiscard]] PostT4HeldPosePolicy post_t4_held_pose_policy(
    WeaponGripMode mode,
    bool transition_pending,
    bool align_grip_tag_before_commit,
    bool preserve_transferred_root) noexcept;

constexpr float kWeaponGripEngage = 0.65F;
constexpr float kWeaponGripRelease = 0.35F;
inline constexpr std::uint64_t kWeaponGripTrackingGraceMilliseconds = 250;
[[nodiscard]] WeaponGripUpdate update_weapon_grip(
    const WeaponGripInput& input,
    WeaponGripState* state) noexcept;

void reset_weapon_grip(WeaponGripState* state) noexcept;

// Bounded renderer-side fallback for a committed held pose whose fresh
// controller geometry was rejected. The first failure opens the same recovery
// window as grip tracking loss; clock regression fails closed.
[[nodiscard]] bool retain_weapon_pose_during_tracking_failure(
    std::uint64_t now_milliseconds,
    std::uint64_t* failure_since_milliseconds) noexcept;

void reset_weapon_pose_tracking_failure(
    std::uint64_t* failure_since_milliseconds) noexcept;

}  // namespace wawvr::mod
