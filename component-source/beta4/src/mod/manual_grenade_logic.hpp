// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace wawvr::mod {

struct ManualGrenadePoint final {
    float x{};
    float y{};
    float z{};
};

// Head-local IW coordinates use +Y to the player's left. These bounds retain
// the accepted COD4 belt volume and mirror it for the tactical slot.
inline constexpr float kManualGrenadeBeltForwardMinimum = -18.0F;
inline constexpr float kManualGrenadeBeltForwardMaximum = 18.0F;
inline constexpr float kManualGrenadeBeltSideMinimum = 2.0F;
inline constexpr float kManualGrenadeBeltSideMaximum = 24.0F;
inline constexpr float kManualGrenadeBeltUpMinimum = -42.0F;
inline constexpr float kManualGrenadeBeltUpMaximum = -14.0F;

inline constexpr float kManualGrenadeTriggerEngage = 0.70F;
inline constexpr float kManualGrenadeTriggerRelease = 0.35F;

inline constexpr std::uint64_t
    kManualGrenadeVelocityHistoryWindowNanoseconds =
        140ULL * 1000ULL * 1000ULL;
inline constexpr float kManualGrenadeVelocityAgePenalty = 0.35F;
inline constexpr float kManualGrenadeMaximumHandSpeed = 500.0F;
inline constexpr std::size_t kManualGrenadeVelocitySampleCapacity = 12U;

inline constexpr float kManualGrenadeDeliberateDropSpeed = 35.0F;
inline constexpr float kManualGrenadeFullStrengthHandSpeed = 260.0F;
inline constexpr float kManualGrenadeStableHorizontalDirectionSpeed = 45.0F;
inline constexpr float kManualGrenadeMinimumNativeStrength = 0.70F;
inline constexpr float kManualGrenadeMaximumNativeStrength = 1.15F;
inline constexpr float kManualGrenadeVerticalHandScale = 0.65F;
inline constexpr float kManualGrenadeMaximumNativeProjectileSpeed = 10000.0F;

enum class ManualGrenadeSlot : std::uint8_t {
    none,
    frag,
    tactical,
};

enum class ManualGrenadeStage : std::uint8_t {
    ready,
    holding,
    released_pending,
};

enum class ManualGrenadeEvent : std::uint8_t {
    none,
    grab_frag,
    grab_tactical,
    release,
    reset,
};

// WaW's native live-grenade prompt publishes the remaining fuse in
// throwBackGrenadeTimeLeft before the player owns the grenade. Matching the
// COD4 VR interaction means a left-hip frag grab may proceed without belt
// ammo while that prompt is active. Tactical slots never inherit this global
// exemption.
[[nodiscard]] bool manual_grenade_has_frag_throwback_override(
    ManualGrenadeSlot slot,
    std::int32_t throwback_time_left) noexcept;

struct ManualGrenadeGestureState final {
    ManualGrenadeStage stage{ManualGrenadeStage::ready};
    ManualGrenadeSlot held_slot{ManualGrenadeSlot::none};
    bool input_initialized{};
    bool trigger_was_held{};
};

[[nodiscard]] bool manual_grenade_left_hip_contains(
    const ManualGrenadePoint& head_local_position) noexcept;

[[nodiscard]] bool manual_grenade_right_hip_contains(
    const ManualGrenadePoint& head_local_position) noexcept;

[[nodiscard]] ManualGrenadeSlot manual_grenade_slot_at(
    const ManualGrenadePoint& head_local_position) noexcept;

// Advances an edge-driven left-index-trigger interaction. A trigger already
// held when input first becomes valid cannot grab a belt object; releasing it
// once arms the next press. input_valid should include gameplay focus and the
// left-hand pose. Losing any of those facts resets without manufacturing a
// throw/release event. new_grab_blocked arbitrates reload/support interactions:
// it consumes a conflicting press edge but never prevents release of a grenade
// that this state machine already owns.
[[nodiscard]] bool update_manual_grenade_gesture(
    bool input_valid,
    bool left_trigger_active,
    float left_trigger_value,
    const ManualGrenadePoint& left_hand_head_local_position,
    bool new_grab_blocked,
    ManualGrenadeGestureState* state,
    ManualGrenadeEvent* event) noexcept;

// The native grenade-spawn hook calls this after consuming or timing out a
// ReleasedPending interaction. Trigger baseline ownership is deliberately
// retained, allowing the next physical press edge to grab another grenade.
[[nodiscard]] bool rearm_manual_grenade_gesture(
    ManualGrenadeGestureState* state) noexcept;

void reset_manual_grenade_gesture(
    ManualGrenadeGestureState* state) noexcept;

struct ManualGrenadeVelocitySample final {
    bool valid{};
    std::uint64_t sampled_monotonic_nanoseconds{};
    ManualGrenadePoint velocity_game_units_per_second{};
};

struct ManualGrenadeVelocityHistory final {
    std::array<ManualGrenadeVelocitySample,
               kManualGrenadeVelocitySampleCapacity>
        samples{};
    std::size_t write_index{};
};

void clear_manual_grenade_velocity_history(
    ManualGrenadeVelocityHistory* history) noexcept;

// Records one finite hand velocity. Overspeed samples are capped without
// changing direction before entering the fixed-size ring.
[[nodiscard]] bool record_manual_grenade_velocity(
    std::uint64_t sampled_monotonic_nanoseconds,
    const ManualGrenadePoint& velocity_game_units_per_second,
    ManualGrenadeVelocityHistory* history) noexcept;

// Selects the strongest sample from the last 140 ms using COD4's bounded age
// penalty, then independently caps the result. Future, stale, and non-finite
// samples are ignored.
[[nodiscard]] bool select_manual_grenade_release_velocity(
    std::uint64_t now_monotonic_nanoseconds,
    const ManualGrenadeVelocityHistory& history,
    ManualGrenadePoint* velocity_game_units_per_second,
    std::uint64_t* sample_age_nanoseconds) noexcept;

struct ManualGrenadeLaunchCalibration final {
    float native_projectile_speed{};
    float native_projectile_speed_forward{};
    float native_projectile_speed_up{};
};

struct ManualGrenadeLaunchResult final {
    ManualGrenadePoint velocity_game_units_per_second{};
    bool deliberate_drop{};
    bool used_fallback_direction{};
    float normalized_strength{};
};

// Maps physical release speed onto the native grenade's projectile tuning
// while preserving the hand's horizontal direction. A still release remains
// a deliberate physical drop. Invalid inputs clear result and fail closed.
[[nodiscard]] bool build_manual_grenade_launch_velocity(
    const ManualGrenadePoint& physical_velocity,
    const ManualGrenadePoint& fallback_forward,
    const ManualGrenadeLaunchCalibration& calibration,
    ManualGrenadeLaunchResult* result) noexcept;

}  // namespace wawvr::mod
