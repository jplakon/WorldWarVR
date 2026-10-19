// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "xr_types.h"

struct IDirect3DDevice9;

namespace wawvr::mod {

// Called only on the established Present/capture owner, before copying the
// current stereo backbuffer. All D3D state/targets are restored before return.
// No overlays are drawn into mono menus, loading screens or physical optics.
void draw_vr_gameplay_prompt_overlay(
    IDirect3DDevice9* device,
    const wawvr::xr::StereoSourceLayout& layout,
    const wawvr::xr::FrameState& frame,
    bool active_stereo_gameplay,
    bool multiplayer) noexcept;

} // namespace wawvr::mod
