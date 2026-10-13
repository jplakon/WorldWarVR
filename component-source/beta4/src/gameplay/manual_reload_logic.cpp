// SPDX-License-Identifier: GPL-3.0-only
#include "manual_reload_logic.hpp"

namespace wawvr::gameplay {
namespace {

[[nodiscard]] bool is_feed_device_held(const ReloadStage stage) noexcept {
    return stage == ReloadStage::HoldingFeedDevice ||
           stage == ReloadStage::NearInsertionPoint;
}

[[nodiscard]] ReloadOutput describe_output(
    const ReloadProfile& profile,
    const ReloadState& state,
    const ReloadEvent event) noexcept {
    ReloadOutput output{};
    output.stage = state.stage;
    output.event = event;
    output.manual_reload_active = state.stage != ReloadStage::Ready;
    output.suppress_native_automatic_commit =
        output.manual_reload_active &&
        profile.suppress_native_automatic_commit;

    // A detachable magazine is absent from the authored weapon for the whole
    // physical transaction: after ejection, while available at the belt,
    // while held, and until the exactly-once native commit is acknowledged.
    // Keeping this state explicit lets the retail renderer suppress only the
    // magazine surface reversibly; cancellation or commit completion returns
    // to the authored loaded model immediately.
    output.hide_authored_feed_device =
        output.manual_reload_active &&
        profile.kind == ReloadProfileKind::DetachableMagazine &&
        profile.hide_authored_device_while_held;

    if (!is_feed_device_held(state.stage)) {
        return output;
    }

    output.render_detached_feed_device = true;
    output.hide_authored_feed_device =
        profile.hide_authored_device_while_held;
    output.position_source = ReloadPositionSource::CurrentTrackedOffHand;
    output.orientation_source =
        state.stage == ReloadStage::NearInsertionPoint &&
                profile.orientation_assist
            ? ReloadOrientationSource::InsertionGuide
            : ReloadOrientationSource::CurrentTrackedOffHand;
    return output;
}

void reset_state(ReloadState* const state) noexcept {
    const auto next_generation = state->generation + 1U;
    *state = {};
    state->generation = next_generation;
}

}  // namespace

ReloadProfile make_reload_profile(const ReloadProfileKind kind) noexcept {
    ReloadProfile profile{};
    profile.kind = kind;

    switch (kind) {
        case ReloadProfileKind::DetachableMagazine:
            profile.manual_interaction = true;
            profile.request_action_open_on_begin = true;
            profile.orientation_assist = true;
            profile.commit_on_grip_release = true;
            profile.suppress_native_automatic_commit = true;
            profile.hide_authored_device_while_held = true;
            break;

        case ReloadProfileKind::InternalStripperClip:
            profile.manual_interaction = true;
            profile.requires_action_open = true;
            profile.request_action_open_on_begin = true;
            profile.orientation_assist = true;
            profile.commit_on_contact = true;
            profile.suppress_native_automatic_commit = true;
            profile.hide_authored_device_while_held = true;
            break;

        case ReloadProfileKind::NativeOnly:
        case ReloadProfileKind::SingleRoundOrTube:
            // Single-round/tube reload needs its own repeated-round policy.
            // Until that policy exists, it remains safely native-only.
            break;
    }
    return profile;
}

ReloadOutput update_manual_reload(
    const ReloadProfile& profile,
    const ReloadInput& input,
    ReloadState* const state) noexcept {
    if (state == nullptr) {
        return {};
    }

    const bool context_valid = input.enabled && input.focused &&
                               input.weapon_supported &&
                               profile.manual_interaction;
    const bool profile_changed =
        state->stage != ReloadStage::Ready &&
        state->active_profile != profile.kind;
    if (input.reset_requested || !context_valid || profile_changed) {
        const bool was_active = state->stage != ReloadStage::Ready;
        reset_state(state);
        auto output = describe_output(
            profile,
            *state,
            was_active ? ReloadEvent::Cancelled : ReloadEvent::None);
        output.request_native_cancel = was_active;
        return output;
    }

    ReloadEvent event = ReloadEvent::None;
    bool request_action_open = false;
    bool request_commit = false;

    switch (state->stage) {
        case ReloadStage::Ready:
            if (!input.reload_pressed_edge || !input.can_reload) {
                break;
            }
            state->active_profile = profile.kind;
            ++state->generation;
            request_action_open = profile.request_action_open_on_begin;
            state->stage = profile.requires_action_open
                               ? ReloadStage::ActionOpen
                               : ReloadStage::FeedDeviceAvailable;
            event = ReloadEvent::ReloadArmed;
            break;

        case ReloadStage::ActionOpen:
            if (input.native_action_open) {
                state->stage = ReloadStage::FeedDeviceAvailable;
                event = ReloadEvent::FeedDeviceAvailable;
            }
            break;

        case ReloadStage::FeedDeviceAvailable:
            if (input.off_hand_pose_valid && input.grip_pressed_edge &&
                input.grip_held && input.hand_in_feed_device_zone) {
                state->stage = ReloadStage::HoldingFeedDevice;
                event = ReloadEvent::FeedDeviceGrabbed;
            }
            break;

        case ReloadStage::HoldingFeedDevice:
            if (!input.off_hand_pose_valid) {
                state->stage = ReloadStage::FeedDeviceAvailable;
                event = ReloadEvent::FeedDeviceDropped;
                break;
            }
            if (input.hand_in_insertion_zone && input.grip_held) {
                state->stage = ReloadStage::NearInsertionPoint;
                event = ReloadEvent::InsertionAssistStarted;
                break;
            }
            if (input.grip_released_edge || !input.grip_held) {
                state->stage = ReloadStage::FeedDeviceAvailable;
                event = ReloadEvent::FeedDeviceDropped;
            }
            break;

        case ReloadStage::NearInsertionPoint:
            if (!input.off_hand_pose_valid) {
                state->stage = ReloadStage::FeedDeviceAvailable;
                event = ReloadEvent::FeedDeviceDropped;
                break;
            }
            if (!input.hand_in_insertion_zone) {
                if (input.grip_released_edge || !input.grip_held) {
                    state->stage = ReloadStage::FeedDeviceAvailable;
                    event = ReloadEvent::FeedDeviceDropped;
                } else {
                    state->stage = ReloadStage::HoldingFeedDevice;
                    event = ReloadEvent::InsertionAssistEnded;
                }
                break;
            }
            if (profile.commit_on_contact ||
                (profile.commit_on_grip_release &&
                 input.grip_released_edge)) {
                state->stage = ReloadStage::Committing;
                event = ReloadEvent::CommitRequested;
                request_commit = true;
            }
            break;

        case ReloadStage::Committing:
            if (input.native_commit_completed) {
                reset_state(state);
                event = ReloadEvent::CommitCompleted;
            } else {
                // Keep the bounded native-reload pulse alive until the retail
                // hook acknowledges the ammo transfer. A single render-frame
                // request can otherwise expire between usercmd samples and
                // leave the physical transaction stuck in Committing.
                request_commit = true;
            }
            break;
    }

    auto output = describe_output(profile, *state, event);
    output.request_native_action_open = request_action_open;
    output.request_native_commit = request_commit;
    return output;
}

}  // namespace wawvr::gameplay
