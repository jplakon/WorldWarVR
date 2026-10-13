// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "controller_state.hpp"

#include "t4/bindings.hpp"
#include "xr_types.h"

#include <cstdint>

namespace wawvr::mod {

enum class TrackedHandsRuntimeStatus : std::uint8_t {
    installed,
    already_installed,
    not_applicable,
    dependency_unavailable,
    rejected_wrong_profile,
    address_out_of_range,
    fingerprint_mismatch,
};

struct TrackedHandsRuntimeInstallResult final {
    TrackedHandsRuntimeStatus status{
        TrackedHandsRuntimeStatus::dependency_unavailable};

    [[nodiscard]] bool ok() const noexcept {
        return status == TrackedHandsRuntimeStatus::installed ||
               status == TrackedHandsRuntimeStatus::already_installed ||
               status == TrackedHandsRuntimeStatus::not_applicable;
    }
};

// Gripping hands use T4's native animated viewmodel pose. Verified pistols use
// a close support palm beside the authored firing hand instead of the forward
// rifle wrist. This is display-only; controller/weapon ownership is unchanged.
// Hands not gripping remain standalone controller-tracked gloves.
struct TrackedHandsWeaponState final {
    bool right_gripping{};
    bool left_gripping{};
    bool weapon_pose_valid{};
    wawvr::xr::Basis3f weapon_axis{};
    bool pistol_support_pose{};
    std::uint64_t pistol_weapon_identity{};
    bool pistol_bake_allowed{};
};

[[nodiscard]] TrackedHandsRuntimeInstallResult install_tracked_hands_runtime(
    const wawvr::t4::ValidatedBindings& bindings) noexcept;

// Builds one wrist-local glove for each hand from model zero of the stock
// first-person DObj, then submits both gloves at the exact controller grip
// poses used by the current stereo frame. The stock arms remain hidden.
void update_tracked_hands_viewmodel(
    void* viewmodel_dobj,
    const void* viewmodel_pose,
    const ControllerFrameSnapshot& controller,
    const wawvr::xr::Vec3f& camera_origin,
    const wawvr::xr::Basis3f& camera_axis,
    const TrackedHandsWeaponState& weapon_state = {}) noexcept;

// Submits one engine-owned XModel as a private, untracked first-person DObj.
// Manual grenades use this alongside the ordinary firearm and tracked glove;
// the model is never inserted into T4's client-DObj handle table.
[[nodiscard]] bool submit_tracked_first_person_model(
    void* xmodel,
    const void* viewmodel_pose,
    const wawvr::xr::EnginePose& world_pose,
    const wawvr::xr::Vec3f& lighting_origin) noexcept;

void request_tracked_hands_shutdown() noexcept;

[[nodiscard]] const char* tracked_hands_runtime_status_name(
    TrackedHandsRuntimeStatus status) noexcept;

}  // namespace wawvr::mod
