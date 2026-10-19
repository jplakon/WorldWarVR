// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
namespace wawvr::mod {
// Initial witnessed frontend fixture ONLY. No active-gameplay gate is widened.
struct LoadingTipFramePolicy final {
    bool opt_in{}, presentation_valid{}, frame_current{}, views_valid{}, focused{};
    std::int32_t connection{};
    std::uint32_t key_catchers{};
};
[[nodiscard]] constexpr bool loading_tip_frame_allows(const LoadingTipFramePolicy& p) noexcept {
    return p.opt_in && p.presentation_valid && p.frame_current && p.views_valid &&
        p.focused && p.connection==2 && p.key_catchers==0x10;
}
} // namespace wawvr::mod
