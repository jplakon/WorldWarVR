#include "hud_placement_logic.hpp"

#include <cmath>

namespace wawvr::mod {

HudPlacementPlan plan_hud_placement(
    const HudPlacementInput& input) noexcept {
    HudPlacementPlan plan{};
    if (input.full_width <= 0 || input.full_height <= 0 ||
        input.full_width > kHudMaximumViewportDimension ||
        input.full_height > kHudMaximumViewportDimension) {
        return plan;
    }

    plan.valid = true;
    plan.width = input.full_width;
    plan.height = input.full_height;

    const bool active_unpaused_gameplay =
        input.presentation_state_valid &&
        input.stereo_packing_ready &&
        input.connection_state == input.active_connection_state &&
        input.key_catchers == 0;
    if (!active_unpaused_gameplay) {
        return plan;
    }

    // An odd packed width cannot describe two equal source viewports. Keep
    // the safe full placement instead of truncating or leaving a stale prior
    // half-width placement behind.
    if (input.scope_reserved_width < 0 ||
        input.scope_reserved_width >= input.full_width) {
        return plan;
    }
    const auto stereo_width = input.full_width - input.scope_reserved_width;
    if (stereo_width < 2 || (stereo_width & 1) != 0) {
        return plan;
    }

    plan.mode = HudPlacementMode::packed_eye;
    plan.width = stereo_width / 2;
    return plan;
}

bool apply_packed_hud_comfort(
    HudScreenPlacement* const placement,
    const std::int32_t eye_width,
    const std::int32_t eye_height) noexcept {
    if (placement == nullptr || eye_width <= 0 || eye_height <= 0 ||
        eye_width > kHudMaximumViewportDimension ||
        eye_height > kHudMaximumViewportDimension) {
        return false;
    }
    for (const float value : placement->scale_virtual_to_real) {
        if (!std::isfinite(value) || value <= 0.0F) {
            return false;
        }
    }
    for (const float value : placement->scale_virtual_to_full) {
        if (!std::isfinite(value) || value <= 0.0F) {
            return false;
        }
    }
    if (!std::isfinite(placement->sub_screen_left) ||
        !std::isfinite(placement->sub_screen_top)) {
        return false;
    }

    const float old_scale_x = placement->scale_virtual_to_real[0];
    const float old_scale_y = placement->scale_virtual_to_real[1];
    placement->scale_virtual_to_real[0] *= kHudComfortScale;
    placement->scale_virtual_to_real[1] *= kHudComfortScale;
    // T4's default alignment adds these offsets after scaling virtual
    // coordinates. Compensate around the 640x480 virtual center so global
    // HUD reduction does not drag centered text/elements toward the upper-left.
    placement->sub_screen_left +=
        (old_scale_x - placement->scale_virtual_to_real[0]) * 320.0F;
    placement->sub_screen_top +=
        (old_scale_y - placement->scale_virtual_to_real[1]) * 240.0F;
    placement->scale_real_to_virtual[0] =
        1.0F / placement->scale_virtual_to_real[0];
    placement->scale_real_to_virtual[1] =
        1.0F / placement->scale_virtual_to_real[1];

    const float width = static_cast<float>(eye_width);
    const float height = static_cast<float>(eye_height);
    const float inset_x =
        (1.0F - kHudSafeAreaHorizontalFraction) * 0.5F * width;
    const float inset_y =
        (1.0F - kHudSafeAreaVerticalFraction) * 0.5F * height;
    placement->real_viewable_min[0] = inset_x;
    placement->real_viewable_min[1] = inset_y;
    placement->real_viewable_max[0] = width - inset_x;
    placement->real_viewable_max[1] = height - inset_y;
    for (std::size_t axis = 0; axis < 2; ++axis) {
        placement->virtual_viewable_min[axis] =
            placement->real_viewable_min[axis] *
            placement->scale_real_to_virtual[axis];
        placement->virtual_viewable_max[axis] =
            placement->real_viewable_max[axis] *
            placement->scale_real_to_virtual[axis];
    }
    return true;
}

}  // namespace wawvr::mod
