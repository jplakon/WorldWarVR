// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>

namespace wawvr::gameplay {

enum class ReloadProfileKind : std::uint8_t {
    NativeOnly,
    DetachableMagazine,
    InternalStripperClip,
    SingleRoundOrTube,
};

enum class ReloadStage : std::uint8_t {
    Ready,
    ActionOpen,
    FeedDeviceAvailable,
    HoldingFeedDevice,
    NearInsertionPoint,
    Committing,
};

enum class ReloadEvent : std::uint8_t {
    None,
    ReloadArmed,
    FeedDeviceAvailable,
    FeedDeviceGrabbed,
    FeedDeviceDropped,
    InsertionAssistStarted,
    InsertionAssistEnded,
    CommitRequested,
    CommitCompleted,
    Cancelled,
};

// Held feed devices never derive their position from a weapon, a cached pose,
// or a mirrored transform. The integration layer must use the current tracked
// off-hand pose every frame while this source is selected.
enum class ReloadPositionSource : std::uint8_t {
    None,
    CurrentTrackedOffHand,
};

enum class ReloadOrientationSource : std::uint8_t {
    None,
    CurrentTrackedOffHand,
    InsertionGuide,
};

struct ReloadProfile final {
    ReloadProfileKind kind{ReloadProfileKind::NativeOnly};
    bool manual_interaction{};
    bool requires_action_open{};
    bool request_action_open_on_begin{};
    bool orientation_assist{};
    bool commit_on_contact{};
    bool commit_on_grip_release{};
    bool suppress_native_automatic_commit{};
    bool hide_authored_device_while_held{};
};

struct ReloadInput final {
    bool enabled{};
    bool focused{};
    bool weapon_supported{};
    bool can_reload{};
    bool reset_requested{};

    bool reload_pressed_edge{};
    bool native_action_open{};
    bool native_commit_completed{};

    bool off_hand_pose_valid{};
    bool grip_pressed_edge{};
    bool grip_held{};
    bool grip_released_edge{};
    bool hand_in_feed_device_zone{};
    bool hand_in_insertion_zone{};
};

struct ReloadState final {
    ReloadStage stage{ReloadStage::Ready};
    ReloadProfileKind active_profile{ReloadProfileKind::NativeOnly};
    std::uint64_t generation{};
};

struct ReloadOutput final {
    ReloadStage stage{ReloadStage::Ready};
    ReloadEvent event{ReloadEvent::None};

    bool manual_reload_active{};
    bool suppress_native_automatic_commit{};
    bool request_native_action_open{};
    bool request_native_commit{};
    bool request_native_cancel{};

    bool render_detached_feed_device{};
    bool hide_authored_feed_device{};
    ReloadPositionSource position_source{ReloadPositionSource::None};
    ReloadOrientationSource orientation_source{
        ReloadOrientationSource::None};
};

[[nodiscard]] ReloadProfile make_reload_profile(
    ReloadProfileKind kind) noexcept;

// Advances at most one externally meaningful stage per call. Retail memory,
// animation, model/tag lookup, OpenXR sampling, and ammo mutation deliberately
// remain outside this portable module.
[[nodiscard]] ReloadOutput update_manual_reload(
    const ReloadProfile& profile,
    const ReloadInput& input,
    ReloadState* state) noexcept;

}  // namespace wawvr::gameplay
