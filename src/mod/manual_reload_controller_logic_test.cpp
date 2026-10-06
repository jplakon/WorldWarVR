// SPDX-License-Identifier: GPL-3.0-only
#include "manual_reload_controller_logic.hpp"
#include "kar98_bolt_action_logic.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <tuple>
#include <utility>

namespace {

void expect(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

using wawvr::gameplay::ReloadEvent;
using wawvr::gameplay::ReloadProfileKind;
using wawvr::gameplay::ReloadStage;
using wawvr::mod::ManualReloadControllerFrame;
using wawvr::mod::ManualReloadControllerState;
using wawvr::mod::ManualReloadPoint;
using wawvr::mod::ManualReloadReceiverGeometry;
using wawvr::mod::ManualStripperClipCommitInput;
using wawvr::mod::Kar98BoltActionFrame;
using wawvr::mod::Kar98BoltActionState;
using wawvr::mod::Kar98BoltEvent;
using wawvr::mod::kKar98BoltTravelUnits;
using wawvr::mod::calculate_manual_reload_top_feed_receiver;
using wawvr::mod::manual_reload_left_waist_contains;
using wawvr::mod::manual_reload_right_waist_contains;
using wawvr::mod::plan_manual_stripper_clip_commit;
using wawvr::mod::update_kar98_bolt_action;
using wawvr::mod::update_manual_reload_controller;

ManualReloadControllerFrame tracked_kar98() {
    ManualReloadControllerFrame frame{};
    frame.input_owned = true;
    frame.action_sequence = 1;
    frame.profile_kind = ReloadProfileKind::InternalStripperClip;
    frame.weapon_supported = true;
    frame.can_reload = true;
    frame.feed_pose_valid = true;
    return frame;
}

Kar98BoltActionFrame tracked_bolt(const std::uint64_t sequence) {
    Kar98BoltActionFrame frame{};
    frame.enabled = true;
    frame.focused = true;
    frame.weapon_supported = true;
    frame.action_sequence = sequence;
    frame.left_rifle_gripped = true;
    frame.right_hand_pose_valid = true;
    frame.right_hand_near_bolt = true;
    frame.right_trigger_active = true;
    frame.left_trigger_active = true;
    return frame;
}

void test_physical_bolt_and_right_hand_drive_stripper_clip_sequence() {
    ManualReloadControllerState state{};
    auto frame = tracked_kar98();
    auto update = update_manual_reload_controller(frame, &state);
    expect(update.reload.stage == ReloadStage::Ready,
           "first focused sample only baselines actions");

    frame.action_sequence = 2;
    frame.begin_reload_pressed_edge = true;
    update = update_manual_reload_controller(frame, &state);
    expect(update.begin_reload_pressed_edge &&
               update.reload.event == ReloadEvent::ReloadArmed &&
               update.reload.request_native_action_open &&
               update.reload.stage == ReloadStage::ActionOpen,
           "a physical bolt grab arms a supported top-feed reload once");

    frame.action_sequence = 3;
    frame.begin_reload_pressed_edge = false;
    frame.native_action_open = true;
    update = update_manual_reload_controller(frame, &state);
    expect(update.reload.event == ReloadEvent::FeedDeviceAvailable &&
               update.reload.stage == ReloadStage::FeedDeviceAvailable,
           "native open action exposes a fresh stripper clip");

    frame.action_sequence = 4;
    frame.native_action_open = false;
    frame.feed_grip_held = true;
    frame.hand_in_feed_device_zone = true;
    update = update_manual_reload_controller(frame, &state);
    expect(update.feed_grip_pressed_edge &&
               update.reload.event == ReloadEvent::FeedDeviceGrabbed &&
               update.reload.render_detached_feed_device &&
               update.reload.stage == ReloadStage::HoldingFeedDevice,
           "right squeeze grabs the clip from the right waist");

    frame.action_sequence = 5;
    frame.hand_in_feed_device_zone = false;
    frame.hand_in_insertion_zone = true;
    update = update_manual_reload_controller(frame, &state);
    expect(update.reload.event == ReloadEvent::InsertionAssistStarted &&
               update.reload.stage == ReloadStage::NearInsertionPoint,
           "current tracked right hand enters the rifle insertion zone");

    frame.action_sequence = 6;
    frame.feed_grip_held = false;
    update = update_manual_reload_controller(frame, &state);
    expect(update.feed_grip_released_edge &&
               update.reload.event == ReloadEvent::CommitRequested &&
               update.reload.request_native_commit &&
               update.reload.stage == ReloadStage::Committing,
           "top contact commits ammo without closing the physical bolt");

    frame.action_sequence = 7;
    frame.native_commit_completed = true;
    update = update_manual_reload_controller(frame, &state);
    expect(update.reload.event == ReloadEvent::CommitCompleted &&
               update.reload.stage == ReloadStage::Ready,
           "native ammo transfer closes the physical transaction");
}

void test_composed_shot_bolt_clip_and_close_sequence() {
    Kar98BoltActionState bolt_state{};
    ManualReloadControllerState reload_state{};

    auto advance_reload = [&](
                              const Kar98BoltActionFrame& bolt_frame,
                              const bool at_waist,
                              const bool at_receiver,
                              const bool commit_completed = false) {
        const auto bolt =
            update_kar98_bolt_action(bolt_frame, &bolt_state);
        ManualReloadControllerFrame reload = tracked_kar98();
        reload.action_sequence = bolt_frame.action_sequence;
        reload.native_action_open = bolt.action_open;
        reload.native_commit_completed = commit_completed;
        reload.begin_reload_pressed_edge =
            bolt.event == Kar98BoltEvent::Grabbed &&
            bolt.begin_reload_gesture;
        reload.feed_grip_held = bolt_frame.right_grip_held;
        reload.feed_pose_valid = bolt_frame.right_hand_pose_valid &&
            bolt.action_open;
        reload.hand_in_feed_device_zone = bolt.action_open && at_waist;
        reload.hand_in_insertion_zone = bolt.action_open && at_receiver;
        return std::pair{
            bolt,
            update_manual_reload_controller(reload, &reload_state)};
    };

    auto bolt_frame = tracked_bolt(1);
    bolt_frame.shot_fired = true;
    bolt_frame.right_grip_held = true;
    bolt_frame.right_trigger_value = 1.0F;
    auto [bolt, reload] = advance_reload(bolt_frame, false, false);
    expect(bolt.cycle_required &&
               reload.reload.stage == ReloadStage::Ready,
           "a fired round locks the chamber while the reload controller baselines input");

    bolt_frame = tracked_bolt(2);
    std::tie(bolt, reload) = advance_reload(bolt_frame, false, false);
    bolt_frame = tracked_bolt(3);
    bolt_frame.right_trigger_value = 1.0F;
    std::tie(bolt, reload) = advance_reload(bolt_frame, false, false);
    expect(bolt.bolt_grabbed &&
               reload.reload.stage == ReloadStage::ActionOpen,
           "after right-hand release, a fresh trigger grab arms the top-feed transaction");

    bolt_frame = tracked_bolt(4);
    bolt_frame.right_trigger_value = 1.0F;
    bolt_frame.right_hand_forward_coordinate = -kKar98BoltTravelUnits;
    std::tie(bolt, reload) = advance_reload(bolt_frame, false, false);
    expect(bolt.action_open &&
               reload.reload.stage == ReloadStage::FeedDeviceAvailable,
           "full rearward travel exposes the right-waist stripper clip");

    bolt_frame = tracked_bolt(5);
    std::tie(bolt, reload) = advance_reload(bolt_frame, false, false);
    bolt_frame = tracked_bolt(6);
    bolt_frame.right_grip_held = true;
    std::tie(bolt, reload) = advance_reload(bolt_frame, true, false);
    expect(reload.reload.stage == ReloadStage::HoldingFeedDevice &&
               reload.reload.render_detached_feed_device,
           "right grip takes a visible clip from the right waist while the bolt remains open");

    bolt_frame = tracked_bolt(7);
    bolt_frame.right_grip_held = true;
    std::tie(bolt, reload) = advance_reload(bolt_frame, false, true);
    expect(reload.reload.stage == ReloadStage::NearInsertionPoint,
           "the tracked right-hand clip reaches the open receiver");

    bolt_frame = tracked_bolt(8);
    std::tie(bolt, reload) = advance_reload(bolt_frame, false, true);
    expect(reload.reload.stage == ReloadStage::Committing &&
               reload.reload.request_native_commit && bolt.action_open,
           "insertion requests ammo transfer without automatically closing the bolt");

    bolt_frame = tracked_bolt(9);
    std::tie(bolt, reload) = advance_reload(bolt_frame, false, false);
    expect(reload.reload.stage == ReloadStage::Committing &&
               reload.reload.request_native_commit,
           "a missed native commit sample is retried rather than deadlocking the rifle");

    bolt_frame = tracked_bolt(10);
    std::tie(bolt, reload) =
        advance_reload(bolt_frame, false, false, true);
    expect(reload.reload.stage == ReloadStage::Ready && bolt.action_open,
           "ammo transfer completes independently while the physical bolt stays open");

    bolt_frame = tracked_bolt(11);
    bolt_frame.right_trigger_value = 1.0F;
    bolt_frame.right_hand_forward_coordinate = -kKar98BoltTravelUnits;
    std::tie(bolt, reload) = advance_reload(bolt_frame, false, false);
    expect(bolt.bolt_grabbed,
           "a fresh index-trigger grab takes the open bolt for closing");
    bolt_frame = tracked_bolt(12);
    bolt_frame.right_trigger_value = 1.0F;
    bolt_frame.right_hand_forward_coordinate = 0.0F;
    std::tie(bolt, reload) = advance_reload(bolt_frame, false, false);
    expect(!bolt.cycle_required && bolt.bolt_fraction == 0.0F,
           "pushing the bolt fully forward chambers the manually loaded rifle");
    bolt_frame = tracked_bolt(13);
    std::tie(bolt, reload) = advance_reload(bolt_frame, false, false);
    expect(!bolt.block_attack && !bolt.reserve_right_grip,
           "final release restores ordinary firing and right-hand rifle pickup");
}

void test_focus_recovery_cannot_synthesize_actions() {
    ManualReloadControllerState state{};
    auto frame = tracked_kar98();
    frame.input_owned = false;
    frame.begin_reload_pressed_edge = true;
    frame.feed_grip_held = true;
    auto update = update_manual_reload_controller(frame, &state);
    expect(update.reload.stage == ReloadStage::Ready,
           "unfocused held actions cannot enter manual reload");

    frame.input_owned = true;
    frame.action_sequence = 2;
    update = update_manual_reload_controller(frame, &state);
    expect(!update.begin_reload_pressed_edge &&
               !update.feed_grip_pressed_edge &&
               update.reload.stage == ReloadStage::Ready,
           "focus recovery baselines held feed grip and ignores stale gestures");
}

void test_native_profile_falls_back_without_suppression() {
    ManualReloadControllerState state{};
    auto frame = tracked_kar98();
    frame.profile_kind = ReloadProfileKind::NativeOnly;
    static_cast<void>(update_manual_reload_controller(frame, &state));
    frame.action_sequence = 2;
    frame.begin_reload_pressed_edge = true;
    const auto update = update_manual_reload_controller(frame, &state);
    expect(update.reload.stage == ReloadStage::Ready &&
               !update.reload.manual_reload_active &&
               !update.reload.suppress_native_automatic_commit,
           "unsupported weapons retain their ordinary native reload path");
}

void test_left_hand_drives_detachable_magazine_sequence() {
    ManualReloadControllerState state{};
    ManualReloadControllerFrame frame{};
    frame.input_owned = true;
    frame.action_sequence = 1;
    frame.profile_kind = ReloadProfileKind::DetachableMagazine;
    frame.weapon_supported = true;
    frame.can_reload = true;
    frame.feed_pose_valid = true;
    auto update = update_manual_reload_controller(frame, &state);
    expect(update.reload.stage == ReloadStage::Ready,
           "first detachable sample baselines A and left grip");

    frame.action_sequence = 2;
    frame.begin_reload_pressed_edge = true;
    update = update_manual_reload_controller(frame, &state);
    expect(update.begin_reload_pressed_edge &&
               update.reload.stage == ReloadStage::FeedDeviceAvailable &&
               update.reload.hide_authored_feed_device,
           "A ejects the detachable magazine exactly once");

    frame.action_sequence = 3;
    frame.begin_reload_pressed_edge = false;
    frame.feed_grip_held = true;
    frame.hand_in_feed_device_zone = true;
    update = update_manual_reload_controller(frame, &state);
    expect(update.feed_grip_pressed_edge &&
               update.reload.stage == ReloadStage::HoldingFeedDevice &&
               update.reload.render_detached_feed_device,
           "left grip at the off-hand hip draws a tracked magazine");

    frame.action_sequence = 4;
    frame.hand_in_feed_device_zone = false;
    frame.hand_in_insertion_zone = true;
    update = update_manual_reload_controller(frame, &state);
    expect(update.reload.stage == ReloadStage::NearInsertionPoint &&
               update.reload.render_detached_feed_device &&
               !update.reload.request_native_commit,
           "magazine position remains held until release in the well");

    frame.action_sequence = 5;
    frame.feed_grip_held = false;
    update = update_manual_reload_controller(frame, &state);
    expect(update.feed_grip_released_edge &&
               update.reload.stage == ReloadStage::Committing &&
               update.reload.request_native_commit,
           "left-grip release in the well requests one native ammo transfer");

    frame.action_sequence = 6;
    frame.hand_in_insertion_zone = false;
    frame.native_commit_completed = true;
    update = update_manual_reload_controller(frame, &state);
    expect(update.reload.stage == ReloadStage::Ready &&
               !update.reload.hide_authored_feed_device,
           "native acknowledgement seats the authored Colt magazine");
}

void test_receiver_tracks_the_clip_along_the_rifle_top() {
    ManualReloadPoint receiver{};
    float fraction = 0.0F;
    expect(calculate_manual_reload_top_feed_receiver(
               {40.0F, 7.0F, 3.0F}, {0.0F, 0.0F, 0.0F},
               {100.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F},
               &receiver, &fraction),
           "valid rifle segment produces a top-feed receiver");
    expect(std::abs(fraction - 0.40F) < 0.0001F &&
               std::abs(receiver.x - 40.0F) < 0.0001F &&
               std::abs(receiver.y) < 0.0001F &&
               std::abs(receiver.z - 3.0F) < 0.0001F,
           "receiver follows the current clip projection along the rifle");

    expect(calculate_manual_reload_top_feed_receiver(
               {-100.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F},
               {100.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F},
               &receiver, &fraction) &&
               std::abs(fraction -
                        wawvr::mod::kManualReloadReceiverSegmentMinimum) <
                   0.0001F,
           "receiver clamps behind-grip probes to the usable rifle segment");
    expect(calculate_manual_reload_top_feed_receiver(
               {500.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F},
               {100.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F},
               &receiver, &fraction) &&
               std::abs(fraction -
                        wawvr::mod::kManualReloadReceiverSegmentMaximum) <
                   0.0001F,
           "receiver clamps beyond-muzzle probes to the usable rifle segment");

    const ManualReloadReceiverGeometry synthetic{0.20F, 0.60F, 6.0F};
    expect(calculate_manual_reload_top_feed_receiver(
               synthetic, {90.0F, 0.0F, 0.0F},
               {0.0F, 0.0F, 0.0F}, {100.0F, 0.0F, 0.0F},
               {0.0F, 0.0F, 1.0F}, &receiver, &fraction) &&
               std::abs(fraction - 0.60F) < 0.0001F &&
               std::abs(receiver.x - 60.0F) < 0.0001F &&
               std::abs(receiver.z - 6.0F) < 0.0001F,
           "profile receiver geometry controls projection and top offset");
}

void test_feed_zone_is_on_the_right_waist() {
    expect(manual_reload_right_waist_contains({0.0F, -12.0F, -28.0F}),
           "a tracked right-hand pose at the right waist reaches clips");
    expect(!manual_reload_right_waist_contains({0.0F, 12.0F, -28.0F}),
           "the former left-waist zone cannot produce a right-hand clip");
    expect(!manual_reload_right_waist_contains({0.0F, -12.0F, 0.0F}),
           "a right hand near the rifle cannot also count as the waist belt");
}

void test_detachable_feed_zone_is_on_the_left_waist() {
    expect(manual_reload_left_waist_contains({0.0F, 12.0F, -28.0F}),
           "a tracked left hand at the off-hand waist reaches magazines");
    expect(!manual_reload_left_waist_contains({0.0F, -12.0F, -28.0F}),
           "the stripper-clip waist cannot draw a detachable magazine");
    expect(!manual_reload_left_waist_contains({0.0F, 12.0F, 0.0F}),
           "a left hand near the pistol cannot also count as the hip belt");
}

void test_scoped_segmented_stripper_clip_plans_full_capacity() {
    const ManualStripperClipCommitInput scoped{
        .weapon_state = 0,
        .clip_size = 5,
        .reserve = 20,
        .loaded = 0,
        .reload_ammo_add = 1,
        .reload_start_add = 1,
        .bolt_action = 1,
        .segmented_reload = 1,
    };
    auto plan = plan_manual_stripper_clip_commit(scoped);
    expect(plan.valid && plan.rounds_to_transfer == 5 &&
               plan.native_reload_clip_calls == 5,
           "one scoped segmented stripper clip plans five one-round native transfers");

    auto partial = scoped;
    partial.loaded = 3;
    plan = plan_manual_stripper_clip_commit(partial);
    expect(plan.valid && plan.rounds_to_transfer == 2 &&
               plan.native_reload_clip_calls == 2,
           "a scoped stripper clip stops when the five-round internal magazine is full");

    auto reserve_limited = scoped;
    reserve_limited.reserve = 2;
    plan = plan_manual_stripper_clip_commit(reserve_limited);
    expect(plan.valid && plan.rounds_to_transfer == 2 &&
               plan.native_reload_clip_calls == 2,
           "a scoped stripper clip cannot transfer more rounds than reserve contains");

    auto reload_start = scoped;
    reload_start.weapon_state = 9;
    plan = plan_manual_stripper_clip_commit(reload_start);
    expect(plan.valid && plan.native_reload_clip_calls == 5,
           "scoped reload-start semantics use the exact one-round start-add cap");
}

void test_unsegmented_stripper_clip_keeps_one_native_top_up() {
    const ManualStripperClipCommitInput unscoped{
        .weapon_state = 0,
        .clip_size = 5,
        .reserve = 20,
        .loaded = 0,
        .reload_ammo_add = 0,
        .reload_start_add = 0,
        .bolt_action = 1,
        .segmented_reload = 0,
    };
    const auto plan = plan_manual_stripper_clip_commit(unscoped);
    expect(plan.valid && plan.rounds_to_transfer == 5 &&
               plan.native_reload_clip_calls == 1,
           "the accepted unscoped WeaponDef still fills through one native call");
}

void test_stripper_clip_plan_rejects_unprovable_ammo_state() {
    ManualStripperClipCommitInput input{
        .weapon_state = 0,
        .clip_size = 5,
        .reserve = 20,
        .loaded = 5,
        .reload_ammo_add = 1,
        .reload_start_add = 1,
        .bolt_action = 1,
        .segmented_reload = 1,
    };
    expect(!plan_manual_stripper_clip_commit(input).valid,
           "a full internal magazine cannot start another physical clip commit");
    input.loaded = 0;
    input.reserve = 0;
    expect(!plan_manual_stripper_clip_commit(input).valid,
           "an empty reserve cannot start a physical clip commit");
    input.reserve = 20;
    input.clip_size = 6;
    expect(!plan_manual_stripper_clip_commit(input).valid,
           "an unexpected clip size fails closed instead of borrowing five-round semantics");
    input.clip_size = 5;
    input.bolt_action = 0;
    expect(!plan_manual_stripper_clip_commit(input).valid,
           "a non-bolt WeaponDef cannot enter the stripper-clip commit path");
    input.bolt_action = 1;
    input.physical_clip_capacity = 6;
    input.clip_size = 6;
    expect(!plan_manual_stripper_clip_commit(input).valid,
           "a non-five-round physical clip capacity fails closed");
    input.physical_clip_capacity = 5;
    input.clip_size = 5;
    input.weapon_state = 9;
    input.reload_start_add = 0;
    expect(!plan_manual_stripper_clip_commit(input).valid,
           "a reload-start state that PM_ReloadClip cannot advance fails closed");
}

}  // namespace

int main() {
    test_physical_bolt_and_right_hand_drive_stripper_clip_sequence();
    test_composed_shot_bolt_clip_and_close_sequence();
    test_focus_recovery_cannot_synthesize_actions();
    test_native_profile_falls_back_without_suppression();
    test_left_hand_drives_detachable_magazine_sequence();
    test_receiver_tracks_the_clip_along_the_rifle_top();
    test_feed_zone_is_on_the_right_waist();
    test_detachable_feed_zone_is_on_the_left_waist();
    test_scoped_segmented_stripper_clip_plans_full_capacity();
    test_unsegmented_stripper_clip_keeps_one_native_top_up();
    test_stripper_clip_plan_rejects_unprovable_ammo_state();
    return 0;
}
