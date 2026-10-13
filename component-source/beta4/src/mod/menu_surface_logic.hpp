// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "menu_input_logic.hpp"
#include "present_hook_logic.hpp"

#include "xr_types.h"

#include <cstdint>

namespace wawvr::mod {

// Describes the exact native UI pixels that were successfully submitted on a
// finite quad. Geometry alone is insufficient during recovery: current engine
// state can advance while the headset still displays an older surface.
struct MenuPointerSurface final {
    wawvr::xr::QuadLayer panel{};
    wawvr::xr::NormalizedViewport content_viewport{};
    MenuCursorRegion cursor_region{};
    std::int32_t connection_state{};
    std::uint32_t key_catchers{};
    std::uint32_t source_width{};
    std::uint32_t source_height{};
};

[[nodiscard]] bool build_menu_pointer_surface(
    const wawvr::xr::QuadLayer& panel,
    const wawvr::xr::StereoSourceLayout& submitted_layout,
    std::uint32_t source_width,
    std::uint32_t source_height,
    std::int32_t connection_state,
    std::uint32_t key_catchers,
    MenuPointerSurface* surface) noexcept;

// Exact state equality deliberately disables pointing during a UI transition
// until the corresponding pixels have actually reached the headset. Static
// menus still match and remain interactive while their released image is
// safely resubmitted.
[[nodiscard]] bool menu_pointer_surface_matches_state(
    const MenuPointerSurface& surface,
    std::int32_t connection_state,
    std::uint32_t key_catchers) noexcept;

// Native UI state fields are too coarse to identify an individual menu page.
// Interaction is therefore allowed only against a descriptor for pixels that
// have actually reached the headset. An Escape gesture invalidates that
// descriptor before any other navigation can run in the same service call.
[[nodiscard]] bool menu_pointer_surface_allows_interaction(
    const MenuPointerSurface* surface,
    std::int32_t connection_state,
    std::uint32_t key_catchers,
    bool input_focused,
    bool menu_escape_tap_requested) noexcept;

// Any native action can synchronously change the page without changing T4's
// connection state or key-catcher mask. Retire the input descriptor until a
// newly rendered page is successfully submitted.
[[nodiscard]] bool menu_ui_action_invalidates_pointer_surface(
    bool menu_button_tapped,
    bool confirm_tapped,
    bool back_tapped) noexcept;

}  // namespace wawvr::mod
