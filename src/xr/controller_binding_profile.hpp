// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "xr_types.h"

namespace wawvr::xr {

// Keep a semantic label on any uncertainty. This function affects only
// printed guidance, never action ownership, controller poses or input state.
[[nodiscard]] constexpr ControllerBindingProfile verified_controller_binding_profile(
    const ControllerBindingProfile reported_profile,
    const bool primary_source_matches,
    const bool secondary_source_matches) noexcept {
    return primary_source_matches && secondary_source_matches &&
        (reported_profile == ControllerBindingProfile::touch ||
         reported_profile == ControllerBindingProfile::index)
        ? reported_profile : ControllerBindingProfile::generic;
}

} // namespace wawvr::xr
