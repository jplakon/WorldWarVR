#pragma once

#include "t4/bindings.hpp"

#include <cstdint>

namespace wawvr::mod {

enum class LodFovClampPatchStatus : std::uint8_t {
    installed,
    already_installed,
    disabled_by_environment,
    unsupported_compiler_or_architecture,
    preparation_failed,
    jump_out_of_range,
    thread_suspend_failed,
    expected_bytes_changed,
    target_protection_failed,
    patch_write_failed,
    patch_cache_flush_failed,
    protection_restore_failed,
};

struct LodFovClampPatchResult final {
    LodFovClampPatchStatus status{
        LodFovClampPatchStatus::preparation_failed};
    std::uintptr_t target{};
    std::uintptr_t continuation{};
    std::uint32_t system_error{};

    [[nodiscard]] bool ok() const noexcept {
        return status == LodFovClampPatchStatus::installed ||
               status == LodFovClampPatchStatus::already_installed;
    }
};

enum class LodFovClampRestoreStatus : std::uint8_t {
    restored,
    not_installed,
    foreign_patch_preserved,
    thread_suspend_failed,
    target_protection_failed,
    patch_write_failed,
    patch_cache_flush_failed,
    protection_restore_failed,
};

// This scope is deliberately thread-local. It must surround only a validated
// stereo eye's R_RenderScene call, never a mono/menu/fallback call.
class ScopedStereoLodFovClamp final {
public:
    ScopedStereoLodFovClamp() noexcept;
    ~ScopedStereoLodFovClamp();

    ScopedStereoLodFovClamp(const ScopedStereoLodFovClamp&) = delete;
    ScopedStereoLodFovClamp& operator=(const ScopedStereoLodFovClamp&) = delete;

private:
    bool previous_{};
};

[[nodiscard]] LodFovClampPatchResult install_lod_fov_clamp_patch(
    const wawvr::t4::ValidatedBindings& bindings) noexcept;
[[nodiscard]] LodFovClampRestoreStatus restore_lod_fov_clamp_patch() noexcept;
[[nodiscard]] bool lod_fov_clamp_patch_installed() noexcept;

[[nodiscard]] const char* lod_fov_clamp_patch_status_name(
    LodFovClampPatchStatus status) noexcept;
[[nodiscard]] const char* lod_fov_clamp_restore_status_name(
    LodFovClampRestoreStatus status) noexcept;

} // namespace wawvr::mod
