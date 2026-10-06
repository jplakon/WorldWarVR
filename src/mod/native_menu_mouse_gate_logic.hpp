// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>

namespace wawvr::mod {

// At the patched CL_MouseEvent call, the replacement bridge is entered after
// x, y, and the original return address have been pushed. The caller's dx/dy
// arguments therefore remain at these exact stack offsets.
inline constexpr std::ptrdiff_t kNativeMenuMouseDxBridgeStackOffset = 0x14;
inline constexpr std::ptrdiff_t kNativeMenuMouseDyBridgeStackOffset = 0x18;

[[nodiscard]] constexpr bool should_forward_native_menu_mouse_event(
    const std::int32_t dx, const std::int32_t dy) noexcept {
    return dx != 0 || dy != 0;
}

}  // namespace wawvr::mod
