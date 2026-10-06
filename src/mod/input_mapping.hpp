// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "controller_state.hpp"

#include "t4/usercmd.hpp"
#include "xr_types.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace wawvr::mod {

inline constexpr std::uint64_t kMaximumControllerFrameAgeMilliseconds = 250;
inline constexpr float kControllerStickDeadzone = 0.20F;
inline constexpr float kControllerButtonThreshold = 0.55F;
inline constexpr float kSnapTurnEngageThreshold = 0.75F;
inline constexpr float kSnapTurnReleaseThreshold = 0.35F;
inline constexpr float kSnapTurnVerticalDominanceMargin = 0.15F;
inline constexpr float kSnapTurnDegrees = 45.0F;
inline constexpr float kSmoothTurnDeadzone = 0.25F;
inline constexpr float kSmoothTurnDegreesPerSecond = 120.0F;
inline constexpr std::uint64_t kSmoothTurnMaximumElapsedMilliseconds = 50;
inline constexpr float kBodyYawSyncMinimumDegrees = 0.25F;
inline constexpr std::uint32_t kSnapTurnBlockedKeyCatcherMask = 0x08U | 0x10U;
inline constexpr std::int32_t kT4SpActiveConnectionState = 10;
inline constexpr std::int32_t kT4MpActiveConnectionState = 10;
// Kept as a source-compatible alias for the established SP tests/callers.
inline constexpr std::int32_t kT4ActiveConnectionState =
    kT4SpActiveConnectionState;

struct SnapTurnState final {
    bool armed{true};
};

inline constexpr wchar_t kTurnModeEnvironmentVariable[] =
    L"WAWVR_TURN_MODE";

enum class TurnMode : std::uint8_t {
    Snap,
    Smooth,
};

struct SmoothTurnState final {
    bool input_was_owned{};
    std::uint64_t previous_update_milliseconds{};
    std::uint64_t last_action_sequence{};
};

inline constexpr float kDirectionalActionEngageThreshold = 0.80F;
inline constexpr float kDirectionalActionReleaseThreshold = 0.35F;
inline constexpr float kDirectionalActionDominanceMargin = 0.15F;

struct DirectionalActionState final {
    bool input_was_owned{};
    bool armed{};
    std::uint64_t last_action_sequence{};
    enum class Stance : std::uint8_t {
        standing,
        crouched,
        prone,
    } stance{Stance::standing};
};

enum class DirectionalAction : std::uint8_t {
    none,
    crouch,
    prone,
    stand,
    jump,
};

struct DirectionalActionUpdate final {
    DirectionalAction action{DirectionalAction::none};
};

struct SprintLatchState final {
    bool input_was_owned{};
    bool click_was_held{};
    bool latched{};
    std::uint64_t last_action_sequence{};
};

inline constexpr std::uint64_t kPhysicalMeleeMinimumSampleMilliseconds = 8;
inline constexpr std::uint64_t kPhysicalMeleeMaximumSampleMilliseconds = 150;
inline constexpr std::uint64_t kPhysicalMeleeCooldownMilliseconds = 450;
inline constexpr float kPhysicalMeleeMinimumTravelMeters = 0.04F;
inline constexpr float kPhysicalMeleeMinimumSpeedMetersPerSecond = 0.90F;
inline constexpr float kPhysicalMeleeTwoHandMinimumSpeedMetersPerSecond =
    0.65F;
inline constexpr float kPhysicalMeleeMinimumForwardFraction = 0.55F;
inline constexpr float kPhysicalMeleeMinimumPerHandThrustMeters = 0.02F;
inline constexpr float kPhysicalMeleeMinimumHandTravelRatio = 0.55F;
inline constexpr float kPhysicalMeleeMinimumHandDirectionCorrelation = 0.80F;
inline constexpr float kPhysicalMeleeMinimumWorldTravelMeters = 0.03F;
inline constexpr float kPhysicalMeleeMinimumWorldDirectionCorrelation = 0.50F;
inline constexpr float kPhysicalMeleeMaximumWorldTravelMeters = 0.30F;

enum class PhysicalMeleeGesture : std::uint8_t {
    none,
    right_hand_swing,
    two_hand_thrust,
    right_hand_pistol_strike,
};

struct HeldPistolMeleeSample final {
    wawvr::xr::Vec3f hand_relative_to_head{};
    wawvr::xr::Vec3f hand_position{};
    std::uint64_t milliseconds{};
};

struct PhysicalMeleeState final {
    bool input_was_owned{};
    bool pose_was_valid{};
    bool right_grip_latched{};
    bool left_grip_latched{};
    bool two_hand_weapon_was_held{};
    std::uint64_t held_pistol_identity{};
    wawvr::xr::Vec3f gesture_origin_hand_relative_to_head{};
    wawvr::xr::Vec3f gesture_origin_support_hand_relative_to_head{};
    wawvr::xr::Vec3f gesture_origin_hand_position{};
    wawvr::xr::Vec3f gesture_origin_support_hand_position{};
    std::uint64_t gesture_origin_milliseconds{};
    std::uint64_t last_action_sequence{};
    std::uint64_t cooldown_until_milliseconds{};
    float last_trigger_relative_travel_meters{};
    float last_trigger_world_travel_meters{};
    float last_trigger_speed_meters_per_second{};
    float last_trigger_outward_travel_meters{};
    // At most one sample per integer millisecond. Keeping the whole bounded
    // window avoids both idle-time speed dilution and a gesture being split
    // by an arbitrary 150 ms origin rollover. No allocation in the input hook.
    std::array<HeldPistolMeleeSample,
               kPhysicalMeleeMaximumSampleMilliseconds + 1>
        held_pistol_samples{};
    std::uint32_t held_pistol_sample_count{};
    std::uint32_t held_pistol_next_sample{};
};

// A normal thumbstick click is much shorter than a comfortable sustained
// press in VR. Latch sprint on a fresh L3 edge and keep submitting the native
// sprint bit until the locomotion stick returns to neutral. Ownership loss,
// stale/inactive actions, and UI entry clear the latch immediately.
[[nodiscard]] bool update_sprint_latch(
    bool input_owned,
    std::uint64_t action_sequence,
    const wawvr::xr::Vec2ActionState& movement_stick,
    const wawvr::xr::BoolActionState& stick_click,
    SprintLatchState* state) noexcept;

void reset_sprint_latch(SprintLatchState* state) noexcept;

// Detects one deliberate fast right-hand swing relative to the HMD, or a
// rigid forward rifle thrust while both weapon grips are held. Motion is
// accumulated over a bounded 150 ms window rather than requiring the full
// gesture inside one headset frame. Sampling is advanced only once per
// OpenXR action sequence, so the several T4 usercmds that can consume one
// predicted frame cannot synthesize duplicates. Trigger holds and one-handed
// grip transitions suppress the gesture; a two-hand thrust additionally
// requires both hands to travel along the rifle axis. A gesture must contain
// real controller travel in tracking space as well as hand-to-head travel;
// leaning the HMD away from otherwise stationary hands cannot synthesize an
// attack. Implausibly large tracking-space jumps rebaseline the detector.
// An exact pistol identity may also allow a deliberate right-hand strike while
// that hand keeps its grip. Entry/exit establishes a fresh motion baseline;
// its bounded sliding history makes detection independent of idle-window phase.
// The caller must withhold this permission during reload interactions.
// Right-stick click remains an independent fallback.
[[nodiscard]] PhysicalMeleeGesture update_physical_melee_gesture(
    bool input_owned,
    const ControllerFrameSnapshot& snapshot,
    std::uint64_t now_milliseconds,
    PhysicalMeleeState* state,
    bool supported_weapon_owned = false,
    std::uint64_t single_hand_pistol_identity = 0) noexcept;

void reset_physical_melee_gesture(PhysicalMeleeState* state) noexcept;

struct ControllerInputResult final {
    bool frame_accepted{};
    bool movement_applied{};
    bool gameplay_buttons_applied{};
    bool melee_comfort_applied{};
    bool melee_button_held{};
    bool weapon_aim_applied{};
    bool weapon_trigger_applied{};
    float weapon_pitch_degrees{};
    float weapon_yaw_degrees{};
};

using MountedGunRouteProbe = bool (*)(
    const ControllerFrameSnapshot&, std::uint64_t) noexcept;

// The mounted runtime binds this only after both profiled call hooks commit.
// Tests and unsupported profiles retain a null, native-fallback probe.
void bind_mounted_gun_route_probe(MountedGunRouteProbe probe) noexcept;

[[nodiscard]] bool controller_frame_is_current(
    const ControllerFrameSnapshot& snapshot,
    std::uint64_t now_milliseconds) noexcept;

// Converts H_anchor^-1 * C_current into IW local space, composes it through the
// stock/body camera basis, then derives T4 pitch/yaw degrees. The exact stereo
// thunk restores this global refdef after its temporary eye draws.
[[nodiscard]] bool controller_aim_degrees(
    const ControllerFrameSnapshot& snapshot,
    const wawvr::xr::Basis3f& camera_axis,
    float* pitch_degrees,
    float* yaw_degrees) noexcept;

// Returns the HMD's horizontal yaw relative to the frozen tracking anchor in
// T4's positive-left convention. The input hook transfers this angle into the
// native body yaw while rebasing the anchor by the same amount, keeping the
// visible HMD/controller pose stationary as the hidden player body catches up.
[[nodiscard]] bool controller_body_yaw_delta_degrees(
    const ControllerFrameSnapshot& snapshot,
    float* yaw_degrees) noexcept;

// COD4 updates its hidden body heading only while sprint is latched and the
// movement stick remains active. Grip/support transitions are deliberately not
// part of this policy: releasing a support hand must never discharge accumulated
// HMD yaw into the rifle's tracking anchor before the next regrip.
[[nodiscard]] bool automatic_body_yaw_sync_allowed(
    bool gameplay_controller_allowed,
    bool controller_frame_current,
    bool sprint_movement_active) noexcept;

// Horizontal-only snap latch. Positive IW yaw turns left, so
// right-stick-right produces -45 degrees. The caller owns the exact T4
// view-angle write and gameplay/UI gating.
[[nodiscard]] float consume_snap_turn_degrees(
    const wawvr::xr::Vec2ActionState& right_stick,
    SnapTurnState* state) noexcept;

void reset_snap_turn(SnapTurnState* state) noexcept;

// Snap remains the fail-closed default. The launcher opts into continuous
// turning only with the exact child-process value `smooth`.
[[nodiscard]] TurnMode turn_mode_from_setting(
    std::wstring_view value) noexcept;

// COD4-parity analog turning at a fixed 120 degrees/second. The usable stick
// range is remapped from zero immediately outside the deadzone to full speed
// at full tilt, and a 50 ms elapsed-time cap prevents a post-hitch jump.
// Horizontal dominance leaves deliberate vertical stance gestures untouched;
// an XR action sequence is consumed at most once even if T4 builds several
// native commands from the same published controller sample.
[[nodiscard]] float consume_smooth_turn_degrees(
    bool input_owned,
    std::uint64_t action_sequence,
    std::uint64_t now_milliseconds,
    const wawvr::xr::Vec2ActionState& right_stick,
    SmoothTurnState* state) noexcept;

void reset_smooth_turn(SmoothTurnState* state) noexcept;

// Deliberate right-stick up/down gestures traverse one explicit stance rung:
// down is standing -> crouched -> prone, while up is prone -> crouched ->
// standing and becomes jump only when already standing. Horizontal deflection
// remains owned by the selected turn mode. A neutral sample is required before each new gesture,
// so holding the stick cannot repeat an action across OpenXR/native frames.
[[nodiscard]] DirectionalActionUpdate update_directional_actions(
    bool input_owned,
    std::uint64_t action_sequence,
    const wawvr::xr::Vec2ActionState& right_stick,
    DirectionalActionState* state) noexcept;

void reset_directional_actions(DirectionalActionState* state) noexcept;

// Exact T4 high-level commands used by the post-Com_Frame queue. Stand and
// jump deliberately share the native +gostand press/release transaction; T4
// raises a lowered player and jumps only when already standing.
[[nodiscard]] std::string_view directional_action_console_command(
    DirectionalAction action) noexcept;

// Both exact supported SP and MP images report connection state 10 during
// active gameplay. Catchers 0x08 and 0x10 both own horizontal input, so neither
// may leak gameplay turning.
[[nodiscard]] bool snap_turn_gameplay_allowed(
    std::uint32_t key_catchers,
    std::int32_t connection_state,
    std::int32_t active_connection_state =
        kT4SpActiveConnectionState) noexcept;

// Usercmd controller movement, weapon input, and A/B gameplay bindings are
// owned only by exact active gameplay with no native key catcher. This prevents
// the same controller actions from leaking through UI, console, or another
// engine input owner.
[[nodiscard]] bool controller_gameplay_input_allowed(
    std::uint32_t key_catchers,
    std::int32_t connection_state,
    std::int32_t active_connection_state =
        kT4SpActiveConnectionState) noexcept;

// T4 treats a nonzero completed-command melee charge distance as permission
// to rotate/lunge toward a target. Zero is the engine's native no-charge
// sentinel, so the VR post-build hook clears these dedicated fields even if
// controller tracking is temporarily stale. The ordinary melee button stays
// untouched and still drives the knife damage/animation path.
void suppress_t4_melee_charge(wawvr::t4::UsercmdSp& command) noexcept;

// Applies one consumed snap to both T4's live float yaw and the already-built
// command (the hook runs after native serialization), and rotates the sampled
// stock camera for same-command controller gun alignment.
[[nodiscard]] bool apply_snap_turn_to_t4_command(
    wawvr::t4::UsercmdSp& command,
    float snap_degrees,
    float* client_yaw_degrees,
    wawvr::xr::Basis3f* sampled_camera_axis) noexcept;

[[nodiscard]] bool apply_snap_turn_to_t4_command(
    wawvr::t4::UsercmdMp& command,
    float snap_degrees,
    float* client_yaw_degrees,
    wawvr::xr::Basis3f* sampled_camera_axis) noexcept;

// Applies only independently verified T4 usercmd fields. Native keyboard and
// mouse values are preserved: buttons are ORed and movement is saturating-add.
[[nodiscard]] ControllerInputResult apply_controller_input(
    wawvr::t4::UsercmdSp& command,
    const ControllerFrameSnapshot& snapshot,
    const wawvr::xr::Basis3f& camera_axis,
    std::uint64_t now_milliseconds) noexcept;

[[nodiscard]] ControllerInputResult apply_controller_input(
    wawvr::t4::UsercmdMp& command,
    const ControllerFrameSnapshot& snapshot,
    const wawvr::xr::Basis3f& camera_axis,
    std::uint64_t now_milliseconds) noexcept;

}  // namespace wawvr::mod
