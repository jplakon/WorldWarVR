#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>

namespace wawvr::mod {

// Retail SP GfxBackEndData keeps its GfxEntity records here. The three retail
// XModel tessellators address an entry as base + index * 3 * 8 and test
// renderFxFlags bit 1 at the start of that 24-byte record.
inline constexpr std::size_t kRetailGfxEntitiesOffset = 0x13B160;
inline constexpr std::size_t kRetailGfxEntityStride = 24;
inline constexpr std::size_t kRetailGfxEntityCount = 128;
inline constexpr std::uint32_t kDepthHackRenderFxFlag = 2;

// Diagnostic-only reversible snapshot used to test whether T4's special
// first-person depth-hack renderer is introducing relative weapon shimmer.
// It intentionally covers only GfxEntity renderFxFlags; draw lists, models,
// cameras, and world geometry remain untouched.
struct RetailDepthHackFlagSnapshot final {
    bool valid{};
    std::uint32_t cleared_count{};
    std::array<std::uint32_t, kRetailGfxEntityCount> render_fx_flags{};
};

// Returns true only for an XModel draw surface whose owning retail GfxEntity
// carries renderFxFlags bit 1 (the depth-hacked first-person viewmodel bit).
// Malformed or incomplete data fails open so a bad filter cannot remove world
// geometry from the normal render.
[[nodiscard]] bool is_depth_hacked_viewmodel_draw_surface(
    std::uint64_t packed_draw_surface,
    std::span<const std::byte> back_end_data) noexcept;

[[nodiscard]] bool clear_retail_depth_hack_flags(
    std::span<std::byte> back_end_data,
    RetailDepthHackFlagSnapshot* snapshot) noexcept;

[[nodiscard]] bool restore_retail_depth_hack_flags(
    std::span<std::byte> back_end_data,
    const RetailDepthHackFlagSnapshot& snapshot) noexcept;

}  // namespace wawvr::mod
