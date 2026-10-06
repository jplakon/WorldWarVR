// SPDX-License-Identifier: GPL-3.0-only
#include "weapon_grip_logic.hpp"

#include <cmath>

namespace wawvr::mod {
namespace {

void update_latch(
    const bool active,
    const float value,
    const bool pose_usable,
    const std::uint64_t now_milliseconds,
    bool* const latched,
    bool* const tracking_unavailable,
    std::uint64_t* const last_usable_milliseconds,
    std::uint64_t* const tracking_unavailable_since,
    bool* const rearm_required) noexcept {
    if (latched == nullptr || tracking_unavailable == nullptr ||
        last_usable_milliseconds == nullptr ||
        tracking_unavailable_since == nullptr || rearm_required == nullptr) {
        return;
    }

    const bool readable_value = active && std::isfinite(value);
    if (readable_value && value < kWeaponGripRelease) {
        // A real, readable release is authoritative and immediate. It also
        // rearms a hand that was retired after a prolonged tracking outage.
        *latched = false;
        *tracking_unavailable = false;
        *tracking_unavailable_since = 0;
        *rearm_required = false;
        return;
    }

    if (*rearm_required) {
        *latched = false;
        return;
    }

    if (readable_value && pose_usable) {
        *tracking_unavailable = false;
        *last_usable_milliseconds = now_milliseconds;
        *tracking_unavailable_since = 0;
        *latched = *latched ? value >= kWeaponGripRelease
                            : value >= kWeaponGripEngage;
        return;
    }

    // Never acquire from an inactive action or an invalid pose. If an
    // already-held hand briefly loses either signal, retain logical ownership
    // while the renderer freezes the last complete weapon pose.
    if (!*latched) {
        *tracking_unavailable = false;
        *tracking_unavailable_since = 0;
        return;
    }
    if (!*tracking_unavailable) {
        *tracking_unavailable = true;
        *tracking_unavailable_since = *last_usable_milliseconds != 0
            ? *last_usable_milliseconds
            : now_milliseconds;
    }
    if (now_milliseconds < *tracking_unavailable_since ||
        now_milliseconds - *tracking_unavailable_since >
            kWeaponGripTrackingGraceMilliseconds) {
        *latched = false;
        *tracking_unavailable = false;
        *tracking_unavailable_since = 0;
        *rearm_required = true;
    }
}

}  // namespace

PostT4HeldPosePolicy post_t4_held_pose_policy(
    const WeaponGripMode mode,
    const bool transition_pending,
    const bool align_grip_tag_before_commit,
    const bool preserve_transferred_root) noexcept {
    const bool held = mode != WeaponGripMode::Chest;
    const bool establishing_attachment = held &&
        (transition_pending || align_grip_tag_before_commit);
    const bool preserving_pending_root = held && transition_pending &&
        preserve_transferred_root && !align_grip_tag_before_commit;
    return {
        .align_visible_grip =
            establishing_attachment && !preserving_pending_root,
        .reapply_controller_root =
            held && (!establishing_attachment || preserving_pending_root),
        .recapture_attachment = establishing_attachment,
    };
}

WeaponGripUpdate update_weapon_grip(
    const WeaponGripInput& input,
    WeaponGripState* const state) noexcept {
    if (state == nullptr) {
        return {};
    }
    if (!input.enabled || input.weapon_identity == 0) {
        reset_weapon_grip(state);
        return {state->mode, false, false, false, false, false};
    }
    if (!input.focused) {
        const bool right_was_latched = state->right_latched;
        const bool left_was_latched = state->left_latched;
        reset_weapon_grip(state);
        state->weapon_identity = input.weapon_identity;
        state->right_rearm_required = right_was_latched;
        state->left_rearm_required = left_was_latched;
        return {state->mode, false, false, false, false, false};
    }

    if (state->weapon_identity != 0 &&
        state->weapon_identity != input.weapon_identity) {
        // A two-hand support latch is meaningful only for the weapon on which
        // it was established. Rebaseline held controls on a weapon change so
        // a new scoped rifle cannot inherit another weapon's left-only optic.
        reset_weapon_grip(state);
    }
    state->weapon_identity = input.weapon_identity;

    const WeaponGripMode prior_mode = state->mode;
    update_latch(
        input.right_squeeze_active, input.right_squeeze,
        input.right_pose_usable, input.now_milliseconds,
        &state->right_latched, &state->right_tracking_unavailable,
        &state->right_last_usable_milliseconds,
        &state->right_tracking_unavailable_since,
        &state->right_rearm_required);
    update_latch(
        input.left_squeeze_active, input.left_squeeze,
        input.left_pose_usable, input.now_milliseconds,
        &state->left_latched, &state->left_tracking_unavailable,
        &state->left_last_usable_milliseconds,
        &state->left_tracking_unavailable_since,
        &state->left_rearm_required);
    const bool physical_right_latched = state->right_latched;
    const bool physical_left_latched = state->left_latched;
    const bool preserve_existing_left_owner =
        state->mode == WeaponGripMode::LeftHand &&
        state->left_latched;
    const bool preserve_existing_right_owner =
        state->mode == WeaponGripMode::RightHand &&
        state->right_latched;
    state->left_latched =
        input.left_interaction_reserved && state->right_latched &&
            preserve_existing_right_owner
        ? false
        : physical_left_latched;
    state->right_latched =
        input.right_interaction_reserved && state->left_latched &&
            preserve_existing_left_owner
        ? false
        : physical_right_latched;

    if (state->right_latched && state->left_latched) {
        state->mode = WeaponGripMode::TwoHand;
    } else if (state->right_latched) {
        state->mode = WeaponGripMode::RightHand;
    } else if (state->left_latched) {
        state->mode = WeaponGripMode::LeftHand;
    } else {
        state->mode = WeaponGripMode::Chest;
    }
    state->initialized = true;

    // Both hands enter T4's authored ADS/support pose. If the right hand then
    // releases, keep that pose while the left hand owns the rifle so the game
    // does not lift and roll the viewmodel during a bolt-manipulation handoff.
    // Releasing the left support hand or holstering ends the latch. A direct
    // left-only pickup from the chest therefore remains a normal hip pickup.
    if (state->mode == WeaponGripMode::Chest || !state->left_latched) {
        state->support_pose_latched = false;
    } else if (state->mode == WeaponGripMode::TwoHand) {
        state->support_pose_latched = true;
    }

    const bool restore_saved_right_attachment =
        prior_mode == WeaponGripMode::TwoHand &&
        state->mode == WeaponGripMode::RightHand &&
        state->right_latched && !state->left_latched;

    // A fresh right grip takes pose ownership back from a left-retained rifle.
    // Preserve that direct transfer root until holster/reset. Pair-to-right is
    // different: releasing the support hand must restore the saved native
    // right aim immediately rather than retaining the last pair direction.
    if (state->mode == WeaponGripMode::Chest) {
        state->preserve_right_handoff_root = false;
    } else if (restore_saved_right_attachment) {
        state->preserve_right_handoff_root = false;
    } else if (prior_mode != WeaponGripMode::Chest &&
               prior_mode != WeaponGripMode::RightHand &&
               state->mode == WeaponGripMode::RightHand &&
               state->right_latched) {
        state->preserve_right_handoff_root = true;
    }

    return {
        state->mode,
        state->mode != prior_mode,
        state->right_latched,
        state->left_latched,
        state->support_pose_latched,
        state->preserve_right_handoff_root,
    };
}

void reset_weapon_grip(WeaponGripState* const state) noexcept {
    if (state != nullptr) {
        *state = {};
    }
}

bool retain_weapon_pose_during_tracking_failure(
    const std::uint64_t now_milliseconds,
    std::uint64_t* const failure_since_milliseconds) noexcept {
    if (failure_since_milliseconds == nullptr) {
        return false;
    }
    if (*failure_since_milliseconds == 0) {
        *failure_since_milliseconds = now_milliseconds;
        return true;
    }
    return now_milliseconds >= *failure_since_milliseconds &&
        now_milliseconds - *failure_since_milliseconds <=
            kWeaponGripTrackingGraceMilliseconds;
}

void reset_weapon_pose_tracking_failure(
    std::uint64_t* const failure_since_milliseconds) noexcept {
    if (failure_since_milliseconds != nullptr) {
        *failure_since_milliseconds = 0;
    }
}

}  // namespace wawvr::mod
