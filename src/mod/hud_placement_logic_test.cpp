#include "hud_placement_logic.hpp"
#include "physical_scope_layout.hpp"

#include <iostream>
#include <cmath>
#include <string_view>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

}  // namespace

int main() {
    using namespace wawvr::mod;

    auto plan = plan_hud_placement({
        .presentation_state_valid = true,
        .stereo_packing_ready = true,
        .connection_state = 10,
        .key_catchers = 0,
        .full_width = 2560,
        .full_height = 1440,
    });
    expect(plan.valid && plan.mode == HudPlacementMode::packed_eye &&
               plan.width == 1280 && plan.height == 1440,
           "high-clarity gameplay uses exactly one packed eye width");

    plan = plan_hud_placement({
        .presentation_state_valid = true,
        .stereo_packing_ready = true,
        .connection_state = 10,
        .key_catchers = 0,
        .full_width = 1600,
        .full_height = 900,
    });
    expect(plan.valid && plan.mode == HudPlacementMode::packed_eye &&
               plan.width == 800 && plan.height == 900,
           "performance gameplay uses exactly one packed eye width");

    plan = plan_hud_placement({
        .presentation_state_valid = true,
        .stereo_packing_ready = true,
        .connection_state = 10,
        .key_catchers = 0,
        .full_width = 1024,
        .full_height = 768,
    });
    expect(plan.valid && plan.mode == HudPlacementMode::packed_eye &&
               plan.width == 512 && plan.height == 768,
           "fallback packed resolution uses exactly half its full width");

    expect(physical_scope_reserved_width(true, 6016, 2688) == 1024,
           "an active physical scope reserves its dedicated optical strip");
    expect(physical_scope_reserved_width(true, 6016, 1024) == 1024,
           "the minimum supported optical panel height reserves the strip");
    expect(physical_scope_reserved_width(false, 6016, 2688) == 0,
           "inactive scopes never reserve a strip at native resolution");
    expect(physical_scope_reserved_width(true, 6015, 2688) == 0 &&
               physical_scope_reserved_width(true, 6016, 1023) == 0,
           "insufficient source width or height does not reserve an optical strip");

    const HudPlacementInput scoped_input{
        .presentation_state_valid = true,
        .stereo_packing_ready = true,
        .connection_state = 10,
        .key_catchers = 0,
        .full_width = 6016,
        .full_height = 2688,
        .scope_reserved_width = physical_scope_reserved_width(true, 6016, 2688),
    };
    plan = plan_hud_placement(scoped_input);
    expect(plan.valid && plan.mode == HudPlacementMode::packed_eye &&
               plan.width == 2496 && plan.height == 2688,
           "scope-active HUD matches each 2496-pixel eye without the optical strip");

    auto ordinary_input = scoped_input;
    ordinary_input.scope_reserved_width =
        physical_scope_reserved_width(false, 6016, 2688);
    plan = plan_hud_placement(ordinary_input);
    expect(plan.valid && plan.mode == HudPlacementMode::packed_eye &&
               plan.width == 3008 && plan.height == 2688,
           "leaving the scope restores the ordinary 3008-pixel eye placement");

    auto smaller_input = scoped_input;
    smaller_input.full_width = 4992;
    smaller_input.scope_reserved_width = physical_scope_reserved_width(
        true, smaller_input.full_width, smaller_input.full_height);
    plan = plan_hud_placement(smaller_input);
    expect(plan.valid && plan.mode == HudPlacementMode::packed_eye &&
               plan.width == 2496 && plan.height == 2688,
           "a source too narrow for the optical strip retains ordinary stereo HUD packing");
    smaller_input = scoped_input;
    smaller_input.full_height = 1023;
    smaller_input.scope_reserved_width = physical_scope_reserved_width(
        true, smaller_input.full_width, smaller_input.full_height);
    plan = plan_hud_placement(smaller_input);
    expect(plan.valid && plan.mode == HudPlacementMode::packed_eye &&
               plan.width == 3008 && plan.height == 1023,
           "a source too short for the optical strip retains ordinary stereo HUD packing");

    auto unavailable_scope_input = scoped_input;
    unavailable_scope_input.stereo_packing_ready = false;
    plan = plan_hud_placement(unavailable_scope_input);
    expect(plan.valid && plan.mode == HudPlacementMode::full_frame &&
               plan.width == 6016 && plan.height == 2688,
           "unavailable XR restores the complete frame despite a reserved optical strip");
    unavailable_scope_input = scoped_input;
    unavailable_scope_input.key_catchers = 0x10;
    plan = plan_hud_placement(unavailable_scope_input);
    expect(plan.valid && plan.mode == HudPlacementMode::full_frame &&
               plan.width == 6016 && plan.height == 2688,
           "pausing with the scope raised restores the complete mono HUD placement");

    for (const auto reserved_width : std::array<std::int32_t, 5>{
             -1, 6016, 6017, 1023, 6015}) {
        auto invalid_scope_input = scoped_input;
        invalid_scope_input.scope_reserved_width = reserved_width;
        plan = plan_hud_placement(invalid_scope_input);
        expect(plan.valid && plan.mode == HudPlacementMode::full_frame &&
                   plan.width == 6016 && plan.height == 2688,
               "invalid or unequally packed optical reservations fail safe to the complete frame");
    }

    expect(stereo_hud_command_views(true) ==
               std::array<bool, 3>{false, true, true},
           "scope,left,right routing preserves both eye HUDs and excludes the magnified feed");
    expect(stereo_hud_command_views(false) ==
               std::array<bool, 3>{true, true, false},
           "ordinary left,right routing preserves both eye HUDs without a third view");

    plan = plan_hud_placement({
        .presentation_state_valid = true,
        .stereo_packing_ready = false,
        .connection_state = 10,
        .key_catchers = 0,
        .full_width = 1600,
        .full_height = 900,
    });
    expect(plan.valid && plan.mode == HudPlacementMode::full_frame &&
               plan.width == 1600 && plan.height == 900,
           "active gameplay stays full width until a current stereo frame is ready");

    plan = plan_hud_placement({
        .presentation_state_valid = true,
        .stereo_packing_ready = true,
        .connection_state = 10,
        .key_catchers = 0x10,
        .full_width = 1600,
        .full_height = 900,
    });
    expect(plan.valid && plan.mode == HudPlacementMode::full_frame &&
               plan.width == 1600 && plan.height == 900,
           "active pause/UI catcher restores the full mono placement");

    plan = plan_hud_placement({
        .presentation_state_valid = true,
        .stereo_packing_ready = true,
        .connection_state = 10,
        .key_catchers = 0x01,
        .full_width = 1600,
        .full_height = 900,
    });
    expect(plan.valid && plan.mode == HudPlacementMode::full_frame,
           "active console catcher restores the full mono placement");

    plan = plan_hud_placement({
        .presentation_state_valid = true,
        .stereo_packing_ready = true,
        .connection_state = 10,
        .key_catchers = 0x08,
        .full_width = 1600,
        .full_height = 900,
    });
    expect(plan.valid && plan.mode == HudPlacementMode::full_frame,
           "any other active catcher restores the full mono placement");

    plan = plan_hud_placement({
        .presentation_state_valid = true,
        .stereo_packing_ready = true,
        .connection_state = 2,
        .key_catchers = 0,
        .full_width = 1600,
        .full_height = 900,
    });
    expect(plan.valid && plan.mode == HudPlacementMode::full_frame,
           "cinematic connection state retains full-frame placement");

    plan = plan_hud_placement({
        .presentation_state_valid = false,
        .stereo_packing_ready = true,
        .connection_state = 10,
        .key_catchers = 0,
        .full_width = 1600,
        .full_height = 900,
    });
    expect(plan.valid && plan.mode == HudPlacementMode::full_frame,
           "unavailable presentation state fails safe to full placement");

    plan = plan_hud_placement({
        .presentation_state_valid = true,
        .stereo_packing_ready = true,
        .connection_state = 10,
        .key_catchers = 0,
        .full_width = 1599,
        .full_height = 900,
    });
    expect(plan.valid && plan.mode == HudPlacementMode::full_frame &&
               plan.width == 1599,
           "odd active width is never truncated into unequal eye viewports");

    expect(!plan_hud_placement({
                .presentation_state_valid = true,
                .stereo_packing_ready = true,
                .connection_state = 10,
                .key_catchers = 0,
                .full_width = 0,
                .full_height = 900,
            }).valid,
           "zero width is rejected");
    expect(!plan_hud_placement({
                .presentation_state_valid = true,
                .stereo_packing_ready = true,
                .connection_state = 10,
                .key_catchers = 0,
                .full_width = 1600,
                .full_height = -1,
            }).valid,
           "negative height is rejected");
    expect(!plan_hud_placement({
                .presentation_state_valid = true,
                .stereo_packing_ready = true,
                .connection_state = 10,
                .key_catchers = 0,
                .full_width = kHudMaximumViewportDimension + 1,
                .full_height = 900,
            }).valid,
           "implausibly large dimensions are rejected");

    HudScreenPlacement placement{};
    placement.scale_virtual_to_real[0] = 1.25F;
    placement.scale_virtual_to_real[1] = 1.875F;
    placement.scale_virtual_to_full[0] = 1.25F;
    placement.scale_virtual_to_full[1] = 1.875F;
    placement.scale_real_to_virtual[0] = 0.8F;
    placement.scale_real_to_virtual[1] = 1.0F / 1.875F;
    placement.real_viewport_size[0] = 800.0F;
    placement.real_viewport_size[1] = 900.0F;
    placement.sub_screen_left = 17.0F;
    placement.sub_screen_top = 23.0F;
    const float full_scale_x_before = placement.scale_virtual_to_full[0];
    const float full_scale_y_before = placement.scale_virtual_to_full[1];
    const float center_x_before =
        320.0F * placement.scale_virtual_to_real[0] +
        placement.sub_screen_left;
    const float center_y_before =
        240.0F * placement.scale_virtual_to_real[1] +
        placement.sub_screen_top;
    expect(apply_packed_hud_comfort(&placement, 800, 900),
           "valid packed placement accepts the VR comfort transform");
    expect(std::abs(placement.scale_virtual_to_real[0] - 0.475F) < 0.0001F &&
               std::abs(placement.scale_virtual_to_real[1] - 0.7125F) < 0.0001F,
           "ordinary HUD elements are uniformly reduced around their anchors");
    expect(std::abs(
               320.0F * placement.scale_virtual_to_real[0] +
                   placement.sub_screen_left - center_x_before) < 0.0001F &&
               std::abs(
               240.0F * placement.scale_virtual_to_real[1] +
                   placement.sub_screen_top - center_y_before) < 0.0001F,
           "default-aligned HUD content remains centered while it shrinks");
    expect(std::abs(placement.real_viewable_min[0] - 216.0F) < 0.0001F &&
               std::abs(placement.real_viewable_max[0] - 584.0F) < 0.0001F &&
               std::abs(placement.real_viewable_min[1] - 180.0F) < 0.0001F &&
               std::abs(placement.real_viewable_max[1] - 720.0F) < 0.0001F,
           "edge HUD elements use the intended binocular-safe rectangle");
    expect(placement.scale_virtual_to_full[0] == full_scale_x_before &&
               placement.scale_virtual_to_full[1] == full_scale_y_before,
           "full-screen gameplay overlays keep their native extent");

    HudScreenPlacement high_resolution_placement{};
    high_resolution_placement.scale_virtual_to_real[0] = 2.0F;
    high_resolution_placement.scale_virtual_to_real[1] = 3.0F;
    high_resolution_placement.scale_virtual_to_full[0] = 2.0F;
    high_resolution_placement.scale_virtual_to_full[1] = 3.0F;
    expect(apply_packed_hud_comfort(
               &high_resolution_placement, 1280, 1440),
           "the shipped high-clarity eye viewport accepts the comfort transform");
    expect(std::abs(
               high_resolution_placement.real_viewable_min[0] - 345.6F) <
               0.0001F &&
               std::abs(
                   high_resolution_placement.real_viewable_max[0] - 934.4F) <
                   0.0001F &&
               std::abs(
                   high_resolution_placement.real_viewable_min[1] - 288.0F) <
                   0.0001F &&
               std::abs(
                   high_resolution_placement.real_viewable_max[1] - 1152.0F) <
                   0.0001F,
           "high-clarity ammo, map, rounds, and points stay in the central comfort field");

    HudScreenPlacement invalid_placement = placement;
    invalid_placement.scale_virtual_to_real[0] = std::nanf("");
    expect(!apply_packed_hud_comfort(&invalid_placement, 800, 900),
           "non-finite ScreenPlacement fails closed");
    expect(!apply_packed_hud_comfort(nullptr, 800, 900),
           "null ScreenPlacement fails closed");

    if (failures != 0) {
        std::cerr << failures << " HUD-placement logic test(s) failed\n";
        return 1;
    }
    std::cout << "HUD-placement logic tests passed\n";
    return 0;
}
