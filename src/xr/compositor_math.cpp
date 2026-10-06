// SPDX-License-Identifier: GPL-3.0-only

#include "compositor_math.hpp"

#include <cmath>

namespace wawvr::xr {

bool fit_source_aspect_viewport(
    const NormalizedRect& source,
    const std::uint32_t source_width,
    const std::uint32_t source_height,
    const float source_aspect_multiplier,
    const NormalizedViewport& destination,
    const Fovf& projection_fov,
    NormalizedViewport* const fitted) noexcept {
    if (fitted == nullptr || source_width == 0 || source_height == 0 ||
        !std::isfinite(source.width) || !std::isfinite(source.height) ||
        !std::isfinite(source_aspect_multiplier) ||
        !std::isfinite(destination.x) || !std::isfinite(destination.y) ||
        !std::isfinite(destination.width) ||
        !std::isfinite(destination.height) ||
        !std::isfinite(projection_fov.angle_left) ||
        !std::isfinite(projection_fov.angle_right) ||
        !std::isfinite(projection_fov.angle_up) ||
        !std::isfinite(projection_fov.angle_down) ||
        source.width <= 0.0F || source.height <= 0.0F ||
        source_aspect_multiplier <= 0.0F ||
        destination.width <= 0.0F || destination.height <= 0.0F) {
        return false;
    }

    const float selected_width =
        static_cast<float>(source_width) * source.width;
    const float selected_height =
        static_cast<float>(source_height) * source.height;
    const float tangent_left = std::tan(projection_fov.angle_left);
    const float tangent_right = std::tan(projection_fov.angle_right);
    const float tangent_up = std::tan(projection_fov.angle_up);
    const float tangent_down = std::tan(projection_fov.angle_down);
    const float tangent_width = tangent_right - tangent_left;
    const float tangent_height = tangent_up - tangent_down;
    const float source_aspect =
        selected_width / selected_height * source_aspect_multiplier;
    const float destination_aspect =
        tangent_width * destination.width /
        (tangent_height * destination.height);
    if (!std::isfinite(source_aspect) ||
        !std::isfinite(destination_aspect) ||
        !std::isfinite(tangent_width) ||
        !std::isfinite(tangent_height) || source_aspect <= 0.0F ||
        tangent_width <= 0.0F || tangent_height <= 0.0F ||
        destination_aspect <= 0.0F) {
        return false;
    }

    *fitted = destination;
    if (source_aspect > destination_aspect) {
        fitted->height =
            destination.height * destination_aspect / source_aspect;
        fitted->y = destination.y +
            0.5F * (destination.height - fitted->height);
    } else {
        fitted->width =
            destination.width * source_aspect / destination_aspect;
        fitted->x = destination.x +
            0.5F * (destination.width - fitted->width);
    }
    return std::isfinite(fitted->x) && std::isfinite(fitted->y) &&
           std::isfinite(fitted->width) && std::isfinite(fitted->height) &&
           fitted->width > 0.0F && fitted->height > 0.0F;
}

}  // namespace wawvr::xr
