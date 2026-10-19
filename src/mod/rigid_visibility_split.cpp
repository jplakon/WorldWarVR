// SPDX-License-Identifier: GPL-3.0-only
#include "rigid_visibility_split.hpp"

#include <limits>

namespace wawvr::mod {

std::optional<RigidVisibilitySplitPlan> plan_rigid_visibility_split(
    const std::uint32_t surface_vertex_count,
    const std::uint32_t surface_triangle_count,
    const std::span<const gameplay::RigidSubmeshRange> ranges,
    const std::uint16_t hidden_bone_id,
    const std::span<const std::uint16_t> source_triangle_indices) noexcept {
    if (hidden_bone_id >= 128 || ranges.empty() ||
        ranges.size() > kMaximumRigidVisibilityRanges ||
        surface_triangle_count > (std::numeric_limits<std::uint16_t>::max)()) {
        return std::nullopt;
    }
    RigidVisibilitySplitPlan plan{};
    std::uint32_t owned_triangle_count{};
    for (std::size_t index = 0; index < ranges.size(); ++index) {
        const auto& range = ranges[index];
        if (range.bone_id >= 128) return std::nullopt;
        const auto exact = gameplay::inspect_exact_rigid_submesh(
            surface_vertex_count, surface_triangle_count, ranges, index,
            range.bone_id, source_triangle_indices);
        if (!exact.has_value()) return std::nullopt;
        owned_triangle_count += exact->triangle_count;
        if (range.bone_id == hidden_bone_id) {
            ++plan.hidden_range_count;
        } else {
            plan.retained[plan.retained_count++] = {
                .range_index = index,
                .bone_id = exact->bone_id,
                .first_vertex = exact->first_vertex,
                .vertex_count = exact->vertex_count,
                .triangle_offset = exact->triangle_offset,
                .triangle_count = exact->triangle_count,
            };
            plan.retained_vertex_count += exact->vertex_count;
            plan.retained_triangle_count += exact->triangle_count;
        }
    }
    if (plan.hidden_range_count == 0) return std::nullopt;
    // inspect_exact_rigid_submesh accepts a retail padding tail. A complement
    // cannot discard arbitrary non-rigid geometry, so prove its degeneracy.
    for (std::uint32_t triangle = owned_triangle_count;
         triangle < surface_triangle_count; ++triangle) {
        const auto offset = static_cast<std::size_t>(triangle) * 3;
        const auto a = source_triangle_indices[offset];
        const auto b = source_triangle_indices[offset + 1];
        const auto c = source_triangle_indices[offset + 2];
        if (a >= surface_vertex_count || a != b || b != c) {
            return std::nullopt;
        }
        ++plan.degenerate_trailing_triangle_count;
    }
    return plan;
}

bool should_render_owned_rigid_complement(
    const bool manual_hide_requested,
    const bool prior_native_clip_visible,
    const std::uint16_t hidden_bone_id,
    const std::array<std::uint32_t, 4>& source_surface_part_bits,
    const std::array<std::uint32_t, 4>& current_native_hidden_bones) noexcept {
    if (!manual_hide_requested || !prior_native_clip_visible ||
        hidden_bone_id >= 128) return false;
    const auto word = static_cast<std::size_t>(hidden_bone_id >> 5);
    const auto mask = 0x80000000U >> (hidden_bone_id & 31U);
    if ((source_surface_part_bits[word] & mask) == 0 ||
        (current_native_hidden_bones[word] & mask) == 0) return false;
    for (std::size_t index = 0; index < source_surface_part_bits.size(); ++index) {
        const auto owned = index == word ? mask : 0U;
        if ((source_surface_part_bits[index] &
             current_native_hidden_bones[index] & ~owned) != 0) return false;
    }
    return true;
}

} // namespace wawvr::mod
