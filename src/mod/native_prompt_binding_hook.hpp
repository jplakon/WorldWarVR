// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "t4/bindings.hpp"

namespace wawvr::mod {
enum class NativePromptBindingHookStatus : unsigned char {
    installed, already_installed, disabled, unsupported, fingerprint_mismatch,
    thread_suspend_failed, patch_failed,
};

[[nodiscard]] NativePromptBindingHookStatus install_native_prompt_binding_hook(
    const wawvr::t4::ValidatedBindings& bindings) noexcept;
void request_native_prompt_binding_hook_shutdown() noexcept;
[[nodiscard]] bool native_prompt_binding_hook_installed() noexcept;
// Returns false on a still-owned restoration failure. Foreign bytes are
// preserved and cease being owned; no native text storage is ever patched.
[[nodiscard]] bool restore_native_prompt_binding_hook() noexcept;
} // namespace wawvr::mod
