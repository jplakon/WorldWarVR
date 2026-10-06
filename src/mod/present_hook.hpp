#pragma once

#include "../t4/profile.hpp"

#include <windows.h>

namespace wawvr::mod {

// Selects the exact renderer layout only after the executable profile has
// passed file and mapped-image validation. Configuration is process-wide,
// idempotent for the same validated layout, and rejects cross-layout changes.
[[nodiscard]] bool configure_renderer_hooks(
    wawvr::t4::ExecutableLayoutId layout) noexcept;

// Internal read-only selector shared by the scene/backend/present hooks. It
// returns false until configure_renderer_hooks has selected a known layout.
[[nodiscard]] bool get_configured_renderer_layout(
    wawvr::t4::ExecutableLayoutId* layout) noexcept;

// Runs on the post-validation bootstrap worker. It validates the exact
// RB_SwapBuffers bytes, waits for T4's selected swap chain, then patches only
// its Present slot (plus the device Reset slot). The function normally lives
// for the lifetime of the process.
void run_present_hook_monitor(HMODULE module) noexcept;

// Cooperative teardown hook. The monitor restores only slots whose current
// value is still the replacement installed by this DLL.
void request_present_hook_shutdown() noexcept;

// Called only by the validated WinMain Com_Frame callsite wrapper, after the
// original Com_Frame has returned and T4 has completed its render/fence work.
void service_present_hook_after_com_frame() noexcept;

// Shared, process-local diagnostic sink used by low-frequency input events.
// It writes beside the injected DLL when the renderer monitor has established
// its module path, with OutputDebugString as the startup fallback.
void input_diagnostic_log(const char* format, ...) noexcept;

[[nodiscard]] bool present_hook_installed() noexcept;
[[nodiscard]] bool mono_xr_ready() noexcept;

} // namespace wawvr::mod
