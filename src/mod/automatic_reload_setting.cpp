// SPDX-License-Identifier: GPL-3.0-only
#include "automatic_reload_setting.hpp"

namespace wawvr::mod {

bool automatic_reload_setting_enabled(
    const std::wstring_view value) noexcept {
    return value == L"1";
}

}  // namespace wawvr::mod
