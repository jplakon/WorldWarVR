#include "camera_comfort_logic.hpp"

#include <cmath>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] bool near(
    const float left, const float right,
    const float tolerance = 1.0e-4F) noexcept {
    return std::abs(left - right) <= tolerance;
}

}  // namespace

int main() {
    using wawvr::mod::gravity_level_t4_camera_axis;
    using wawvr::xr::Basis3f;

    Basis3f output{};
    const Basis3f rolled_and_pitched{
        .forward = {0.6F, 0.8F, 0.75F},
        .left = {-0.64F, 0.48F, 0.60F},
        .up = {0.48F, -0.36F, 0.80F},
    };
    expect(gravity_level_t4_camera_axis(rolled_and_pitched, &output),
           "finite animated camera axis is accepted");
    expect(near(output.forward.x, 0.6F) &&
               near(output.forward.y, 0.8F) &&
               near(output.forward.z, 0.0F),
           "body yaw is retained while pitch is removed");
    expect(near(output.left.x, -0.8F) &&
               near(output.left.y, 0.6F) &&
               near(output.left.z, 0.0F) &&
               near(output.up.x, 0.0F) && near(output.up.y, 0.0F) &&
               near(output.up.z, 1.0F),
           "roll is replaced by a gravity-level IW basis");

    const Basis3f vertical_spawn{
        .forward = {0.0F, 0.0F, 1.0F},
        .left = {-1.0F, 0.0F, 0.0F},
        .up = {0.0F, -1.0F, 0.0F},
    };
    expect(gravity_level_t4_camera_axis(vertical_spawn, &output),
           "vertical stock forward row recovers yaw from the left row");
    expect(near(output.forward.x, 0.0F) &&
               near(output.forward.y, 1.0F) &&
               near(output.forward.z, 0.0F),
           "ceiling/floor spawn basis is made horizontal");

    Basis3f invalid{};
    invalid.forward.x = std::nanf("");
    expect(!gravity_level_t4_camera_axis(invalid, &output),
           "non-finite stock camera fails closed");
    expect(!gravity_level_t4_camera_axis(Basis3f{}, nullptr),
           "null destination fails closed");

    if (failures != 0) {
        std::cerr << failures << " camera-comfort logic test(s) failed\n";
        return 1;
    }
    std::cout << "Camera-comfort logic tests passed\n";
    return 0;
}
