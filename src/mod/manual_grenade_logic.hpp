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
    // A contextual grab must never silently fall back to a fresh belt grenade
    // if the native throwback window expires between sample and inventory read.
    bool contextual_throwback{};
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
    ManualGrenadeEvent* event,
    bool contextual_throwback_active = false) noexcept;

// The native grenade-spawn hook calls this after consuming or timing out a
// ReleasedPending interaction. Trigger baseline ownership is deliberately
// retained, allowing the next physical press edge to grab another grenade.
[[nodiscard]] bool rearm_manual_grenade_gesture(
    ManualGrenadeGestureState* state) noexcept;

void reset_manual_grenade_gesture(
    ManualGrenadeGestureState* state) noexcept;

// Experimental contextual throwback is an enemy-object transaction, not an
// ordinary frag-button fallback. A native pickup witness must prove that the
// existing missile was consumed before either local simulation may fire. The
// tombstone survives the visual/gesture transaction until both local native
// states are idle, the trigger is released and their event queues have a
// separately verified exact-generation drain witness.
enum class ContextualThrowbackStage : std::uint8_t {
    inactive,
    waiting_for_pickup,
    acquired,
    dispatch_in_flight,
    spent,
    aborted,
};

enum class ContextualThrowbackPlayerState : std::uint8_t {
    unrelated,
    predicted,
    authoritative,
};

enum class ContextualThrowbackFireDecision : std::uint8_t {
    passthrough,
    hold,
    cancel,
    allow_enemy,
};

// This receipt must come from a separately validated native event-ownership
// adapter. A current weapon/owner, mod generation or wrapped event ordinal is
// NOT such proof. The dormant runtime has no adapter and supplies no receipt.
struct ContextualThrowbackNativeEventReceipt final {
    bool identity_verified{};
    std::uint64_t generation{};
    std::uint64_t dispatch_receipt{};
};

// Native idle weapon states do not prove that old event39/event40 entries have
// drained. Both native queues need an exact-generation drain witness before a
// tombstone can disappear and permit a fresh transaction. Unknown stays held.
struct ContextualThrowbackNativeDrainWitness final {
    std::uint64_t generation{};
    bool authoritative_queue_drained{};
    bool predicted_queue_drained{};
};

struct ContextualThrowbackTransaction final {
    ContextualThrowbackStage stage{ContextualThrowbackStage::inactive};
    std::uint64_t generation{};
    std::uint64_t began_milliseconds{};
    std::uint64_t last_observed_milliseconds{};
    std::uint64_t pickup_deadline_milliseconds{};
    std::uint32_t weapon_index{};
    std::int32_t native_owner{0x3FF};
    std::int32_t native_window_at_begin{};
    std::int32_t fuse_at_pickup{};
    bool authoritative_fire_permitted{};
    std::uint64_t dispatch_receipt{};
};

[[nodiscard]] bool begin_contextual_throwback(
    std::uint64_t now_milliseconds,
    std::uint64_t generation,
    std::uint32_t weapon_index,
    std::int32_t positive_native_window,
    ContextualThrowbackTransaction* transaction) noexcept;

// Must be called immediately BEFORE entering the native item-consumption
// routine, not merely when observing its return. A stale generation leaves the
// current transaction untouched; an expired or regressed clock tombstones the
// matching transaction without allowing an enemy missile to be consumed.
[[nodiscard]] bool validate_contextual_throwback_pickup_attempt(
    std::uint64_t now_milliseconds,
    std::uint64_t generation,
    ContextualThrowbackTransaction* transaction) noexcept;

[[nodiscard]] bool witness_contextual_throwback_pickup(
    std::uint64_t now_milliseconds,
    std::uint64_t generation,
    std::uint32_t native_weapon_index,
    std::int32_t native_owner,
    std::int32_t unchanged_native_fuse,
    bool source_was_live_missile,
    bool source_was_freed_by_native_pickup,
    ContextualThrowbackTransaction* transaction) noexcept;

[[nodiscard]] ContextualThrowbackFireDecision
evaluate_contextual_throwback_fire(
    std::uint64_t now_milliseconds,
    ContextualThrowbackPlayerState player_state,
    std::uint32_t native_weapon_index,
    std::int32_t native_owner,
    ContextualThrowbackTransaction* transaction) noexcept;

// Reserves one authoritative dispatch permission without marking it spent
// before the native void handler executes. No verified receipt means no native
// dispatch; this is an intentionally unavailable boundary in the dormant code.
[[nodiscard]] bool consume_contextual_throwback_spawn(
    std::uint32_t native_weapon_index,
    ContextualThrowbackTransaction* transaction,
    const ContextualThrowbackNativeEventReceipt* receipt = nullptr) noexcept;

// Acknowledges the exact same handler only AFTER its native call has returned.
// It is not a grenade-spawn, inventory or remaining-fuse acceptance witness.
[[nodiscard]] bool complete_contextual_throwback_dispatch(
    const ContextualThrowbackNativeEventReceipt& receipt,
    ContextualThrowbackTransaction* transaction) noexcept;

void abort_contextual_throwback(
    ContextualThrowbackTransaction* transaction) noexcept;

[[nodiscard]] bool retire_contextual_throwback_tombstone(
    bool physical_trigger_released,
    bool authoritative_native_idle,
    bool predicted_native_idle,
    ContextualThrowbackTransaction* transaction,
    const ContextualThrowbackNativeDrainWitness* drain = nullptr) noexcept;

// Read-only comparison in one independently verified, unwrapped native level-
// time domain. Argument equality alone cannot establish the native scheduled
// expiry: G_FireGrenade can consume playerState+0x48 instead of that argument.
// Past expiry and zero/negative remaining cook time are not refreshed/clamped.
struct ContextualThrowbackExpiryWitness final {
    bool source_identity_verified{};
    bool spawned_missile_identity_verified{};
    bool same_native_clock_verified{};
    std::int64_t source_scheduled_expiry{};
    std::int64_t acquisition_native_time{};
    std::int64_t spawn_native_time{};
    std::int64_t spawned_scheduled_expiry{};
    std::int32_t remaining_fuse_at_spawn{};
};

[[nodiscard]] bool contextual_throwback_scheduled_expiry_preserved(
    const ContextualThrowbackExpiryWitness& witness) noexcept;

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
