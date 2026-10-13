// SPDX-License-Identifier: GPL-3.0-only
#include "kar98_bolt_action_logic.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

using namespace wawvr::mod;

void expect(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

Kar98BoltActionFrame tracked_frame(const std::uint64_t sequence) {
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

Kar98BoltActionFrame tracked_left_frame(const std::uint64_t sequence) {
    Kar98BoltActionFrame frame{};
    frame.enabled = true;
    frame.focused = true;
    frame.weapon_supported = true;
    frame.action_sequence = sequence;
    frame.right_rifle_gripped = true;
    frame.left_hand_pose_valid = true;
    frame.left_hand_near_bolt = true;
    frame.left_trigger_active = true;
    frame.right_trigger_active = true;
    return frame;
}

void expect_same_state(
    const Kar98BoltActionState& left,
    const Kar98BoltActionState& right,
    const char* const message) {
    expect(left.input_owned == right.input_owned &&
               left.last_action_sequence == right.last_action_sequence &&
               left.trigger_was_held == right.trigger_was_held &&
               left.left_trigger_was_held ==
                   right.left_trigger_was_held &&
               left.manipulating_hand_selected ==
                   right.manipulating_hand_selected &&
               left.manipulating_left_hand ==
                   right.manipulating_left_hand &&
               left.bolt_fraction == right.bolt_fraction &&
               left.bolt_grabbed == right.bolt_grabbed &&
               left.grab_started_closed == right.grab_started_closed &&
               left.grab_start_coordinate == right.grab_start_coordinate &&
               left.grab_start_fraction == right.grab_start_fraction &&
               left.fully_opened == right.fully_opened &&
               left.cycle_required == right.cycle_required &&
               left.awaiting_controls_release ==
                   right.awaiting_controls_release,
           message);
}

void expect_same_update(
    const Kar98BoltActionUpdate& left,
    const Kar98BoltActionUpdate& right,
    const char* const message) {
    expect(left.event == right.event &&
               left.bolt_fraction == right.bolt_fraction &&
               left.trigger_pressed_edge == right.trigger_pressed_edge &&
               left.trigger_released_edge == right.trigger_released_edge &&
               left.bolt_grabbed == right.bolt_grabbed &&
               left.begin_reload_gesture == right.begin_reload_gesture &&
               left.action_open == right.action_open &&
               left.cycle_required == right.cycle_required &&
               left.block_attack == right.block_attack &&
               left.reserve_right_grip == right.reserve_right_grip &&
               left.reserve_left_grip == right.reserve_left_grip &&
               left.manipulating_left_hand ==
                   right.manipulating_left_hand,
           message);
}

void test_kar98_wrapper_exactly_matches_profile_path() {
    Kar98BoltActionState wrapper_state{};
    Kar98BoltActionState profile_state{};
    const auto advance = [&](const Kar98BoltActionFrame& frame) {
        const auto wrapper =
            update_kar98_bolt_action(frame, &wrapper_state);
        const auto generic = update_bolt_action(
            kKar98BoltActionWeaponProfile, frame, &profile_state);
        expect_same_update(
            wrapper, generic,
            "the Kar98 compatibility wrapper and profile path return the same update");
        expect_same_state(
            wrapper_state, profile_state,
            "the Kar98 compatibility wrapper and profile path retain the same state");
    };

    auto frame = tracked_frame(1);
    frame.shot_fired = true;
    frame.right_grip_held = true;
    frame.right_trigger_value = 1.0F;
    advance(frame);
    frame = tracked_frame(2);
    advance(frame);
    frame = tracked_frame(3);
    frame.right_trigger_value = 1.0F;
    advance(frame);
    frame = tracked_frame(4);
    frame.right_trigger_value = 1.0F;
    frame.right_hand_forward_coordinate = -kKar98BoltTravelUnits;
    advance(frame);
    frame = tracked_frame(5);
    advance(frame);
    frame = tracked_frame(6);
    frame.right_trigger_value = 1.0F;
    frame.right_hand_forward_coordinate = -kKar98BoltTravelUnits;
    advance(frame);
    frame = tracked_frame(7);
    frame.right_trigger_value = 1.0F;
    advance(frame);
    frame = tracked_frame(8);
    advance(frame);

    const auto wrapper_visual = decide_kar98_bolt_visual(true, 0.5F, true);
    const auto profile_visual = decide_bolt_action_bolt_visual(
        kKar98BoltActionWeaponProfile, true, 0.5F, true);
    expect(wrapper_visual.capture_closed_pose ==
                   profile_visual.capture_closed_pose &&
               wrapper_visual.apply_manual_pose ==
                   profile_visual.apply_manual_pose,
           "the Kar98 visual wrapper exactly matches its profile path");
    expect(select_kar98_rechamber_visual_anim(
               true, true, true, kKar98WeaponAnimRechamberHip) ==
               select_bolt_action_rechamber_visual_anim(
                   kKar98BoltActionWeaponProfile, true, true, true,
                   kKar98WeaponAnimRechamberHip),
           "the Kar98 animation wrapper exactly matches its profile path");
}

void test_generic_path_consumes_profile_calibration() {
    BoltActionWeaponProfile synthetic = kKar98BoltActionWeaponProfile;
    synthetic.internal_weapon_name = "synthetic_bolt";
    synthetic.viewmodel_model_name = "viewmodel_synthetic_bolt";
    synthetic.bolt_travel_units = 7.5F;
    synthetic.bolt_open_threshold = 0.90F;

    Kar98BoltActionState state{};
    auto frame = tracked_frame(1);
    static_cast<void>(update_bolt_action(synthetic, frame, &state));
    frame = tracked_frame(2);
    frame.right_trigger_value = 1.0F;
    static_cast<void>(update_bolt_action(synthetic, frame, &state));
    frame = tracked_frame(3);
    frame.right_trigger_value = 1.0F;
    frame.right_hand_forward_coordinate = -3.75F;
    const auto update = update_bolt_action(synthetic, frame, &state);
    expect(std::abs(update.bolt_fraction - 0.5F) < 0.0001F &&
               !update.action_open,
           "generic bolt travel uses the selected profile rather than Kar98 constants");

    Kar98BoltVector travel{};
    const float identity[4]{0.0F, 0.0F, 0.0F, 1.0F};
    expect(calculate_bolt_action_bolt_parent_travel(
               synthetic, identity, 1.0F, &travel) &&
               std::abs(travel.x + 7.5F) < 0.0001F,
           "generic visual travel uses the selected profile stroke");
}

void test_springfield_profile_drives_generic_bolt_and_animation() {
    Kar98BoltVector travel{};
    const float identity[4]{0.0F, 0.0F, 0.0F, 1.0F};
    expect(calculate_bolt_action_bolt_parent_travel(
               kSpringfieldBoltActionWeaponProfile, identity, 1.0F,
               &travel) &&
               std::abs(
                   travel.x +
                   kSpringfieldBoltActionWeaponProfile.bolt_travel_units) <
                   0.0001F &&
               std::abs(travel.y) < 0.0001F &&
               std::abs(travel.z) < 0.0001F,
           "Springfield profile moves its bolt through the decoded local-X stroke");
    expect(select_bolt_action_rechamber_visual_anim(
               kSpringfieldBoltActionWeaponProfile, true, true, true, 4) ==
                   0 &&
               select_bolt_action_rechamber_visual_anim(
                   kSpringfieldBoltActionWeaponProfile, true, true, true,
                   7) == 0 &&
               select_bolt_action_rechamber_visual_anim(
                   kSpringfieldBoltActionWeaponProfile, true, true, true,
                   5) == 5,
           "Springfield suppresses only the decoded automatic rechamber visuals");

    Kar98BoltActionState state{};
    auto frame = tracked_frame(1);
    static_cast<void>(update_bolt_action(
        kSpringfieldBoltActionWeaponProfile, frame, &state));
    frame = tracked_frame(2);
    frame.right_trigger_value = 1.0F;
    auto update = update_bolt_action(
        kSpringfieldBoltActionWeaponProfile, frame, &state);
    expect(update.bolt_grabbed && update.begin_reload_gesture,
           "Springfield closed-bolt trigger grab begins a feed transaction");
    frame = tracked_frame(3);
    frame.right_trigger_value = 1.0F;
    frame.right_hand_forward_coordinate =
        -kSpringfieldBoltActionWeaponProfile.bolt_travel_units;
    update = update_bolt_action(
        kSpringfieldBoltActionWeaponProfile, frame, &state);
    expect(update.action_open && update.bolt_fraction == 1.0F,
           "Springfield reaches the full-open endpoint at its decoded stroke");
    frame = tracked_frame(4);
    static_cast<void>(update_bolt_action(
        kSpringfieldBoltActionWeaponProfile, frame, &state));
    frame = tracked_frame(5);
    frame.right_trigger_value = 1.0F;
    frame.right_hand_forward_coordinate =
        -kSpringfieldBoltActionWeaponProfile.bolt_travel_units;
    update = update_bolt_action(
        kSpringfieldBoltActionWeaponProfile, frame, &state);
    expect(update.bolt_grabbed && !update.begin_reload_gesture,
           "Springfield open bolt can be regripped for closing");
    frame = tracked_frame(6);
    frame.right_trigger_value = 1.0F;
    frame.right_hand_forward_coordinate = 0.0F;
    update = update_bolt_action(
        kSpringfieldBoltActionWeaponProfile, frame, &state);
    expect(update.event == Kar98BoltEvent::FullyClosed &&
               update.bolt_fraction == 0.0F,
           "Springfield completes one full manual open-close cycle");
}

void test_type99_profile_drives_full_manual_cycle() {
    Kar98BoltVector travel{};
    const float identity[4]{0.0F, 0.0F, 0.0F, 1.0F};
    expect(calculate_bolt_action_bolt_parent_travel(
               kType99BoltActionWeaponProfile, identity, 1.0F, &travel) &&
               std::abs(
                   travel.x +
                   kType99BoltActionWeaponProfile.bolt_travel_units) <
                   0.0001F &&
               std::abs(travel.y) < 0.0001F &&
               std::abs(travel.z) < 0.0001F,
           "Type 99 profile moves its bolt through the measured native local-X stroke");
    expect(select_bolt_action_rechamber_visual_anim(
               kType99BoltActionWeaponProfile, true, true, true, 4) == 0 &&
               select_bolt_action_rechamber_visual_anim(
                   kType99BoltActionWeaponProfile, true, true, true, 7) == 0 &&
               select_bolt_action_rechamber_visual_anim(
                   kType99BoltActionWeaponProfile, true, true, true, 5) == 5,
           "Type 99 suppresses only the automatic rechamber visuals");

    Kar98BoltActionState state{};
    auto frame = tracked_frame(1);
    frame.shot_fired = true;
    frame.right_grip_held = true;
    frame.right_trigger_value = 1.0F;
    auto update = update_bolt_action(
        kType99BoltActionWeaponProfile, frame, &state);
    expect(update.cycle_required && update.block_attack,
           "a Type 99 shot requires a physical bolt cycle before another shot");

    frame = tracked_frame(2);
    static_cast<void>(update_bolt_action(
        kType99BoltActionWeaponProfile, frame, &state));
    frame = tracked_frame(3);
    frame.right_trigger_value = 1.0F;
    update = update_bolt_action(
        kType99BoltActionWeaponProfile, frame, &state);
    expect(update.bolt_grabbed,
           "the free right trigger grabs the Type 99 bolt while the left hand retains the rifle");
    frame = tracked_frame(4);
    frame.right_trigger_value = 1.0F;
    frame.right_hand_forward_coordinate =
        -kType99BoltActionWeaponProfile.bolt_travel_units;
    update = update_bolt_action(
        kType99BoltActionWeaponProfile, frame, &state);
    expect(update.action_open && update.bolt_fraction == 1.0F,
           "the Type 99 reaches the full-open endpoint at its measured stroke");
    frame = tracked_frame(5);
    static_cast<void>(update_bolt_action(
        kType99BoltActionWeaponProfile, frame, &state));
    frame = tracked_frame(6);
    frame.right_trigger_value = 1.0F;
    frame.right_hand_forward_coordinate =
        -kType99BoltActionWeaponProfile.bolt_travel_units;
    static_cast<void>(update_bolt_action(
        kType99BoltActionWeaponProfile, frame, &state));
    frame = tracked_frame(7);
    frame.right_trigger_value = 1.0F;
    frame.right_hand_forward_coordinate = 0.0F;
    update = update_bolt_action(
        kType99BoltActionWeaponProfile, frame, &state);
    expect(update.event == Kar98BoltEvent::FullyClosed &&
               update.bolt_fraction == 0.0F && update.block_attack,
           "the Type 99 completes a full manual open-close cycle before controls settle");
    frame = tracked_frame(8);
    update = update_bolt_action(
        kType99BoltActionWeaponProfile, frame, &state);
    expect(update.event == Kar98BoltEvent::ControlsRearmed &&
               !update.block_attack,
           "releasing the Type 99 bolt controls rearms firing after the completed cycle");
}

void test_shot_requires_full_open_close_and_release() {
    Kar98BoltActionState state{};
    auto frame = tracked_frame(1);
    frame.shot_fired = true;
    frame.right_grip_held = true;
    frame.right_trigger_value = 1.0F;
    auto update = update_kar98_bolt_action(frame, &state);
    expect(update.cycle_required && update.block_attack &&
               !update.reserve_right_grip &&
               !update.reserve_left_grip,
           "an unresolved post-shot lock blocks attack without choosing or reserving either hand");

    frame = tracked_frame(2);
    update = update_kar98_bolt_action(frame, &state);
    expect(update.cycle_required && !update.bolt_grabbed,
           "physically releasing right grip and trigger transfers retention to the left hand without cycling");

    frame = tracked_frame(3);
    frame.right_trigger_value = 1.0F;
    update = update_kar98_bolt_action(frame, &state);
    expect(update.bolt_grabbed && update.begin_reload_gesture,
           "a fresh right-trigger edge at the bolt grabs it from closed");

    frame = tracked_frame(4);
    frame.right_trigger_value = 1.0F;
    update = update_kar98_bolt_action(frame, &state);
    expect(update.bolt_grabbed && update.bolt_fraction == 0.0F,
           "holding a grabbed closed bolt stationary keeps the physical latch");

    frame = tracked_frame(5);
    frame.right_trigger_value = 1.0F;
    frame.right_hand_forward_coordinate = -0.10F;
    update = update_kar98_bolt_action(frame, &state);
    expect(update.bolt_grabbed && update.bolt_fraction > 0.0F &&
               update.bolt_fraction < kKar98BoltClosedThreshold,
           "initial sub-threshold rearward travel keeps the physical latch");

    frame = tracked_frame(6);
    frame.right_trigger_value = 1.0F;
    frame.right_hand_forward_coordinate = -kKar98BoltTravelUnits;
    update = update_kar98_bolt_action(frame, &state);
    expect(update.action_open &&
               std::abs(update.bolt_fraction - 1.0F) < 0.0001F,
           "rearward hand travel physically opens the bolt");

    frame = tracked_frame(7);
    update = update_kar98_bolt_action(frame, &state);
    expect(!update.bolt_grabbed && update.action_open && update.block_attack,
           "releasing the trigger leaves the opened bolt in place");

    frame = tracked_frame(8);
    frame.right_trigger_value = 1.0F;
    frame.right_hand_forward_coordinate = -kKar98BoltTravelUnits;
    update = update_kar98_bolt_action(frame, &state);
    expect(update.bolt_grabbed && !update.begin_reload_gesture,
           "a fresh trigger edge regrabs the already-open bolt");

    frame = tracked_frame(9);
    frame.right_trigger_value = 1.0F;
    frame.right_hand_forward_coordinate = 0.0F;
    update = update_kar98_bolt_action(frame, &state);
    expect(!update.cycle_required && update.block_attack &&
               update.bolt_fraction == 0.0F,
           "full forward travel closes the bolt but keeps controls consumed");

    frame = tracked_frame(10);
    update = update_kar98_bolt_action(frame, &state);
    expect(!update.block_attack && !update.reserve_right_grip,
           "trigger and grip release rearm firing and ordinary right pickup");
}

void test_left_hand_completes_cycle_and_both_triggers_must_release() {
    Kar98BoltActionState state{};
    auto frame = tracked_left_frame(1);
    frame.shot_fired = true;
    // The retaining right index trigger is still held from the shot. It must
    // not become the bolt hand, and it must not permit firing to rearm after
    // the left hand closes the action.
    frame.right_trigger_active = true;
    frame.right_trigger_value = 1.0F;
    auto update = update_kar98_bolt_action(frame, &state);
    expect(update.event == Kar98BoltEvent::ShotLocked &&
               update.cycle_required && update.block_attack &&
               !update.reserve_right_grip &&
               !update.reserve_left_grip,
           "a left-capable shot lock initially reserves neither unresolved hand");

    frame = tracked_left_frame(2);
    frame.right_trigger_active = true;
    frame.right_trigger_value = 1.0F;
    frame.left_trigger_value = 1.0F;
    update = update_kar98_bolt_action(frame, &state);
    expect(update.event == Kar98BoltEvent::Grabbed &&
               update.bolt_grabbed && update.begin_reload_gesture &&
               update.manipulating_left_hand &&
               update.reserve_left_grip &&
               !update.reserve_right_grip,
           "a fresh left-trigger edge grabs the bolt while the right hand retains the rifle");

    frame = tracked_left_frame(3);
    frame.right_trigger_active = true;
    frame.right_trigger_value = 1.0F;
    frame.left_trigger_value = 1.0F;
    frame.left_hand_forward_coordinate = -kKar98BoltTravelUnits;
    update = update_kar98_bolt_action(frame, &state);
    expect(update.event == Kar98BoltEvent::FullyOpened &&
               update.action_open && update.manipulating_left_hand,
           "left-hand rearward travel reaches the full-open endpoint");

    frame = tracked_left_frame(4);
    frame.right_trigger_active = true;
    frame.right_trigger_value = 1.0F;
    update = update_kar98_bolt_action(frame, &state);
    expect(update.event == Kar98BoltEvent::Released &&
               update.action_open && !update.bolt_grabbed &&
               !update.reserve_right_grip &&
               !update.reserve_left_grip,
           "an open bolt with no selected hand remains locked but reserves neither grip");

    frame = tracked_left_frame(5);
    frame.right_trigger_active = true;
    frame.right_trigger_value = 1.0F;
    frame.left_trigger_value = 1.0F;
    frame.left_hand_forward_coordinate = -kKar98BoltTravelUnits;
    update = update_kar98_bolt_action(frame, &state);
    expect(update.event == Kar98BoltEvent::Grabbed &&
               update.bolt_grabbed && !update.begin_reload_gesture &&
               update.manipulating_left_hand,
           "the left trigger can regrab an already-open bolt for closing");

    frame = tracked_left_frame(6);
    frame.right_trigger_active = true;
    frame.right_trigger_value = 1.0F;
    frame.left_trigger_value = 1.0F;
    frame.left_hand_forward_coordinate = 0.0F;
    update = update_kar98_bolt_action(frame, &state);
    expect(update.event == Kar98BoltEvent::FullyClosed &&
               !update.cycle_required && update.block_attack &&
               update.reserve_left_grip,
           "left-hand forward travel closes the bolt but keeps controls consumed");

    frame = tracked_left_frame(7);
    frame.right_trigger_active = true;
    frame.right_trigger_value = 1.0F;
    update = update_kar98_bolt_action(frame, &state);
    expect(update.event == Kar98BoltEvent::None &&
               update.block_attack && update.reserve_left_grip,
           "releasing only the left trigger cannot rearm while the firing trigger remains held");

    frame = tracked_left_frame(8);
    frame.right_trigger_active = false;
    update = update_kar98_bolt_action(frame, &state);
    expect(update.event != Kar98BoltEvent::ControlsRearmed &&
               update.block_attack,
           "an inactive opposite-trigger sample cannot rearm a completed left-hand bolt cycle");

    frame = tracked_left_frame(9);
    frame.right_trigger_active = true;
    frame.left_trigger_value =
        (std::numeric_limits<float>::quiet_NaN)();
    update = update_kar98_bolt_action(frame, &state);
    expect(update.event != Kar98BoltEvent::ControlsRearmed &&
               update.block_attack,
           "a non-finite selected-trigger sample cannot rearm a completed left-hand bolt cycle");

    frame = tracked_left_frame(10);
    frame.right_trigger_active = true;
    frame.left_trigger_active = true;
    frame.right_trigger_value = 0.0F;
    frame.left_trigger_value = 0.0F;
    frame.left_hand_pose_valid = true;
    frame.left_grip_held = false;
    update = update_kar98_bolt_action(frame, &state);
    expect(update.event == Kar98BoltEvent::ControlsRearmed &&
               !update.block_attack &&
               !update.reserve_right_grip &&
               !update.reserve_left_grip,
           "both index triggers must be released before a left-hand cycle rearms firing");
}

void test_selected_left_hand_cannot_be_stolen_mid_gesture() {
    Kar98BoltActionState state{};
    auto frame = tracked_left_frame(1);
    static_cast<void>(update_kar98_bolt_action(frame, &state));

    frame = tracked_left_frame(2);
    frame.left_trigger_value = 1.0F;
    auto update = update_kar98_bolt_action(frame, &state);
    expect(update.bolt_grabbed && update.manipulating_left_hand,
           "left hand establishes the active bolt gesture");

    frame = tracked_left_frame(3);
    frame.left_trigger_value = 1.0F;
    frame.left_hand_forward_coordinate = 0.0F;
    frame.right_trigger_active = true;
    frame.right_trigger_value = 1.0F;
    frame.right_hand_pose_valid = true;
    frame.right_hand_near_bolt = true;
    frame.right_hand_forward_coordinate = -kKar98BoltTravelUnits;
    update = update_kar98_bolt_action(frame, &state);
    expect(update.bolt_grabbed && update.manipulating_left_hand &&
               update.bolt_fraction == 0.0F &&
               update.reserve_left_grip &&
               !update.reserve_right_grip,
           "another trigger edge and controller movement cannot steal or drive an active left-hand grab");
}

void test_every_profile_accepts_the_same_left_hand_cycle() {
    const BoltActionWeaponProfile* const profiles[]{
        &kKar98BoltActionWeaponProfile,
        &kKar98ScopedZombieBoltActionWeaponProfile,
        &kSpringfieldBoltActionWeaponProfile,
        &kMosinBoltActionWeaponProfile,
        &kMosinScopedBoltActionWeaponProfile,
        &kType99BoltActionWeaponProfile,
        &kType99BayonetBoltActionWeaponProfile,
        &kType99ScopedBoltActionWeaponProfile,
    };
    for (const BoltActionWeaponProfile* const profile : profiles) {
        Kar98BoltActionState state{};
        auto frame = tracked_left_frame(1);
        static_cast<void>(update_bolt_action(*profile, frame, &state));

        frame = tracked_left_frame(2);
        frame.left_trigger_value = 1.0F;
        auto update = update_bolt_action(*profile, frame, &state);
        expect(update.event == Kar98BoltEvent::Grabbed &&
                   update.manipulating_left_hand,
               "every registered bolt profile accepts a left-hand grab");

        frame = tracked_left_frame(3);
        frame.left_trigger_value = 1.0F;
        frame.left_hand_forward_coordinate = -profile->bolt_travel_units;
        update = update_bolt_action(*profile, frame, &state);
        expect(update.event == Kar98BoltEvent::FullyOpened &&
                   update.bolt_fraction == 1.0F,
               "every registered bolt profile reaches its calibrated left-hand open endpoint");

        frame = tracked_left_frame(4);
        static_cast<void>(update_bolt_action(*profile, frame, &state));
        frame = tracked_left_frame(5);
        frame.left_trigger_value = 1.0F;
        frame.left_hand_forward_coordinate = -profile->bolt_travel_units;
        update = update_bolt_action(*profile, frame, &state);
        expect(update.event == Kar98BoltEvent::Grabbed &&
                   update.manipulating_left_hand,
               "every registered bolt profile permits left-hand regrab while open");

        frame = tracked_left_frame(6);
        frame.left_trigger_value = 1.0F;
        frame.left_hand_forward_coordinate = 0.0F;
        update = update_bolt_action(*profile, frame, &state);
        expect(update.event == Kar98BoltEvent::FullyClosed &&
                   update.bolt_fraction == 0.0F,
               "every registered bolt profile reaches its calibrated left-hand closed endpoint");

        frame = tracked_left_frame(7);
        update = update_bolt_action(*profile, frame, &state);
        expect(update.event == Kar98BoltEvent::ControlsRearmed &&
                   !update.block_attack,
               "every registered bolt profile rearms after a completed left-hand cycle");
    }
}

void test_partial_pull_cannot_rearm_a_shot() {
    Kar98BoltActionState state{};
    auto frame = tracked_frame(1);
    frame.shot_fired = true;
    frame.right_grip_held = true;
    frame.right_trigger_value = 1.0F;
    static_cast<void>(update_kar98_bolt_action(frame, &state));
    frame = tracked_frame(2);
    static_cast<void>(update_kar98_bolt_action(frame, &state));
    frame = tracked_frame(3);
    frame.right_trigger_value = 1.0F;
    static_cast<void>(update_kar98_bolt_action(frame, &state));
    frame = tracked_frame(4);
    frame.right_trigger_value = 1.0F;
    frame.right_hand_forward_coordinate = -1.5F;
    static_cast<void>(update_kar98_bolt_action(frame, &state));
    frame = tracked_frame(5);
    frame.right_trigger_value = 1.0F;
    frame.right_hand_forward_coordinate = 0.0F;
    auto update = update_kar98_bolt_action(frame, &state);
    expect(update.cycle_required && update.block_attack,
           "returning a partly pulled bolt cannot satisfy the rechamber cycle");
    frame = tracked_frame(6);
    update = update_kar98_bolt_action(frame, &state);
    expect(update.cycle_required && update.block_attack,
           "control release does not erase an incomplete shot cycle");

    frame = tracked_frame(7);
    frame.right_trigger_value = 1.0F;
    update = update_kar98_bolt_action(frame, &state);
    expect(update.bolt_grabbed,
           "release and repress permits a fresh attempt after a partial pull");
    frame = tracked_frame(8);
    frame.right_trigger_value = 1.0F;
    frame.right_hand_forward_coordinate = -kKar98BoltTravelUnits;
    update = update_kar98_bolt_action(frame, &state);
    expect(update.action_open,
           "the recovery attempt can still reach the full-open endpoint");
    frame = tracked_frame(9);
    static_cast<void>(update_kar98_bolt_action(frame, &state));
    frame = tracked_frame(10);
    frame.right_trigger_value = 1.0F;
    frame.right_hand_forward_coordinate = -kKar98BoltTravelUnits;
    static_cast<void>(update_kar98_bolt_action(frame, &state));
    frame = tracked_frame(11);
    frame.right_trigger_value = 1.0F;
    frame.right_hand_forward_coordinate = 0.0F;
    update = update_kar98_bolt_action(frame, &state);
    expect(!update.cycle_required,
           "a later complete open-close cycle clears the original shot lock");
    frame = tracked_frame(12);
    update = update_kar98_bolt_action(frame, &state);
    expect(!update.block_attack,
           "final control release rearms firing after the recovered cycle");
}

void test_left_retention_and_free_right_hand_are_required() {
    Kar98BoltActionState state{};
    auto frame = tracked_frame(1);
    static_cast<void>(update_kar98_bolt_action(frame, &state));

    frame = tracked_frame(2);
    frame.left_rifle_gripped = false;
    frame.right_trigger_value = 1.0F;
    auto update = update_kar98_bolt_action(frame, &state);
    expect(!update.bolt_grabbed,
           "the right trigger cannot take the bolt without left rifle retention");

    frame = tracked_frame(3);
    static_cast<void>(update_kar98_bolt_action(frame, &state));
    frame = tracked_frame(4);
    frame.right_trigger_value = 1.0F;
    frame.right_grip_held = true;
    update = update_kar98_bolt_action(frame, &state);
    expect(!update.bolt_grabbed,
           "the right hand cannot grab the bolt while its grip is occupied");
}

void test_focus_recovery_baselines_held_trigger() {
    Kar98BoltActionState state{};
    auto frame = tracked_frame(1);
    frame.shot_fired = true;
    frame.right_grip_held = true;
    frame.right_trigger_value = 1.0F;
    static_cast<void>(update_kar98_bolt_action(frame, &state));
    frame = tracked_frame(2);
    frame.focused = false;
    frame.left_rifle_gripped = false;
    frame.right_trigger_value = 1.0F;
    auto update = update_kar98_bolt_action(frame, &state);
    expect(update.cycle_required && update.block_attack,
           "focus loss cannot erase a required post-shot bolt cycle");
    frame = tracked_frame(3);
    frame.right_trigger_value = 1.0F;
    update = update_kar98_bolt_action(frame, &state);
    expect(!update.trigger_pressed_edge && !update.bolt_grabbed,
           "left-hand pickup after holstering cannot synthesize a bolt grab from a held trigger");
    frame = tracked_frame(4);
    update = update_kar98_bolt_action(frame, &state);
    frame = tracked_frame(5);
    frame.right_trigger_value = 1.0F;
    update = update_kar98_bolt_action(frame, &state);
    expect(update.bolt_grabbed && update.cycle_required,
           "a real release and repress can resume the preserved cycle after pickup");
}

void test_bolt_travel_rotates_with_the_dobj_root() {
    Kar98BoltVector travel{};
    const float identity[4]{0.0F, 0.0F, 0.0F, 1.0F};
    expect(calculate_kar98_bolt_parent_travel(identity, 1.0F, &travel) &&
               std::abs(travel.x + kKar98BoltTravelUnits) < 0.0001F &&
               std::abs(travel.y) < 0.0001F &&
               std::abs(travel.z) < 0.0001F,
           "an unrotated rifle moves its bolt rearward on local X");

    constexpr float kHalfSqrtTwo = 0.70710678118F;
    const float quarter_turn_z[4]{
        0.0F, 0.0F, kHalfSqrtTwo, kHalfSqrtTwo};
    expect(calculate_kar98_bolt_parent_travel(
               quarter_turn_z, 1.0F, &travel) &&
               std::abs(travel.x) < 0.0001F &&
               std::abs(travel.y + kKar98BoltTravelUnits) < 0.0001F &&
               std::abs(travel.z) < 0.0001F,
           "a ninety-degree root rotates bolt travel into parent-space Y");
}

void test_closed_visual_pose_is_latched_and_owned_every_frame() {
    auto decision = decide_kar98_bolt_visual(false, 0.0F, false);
    expect(decision.capture_closed_pose && decision.apply_manual_pose,
           "the first quiet closed frame can establish visual ownership");

    decision = decide_kar98_bolt_visual(true, 0.0F, false);
    expect(!decision.capture_closed_pose && decision.apply_manual_pose,
           "an idle native animation cannot overwrite or bypass the latched closed pose");

    decision = decide_kar98_bolt_visual(true, 0.0F, true);
    expect(!decision.capture_closed_pose && decision.apply_manual_pose,
           "the latched closed pose remains authoritative as a shot cycle begins");

    decision = decide_kar98_bolt_visual(true, 1.0F, true);
    expect(!decision.capture_closed_pose && decision.apply_manual_pose,
           "manual visual ownership remains active at the open endpoint");

    decision = decide_kar98_bolt_visual(false, 0.0F, true);
    expect(!decision.capture_closed_pose && !decision.apply_manual_pose,
           "a native post-shot frame cannot become the closed baseline");
}

void test_only_automatic_rechamber_visuals_are_suppressed() {
    expect(select_kar98_rechamber_visual_anim(
               true, true, true, kKar98WeaponAnimRechamberHip) ==
               kKar98WeaponAnimIdle,
           "the owned Kar98 cycle suppresses the stock hip rechamber visual");
    expect(select_kar98_rechamber_visual_anim(
               true, true, true, kKar98WeaponAnimRechamberAds) ==
               kKar98WeaponAnimIdle,
           "the owned Kar98 cycle suppresses the stock ADS rechamber visual");
    expect(select_kar98_rechamber_visual_anim(true, true, true, 5) == 5,
           "the Kar98 firing animation remains native");
    expect(select_kar98_rechamber_visual_anim(
               true, false, true, kKar98WeaponAnimRechamberHip) ==
               kKar98WeaponAnimRechamberHip,
           "ordinary native rechambering remains intact outside a physical cycle");
    expect(select_kar98_rechamber_visual_anim(
               true, true, false, kKar98WeaponAnimRechamberAds) ==
               kKar98WeaponAnimRechamberAds,
           "other weapons retain their native rechamber animation");
    expect(select_kar98_rechamber_visual_anim(
               false, true, true, kKar98WeaponAnimRechamberHip) ==
               kKar98WeaponAnimRechamberHip,
           "a disabled runtime is exactly stock");
}

}  // namespace

int main() {
    test_kar98_wrapper_exactly_matches_profile_path();
    test_generic_path_consumes_profile_calibration();
    test_springfield_profile_drives_generic_bolt_and_animation();
    test_type99_profile_drives_full_manual_cycle();
    test_shot_requires_full_open_close_and_release();
    test_left_hand_completes_cycle_and_both_triggers_must_release();
    test_selected_left_hand_cannot_be_stolen_mid_gesture();
    test_every_profile_accepts_the_same_left_hand_cycle();
    test_partial_pull_cannot_rearm_a_shot();
    test_left_retention_and_free_right_hand_are_required();
    test_focus_recovery_baselines_held_trigger();
    test_bolt_travel_rotates_with_the_dobj_root();
    test_closed_visual_pose_is_latched_and_owned_every_frame();
    test_only_automatic_rechamber_visuals_are_suppressed();
    return 0;
}
