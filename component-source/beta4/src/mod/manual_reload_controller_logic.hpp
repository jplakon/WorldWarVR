// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "gameplay/manual_reload_logic.hpp"

#include <cstdint>

namespace wawvr::mod {

struct ManualReloadControllerFrame final {
    bool input_owned{};
    std::uint64_t action_sequence{};
    gameplay::ReloadProfileKind profile_kind{
        gameplay::ReloadProfileKind::NativeOnly};
    bool weapon_supported{};
    bool can_reload{};
    bool reset_requested{};
    bool native_action_open{};
    bool native_commit_completed{};
    bool begin_reload_pressed_edge{};
    bool feed_grip_held{};
    bool feed_pose_valid{};
    bool hand_in_feed_device_zone{};
    bool hand_in_insertion_zone{};
};

struct ManualReloadControllerState final {
    bool input_owned{};
    std::uint64_t last_action_sequence{};
    bool feed_grip_was_held{};
    gameplay::ReloadState reload{};
};

struct ManualReloadControllerUpdate final {
    gameplay::ReloadOutput reload{};
    bool begin_reload_pressed_edge{};
    bool feed_grip_pressed_edge{};
    bool feed_grip_released_edge{};
};

// Every currently accepted physical stripper-clip rifle has a five-round
// internal magazine. Keep this gameplay capacity independent from authored
// surface counts: those describe render geometry, not ammunition.
inline constexpr std::int32_t kManualStripperClipCapacity = 5;

struct ManualStripperClipCommitInput final {
    std::int32_t weapon_state{};
    std::int32_t clip_size{};
    std::int32_t reserve{};
    std::int32_t loaded{};
    std::int32_t reload_ammo_add{};
    std::int32_t reload_start_add{};
    std::int32_t bolt_action{};
    std::int32_t segmented_reload{};
    std::int32_t physical_clip_capacity{kManualStripperClipCapacity};
};

struct ManualStripperClipCommitPlan final {
    bool valid{};
    std::int32_t rounds_to_transfer{};
    std::int32_t native_reload_clip_calls{};
};

// PM_ReloadClip tops up ordinary rifles in one call when iReloadAmmoAdd is
// zero, but retail scoped/segmented rifles cap each call to one round. Plan a
// bounded number of native calls so one physical five-round clip has the same
// ammunition result for either WeaponDef policy.
[[nodiscard]] ManualStripperClipCommitPlan
plan_manual_stripper_clip_commit(
    const ManualStripperClipCommitInput& input) noexcept;

struct ManualReloadPoint final {
    float x{};
    float y{};
    float z{};
};

inline constexpr float kManualReloadReceiverSegmentMinimum = 0.05F;
inline constexpr float kManualReloadReceiverSegmentMaximum = 0.75F;
inline constexpr float kManualReloadReceiverTopOffset = 3.0F;

struct ManualReloadReceiverGeometry final {
    float segment_minimum{kManualReloadReceiverSegmentMinimum};
    float segment_maximum{kManualReloadReceiverSegmentMaximum};
    float top_offset{kManualReloadReceiverTopOffset};
};

// Head-local IW coordinates use +Y to the player's left, so the right-waist
// inventory zone is the mirrored negative-Y counterpart of the old left belt.
[[nodiscard]] bool manual_reload_right_waist_contains(
    const ManualReloadPoint& head_local) noexcept;

// Detachable magazines use the off-hand hip. This is the positive-Y mirror
// of the accepted right-waist stripper-clip volume.
[[nodiscard]] bool manual_reload_left_waist_contains(
    const ManualReloadPoint& head_local) noexcept;

// Projects the current tracked clip position onto the usable top section of
// the rifle. This makes the insertion gate follow the actual hand instead of
// assuming one fixed point along weapons of different lengths.
[[nodiscard]] bool calculate_manual_reload_top_feed_receiver(
    const ManualReloadReceiverGeometry& geometry,
    const ManualReloadPoint& clip_probe,
    const ManualReloadPoint& tracked_rifle_grip,
    const ManualReloadPoint& tag_flash,
    const ManualReloadPoint& weapon_up,
    ManualReloadPoint* receiver,
    float* segment_fraction) noexcept;

// Compatibility wrapper for the accepted Kar98 receiver geometry.
[[nodiscard]] bool calculate_manual_reload_top_feed_receiver(
    const ManualReloadPoint& clip_probe,
    const ManualReloadPoint& tracked_rifle_grip,
    const ManualReloadPoint& tag_flash,
    const ManualReloadPoint& weapon_up,
    ManualReloadPoint* receiver,
    float* segment_fraction) noexcept;

// Binds a physical action-open gesture and feed-hand squeeze to the portable
// reload transaction. Grip edges advance once per OpenXR action sequence,
// while bolt/zone/native facts may advance on later render callbacks.
[[nodiscard]] ManualReloadControllerUpdate update_manual_reload_controller(
    const ManualReloadControllerFrame& frame,
    ManualReloadControllerState* state) noexcept;

void reset_manual_reload_controller(
    ManualReloadControllerState* state) noexcept;

}  // namespace wawvr::mod
