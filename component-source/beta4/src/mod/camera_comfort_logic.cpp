#include "camera_comfort_logic.hpp"

#include <cmath>

namespace wawvr::mod {
namespace {

[[nodiscard]] bool finite_vector(const wawvr::xr::Vec3f& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] bool normalized_horizontal(
    float x, float y, wawvr::xr::Vec3f* const output) noexcept {
    if (output == nullptr || !std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }
    const float length_squared = x * x + y * y;
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-8F) {
        return false;
    }
    const float inverse_length = 1.0F / std::sqrt(length_squared);
    output->x = x * inverse_length;
    output->y = y * inverse_length;
    output->z = 0.0F;
    return finite_vector(*output);
}

}  // namespace

bool gravity_level_t4_camera_axis(
    const wawvr::xr::Basis3f& stock,
    wawvr::xr::Basis3f* const leveled) noexcept {
    if (leveled == nullptr || !finite_vector(stock.forward) ||
        !finite_vector(stock.left) || !finite_vector(stock.up)) {
        return false;
    }

    wawvr::xr::Vec3f forward{};
    if (!normalized_horizontal(stock.forward.x, stock.forward.y, &forward)) {
        // A spawn/death animation can point the stock forward row almost
        // straight up or down.  The left row still carries body yaw in that
        // case: IW left=(-sin(yaw), cos(yaw), 0).
        if (!normalized_horizontal(stock.left.y, -stock.left.x, &forward)) {
            return false;
        }
    }

    *leveled = {
        .forward = forward,
        .left = {-forward.y, forward.x, 0.0F},
        .up = {0.0F, 0.0F, 1.0F},
    };
    return true;
}

}  // namespace wawvr::mod
