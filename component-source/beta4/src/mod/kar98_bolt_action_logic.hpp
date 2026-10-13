// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "bolt_action_weapon_profile.hpp"

#include <cstdint>

namespace wawvr::mod {

// Compatibility aliases keep every headset-accepted Kar98 calibration exact
// while the implementation consumes a weapon profile.
inline constexpr float kKar98BoltTravelUnits =
    kKar98BoltActionWeaponProfile.bolt_travel_units;
inline constexpr float kKar98BoltOpenThreshold =
    kKar98BoltActionWeaponProfile.bolt_open_threshold;
inline constexpr float kKar98BoltClosedThreshold =
    kKar98BoltActionWeaponProfile.bolt_closed_threshold;
inline constexpr float kKar98BoltTriggerEngage =
    kKar98BoltActionWeaponProfile.trigger_engage;
inline constexpr float kKar98BoltTriggerRelease =
    kKar98BoltActionWeaponProfile.trigger_release;
inline constexpr std::int32_t kKar98WeaponAnimIdle =
    kKar98BoltActionWeaponProfile.idle_anim;
inline constexpr std::int32_t kKar98WeaponAnimRechamberHip =
    kKar98BoltActionWeaponProfile.rechamber_hip_anim;
inline constexpr std::int32_t kKar98WeaponAnimRechamberAds =
    kKar98BoltActionWeaponProfile.rechamber_ads_anim;

enum class Kar98BoltEvent : std::uint8_t {
    None,
    ShotLocked,
    Grabbed,
    Released,
    FullyOpened,
    FullyClosed,
    ControlsRearmed,
    Reset,
};

struct Kar98BoltActionFrame final {
    bool enabled{};
    bool focused{};
    bool weapon_supported{};
    bool reset_requested{};
    std::uint64_t action_sequence{};

    bool shot_fired{};
    bool left_rifle_gripped{};
    bool right_hand_pose_valid{};
    bool right_hand_near_bolt{};
    bool right_grip_held{};
    bool right_trigger_active{};
    float right_trigger_value{};

    // Projection of (right hand - closed bolt anchor) onto rifle forward.
    // Pulling the hand rearward decreases this value.
    float right_hand_forward_coordinate{};

    // Symmetric free-left-hand path. The right-hand fields above remain the
    // accepted compatibility path; exactly one hand is latched when its
    // trigger edge occurs near the bolt while the opposite hand owns the
    // rifle.
    bool right_rifle_gripped{};
    bool left_hand_pose_valid{};
    bool left_hand_near_bolt{};
    bool left_grip_held{};
    bool left_trigger_active{};
    float left_trigger_value{};
    float left_hand_forward_coordinate{};
};

struct Kar98BoltActionState final {
    bool input_owned{};
    std::uint64_t last_action_sequence{};
    // Kept as the right-trigger baseline for compatibility with the accepted
    // Kar98 tests and serialized diagnostics.
    bool trigger_was_held{};
    bool left_trigger_was_held{};

    bool manipulating_hand_selected{};
    bool manipulating_left_hand{};

    float bolt_fraction{};
    bool bolt_grabbed{};
    bool grab_started_closed{};
    float grab_start_coordinate{};
    float grab_start_fraction{};
    bool fully_opened{};
    bool cycle_required{};
    bool awaiting_controls_release{};
};

struct Kar98BoltActionUpdate final {
    Kar98BoltEvent event{Kar98BoltEvent::None};
    float bolt_fraction{};
    bool trigger_pressed_edge{};
    bool trigger_released_edge{};
    bool bolt_grabbed{};
    bool begin_reload_gesture{};
    bool action_open{};
    bool cycle_required{};
    bool block_attack{};
    bool reserve_right_grip{};
    bool reserve_left_grip{};
    bool manipulating_left_hand{};
};

struct Kar98BoltVector final {
    float x{};
    float y{};
    float z{};
};

struct Kar98BoltVisualDecision final {
    bool capture_closed_pose{};
    bool apply_manual_pose{};
};

// Profile-driven policy. The Kar98-named entry points below remain exact
// wrappers so the accepted rifle can be regression-compared during rollout.
[[nodiscard]] std::int32_t select_bolt_action_rechamber_visual_anim(
    const BoltActionWeaponProfile& profile,
    bool enabled,
    bool cycle_required,
    bool weapon_supported,
    std::int32_t native_anim) noexcept;

[[nodiscard]] Kar98BoltVisualDecision decide_bolt_action_bolt_visual(
    const BoltActionWeaponProfile& profile,
    bool closed_pose_latched,
    float bolt_fraction,
    bool interaction_active) noexcept;

[[nodiscard]] bool calculate_bolt_action_bolt_parent_travel(
    const BoltActionWeaponProfile& profile,
    const float root_quaternion[4],
    float bolt_fraction,
    Kar98BoltVector* travel) noexcept;

// Shared skeleton-space translation for any rigid, root-local linear action.
// Bolt profiles and spring-return magazine handles both move rearward on the
// weapon's authored local X axis.
[[nodiscard]] bool calculate_linear_action_parent_travel(
    float travel_units,
    const float root_quaternion[4],
    float action_fraction,
    Kar98BoltVector* travel) noexcept;

[[nodiscard]] Kar98BoltActionUpdate update_bolt_action(
    const BoltActionWeaponProfile& profile,
    const Kar98BoltActionFrame& frame,
    Kar98BoltActionState* state) noexcept;

// T4 starts its canned hip/ADS rechamber animation after every bolt-action
// shot. Once the physical cycle owns the Kar98, substitute idle only for those
// two visual starts; all weapon timing, state, sound, and unrelated animations
// remain native.
[[nodiscard]] std::int32_t select_kar98_rechamber_visual_anim(
    bool enabled,
    bool cycle_required,
    bool weapon_supported,
    std::int32_t native_anim) noexcept;

// A clean closed pose is learned once for the active Kar98 model and then
// remains authoritative. This prevents T4's native fire/rechamber animation
// from leaking through, or replacing the baseline, between manual cycles.
[[nodiscard]] Kar98BoltVisualDecision decide_kar98_bolt_visual(
    bool closed_pose_latched,
    float bolt_fraction,
    bool interaction_active) noexcept;

// Rotates the root-local rearward bolt travel into the DObj parent space. This
// prevents a tracked/world weapon axis from being written into skeleton-space
// translations when the rifle is turned or rolled.
[[nodiscard]] bool calculate_kar98_bolt_parent_travel(
    const float root_quaternion[4],
    float bolt_fraction,
    Kar98BoltVector* travel) noexcept;

// Pure physical bolt policy. The runtime supplies a hand coordinate relative
// to the moving rifle, so moving the rifle itself cannot masquerade as bolt
// travel. A shot is not rearmed until the bolt reaches both endpoints and the
// trigger/grip have subsequently been released.
[[nodiscard]] Kar98BoltActionUpdate update_kar98_bolt_action(
    const Kar98BoltActionFrame& frame,
    Kar98BoltActionState* state) noexcept;

void reset_kar98_bolt_action(Kar98BoltActionState* state) noexcept;

}  // namespace wawvr::mod
