// SPDX-License-Identifier: GPL-3.0-only
#include "bazooka_trail_hook.hpp"

#include "bazooka_trail_seed_logic.hpp"
#include "peer_thread_quiescence.hpp"
#include "stereo_diagnostics.hpp"
#include "t4_layout_selector.hpp"

#include "t4/hook_api.hpp"
#include "t4/profile.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

namespace wawvr::mod {
namespace {

constexpr std::size_t kCallInstructionSize = 5;
constexpr std::size_t kGEntityNumberOffset = 0x00;
constexpr std::size_t kWeaponDefinitionProjectileTrailEffectOffset = 0x7BC;
constexpr std::size_t kFxSystemEffectsOffset = 0x170;
constexpr std::size_t kFxSystemTrailsOffset = 0x178;
constexpr std::size_t kFxSystemTrailElemsOffset = 0x17C;
constexpr std::size_t kFxSystemExtent = 0xA60;
constexpr std::size_t kFxTrailSize = 0x08;
constexpr std::size_t kFxTrailCount = 128;
constexpr std::size_t kFxTrailElemSize = 0x20;
constexpr std::size_t kFxTrailElemCount = 2048;
constexpr std::size_t kFxPoolHandleScale = 4;
constexpr std::uint32_t kFxDObjHandleMask = 0xFFF;

struct FxEffectPrefix final {
    std::uint32_t definition{};
    std::int32_t status{};
    std::uint16_t first_elem_handle[3]{};
    std::uint16_t first_sorted_elem_handle{};
    std::uint16_t first_trail_handle{};
    std::uint16_t random_seed{};
    std::uint16_t owner{};
    std::uint16_t packed_lighting{};
    std::uint32_t packed_bolt_and_sort_order{};
};
static_assert(sizeof(FxEffectPrefix) == 0x1C);
static_assert(offsetof(FxEffectPrefix, first_trail_handle) == 0x10);
static_assert(
    offsetof(FxEffectPrefix, packed_bolt_and_sort_order) == 0x18);

struct FxTrailLayout final {
    std::uint16_t next_trail_handle{};
    std::uint16_t first_elem_handle{};
    std::uint16_t last_elem_handle{};
    std::uint8_t definition_index{};
    std::uint8_t sequence{};
};
static_assert(sizeof(FxTrailLayout) == kFxTrailSize);
static_assert(offsetof(FxTrailLayout, first_elem_handle) == 0x02);

struct FxTrailElemLayout final {
    std::array<float, 3> origin{};
    float spawn_distance{};
    std::int32_t msec_begin{};
    std::uint16_t next_trail_elem_handle{};
    std::int16_t base_velocity_z{};
    std::uint8_t basis[2][3]{};
    std::uint8_t sequence{};
    std::uint8_t unused{};
};
static_assert(sizeof(FxTrailElemLayout) == kFxTrailElemSize);
static_assert(offsetof(FxTrailElemLayout, origin) == 0x00);
static_assert(offsetof(FxTrailElemLayout, spawn_distance) == 0x0C);

struct FxSystemPoolPrefix final {
    std::array<std::uint8_t, kFxSystemEffectsOffset> opaque{};
    std::uint32_t effects{};
    std::uint32_t elems{};
    std::uint32_t trails{};
    std::uint32_t trail_elems{};
};
static_assert(offsetof(FxSystemPoolPrefix, effects) == kFxSystemEffectsOffset);
static_assert(offsetof(FxSystemPoolPrefix, trails) == kFxSystemTrailsOffset);
static_assert(
    offsetof(FxSystemPoolPrefix, trail_elems) == kFxSystemTrailElemsOffset);

std::atomic<bool> g_installed{false};
std::atomic<bool> g_service_enabled{false};
std::uintptr_t g_callsite = 0;
std::uintptr_t g_fx_system_address = 0;
std::array<std::uint8_t, kCallInstructionSize> g_original_call{};
std::array<std::uint8_t, kCallInstructionSize> g_replacement_call{};
SRWLOCK g_seed_lock = SRWLOCK_INIT;
std::array<BazookaTrailSeedToken, kBazookaTrailEntityCount> g_seed_tokens{};

[[nodiscard]] bool accessible_range(
    const void* address,
    std::size_t size,
    bool write) noexcept;

class ScopedFxEffectLock final {
public:
    ScopedFxEffectLock() = default;
    ScopedFxEffectLock(const ScopedFxEffectLock&) = delete;
    ScopedFxEffectLock& operator=(const ScopedFxEffectLock&) = delete;

    ~ScopedFxEffectLock() {
        if (status_ != nullptr) {
            InterlockedExchangeAdd(
                status_, -kFxEffectCooperativeLockBit);
        }
    }

    [[nodiscard]] bool acquire(
        const std::uintptr_t effect_address) noexcept {
        if (status_ != nullptr ||
            effect_address >
                (std::numeric_limits<std::uintptr_t>::max)() -
                    offsetof(FxEffectPrefix, status)) {
            return false;
        }
        void* const status_address = reinterpret_cast<void*>(
            effect_address + offsetof(FxEffectPrefix, status));
        if (!accessible_range(status_address, sizeof(LONG), true)) {
            return false;
        }
        auto* const status =
            static_cast<volatile LONG*>(status_address);
        for (;;) {
            const LONG previous = InterlockedExchangeAdd(
                status, kFxEffectCooperativeLockBit);
            if (fx_effect_lock_attempt_succeeded(previous)) {
                status_ = status;
                return true;
            }
            InterlockedExchangeAdd(
                status, -kFxEffectCooperativeLockBit);
        }
    }

private:
    volatile LONG* status_{};
};

[[nodiscard]] bool protection_allows_access(
    const DWORD protection, const bool write) noexcept {
    const DWORD access = protection & 0xFFU;
    if (write) {
        return access == PAGE_READWRITE || access == PAGE_WRITECOPY ||
               access == PAGE_EXECUTE_READWRITE ||
               access == PAGE_EXECUTE_WRITECOPY;
    }
    return access == PAGE_READONLY || access == PAGE_READWRITE ||
           access == PAGE_WRITECOPY || access == PAGE_EXECUTE_READ ||
           access == PAGE_EXECUTE_READWRITE ||
           access == PAGE_EXECUTE_WRITECOPY;
}

[[nodiscard]] bool accessible_range(
    const void* const address,
    const std::size_t size,
    const bool write) noexcept {
    if (address == nullptr || size == 0) {
        return false;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    if (begin > (std::numeric_limits<std::uintptr_t>::max)() - size) {
        return false;
    }
    const std::uintptr_t end = begin + size;
    std::uintptr_t cursor = begin;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(
                reinterpret_cast<const void*>(cursor), &memory,
                sizeof(memory)) != sizeof(memory) ||
            memory.State != MEM_COMMIT ||
            (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
            !protection_allows_access(memory.Protect, write)) {
            return false;
        }
        const auto region_begin =
            reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
        if (memory.RegionSize == 0 ||
            region_begin >
                (std::numeric_limits<std::uintptr_t>::max)() -
                    memory.RegionSize) {
            return false;
        }
        const std::uintptr_t region_end = region_begin + memory.RegionSize;
        if (cursor < region_begin || cursor >= region_end) {
            return false;
        }
        cursor = std::min(end, region_end);
    }
    return true;
}

template <typename T>
[[nodiscard]] bool read_value(
    const std::uintptr_t address, T* const value) noexcept {
    if (value == nullptr ||
        !accessible_range(reinterpret_cast<const void*>(address), sizeof(T),
                          false)) {
        return false;
    }
    std::memcpy(value, reinterpret_cast<const void*>(address), sizeof(T));
    return true;
}

[[nodiscard]] bool make_relative_call(
    const std::uintptr_t source,
    const std::uintptr_t destination,
    std::array<std::uint8_t, kCallInstructionSize>* const output) noexcept {
    if (output == nullptr) {
        return false;
    }
    const std::int64_t displacement =
        static_cast<std::int64_t>(destination) -
        static_cast<std::int64_t>(source + kCallInstructionSize);
    if (displacement < (std::numeric_limits<std::int32_t>::min)() ||
        displacement > (std::numeric_limits<std::int32_t>::max)()) {
        return false;
    }
    (*output)[0] = 0xE8;
    const auto encoded = static_cast<std::int32_t>(displacement);
    std::memcpy(output->data() + 1, &encoded, sizeof(encoded));
    return true;
}

[[nodiscard]] std::uintptr_t decode_relative_call_target(
    const std::uintptr_t source,
    const std::array<std::uint8_t, kCallInstructionSize>& bytes) noexcept {
    std::int32_t displacement = 0;
    std::memcpy(&displacement, bytes.data() + 1, sizeof(displacement));
    return static_cast<std::uintptr_t>(
        static_cast<std::int64_t>(source + kCallInstructionSize) +
        displacement);
}

enum class PatchResult : std::uint8_t {
    ok,
    thread_suspend_failed,
    expected_bytes_changed,
    target_protection_failed,
    patch_write_failed,
    patch_cache_flush_failed,
    protection_restore_failed,
};

[[nodiscard]] bool bytes_match(
    const void* const address,
    const std::span<const std::uint8_t> expected) noexcept {
    return accessible_range(address, expected.size(), false) &&
           std::memcmp(address, expected.data(), expected.size()) == 0;
}

[[nodiscard]] PatchResult replace_call_bytes(
    const std::uintptr_t target_address,
    const std::array<std::uint8_t, kCallInstructionSize>& expected,
    const std::array<std::uint8_t, kCallInstructionSize>& replacement,
    DWORD* const system_error) noexcept {
    SuspendedPeerThreads suspended;
    PeerThreadQuiesceResult quiesce{};
    if (!suspended.suspend(
            {target_address, kCallInstructionSize}, &quiesce)) {
        if (system_error != nullptr) {
            *system_error = quiesce.system_error;
        }
        return PatchResult::thread_suspend_failed;
    }

    auto* const target = reinterpret_cast<std::uint8_t*>(target_address);
    if (!bytes_match(target, expected)) {
        return PatchResult::expected_bytes_changed;
    }
    DWORD old_protection = 0;
    if (!VirtualProtect(
            target, replacement.size(), PAGE_EXECUTE_READWRITE,
            &old_protection)) {
        if (system_error != nullptr) {
            *system_error = GetLastError();
        }
        return PatchResult::target_protection_failed;
    }
    if (std::memcmp(target, expected.data(), expected.size()) != 0) {
        DWORD ignored = 0;
        VirtualProtect(target, replacement.size(), old_protection, &ignored);
        return PatchResult::expected_bytes_changed;
    }
    std::memcpy(target, replacement.data(), replacement.size());
    if (std::memcmp(target, replacement.data(), replacement.size()) != 0) {
        std::memcpy(target, expected.data(), expected.size());
        FlushInstructionCache(GetCurrentProcess(), target, expected.size());
        DWORD ignored = 0;
        VirtualProtect(target, replacement.size(), old_protection, &ignored);
        return PatchResult::patch_write_failed;
    }
    if (!FlushInstructionCache(
            GetCurrentProcess(), target, replacement.size())) {
        if (system_error != nullptr) {
            *system_error = GetLastError();
        }
        std::memcpy(target, expected.data(), expected.size());
        FlushInstructionCache(GetCurrentProcess(), target, expected.size());
        DWORD ignored = 0;
        VirtualProtect(target, replacement.size(), old_protection, &ignored);
        return PatchResult::patch_cache_flush_failed;
    }
    DWORD ignored = 0;
    if (!VirtualProtect(
            target, replacement.size(), old_protection, &ignored)) {
        if (system_error != nullptr) {
            *system_error = GetLastError();
        }
        std::memcpy(target, expected.data(), expected.size());
        FlushInstructionCache(GetCurrentProcess(), target, expected.size());
        VirtualProtect(target, replacement.size(), old_protection, &ignored);
        return PatchResult::protection_restore_failed;
    }
    return PatchResult::ok;
}

[[nodiscard]] BazookaTrailHookStatus status_from_patch(
    const PatchResult patch) noexcept {
    switch (patch) {
    case PatchResult::ok: return BazookaTrailHookStatus::installed;
    case PatchResult::thread_suspend_failed:
        return BazookaTrailHookStatus::thread_suspend_failed;
    case PatchResult::expected_bytes_changed:
        return BazookaTrailHookStatus::expected_bytes_changed;
    case PatchResult::target_protection_failed:
        return BazookaTrailHookStatus::target_protection_failed;
    case PatchResult::patch_write_failed:
        return BazookaTrailHookStatus::patch_write_failed;
    case PatchResult::patch_cache_flush_failed:
        return BazookaTrailHookStatus::patch_cache_flush_failed;
    case PatchResult::protection_restore_failed:
        return BazookaTrailHookStatus::protection_restore_failed;
    }
    return BazookaTrailHookStatus::patch_write_failed;
}

enum class RuntimeSeedStatus : std::uint8_t {
    applied,
    fx_system_unavailable,
    effect_pool_geometry_invalid,
    effect_pool_unreadable,
    effect_token_unresolved,
    effect_token_ambiguous,
    effect_token_changed,
    effect_unreadable,
    effect_lock_unavailable,
    effect_inactive,
    effect_definition_changed,
    effect_bolt_mismatch,
    trail_pool_unavailable,
    trail_handle_invalid,
    trail_unreadable,
    trail_element_pool_unavailable,
    trail_element_handle_invalid,
    trail_element_unwritable,
    trail_element_not_initial,
    trail_element_chain_invalid,
    trail_gap_invalid,
    trail_element_write_failed,
};

[[nodiscard]] const char* runtime_seed_status_name(
    const RuntimeSeedStatus status) noexcept {
    switch (status) {
    case RuntimeSeedStatus::applied: return "applied";
    case RuntimeSeedStatus::fx_system_unavailable:
        return "fx-system-unavailable";
    case RuntimeSeedStatus::effect_pool_geometry_invalid:
        return "effect-pool-geometry-invalid";
    case RuntimeSeedStatus::effect_pool_unreadable:
        return "effect-pool-unreadable";
    case RuntimeSeedStatus::effect_token_unresolved:
        return "effect-token-unresolved";
    case RuntimeSeedStatus::effect_token_ambiguous:
        return "effect-token-ambiguous";
    case RuntimeSeedStatus::effect_token_changed:
        return "effect-token-changed";
    case RuntimeSeedStatus::effect_unreadable: return "effect-unreadable";
    case RuntimeSeedStatus::effect_lock_unavailable:
        return "effect-lock-unavailable";
    case RuntimeSeedStatus::effect_inactive: return "effect-inactive";
    case RuntimeSeedStatus::effect_definition_changed:
        return "effect-definition-changed";
    case RuntimeSeedStatus::effect_bolt_mismatch: return "effect-bolt-mismatch";
    case RuntimeSeedStatus::trail_pool_unavailable:
        return "trail-pool-unavailable";
    case RuntimeSeedStatus::trail_handle_invalid:
        return "trail-handle-invalid";
    case RuntimeSeedStatus::trail_unreadable: return "trail-unreadable";
    case RuntimeSeedStatus::trail_element_pool_unavailable:
        return "trail-element-pool-unavailable";
    case RuntimeSeedStatus::trail_element_handle_invalid:
        return "trail-element-handle-invalid";
    case RuntimeSeedStatus::trail_element_unwritable:
        return "trail-element-unwritable";
    case RuntimeSeedStatus::trail_element_not_initial:
        return "trail-element-not-initial";
    case RuntimeSeedStatus::trail_element_chain_invalid:
        return "trail-element-chain-invalid";
    case RuntimeSeedStatus::trail_gap_invalid:
        return "trail-gap-invalid";
    case RuntimeSeedStatus::trail_element_write_failed:
        return "trail-element-write-failed";
    }
    return "unknown";
}

struct RuntimeSeedResult final {
    RuntimeSeedStatus status{RuntimeSeedStatus::fx_system_unavailable};
    std::uintptr_t resolved_effect_address{};
    std::uint16_t trail_handle{kFxHandleNone};
    std::uint16_t trail_element_handle{kFxHandleNone};
    std::array<float, 3> previous_origin{};
    float previous_spawn_distance{};
    float seeded_spawn_distance{};
    float connector_gap_world_units{};
};

[[nodiscard]] RuntimeSeedResult seed_first_trail_element(
    const std::uint32_t effect_token,
    const std::uintptr_t expected_effect_definition,
    const std::uint32_t dobj_handle,
    const std::array<float, 3>& muzzle_origin) noexcept {
    RuntimeSeedResult result{};
    FxSystemPoolPrefix system{};
    if (g_fx_system_address == 0 ||
        !accessible_range(
            reinterpret_cast<const void*>(g_fx_system_address),
            sizeof(system), false)) {
        return result;
    }
    std::memcpy(
        &system, reinterpret_cast<const void*>(g_fx_system_address),
        sizeof(system));
    const std::uintptr_t effects = system.effects;
    const std::uintptr_t elems = system.elems;
    if (!valid_retail_fx_effect_pool_geometry(effects, elems)) {
        result.status = RuntimeSeedStatus::effect_pool_geometry_invalid;
        return result;
    }
    const auto* const effect_pool_bytes =
        reinterpret_cast<const std::byte*>(effects);
    if (!accessible_range(
            effect_pool_bytes, kRetailFxEffectPoolSize, false)) {
        result.status = RuntimeSeedStatus::effect_pool_unreadable;
        return result;
    }
    const std::span<const std::byte> effect_pool{
        effect_pool_bytes, kRetailFxEffectPoolSize};
    const auto resolution =
        resolve_retail_fx_effect_token(effect_pool, effect_token);
    if (resolution.status == FxEffectTokenResolutionStatus::ambiguous) {
        result.status = RuntimeSeedStatus::effect_token_ambiguous;
        return result;
    }
    if (!resolution.resolved()) {
        result.status = RuntimeSeedStatus::effect_token_unresolved;
        return result;
    }
    const std::uintptr_t effect =
        effects + resolution.index * kRetailFxEffectStride;
    result.resolved_effect_address = effect;
    if (!accessible_range(
            reinterpret_cast<const void*>(effect), kRetailFxEffectStride,
            false)) {
        result.status = RuntimeSeedStatus::effect_unreadable;
        return result;
    }
    ScopedFxEffectLock effect_lock;
    if (!effect_lock.acquire(effect)) {
        result.status = RuntimeSeedStatus::effect_lock_unavailable;
        return result;
    }
    FxEffectPrefix effect_prefix{};
    std::uint32_t locked_public_token = 0;
    std::memcpy(
        &locked_public_token,
        reinterpret_cast<const void*>(
            effect + kRetailFxEffectPublicTokenOffset),
        sizeof(locked_public_token));
    std::memcpy(
        &effect_prefix, reinterpret_cast<const void*>(effect),
        sizeof(effect_prefix));
    if (locked_public_token != effect_token) {
        result.status = RuntimeSeedStatus::effect_token_changed;
        return result;
    }
    const auto locked_resolution =
        resolve_retail_fx_effect_token(effect_pool, effect_token);
    if (locked_resolution.status ==
        FxEffectTokenResolutionStatus::ambiguous) {
        result.status = RuntimeSeedStatus::effect_token_ambiguous;
        return result;
    }
    if (!locked_resolution.resolved() ||
        locked_resolution.index != resolution.index) {
        result.status = RuntimeSeedStatus::effect_token_changed;
        return result;
    }
    if ((effect_prefix.status & 0xFFFF) == 0) {
        result.status = RuntimeSeedStatus::effect_inactive;
        return result;
    }
    if (effect_prefix.definition != expected_effect_definition) {
        result.status = RuntimeSeedStatus::effect_definition_changed;
        return result;
    }
    if ((effect_prefix.packed_bolt_and_sort_order & kFxDObjHandleMask) !=
        dobj_handle) {
        result.status = RuntimeSeedStatus::effect_bolt_mismatch;
        return result;
    }
    if (system.trails == 0) {
        result.status = RuntimeSeedStatus::trail_pool_unavailable;
        return result;
    }
    result.trail_handle = effect_prefix.first_trail_handle;
    if (!valid_fx_pool_handle(
            result.trail_handle, kFxTrailSize, kFxPoolHandleScale,
            kFxTrailCount)) {
        result.status = RuntimeSeedStatus::trail_handle_invalid;
        return result;
    }
    const std::uintptr_t trail_address =
        static_cast<std::uintptr_t>(system.trails) +
        static_cast<std::uintptr_t>(result.trail_handle) *
            kFxPoolHandleScale;
    FxTrailLayout trail{};
    if (!accessible_range(
            reinterpret_cast<const void*>(trail_address), sizeof(trail),
            false)) {
        result.status = RuntimeSeedStatus::trail_unreadable;
        return result;
    }
    std::memcpy(&trail, reinterpret_cast<const void*>(trail_address),
                sizeof(trail));
    if (system.trail_elems == 0) {
        result.status = RuntimeSeedStatus::trail_element_pool_unavailable;
        return result;
    }
    result.trail_element_handle = trail.first_elem_handle;
    if (!valid_fx_pool_handle(
            result.trail_element_handle, kFxTrailElemSize,
            kFxPoolHandleScale, kFxTrailElemCount)) {
        result.status = RuntimeSeedStatus::trail_element_handle_invalid;
        return result;
    }
    const std::uintptr_t trail_element_address =
        static_cast<std::uintptr_t>(system.trail_elems) +
        static_cast<std::uintptr_t>(result.trail_element_handle) *
            kFxPoolHandleScale;
    auto* const trail_element =
        reinterpret_cast<FxTrailElemLayout*>(trail_element_address);
    if (!accessible_range(trail_element, sizeof(*trail_element), true)) {
        result.status = RuntimeSeedStatus::trail_element_unwritable;
        return result;
    }
    std::memcpy(
        result.previous_origin.data(), trail_element->origin.data(),
        sizeof(result.previous_origin));
    result.previous_spawn_distance = trail_element->spawn_distance;
    if (!is_initial_fx_trail_element(
            trail_element->spawn_distance, trail_element->sequence)) {
        result.status = RuntimeSeedStatus::trail_element_not_initial;
        return result;
    }
    if (trail.first_elem_handle == trail.last_elem_handle ||
        !valid_fx_pool_handle(
            trail_element->next_trail_elem_handle, kFxTrailElemSize,
            kFxPoolHandleScale, kFxTrailElemCount)) {
        result.status = RuntimeSeedStatus::trail_element_chain_invalid;
        return result;
    }
    const std::uintptr_t next_trail_element_address =
        static_cast<std::uintptr_t>(system.trail_elems) +
        static_cast<std::uintptr_t>(
            trail_element->next_trail_elem_handle) *
            kFxPoolHandleScale;
    FxTrailElemLayout next_trail_element{};
    if (!accessible_range(
            reinterpret_cast<const void*>(next_trail_element_address),
            sizeof(next_trail_element), false)) {
        result.status = RuntimeSeedStatus::trail_element_chain_invalid;
        return result;
    }
    std::memcpy(
        &next_trail_element,
        reinterpret_cast<const void*>(next_trail_element_address),
        sizeof(next_trail_element));
    if (next_trail_element.sequence != 1 ||
        !std::isfinite(next_trail_element.spawn_distance)) {
        result.status = RuntimeSeedStatus::trail_element_chain_invalid;
        return result;
    }

    const auto distance_adjustment =
        compute_bazooka_trail_seed_distance_adjustment(
            result.previous_origin, muzzle_origin);
    if (!distance_adjustment.valid) {
        result.status = RuntimeSeedStatus::trail_gap_invalid;
        return result;
    }
    result.seeded_spawn_distance =
        distance_adjustment.first_spawn_distance;
    result.connector_gap_world_units =
        distance_adjustment.gap_world_units;

    struct TrailSeedPrefix final {
        std::array<float, 3> origin{};
        float spawn_distance{};
    };
    static_assert(sizeof(TrailSeedPrefix) == 0x10);
    TrailSeedPrefix previous_prefix{};
    std::memcpy(&previous_prefix, trail_element, sizeof(previous_prefix));
    TrailSeedPrefix seeded_prefix{
        muzzle_origin, distance_adjustment.first_spawn_distance};

    // FX_DrawTrail maps each point's spawnDist directly to longitudinal U.
    // Moving only the first point therefore also moves its distance backward
    // by the geometric connector length. Native time, sequence, velocity,
    // basis, and both effect spatial frames remain untouched, and later trail
    // points continue to follow the stock missile.
    std::memcpy(trail_element, &seeded_prefix, sizeof(seeded_prefix));
    TrailSeedPrefix verified_prefix{};
    std::memcpy(&verified_prefix, trail_element, sizeof(verified_prefix));
    if (std::memcmp(
            &verified_prefix, &seeded_prefix,
            sizeof(verified_prefix)) != 0) {
        std::memcpy(trail_element, &previous_prefix, sizeof(previous_prefix));
        result.status = RuntimeSeedStatus::trail_element_write_failed;
        return result;
    }
    result.status = RuntimeSeedStatus::applied;
    return result;
}

[[nodiscard]] const char* decision_name(
    const BazookaTrailSeedDecision decision) noexcept {
    switch (decision) {
    case BazookaTrailSeedDecision::apply: return "apply";
    case BazookaTrailSeedDecision::hook_disabled: return "hook-disabled";
    case BazookaTrailSeedDecision::wrong_local_client:
        return "wrong-local-client";
    case BazookaTrailSeedDecision::entity_out_of_range:
        return "entity-out-of-range";
    case BazookaTrailSeedDecision::no_pending_seed: return "no-pending-seed";
    case BazookaTrailSeedDecision::effect_definition_mismatch:
        return "effect-definition-mismatch";
    case BazookaTrailSeedDecision::clock_reversed: return "clock-reversed";
    case BazookaTrailSeedDecision::stale: return "stale";
    case BazookaTrailSeedDecision::effect_spawn_failed:
        return "effect-spawn-failed";
    }
    return "unknown";
}

}  // namespace

// FX_SpawnBoltedEffect is a recovered retail register/stack ABI: boneIndex in
// EAX, localClientNum in ECX, then FxEffectDef*, msecBegin, and dobjHandle on
// the stack. This immutable native target is published before replacing the
// sole CG_PlayBoltedEffect call and remains valid for the process lifetime.
extern "C" std::uintptr_t
    wawvr_bazooka_trail_original_fx_spawn_bolted_effect_address = 0;

extern "C" void __cdecl wawvr_bazooka_trail_seed_service(
    const std::uint32_t effect_token,
    const std::int32_t local_client_number,
    const void* const effect_definition,
    const std::int32_t msec_begin,
    const std::uint32_t dobj_handle,
    const std::uint32_t bone_index) noexcept {
    BazookaTrailSeedToken token{};
    BazookaTrailSeedDecision decision{};
    RuntimeSeedResult seeded{};
    AcquireSRWLockExclusive(&g_seed_lock);
    if (dobj_handle < g_seed_tokens.size()) {
        token = g_seed_tokens[dobj_handle];
    }
    decision = evaluate_bazooka_trail_seed(
        token, g_service_enabled.load(std::memory_order_acquire),
        local_client_number, dobj_handle,
        reinterpret_cast<std::uintptr_t>(effect_definition),
        effect_token != 0, GetTickCount64());
    if (dobj_handle < g_seed_tokens.size() &&
        bazooka_trail_seed_decision_consumes_token(decision)) {
        g_seed_tokens[dobj_handle] = {};
    }
    if (decision == BazookaTrailSeedDecision::apply) {
        // Keep the publication lock through the native effect lock/write so
        // shutdown's exclusive acquisition drains every in-flight seed.
        seeded = seed_first_trail_element(
            effect_token, token.effect_definition, dobj_handle,
            token.muzzle_origin);
    }
    ReleaseSRWLockExclusive(&g_seed_lock);

    if (decision != BazookaTrailSeedDecision::apply) {
        if (token.valid &&
            decision != BazookaTrailSeedDecision::hook_disabled &&
            decision != BazookaTrailSeedDecision::no_pending_seed) {
            static std::atomic<std::uint32_t> rejection_logs{0};
            const std::uint32_t log_index =
                rejection_logs.fetch_add(1, std::memory_order_relaxed);
            if (log_index < 24) {
                stereo_diagnostic_log(
                    "BazookaTrail seed rejected[%u]: reason=%s entity=%u localClient=%d spawnToken=0x%08X effectDef=%p expectedDef=%p spawned=%d",
                    log_index, decision_name(decision), dobj_handle,
                    local_client_number, effect_token, effect_definition,
                    reinterpret_cast<const void*>(token.effect_definition),
                    effect_token != 0 ? 1 : 0);
            }
        }
        return;
    }

    static std::atomic<std::uint32_t> seed_logs{0};
    const std::uint32_t log_index =
        seed_logs.fetch_add(1, std::memory_order_relaxed);
    if (log_index < 24) {
        stereo_diagnostic_log(
            "BazookaTrail first-element seed[%u]: status=%s entity=%u token=0x%08X raw=%p def=%p msec=%d bone=%u trail=0x%X elem=0x%X nativeSpawnDist=%.3f seedSpawnDist=%.3f gap=%.3f authored=%.2f %.2f %.2f muzzle=%.2f %.2f %.2f",
            log_index, runtime_seed_status_name(seeded.status), dobj_handle,
            effect_token,
            reinterpret_cast<const void*>(seeded.resolved_effect_address),
            effect_definition, msec_begin, bone_index,
            seeded.trail_handle, seeded.trail_element_handle,
            seeded.previous_spawn_distance,
            seeded.seeded_spawn_distance,
            seeded.connector_gap_world_units,
            seeded.previous_origin[0], seeded.previous_origin[1],
            seeded.previous_origin[2], token.muzzle_origin[0],
            token.muzzle_origin[1], token.muzzle_origin[2]);
    }
}

#if defined(_MSC_VER) && defined(_M_IX86)
extern "C" __declspec(naked) void
wawvr_bazooka_trail_fx_spawn_bridge() noexcept {
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 0x0C
        mov dword ptr [ebp - 0x04], eax
        mov dword ptr [ebp - 0x08], ecx

        push dword ptr [ebp + 0x10]
        push dword ptr [ebp + 0x0C]
        push dword ptr [ebp + 0x08]
        mov eax, dword ptr [ebp - 0x04]
        mov ecx, dword ptr [ebp - 0x08]
        call dword ptr [wawvr_bazooka_trail_original_fx_spawn_bolted_effect_address]
        add esp, 0x0C
        mov dword ptr [ebp - 0x0C], eax

        push dword ptr [ebp - 0x04]
        push dword ptr [ebp + 0x10]
        push dword ptr [ebp + 0x0C]
        push dword ptr [ebp + 0x08]
        push dword ptr [ebp - 0x08]
        push dword ptr [ebp - 0x0C]
        call wawvr_bazooka_trail_seed_service
        add esp, 0x18

        mov eax, dword ptr [ebp - 0x0C]
        mov esp, ebp
        pop ebp
        ret
    }
}
#endif

BazookaTrailHookInstallResult install_bazooka_trail_hook(
    const wawvr::t4::ValidatedBindings& bindings) noexcept {
    BazookaTrailHookInstallResult result{};
    if (g_installed.load(std::memory_order_acquire)) {
        result.status = BazookaTrailHookStatus::already_installed;
        result.target = g_callsite;
        result.original =
            wawvr_bazooka_trail_original_fx_spawn_bolted_effect_address;
        return result;
    }

    const T4LayoutFamily layout = select_t4_layout_family(bindings.profile());
    if (layout == T4LayoutFamily::multiplayer_1_7_1263) {
        result.status = BazookaTrailHookStatus::not_applicable;
        return result;
    }
    if (layout != T4LayoutFamily::single_player_1_7_1263) {
        result.status = BazookaTrailHookStatus::rejected_wrong_profile;
        return result;
    }

#if !defined(_MSC_VER) || !defined(_M_IX86)
    static_cast<void>(bindings);
    result.status =
        BazookaTrailHookStatus::unsupported_compiler_or_architecture;
    return result;
#else
    const auto prepared = wawvr::t4::prepare_inline_hook(
        bindings,
        wawvr::t4::HookSiteId::cg_play_bolted_effect_spawn_call,
        reinterpret_cast<std::uintptr_t>(
            &wawvr_bazooka_trail_fx_spawn_bridge));
    if (!prepared.ok() ||
        !bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::
                cg_missile_bazooka_trail_call_sentinel) ||
        !bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::
                cg_play_bolted_effect_spawn_context_sentinel) ||
        !bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::
                fx_spawn_bolted_effect_entry_sentinel) ||
        !bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::
                fx_spawn_bolted_effect_return_token_sentinel)) {
        result.status = BazookaTrailHookStatus::preparation_failed;
        return result;
    }
    result.target = prepared.hook->target;
    std::array<std::uint8_t, kCallInstructionSize> original_call{};
    if (prepared.hook->expected_size == original_call.size()) {
        std::copy_n(
            prepared.hook->expected.begin(), original_call.size(),
            original_call.begin());
    }
    const auto expected_original = bindings.site_address(
        wawvr::t4::HookSiteId::fx_spawn_bolted_effect_entry_sentinel);
    if (!expected_original.has_value() ||
        prepared.hook->expected_size != original_call.size() ||
        original_call[0] != 0xE8 ||
        decode_relative_call_target(prepared.hook->target, original_call) !=
            *expected_original) {
        result.status = BazookaTrailHookStatus::original_target_mismatch;
        return result;
    }
    result.original = *expected_original;
    const auto fx_system = bindings.data_address(
        wawvr::t4::DataSymbolId::fx_system, kFxSystemExtent);
    if (!fx_system.has_value()) {
        result.status = BazookaTrailHookStatus::preparation_failed;
        return result;
    }

    std::array<std::uint8_t, kCallInstructionSize> replacement{};
    if (!make_relative_call(
            prepared.hook->target,
            reinterpret_cast<std::uintptr_t>(
                &wawvr_bazooka_trail_fx_spawn_bridge),
            &replacement)) {
        result.status = BazookaTrailHookStatus::jump_out_of_range;
        return result;
    }

    wawvr_bazooka_trail_original_fx_spawn_bolted_effect_address =
        *expected_original;
    g_fx_system_address = *fx_system;
    g_original_call = original_call;
    DWORD system_error = 0;
    const PatchResult patch = replace_call_bytes(
        prepared.hook->target, original_call, replacement, &system_error);
    result.status = status_from_patch(patch);
    result.system_error = system_error;
    if (patch != PatchResult::ok) {
        wawvr_bazooka_trail_original_fx_spawn_bolted_effect_address = 0;
        g_fx_system_address = 0;
        g_original_call = {};
        return result;
    }

    g_callsite = prepared.hook->target;
    g_replacement_call = replacement;
    g_installed.store(true, std::memory_order_release);
    g_service_enabled.store(true, std::memory_order_release);
    return result;
#endif
}

bool publish_bazooka_trail_seed_from_rocket(
    const void* const rocket_entity,
    const std::uintptr_t weapon_definition,
    const wawvr::xr::Vec3f& muzzle_origin) noexcept {
    if (!g_service_enabled.load(std::memory_order_acquire) ||
        rocket_entity == nullptr || weapon_definition == 0) {
        return false;
    }
    const std::array<float, 3> muzzle{
        muzzle_origin.x, muzzle_origin.y, muzzle_origin.z};
    if (!finite_bazooka_trail_muzzle(muzzle) ||
        weapon_definition >
            (std::numeric_limits<std::uintptr_t>::max)() -
                kWeaponDefinitionProjectileTrailEffectOffset) {
        return false;
    }

    std::int32_t entity_number = -1;
    std::uint32_t effect_definition = 0;
    if (!read_value(
            reinterpret_cast<std::uintptr_t>(rocket_entity) +
                kGEntityNumberOffset,
            &entity_number) ||
        entity_number < 0 ||
        static_cast<std::uint32_t>(entity_number) >=
            kBazookaTrailEntityCount ||
        !read_value(
            weapon_definition +
                kWeaponDefinitionProjectileTrailEffectOffset,
            &effect_definition) ||
        effect_definition == 0 ||
        !accessible_range(
            reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(effect_definition)),
            sizeof(std::uint32_t), false)) {
        return false;
    }

    BazookaTrailSeedToken token{
        true,
        static_cast<std::uint32_t>(entity_number),
        static_cast<std::uintptr_t>(effect_definition),
        GetTickCount64(),
        muzzle,
    };
    AcquireSRWLockExclusive(&g_seed_lock);
    if (!g_service_enabled.load(std::memory_order_acquire)) {
        ReleaseSRWLockExclusive(&g_seed_lock);
        return false;
    }
    g_seed_tokens[token.entity_number] = token;
    ReleaseSRWLockExclusive(&g_seed_lock);

    static std::atomic<std::uint32_t> publication_logs{0};
    const std::uint32_t log_index =
        publication_logs.fetch_add(1, std::memory_order_relaxed);
    if (log_index < 24) {
        stereo_diagnostic_log(
            "BazookaTrail fire-time seed published[%u]: entity=%u def=%p muzzle=%.2f %.2f %.2f",
            log_index, token.entity_number,
            reinterpret_cast<const void*>(token.effect_definition),
            token.muzzle_origin[0], token.muzzle_origin[1],
            token.muzzle_origin[2]);
    }
    return true;
}

void request_bazooka_trail_hook_shutdown() noexcept {
    g_service_enabled.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&g_seed_lock);
    g_seed_tokens = {};
    ReleaseSRWLockExclusive(&g_seed_lock);
}

bool bazooka_trail_hook_installed() noexcept {
    return g_installed.load(std::memory_order_acquire);
}

bool bazooka_trail_hook_enabled() noexcept {
    return g_service_enabled.load(std::memory_order_acquire);
}

const char* bazooka_trail_hook_status_name(
    const BazookaTrailHookStatus status) noexcept {
    switch (status) {
    case BazookaTrailHookStatus::installed: return "installed";
    case BazookaTrailHookStatus::already_installed:
        return "already-installed";
    case BazookaTrailHookStatus::not_applicable: return "not-applicable";
    case BazookaTrailHookStatus::dependency_unavailable:
        return "dependency-unavailable";
    case BazookaTrailHookStatus::rejected_wrong_profile:
        return "rejected-wrong-profile";
    case BazookaTrailHookStatus::unsupported_compiler_or_architecture:
        return "unsupported-compiler-or-architecture";
    case BazookaTrailHookStatus::preparation_failed:
        return "preparation-failed";
    case BazookaTrailHookStatus::original_target_mismatch:
        return "original-target-mismatch";
    case BazookaTrailHookStatus::jump_out_of_range:
        return "jump-out-of-range";
    case BazookaTrailHookStatus::thread_suspend_failed:
        return "thread-suspend-failed";
    case BazookaTrailHookStatus::expected_bytes_changed:
        return "expected-bytes-changed";
    case BazookaTrailHookStatus::target_protection_failed:
        return "target-protection-failed";
    case BazookaTrailHookStatus::patch_write_failed:
        return "patch-write-failed";
    case BazookaTrailHookStatus::patch_cache_flush_failed:
        return "patch-cache-flush-failed";
    case BazookaTrailHookStatus::protection_restore_failed:
        return "protection-restore-failed";
    }
    return "unknown";
}

}  // namespace wawvr::mod
