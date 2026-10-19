// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>

namespace wawvr::xr {
// Internal, capture-owned diagnostic transport. Never an OpenXR/native ABI.
// Zero initialization leaves ordinary production capture entirely unchanged.
struct SightPixelCaptureRequest final {
    std::uint64_t frame_id{};
    std::uint64_t pose_serial{};
    std::uint32_t asset{}; // exact observer recipes: 1 Garand, 2 Mosin, 3 SVT, 4 Colt
    std::uint32_t source_width{},source_height{};
};
struct SightPixelCaptureState final { std::uint32_t attempted_assets{}; };
[[nodiscard]] inline std::uint32_t consume_sight_pixel_capture_request(
    const SightPixelCaptureRequest& request, const std::uint64_t source_frame,
    const std::uint32_t source_width,const std::uint32_t source_height,
    const bool enabled, SightPixelCaptureState* const state) noexcept {
    if (!enabled || !state || !source_frame || request.frame_id!=source_frame ||
        !request.pose_serial || request.asset<1 || request.asset>4 ||
        !source_width||!source_height||source_width>16384||source_height>16384||
        request.source_width!=source_width||request.source_height!=source_height) return 0;
    const auto bit=1U<<(request.asset-1U);
    if (state->attempted_assets&bit) return 0;
    // A failed readback is still an attempt: never stall repeatedly on a GPU
    // readback or fill a disk during gameplay.
    state->attempted_assets|=bit;
    return request.asset;
}
} // namespace wawvr::xr
