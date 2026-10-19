// SPDX-License-Identifier: GPL-3.0-only
#include "manual_grenade_logic.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace wawvr::mod {
namespace {

[[nodiscard]] bool finite_vector(
    const ManualGrenadePoint& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] float vector_length(
    const ManualGrenadePoint& value) noexcept {
    const double length_squared =
        static_cast<double>(value.x) * value.x +
        static_cast<double>(value.y) * value.y +
        static_cast<double>(value.z) * value.z;
    if (!std::isfinite(length_squared) || length_squared < 0.0) {
        return (std::numeric_limits<float>::quiet_NaN)();
    }
    const double length = std::sqrt(length_squared);
    if (!std::isfinite(length) ||
        length > static_cast<double>((std::numeric_limits<float>::max)())) {
        return (std::numeric_limits<float>::quiet_NaN)();
    }
    return static_cast<float>(length);
}

[[nodiscard]] bool capped_velocity(
    const ManualGrenadePoint& input,
    ManualGrenadePoint* const output,
    float* const original_speed = nullptr) noexcept {
    if (output == nullptr || !finite_vector(input)) {
        return false;
    }
    const float speed = vector_length(input);
    if (!std::isfinite(speed)) {
        return false;
    }
    if (original_speed != nullptr) {
        *original_speed = speed;
    }
    *output = input;
    if (speed > kManualGrenadeMaximumHandSpeed) {
        const float scale = kManualGrenadeMaximumHandSpeed / speed;
        output->x *= scale;
        output->y *= scale;
        output->z *= scale;
    }
    return finite_vector(*output);
}

[[nodiscard]] bool valid_slot(const ManualGrenadeSlot slot) noexcept {
    return slot == ManualGrenadeSlot::none ||
           slot == ManualGrenadeSlot::frag ||
           slot == ManualGrenadeSlot::tactical;
}

[[nodiscard]] bool valid_gesture_state(
    const ManualGrenadeGestureState& state) noexcept {
    if (!valid_slot(state.held_slot)) {
        return false;
    }
    switch (state.stage) {
        case ManualGrenadeStage::ready:
            return state.held_slot == ManualGrenadeSlot::none;
        case ManualGrenadeStage::holding:
        case ManualGrenadeStage::released_pending:
            return state.held_slot == ManualGrenadeSlot::frag ||
                   state.held_slot == ManualGrenadeSlot::tactical;
    }
    return false;
}

[[nodiscard]] bool valid_calibration_component(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F &&
           value <= kManualGrenadeMaximumNativeProjectileSpeed;
}

}  // namespace

bool manual_grenade_left_hip_contains(
    const ManualGrenadePoint& head_local_position) noexcept {
    return finite_vector(head_local_position) &&
           head_local_position.x >= kManualGrenadeBeltForwardMinimum &&
           head_local_position.x <= kManualGrenadeBeltForwardMaximum &&
           head_local_position.y >= kManualGrenadeBeltSideMinimum &&
           head_local_position.y <= kManualGrenadeBeltSideMaximum &&
           head_local_position.z >= kManualGrenadeBeltUpMinimum &&
           head_local_position.z <= kManualGrenadeBeltUpMaximum;
}

bool manual_grenade_right_hip_contains(
    const ManualGrenadePoint& head_local_position) noexcept {
    return finite_vector(head_local_position) &&
           head_local_position.x >= kManualGrenadeBeltForwardMinimum &&
           head_local_position.x <= kManualGrenadeBeltForwardMaximum &&
           head_local_position.y <= -kManualGrenadeBeltSideMinimum &&
           head_local_position.y >= -kManualGrenadeBeltSideMaximum &&
           head_local_position.z >= kManualGrenadeBeltUpMinimum &&
           head_local_position.z <= kManualGrenadeBeltUpMaximum;
}

ManualGrenadeSlot manual_grenade_slot_at(
    const ManualGrenadePoint& head_local_position) noexcept {
    if (manual_grenade_left_hip_contains(head_local_position)) {
        return ManualGrenadeSlot::frag;
    }
    if (manual_grenade_right_hip_contains(head_local_position)) {
        return ManualGrenadeSlot::tactical;
    }
    return ManualGrenadeSlot::none;
}

bool manual_grenade_has_frag_throwback_override(
    const ManualGrenadeSlot slot,
    const std::int32_t throwback_time_left) noexcept {
    return slot == ManualGrenadeSlot::frag &&
           throwback_time_left > 0;
}

bool update_manual_grenade_gesture(
    const bool input_valid,
    const bool left_trigger_active,
    const float left_trigger_value,
    const ManualGrenadePoint& left_hand_head_local_position,
    const bool new_grab_blocked,
    ManualGrenadeGestureState* const state,
    ManualGrenadeEvent* const event,
    const bool contextual_throwback_active) noexcept {
    if (state == nullptr || event == nullptr) {
        return false;
    }
    *event = ManualGrenadeEvent::none;

    const bool sample_valid =
        input_valid && left_trigger_active &&
        std::isfinite(left_trigger_value) &&
        finite_vector(left_hand_head_local_position);
    if (!sample_valid || !valid_gesture_state(*state)) {
        const bool changed =
            state->stage != ManualGrenadeStage::ready ||
            state->held_slot != ManualGrenadeSlot::none ||
            state->input_initialized || state->trigger_was_held;
        *state = {};
        if (changed) {
            *event = ManualGrenadeEvent::reset;
        }
        return true;
    }

    const float clamped =
        std::clamp(left_trigger_value, 0.0F, 1.0F);
    const bool trigger_held = state->trigger_was_held
        ? clamped > kManualGrenadeTriggerRelease
        : clamped >= kManualGrenadeTriggerEngage;

    if (!state->input_initialized) {
        state->input_initialized = true;
        state->trigger_was_held = trigger_held;
        return true;
    }

    const bool pressed_edge = trigger_held && !state->trigger_was_held;
    const bool released_edge = !trigger_held && state->trigger_was_held;
    state->trigger_was_held = trigger_held;

    if (state->stage == ManualGrenadeStage::ready && pressed_edge &&
        !new_grab_blocked) {
        const ManualGrenadeSlot slot = contextual_throwback_active
            ? ManualGrenadeSlot::frag
            : manual_grenade_slot_at(left_hand_head_local_position);
        if (slot == ManualGrenadeSlot::frag) {
            state->stage = ManualGrenadeStage::holding;
            state->held_slot = slot;
            state->contextual_throwback = contextual_throwback_active;
            *event = ManualGrenadeEvent::grab_frag;
        } else if (slot == ManualGrenadeSlot::tactical) {
            state->stage = ManualGrenadeStage::holding;
            state->held_slot = slot;
            *event = ManualGrenadeEvent::grab_tactical;
        }
    } else if (state->stage == ManualGrenadeStage::holding &&
               released_edge) {
        state->stage = ManualGrenadeStage::released_pending;
        *event = ManualGrenadeEvent::release;
    }
    return true;
}

bool rearm_manual_grenade_gesture(
    ManualGrenadeGestureState* const state) noexcept {
    if (state == nullptr || !valid_gesture_state(*state) ||
        state->stage == ManualGrenadeStage::holding) {
        return false;
    }
    state->stage = ManualGrenadeStage::ready;
    state->held_slot = ManualGrenadeSlot::none;
    state->contextual_throwback = false;
    return true;
}

void reset_manual_grenade_gesture(
    ManualGrenadeGestureState* const state) noexcept {
    if (state != nullptr) {
        *state = {};
    }
}

bool begin_contextual_throwback(
    const std::uint64_t now_milliseconds,
    const std::uint64_t generation,
    const std::uint32_t weapon_index,
    const std::int32_t positive_native_window,
    ContextualThrowbackTransaction* const transaction) noexcept {
    if (transaction == nullptr ||
        transaction->stage != ContextualThrowbackStage::inactive ||
        now_milliseconds == 0 || generation == 0 ||
        weapon_index == 0 || weapon_index > 127 ||
        positive_native_window <= 0 || positive_native_window > 60000) {
        return false;
    }
    const auto wait = (std::min)(
        static_cast<std::uint64_t>(positive_native_window), 1000ULL);
    if (now_milliseconds >
        (std::numeric_limits<std::uint64_t>::max)() - wait) {
        return false;
    }
    *transaction = {};
    transaction->stage = ContextualThrowbackStage::waiting_for_pickup;
    transaction->generation = generation;
    transaction->began_milliseconds = now_milliseconds;
    transaction->last_observed_milliseconds = now_milliseconds;
    transaction->weapon_index = weapon_index;
    transaction->native_window_at_begin = positive_native_window;
    transaction->pickup_deadline_milliseconds = now_milliseconds + wait;
    return true;
}

bool validate_contextual_throwback_pickup_attempt(
    const std::uint64_t now_milliseconds,
    const std::uint64_t generation,
    ContextualThrowbackTransaction* const transaction) noexcept {
    if (transaction == nullptr || generation == 0 ||
        transaction->generation != generation ||
        transaction->stage != ContextualThrowbackStage::waiting_for_pickup) {
        return false;
    }
    if (transaction->began_milliseconds == 0 ||
        transaction->last_observed_milliseconds < transaction->began_milliseconds ||
        now_milliseconds < transaction->last_observed_milliseconds ||
        now_milliseconds >= transaction->pickup_deadline_milliseconds) {
        transaction->stage = ContextualThrowbackStage::aborted;
        return false;
    }
    transaction->last_observed_milliseconds = now_milliseconds;
    return true;
}

bool witness_contextual_throwback_pickup(
    const std::uint64_t now_milliseconds,
    const std::uint64_t generation,
    const std::uint32_t native_weapon_index,
    const std::int32_t native_owner,
    const std::int32_t unchanged_native_fuse,
    const bool source_was_live_missile,
    const bool source_was_freed_by_native_pickup,
    ContextualThrowbackTransaction* const transaction) noexcept {
    if (!validate_contextual_throwback_pickup_attempt(
            now_milliseconds, generation, transaction)) {
        return false;
    }
    if (native_weapon_index != transaction->weapon_index ||
        native_owner < 0 || native_owner >= 0x3FF ||
        unchanged_native_fuse <= 0 || unchanged_native_fuse > 60000 ||
        unchanged_native_fuse > transaction->native_window_at_begin ||
        !source_was_live_missile || !source_was_freed_by_native_pickup) {
        transaction->stage = ContextualThrowbackStage::aborted;
        return false;
    }
    transaction->native_owner = native_owner;
    transaction->fuse_at_pickup = unchanged_native_fuse;
    transaction->stage = ContextualThrowbackStage::acquired;
    return true;
}

ContextualThrowbackFireDecision evaluate_contextual_throwback_fire(
    const std::uint64_t now_milliseconds,
    const ContextualThrowbackPlayerState player_state,
    const std::uint32_t native_weapon_index,
    const std::int32_t native_owner,
    ContextualThrowbackTransaction* const transaction) noexcept {
    using Decision = ContextualThrowbackFireDecision;
    using Stage = ContextualThrowbackStage;
    if (transaction == nullptr ||
        transaction->stage == Stage::inactive ||
        player_state == ContextualThrowbackPlayerState::unrelated) {
        return Decision::passthrough;
    }
    if (player_state != ContextualThrowbackPlayerState::predicted &&
        player_state != ContextualThrowbackPlayerState::authoritative) {
        return Decision::hold;
    }
    if (transaction->stage == Stage::spent ||
        transaction->stage == Stage::aborted ||
        transaction->stage == Stage::dispatch_in_flight ||
        transaction->authoritative_fire_permitted) {
        // A queued event39 reads CURRENT owner/fuse, not immutable event data.
        // Never enter native cancel while dispatch is unresolved, including on
        // mismatch, clock regression or teardown. Even after handler return,
        // idle states alone cannot prove that its old event queues are empty.
        return Decision::hold;
    }
    if (transaction->stage == Stage::waiting_for_pickup) {
        if (!validate_contextual_throwback_pickup_attempt(
                now_milliseconds, transaction->generation, transaction)) {
            return Decision::hold;
        }
        return Decision::hold;
    }
    if (transaction->stage != Stage::acquired ||
        now_milliseconds < transaction->last_observed_milliseconds) {
        transaction->stage = Stage::aborted;
        return Decision::hold;
    }
    transaction->last_observed_milliseconds = now_milliseconds;
    if (native_weapon_index != transaction->weapon_index ||
        native_owner != transaction->native_owner || native_owner == 0x3FF) {
        // The prediction copy can lag the accepted server pickup. Never let
        // its sentinel owner enter the native own-ammo branch while waiting.
        if (player_state == ContextualThrowbackPlayerState::predicted &&
            now_milliseconds <= transaction->pickup_deadline_milliseconds) {
            return Decision::hold;
        }
        if (player_state == ContextualThrowbackPlayerState::authoritative) {
            transaction->stage = Stage::aborted;
        }
        return Decision::hold;
    }
    if (player_state == ContextualThrowbackPlayerState::authoritative) {
        transaction->authoritative_fire_permitted = true;
    }
    // Prediction may replay a usercmd. Do not treat matching prediction replay
    // as a second authoritative missile; the spawn seam is separately one-use.
    return Decision::allow_enemy;
}

bool consume_contextual_throwback_spawn(
    const std::uint32_t native_weapon_index,
    ContextualThrowbackTransaction* const transaction,
    const ContextualThrowbackNativeEventReceipt* const receipt) noexcept {
    if (transaction == nullptr ||
        transaction->stage != ContextualThrowbackStage::acquired ||
        !transaction->authoritative_fire_permitted ||
        native_weapon_index != transaction->weapon_index ||
        receipt == nullptr || !receipt->identity_verified ||
        receipt->generation == 0 ||
        receipt->generation != transaction->generation ||
        receipt->dispatch_receipt == 0 ||
        transaction->dispatch_receipt != 0) {
        return false;
    }
    transaction->dispatch_receipt = receipt->dispatch_receipt;
    transaction->stage = ContextualThrowbackStage::dispatch_in_flight;
    return true;
}

bool complete_contextual_throwback_dispatch(
    const ContextualThrowbackNativeEventReceipt& receipt,
    ContextualThrowbackTransaction* const transaction) noexcept {
    if (transaction == nullptr || !receipt.identity_verified ||
        receipt.generation == 0 ||
        receipt.generation != transaction->generation ||
        receipt.dispatch_receipt == 0 ||
        receipt.dispatch_receipt != transaction->dispatch_receipt ||
        transaction->stage != ContextualThrowbackStage::dispatch_in_flight) {
        return false;
    }
    transaction->stage = ContextualThrowbackStage::spent;
    return true;
}

void abort_contextual_throwback(
    ContextualThrowbackTransaction* const transaction) noexcept {
    if (transaction != nullptr &&
        transaction->stage == ContextualThrowbackStage::waiting_for_pickup) {
        transaction->stage = ContextualThrowbackStage::aborted;
    }
}

bool retire_contextual_throwback_tombstone(
    const bool physical_trigger_released,
    const bool authoritative_native_idle,
    const bool predicted_native_idle,
    ContextualThrowbackTransaction* const transaction,
    const ContextualThrowbackNativeDrainWitness* const drain) noexcept {
    if (transaction == nullptr ||
        (transaction->stage != ContextualThrowbackStage::spent &&
         transaction->stage != ContextualThrowbackStage::aborted) ||
        !physical_trigger_released || !authoritative_native_idle ||
        !predicted_native_idle || drain == nullptr ||
        drain->generation == 0 ||
        drain->generation != transaction->generation ||
        !drain->authoritative_queue_drained ||
        !drain->predicted_queue_drained) {
        return false;
    }
    *transaction = {};
    return true;
}

bool contextual_throwback_scheduled_expiry_preserved(
    const ContextualThrowbackExpiryWitness& witness) noexcept {
    if (!witness.source_identity_verified ||
        !witness.spawned_missile_identity_verified ||
        !witness.same_native_clock_verified ||
        witness.source_scheduled_expiry <= 0 ||
        witness.acquisition_native_time < 0 ||
        witness.spawn_native_time < witness.acquisition_native_time ||
        witness.source_scheduled_expiry != witness.spawned_scheduled_expiry) {
        return false;
    }
    // Both operands are nonnegative int64 values, so their difference cannot
    // overflow. This checks the remaining native fuse without rewriting it or
    // relating native level time to GetTickCount64's unrelated clock domain.
    return witness.source_scheduled_expiry - witness.spawn_native_time ==
        static_cast<std::int64_t>(witness.remaining_fuse_at_spawn);
}

void clear_manual_grenade_velocity_history(
    ManualGrenadeVelocityHistory* const history) noexcept {
    if (history != nullptr) {
        *history = {};
    }
}

bool record_manual_grenade_velocity(
    const std::uint64_t sampled_monotonic_nanoseconds,
    const ManualGrenadePoint& velocity_game_units_per_second,
    ManualGrenadeVelocityHistory* const history) noexcept {
    if (sampled_monotonic_nanoseconds == 0U || history == nullptr ||
        history->write_index >= history->samples.size()) {
        return false;
    }
    ManualGrenadePoint bounded{};
    if (!capped_velocity(velocity_game_units_per_second, &bounded)) {
        return false;
    }
    ManualGrenadeVelocitySample& sample =
        history->samples[history->write_index];
    sample.valid = true;
    sample.sampled_monotonic_nanoseconds =
        sampled_monotonic_nanoseconds;
    sample.velocity_game_units_per_second = bounded;
    history->write_index =
        (history->write_index + 1U) % history->samples.size();
    return true;
}

bool select_manual_grenade_release_velocity(
    const std::uint64_t now_monotonic_nanoseconds,
    const ManualGrenadeVelocityHistory& history,
    ManualGrenadePoint* const velocity_game_units_per_second,
    std::uint64_t* const sample_age_nanoseconds) noexcept {
    if (velocity_game_units_per_second == nullptr ||
        sample_age_nanoseconds == nullptr) {
        return false;
    }
    *velocity_game_units_per_second = {};
    *sample_age_nanoseconds = 0U;
    if (now_monotonic_nanoseconds == 0U ||
        history.write_index >= history.samples.size()) {
        return false;
    }

    bool selected = false;
    float selected_score = -1.0F;
    ManualGrenadePoint selected_velocity{};
    std::uint64_t selected_age = 0U;
    for (const ManualGrenadeVelocitySample& sample : history.samples) {
        if (!sample.valid || sample.sampled_monotonic_nanoseconds == 0U ||
            sample.sampled_monotonic_nanoseconds >
                now_monotonic_nanoseconds ||
            !finite_vector(sample.velocity_game_units_per_second)) {
            continue;
        }
        const std::uint64_t age = now_monotonic_nanoseconds -
            sample.sampled_monotonic_nanoseconds;
        if (age > kManualGrenadeVelocityHistoryWindowNanoseconds) {
            continue;
        }
        const float speed =
            vector_length(sample.velocity_game_units_per_second);
        if (!std::isfinite(speed)) {
            continue;
        }
        const float normalized_age = static_cast<float>(
            static_cast<double>(age) /
            static_cast<double>(
                kManualGrenadeVelocityHistoryWindowNanoseconds));
        const float score =
            speed * (1.0F -
                     kManualGrenadeVelocityAgePenalty * normalized_age);
        if (!std::isfinite(score)) {
            continue;
        }
        if (!selected || score > selected_score) {
            selected = true;
            selected_score = score;
            selected_velocity = sample.velocity_game_units_per_second;
            selected_age = age;
        }
    }
    if (!selected ||
        !capped_velocity(selected_velocity,
                         velocity_game_units_per_second)) {
        *velocity_game_units_per_second = {};
        return false;
    }
    *sample_age_nanoseconds = selected_age;
    return true;
}

bool build_manual_grenade_launch_velocity(
    const ManualGrenadePoint& physical_velocity,
    const ManualGrenadePoint& fallback_forward,
    const ManualGrenadeLaunchCalibration& calibration,
    ManualGrenadeLaunchResult* const result) noexcept {
    if (result == nullptr) {
        return false;
    }
    *result = {};
    if (!finite_vector(physical_velocity) ||
        !finite_vector(fallback_forward) ||
        !valid_calibration_component(calibration.native_projectile_speed) ||
        !valid_calibration_component(
            calibration.native_projectile_speed_forward) ||
        !valid_calibration_component(
            calibration.native_projectile_speed_up)) {
        return false;
    }

    const float physical_speed = vector_length(physical_velocity);
    if (!std::isfinite(physical_speed)) {
        return false;
    }
    result->deliberate_drop =
        physical_speed < kManualGrenadeDeliberateDropSpeed;
    if (result->deliberate_drop) {
        result->velocity_game_units_per_second = physical_velocity;
        return true;
    }

    const float strength = std::clamp(
        (physical_speed - kManualGrenadeDeliberateDropSpeed) /
            (kManualGrenadeFullStrengthHandSpeed -
             kManualGrenadeDeliberateDropSpeed),
        0.0F, 1.0F);
    result->normalized_strength = strength;

    float horizontal_x = physical_velocity.x;
    float horizontal_y = physical_velocity.y;
    float horizontal_length = static_cast<float>(std::hypot(
        static_cast<double>(horizontal_x),
        static_cast<double>(horizontal_y)));
    if (!std::isfinite(horizontal_length)) {
        *result = {};
        return false;
    }
    if (horizontal_length <
        kManualGrenadeStableHorizontalDirectionSpeed) {
        horizontal_x = fallback_forward.x;
        horizontal_y = fallback_forward.y;
        horizontal_length = static_cast<float>(std::hypot(
            static_cast<double>(horizontal_x),
            static_cast<double>(horizontal_y)));
        result->used_fallback_direction = true;
    }
    if (!std::isfinite(horizontal_length) || horizontal_length < 0.001F) {
        horizontal_x = 1.0F;
        horizontal_y = 0.0F;
        horizontal_length = 1.0F;
        result->used_fallback_direction = true;
    }
    horizontal_x /= horizontal_length;
    horizontal_y /= horizontal_length;

    float native_horizontal = calibration.native_projectile_speed +
        calibration.native_projectile_speed_forward;
    if (!std::isfinite(native_horizontal)) {
        *result = {};
        return false;
    }
    if (native_horizontal < 1.0F) {
        native_horizontal = 700.0F;
    }
    const float horizontal_launch = native_horizontal *
        (kManualGrenadeMinimumNativeStrength +
         (kManualGrenadeMaximumNativeStrength -
          kManualGrenadeMinimumNativeStrength) *
             strength);
    result->velocity_game_units_per_second.x =
        horizontal_x * horizontal_launch;
    result->velocity_game_units_per_second.y =
        horizontal_y * horizontal_launch;

    const float native_up = calibration.native_projectile_speed_up;
    float vertical =
        native_up + physical_velocity.z * kManualGrenadeVerticalHandScale;
    const float minimum_arc = std::max(native_up * 0.35F, 80.0F);
    const float maximum_arc =
        std::max(native_up + native_horizontal * 0.55F, 400.0F);
    vertical = std::clamp(vertical, minimum_arc, maximum_arc);
    result->velocity_game_units_per_second.z = vertical;

    if (!finite_vector(result->velocity_game_units_per_second) ||
        !std::isfinite(result->normalized_strength)) {
        *result = {};
        return false;
    }
    return true;
}

}  // namespace wawvr::mod
