#pragma once

#include <cstdint>
#include <limits>

namespace wawvr::mod {

struct StereoEmissiveViewport final {
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t width{};
    std::int32_t height{};
};

[[nodiscard]] constexpr bool stereo_emissive_viewport_fits_target(
    const StereoEmissiveViewport& viewport,
    const std::uint32_t target_width,
    const std::uint32_t target_height) noexcept {
    if (viewport.x < 0 || viewport.y < 0 ||
        viewport.width <= 0 || viewport.height <= 0) {
        return false;
    }
    const auto x = static_cast<std::uint32_t>(viewport.x);
    const auto y = static_cast<std::uint32_t>(viewport.y);
    const auto width = static_cast<std::uint32_t>(viewport.width);
    const auto height = static_cast<std::uint32_t>(viewport.height);
    return x <= target_width && y <= target_height &&
           width <= target_width - x &&
           height <= target_height - y;
}

[[nodiscard]] constexpr bool stereo_emissive_viewports_match(
    const StereoEmissiveViewport& expected,
    const StereoEmissiveViewport& actual) noexcept {
    return expected.x == actual.x && expected.y == actual.y &&
           expected.width == actual.width &&
           expected.height == actual.height;
}

[[nodiscard]] constexpr bool should_force_stereo_emissive_viewport(
    const bool requested,
    const std::uint32_t eye_phase,
    const StereoEmissiveViewport& expected,
    const StereoEmissiveViewport& actual,
    const std::uint32_t target_width,
    const std::uint32_t target_height) noexcept {
    return requested && eye_phase != 0u &&
           stereo_emissive_viewport_fits_target(
               expected, target_width, target_height) &&
           !stereo_emissive_viewports_match(expected, actual);
}

} // namespace wawvr::mod
