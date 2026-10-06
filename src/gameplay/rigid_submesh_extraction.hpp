// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace wawvr::gameplay {

// Portable description of one ordered rigid range in an XSurface. Vertex
// ranges are implicit: a rigid range begins immediately after all preceding
// rigid vertex counts. Triangle ranges retain their authored surface offsets.
struct RigidSubmeshRange final {
    std::uint16_t bone_id{};
    std::uint32_t vertex_count{};
    std::uint32_t triangle_offset{};
    std::uint32_t triangle_count{};
};

struct ExtractedRigidSubmesh final {
    std::uint16_t bone_id{};
    std::uint32_t first_vertex{};
    std::uint32_t vertex_count{};
    std::uint32_t triangle_offset{};
    std::uint32_t triangle_count{};
    std::vector<std::uint16_t> triangle_indices{};
};

struct InspectedRigidSubmesh final {
    std::uint16_t bone_id{};
    std::uint32_t first_vertex{};
    std::uint32_t vertex_count{};
    std::uint32_t triangle_offset{};
    std::uint32_t triangle_count{};
};

// Validates the complete ordered rigid topology without allocating. Every
// authored rigid triangle must stay inside its owning vertex range; trailing
// non-rigid triangles are permitted but are never part of the result.
[[nodiscard]] inline std::optional<InspectedRigidSubmesh>
inspect_exact_rigid_submesh(
    const std::uint32_t surface_vertex_count,
    const std::uint32_t surface_triangle_count,
    const std::span<const RigidSubmeshRange> ordered_rigid_ranges,
    const std::size_t selected_rigid_index,
    const std::uint16_t selected_bone_id,
    const std::span<const std::uint16_t> source_triangle_indices) noexcept {
    constexpr std::uint64_t kIndicesPerTriangle = 3U;
    constexpr std::uint64_t kMaximumAddressableVertexCount =
        static_cast<std::uint64_t>(
            (std::numeric_limits<std::uint16_t>::max)()) +
        1U;

    if (surface_vertex_count == 0 || surface_triangle_count == 0 ||
        surface_vertex_count > kMaximumAddressableVertexCount ||
        ordered_rigid_ranges.empty() ||
        selected_rigid_index >= ordered_rigid_ranges.size()) {
        return std::nullopt;
    }

    const std::uint64_t surface_index_count =
        static_cast<std::uint64_t>(surface_triangle_count) *
        kIndicesPerTriangle;
    if (surface_index_count >
            (std::numeric_limits<std::size_t>::max)() ||
        source_triangle_indices.size() !=
            static_cast<std::size_t>(surface_index_count)) {
        return std::nullopt;
    }

    std::uint64_t next_vertex = 0;
    std::uint64_t next_triangle = 0;
    InspectedRigidSubmesh selected{};
    for (std::size_t rigid_index = 0;
         rigid_index < ordered_rigid_ranges.size(); ++rigid_index) {
        const RigidSubmeshRange& range =
            ordered_rigid_ranges[rigid_index];
        if (range.vertex_count == 0 || range.triangle_count == 0 ||
            range.triangle_offset != next_triangle) {
            return std::nullopt;
        }

        const std::uint64_t first_vertex = next_vertex;
        const std::uint64_t next_vertex_end =
            first_vertex + static_cast<std::uint64_t>(range.vertex_count);
        const std::uint64_t next_triangle_end =
            next_triangle + static_cast<std::uint64_t>(range.triangle_count);
        if (next_vertex_end > surface_vertex_count ||
            next_vertex_end > kMaximumAddressableVertexCount ||
            next_triangle_end > surface_triangle_count) {
            return std::nullopt;
        }

        const std::uint64_t first_index =
            next_triangle * kIndicesPerTriangle;
        const std::uint64_t next_index =
            next_triangle_end * kIndicesPerTriangle;
        for (std::uint64_t index = first_index; index < next_index; ++index) {
            const std::uint64_t source_index =
                source_triangle_indices[static_cast<std::size_t>(index)];
            if (source_index < first_vertex ||
                source_index >= next_vertex_end) {
                return std::nullopt;
            }
        }

        if (rigid_index == selected_rigid_index) {
            selected = {
                .bone_id = range.bone_id,
                .first_vertex = static_cast<std::uint32_t>(first_vertex),
                .vertex_count = range.vertex_count,
                .triangle_offset = range.triangle_offset,
                .triangle_count = range.triangle_count,
            };
        }
        next_vertex = next_vertex_end;
        next_triangle = next_triangle_end;
    }

    // Every surface vertex must have exactly one ordered rigid owner. The
    // retail format may retain trailing non-rigid triangles, but never an
    // unowned vertex span or an interior triangle discontinuity.
    if (next_vertex != surface_vertex_count ||
        selected.bone_id != selected_bone_id) {
        return std::nullopt;
    }
    return selected;
}

// Validates one rigid range against the complete ordered surface topology,
// copies only that range's triangles, and rebases their vertex indices to the
// selected range. The result is absent on every mismatch. A trailing surface
// triangle that is not covered by a rigid range is permitted because the
// retail Colt surface contains exactly that layout; gaps or overlaps between
// ordered rigid ranges remain invalid.
[[nodiscard]] inline std::optional<ExtractedRigidSubmesh>
extract_exact_rigid_submesh(
    const std::uint32_t surface_vertex_count,
    const std::uint32_t surface_triangle_count,
    const std::span<const RigidSubmeshRange> ordered_rigid_ranges,
    const std::size_t selected_rigid_index,
    const std::uint16_t selected_bone_id,
    const std::span<const std::uint16_t> source_triangle_indices) noexcept {
    constexpr std::uint64_t kIndicesPerTriangle = 3U;
    const auto inspected = inspect_exact_rigid_submesh(
        surface_vertex_count, surface_triangle_count, ordered_rigid_ranges,
        selected_rigid_index, selected_bone_id, source_triangle_indices);
    if (!inspected.has_value()) {
        return std::nullopt;
    }

    const std::uint64_t selected_first_vertex = inspected->first_vertex;
    const std::uint64_t selected_index_offset =
        static_cast<std::uint64_t>(inspected->triangle_offset) *
        kIndicesPerTriangle;
    const std::uint64_t selected_index_count =
        static_cast<std::uint64_t>(inspected->triangle_count) *
        kIndicesPerTriangle;

    ExtractedRigidSubmesh result{};
    result.bone_id = inspected->bone_id;
    result.first_vertex = inspected->first_vertex;
    result.vertex_count = inspected->vertex_count;
    result.triangle_offset = inspected->triangle_offset;
    result.triangle_count = inspected->triangle_count;

    try {
        result.triangle_indices.reserve(
            static_cast<std::size_t>(selected_index_count));
        const auto first_source_index =
            static_cast<std::size_t>(selected_index_offset);
        for (std::size_t index = 0;
             index < static_cast<std::size_t>(selected_index_count);
             ++index) {
            const std::uint64_t source_index =
                source_triangle_indices[first_source_index + index];
            result.triangle_indices.push_back(
                static_cast<std::uint16_t>(
                    source_index - selected_first_vertex));
        }
    } catch (...) {
        return std::nullopt;
    }

    return result;
}

}  // namespace wawvr::gameplay
