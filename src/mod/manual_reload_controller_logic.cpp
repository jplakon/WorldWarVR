// SPDX-License-Identifier: GPL-3.0-only
#include "manual_reload_controller_logic.hpp"
#include "reload_pouch_logic.hpp"

#include <algorithm>
#include <cmath>

namespace wawvr::mod {

bool begin_manual_stripper_clip_request(
    const std::uint64_t physical_request_serial,
    ManualStripperClipRequestGate* const gate) noexcept {
    if (gate == nullptr || physical_request_serial == 0 ||
        (gate->issued && physical_request_serial <= gate->physical_request_serial)) return false;
    *gate = {true, physical_request_serial};
    return true;
}

ManualStripperClipCommitPlan plan_manual_stripper_clip_commit(
    const ManualStripperClipCommitInput& input) noexcept {
    constexpr std::int32_t kReloadStart = 9;
    constexpr std::int32_t kReloadStartInterrupt = 10;

    if (input.physical_clip_capacity != kManualStripperClipCapacity ||
        input.clip_size != input.physical_clip_capacity ||
        input.bolt_action != 1 ||
        (input.segmented_reload != 0 && input.segmented_reload != 1) ||
        input.reserve <= 0 || input.loaded < 0 ||
        input.loaded >= input.clip_size || input.reload_ammo_add < 0 ||
        input.reload_ammo_add > input.clip_size ||
        input.reload_start_add < 0 ||
        input.reload_start_add > input.clip_size) {
        return {};
    }

    const std::int32_t rounds_to_transfer = std::min(
        {input.physical_clip_capacity, input.reserve,
         input.clip_size - input.loaded});
    if (rounds_to_transfer <= 0) {
        return {};
    }

    const bool reload_start_state =
        input.weapon_state == kReloadStart ||
        input.weapon_state == kReloadStartInterrupt;
    const std::int32_t native_add = reload_start_state
        ? input.reload_start_add
        : input.reload_ammo_add;
    // PM_ReloadClip transfers nothing in a reload-start state whose
    // iReloadStartAdd is zero. Outside that state, zero means full top-up.
    if (reload_start_state && native_add == 0) {
        return {};
    }

    const std::int32_t native_calls =
        native_add == 0 || native_add >= input.clip_size
        ? 1
        : (rounds_to_transfer + native_add - 1) / native_add;
    if (native_calls <= 0 ||
        native_calls > input.physical_clip_capacity) {
        return {};
    }
    return {
        .valid = true,
        .rounds_to_transfer = rounds_to_transfer,
        .native_reload_clip_calls = native_calls,
    };
}

namespace {

bool valid_clip_commit_key(const ManualStripperClipCommitKey& key) noexcept {
    return key.session_epoch != 0 && key.publication_generation != 0 &&
        (key.publication_generation & 1U) == 0 && key.request_serial != 0 &&
        key.weapon_index > 0 && key.definition_address != 0 &&
        key.ammo_index >= 0 && key.clip_index >= 0;
}

bool same_clip_policy(const ManualStripperClipCommitInput& a,
                      const ManualStripperClipCommitInput& b) noexcept {
    // weapon_state may differ between prediction and authoritative passes.
    return a.clip_size == b.clip_size &&
        a.reload_ammo_add == b.reload_ammo_add &&
        a.reload_start_add == b.reload_start_add &&
        a.bolt_action == b.bolt_action &&
        a.segmented_reload == b.segmented_reload &&
        a.physical_clip_capacity == b.physical_clip_capacity;
}

bool clip_fanout_frame_matches(const ManualStripperClipFanoutFrame& frame,
                              const ManualStripperClipFanoutState& state) noexcept {
    return state.initialized && !state.failed && frame.player_state != 0 &&
        frame.key == state.key &&
        frame.now_milliseconds >= state.started_at_milliseconds &&
        frame.now_milliseconds - state.started_at_milliseconds <
            kManualStripperClipFanoutMilliseconds &&
        same_clip_policy(frame.ammo, state.before);
}

}  // namespace

ManualStripperClipFanoutPlan plan_manual_stripper_clip_fanout(
    const ManualStripperClipFanoutFrame& frame,
    ManualStripperClipFanoutState* const state) noexcept {
    if (state == nullptr || !valid_clip_commit_key(frame.key) ||
        frame.player_state == 0 || frame.now_milliseconds == 0) {
        return {};
    }
    auto next = *state;
    if (!next.initialized) {
        const auto plan = plan_manual_stripper_clip_commit(frame.ammo);
        if (!plan.valid) return {};
        next.initialized = true;
        next.key = frame.key;
        next.started_at_milliseconds = frame.now_milliseconds;
        next.before = frame.ammo;
        next.expected_loaded = frame.ammo.loaded + plan.rounds_to_transfer;
        next.expected_reserve = frame.ammo.reserve - plan.rounds_to_transfer;
    }
    if (!clip_fanout_frame_matches(frame, next)) return {};

    const bool already_applied = next.verified &&
        frame.ammo.loaded == next.expected_loaded &&
        frame.ammo.reserve == next.expected_reserve;
    ManualStripperClipCommitPlan native{};
    if (!already_applied) {
        if (frame.ammo.loaded != next.before.loaded ||
            frame.ammo.reserve != next.before.reserve) return {};
        native = plan_manual_stripper_clip_commit(frame.ammo);
        if (!native.valid ||
            frame.ammo.loaded + native.rounds_to_transfer != next.expected_loaded ||
            frame.ammo.reserve - native.rounds_to_transfer != next.expected_reserve) {
            return {};
        }
    }
    if (std::find(next.player_states.begin(), next.player_states.end(),
                  frame.player_state) == next.player_states.end()) {
        const auto empty = std::find(next.player_states.begin(),
                                     next.player_states.end(), std::uintptr_t{});
        if (empty == next.player_states.end()) return {};
        *empty = frame.player_state;
    }
    *state = next;
    return {true, already_applied, native.native_reload_clip_calls,
            next.expected_loaded, next.expected_reserve};
}

bool verify_manual_stripper_clip_fanout(
    const ManualStripperClipFanoutFrame& frame,
    ManualStripperClipFanoutState* const state) noexcept {
    if (state == nullptr || !state->initialized || frame.key != state->key) {
        return false;
    }
    const bool verified = clip_fanout_frame_matches(frame, *state) &&
        std::find(state->player_states.begin(), state->player_states.end(),
                  frame.player_state) != state->player_states.end() &&
        frame.ammo.loaded == state->expected_loaded &&
        frame.ammo.reserve == state->expected_reserve;
    state->verified = state->verified || verified;
    state->failed = !verified;
    return verified;
}

bool manual_reload_right_waist_contains(
    const ManualReloadPoint& head_local) noexcept {
    return reload_pouch_contains({head_local.x, head_local.y, head_local.z}, false);
}

bool manual_reload_left_waist_contains(
    const ManualReloadPoint& head_local) noexcept {
    return reload_pouch_contains({head_local.x, head_local.y, head_local.z}, true);
}

bool calculate_manual_reload_top_feed_receiver(
    const ManualReloadReceiverGeometry& geometry,
    const ManualReloadPoint& clip_probe,
    const ManualReloadPoint& tracked_rifle_grip,
    const ManualReloadPoint& tag_flash,
    const ManualReloadPoint& weapon_up,
    ManualReloadPoint* const receiver,
    float* const segment_fraction) noexcept {
    if (receiver == nullptr || segment_fraction == nullptr ||
        !std::isfinite(clip_probe.x) || !std::isfinite(clip_probe.y) ||
        !std::isfinite(clip_probe.z) ||
        !std::isfinite(tracked_rifle_grip.x) ||
        !std::isfinite(tracked_rifle_grip.y) ||
        !std::isfinite(tracked_rifle_grip.z) ||
        !std::isfinite(tag_flash.x) || !std::isfinite(tag_flash.y) ||
        !std::isfinite(tag_flash.z) || !std::isfinite(weapon_up.x) ||
        !std::isfinite(weapon_up.y) || !std::isfinite(weapon_up.z) ||
        !std::isfinite(geometry.segment_minimum) ||
        !std::isfinite(geometry.segment_maximum) ||
        !std::isfinite(geometry.top_offset) ||
        geometry.segment_minimum < 0.0F ||
        geometry.segment_minimum >= geometry.segment_maximum ||
        geometry.segment_maximum > 1.0F) {
        return false;
    }

    const ManualReloadPoint segment{
        tag_flash.x - tracked_rifle_grip.x,
        tag_flash.y - tracked_rifle_grip.y,
        tag_flash.z - tracked_rifle_grip.z,
    };
    const double length_squared =
        static_cast<double>(segment.x) * segment.x +
        static_cast<double>(segment.y) * segment.y +
        static_cast<double>(segment.z) * segment.z;
    if (!std::isfinite(length_squared) || length_squared < 1.0) {
        return false;
    }
    const ManualReloadPoint from_grip{
        clip_probe.x - tracked_rifle_grip.x,
        clip_probe.y - tracked_rifle_grip.y,
        clip_probe.z - tracked_rifle_grip.z,
    };
    const double projected =
        static_cast<double>(from_grip.x) * segment.x +
        static_cast<double>(from_grip.y) * segment.y +
        static_cast<double>(from_grip.z) * segment.z;
    const float fraction = static_cast<float>(std::clamp(
        projected / length_squared,
        static_cast<double>(geometry.segment_minimum),
        static_cast<double>(geometry.segment_maximum)));
    *receiver = {
        tracked_rifle_grip.x + fraction * segment.x +
            weapon_up.x * geometry.top_offset,
        tracked_rifle_grip.y + fraction * segment.y +
            weapon_up.y * geometry.top_offset,
        tracked_rifle_grip.z + fraction * segment.z +
            weapon_up.z * geometry.top_offset,
    };
    *segment_fraction = fraction;
    return std::isfinite(receiver->x) && std::isfinite(receiver->y) &&
           std::isfinite(receiver->z);
}

bool calculate_manual_reload_top_feed_receiver(
    const ManualReloadPoint& clip_probe,
    const ManualReloadPoint& tracked_rifle_grip,
    const ManualReloadPoint& tag_flash,
    const ManualReloadPoint& weapon_up,
    ManualReloadPoint* const receiver,
    float* const segment_fraction) noexcept {
    return calculate_manual_reload_top_feed_receiver(
        {}, clip_probe, tracked_rifle_grip, tag_flash, weapon_up, receiver,
        segment_fraction);
}

StripperClipFeedHand select_stripper_clip_feed_hand(
    const ManualReloadControllerState& state,
    const bool right_rifle_gripped,
    const bool left_rifle_gripped) noexcept {
    if (state.stripper_clip_hand != StripperClipFeedHand::None) {
        return state.stripper_clip_hand;
    }
    if (right_rifle_gripped != left_rifle_gripped) {
        return right_rifle_gripped ? StripperClipFeedHand::Left
                                   : StripperClipFeedHand::Right;
    }
    return StripperClipFeedHand::None;
}

ManualReloadControllerUpdate update_manual_reload_controller(
    const ManualReloadControllerFrame& frame,
    ManualReloadControllerState* const state) noexcept {
    ManualReloadControllerUpdate update{};
    if (state == nullptr) {
        return update;
    }

    const bool sequence_valid =
        frame.input_owned && frame.action_sequence != 0;
    const bool stripper_clip = frame.profile_kind ==
        gameplay::ReloadProfileKind::InternalStripperClip;
    const bool feed_hand_matches = !stripper_clip ||
        (frame.feed_hand != StripperClipFeedHand::None &&
         (state->stripper_clip_hand == StripperClipFeedHand::None ||
          state->stripper_clip_hand == frame.feed_hand));
    const bool feed_grip_held = feed_hand_matches && frame.feed_grip_held;
    if (stripper_clip && state->sampled_feed_hand != frame.feed_hand) {
        // A deliberate rifle handoff is not a new clip squeeze. Rebaseline
        // only grip edges; the physical bolt-begin edge remains independent.
        state->feed_grip_was_held = feed_grip_held;
        state->sampled_feed_hand = frame.feed_hand;
    }
    bool begin_reload_pressed_edge = false;
    bool grip_pressed_edge = false;
    bool grip_released_edge = false;
    if (!sequence_valid) {
        state->input_owned = false;
        state->last_action_sequence = frame.action_sequence;
        state->feed_grip_was_held = feed_grip_held;
    } else if (!state->input_owned) {
        // Baseline actions held while gameplay/focus was unavailable. This
        // prevents focus restoration from manufacturing a reload or grab.
        state->input_owned = true;
        state->last_action_sequence = frame.action_sequence;
        state->feed_grip_was_held = feed_grip_held;
    } else if (frame.action_sequence != state->last_action_sequence) {
        begin_reload_pressed_edge = frame.begin_reload_pressed_edge;
        grip_pressed_edge =
            feed_grip_held && !state->feed_grip_was_held;
        grip_released_edge =
            !feed_grip_held && state->feed_grip_was_held;
        state->last_action_sequence = frame.action_sequence;
        state->feed_grip_was_held = feed_grip_held;
    }

    update.begin_reload_pressed_edge = begin_reload_pressed_edge;
    update.feed_grip_pressed_edge = grip_pressed_edge;
    update.feed_grip_released_edge = grip_released_edge;
    update.reload = gameplay::update_manual_reload(
        gameplay::make_reload_profile(frame.profile_kind),
        {
            .enabled = frame.profile_kind !=
                gameplay::ReloadProfileKind::NativeOnly,
            .focused = sequence_valid,
            .weapon_supported = frame.weapon_supported,
            .can_reload = frame.can_reload,
            .reset_requested = frame.reset_requested,
            .reload_pressed_edge = begin_reload_pressed_edge,
            .native_action_open = frame.native_action_open,
            .native_commit_completed = frame.native_commit_completed,
            .off_hand_pose_valid = feed_hand_matches && frame.feed_pose_valid,
            .grip_pressed_edge = grip_pressed_edge,
            .grip_held = sequence_valid && feed_grip_held,
            .grip_released_edge = grip_released_edge,
            .hand_in_feed_device_zone =
                frame.hand_in_feed_device_zone,
            .hand_in_insertion_zone = frame.hand_in_insertion_zone,
        },
        &state->reload);
    if (!stripper_clip || !frame.weapon_supported || frame.reset_requested) {
        state->stripper_clip_grip_reserved_until_release = false;
        state->stripper_clip_hand = StripperClipFeedHand::None;
    } else if (update.reload.event == gameplay::ReloadEvent::FeedDeviceGrabbed) {
        state->stripper_clip_grip_reserved_until_release = true;
        state->stripper_clip_hand = frame.feed_hand;
    }
    // Do not infer release from an inactive action, focus/tracking loss, an
    // insertion-pose failure, or native commit completion. A held clip squeeze
    // must not become a fresh rifle grip when its ammo transfer completes.
    if (sequence_valid && feed_hand_matches && frame.feed_grip_release_observed) {
        state->stripper_clip_grip_reserved_until_release = false;
    }
    if (!state->stripper_clip_grip_reserved_until_release &&
        !update.reload.render_detached_feed_device &&
        update.reload.stage != gameplay::ReloadStage::Committing) {
        state->stripper_clip_hand = StripperClipFeedHand::None;
    }
    update.reserves_feed_grip = stripper_clip &&
        (feed_hand_matches || state->stripper_clip_hand != StripperClipFeedHand::None) &&
        (update.reload.manual_reload_active ||
         state->stripper_clip_grip_reserved_until_release);
    return update;
}

void reset_manual_reload_controller(
    ManualReloadControllerState* const state,
    const bool preserve_feed_grip_reservation) noexcept {
    if (state != nullptr) {
        const bool pending_release = preserve_feed_grip_reservation &&
            state->stripper_clip_grip_reserved_until_release;
        const auto clip_hand = state->stripper_clip_hand;
        *state = {};
        state->stripper_clip_grip_reserved_until_release = pending_release;
        if (pending_release) {
            state->stripper_clip_hand = clip_hand;
        }
    }
}

}  // namespace wawvr::mod
