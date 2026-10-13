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

// Integer-only state saved by the scope to preserve nested diagnostic samples.
struct StereoLodFovClampExecutionSample final {
    bool enabled{};
    std::uint32_t active_calls{};
    std::uint32_t changed_calls{};
    std::uint32_t first_source_bits{};
    std::uint32_t first_selected_bits{};
    std::uint32_t last_source_bits{};
    std::uint32_t last_selected_bits{};
};

// This scope is deliberately thread-local. It must surround only a validated
// stereo eye's R_RenderScene call, never a mono/menu/fallback call.
// WAWVR_LOD_DIAGNOSTICS=1, sampled at patch installation, enables bounded
// execution reports after this scope's native scene call has returned.
class ScopedStereoLodFovClamp final {
public:
    explicit ScopedStereoLodFovClamp(
        std::uint32_t eye_index = 0xFFFFFFFFU,
        std::uint64_t frame_id = 0) noexcept;
    ~ScopedStereoLodFovClamp();

    ScopedStereoLodFovClamp(const ScopedStereoLodFovClamp&) = delete;
    ScopedStereoLodFovClamp& operator=(const ScopedStereoLodFovClamp&) = delete;

private:
    bool previous_{};
    StereoLodFovClampExecutionSample previous_execution_sample_{};
    std::uint32_t eye_index_{};
    std::uint64_t frame_id_{};
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
