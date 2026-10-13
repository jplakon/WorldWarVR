#include "compositor_math.hpp"

#include <cmath>
#include <iostream>

namespace {

int failures = 0;

void expect(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool near(const float left, const float right) {
    return std::abs(left - right) < 0.0001F;
}

wawvr::xr::Fovf comfort_fov() {
    return {
        .angle_left = -0.62F,
        .angle_right = 0.62F,
        .angle_up = 0.38F,
        .angle_down = -0.38F,
    };
}

float comfort_tangent_aspect() {
    return std::tan(0.62F) / std::tan(0.38F);
}

void test_four_by_three_into_comfort_projection() {
    wawvr::xr::NormalizedViewport fitted{};
    expect(wawvr::xr::fit_source_aspect_viewport(
               {0.0F, 0.0F, 1.0F, 1.0F}, 1024, 768,
               1.0F, {0.0F, 0.0F, 1.0F, 1.0F}, comfort_fov(),
               &fitted),
           "4:3 source fits the submitted comfort projection");
    const float expected_width =
        (4.0F / 3.0F) / comfort_tangent_aspect();
    expect(near(fitted.y, 0.0F) && near(fitted.height, 1.0F),
           "4:3 comfort image uses complete projection height");
    expect(near(fitted.width, expected_width) &&
               near(fitted.x, 0.5F * (1.0F - expected_width)),
           "4:3 comfort image pillarboxes in projection tangent space");
}

void test_packed_right_half_restores_logical_width() {
    wawvr::xr::NormalizedViewport fitted{};
    expect(wawvr::xr::fit_source_aspect_viewport(
               {0.5F, 0.0F, 0.5F, 1.0F}, 1600, 900,
               2.0F, {0.0F, 0.0F, 1.0F, 1.0F}, comfort_fov(),
               &fitted),
           "packed right-eye crop restores its pre-packing width");
    const float logical_aspect = 1600.0F / 900.0F;
    const float expected_width =
        logical_aspect / comfort_tangent_aspect();
    expect(near(fitted.height, 1.0F) &&
               near(fitted.width, expected_width) &&
               near(fitted.x, 0.5F * (1.0F - fitted.width)),
           "packed half is not mistaken for a portrait image");
}

void test_invalid_input_fails_closed() {
    wawvr::xr::NormalizedViewport fitted{};
    expect(!wawvr::xr::fit_source_aspect_viewport(
               {}, 0, 768, 1.0F, {}, comfort_fov(), &fitted),
           "zero source extent fails closed");
    expect(!wawvr::xr::fit_source_aspect_viewport(
               {}, 1024, 768, 1.0F, {}, comfort_fov(), nullptr),
           "null output fails closed");
    expect(!wawvr::xr::fit_source_aspect_viewport(
               {}, 1024, 768, 0.0F, {}, comfort_fov(), &fitted),
           "invalid logical aspect multiplier fails closed");
}

}  // namespace

int main() {
    test_four_by_three_into_comfort_projection();
    test_packed_right_half_restores_logical_width();
    test_invalid_input_fails_closed();
    if (failures != 0) {
        std::cerr << failures << " compositor-math test(s) failed\n";
        return 1;
    }
    std::cout << "compositor-math tests passed\n";
    return 0;
}
