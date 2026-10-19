// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "xr_types.h"
#include "t4/bindings.hpp"

#include <array>
#include <cstdint>

namespace wawvr::mod {

// These are observations of the exact retail server path, never a replacement
// trace or a synthetic target. Predicted client hits remain a different source.
struct ServerImpactAuditIdentity final {
    bool accepted{};
    std::int32_t weapon_index{};
    std::array<char, 64> name{};
    std::uint64_t controller_generation{};
    float right_grip{};
    float left_grip{};
};
using ServerImpactIdentityReader = bool (*)(
    const void* attacker, const void* weapon_definition,
    ServerImpactAuditIdentity* output) noexcept;

enum class ServerImpactAuditHookStatus {
    installed, already_installed, disabled, unsupported,
    fingerprint_mismatch, thread_suspend_failed, patch_failed,
};

[[nodiscard]] ServerImpactAuditHookStatus install_server_impact_audit(
    const wawvr::t4::ValidatedBindings& bindings,
    ServerImpactIdentityReader identity_reader) noexcept;
// COLT_ZERO_DEFERRED_OBSERVER_STATUS_BEGIN
// Record the actual original installer result; flush only after the existing
// file logger is ready. These functions never install, retry, or enable hooks.
void record_server_impact_audit_install_status(
    ServerImpactAuditHookStatus status) noexcept;
void log_server_impact_audit_install_status_if_ready() noexcept;
// COLT_ZERO_DEFERRED_OBSERVER_STATUS_END
void request_server_impact_audit_shutdown() noexcept;
[[nodiscard]] bool restore_server_impact_audit() noexcept;

// Called from the already-validated local FireWeapon -> Bullet_Fire bridge.
// Exactly one observation is created per real native discharge. It never
// touches WeaponParms, spread, ammo, recoil, native trace inputs or outputs.
void begin_server_impact_audit_shot(
    const void* weapon_parms, const void* attacker) noexcept;

} // namespace wawvr::mod
