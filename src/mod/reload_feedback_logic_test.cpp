// SPDX-License-Identifier: GPL-3.0-only
#include "reload_feedback_logic.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace {
using namespace wawvr::mod;
using wawvr::gameplay::ReloadEvent;
using wawvr::gameplay::ReloadProfileKind;
int failures{};
void expect(const bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

void stage_policy() {
    for (const auto profile : {ReloadProfileKind::DetachableMagazine,
                              ReloadProfileKind::InternalStripperClip}) {
        for (const auto event : {ReloadEvent::None, ReloadEvent::FeedDeviceAvailable,
             ReloadEvent::FeedDeviceDropped, ReloadEvent::InsertionAssistStarted,
             ReloadEvent::InsertionAssistEnded, ReloadEvent::CommitRequested,
             ReloadEvent::Cancelled}) {
            expect(reload_feedback_step(event, profile) == ReloadFeedbackStep::None,
                   "requests, retries, guide proximity, drops and cancellation are silent");
        }
        expect(reload_feedback_step(ReloadEvent::FeedDeviceGrabbed, profile) == ReloadFeedbackStep::Grabbed,
               "physical pickup produces grab cue");
        expect(reload_feedback_step(ReloadEvent::CommitCompleted, profile) == ReloadFeedbackStep::Inserted,
               "only acknowledged commit produces insertion cue");
    }
    expect(reload_feedback_step(ReloadEvent::ReloadArmed, ReloadProfileKind::DetachableMagazine) == ReloadFeedbackStep::Ejected,
           "detachable reload arm is physical eject");
    expect(reload_feedback_step(ReloadEvent::ReloadArmed, ReloadProfileKind::InternalStripperClip) == ReloadFeedbackStep::None,
           "bolt grab arming is not a magazine eject");
    expect(reload_feedback_step(ReloadEvent::CommitCompleted, ReloadProfileKind::NativeOnly) == ReloadFeedbackStep::None,
           "automatic and unsupported native reloads receive no duplicate cue");
}

void actual_transaction_completion() {
    const auto profile = wawvr::gameplay::make_reload_profile(ReloadProfileKind::DetachableMagazine);
    wawvr::gameplay::ReloadState state{};
    wawvr::gameplay::ReloadInput input{};
    input.enabled = input.focused = input.weapon_supported = input.can_reload = true;
    input.reload_pressed_edge = true;
    auto update = wawvr::gameplay::update_manual_reload(profile, input, &state);
    expect(reload_feedback_step(update.event, profile.kind) == ReloadFeedbackStep::Ejected, "real transaction eject");
    input.reload_pressed_edge = false;
    input.off_hand_pose_valid = input.grip_pressed_edge = input.grip_held = input.hand_in_feed_device_zone = true;
    update = wawvr::gameplay::update_manual_reload(profile, input, &state);
    expect(reload_feedback_step(update.event, profile.kind) == ReloadFeedbackStep::Grabbed, "real transaction pickup");
    input.grip_pressed_edge = false;
    input.hand_in_insertion_zone = true;
    update = wawvr::gameplay::update_manual_reload(profile, input, &state);
    expect(reload_feedback_step(update.event, profile.kind) == ReloadFeedbackStep::None, "guide alone is silent");
    input.grip_held = false;
    input.grip_released_edge = true;
    update = wawvr::gameplay::update_manual_reload(profile, input, &state);
    expect(update.request_native_commit && reload_feedback_step(update.event, profile.kind) == ReloadFeedbackStep::None,
           "first unacknowledged native request is silent");
    input.grip_released_edge = false;
    for (int retry = 0; retry < 300; ++retry) {
        update = wawvr::gameplay::update_manual_reload(profile, input, &state);
        expect(update.request_native_commit && reload_feedback_step(update.event, profile.kind) == ReloadFeedbackStep::None,
               "failed/unacknowledged native retry never produces insertion success");
    }
    input.native_commit_completed = true;
    update = wawvr::gameplay::update_manual_reload(profile, input, &state);
    expect(reload_feedback_step(update.event, profile.kind) == ReloadFeedbackStep::Inserted, "acknowledged transfer sounds once");
    for (int repeated = 0; repeated < 100; ++repeated) {
        update = wawvr::gameplay::update_manual_reload(profile, input, &state);
        expect(reload_feedback_step(update.event, profile.kind) == ReloadFeedbackStep::None,
               "retained completion flag cannot repeat insertion cue");
    }
}

void mailbox() {
    ReloadFeedbackMailbox box{};
    expect(!queue_reload_feedback_event(&box, ReloadFeedbackStep::Inserted, ReloadFeedbackHand::Left, 1, 1000), "unarmed rejects");
    set_reload_feedback_acceptance(&box, true, 1000);
    expect(queue_reload_feedback_event(&box, ReloadFeedbackStep::Inserted, ReloadFeedbackHand::Left, 1, 1000), "fresh accepts");
    expect(!queue_reload_feedback_event(&box, ReloadFeedbackStep::Inserted, ReloadFeedbackHand::Left, 1, 1000), "duplicate producer rejected");
    auto batch = consume_reload_feedback_events(&box, 1001);
    expect(batch.count == 1 && batch.cues[0].hand == ReloadFeedbackHand::Left && batch.cues[0].serial == 1, "left receipt retained");
    expect(consume_reload_feedback_events(&box, 1002).count == 0, "consumes once");
    expect(!queue_reload_feedback_event(&box, ReloadFeedbackStep::Inserted, ReloadFeedbackHand::Left, 1, 1002), "duplicate after drain rejected");
    expect(queue_reload_feedback_event(&box, ReloadFeedbackStep::ActionClosed, ReloadFeedbackHand::Right, 1, 1002), "different same-frame endpoint allowed");
    expect(consume_reload_feedback_events(&box, 1002).cues[0].hand == ReloadFeedbackHand::Right, "right action hand retained");
    expect(queue_reload_feedback_event(&box, ReloadFeedbackStep::Grabbed, ReloadFeedbackHand::Left, 2, 1003), "next physical action accepted");
    expect(!queue_reload_feedback_event(&box, ReloadFeedbackStep::Ejected, ReloadFeedbackHand::Right, 1, 1004), "old action sequence rejected");
    clear_reload_feedback_pending(&box);
    expect(consume_reload_feedback_events(&box, 1004).count == 0, "weapon/reset drops pending cue");
    expect(queue_reload_feedback_event(&box, ReloadFeedbackStep::Grabbed, ReloadFeedbackHand::Right, 3, 1004), "new scoped action allowed");
    set_reload_feedback_acceptance(&box, false, 1005);
    set_reload_feedback_acceptance(&box, true, 1006);
    expect(consume_reload_feedback_events(&box, 1006).count == 0, "focus recovery never replays old cue");
    expect(queue_reload_feedback_event(&box, ReloadFeedbackStep::Inserted, ReloadFeedbackHand::Right, 4, 1006), "fresh completion accepted");
    set_reload_feedback_acceptance(&box, true, 1107);
    expect(consume_reload_feedback_events(&box, 1107).count == 0, "service hitch clears old cue");
    expect(queue_reload_feedback_event(&box, ReloadFeedbackStep::Inserted, ReloadFeedbackHand::Right, 5, 1107), "new epoch accepts");
    expect(consume_reload_feedback_events(&box, 1106).count == 0, "clock regression disarms");

    set_reload_feedback_acceptance(&box, true, 2000);
    for (std::uint64_t sequence = 1; sequence <= 20; ++sequence) {
        const bool queued = queue_reload_feedback_event(&box, ReloadFeedbackStep::Inserted, ReloadFeedbackHand::Left, sequence, 2000);
        expect(queued == (sequence <= kReloadFeedbackBatchCapacity), "bounded capacity drops overflow");
    }
    expect(consume_reload_feedback_events(&box, 2001).count == kReloadFeedbackBatchCapacity, "bounded drain");
    expect(!queue_reload_feedback_event(&box, ReloadFeedbackStep::Inserted, ReloadFeedbackHand::Left, 20, 2001), "dropped overflow is not a delayed retry");
    expect(!queue_reload_feedback_event(&box, ReloadFeedbackStep::None, ReloadFeedbackHand::Left, 21, 2001), "none cannot make feedback");
    expect(!queue_reload_feedback_event(&box, ReloadFeedbackStep::Grabbed, ReloadFeedbackHand::Left, 0, 2001), "zero sequence rejected");
    expect(!queue_reload_feedback_event(nullptr, ReloadFeedbackStep::Grabbed, ReloadFeedbackHand::Left, 1, 2001), "null mailbox rejected");
}

void samples() {
    expect(make_reload_feedback_samples(ReloadFeedbackStep::None) == ReloadFeedbackSamples{}, "none is silent PCM");
    for (const auto step : {ReloadFeedbackStep::Ejected, ReloadFeedbackStep::Grabbed,
         ReloadFeedbackStep::Inserted, ReloadFeedbackStep::ActionOpened, ReloadFeedbackStep::ActionClosed,
         ReloadFeedbackStep::ChargeReady}) {
        const auto pcm = make_reload_feedback_samples(step);
        expect(pcm == make_reload_feedback_samples(step), "PCM deterministic and independent of game assets");
        expect(pcm.front() == 0 && pcm.back() == 0, "PCM endpoints silent");
        int peak = 0;
        for (const auto sample : pcm) peak = std::max(peak, std::abs(static_cast<int>(sample)));
        expect(peak > 100 && peak < 4096, "cue present with conservative peak below one eighth full-scale");
        expect(reload_feedback_amplitude(step) > 0.0F && reload_feedback_amplitude(step) <= 0.32F &&
               reload_feedback_duration_seconds(step) > 0.0F && reload_feedback_duration_seconds(step) <= 0.045F,
               "reload haptics bounded below shot feedback strength and duration");
    }
}
}

int main() {
    stage_policy(); actual_transaction_completion(); mailbox(); samples();
    if (failures != 0) return 1;
    std::cout << "Reload feedback policy, completion, freshness, hand, deduplication and PCM checks passed\n";
}
