// SPDX-License-Identifier: GPL-3.0-only
#include "menu_panel_logic.hpp"

#include "xr_math.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace {

int failures = 0;

void expect(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool near(const float left, const float right, const float epsilon = 0.0001F) {
    return std::abs(left - right) <= epsilon;
}

void test_panel_preserves_accepted_angular_size() {
    wawvr::xr::QuadLayer panel{};
    expect(wawvr::mod::build_world_menu_panel(
               {{}, {1.0F, 2.0F, 3.0F}}, &panel),
           "identity entry pose builds a finite menu panel");
    expect(near(panel.pose.position.x, 1.0F) &&
               near(panel.pose.position.y, 2.0F) &&
               near(panel.pose.position.z, 1.0F),
           "panel centre is exactly two metres along OpenXR -Z");
    expect(near(
               std::atan(0.5F * panel.size_meters.x /
                         wawvr::mod::kMenuPanelDistanceMeters),
               wawvr::mod::kMenuPanelHalfHorizontalAngleRadians) &&
               near(
                   std::atan(0.5F * panel.size_meters.y /
                             wawvr::mod::kMenuPanelDistanceMeters),
                   wawvr::mod::kMenuPanelHalfVerticalAngleRadians),
           "finite quad preserves the headset-approved angular coverage");

    constexpr float kSqrtHalf = 0.70710678118F;
    wawvr::xr::QuadLayer yawed{};
    expect(wawvr::mod::build_world_menu_panel(
               {{0.0F, kSqrtHalf, 0.0F, kSqrtHalf}, {}}, &yawed) &&
               near(yawed.pose.position.x, -2.0F) &&
               near(yawed.pose.position.z, 0.0F),
           "panel follows entry yaw while remaining fixed in LOCAL space");

    wawvr::xr::TrackedPose yawed_aim{
        .active = true,
        .orientation_valid = true,
        .position_valid = true,
        .pose = {{0.0F, kSqrtHalf, 0.0F, kSqrtHalf}, {}},
    };
    const auto yawed_hit = wawvr::mod::point_at_world_menu_panel(
        yawed_aim, yawed);
    expect(yawed_hit.valid && near(yawed_hit.u, 0.5F) &&
               near(yawed_hit.v, 0.5F),
           "yawed controller and panel share the same Local-space centre ray");
}

void test_controller_ray_hits_front_face_only() {
    wawvr::xr::QuadLayer panel{};
    expect(wawvr::mod::build_world_menu_panel({}, &panel),
           "origin panel builds");

    wawvr::xr::TrackedPose aim{
        .active = true,
        .orientation_valid = true,
        .position_valid = true,
    };
    auto hit = wawvr::mod::point_at_world_menu_panel(aim, panel);
    expect(hit.valid && near(hit.u, 0.5F) && near(hit.v, 0.5F) &&
               near(hit.distance_meters, 2.0F),
           "identity aim ray lands at panel centre");

    aim.pose.position.x = 0.5F * panel.size_meters.x;
    aim.pose.position.y = 0.5F * panel.size_meters.y;
    hit = wawvr::mod::point_at_world_menu_panel(aim, panel);
    expect(hit.valid && near(hit.u, 1.0F) && near(hit.v, 0.0F),
           "panel right/top edge maps to native top-right UV");

    aim.pose.position.x += 0.01F;
    expect(!wawvr::mod::point_at_world_menu_panel(aim, panel).valid,
           "ray beyond the finite panel is a miss, not an edge clamp");

    aim.pose.position = {0.0F, 0.0F, -3.0F};
    aim.pose.orientation = {0.0F, 1.0F, 0.0F, 0.0F};
    expect(!wawvr::mod::point_at_world_menu_panel(aim, panel).valid,
           "invisible OpenXR quad back face cannot be clicked");

    aim.position_valid = false;
    expect(!wawvr::mod::point_at_world_menu_panel(aim, panel).valid,
           "orientation-only controller tracking cannot invent a ray origin");
}

void test_content_bars_are_not_clickable() {
    const wawvr::mod::MenuPointerHit center{
        .valid = true,
        .u = 0.5F,
        .v = 0.5F,
        .distance_meters = 2.0F,
    };
    const wawvr::xr::NormalizedViewport four_by_three{
        .x = 0.125F,
        .y = 0.0F,
        .width = 0.75F,
        .height = 1.0F,
    };
    auto hit = wawvr::mod::remap_menu_pointer_to_content(
        center, four_by_three);
    expect(hit.valid && near(hit.u, 0.5F) && near(hit.v, 0.5F),
           "centre remains centre after pillarbox remap");

    wawvr::mod::MenuPointerHit left_bar = center;
    left_bar.u = 0.10F;
    expect(!wawvr::mod::remap_menu_pointer_to_content(
                left_bar, four_by_three).valid,
           "black pillarbox area is not sent to native UI hit testing");

    wawvr::mod::MenuPointerHit content_left = center;
    content_left.u = 0.125F;
    hit = wawvr::mod::remap_menu_pointer_to_content(
        content_left, four_by_three);
    expect(hit.valid && near(hit.u, 0.0F),
           "visible content edge maps to native source edge");
}

void test_reticle_keeps_raw_panel_uv_while_native_cursor_remaps() {
    const wawvr::mod::MenuPointerHit raw_panel_hit{
        .valid = true,
        .u = 0.375F,
        .v = 0.25F,
        .distance_meters = 2.0F,
    };
    const wawvr::xr::NormalizedViewport centered_half_width{
        .x = 0.25F,
        .y = 0.0F,
        .width = 0.5F,
        .height = 1.0F,
    };
    const auto native_cursor_hit =
        wawvr::mod::remap_menu_pointer_to_content(
            raw_panel_hit, centered_half_width);

    expect(native_cursor_hit.valid && near(native_cursor_hit.u, 0.25F) &&
               near(native_cursor_hit.v, 0.25F),
           "native cursor receives content-remapped UV");
    expect(near(raw_panel_hit.u, 0.375F) && near(raw_panel_hit.v, 0.25F),
           "visible reticle retains raw full-panel UV");
}

void test_invalid_panel_input_fails_closed() {
    wawvr::xr::QuadLayer panel{};
    auto pose = wawvr::xr::Posef{};
    pose.position.x = std::numeric_limits<float>::quiet_NaN();
    expect(!wawvr::mod::build_world_menu_panel(pose, &panel),
           "non-finite anchor cannot create a runtime layer");
    expect(!wawvr::mod::build_world_menu_panel({}, nullptr),
           "null panel output fails closed");
}

}  // namespace

int main() {
    test_panel_preserves_accepted_angular_size();
    test_controller_ray_hits_front_face_only();
    test_content_bars_are_not_clickable();
    test_reticle_keeps_raw_panel_uv_while_native_cursor_remaps();
    test_invalid_panel_input_fails_closed();
    if (failures != 0) {
        std::cerr << failures << " menu-panel logic test(s) failed\n";
        return 1;
    }
    std::cout << "menu-panel logic tests passed\n";
    return 0;
}
