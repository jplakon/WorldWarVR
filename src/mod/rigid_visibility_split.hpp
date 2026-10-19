// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "gameplay/rigid_submesh_extraction.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace wawvr::mod {

inline constexpr std::size_t kMaximumRigidVisibilityRanges = 16;

struct RetainedRigidVisibilityRange final {
    std::size_t range_index{};
    std::uint16_t bone_id{};
    std::uint32_t first_vertex{};
    std::uint32_t vertex_count{};
    std::uint32_t triangle_offset{};
    std::uint32_t triangle_count{};
};

struct RigidVisibilitySplitPlan final {
    std::array<RetainedRigidVisibilityRange, kMaximumRigidVisibilityRanges> retained{};
    std::size_t retained_count{};
    std::size_t hidden_range_count{};
    std::uint32_t retained_vertex_count{};
    std::uint32_t retained_triangle_count{};
    std::uint32_t degenerate_trailing_triangle_count{};
};

// Allocation-free exact-surface complement planner. Absent means malformed or
// not applicable (no range belongs to hidden_bone_id); neither may produce a
// companion draw. An all-hidden valid surface yields a plan with zero retained
// ranges. Unowned trailing triangles are accepted ONLY when all three indices
// are equal and in bounds, so a real primitive cannot silently disappear.
[[nodiscard]] std::optional<RigidVisibilitySplitPlan> plan_rigid_visibility_split(
    std::uint32_t surface_vertex_count,
    std::uint32_t surface_triangle_count,
    std::span<const gameplay::RigidSubmeshRange> ordered_rigid_ranges,
    std::uint16_t hidden_bone_id,
    std::span<const std::uint16_t> source_triangle_indices) noexcept;

// All masks and hidden_bone_id must use the SAME 128-bone index space (the
// caller must convert a model-local plan to DObj/global masks if necessary).
// This permits only our owned hide while preserving every unrelated native
// cause of whole-surface suppression. Stateless/idempotent across both eyes.
[[nodiscard]] bool should_render_owned_rigid_complement(
    bool manual_hide_requested,
    bool prior_native_clip_visible,
    std::uint16_t hidden_bone_id,
    const std::array<std::uint32_t, 4>& source_surface_part_bits,
    const std::array<std::uint32_t, 4>& current_native_hidden_bones) noexcept;

} // namespace wawvr::mod
