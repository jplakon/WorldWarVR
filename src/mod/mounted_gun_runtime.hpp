#pragma once

#include "controller_state.hpp"
#include "t4/bindings.hpp"

#include <windows.h>

#include <cstdint>

namespace wawvr::mod {

enum class MountedGunRuntimeStatus : std::uint8_t {
    not_attempted,
    installed,
    already_installed,
    not_applicable,
    dependency_unavailable,
    address_out_of_range,
    preparation_failed,
    unexpected_instruction_boundary,
    thread_suspend_failed,
    expected_bytes_changed,
    jump_out_of_range,
    protection_failed,
    patch_failed,
    rollback_failed,
};

struct MountedGunRuntimeInstallResult {
    MountedGunRuntimeStatus status{MountedGunRuntimeStatus::not_attempted};
    DWORD system_error{};
    std::uintptr_t server_call{};
    std::uintptr_t fire_call{};
    std::uintptr_t client_call{};

    [[nodiscard]] bool ok() const noexcept {
        return status == MountedGunRuntimeStatus::installed ||
            status == MountedGunRuntimeStatus::already_installed;
    }
};

[[nodiscard]] MountedGunRuntimeInstallResult install_mounted_gun_runtime(
    const wawvr::t4::ValidatedBindings& bindings) noexcept;

// Used by post-CL_CreateCmd input mapping to permit the mounted trigger even
// when the ordinary handheld weapon grip is intentionally not owned.
[[nodiscard]] bool mounted_gun_controller_route_available(
    const ControllerFrameSnapshot& snapshot,
    std::uint64_t now_milliseconds) noexcept;

// Publish the controller-world direction already resolved against T4's stock
// body basis by the input hook. The mounted server and client hooks both read
// this publication, guaranteeing that model and bullets share one ray.
void publish_mounted_gun_controller_aim(
    float pitch_degrees,
    float yaw_degrees,
    std::uint64_t controller_generation,
    std::uint64_t publication_milliseconds) noexcept;

}  // namespace wawvr::mod
