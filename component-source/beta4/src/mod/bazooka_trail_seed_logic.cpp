// SPDX-License-Identifier: GPL-3.0-only
#include "bazooka_trail_seed_logic.hpp"

#include <cmath>
#include <cstring>

namespace wawvr::mod {

FxEffectTokenResolution resolve_retail_fx_effect_token(
    const std::span<const std::byte> effect_pool,
    const std::uint32_t public_token) noexcept {
    if (effect_pool.size() != kRetailFxEffectPoolSize) {
        return {FxEffectTokenResolutionStatus::invalid_pool_size, 0};
    }
    if (public_token == 0) {
        return {FxEffectTokenResolutionStatus::zero_token, 0};
    }

    std::size_t match_index = 0;
    bool matched = false;
    for (std::size_t index = 0; index < kRetailFxEffectCount; ++index) {
        std::uint32_t candidate_token = 0;
        std::memcpy(
            &candidate_token,
            effect_pool.data() + index * kRetailFxEffectStride +
                kRetailFxEffectPublicTokenOffset,
            sizeof(candidate_token));
        if (candidate_token != public_token) {
            continue;
        }
        if (matched) {
            return {FxEffectTokenResolutionStatus::ambiguous, 0};
        }
        matched = true;
        match_index = index;
    }
    if (!matched) {
        return {FxEffectTokenResolutionStatus::not_found, 0};
    }
    return {FxEffectTokenResolutionStatus::resolved, match_index};
}

BazookaTrailSeedDecision evaluate_bazooka_trail_seed(
    const BazookaTrailSeedToken& token,
    const bool hook_enabled,
    const std::int32_t local_client_number,
    const std::uint32_t dobj_handle,
    const std::uintptr_t effect_definition,
    const bool effect_spawned,
    const std::uint64_t now_milliseconds) noexcept {
    if (!hook_enabled) {
        return BazookaTrailSeedDecision::hook_disabled;
    }
    if (local_client_number != 0) {
        return BazookaTrailSeedDecision::wrong_local_client;
    }
    if (dobj_handle >= kBazookaTrailEntityCount) {
        return BazookaTrailSeedDecision::entity_out_of_range;
    }
    if (!token.valid || token.entity_number != dobj_handle) {
        return BazookaTrailSeedDecision::no_pending_seed;
    }
    if (token.effect_definition == 0 ||
        token.effect_definition != effect_definition) {
        return BazookaTrailSeedDecision::effect_definition_mismatch;
    }
    if (now_milliseconds < token.publication_milliseconds) {
        return BazookaTrailSeedDecision::clock_reversed;
    }
    if (now_milliseconds - token.publication_milliseconds >
        kBazookaTrailSeedMaximumAgeMilliseconds) {
        return BazookaTrailSeedDecision::stale;
    }
    if (!effect_spawned) {
        return BazookaTrailSeedDecision::effect_spawn_failed;
    }
    return BazookaTrailSeedDecision::apply;
}

bool finite_bazooka_trail_muzzle(
    const std::array<float, 3>& muzzle_origin) noexcept {
    return std::isfinite(muzzle_origin[0]) &&
           std::isfinite(muzzle_origin[1]) &&
           std::isfinite(muzzle_origin[2]);
}

BazookaTrailSeedDistanceAdjustment
compute_bazooka_trail_seed_distance_adjustment(
    const std::array<float, 3>& authored_first_origin,
    const std::array<float, 3>& muzzle_origin) noexcept {
    if (!finite_bazooka_trail_muzzle(authored_first_origin) ||
        !finite_bazooka_trail_muzzle(muzzle_origin)) {
        return {};
    }

    const double delta_x =
        static_cast<double>(authored_first_origin[0]) - muzzle_origin[0];
    const double delta_y =
        static_cast<double>(authored_first_origin[1]) - muzzle_origin[1];
    const double delta_z =
        static_cast<double>(authored_first_origin[2]) - muzzle_origin[2];
    const double gap = std::sqrt(
        delta_x * delta_x + delta_y * delta_y + delta_z * delta_z);
    if (!std::isfinite(gap) ||
        gap > static_cast<double>(kBazookaTrailSeedMaximumGapWorldUnits)) {
        return {};
    }

    const float gap_world_units = static_cast<float>(gap);
    if (!std::isfinite(gap_world_units)) {
        return {};
    }
    return {true, gap_world_units, -gap_world_units};
}

}  // namespace wawvr::mod
