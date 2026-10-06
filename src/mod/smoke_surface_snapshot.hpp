// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>

struct IDirect3DDevice9;
struct IDirect3DTexture9;

namespace wawvr::mod {

// One-shot diagnostic captures used only when
// WAWVR_FX_SURFACE_SNAPSHOTS=1. The right-emissive capture arms the matching
// pre-Present capture so both files always describe the same rendered frame.
bool capture_smoke_after_right_emissive(
    IDirect3DDevice9* device) noexcept;
bool capture_smoke_floatz_after_eye(
    IDirect3DDevice9* device,
    IDirect3DTexture9* texture,
    std::uint32_t eye_phase) noexcept;
bool capture_smoke_before_present(IDirect3DDevice9* device) noexcept;

} // namespace wawvr::mod
