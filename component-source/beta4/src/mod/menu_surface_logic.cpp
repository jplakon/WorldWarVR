// SPDX-License-Identifier: GPL-3.0-only
#include "menu_surface_logic.hpp"

#include "comfort_panel_constants.hpp"
#include "compositor_math.hpp"

#include <cmath>
#include <limits>

namespace wawvr::mod {
namespace {

[[nodiscard]] bool finite(const float value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool valid_panel(const wawvr::xr::QuadLayer& panel) noexcept {
    const auto& q = panel.pose.orientation;
    const float length_squared =
        q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    return panel.source_eye < wawvr::xr::kEyeCount &&
           finite(panel.pose.position.x) && finite(panel.pose.position.y) &&
           finite(panel.pose.position.z) && finite(q.x) && finite(q.y) &&
           finite(q.z) && finite(q.w) && finite(length_squared) &&
           length_squared >= 0.98F && length_squared <= 1.02F &&
           finite(panel.size_meters.x) && finite(panel.size_meters.y) &&
           panel.size_meters.x > 0.0F && panel.size_meters.y > 0.0F;
}

}  // namespace

bool build_menu_pointer_surface(
    const wawvr::xr::QuadLayer& panel,
    const wawvr::xr::StereoSourceLayout& submitted_layout,
    const std::uint32_t source_width,
    const std::uint32_t source_height,
    const std::int32_t connection_state,
    const std::uint32_t key_catchers,
    MenuPointerSurface* const surface) noexcept {
    if (surface == nullptr || source_width == 0 || source_height == 0 ||
        (key_catchers & kT4UiKeyCatcher) == 0 || !valid_panel(panel)) {
        return false;
    }
    const wawvr::xr::NormalizedRect& source =
        submitted_layout.eyes[panel.source_eye];
    if (!finite(source.x) || !finite(source.y) || !finite(source.width) ||
        !finite(source.height) || source.x < 0.0F || source.y != 0.0F ||
        source.width <= 0.0F || source.height != 1.0F ||
        source.x + source.width > 1.0F) {
        return false;
    }

    const double origin_pixels =
        static_cast<double>(source_width) * source.x;
    const double width_pixels =
        static_cast<double>(source_width) * source.width;
    if (!std::isfinite(origin_pixels) || !std::isfinite(width_pixels) ||
        origin_pixels < 0.0 || width_pixels < 1.0 ||
        origin_pixels + width_pixels >
            static_cast<double>(source_width) + 0.001) {
        return false;
    }
    const auto origin = static_cast<std::uint64_t>(std::llround(origin_pixels));
    const auto width = static_cast<std::uint64_t>(std::llround(width_pixels));
    if (origin > std::numeric_limits<std::uint32_t>::max() ||
        width == 0 || width > std::numeric_limits<std::uint32_t>::max() ||
        origin + width > source_width) {
        return false;
    }

    wawvr::xr::NormalizedViewport content_viewport =
        submitted_layout.destinations[panel.source_eye];
    if (submitted_layout.preserve_source_aspect &&
        !wawvr::xr::fit_source_aspect_viewport(
            source, source_width, source_height,
            submitted_layout.source_aspect_multipliers[panel.source_eye],
            submitted_layout.destinations[panel.source_eye],
            menu_panel_comfort_fov(), &content_viewport)) {
        return false;
    }

    *surface = {
        .panel = panel,
        .content_viewport = content_viewport,
        .cursor_region = {
            .origin_x = static_cast<std::uint32_t>(origin),
            .width = static_cast<std::uint32_t>(width),
            .height = source_height,
        },
        .connection_state = connection_state,
        .key_catchers = key_catchers,
        .source_width = source_width,
        .source_height = source_height,
    };
    return true;
}

bool menu_pointer_surface_matches_state(
    const MenuPointerSurface& surface,
    const std::int32_t connection_state,
    const std::uint32_t key_catchers) noexcept {
    return (surface.key_catchers & kT4UiKeyCatcher) != 0 &&
           surface.cursor_region.width != 0 &&
           surface.cursor_region.height != 0 &&
           connection_state == surface.connection_state &&
           key_catchers == surface.key_catchers;
}

bool menu_pointer_surface_allows_interaction(
    const MenuPointerSurface* const surface,
    const std::int32_t connection_state,
    const std::uint32_t key_catchers,
    const bool input_focused,
    const bool menu_escape_tap_requested) noexcept {
    return input_focused && !menu_escape_tap_requested &&
           surface != nullptr && menu_pointer_surface_matches_state(
               *surface, connection_state, key_catchers);
}

bool menu_ui_action_invalidates_pointer_surface(
    const bool menu_button_tapped,
    const bool confirm_tapped,
    const bool back_tapped) noexcept {
    return menu_button_tapped || confirm_tapped || back_tapped;
}

}  // namespace wawvr::mod
