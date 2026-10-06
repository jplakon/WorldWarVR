// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace wawvr::mod {

inline constexpr std::uint32_t kBazookaTrailEntityCount = 1024;
inline constexpr std::uint64_t kBazookaTrailSeedMaximumAgeMilliseconds = 2'000;
inline constexpr float kBazookaTrailSeedMaximumGapWorldUnits = 512.0F;
inline constexpr std::uint16_t kFxHandleNone = 0xFFFF;
inline constexpr std::int32_t kFxEffectCooperativeLockBit = 0x20000000;
inline constexpr std::size_t kRetailFxEffectStride = 0xB4;
inline constexpr std::size_t kRetailFxEffectCount = 1024;
inline constexpr std::size_t kRetailFxEffectPublicTokenOffset = 0xB0;
inline constexpr std::size_t kRetailFxEffectPoolSize =
    kRetailFxEffectStride * kRetailFxEffectCount;
static_assert(
    kRetailFxEffectPublicTokenOffset + sizeof(std::uint32_t) ==
    kRetailFxEffectStride);
static_assert(kRetailFxEffectPoolSize == 0x2D000);

enum class FxEffectTokenResolutionStatus : std::uint8_t {
    resolved,
    invalid_pool_size,
    zero_token,
    not_found,
    ambiguous,
};

struct FxEffectTokenResolution final {
    FxEffectTokenResolutionStatus status{
        FxEffectTokenResolutionStatus::invalid_pool_size};
    std::size_t index{};

    [[nodiscard]] constexpr bool resolved() const noexcept {
        return status == FxEffectTokenResolutionStatus::resolved;
    }
};

// Retail FX_SpawnBoltedEffect returns a public DWORD token, not a raw
// FxEffect pointer. Resolve it against the exact 1024-entry retail pool while
// refusing zero, missing, or duplicated tokens.
[[nodiscard]] FxEffectTokenResolution resolve_retail_fx_effect_token(
    std::span<const std::byte> effect_pool,
    std::uint32_t public_token) noexcept;

[[nodiscard]] constexpr bool valid_retail_fx_effect_pool_geometry(
    const std::uintptr_t effects,
    const std::uintptr_t elems) noexcept {
    return effects != 0 && elems >= effects &&
           effects <=
               (std::numeric_limits<std::uintptr_t>::max)() -
                   kRetailFxEffectPoolSize &&
           effects + kRetailFxEffectPoolSize == elems;
}

struct BazookaTrailSeedToken final {
    bool valid{};
    std::uint32_t entity_number{};
    std::uintptr_t effect_definition{};
    std::uint64_t publication_milliseconds{};
    std::array<float, 3> muzzle_origin{};
};

enum class BazookaTrailSeedDecision : std::uint8_t {
    apply,
    hook_disabled,
    wrong_local_client,
    entity_out_of_range,
    no_pending_seed,
    effect_definition_mismatch,
    clock_reversed,
    stale,
    effect_spawn_failed,
};

// Pure gate for the client FX bridge. A definition mismatch intentionally
// retains the token: an unrelated bolted effect may be spawned on the same
// missile immediately before its projectile trail.
[[nodiscard]] BazookaTrailSeedDecision evaluate_bazooka_trail_seed(
    const BazookaTrailSeedToken& token,
    bool hook_enabled,
    std::int32_t local_client_number,
    std::uint32_t dobj_handle,
    std::uintptr_t effect_definition,
    bool effect_spawned,
    std::uint64_t now_milliseconds) noexcept;

[[nodiscard]] constexpr bool bazooka_trail_seed_decision_consumes_token(
    const BazookaTrailSeedDecision decision) noexcept {
    return decision == BazookaTrailSeedDecision::apply ||
           decision == BazookaTrailSeedDecision::clock_reversed ||
           decision == BazookaTrailSeedDecision::stale ||
           decision == BazookaTrailSeedDecision::effect_spawn_failed;
}

[[nodiscard]] bool finite_bazooka_trail_muzzle(
    const std::array<float, 3>& muzzle_origin) noexcept;

struct BazookaTrailSeedDistanceAdjustment final {
    bool valid{};
    float gap_world_units{};
    float first_spawn_distance{};
};

// Moving the native first trail element back to the physical muzzle adds a
// connector segment that the authored spawn distance did not account for.
// Give that element a negative distance so T4's trail UV progression includes
// the connector while every later native element retains its original value.
[[nodiscard]] BazookaTrailSeedDistanceAdjustment
compute_bazooka_trail_seed_distance_adjustment(
    const std::array<float, 3>& authored_first_origin,
    const std::array<float, 3>& muzzle_origin) noexcept;

// Mirrors T4's signed LONG comparison after InterlockedExchangeAdd(status,
// 0x20000000). A previous value below the lock bit means this caller acquired
// the cooperative per-effect lock; otherwise it must undo the add and retry.
[[nodiscard]] constexpr bool fx_effect_lock_attempt_succeeded(
    const std::int32_t previous_status) noexcept {
    return previous_status < kFxEffectCooperativeLockBit;
}

[[nodiscard]] constexpr bool is_initial_fx_trail_element(
    const float spawn_distance,
    const std::uint8_t sequence) noexcept {
    return spawn_distance == 0.0F && sequence == 0;
}

// T4 FX pool handles encode a byte offset divided by HANDLE_SCALE. This
// rejects the sentinel, misaligned handles, and the first value beyond the
// exact pool capacity before runtime code computes an address.
[[nodiscard]] constexpr bool valid_fx_pool_handle(
    const std::uint16_t handle,
    const std::size_t item_size,
    const std::size_t handle_scale,
    const std::size_t capacity) noexcept {
    if (handle == kFxHandleNone || item_size == 0 || handle_scale == 0 ||
        capacity == 0 || item_size % handle_scale != 0) {
        return false;
    }
    const std::size_t handle_stride = item_size / handle_scale;
    return handle_stride != 0 && handle % handle_stride == 0 &&
           static_cast<std::size_t>(handle) < capacity * handle_stride;
}

}  // namespace wawvr::mod
