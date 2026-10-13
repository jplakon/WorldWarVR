// SPDX-License-Identifier: GPL-3.0-only
#include "manual_reload_controller_logic.hpp"

#include <algorithm>
#include <cmath>

namespace wawvr::mod {

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

bool manual_reload_right_waist_contains(
    const ManualReloadPoint& head_local) noexcept {
    return std::isfinite(head_local.x) && std::isfinite(head_local.y) &&
           std::isfinite(head_local.z) &&
           head_local.x >= -18.0F && head_local.x <= 24.0F &&
           head_local.y >= -24.0F && head_local.y <= -2.0F &&
           head_local.z >= -42.0F && head_local.z <= -14.0F;
}

bool manual_reload_left_waist_contains(
    const ManualReloadPoint& head_local) noexcept {
    return std::isfinite(head_local.x) && std::isfinite(head_local.y) &&
           std::isfinite(head_local.z) &&
           head_local.x >= -18.0F && head_local.x <= 24.0F &&
           head_local.y >= 2.0F && head_local.y <= 24.0F &&
           head_local.z >= -42.0F && head_local.z <= -14.0F;
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

ManualReloadControllerUpdate update_manual_reload_controller(
    const ManualReloadControllerFrame& frame,
    ManualReloadControllerState* const state) noexcept {
    ManualReloadControllerUpdate update{};
    if (state == nullptr) {
        return update;
    }

    const bool sequence_valid =
        frame.input_owned && frame.action_sequence != 0;
    bool begin_reload_pressed_edge = false;
    bool grip_pressed_edge = false;
    bool grip_released_edge = false;
    if (!sequence_valid) {
        state->input_owned = false;
        state->last_action_sequence = frame.action_sequence;
        state->feed_grip_was_held = frame.feed_grip_held;
    } else if (!state->input_owned) {
        // Baseline actions held while gameplay/focus was unavailable. This
        // prevents focus restoration from manufacturing a reload or grab.
        state->input_owned = true;
        state->last_action_sequence = frame.action_sequence;
        state->feed_grip_was_held = frame.feed_grip_held;
    } else if (frame.action_sequence != state->last_action_sequence) {
        begin_reload_pressed_edge = frame.begin_reload_pressed_edge;
        grip_pressed_edge =
            frame.feed_grip_held && !state->feed_grip_was_held;
        grip_released_edge =
            !frame.feed_grip_held && state->feed_grip_was_held;
        state->last_action_sequence = frame.action_sequence;
        state->feed_grip_was_held = frame.feed_grip_held;
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
            .off_hand_pose_valid = frame.feed_pose_valid,
            .grip_pressed_edge = grip_pressed_edge,
            .grip_held = sequence_valid && frame.feed_grip_held,
            .grip_released_edge = grip_released_edge,
            .hand_in_feed_device_zone =
                frame.hand_in_feed_device_zone,
            .hand_in_insertion_zone = frame.hand_in_insertion_zone,
        },
        &state->reload);
    return update;
}

void reset_manual_reload_controller(
    ManualReloadControllerState* const state) noexcept {
    if (state != nullptr) {
        *state = {};
    }
}

}  // namespace wawvr::mod
