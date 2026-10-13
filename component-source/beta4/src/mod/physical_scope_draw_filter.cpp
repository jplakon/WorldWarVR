#include "physical_scope_draw_filter.hpp"

#include <cstring>
#include <limits>

namespace wawvr::mod {
namespace {

constexpr std::uint32_t kSurfaceTypeXModelRigid = 7;
constexpr std::uint32_t kSurfaceTypeXModelRigidSkinned = 8;
constexpr std::uint32_t kSurfaceTypeXModelSkinned = 9;
constexpr std::size_t kModelSurfaceScale = 4;
constexpr std::size_t kModelSurfaceGfxEntityIndexOffset = 14;
template <typename T>
[[nodiscard]] bool read_at(
    const std::span<const std::byte> bytes,
    const std::size_t offset,
    T* const output) noexcept {
    if (output == nullptr || offset > bytes.size() ||
        sizeof(T) > bytes.size() - offset) {
        return false;
    }
    std::memcpy(output, bytes.data() + offset, sizeof(T));
    return true;
}

template <typename T>
[[nodiscard]] bool write_at(
    const std::span<std::byte> bytes,
    const std::size_t offset,
    const T& value) noexcept {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) {
        return false;
    }
    std::memcpy(bytes.data() + offset, &value, sizeof(T));
    return true;
}

}  // namespace

bool is_depth_hacked_viewmodel_draw_surface(
    const std::uint64_t packed_draw_surface,
    const std::span<const std::byte> back_end_data) noexcept {
    const auto surface_type = static_cast<std::uint32_t>(
        (packed_draw_surface >> 54U) & 0x0FU);
    if (surface_type != kSurfaceTypeXModelRigid &&
        surface_type != kSurfaceTypeXModelRigidSkinned &&
        surface_type != kSurfaceTypeXModelSkinned) {
        return false;
    }

    const auto object_id = static_cast<std::uint32_t>(
        packed_draw_surface & 0xFFFFU);
    if (object_id >
        (std::numeric_limits<std::size_t>::max() -
         kModelSurfaceGfxEntityIndexOffset) /
            kModelSurfaceScale) {
        return false;
    }
    const std::size_t model_surface_offset =
        static_cast<std::size_t>(object_id) * kModelSurfaceScale;
    std::uint16_t gfx_entity_index = 0;
    if (!read_at(
            back_end_data,
            model_surface_offset + kModelSurfaceGfxEntityIndexOffset,
            &gfx_entity_index) ||
        gfx_entity_index == 0 ||
        gfx_entity_index >= kRetailGfxEntityCount) {
        return false;
    }

    const std::size_t entity_offset =
        kRetailGfxEntitiesOffset +
        static_cast<std::size_t>(gfx_entity_index) * kRetailGfxEntityStride;
    std::uint32_t render_fx_flags = 0;
    return read_at(back_end_data, entity_offset, &render_fx_flags) &&
           (render_fx_flags & kDepthHackRenderFxFlag) != 0;
}

bool clear_retail_depth_hack_flags(
    const std::span<std::byte> back_end_data,
    RetailDepthHackFlagSnapshot* const snapshot) noexcept {
    if (snapshot == nullptr) {
        return false;
    }
    *snapshot = {};
    const std::size_t required = kRetailGfxEntitiesOffset +
        kRetailGfxEntityCount * kRetailGfxEntityStride;
    if (back_end_data.size() < required) {
        return false;
    }

    RetailDepthHackFlagSnapshot captured{};
    for (std::size_t index = 0; index < kRetailGfxEntityCount; ++index) {
        const std::size_t offset = kRetailGfxEntitiesOffset +
            index * kRetailGfxEntityStride;
        std::uint32_t flags = 0;
        if (!read_at(
                std::span<const std::byte>{
                    back_end_data.data(), back_end_data.size()},
                offset, &flags)) {
            return false;
        }
        captured.render_fx_flags[index] = flags;
        if ((flags & kDepthHackRenderFxFlag) == 0) {
            continue;
        }
        const std::uint32_t ordinary = flags & ~kDepthHackRenderFxFlag;
        if (!write_at(back_end_data, offset, ordinary)) {
            return false;
        }
        ++captured.cleared_count;
    }
    captured.valid = true;
    *snapshot = captured;
    return true;
}

bool restore_retail_depth_hack_flags(
    const std::span<std::byte> back_end_data,
    const RetailDepthHackFlagSnapshot& snapshot) noexcept {
    const std::size_t required = kRetailGfxEntitiesOffset +
        kRetailGfxEntityCount * kRetailGfxEntityStride;
    if (!snapshot.valid || back_end_data.size() < required) {
        return false;
    }
    for (std::size_t index = 0; index < kRetailGfxEntityCount; ++index) {
        const std::size_t offset = kRetailGfxEntitiesOffset +
            index * kRetailGfxEntityStride;
        if (!write_at(
                back_end_data, offset, snapshot.render_fx_flags[index])) {
            return false;
        }
    }
    return true;
}

}  // namespace wawvr::mod
