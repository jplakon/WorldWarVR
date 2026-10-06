#pragma once

#include "t4/bindings.hpp"

#include <cstdint>

namespace wawvr::mod {

inline constexpr wawvr::t4::Rva kComFrameRva = 0x0019E330;

enum class FrameBoundaryHookStatus : std::uint8_t {
    installed,
    already_installed,
    disabled_by_environment,
    unsupported_compiler_or_architecture,
    rejected_wrong_profile,
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

struct FrameBoundaryHookInstallResult final {
    FrameBoundaryHookStatus status{FrameBoundaryHookStatus::preparation_failed};
    std::uintptr_t target{};
    std::uintptr_t original{};
    std::uint32_t system_error{};

    [[nodiscard]] bool ok() const noexcept {
        return status == FrameBoundaryHookStatus::installed ||
               status == FrameBoundaryHookStatus::already_installed;
    }
};

enum class FrameBoundaryRestoreStatus : std::uint8_t {
    restored,
    not_installed,
    foreign_patch_preserved,
    thread_suspend_failed,
    target_protection_failed,
    patch_write_failed,
    patch_cache_flush_failed,
    protection_restore_failed,
};

[[nodiscard]] FrameBoundaryHookInstallResult install_frame_boundary_hook(
    const wawvr::t4::ValidatedBindings& bindings) noexcept;

// Stops post-frame servicing immediately. The bootstrap monitor subsequently
// restores the original E8 call after its XR/Present state is quiescent.
void request_frame_boundary_hook_shutdown() noexcept;

[[nodiscard]] FrameBoundaryRestoreStatus restore_frame_boundary_hook() noexcept;
[[nodiscard]] bool frame_boundary_hook_installed() noexcept;

[[nodiscard]] const char* frame_boundary_hook_status_name(
    FrameBoundaryHookStatus status) noexcept;
[[nodiscard]] const char* frame_boundary_restore_status_name(
    FrameBoundaryRestoreStatus status) noexcept;

} // namespace wawvr::mod
