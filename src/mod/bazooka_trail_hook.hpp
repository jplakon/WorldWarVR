// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "t4/bindings.hpp"
#include "xr_types.h"

#include <cstdint>

namespace wawvr::mod {

enum class BazookaTrailHookStatus : std::uint8_t {
    installed,
    already_installed,
    not_applicable,
    dependency_unavailable,
    rejected_wrong_profile,
    unsupported_compiler_or_architecture,
    preparation_failed,
    original_target_mismatch,
    jump_out_of_range,
    thread_suspend_failed,
    expected_bytes_changed,
    target_protection_failed,
    patch_write_failed,
    patch_cache_flush_failed,
    protection_restore_failed,
};

struct BazookaTrailHookInstallResult final {
    BazookaTrailHookStatus status{
        BazookaTrailHookStatus::preparation_failed};
    std::uintptr_t target{};
    std::uintptr_t original{};
    std::uint32_t system_error{};

    [[nodiscard]] bool ok() const noexcept {
        return status == BazookaTrailHookStatus::installed ||
               status == BazookaTrailHookStatus::already_installed ||
               status == BazookaTrailHookStatus::not_applicable;
    }
};

[[nodiscard]] BazookaTrailHookInstallResult install_bazooka_trail_hook(
    const wawvr::t4::ValidatedBindings& bindings) noexcept;

// Publishes the exact fire-time viewmodel muzzle under the returned missile's
// entity number. The client bridge consumes it only when that same DObj spawns
// the exact projectile-trail FxEffectDef from this WeaponDef.
[[nodiscard]] bool publish_bazooka_trail_seed_from_rocket(
    const void* rocket_entity,
    std::uintptr_t weapon_definition,
    const wawvr::xr::Vec3f& muzzle_origin) noexcept;

void request_bazooka_trail_hook_shutdown() noexcept;
[[nodiscard]] bool bazooka_trail_hook_installed() noexcept;
[[nodiscard]] bool bazooka_trail_hook_enabled() noexcept;
[[nodiscard]] const char* bazooka_trail_hook_status_name(
    BazookaTrailHookStatus status) noexcept;

}  // namespace wawvr::mod
