// SPDX-License-Identifier: GPL-3.0-only
#include "manual_reload_controller_logic.hpp"
#include "detachable_magazine_weapon_profile.hpp"
#include "bolt_action_weapon_profile.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace {
using namespace wawvr;
using gameplay::ReloadEvent;
using gameplay::ReloadProfileKind;
using gameplay::ReloadStage;

void require(const bool okay, const char* const message) {
    if (!okay) {
        std::fprintf(stderr, "reload-family stress failed: %s\n", message);
        std::exit(1);
    }
}

mod::ManualReloadControllerFrame frame_for(const ReloadProfileKind kind) {
    mod::ManualReloadControllerFrame frame{};
    frame.input_owned = true;
    frame.action_sequence = 1;
    frame.profile_kind = kind;
    frame.weapon_supported = true;
    frame.can_reload = true;
    frame.feed_hand = mod::StripperClipFeedHand::Left;
    frame.feed_pose_valid = true;
    return frame;
}

void run_transaction(const ReloadProfileKind kind, const bool fast_insertion,
                     const bool drop_and_recover, const bool interrupt) {
    mod::ManualReloadControllerState state{};
    auto frame = frame_for(kind);
    auto update = mod::update_manual_reload_controller(frame, &state);
    require(update.reload.stage == ReloadStage::Ready, "first action only baselines");

    frame.action_sequence++;
    frame.can_reload = false;
    frame.begin_reload_pressed_edge = true;
    update = mod::update_manual_reload_controller(frame, &state);
    require(update.reload.stage == ReloadStage::Ready &&
                !update.reload.request_native_commit,
            "full clip or absent reserve rejects an otherwise deliberate begin");
    frame.action_sequence++;
    frame.can_reload = true;
    update = mod::update_manual_reload_controller(frame, &state);
    require(update.reload.event == ReloadEvent::ReloadArmed,
            "fresh eligible deliberate begin arms the supported family");
    frame.begin_reload_pressed_edge = false;
    if (kind == ReloadProfileKind::InternalStripperClip) {
        frame.action_sequence++;
        frame.native_action_open = true;
        update = mod::update_manual_reload_controller(frame, &state);
    }
    require(update.reload.stage == ReloadStage::FeedDeviceAvailable,
            "family action exposes ammunition only after its open-action policy");

    frame.action_sequence++;
    frame.feed_grip_held = true;
    frame.hand_in_feed_device_zone = true;
    update = mod::update_manual_reload_controller(frame, &state);
    require(update.reload.event == ReloadEvent::FeedDeviceGrabbed &&
                update.reload.render_detached_feed_device,
            "free-hand belt squeeze owns exactly one feed device");
    frame.hand_in_feed_device_zone = false;

    if (drop_and_recover) {
        frame.action_sequence++;
        frame.feed_pose_valid = false;
        update = mod::update_manual_reload_controller(frame, &state);
        require(update.reload.event == ReloadEvent::FeedDeviceDropped &&
                    !update.reload.request_native_commit,
                "lost tracked device cannot transfer ammunition");
        frame.action_sequence++;
        frame.feed_pose_valid = true;
        frame.hand_in_feed_device_zone = true;
        update = mod::update_manual_reload_controller(frame, &state);
        require(update.reload.stage == ReloadStage::FeedDeviceAvailable,
                "tracking restoration with held squeeze cannot synthesize pickup");
        frame.action_sequence++;
        frame.feed_grip_held = false;
        frame.feed_grip_release_observed = true;
        static_cast<void>(mod::update_manual_reload_controller(frame, &state));
        frame.action_sequence++;
        frame.feed_grip_held = true;
        frame.feed_grip_release_observed = false;
        update = mod::update_manual_reload_controller(frame, &state);
        require(update.reload.event == ReloadEvent::FeedDeviceGrabbed,
                "actual release and regrip recovers the dropped device");
        frame.hand_in_feed_device_zone = false;
    }

    if (interrupt) {
        frame.action_sequence++;
        frame.input_owned = false;
        update = mod::update_manual_reload_controller(frame, &state);
        require(update.reload.event == ReloadEvent::Cancelled &&
                    !update.reload.request_native_commit &&
                    !update.reload.hide_authored_feed_device,
                "focus interruption cancels and restores authored visibility");
        frame.action_sequence++;
        frame.input_owned = true;
        frame.begin_reload_pressed_edge = true;
        update = mod::update_manual_reload_controller(frame, &state);
        require(update.reload.stage == ReloadStage::Ready &&
                    !update.begin_reload_pressed_edge,
                "focus restoration cannot manufacture a new reload edge");
        return;
    }

    frame.action_sequence++;
    frame.hand_in_insertion_zone = true;
    frame.feed_grip_held =
        kind == ReloadProfileKind::InternalStripperClip || !fast_insertion;
    frame.feed_grip_release_observed = !frame.feed_grip_held;
    update = mod::update_manual_reload_controller(frame, &state);
    if (kind == ReloadProfileKind::InternalStripperClip || !fast_insertion) {
        require(update.reload.stage == ReloadStage::NearInsertionPoint,
                "held insertion enters the family guide");
        frame.action_sequence++;
        if (kind == ReloadProfileKind::DetachableMagazine) {
            frame.feed_grip_held = false;
            frame.feed_grip_release_observed = true;
        }
        update = mod::update_manual_reload_controller(frame, &state);
    }
    require(update.reload.event == ReloadEvent::CommitRequested &&
                update.reload.request_native_commit,
            "contact or deliberate in-well release requests one transfer");
    unsigned commit_events = 1;
    for (int sample = 0; sample < 32; ++sample) {
        if ((sample & 1) == 0) frame.action_sequence++;
        update = mod::update_manual_reload_controller(frame, &state);
        commit_events += update.reload.event == ReloadEvent::CommitRequested;
        require(update.reload.stage == ReloadStage::Committing &&
                    update.reload.request_native_commit,
                "duplicate and fresh samples retain the same unacknowledged request");
    }
    frame.action_sequence++;
    frame.native_commit_completed = true; // Explicit integration acknowledgment.
    update = mod::update_manual_reload_controller(frame, &state);
    require(commit_events == 1 &&
                update.reload.event == ReloadEvent::CommitCompleted &&
                !update.reload.request_native_commit &&
                !update.reload.hide_authored_feed_device,
            "one policy request completes only after external acknowledgment");
    for (int repeat = 0; repeat < 8; ++repeat) {
        frame.action_sequence++;
        update = mod::update_manual_reload_controller(frame, &state);
        require(update.reload.stage == ReloadStage::Ready &&
                    !update.reload.request_native_commit,
                "stale release or acknowledgment cannot begin an automatic second reload");
    }
}
} // namespace

int main() {
    require(mod::validate_detachable_magazine_weapon_profile_registry() &&
                mod::validate_bolt_action_weapon_profile_registry(),
            "the exact supported registries remain valid");
    // These are exact Pacific weapon identities, not guessed model aliases.
    for (const std::string_view name : {"thompson", "thompson_wet", "type100_smg",
                                       "type100_smg_nosound", "m1garand", "m1carbine",
                                       "colt", "colt_wet", "nambu", "type99_lmg"}) {
        require(mod::find_detachable_magazine_weapon_profile_by_internal_name(name) != nullptr,
                "Pacific magazine identity has an exact supported reload profile");
    }
    require(mod::find_bolt_action_weapon_profile_by_internal_name("type99_rifle") != nullptr,
            "Pacific Arisaka bolt rifle has an exact supported reload profile");
    std::size_t transactions = 0;
    for (const auto* const profile : mod::detachable_magazine_weapon_profiles()) {
        for (int trial = 0; trial < 64; ++trial) {
            run_transaction(profile->reload_kind, (trial & 1) != 0,
                            (trial & 2) != 0, (trial & 4) != 0);
            ++transactions;
        }
    }
    for (const auto* const profile : mod::bolt_action_weapon_profiles()) {
        for (int trial = 0; trial < 64; ++trial) {
            run_transaction(profile->reload_kind, false,
                            (trial & 2) != 0, (trial & 4) != 0);
            ++transactions;
        }
    }
    std::printf("portable-policy-only: detachableProfiles=%zu boltProfiles=%zu transactions=%zu; nativeAmmoAndLiveAssetsNotAsserted\n",
        mod::detachable_magazine_weapon_profiles().size(),
        mod::bolt_action_weapon_profiles().size(), transactions);
    return 0;
}
