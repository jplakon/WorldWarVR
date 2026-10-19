// SPDX-License-Identifier: GPL-3.0-only
#include "manual_reload_controller_logic.hpp"
#include "kar98_bolt_action_logic.hpp"

#include <algorithm>
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
using wawvr::mod::ManualReloadControllerUpdate;
using wawvr::mod::ManualReloadPoint;
using wawvr::mod::ManualReloadReceiverGeometry;
using wawvr::mod::ManualStripperClipCommitInput;
using wawvr::mod::ManualStripperClipFanoutFrame;
using wawvr::mod::ManualStripperClipFanoutState;
using wawvr::mod::ManualStripperClipRequestGate;
using wawvr::mod::StripperClipFeedHand;
using wawvr::mod::Kar98BoltActionFrame;
using wawvr::mod::Kar98BoltActionState;
using wawvr::mod::Kar98BoltEvent;
using wawvr::mod::kKar98BoltTravelUnits;
using wawvr::mod::calculate_manual_reload_top_feed_receiver;
using wawvr::mod::manual_reload_left_waist_contains;
using wawvr::mod::manual_reload_right_waist_contains;
using wawvr::mod::plan_manual_stripper_clip_commit;
using wawvr::mod::plan_manual_stripper_clip_fanout;
using wawvr::mod::verify_manual_stripper_clip_fanout;
using wawvr::mod::begin_manual_stripper_clip_request;
using wawvr::mod::reset_manual_reload_controller;
using wawvr::mod::select_stripper_clip_feed_hand;
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
    frame.feed_grip_release_observed = true;
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
               update.reload.stage == ReloadStage::Ready &&
               !update.reserves_feed_grip,
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
        reload.feed_grip_release_observed = bolt_frame.focused &&
            bolt_frame.right_hand_pose_valid && !bolt_frame.right_grip_held;
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

ManualReloadControllerUpdate grab_and_insert_held_clip(
    ManualReloadControllerFrame* const frame,
    ManualReloadControllerState* const state,
    const StripperClipFeedHand hand = StripperClipFeedHand::Right) {
    *frame = tracked_kar98();
    frame->feed_hand = hand;
    static_cast<void>(update_manual_reload_controller(*frame, state));
    ++frame->action_sequence;
    frame->begin_reload_pressed_edge = true;
    static_cast<void>(update_manual_reload_controller(*frame, state));
    ++frame->action_sequence;
    frame->begin_reload_pressed_edge = false;
    frame->native_action_open = true;
    static_cast<void>(update_manual_reload_controller(*frame, state));
    ++frame->action_sequence;
    frame->feed_grip_held = true;
    frame->hand_in_feed_device_zone = true;
    auto update = update_manual_reload_controller(*frame, state);
    expect(update.reload.event == ReloadEvent::FeedDeviceGrabbed &&
               state->stripper_clip_grip_reserved_until_release,
           "an actual stripper-clip grab reserves its held squeeze until release");
    ++frame->action_sequence;
    frame->hand_in_feed_device_zone = false;
    frame->hand_in_insertion_zone = true;
    update = update_manual_reload_controller(*frame, state);
    expect(update.reload.stage == ReloadStage::NearInsertionPoint,
           "held clip reaches the insertion guide before contact commit");
    // T4 can render/acknowledge again before another XR action publication.
    update = update_manual_reload_controller(*frame, state);
    expect(update.reload.stage == ReloadStage::Committing &&
               update.reload.request_native_commit && update.reserves_feed_grip,
           "same-publication contact commit keeps the clip-hand reservation");
    return update;
}

void test_symmetric_clip_selection_and_pre_reservation() {
    ManualReloadControllerState state{};
    expect(select_stripper_clip_feed_hand(state, true, false) == StripperClipFeedHand::Left &&
               select_stripper_clip_feed_hand(state, false, true) == StripperClipFeedHand::Right,
           "either sole rifle owner offers the opposite free clip hand");
    expect(select_stripper_clip_feed_hand(state, true, true) == StripperClipFeedHand::None &&
               select_stripper_clip_feed_hand(state, false, false) == StripperClipFeedHand::None,
           "clip availability never breaks an existing pair or invents a rifle owner");
    for (const auto hand : {StripperClipFeedHand::Right, StripperClipFeedHand::Left}) {
        reset_manual_reload_controller(&state);
        auto frame = tracked_kar98();
        frame.feed_hand = hand;
        static_cast<void>(update_manual_reload_controller(frame, &state));
        ++frame.action_sequence;
        frame.begin_reload_pressed_edge = true;
        auto update = update_manual_reload_controller(frame, &state);
        expect(update.reserves_feed_grip && state.stripper_clip_hand == StripperClipFeedHand::None,
               "the free hand is reserved while opening, before its physical belt grab");
        ++frame.action_sequence;
        frame.begin_reload_pressed_edge = false;
        frame.native_action_open = true;
        static_cast<void>(update_manual_reload_controller(frame, &state));
        frame.hand_in_feed_device_zone = true;
        for (const float squeeze : {0.60F, 0.64F, 0.65F, 0.66F, 0.68F, 0.70F}) {
            ++frame.action_sequence;
            frame.feed_grip_held = squeeze >= 0.70F;
            update = update_manual_reload_controller(frame, &state);
            expect(update.reserves_feed_grip,
                   "free-hand pre-reservation already covers the rifle's 0.65 engage threshold");
            expect((state.stripper_clip_hand == hand) == (squeeze >= 0.70F),
                   "only the belt pickup threshold latches the actual clip hand");
        }
        expect(update.reload.event == ReloadEvent::FeedDeviceGrabbed &&
                   select_stripper_clip_feed_hand(state, hand == StripperClipFeedHand::Right,
                       hand == StripperClipFeedHand::Left) == hand &&
                   select_stripper_clip_feed_hand(state, true, true) == hand &&
                   select_stripper_clip_feed_hand(state, false, false) == hand,
               "a grabbed clip does not change hands when rifle ownership or tracking changes");
    }
}

void test_symmetric_clip_release_and_reset_boundaries() {
    for (const auto hand : {StripperClipFeedHand::Right, StripperClipFeedHand::Left}) {
        const auto other = hand == StripperClipFeedHand::Right
            ? StripperClipFeedHand::Left : StripperClipFeedHand::Right;
        ManualReloadControllerState state{};
        ManualReloadControllerFrame frame{};
        static_cast<void>(grab_and_insert_held_clip(&frame, &state, hand));
        frame.native_commit_completed = true;
        auto update = update_manual_reload_controller(frame, &state);
        expect(update.reload.event == ReloadEvent::CommitCompleted &&
                   update.reserves_feed_grip && state.stripper_clip_hand == hand,
               "same-publication completion preserves either selected clip hand");
        reset_manual_reload_controller(&state, true);
        frame.native_commit_completed = false;
        frame.native_action_open = false;
        frame.feed_pose_valid = false;
        frame.can_reload = false;
        frame.feed_grip_held = false;
        ++frame.action_sequence;
        update = update_manual_reload_controller(frame, &state);
        expect(update.reserves_feed_grip && state.stripper_clip_hand == hand,
               "normal bolt close preserves selected hand as well as pending physical release");
        for (const auto sample_hand : {other, StripperClipFeedHand::None}) {
            ++frame.action_sequence;
            frame.feed_hand = sample_hand;
            frame.feed_grip_release_observed = true;
            update = update_manual_reload_controller(frame, &state);
            expect(update.reserves_feed_grip && state.stripper_clip_hand == hand,
                   "opposite-hand or unselected release cannot retire a selected clip squeeze");
        }
        frame.feed_hand = hand;
        frame.input_owned = false;
        for (int repeat = 0; repeat < 3; ++repeat) {
            ++frame.action_sequence;
            update = update_manual_reload_controller(frame, &state);
            expect(update.reserves_feed_grip && state.stripper_clip_hand == hand,
                   "repeated tracking/focus loss cannot release either clip hand");
        }
        ++frame.action_sequence;
        frame.input_owned = true;
        frame.feed_grip_release_observed = false; // Inactive/invalid or 0.50 recovery.
        update = update_manual_reload_controller(frame, &state);
        expect(update.reserves_feed_grip && state.stripper_clip_hand == hand,
               "restored input without a proven below-threshold release retains clip ownership");
        ++frame.action_sequence;
        frame.feed_grip_release_observed = true;
        update = update_manual_reload_controller(frame, &state);
        expect(!update.reserves_feed_grip && state.stripper_clip_hand == StripperClipFeedHand::None,
               "selected tracked release works after closed-bolt insertion geometry becomes invalid");
        ++frame.action_sequence;
        frame.feed_grip_release_observed = false;
        frame.feed_grip_held = true;
        update = update_manual_reload_controller(frame, &state);
        expect(!update.reserves_feed_grip && update.feed_grip_pressed_edge,
               "a fresh squeeze is available for ordinary rifle regrip after release");
        reset_manual_reload_controller(&state);
        static_cast<void>(grab_and_insert_held_clip(&frame, &state, hand));
        reset_manual_reload_controller(&state);
        expect(state.stripper_clip_hand == StripperClipFeedHand::None &&
                   !state.stripper_clip_grip_reserved_until_release,
               "explicit weapon/session reset clears either hand's latch");
    }
}

void test_clip_candidate_handoff_and_invalid_pickups() {
    for (const auto hand : {StripperClipFeedHand::Right, StripperClipFeedHand::Left}) {
        ManualReloadControllerState state{};
        auto frame = tracked_kar98();
        frame.feed_hand = hand == StripperClipFeedHand::Right
            ? StripperClipFeedHand::Left : StripperClipFeedHand::Right;
        static_cast<void>(update_manual_reload_controller(frame, &state));
        ++frame.action_sequence;
        frame.feed_hand = hand;
        frame.feed_grip_held = true;
        frame.begin_reload_pressed_edge = true;
        auto update = update_manual_reload_controller(frame, &state);
        expect(update.begin_reload_pressed_edge && !update.feed_grip_pressed_edge &&
                   update.reload.stage == ReloadStage::ActionOpen,
               "changing candidate baselines held grip without losing an independent bolt gesture");
        ++frame.action_sequence;
        frame.begin_reload_pressed_edge = false;
        frame.native_action_open = true;
        frame.hand_in_feed_device_zone = true;
        static_cast<void>(update_manual_reload_controller(frame, &state));
        ++frame.action_sequence;
        update = update_manual_reload_controller(frame, &state);
        expect(update.reload.stage == ReloadStage::FeedDeviceAvailable,
               "a held squeeze after handoff cannot synthesize a belt pickup");
        for (int invalid = 0; invalid < 3; ++invalid) {
            ++frame.action_sequence;
            frame.feed_grip_held = false;
            static_cast<void>(update_manual_reload_controller(frame, &state));
            ++frame.action_sequence;
            frame.feed_grip_held = true;
            frame.feed_pose_valid = invalid != 0;
            frame.hand_in_feed_device_zone = invalid != 1;
            frame.feed_hand = invalid == 2 ? StripperClipFeedHand::None : hand;
            update = update_manual_reload_controller(frame, &state);
            expect(update.reload.stage == ReloadStage::FeedDeviceAvailable &&
                       state.stripper_clip_hand == StripperClipFeedHand::None,
                   "invalid pose, wrong belt, or no selected hand cannot grab a clip");
        }
        frame.feed_hand = hand;
        frame.feed_pose_valid = true;
        frame.feed_grip_held = false;
        ++frame.action_sequence;
        static_cast<void>(update_manual_reload_controller(frame, &state));
        ++frame.action_sequence;
        frame.feed_grip_held = true;
        update = update_manual_reload_controller(frame, &state);
        expect(update.reload.event == ReloadEvent::FeedDeviceGrabbed &&
                   state.stripper_clip_hand == hand,
               "valid release and fresh correct-belt squeeze grabs with either candidate hand");
    }
}

void test_either_bolt_hand_can_precede_either_clip_hand() {
    for (const bool bolt_left : {false, true}) {
        for (const bool feed_left : {false, true}) {
            Kar98BoltActionState bolt_state{};
            ManualReloadControllerState controller{};
            auto physical = tracked_bolt(1);
            physical.left_rifle_gripped = !bolt_left;
            physical.right_rifle_gripped = bolt_left;
            physical.left_hand_pose_valid = true;
            physical.left_hand_near_bolt = true;
            auto frame = tracked_kar98();
            const auto advance = [&](const bool waist, const bool receiver) {
                const auto bolt = update_kar98_bolt_action(physical, &bolt_state);
                frame.action_sequence = physical.action_sequence;
                frame.feed_hand = select_stripper_clip_feed_hand(controller,
                    physical.right_rifle_gripped, physical.left_rifle_gripped);
                frame.native_action_open = bolt.action_open;
                frame.begin_reload_pressed_edge = bolt.event == Kar98BoltEvent::Grabbed &&
                    bolt.begin_reload_gesture;
                frame.feed_grip_held = frame.feed_hand == StripperClipFeedHand::Left
                    ? physical.left_grip_held : physical.right_grip_held;
                frame.feed_pose_valid = bolt.action_open;
                frame.hand_in_feed_device_zone = waist && bolt.action_open;
                frame.hand_in_insertion_zone = receiver && bolt.action_open;
                return std::pair{bolt, update_manual_reload_controller(frame, &controller)};
            };
            static_cast<void>(advance(false, false));
            ++physical.action_sequence;
            (bolt_left ? physical.left_trigger_value : physical.right_trigger_value) = 1.0F;
            auto [bolt, update] = advance(false, false);
            expect(bolt.bolt_grabbed && bolt.manipulating_left_hand == bolt_left &&
                       update.reload.stage == ReloadStage::ActionOpen,
                   "either physical trigger hand independently arms the bolt transaction");
            ++physical.action_sequence;
            (bolt_left ? physical.left_hand_forward_coordinate : physical.right_hand_forward_coordinate) =
                -kKar98BoltTravelUnits;
            std::tie(bolt, update) = advance(false, false);
            expect(bolt.action_open && update.reload.stage == ReloadStage::FeedDeviceAvailable,
                   "either bolt hand opens the same feed transaction");
            ++physical.action_sequence;
            physical.left_trigger_value = 0.0F;
            physical.right_trigger_value = 0.0F;
            std::tie(bolt, update) = advance(false, false);
            expect(!bolt_state.manipulating_hand_selected && bolt.action_open,
                   "releasing the bolt retires bolt-hand selection without closing the action");
            // Mixed cases deliberately hand the rifle to the former bolt hand.
            // Its existing sole-owner transfer policy is independent of feed.
            ++physical.action_sequence;
            physical.right_rifle_gripped = feed_left;
            physical.left_rifle_gripped = !feed_left;
            std::tie(bolt, update) = advance(false, false);
            const auto expected_hand = feed_left ? StripperClipFeedHand::Left : StripperClipFeedHand::Right;
            expect(frame.feed_hand == expected_hand && update.reserves_feed_grip &&
                       !update.feed_grip_pressed_edge,
                   "after an explicit rifle handoff, feed selection follows its free hand, not the old bolt hand");
            ++physical.action_sequence;
            (feed_left ? physical.left_grip_held : physical.right_grip_held) = true;
            std::tie(bolt, update) = advance(true, false);
            expect(update.reload.event == ReloadEvent::FeedDeviceGrabbed &&
                       controller.stripper_clip_hand == expected_hand,
                   "all four bolt/feed hand combinations support an independent belt grab");
            ++physical.action_sequence;
            std::tie(bolt, update) = advance(false, true);
            std::tie(bolt, update) = advance(false, true);
            expect(update.reload.stage == ReloadStage::Committing && update.reload.request_native_commit,
                   "all four hand combinations retain the same contact-driven native ammo request");
        }
    }
}

void test_held_clip_reservation_survives_commit_until_proven_release() {
    ManualReloadControllerState state{};
    ManualReloadControllerFrame frame{};
    auto update = grab_and_insert_held_clip(&frame, &state);
    frame.native_commit_completed = true;
    frame.can_reload = false; // The transfer has filled the rifle.
    update = update_manual_reload_controller(frame, &state);
    expect(update.reload.event == ReloadEvent::CommitCompleted &&
               update.reload.stage == ReloadStage::Ready &&
               !update.reload.manual_reload_active && update.reserves_feed_grip,
           "same-publication ammo completion cannot turn a held clip squeeze into a rifle grip");
    frame.native_commit_completed = false;
    ++frame.action_sequence;
    update = update_manual_reload_controller(frame, &state);
    expect(update.reserves_feed_grip && !update.feed_grip_pressed_edge,
           "later held samples remain reserved without a new clip transaction");

    // Closed-action insertion geometry is invalid, but a tracked physical
    // release remains independently observable and must end the reservation.
    ++frame.action_sequence;
    frame.native_action_open = false;
    frame.feed_pose_valid = false;
    frame.feed_grip_held = false;
    frame.feed_grip_release_observed = true;
    update = update_manual_reload_controller(frame, &state);
    expect(!update.reserves_feed_grip &&
               !state.stripper_clip_grip_reserved_until_release,
           "a proven release clears the reservation even after the bolt closes");
    ++frame.action_sequence;
    frame.feed_grip_held = true;
    frame.feed_grip_release_observed = false;
    update = update_manual_reload_controller(frame, &state);
    expect(update.feed_grip_pressed_edge && !update.reserves_feed_grip,
           "a fresh squeeze after real release is available for ordinary rifle regrip");
}

void test_clip_reservation_rejects_focus_and_invalid_sample_releases() {
    ManualReloadControllerState state{};
    ManualReloadControllerFrame frame{};
    static_cast<void>(grab_and_insert_held_clip(&frame, &state));
    frame.native_commit_completed = true;
    static_cast<void>(update_manual_reload_controller(frame, &state));
    frame.native_commit_completed = false;

    ++frame.action_sequence;
    frame.feed_pose_valid = false;
    frame.feed_grip_held = false; // Inactive/nonfinite input may normalize to zero.
    auto update = update_manual_reload_controller(frame, &state);
    expect(update.reserves_feed_grip,
           "invalid tracking or inactive squeeze cannot masquerade as clip-hand release");
    frame.input_owned = false;
    frame.feed_grip_release_observed = true;
    for (int index = 0; index < 3; ++index) {
        ++frame.action_sequence;
        update = update_manual_reload_controller(frame, &state);
        expect(update.reserves_feed_grip,
               "repeated unfocused samples cannot clear a latched clip reservation");
    }
    // Recovery at raw squeeze 0.50 can read held=false after action baselining,
    // while still above the physical 0.35 release threshold. The separate
    // release fact must remain false; feed_grip_held alone is insufficient.
    ++frame.action_sequence;
    frame.input_owned = true;
    frame.feed_pose_valid = true;
    frame.feed_grip_release_observed = false;
    update = update_manual_reload_controller(frame, &state);
    expect(update.reserves_feed_grip,
           "recovered squeeze in the hysteresis band is not a proven release");
    frame.action_sequence = 0;
    frame.feed_grip_release_observed = true;
    update = update_manual_reload_controller(frame, &state);
    expect(update.reserves_feed_grip,
           "zero action sequence cannot validate a clip-hand release");
    frame.action_sequence = 30;
    update = update_manual_reload_controller(frame, &state);
    expect(!update.reserves_feed_grip,
           "a later current tracked below-threshold sample ends the reservation");
}

void test_clip_release_before_commit_keeps_existing_completion_behavior() {
    ManualReloadControllerState state{};
    ManualReloadControllerFrame frame{};
    static_cast<void>(grab_and_insert_held_clip(&frame, &state));
    ++frame.action_sequence;
    frame.feed_grip_held = false;
    frame.feed_grip_release_observed = true;
    auto update = update_manual_reload_controller(frame, &state);
    expect(update.reserves_feed_grip &&
               !state.stripper_clip_grip_reserved_until_release &&
               update.reload.stage == ReloadStage::Committing,
           "release during pending native commit retains only the existing transaction reservation");
    frame.native_commit_completed = true;
    update = update_manual_reload_controller(frame, &state);
    expect(update.reload.stage == ReloadStage::Ready && !update.reserves_feed_grip,
           "a clip released before native completion needs no additional release latch");
}

void test_clip_reservation_reset_and_profile_boundaries() {
    ManualReloadControllerState state{};
    ManualReloadControllerFrame frame{};
    static_cast<void>(grab_and_insert_held_clip(&frame, &state));
    reset_manual_reload_controller(&state, true);
    frame.native_action_open = false;
    frame.feed_pose_valid = false;
    frame.hand_in_insertion_zone = false;
    ++frame.action_sequence;
    auto update = update_manual_reload_controller(frame, &state);
    expect(update.reload.stage == ReloadStage::Ready && update.reserves_feed_grip,
           "routine bolt-action retirement preserves an unproven clip squeeze release");
    reset_manual_reload_controller(&state);
    update = update_manual_reload_controller(frame, &state);
    expect(!update.reserves_feed_grip &&
               !state.stripper_clip_grip_reserved_until_release,
           "explicit weapon/session reset clears the clip reservation");

    for (int boundary = 0; boundary < 4; ++boundary) {
        reset_manual_reload_controller(&state);
        static_cast<void>(grab_and_insert_held_clip(&frame, &state));
        if (boundary == 0) {
            frame.reset_requested = true;
        } else if (boundary == 1) {
            frame.weapon_supported = false;
        } else {
            frame.profile_kind = boundary == 2
                ? ReloadProfileKind::NativeOnly
                : ReloadProfileKind::DetachableMagazine;
        }
        update = update_manual_reload_controller(frame, &state);
        expect(!update.reserves_feed_grip &&
                   !state.stripper_clip_grip_reserved_until_release,
               "reset, unsupported, native and detachable profile boundaries clear the clip-only latch");
    }
    reset_manual_reload_controller(&state);
    frame = tracked_kar98();
    frame.feed_grip_held = true;
    update = update_manual_reload_controller(frame, &state);
    expect(!update.reserves_feed_grip,
           "ordinary held grips without a clip grab never acquire this reservation");
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
               !update.reload.hide_authored_feed_device &&
               !update.reserves_feed_grip &&
               !state.stripper_clip_grip_reserved_until_release,
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

ManualStripperClipFanoutFrame clip_fanout_frame(
    const int loaded = 0, const int reserve = 20, const bool segmented = false) {
    return {
        .key = {3, 8, 17, 4, 20, 0x1000, 2, 3},
        .now_milliseconds = 1000,
        .player_state = 0x2000,
        .ammo = {
            .weapon_state = 0, .clip_size = 5, .reserve = reserve,
            .loaded = loaded, .reload_ammo_add = segmented ? 1 : 0,
            .reload_start_add = 1, .bolt_action = 1,
            .segmented_reload = segmented ? 1 : 0,
        },
    };
}

void apply_and_verify_clip_pass(
    ManualStripperClipFanoutFrame* const frame,
    ManualStripperClipFanoutState* const state) {
    const auto plan = plan_manual_stripper_clip_fanout(*frame, state);
    expect(plan.valid && !plan.already_applied && plan.native_reload_clip_calls >= 1 &&
               plan.native_reload_clip_calls <= 5,
           "a native clip pass has a bounded positive call count");
    // Model PM_ReloadClip's authored per-call policy, rather than treating a
    // successful plan itself as proof of the native ammo result.
    const int add = frame->ammo.weapon_state == 9 || frame->ammo.weapon_state == 10
        ? frame->ammo.reload_start_add : frame->ammo.reload_ammo_add;
    for (int call = 0; call < plan.native_reload_clip_calls; ++call) {
        const int available = std::min(frame->ammo.reserve, 5 - frame->ammo.loaded);
        const int transferred = add == 0 ? available : std::min(available, add);
        frame->ammo.loaded += transferred;
        frame->ammo.reserve -= transferred;
    }
    expect(frame->ammo.loaded == plan.expected_loaded &&
               frame->ammo.reserve == plan.expected_reserve &&
               verify_manual_stripper_clip_fanout(*frame, state),
           "each predicted/authoritative pass must reach the same exact post tuple");
}

void test_clip_fanout_prediction_authority_and_rewind() {
    for (const auto [loaded, reserve, expected_loaded, expected_reserve] : {
             std::tuple{0, 20, 5, 15}, std::tuple{3, 20, 5, 18},
             std::tuple{0, 2, 2, 0}, std::tuple{4, 1, 5, 0},
             std::tuple{0, 8, 5, 3}, std::tuple{4, 45, 5, 44}}) {
        for (const bool segmented : {false, true}) {
            const auto before = clip_fanout_frame(loaded, reserve, segmented);
            auto frame = before;
            ManualStripperClipFanoutState state{};
            apply_and_verify_clip_pass(&frame, &state);
            expect(frame.ammo.loaded == expected_loaded &&
                       frame.ammo.reserve == expected_reserve,
                   "one physical five-round clip fills capacity or exhausts reserve");
            const auto post = frame.ammo;
            for (int repeat = 0; repeat < 8; ++repeat) {
                ++frame.now_milliseconds;
                const auto duplicate = plan_manual_stripper_clip_fanout(frame, &state);
                expect(duplicate.valid && duplicate.already_applied &&
                           duplicate.native_reload_clip_calls == 0 &&
                           verify_manual_stripper_clip_fanout(frame, &state),
                       "already-post duplicate is a verified no-ammo-call, never a refill");
            }
            frame.player_state = 0x3000;
            frame.ammo = before.ammo;
            apply_and_verify_clip_pass(&frame, &state);
            expect(frame.ammo.loaded == post.loaded && frame.ammo.reserve == post.reserve,
                   "authoritative original snapshot receives the identical physical clip");
            frame.player_state = before.player_state;
            frame.ammo = before.ammo;
            apply_and_verify_clip_pass(&frame, &state);
            expect(frame.ammo.loaded == post.loaded && frame.ammo.reserve == post.reserve,
                   "prediction rewind in the same pointer reapplies only the original target");
            frame.player_state = 0x4000;
            expect(!plan_manual_stripper_clip_fanout(frame, &state).valid,
                   "a third native buffer is rejected by the conservative local-state cap");
        }
    }
}

void test_clip_fanout_rejects_changed_identity_ammo_and_deadline() {
    const auto before = clip_fanout_frame();
    auto frame = before;
    ManualStripperClipFanoutState committed{};
    apply_and_verify_clip_pass(&frame, &committed);
    const auto post = frame;
    for (int field = 0; field < 8; ++field) {
        auto wrong = before;
        switch (field) {
        case 0: ++wrong.key.session_epoch; break;
        case 1: wrong.key.publication_generation += 2; break;
        case 2: ++wrong.key.request_serial; break;
        case 3: ++wrong.key.shot_generation; break;
        case 4: ++wrong.key.weapon_index; break;
        case 5: ++wrong.key.definition_address; break;
        case 6: ++wrong.key.ammo_index; break;
        case 7: ++wrong.key.clip_index; break;
        }
        auto state = committed;
        expect(!plan_manual_stripper_clip_fanout(wrong, &state).valid,
               "session, publication, request, shot, weapon, definition and pool changes reject replay");
    }
    frame = post;
    --frame.ammo.loaded;
    expect(!plan_manual_stripper_clip_fanout(frame, &committed).valid,
           "a subsequent shot cannot be silently replaced from the old clip");
    frame = before;
    ++frame.ammo.reserve;
    expect(!plan_manual_stripper_clip_fanout(frame, &committed).valid,
           "new reserve pickup is not funding for a replay");
    frame = post;
    ++frame.ammo.reload_ammo_add;
    expect(!plan_manual_stripper_clip_fanout(frame, &committed).valid,
           "changed authored native policy fails closed");
    frame = before;
    frame.now_milliseconds = 999;
    expect(!plan_manual_stripper_clip_fanout(frame, &committed).valid,
           "clock regression cannot reopen replay");
    frame.now_milliseconds = 1249;
    apply_and_verify_clip_pass(&frame, &committed);
    expect(committed.started_at_milliseconds == 1000,
           "a successful late replay never extends the original deadline");
    frame.now_milliseconds = 1250;
    expect(!plan_manual_stripper_clip_fanout(frame, &committed).valid &&
               !verify_manual_stripper_clip_fanout(frame, &committed),
           "the exact fixed deadline rejects both new native work and acknowledgement");

    auto restored_after_shot = before;
    ++restored_after_shot.key.shot_generation;
    auto state = ManualStripperClipFanoutState{};
    frame = before;
    apply_and_verify_clip_pass(&frame, &state);
    expect(!plan_manual_stripper_clip_fanout(restored_after_shot, &state).valid,
           "even a shot/pickup sequence restoring original ammo cannot reuse an old shot key");
}

void test_clip_fanout_failure_reset_and_policy_boundaries() {
    auto frame = clip_fanout_frame();
    ManualStripperClipFanoutState state{};
    expect(plan_manual_stripper_clip_fanout(frame, &state).valid,
           "first pass prepares an immutable receipt");
    frame.ammo.loaded = 2;
    frame.ammo.reserve = 18;
    expect(!verify_manual_stripper_clip_fanout(frame, &state) && state.failed &&
               !plan_manual_stripper_clip_fanout(frame, &state).valid,
           "a partial native transfer is not acknowledged or topped up by replay");
    frame = clip_fanout_frame();
    expect(!plan_manual_stripper_clip_fanout(frame, &state).valid,
           "a poisoned request cannot restart from its original snapshot");
    state = {};
    const auto old_frame = frame;
    ++frame.key.request_serial;
    expect(plan_manual_stripper_clip_fanout(frame, &state).valid,
           "a deliberate reset/new physical request may reuse the same weapon bindings");
    expect(!verify_manual_stripper_clip_fanout(old_frame, &state) && !state.failed,
           "a stale old-request callback does not poison the newer receipt");
    apply_and_verify_clip_pass(&frame, &state);

    for (const auto [loaded, reserve, clip_size, capacity, bolt] : {
             std::tuple{5, 20, 5, 5, 1}, std::tuple{0, 0, 5, 5, 1},
             std::tuple{0, 20, 8, 5, 1}, std::tuple{0, 20, 5, 8, 1},
             std::tuple{0, 20, 5, 5, 0}}) {
        frame = clip_fanout_frame(loaded, reserve);
        frame.ammo.clip_size = clip_size;
        frame.ammo.physical_clip_capacity = capacity;
        frame.ammo.bolt_action = bolt;
        state = {};
        expect(!plan_manual_stripper_clip_fanout(frame, &state).valid && !state.initialized,
               "full, empty, eight-round, non-five physical and non-bolt profiles never arm fanout");
    }
    frame = clip_fanout_frame();
    state = {};
    apply_and_verify_clip_pass(&frame, &state);
    frame.ammo = clip_fanout_frame().ammo;
    frame.ammo.weapon_state = 9;
    frame.player_state = 0x3000;
    const auto reload_start = plan_manual_stripper_clip_fanout(frame, &state);
    expect(reload_start.valid && reload_start.native_reload_clip_calls == 5,
           "replay recomputes current native call policy without requiring identical weapon state");
    apply_and_verify_clip_pass(&frame, &state);
}

void test_failed_clip_request_cannot_republish_a_new_ammo_budget() {
    for (const int failure : {0, 1, 2}) {
        ManualReloadControllerState controller{};
        ManualReloadControllerFrame hand{};
        auto update = grab_and_insert_held_clip(&hand, &controller);
        const auto first_reload_generation = controller.reload.generation;
        ManualStripperClipRequestGate gate{};
        std::uint64_t physical_serial = 1;
        expect(update.reload.event == ReloadEvent::CommitRequested &&
                   begin_manual_stripper_clip_request(physical_serial, &gate),
               "one physical contact event opens exactly one native request budget");
        ManualStripperClipFanoutState receipt{};
        auto ammo = clip_fanout_frame(4, 45);
        if (failure == 0) {
            apply_and_verify_clip_pass(&ammo, &receipt);
            ammo.player_state = 0x3000;
            ammo.ammo.loaded = 4;
            ammo.ammo.reserve = 44;
            expect(!plan_manual_stripper_clip_fanout(ammo, &receipt).valid,
                   "mismatched authoritative pre tuple is rejected");
        } else if (failure == 1) {
            ammo = clip_fanout_frame();
            expect(plan_manual_stripper_clip_fanout(ammo, &receipt).valid,
                   "partial-result regression begins with one accepted request");
            ammo.ammo.loaded = 2;
            ammo.ammo.reserve = 18;
            expect(!verify_manual_stripper_clip_fanout(ammo, &receipt),
                   "partial native result poisons the receipt");
        } else {
            apply_and_verify_clip_pass(&ammo, &receipt);
            ammo.now_milliseconds += 250;
            expect(!plan_manual_stripper_clip_fanout(ammo, &receipt).valid,
                   "expired replay is rejected without a renewed window");
        }
        // Model the runtime's failed claimed->idle phase. The controller still
        // asks every render frame, but that is NOT another physical contact.
        for (int render = 0; render < 12; ++render) {
            ++hand.action_sequence;
            update = update_manual_reload_controller(hand, &controller);
            expect(update.reload.stage == ReloadStage::Committing &&
                       update.reload.request_native_commit &&
                       update.reload.event != ReloadEvent::CommitRequested &&
                       !begin_manual_stripper_clip_request(physical_serial, &gate),
                   "reject followed by render re-request cannot clear receipt or issue a new serial/budget");
        }
        // A normal bolt-close/full controller reset resets reload.generation;
        // independent physical serials still admit the next deliberate clip.
        reset_manual_reload_controller(&controller, true);
        update = grab_and_insert_held_clip(&hand, &controller);
        expect(controller.reload.generation == first_reload_generation &&
                   update.reload.event == ReloadEvent::CommitRequested,
               "second physical cycle reproduces the controller-generation alias");
        ++physical_serial;
        expect(begin_manual_stripper_clip_request(physical_serial, &gate) &&
                   !begin_manual_stripper_clip_request(physical_serial - 1, &gate),
               "new physical contact is accepted but an older repeated serial is rejected");
        receipt = {};
        ammo = clip_fanout_frame(4, 45);
        ++ammo.key.request_serial;
        apply_and_verify_clip_pass(&ammo, &receipt);
    }
}

}  // namespace

int main() {
    test_symmetric_clip_selection_and_pre_reservation();
    test_symmetric_clip_release_and_reset_boundaries();
    test_clip_candidate_handoff_and_invalid_pickups();
    test_either_bolt_hand_can_precede_either_clip_hand();
    test_physical_bolt_and_right_hand_drive_stripper_clip_sequence();
    test_composed_shot_bolt_clip_and_close_sequence();
    test_focus_recovery_cannot_synthesize_actions();
    test_held_clip_reservation_survives_commit_until_proven_release();
    test_clip_reservation_rejects_focus_and_invalid_sample_releases();
    test_clip_release_before_commit_keeps_existing_completion_behavior();
    test_clip_reservation_reset_and_profile_boundaries();
    test_native_profile_falls_back_without_suppression();
    test_left_hand_drives_detachable_magazine_sequence();
    test_receiver_tracks_the_clip_along_the_rifle_top();
    test_feed_zone_is_on_the_right_waist();
    test_detachable_feed_zone_is_on_the_left_waist();
    test_scoped_segmented_stripper_clip_plans_full_capacity();
    test_unsegmented_stripper_clip_keeps_one_native_top_up();
    test_stripper_clip_plan_rejects_unprovable_ammo_state();
    test_clip_fanout_prediction_authority_and_rewind();
    test_clip_fanout_rejects_changed_identity_ammo_and_deadline();
    test_clip_fanout_failure_reset_and_policy_boundaries();
    test_failed_clip_request_cannot_republish_a_new_ammo_budget();
    return 0;
}
