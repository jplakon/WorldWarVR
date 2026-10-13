#pragma once

#include <bit>
#include <cstdint>
#include <string_view>

namespace wawvr::mod {

inline constexpr float kFoliageDistanceExtensionMultiplier = 4.0F;
inline constexpr float kMaximumOriginalFoliageTerminalRange = 16000.0F;
inline constexpr float kMaximumExtendedFoliageCullDistance = 2000.0F;
inline constexpr std::uint32_t kMaximumOriginalFoliageTerminalRangeBits = 0x467A0000U;
inline constexpr std::uint32_t kMaximumExtendedFoliageCullDistanceBits = 0x44FA0000U;

static_assert(std::bit_cast<std::uint32_t>(kMaximumOriginalFoliageTerminalRange) ==
              kMaximumOriginalFoliageTerminalRangeBits);
static_assert(std::bit_cast<std::uint32_t>(kMaximumExtendedFoliageCullDistance) ==
              kMaximumExtendedFoliageCullDistanceBits);

[[nodiscard]] constexpr bool foliage_model_is_groundcover(
    const std::string_view name) noexcept {
    if (!name.starts_with("foliage_") ||
        name.find("tree") != std::string_view::npos ||
        name.find("palm") != std::string_view::npos ||
        name.find("bamboo") != std::string_view::npos ||
        name.find("branch") != std::string_view::npos ||
        name.find("snapped") != std::string_view::npos) {
        return false;
    }
    return name.find("grass") != std::string_view::npos ||
           name.find("bush") != std::string_view::npos ||
           name.find("shrub") != std::string_view::npos ||
           name.find("fern") != std::string_view::npos ||
           name.find("hedgerow") != std::string_view::npos;
}

[[nodiscard]] constexpr bool positive_normal_foliage_distance_bits(
    const std::uint32_t bits) noexcept {
    return bits >= 0x00800000U && bits < 0x7F800000U;
}

// Bridge-safe integer selectors. IEEE positive normal floats are ordered by
// their unsigned bits. Multiplying a guarded normal value by four adds two to
// its exponent, with no floating-point instructions or FP status changes.
// Subnormals, signed values, NaNs, infinities, and ineligible inputs retain all
// original bits, including payloads. The bounded source ranges cannot overflow.
[[nodiscard]] constexpr std::uint32_t select_extended_foliage_terminal_range_bits(
    const std::uint32_t original_bits, const bool eligible) noexcept {
    if (!eligible || !positive_normal_foliage_distance_bits(original_bits) ||
        original_bits > kMaximumOriginalFoliageTerminalRangeBits) {
        return original_bits;
    }
    return original_bits + 0x01000000U;
}

[[nodiscard]] constexpr std::uint32_t select_extended_foliage_cull_distance_bits(
    const std::uint32_t original_bits, const bool eligible) noexcept {
    if (!eligible || !positive_normal_foliage_distance_bits(original_bits) ||
        original_bits >= kMaximumExtendedFoliageCullDistanceBits) {
        return original_bits;
    }
    const std::uint32_t extended_bits = original_bits + 0x01000000U;
    return extended_bits < kMaximumExtendedFoliageCullDistanceBits
        ? extended_bits : kMaximumExtendedFoliageCullDistanceBits;
}

// The runtime must validate the terminal LOD's nonempty surface span and exact
// model/instance ownership before using either distance selector. Only the
// terminal comparison range changes; high-detail transitions remain intact.
// These float adapters are for policy checks outside native instruction bridges.
[[nodiscard]] inline float select_extended_foliage_terminal_range(
    const float original, const bool terminal_surface_valid) noexcept {
    return std::bit_cast<float>(select_extended_foliage_terminal_range_bits(
        std::bit_cast<std::uint32_t>(original), terminal_surface_valid));
}

[[nodiscard]] inline float select_extended_foliage_cull_distance(
    const float original) noexcept {
    return std::bit_cast<float>(select_extended_foliage_cull_distance_bits(
        std::bit_cast<std::uint32_t>(original), true));
}

} // namespace wawvr::mod
