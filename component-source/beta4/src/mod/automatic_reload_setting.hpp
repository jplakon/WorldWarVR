// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <string_view>

namespace wawvr::mod {

inline constexpr wchar_t kAutomaticReloadEnvironmentVariable[] =
    L"WAWVR_AUTOMATIC_RELOAD";

// Runtime settings use an explicit child-process 1/0 contract. Missing,
// empty, and malformed values preserve the default manual-reload behavior.
[[nodiscard]] bool automatic_reload_setting_enabled(
    std::wstring_view value) noexcept;

}  // namespace wawvr::mod
