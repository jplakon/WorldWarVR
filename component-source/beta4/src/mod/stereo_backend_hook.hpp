#pragma once

namespace wawvr::mod {

enum class StereoBackendHookResult : unsigned char {
    installed,
    already_installed,
    profile_mismatch,
    thread_suspend_failed,
    patch_write_failed,
};

// Installs configured exact-profile call-site replacements in both the
// synchronous and SMP T4 backend paths, plus the two full-target clear sites
// used by the standard 3D draw. No shared renderer entry point is detoured.
[[nodiscard]] StereoBackendHookResult install_stereo_backend_hook() noexcept;
[[nodiscard]] StereoBackendHookResult restore_stereo_backend_hook() noexcept;
[[nodiscard]] bool stereo_backend_hook_installed() noexcept;

// Rechecks ownership of every replacement and the independently mapped
// GfxViewInfo/list-layout sentinels. The frontend scene hook must remain mono
// unless this returns true.
[[nodiscard]] bool stereo_backend_hook_ready() noexcept;

} // namespace wawvr::mod
