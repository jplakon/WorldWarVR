#pragma once

#include <cstdint>

namespace wawvr::mod {

// T4 multiplies tanHalfFovY by 2.118673086 when it builds the rigid and
// skinned-model LOD ramps.  This is the single-precision reciprocal of that
// factor, so selecting it makes the effective stereo LOD distance scale 1.0.
// Keeping the correction at this renderer-only seam leaves the real eye
// projection and the user's archived r_lodScale dvars untouched.
inline constexpr std::uint32_t kNeutralLodTanHalfFovYBits = 0x3EF1A923U;

// Implements the exact ordered comparison used by the verified stereo LOD
// path without converting the source bits. Positive finite values above the
// stock-neutral LOD FOV and positive infinity clamp; NaNs, signed values, and
// smaller positive values survive bit-for-bit. The inactive path is an
// unconditional identity transform.
[[nodiscard]] constexpr std::uint32_t select_lod_tan_half_fov_y_bits(
    const std::uint32_t source_bits, const bool stereo_scene_active) noexcept {
    constexpr std::uint32_t kSignBit = 0x80000000U;
    constexpr std::uint32_t kPositiveInfinityBits = 0x7F800000U;
    const std::uint32_t magnitude = source_bits & ~kSignBit;
    if (stereo_scene_active && (source_bits & kSignBit) == 0 &&
        magnitude > kNeutralLodTanHalfFovYBits &&
        magnitude <= kPositiveInfinityBits) {
        return kNeutralLodTanHalfFovYBits;
    }
    return source_bits;
}

} // namespace wawvr::mod
