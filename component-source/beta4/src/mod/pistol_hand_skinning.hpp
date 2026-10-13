// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "xr_types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace wawvr::mod {

struct HandSkinInfluence {
    std::uint16_t bone{};
    std::uint16_t weight{};
};

struct HandSkinBoneTransform {
    bool valid{};
    wawvr::xr::EnginePose bind_pose{};
    wawvr::xr::EnginePose animated_world_pose{};
};

struct HandSkinnedVertex {
    wawvr::xr::Vec3f position{};
    wawvr::xr::Vec3f normal{};
    wawvr::xr::Vec3f tangent{};
};

// Native blend records store the primary matrix byte offset followed by
// secondary (matrix byte offset, weight) pairs. Matrices occupy 64 bytes;
// the primary weight is the exact remainder of the 65535 fixed-point total.
// Extra supplied words belong to later records and are not inspected.
[[nodiscard]] bool decode_pistol_hand_skin_influences(
    std::span<const std::uint16_t> blend_words,
    std::size_t influence_count,
    std::size_t bone_count,
    std::array<HandSkinInfluence, 4>* output) noexcept;

// Bake a bind-space vertex through its animated bone poses, then into the
// selected root's local space. Normals and tangents use rotations only and
// are normalized after blending. No output is modified on any failure.
[[nodiscard]] bool skin_pistol_hand_vertex_to_root(
    wawvr::xr::Vec3f bind_position,
    wawvr::xr::Vec3f normal,
    wawvr::xr::Vec3f tangent,
    std::span<const HandSkinInfluence> influences,
    std::span<const HandSkinBoneTransform> bones,
    wawvr::xr::EnginePose root_world,
    HandSkinnedVertex* output) noexcept;

}  // namespace wawvr::mod
