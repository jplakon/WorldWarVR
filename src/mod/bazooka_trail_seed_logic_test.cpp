// SPDX-License-Identifier: GPL-3.0-only
#include "bazooka_trail_seed_logic.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace wawvr::mod;

void check(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

[[nodiscard]] BazookaTrailSeedToken accepted_token() noexcept {
    return {
        true,
        321,
        0x00B236D8,
        10'000,
        {2534.97F, 4562.21F, 18.43F},
    };
}

[[nodiscard]] BazookaTrailSeedDecision evaluate(
    const BazookaTrailSeedToken& token,
    const bool enabled = true,
    const std::int32_t local_client = 0,
    const std::uint32_t dobj_handle = 321,
    const std::uintptr_t effect_definition = 0x00B236D8,
    const bool effect_spawned = true,
    const std::uint64_t now_milliseconds = 10'001) noexcept {
    return evaluate_bazooka_trail_seed(
        token, enabled, local_client, dobj_handle, effect_definition,
        effect_spawned, now_milliseconds);
}

void test_exact_match_and_freshness_boundary() {
    const auto token = accepted_token();
    check(evaluate(token) == BazookaTrailSeedDecision::apply,
          "fresh exact entity/effect match is applied");
    check(evaluate(
              token, true, 0, token.entity_number,
              token.effect_definition, true,
              token.publication_milliseconds +
                  kBazookaTrailSeedMaximumAgeMilliseconds) ==
              BazookaTrailSeedDecision::apply,
          "maximum seed age is inclusive");
    check(evaluate(
              token, true, 0, token.entity_number,
              token.effect_definition, true,
              token.publication_milliseconds +
                  kBazookaTrailSeedMaximumAgeMilliseconds + 1) ==
              BazookaTrailSeedDecision::stale,
          "seed older than the exact age bound is rejected");
    check(evaluate(
              token, true, 0, token.entity_number,
              token.effect_definition, true,
              token.publication_milliseconds - 1) ==
              BazookaTrailSeedDecision::clock_reversed,
          "reversed monotonic clock is rejected");
}

void test_scope_gates() {
    const auto token = accepted_token();
    check(evaluate(token, false) == BazookaTrailSeedDecision::hook_disabled,
          "disabled hook cannot seed a trail");
    check(evaluate(token, true, 1) ==
              BazookaTrailSeedDecision::wrong_local_client,
          "nonzero split-screen client is rejected");
    check(evaluate(token, true, 0, kBazookaTrailEntityCount) ==
              BazookaTrailSeedDecision::entity_out_of_range,
          "DObj handle outside the entity table is rejected");

    auto missing = token;
    missing.valid = false;
    check(evaluate(missing) == BazookaTrailSeedDecision::no_pending_seed,
          "inactive entity slot is rejected");
    auto wrong_entity = token;
    wrong_entity.entity_number += 1;
    check(evaluate(wrong_entity) ==
              BazookaTrailSeedDecision::no_pending_seed,
          "slot/entity disagreement is rejected");
}

void test_definition_and_spawn_gates() {
    const auto token = accepted_token();
    check(evaluate(
              token, true, 0, token.entity_number,
              token.effect_definition + 4) ==
              BazookaTrailSeedDecision::effect_definition_mismatch,
          "another bolted effect on the rocket is rejected");
    check(!bazooka_trail_seed_decision_consumes_token(
              BazookaTrailSeedDecision::effect_definition_mismatch),
          "unrelated effect does not consume the pending projectile trail");
    check(evaluate(
              token, true, 0, token.entity_number,
              token.effect_definition, false) ==
              BazookaTrailSeedDecision::effect_spawn_failed,
          "null native effect result is reported");
    check(bazooka_trail_seed_decision_consumes_token(
              BazookaTrailSeedDecision::effect_spawn_failed) &&
              bazooka_trail_seed_decision_consumes_token(
                  BazookaTrailSeedDecision::apply) &&
              bazooka_trail_seed_decision_consumes_token(
                  BazookaTrailSeedDecision::stale) &&
              !bazooka_trail_seed_decision_consumes_token(
                  BazookaTrailSeedDecision::no_pending_seed),
          "only terminal exact-token decisions consume publication");
}

void test_muzzle_and_fx_handle_validation() {
    const auto token = accepted_token();
    check(finite_bazooka_trail_muzzle(token.muzzle_origin),
          "finite fire-time muzzle is accepted");
    auto invalid = token.muzzle_origin;
    invalid[0] = std::numeric_limits<float>::quiet_NaN();
    check(!finite_bazooka_trail_muzzle(invalid),
          "NaN fire-time muzzle is rejected");
    invalid = token.muzzle_origin;
    invalid[2] = std::numeric_limits<float>::infinity();
    check(!finite_bazooka_trail_muzzle(invalid),
          "infinite fire-time muzzle is rejected");

    check(valid_fx_pool_handle(0, 0x08, 4, 128) &&
              valid_fx_pool_handle(254, 0x08, 4, 128),
          "first and last aligned FxTrail handles are accepted");
    check(!valid_fx_pool_handle(1, 0x08, 4, 128) &&
              !valid_fx_pool_handle(256, 0x08, 4, 128) &&
              !valid_fx_pool_handle(kFxHandleNone, 0x08, 4, 128),
          "misaligned, past-end, and none FxTrail handles are rejected");
    check(valid_fx_pool_handle(0x40, 0x20, 4, 2048) &&
              valid_fx_pool_handle(0x3FF8, 0x20, 4, 2048),
          "observed and last aligned FxTrailElem handles are accepted");
    check(!valid_fx_pool_handle(0x3FFF, 0x20, 4, 2048) &&
              !valid_fx_pool_handle(0x4000, 0x20, 4, 2048),
          "misaligned and past-end FxTrailElem handles are rejected");

    check(fx_effect_lock_attempt_succeeded(0x00000001) &&
              fx_effect_lock_attempt_succeeded(0x10000001),
          "unlocked referenced effects acquire the cooperative lock");
    check(!fx_effect_lock_attempt_succeeded(0x20000001) &&
              !fx_effect_lock_attempt_succeeded(0x40000001),
          "existing cooperative lock states require undo and retry");
    check(is_initial_fx_trail_element(0.0F, 0),
          "distance-zero sequence-zero trail element is initial");
    check(!is_initial_fx_trail_element(0.001F, 0) &&
              !is_initial_fx_trail_element(0.0F, 1),
          "advanced distance or sequence cannot be reseeded as the origin");
}

void write_effect_token(
    std::array<std::byte, kRetailFxEffectPoolSize>* const pool,
    const std::size_t index,
    const std::uint32_t token) {
    check(pool != nullptr && index < kRetailFxEffectCount,
          "test token write remains inside the retail effect pool");
    std::memcpy(
        pool->data() + index * kRetailFxEffectStride +
            kRetailFxEffectPublicTokenOffset,
        &token, sizeof(token));
}

void test_fx_effect_public_token_resolution() {
    check(valid_retail_fx_effect_pool_geometry(0x016D8380, 0x01705380),
          "observed retail effects/elems span proves the 0xB4 stride");
    check(!valid_retail_fx_effect_pool_geometry(0, 0x01705380) &&
              !valid_retail_fx_effect_pool_geometry(
                  0x01705380, 0x016D8380) &&
              !valid_retail_fx_effect_pool_geometry(
                  0x016D8380, 0x0170537C) &&
              !valid_retail_fx_effect_pool_geometry(
                  0x016D8380, 0x01705384) &&
              !valid_retail_fx_effect_pool_geometry(
                  0x016D8380, 0x016F8380) &&
              !valid_retail_fx_effect_pool_geometry(
                  (std::numeric_limits<std::uintptr_t>::max)() - 3,
                  (std::numeric_limits<std::uintptr_t>::max)()),
          "null, reversed, short, long, obsolete-stride, and overflow pools are rejected");

    std::array<std::byte, kRetailFxEffectPoolSize> pool{};
    const auto span = std::span<const std::byte>{pool};
    check(resolve_retail_fx_effect_token(span, 0).status ==
              FxEffectTokenResolutionStatus::zero_token,
          "zero public token is rejected");
    check(resolve_retail_fx_effect_token(span, 0x032D0076).status ==
              FxEffectTokenResolutionStatus::not_found,
          "missing public token is rejected");
    check(resolve_retail_fx_effect_token(span.first(span.size() - 4), 1)
                  .status ==
              FxEffectTokenResolutionStatus::invalid_pool_size,
          "wrong-sized effect pool is rejected");

    constexpr std::array<std::size_t, 3> indices{
        0, kRetailFxEffectCount / 2, kRetailFxEffectCount - 1};
    constexpr std::array<std::uint32_t, 3> tokens{
        0x00010001, 0x02010042, 0x040000FF};
    for (std::size_t i = 0; i < indices.size(); ++i) {
        pool = {};
        write_effect_token(&pool, indices[i], tokens[i]);
        const auto resolved = resolve_retail_fx_effect_token(pool, tokens[i]);
        check(resolved.resolved() && resolved.index == indices[i],
              "first, middle, and last effect tokens resolve exactly");
        check(0x016D8380 + resolved.index * kRetailFxEffectStride ==
                  0x016D8380 + indices[i] * 0xB4,
              "resolved raw address uses the retail 0xB4 stride");
    }

    pool = {};
    write_effect_token(&pool, 12, 0x032D0076);
    write_effect_token(&pool, 900, 0x032D0076);
    check(resolve_retail_fx_effect_token(pool, 0x032D0076).status ==
              FxEffectTokenResolutionStatus::ambiguous,
          "duplicated public token is rejected rather than selecting the first match");
}

void test_connector_spawn_distance_adjustment() {
    const auto adjustment = compute_bazooka_trail_seed_distance_adjustment(
        {3.0F, 4.0F, 12.0F}, {0.0F, 0.0F, 0.0F});
    check(adjustment.valid && adjustment.gap_world_units == 13.0F &&
              adjustment.first_spawn_distance == -13.0F,
          "connector length becomes the negative first spawn distance");

    const auto zero_gap = compute_bazooka_trail_seed_distance_adjustment(
        {1.0F, 2.0F, 3.0F}, {1.0F, 2.0F, 3.0F});
    check(zero_gap.valid && zero_gap.gap_world_units == 0.0F &&
              zero_gap.first_spawn_distance == 0.0F,
          "coincident authored and muzzle origins remain distance zero");

    const auto boundary = compute_bazooka_trail_seed_distance_adjustment(
        {kBazookaTrailSeedMaximumGapWorldUnits, 0.0F, 0.0F},
        {0.0F, 0.0F, 0.0F});
    check(boundary.valid &&
              boundary.gap_world_units ==
                  kBazookaTrailSeedMaximumGapWorldUnits,
          "maximum sane connector gap is inclusive");

    const auto too_far = compute_bazooka_trail_seed_distance_adjustment(
        {kBazookaTrailSeedMaximumGapWorldUnits + 1.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 0.0F});
    check(!too_far.valid,
          "connector beyond the sane-gap guard is rejected");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    check(!compute_bazooka_trail_seed_distance_adjustment(
               {nan, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F})
               .valid &&
              !compute_bazooka_trail_seed_distance_adjustment(
                   {0.0F, 0.0F, 0.0F}, {infinity, 0.0F, 0.0F})
                   .valid,
          "non-finite authored or muzzle coordinates are rejected");
}

}  // namespace

int main() {
    test_exact_match_and_freshness_boundary();
    test_scope_gates();
    test_definition_and_spawn_gates();
    test_muzzle_and_fx_handle_validation();
    test_fx_effect_public_token_resolution();
    test_connector_spawn_distance_adjustment();
    return 0;
}
