// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "controller_state.hpp"

#include "t4/bindings.hpp"
#include "xr_types.h"

#include <array>
#include <cstdint>

namespace wawvr::mod {

enum class ManualReloadRuntimeStatus : std::uint8_t {
    installed,
    already_installed,
    not_applicable,
    dependency_unavailable,
    rejected_wrong_profile,
    address_out_of_range,
    fingerprint_mismatch,
    thread_suspend_failed,
    patch_failed,
};

enum class HeadsetSvt40SelectionState : std::uint8_t {
    unknown,
    other_weapon,
    svt40_selected,
};

enum class HeadsetM1GarandGlSelectionState : std::uint8_t {
    unknown,
    other_weapon,
    m1garand_gl_selected,
    m7_launcher_selected,
};

struct ManualReloadRuntimeInstallResult final {
    ManualReloadRuntimeStatus status{
        ManualReloadRuntimeStatus::dependency_unavailable};
    std::uint32_t system_error{};

    [[nodiscard]] bool ok() const noexcept {
        return status == ManualReloadRuntimeStatus::installed ||
               status == ManualReloadRuntimeStatus::already_installed ||
               status == ManualReloadRuntimeStatus::not_applicable;
    }
};

struct ManualReloadViewmodelContext final {
    ControllerFrameSnapshot controller{};
    std::int32_t weapon_index{};
    // The weapon bridge already validates this exact map-local WeaponDef
    // before selecting grip behavior. Reuse that same-frame witness so the
    // reload runtime does not repeat the expensive definition scan in steady
    // detachable-magazine Ready idle.
    std::uint32_t weapon_registered_count{};
    std::uint32_t weapon_definition_address{};
    std::array<char, 64> weapon_internal_name{};
    bool weapon_definition_valid{};
    wawvr::xr::Vec3f camera_origin{};
    wawvr::xr::Basis3f camera_axis{};
    wawvr::xr::Basis3f weapon_axis{};
    wawvr::xr::Vec3f rifle_grip_world{};
    wawvr::xr::Vec3f muzzle_world{};
    bool muzzle_valid{};
    bool right_rifle_gripped{};
    bool left_rifle_gripped{};
    // Read-only SP playerState snapshot used to prove detachable-magazine
    // reserve/clip eligibility before taking native reload ownership.
    const void* player_state{};
    std::int32_t player_command_time{-1};
    bool player_command_time_valid{};
};

[[nodiscard]] ManualReloadRuntimeInstallResult install_manual_reload_runtime(
    const wawvr::t4::ValidatedBindings& bindings) noexcept;

// Resolves an already-interned exact-build script string through the same
// fingerprinted SL_FindString seam used by the manual reload runtime. This is
// shared with rifle-optic anchoring so model tags are never guessed by RVA.
[[nodiscard]] bool resolve_t4_script_string(
    const char* name,
    std::uint16_t* value) noexcept;

// Called from the validated post-Com_Frame main-thread boundary. Normal
// launches return immediately; an exact simulator map-handoff request may use
// this window to inventory one already-loaded weapon asset without requiring a
// rendered player viewmodel.
void service_manual_reload_simulator_asset_inventory() noexcept;

// Publishes the latest exact rendered viewmodel identity across the render/
// post-Com_Frame boundary. The disposable physical SVT trajectory preset uses
// this to stop cycling only after the actual held weapon is `svt40`.
[[nodiscard]] HeadsetSvt40SelectionState
headset_svt40_selection_state() noexcept;
void observe_headset_svt40_weapon_identity(
    const std::array<char, 64>& internal_name,
    bool identity_valid) noexcept;
[[nodiscard]] HeadsetM1GarandGlSelectionState
headset_m1garand_gl_selection_state() noexcept;
void observe_headset_m1garand_gl_weapon_identity(
    const std::array<char, 64>& internal_name,
    bool identity_valid) noexcept;

// Called after the normal viewmodel pose and right-grip/tag_flash correction.
// It owns only exact profiled bolt/clip or detachable-magazine geometry and
// the corresponding manual gesture. At the renderer's rigid-surface
// consumption seam, bolt rifles receive a private snapshot containing the
// cached manual pose without advancing interaction state twice or changing
// the native weapon animation tree. Detachable magazines remain an isolated
// viewmodel-submesh path.
void update_manual_reload_viewmodel(
    void* viewmodel_dobj,
    const void* viewmodel_pose,
    const ManualReloadViewmodelContext& context) noexcept;

// The command hook adds the native reload bit while a physical commit is
// waiting to reach the local simulation. The PM bridge suppresses A for the
// supported weapon and transfers ammo only after the tracked feed device
// reaches its exact receiver. Detachable magazines retain the single physical
// commit briefly so prediction and authoritative simulation each consume it.
[[nodiscard]] bool manual_reload_force_reload_button(
    std::int32_t weapon_index) noexcept;
[[nodiscard]] bool manual_reload_blocks_attack(
    std::int32_t weapon_index) noexcept;
[[nodiscard]] bool manual_reload_reserves_right_grip(
    std::int32_t weapon_index) noexcept;
[[nodiscard]] bool manual_reload_reserves_left_grip(
    std::int32_t weapon_index) noexcept;
// True only while the free left hand is already positioned to manipulate the
// active weapon action, or has latched that action. The grenade bridge uses
// this to reject a new left-trigger grenade press without affecting release
// of a grenade that was already held.
[[nodiscard]] bool manual_reload_blocks_new_left_trigger_action(
    std::int32_t weapon_index) noexcept;

struct ManualReloadGameplayPolicy final {
    bool force_reload_button{};
    bool blocks_attack{};
    bool reserves_right_grip{};
    bool reserves_left_grip{};
    bool blocks_new_left_trigger_action{};
};

// Fresh, synchronous policy read for one input/grip decision. The policies
// share one owned WeaponDef identity only while this function executes; all
// publication, session and chamber-lock checks still read live atomics. Use
// these flags immediately, never across native engine calls or later frames.
[[nodiscard]] ManualReloadGameplayPolicy read_manual_reload_gameplay_policy(
    std::int32_t weapon_index) noexcept;

// Called only from the verified local ordinary-bullet path. The lock is
// published immediately so a second command cannot fire before the render
// thread has consumed the shot and entered the physical bolt state.
void manual_reload_notify_local_shot(
    std::int32_t weapon_index,
    std::uint32_t weapon_definition_address) noexcept;

// Present observes this exact-profile word even while no viewmodel exists.
// Leaving the active SP connection epoch retires persistent chamber state so
// map restart/load cannot resurrect it on a fresh inventory.
void manual_reload_observe_connection_state(
    bool state_valid,
    std::int32_t connection_state,
    std::int32_t active_connection_state) noexcept;

void request_manual_reload_shutdown() noexcept;
// Normal exported shutdown may wait for the render-state mutex so an active
// detachable magazine is restored before input hooks are disabled. DllMain
// must continue using the loader-lock-safe atomic-only function above.
void request_manual_reload_orderly_shutdown() noexcept;

[[nodiscard]] const char* manual_reload_runtime_status_name(
    ManualReloadRuntimeStatus status) noexcept;

}  // namespace wawvr::mod
