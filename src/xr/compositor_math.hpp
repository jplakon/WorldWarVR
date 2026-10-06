// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "xr_types.h"

#include <cstdint>

namespace wawvr::xr {

// Fits the selected source rectangle inside an existing normalized
// destination without stretching it. Aspect is evaluated in projection
// tangent space, not swapchain pixels: OpenXR maps the complete swapchain to
// the submitted FOV regardless of the runtime's recommended image extent.
[[nodiscard]] bool fit_source_aspect_viewport(
    const NormalizedRect& source,
    std::uint32_t source_width,
    std::uint32_t source_height,
    float source_aspect_multiplier,
    const NormalizedViewport& destination,
    const Fovf& projection_fov,
    NormalizedViewport* fitted) noexcept;

}  // namespace wawvr::xr
