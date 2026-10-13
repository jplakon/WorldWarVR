// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "xr_types.h"

namespace wawvr::mod {

// One shared optical definition drives physical quad size, compositor fitting,
// and controller-pointer bar remapping. Keeping it here prevents an apparently
// harmless FOV tweak from desynchronizing visible pixels and native hit tests.
inline constexpr float kMenuPanelDistanceMeters = 2.0F;
inline constexpr float kMenuPanelHalfHorizontalAngleRadians = 0.62F;
inline constexpr float kMenuPanelHalfVerticalAngleRadians = 0.38F;

[[nodiscard]] constexpr wawvr::xr::Fovf menu_panel_comfort_fov() noexcept {
    return {
        .angle_left = -kMenuPanelHalfHorizontalAngleRadians,
        .angle_right = kMenuPanelHalfHorizontalAngleRadians,
        .angle_up = kMenuPanelHalfVerticalAngleRadians,
        .angle_down = -kMenuPanelHalfVerticalAngleRadians,
    };
}

}  // namespace wawvr::mod
