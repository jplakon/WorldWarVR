// SPDX-License-Identifier: GPL-3.0-only
#include "weapon_grip_logic.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void expect(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    using namespace wawvr::mod;

    const auto chest_post = post_t4_held_pose_policy(
        WeaponGripMode::Chest, false, false, false);
    expect(!chest_post.align_visible_grip &&
               !chest_post.reapply_controller_root &&
               !chest_post.recapture_attachment,
           "a chest weapon has no controller-owned post-T4 root");

    for (const WeaponGripMode held_mode : {
             WeaponGripMode::RightHand,
             WeaponGripMode::LeftHand,
             WeaponGripMode::TwoHand}) {
        const auto steady = post_t4_held_pose_policy(
            held_mode, false, false, false);
        expect(!steady.align_visible_grip &&
                   steady.reapply_controller_root &&
                   !steady.recapture_attachment,
               "a steady held pose reapplies its immutable controller root");

        const auto transition = post_t4_held_pose_policy(
            held_mode, true, false, false);
        expect(transition.align_visible_grip &&
                   !transition.reapply_controller_root &&
                   transition.recapture_attachment,
               "a pending held transition aligns and captures exactly once");

        const auto pickup = post_t4_held_pose_policy(
            held_mode, false, true, false);
        expect(pickup.align_visible_grip &&
                   !pickup.reapply_controller_root &&
                   pickup.recapture_attachment,
               "an explicit pickup alignment establishes one attachment");
    }

    const auto pair_to_left_handoff = post_t4_held_pose_policy(
        WeaponGripMode::LeftHand, true, false, true);
    expect(!pair_to_left_handoff.align_visible_grip &&
               pair_to_left_handoff.reapply_controller_root &&
               pair_to_left_handoff.recapture_attachment,
           "two-hand to left-owner bolt handoff preserves the transferred "
           "visible root while recapturing the left attachment");

    const auto explicit_alignment_wins = post_t4_held_pose_policy(
        WeaponGripMode::LeftHand, true, true, true);
    expect(explicit_alignment_wins.align_visible_grip &&
               !explicit_alignment_wins.reapply_controller_root &&
               explicit_alignment_wins.recapture_attachment,
           "an explicitly requested pickup alignment is never suppressed");

    WeaponGripState state{};
    WeaponGripInput input{
        .enabled = true,
        .focused = true,
        .weapon_identity = 1,
        .now_milliseconds = 1'000,
        .right_squeeze_active = true,
        .right_squeeze = 0.0F,
        .right_pose_usable = true,
        .left_squeeze_active = true,
        .left_squeeze = 0.0F,
        .left_pose_usable = true,
    };

    auto update = update_weapon_grip(input, &state);
    expect(update.mode == WeaponGripMode::Chest,
           "released controls keep the rifle on the chest");

    input.right_squeeze = kWeaponGripEngage;
    update = update_weapon_grip(input, &state);
    expect(update.mode == WeaponGripMode::RightHand && update.changed &&
               update.right_latched && !update.left_latched,
           "right squeeze creates a right-only grip");

    input.right_squeeze = 0.5F;
    update = update_weapon_grip(input, &state);
    expect(update.mode == WeaponGripMode::RightHand &&
               update.right_latched && !update.changed,
           "grip hysteresis prevents right-hand flicker");

    input.left_squeeze = 1.0F;
    update = update_weapon_grip(input, &state);
    expect(update.mode == WeaponGripMode::TwoHand && update.changed &&
               update.right_latched && update.left_latched &&
               update.support_pose,
           "second grip enters a real two-hand mode");
    expect(!update.preserve_right_handoff_root,
           "ordinary right-to-pair entry does not invent right-only preservation");

    for (int repeat = 0; repeat < 8; ++repeat) {
        update = update_weapon_grip(input, &state);
        expect(update.mode == WeaponGripMode::TwoHand && !update.changed &&
                   update.right_latched && update.left_latched &&
                   update.support_pose,
               "repeated consumers retain the same two-hand state");
    }

    input.right_squeeze = 0.0F;
    update = update_weapon_grip(input, &state);
    expect(update.mode == WeaponGripMode::LeftHand && update.changed &&
               !update.right_latched && update.left_latched &&
               update.support_pose,
           "right release leaves the rifle in the support hand for bolt work");
    const auto right_release_post = post_t4_held_pose_policy(
        update.mode, update.changed, false, update.support_pose);
    expect(!right_release_post.align_visible_grip &&
               right_release_post.reapply_controller_root &&
               right_release_post.recapture_attachment,
           "right release keeps the exact pair root during left-owner commit");

    input.right_interaction_reserved = true;
    input.right_squeeze = 1.0F;
    update = update_weapon_grip(input, &state);
    expect(update.mode == WeaponGripMode::LeftHand &&
               !update.right_latched && update.left_latched,
           "reserved right interaction cannot rejoin the left-held rifle");

    input.right_interaction_reserved = false;
    update = update_weapon_grip(input, &state);
    expect(update.mode == WeaponGripMode::TwoHand && update.changed &&
               update.right_latched && update.left_latched &&
               update.support_pose,
           "right regrip returns to shared two-hand control without an ownership timer");

    input.left_squeeze = 0.0F;
    update = update_weapon_grip(input, &state);
    expect(update.mode == WeaponGripMode::RightHand && update.changed &&
               update.right_latched && !update.left_latched,
           "left release returns cleanly to right-only control");
    expect(!update.preserve_right_handoff_root,
           "pair-to-right exit does not preserve the pair-directed root");
    expect(!update.support_pose,
           "left release clears the authored support pose");

    update = update_weapon_grip(input, &state);
    expect(update.mode == WeaponGripMode::RightHand && !update.changed &&
               !update.preserve_right_handoff_root,
           "settled right-only control cannot regain the pair-directed root");

    input.left_interaction_reserved = true;
    input.left_squeeze = 1.0F;
    update = update_weapon_grip(input, &state);
    expect(update.mode == WeaponGripMode::RightHand &&
               update.right_latched && !update.left_latched,
           "magazine interaction reservation masks support grip");

    input.left_squeeze = 0.0F;
    input.left_interaction_reserved = false;
    input.right_interaction_reserved = true;
    update = update_weapon_grip(input, &state);
    expect(update.mode == WeaponGripMode::RightHand && update.right_latched,
           "right interaction never drops the only hand holding the rifle");

    input.right_interaction_reserved = false;
    input.right_squeeze = 0.0F;
    update = update_weapon_grip(input, &state);
    expect(update.mode == WeaponGripMode::Chest && update.changed,
           "releasing both grips holsters the rifle");
    expect(!update.preserve_right_handoff_root,
           "holstering clears handoff-root preservation");

    input.left_squeeze = 1.0F;
    update = update_weapon_grip(input, &state);
    expect(update.mode == WeaponGripMode::LeftHand && update.changed &&
               !update.support_pose,
           "left-only chest pickup does not invent a support pose");

    input.left_interaction_reserved = true;
    update = update_weapon_grip(input, &state);
    expect(update.mode == WeaponGripMode::LeftHand && update.left_latched,
           "left reservation cannot drop the only hand holding the rifle");
    input.left_interaction_reserved = false;

    input.right_squeeze = 1.0F;
    update = update_weapon_grip(input, &state);
    expect(update.mode == WeaponGripMode::TwoHand && update.changed &&
               update.right_latched && update.left_latched &&
               update.support_pose,
           "right hand joins a left pickup as a two-hand pair");

    input.weapon_identity = 2;
    input.right_squeeze = 0.0F;
    update = update_weapon_grip(input, &state);
    expect(update.mode == WeaponGripMode::LeftHand &&
               update.left_latched && !update.support_pose,
           "weapon switch rebaselines held controls without inheriting support");
    expect(!update.preserve_right_handoff_root,
           "weapon switch clears the prior attachment-preservation state");

    input.right_squeeze = 1.0F;
    update = update_weapon_grip(input, &state);
    expect(update.mode == WeaponGripMode::TwoHand,
           "both grips deterministically select the pair mode");

    input.focused = false;
    update = update_weapon_grip(input, &state);
    expect(update.mode == WeaponGripMode::Chest && !state.initialized &&
                !update.right_latched && !update.left_latched,
           "focus loss clears every grip and pair latch");
    expect(state.right_rearm_required && state.left_rearm_required,
           "focus loss requires held hands to release before recovery");
    input.focused = true;
    update = update_weapon_grip(input, &state);
    expect(update.mode == WeaponGripMode::Chest,
           "held controls do not reacquire immediately after focus recovery");

    WeaponGripState symmetric_reservation_state{};
    WeaponGripInput symmetric_reservation{
        .enabled = true,
        .focused = true,
        .weapon_identity = 6,
        .now_milliseconds = 1'500,
        .right_squeeze_active = true,
        .right_squeeze = 1.0F,
        .right_pose_usable = true,
        .left_squeeze_active = true,
        .left_squeeze = 1.0F,
        .left_pose_usable = true,
    };
    update = update_weapon_grip(
        symmetric_reservation, &symmetric_reservation_state);
    expect(update.mode == WeaponGripMode::TwoHand &&
               update.right_latched && update.left_latched,
           "a fresh pair establishes two-hand ownership for mirrored reservation tests");

    symmetric_reservation.left_interaction_reserved = true;
    update = update_weapon_grip(
        symmetric_reservation, &symmetric_reservation_state);
    expect(update.mode == WeaponGripMode::TwoHand &&
               update.right_latched && update.left_latched,
           "reserving left interaction cannot evict an already-held support hand from TwoHand");

    symmetric_reservation.left_squeeze = 0.0F;
    update = update_weapon_grip(
        symmetric_reservation, &symmetric_reservation_state);
    expect(update.mode == WeaponGripMode::RightHand &&
               update.right_latched && !update.left_latched,
           "physical left release leaves the right hand as the stable rifle owner");

    symmetric_reservation.left_squeeze = 1.0F;
    update = update_weapon_grip(
        symmetric_reservation, &symmetric_reservation_state);
    expect(update.mode == WeaponGripMode::RightHand &&
               update.right_latched && !update.left_latched,
           "selected left interaction blocks only a later left rifle rejoin");

    symmetric_reservation.left_interaction_reserved = false;
    update = update_weapon_grip(
        symmetric_reservation, &symmetric_reservation_state);
    expect(update.mode == WeaponGripMode::TwoHand &&
               update.right_latched && update.left_latched,
           "clearing left interaction reservation restores the pair on the next physical grip sample");

    symmetric_reservation.left_interaction_reserved = true;
    symmetric_reservation.right_interaction_reserved = true;
    update = update_weapon_grip(
        symmetric_reservation, &symmetric_reservation_state);
    expect(update.mode == WeaponGripMode::TwoHand &&
               update.right_latched && update.left_latched,
           "an unresolved interaction may reserve both candidates without evicting either member of TwoHand");

    symmetric_reservation.right_squeeze = 0.0F;
    update = update_weapon_grip(
        symmetric_reservation, &symmetric_reservation_state);
    expect(update.mode == WeaponGripMode::LeftHand &&
               !update.right_latched && update.left_latched,
           "physical right release leaves the left hand as the stable rifle owner");

    symmetric_reservation.right_squeeze = 1.0F;
    update = update_weapon_grip(
        symmetric_reservation, &symmetric_reservation_state);
    expect(update.mode == WeaponGripMode::LeftHand &&
               !update.right_latched && update.left_latched,
           "selected right interaction blocks only a later right rifle rejoin");

    symmetric_reservation.left_interaction_reserved = false;
    symmetric_reservation.right_interaction_reserved = false;
    update = update_weapon_grip(
        symmetric_reservation, &symmetric_reservation_state);
    expect(update.mode == WeaponGripMode::TwoHand &&
               update.right_latched && update.left_latched,
           "clearing right interaction reservation restores the pair without an ownership timer");

    WeaponGripState recovery_state{};
    WeaponGripInput recovery{
        .enabled = true,
        .focused = true,
        .weapon_identity = 7,
        .now_milliseconds = 2'000,
        .right_squeeze_active = true,
        .right_squeeze = 1.0F,
        .right_pose_usable = false,
        .left_squeeze_active = true,
        .left_squeeze = 0.0F,
        .left_pose_usable = true,
    };
    update = update_weapon_grip(recovery, &recovery_state);
    expect(update.mode == WeaponGripMode::Chest,
           "an untracked pose cannot acquire the rifle");

    recovery.right_pose_usable = true;
    update = update_weapon_grip(recovery, &recovery_state);
    expect(update.mode == WeaponGripMode::RightHand,
           "a tracked held controller acquires normally");

    recovery.now_milliseconds += 1;
    recovery.right_squeeze_active = false;
    recovery.right_pose_usable = false;
    update = update_weapon_grip(recovery, &recovery_state);
    expect(update.mode == WeaponGripMode::RightHand &&
               update.right_latched,
           "one missing OpenXR publication retains the existing owner");

    recovery.now_milliseconds = 2'000 +
        kWeaponGripTrackingGraceMilliseconds;
    update = update_weapon_grip(recovery, &recovery_state);
    expect(update.mode == WeaponGripMode::RightHand,
           "the final millisecond of the recovery window remains stable");

    recovery.now_milliseconds += 1;
    update = update_weapon_grip(recovery, &recovery_state);
    expect(update.mode == WeaponGripMode::Chest &&
               recovery_state.right_rearm_required,
           "a prolonged tracking outage retires ownership without a snap");

    recovery.right_squeeze_active = true;
    recovery.right_pose_usable = true;
    update = update_weapon_grip(recovery, &recovery_state);
    expect(update.mode == WeaponGripMode::Chest,
           "relocalized held input cannot silently reacquire the rifle");

    recovery.right_squeeze = 0.0F;
    update = update_weapon_grip(recovery, &recovery_state);
    expect(!recovery_state.right_rearm_required,
           "a readable physical release rearms acquisition");
    recovery.right_squeeze = 1.0F;
    update = update_weapon_grip(recovery, &recovery_state);
    expect(update.mode == WeaponGripMode::RightHand,
           "a fresh grip after recovery acquires normally");

    recovery.now_milliseconds += 1;
    recovery.right_pose_usable = false;
    recovery.right_squeeze = 0.0F;
    update = update_weapon_grip(recovery, &recovery_state);
    expect(update.mode == WeaponGripMode::Chest &&
               !recovery_state.right_rearm_required,
           "a readable release remains immediate during pose loss");

    WeaponGripState pair_state{};
    WeaponGripInput pair{
        .enabled = true,
        .focused = true,
        .weapon_identity = 9,
        .now_milliseconds = 4'000,
        .right_squeeze_active = true,
        .right_squeeze = 1.0F,
        .right_pose_usable = true,
        .left_squeeze_active = true,
        .left_squeeze = 1.0F,
        .left_pose_usable = true,
    };
    update = update_weapon_grip(pair, &pair_state);
    expect(update.mode == WeaponGripMode::TwoHand && update.support_pose,
           "tracked pair establishes support steering");

    pair.left_pose_usable = false;
    pair.now_milliseconds = 4'000 +
        kWeaponGripTrackingGraceMilliseconds;
    update = update_weapon_grip(pair, &pair_state);
    expect(update.mode == WeaponGripMode::TwoHand && update.support_pose &&
               update.left_latched,
           "transient support-hand loss preserves pair ownership and ADS pose");

    pair.now_milliseconds += 1;
    update = update_weapon_grip(pair, &pair_state);
    expect(update.mode == WeaponGripMode::RightHand &&
               update.right_latched && !update.left_latched &&
               pair_state.left_rearm_required && !update.support_pose,
           "expired support-hand loss retains the healthy right owner only");

    WeaponGripState clock_state{};
    WeaponGripInput clock{
        .enabled = true,
        .focused = true,
        .weapon_identity = 11,
        .now_milliseconds = 8'000,
        .right_squeeze_active = true,
        .right_squeeze = 1.0F,
        .right_pose_usable = true,
    };
    update = update_weapon_grip(clock, &clock_state);
    clock.right_squeeze_active = false;
    clock.right_pose_usable = false;
    clock.now_milliseconds = 7'999;
    update = update_weapon_grip(clock, &clock_state);
    expect(update.mode == WeaponGripMode::Chest &&
               clock_state.right_rearm_required,
           "monotonic-clock regression retires ownership fail-closed");

    std::uint64_t pose_failure_since = 0;
    expect(retain_weapon_pose_during_tracking_failure(
               10'000, &pose_failure_since) &&
               pose_failure_since == 10'000,
           "first rejected fresh pose opens the retained-render window");
    expect(retain_weapon_pose_during_tracking_failure(
               10'000 + kWeaponGripTrackingGraceMilliseconds,
               &pose_failure_since),
           "retained rendering is allowed at the exact recovery deadline");
    expect(!retain_weapon_pose_during_tracking_failure(
               10'001 + kWeaponGripTrackingGraceMilliseconds,
               &pose_failure_since),
           "retained rendering expires one millisecond after the deadline");
    expect(!retain_weapon_pose_during_tracking_failure(
               9'999, &pose_failure_since),
           "retained rendering fails closed on clock regression");
    reset_weapon_pose_tracking_failure(&pose_failure_since);
    expect(pose_failure_since == 0,
           "fresh tracked placement clears the renderer recovery window");
    return 0;
}
