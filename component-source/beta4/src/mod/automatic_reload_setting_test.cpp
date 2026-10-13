// SPDX-License-Identifier: GPL-3.0-only
#include "automatic_reload_setting.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    using wawvr::mod::automatic_reload_setting_enabled;

    require(automatic_reload_setting_enabled(L"1"),
            "explicit 1 enables automatic reload");
    require(!automatic_reload_setting_enabled(L"0"),
            "explicit 0 preserves manual reload");
    require(!automatic_reload_setting_enabled(L""),
            "missing or empty setting defaults to manual reload");
    require(!automatic_reload_setting_enabled(L"true") &&
                !automatic_reload_setting_enabled(L"01") &&
                !automatic_reload_setting_enabled(L" 1"),
            "malformed values fail closed to manual reload");
    return 0;
}
