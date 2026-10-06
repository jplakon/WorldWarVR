#pragma once

#include <cstdint>

namespace wawvr::mod {

enum class StereoSceneHookResult : unsigned char {
    installed,
    already_installed,
    profile_mismatch,
    target_mismatch,
    thread_suspend_failed,
    patch_write_failed,
};

// Installs a five-byte relative-call replacement only at the independently
// mapped gameplay call site in the configured exact WaW SP/MP executable. All
// other R_RenderScene callers retain stock behavior.
[[nodiscard]] StereoSceneHookResult install_stereo_scene_hook() noexcept;
[[nodiscard]] StereoSceneHookResult restore_stereo_scene_hook() noexcept;
[[nodiscard]] bool stereo_scene_hook_installed() noexcept;

// Stronger than the installation-attempt predicate above: revalidates exact
// frontend/backend ownership and requires a non-consuming valid XR publication
// newer than the scene hook's last consumed frame. The exact CG_Draw2D boundary
// precedes gameplay R_RenderScene in the supported binary, so this rejects a
// stale Reset/recovery publication while preserving current-frame HUD packing.
[[nodiscard]] bool stereo_scene_ready_for_current_com_frame() noexcept;
// Used before native HUD command generation, with the same exact-profile,
// fresh optic and visibility-bank gates as the optional third scene view.
[[nodiscard]] std::int32_t stereo_scene_scope_reserved_width(
    std::int32_t full_width, std::int32_t full_height) noexcept;
// Native HUD predicates run after placement on the same game thread. Reuse
// that frame's decision rather than expiring the optic midway through Draw2D.
[[nodiscard]] bool stereo_scene_physical_scope_hud_active() noexcept;
[[nodiscard]] const char* describe_stereo_scene_hook_result(
    StereoSceneHookResult result) noexcept;

} // namespace wawvr::mod
