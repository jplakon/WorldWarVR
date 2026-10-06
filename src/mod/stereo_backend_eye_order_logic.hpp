#pragma once

#include <array>
#include <cstdint>

namespace wawvr::mod {

struct StereoBackendEyeOrder final {
    std::array<std::uint32_t, 3> view_indices{0u, 1u, 2u};
    std::uint32_t count{};
    bool reversed{};
};

[[nodiscard]] constexpr StereoBackendEyeOrder select_stereo_backend_eye_order(
    const std::uint32_t view_count,
    const bool reverse_requested,
    const bool physical_scope_active) noexcept {
    StereoBackendEyeOrder order{};
    order.count = view_count <= order.view_indices.size()
        ? view_count
        : 0u;
    if (order.count == 2u && reverse_requested && !physical_scope_active) {
        order.view_indices[0] = 1u;
        order.view_indices[1] = 0u;
        order.reversed = true;
    }
    return order;
}

} // namespace wawvr::mod
