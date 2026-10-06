#include "lod_fov_clamp_logic.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using wawvr::mod::kNeutralLodTanHalfFovYBits;
using wawvr::mod::select_lod_tan_half_fov_y_bits;

void check(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void test_inactive_selector_is_bit_exact_identity() {
    constexpr std::array<std::uint32_t, 10> values{
        0x00000000U, 0x80000000U, kNeutralLodTanHalfFovYBits,
        0x3F000000U,
        0x3F800001U, 0x7F7FFFFFU, 0x7F800000U, 0x7FC12345U,
        0xFF800000U, 0xFFC12345U,
    };
    for (const std::uint32_t bits : values) {
        check(select_lod_tan_half_fov_y_bits(bits, false) == bits,
              "inactive selector changed source bits");
    }
}

void test_active_selector_restores_stock_neutral_lod_scale() {
    constexpr float kT4LodFovMultiplier = 2.118673086166382F;
    check(std::bit_cast<float>(kNeutralLodTanHalfFovYBits) *
                  kT4LodFovMultiplier ==
              1.0F,
          "neutral tan-half-FOV must produce an exact unit LOD scale");
    check(select_lod_tan_half_fov_y_bits(0x00000000U, true) == 0x00000000U,
          "positive zero is unchanged");
    check(select_lod_tan_half_fov_y_bits(0x80000000U, true) == 0x80000000U,
          "negative zero is unchanged");
    check(select_lod_tan_half_fov_y_bits(
              kNeutralLodTanHalfFovYBits - 1U, true) ==
              kNeutralLodTanHalfFovYBits - 1U,
          "value below the neutral threshold is unchanged");
    check(select_lod_tan_half_fov_y_bits(
              kNeutralLodTanHalfFovYBits, true) ==
              kNeutralLodTanHalfFovYBits,
          "the neutral threshold is unchanged");
    check(select_lod_tan_half_fov_y_bits(
              kNeutralLodTanHalfFovYBits + 1U, true) ==
              kNeutralLodTanHalfFovYBits,
          "the first float above the neutral threshold clamps");
    check(select_lod_tan_half_fov_y_bits(0x3F000000U, true) ==
              kNeutralLodTanHalfFovYBits,
          "a one-half tangent clamps to the neutral threshold");
    check(select_lod_tan_half_fov_y_bits(0x3F800000U, true) ==
              kNeutralLodTanHalfFovYBits,
          "a unit tangent clamps to the neutral threshold");
    check(select_lod_tan_half_fov_y_bits(0x7F7FFFFFU, true) ==
              kNeutralLodTanHalfFovYBits,
          "largest finite positive float clamps");
    check(select_lod_tan_half_fov_y_bits(0x7F800000U, true) ==
              kNeutralLodTanHalfFovYBits,
          "positive infinity clamps like an ordered comparison");
    check(select_lod_tan_half_fov_y_bits(0xBF800001U, true) == 0xBF800001U,
          "negative values are unchanged");
}

void test_active_selector_preserves_nan_payloads() {
    constexpr std::array<std::uint32_t, 4> nans{
        0x7F800001U, 0x7FC12345U, 0xFF800001U, 0xFFC12345U,
    };
    for (const std::uint32_t bits : nans) {
        check(select_lod_tan_half_fov_y_bits(bits, true) == bits,
              "NaN payload or sign changed");
    }
}

} // namespace

int main() {
    try {
        test_inactive_selector_is_bit_exact_identity();
        test_active_selector_restores_stock_neutral_lod_scale();
        test_active_selector_preserves_nan_payloads();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
