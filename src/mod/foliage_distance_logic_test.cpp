#include "foliage_distance_logic.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using wawvr::mod::foliage_model_is_groundcover;
using wawvr::mod::select_extended_foliage_cull_distance;
using wawvr::mod::select_extended_foliage_cull_distance_bits;
using wawvr::mod::select_extended_foliage_terminal_range;
using wawvr::mod::select_extended_foliage_terminal_range_bits;

void check(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_same_bits(
    const float actual, const float expected, const std::string_view message) {
    check(std::bit_cast<std::uint32_t>(actual) ==
              std::bit_cast<std::uint32_t>(expected),
          message);
}

void test_groundcover_classification() {
    constexpr std::array<std::string_view, 6> accepted{
        "foliage_pacific_grass07_clump01", "foliage_pacific_bush01",
        "foliage_shrub_small", "foliage_fern02", "foliage_hedgerow01",
        "foliage_small_bush_grass",
    };
    for (const auto name : accepted) {
        check(foliage_model_is_groundcover(name), "groundcover name rejected");
    }
    constexpr std::array<std::string_view, 13> rejected{
        "", "grass01", "prop_foliage_grass01", "foliage_rock01",
        "foliage_pacific_tree01", "foliage_pacific_palm01",
        "foliage_bamboo_grass", "foliage_bush_branch01",
        "foliage_snapped_bush", "foliage_grass_tree", "foliage_fern_palm",
        "foliage_hedgerow_branch", "foliage_shrub_snapped",
    };
    for (const auto name : rejected) {
        check(!foliage_model_is_groundcover(name),
              "excluded/non-groundcover name accepted");
    }
}

void test_terminal_range_bounds_and_surface_gate() {
    check(select_extended_foliage_terminal_range(3000.0F, true) == 12000.0F,
          "audited terminal range extends fourfold");
    check(select_extended_foliage_terminal_range(16000.0F, true) == 64000.0F,
          "maximum eligible terminal range extends fourfold");
    check_same_bits(select_extended_foliage_terminal_range(3000.0F, false),
                    3000.0F, "invalid terminal surface keeps original range");
    const float above_limit = std::nextafter(
        16000.0F, std::numeric_limits<float>::infinity());
    check_same_bits(select_extended_foliage_terminal_range(above_limit, true),
                    above_limit, "range above eligibility limit is preserved");
    constexpr std::array<float, 5> originals{
        0.25F, 600.0F, 1200.0F, 3000.0F, 16000.0F,
    };
    for (const float original : originals) {
        check(select_extended_foliage_terminal_range(original, true) >= original,
              "terminal range extension must be monotonic");
    }
}

void test_cull_cap_and_existing_long_distances() {
    check(select_extended_foliage_cull_distance(100.0F) == 400.0F,
          "small authored instance cutoff extends fourfold");
    check(select_extended_foliage_cull_distance(500.0F) == 2000.0F,
          "fourfold cutoff reaches cap exactly");
    check(select_extended_foliage_cull_distance(600.0F) == 2000.0F,
          "cutoff extension is capped");
    constexpr std::array<float, 7> originals{
        0.25F, 100.0F, 500.0F, 600.0F, 1999.0F, 2000.0F, 20000.0F,
    };
    for (const float original : originals) {
        const float selected = select_extended_foliage_cull_distance(original);
        check(selected >= original, "cull policy must never reduce a cutoff");
        if (original >= 2000.0F) {
            check_same_bits(selected, original,
                            "already-long instance cutoff is preserved");
        } else {
            check(selected <= 2000.0F, "extended cutoff exceeds cap");
        }
    }
}

void test_invalid_distances_are_preserved() {
    constexpr std::array<std::uint32_t, 8> invalid_bits{
        0x00000000U, 0x80000000U, 0xBF800000U, 0x7F800000U,
        0xFF800000U, 0x7FC12345U, 0xFFC12345U, 0x7F7FFFFFU,
    };
    for (const auto bits : invalid_bits) {
        const float original = std::bit_cast<float>(bits);
        check_same_bits(select_extended_foliage_terminal_range(original, true),
                        original, "invalid terminal range changed");
        check_same_bits(select_extended_foliage_cull_distance(original), original,
                        "invalid/already-large cull distance changed");
    }
}

void test_bit_selectors_preserve_all_ineligible_values() {
    constexpr std::array<std::uint32_t, 16> values{
        0x00000000U, 0x80000000U, 0x00000001U, 0x007FFFFFU,
        0x80000001U, 0x807FFFFFU, 0x00800000U, 0x3F800000U,
        0x453B8000U, 0x467A0000U, 0x7F7FFFFFU, 0x7F800000U,
        0xFF800000U, 0x7F800001U, 0x7FC12345U, 0xFFC12345U,
    };
    for (const auto bits : values) {
        check(select_extended_foliage_terminal_range_bits(bits, false) == bits,
              "ineligible terminal selector changed source bits");
        check(select_extended_foliage_cull_distance_bits(bits, false) == bits,
              "ineligible cull selector changed source bits");
    }
}

void test_active_bit_selectors_preserve_non_normal_values() {
    constexpr std::array<std::uint32_t, 15> values{
        0x00000000U, 0x80000000U, 0x00000001U, 0x007FFFFFU,
        0x80000001U, 0x807FFFFFU, 0x80800000U, 0xBF800000U,
        0x7F800000U, 0xFF800000U, 0x7F800001U, 0x7FC12345U,
        0xFF800001U, 0xFFC12345U, 0x7F7FFFFFU,
    };
    for (const auto bits : values) {
        check(select_extended_foliage_terminal_range_bits(bits, true) == bits,
              "terminal selector changed signed/subnormal/NaN/inf/large bits");
        check(select_extended_foliage_cull_distance_bits(bits, true) == bits,
              "cull selector changed signed/subnormal/NaN/inf/large bits");
    }
}

void test_normal_bit_selector_boundaries_and_float_equivalence() {
    check(select_extended_foliage_terminal_range_bits(0x00800000U, true) ==
              0x01800000U &&
              select_extended_foliage_cull_distance_bits(0x00800000U, true) ==
                  0x01800000U,
          "smallest positive normal extends with an exact exponent shift");
    check(select_extended_foliage_terminal_range_bits(0x467A0000U, true) ==
              0x477A0000U &&
              select_extended_foliage_terminal_range_bits(0x467A0001U, true) ==
                  0x467A0001U,
          "terminal eligibility bound preserves its first excluded float");
    check(select_extended_foliage_cull_distance_bits(0x43F9FFFFU, true) ==
              0x44F9FFFFU &&
              select_extended_foliage_cull_distance_bits(0x43FA0000U, true) ==
                  0x44FA0000U &&
              select_extended_foliage_cull_distance_bits(0x43FA0001U, true) ==
                  0x44FA0000U,
          "cull cap covers the floats below, at, and above its fourfold source");
    check(select_extended_foliage_cull_distance_bits(0x44F9FFFFU, true) ==
              0x44FA0000U &&
              select_extended_foliage_cull_distance_bits(0x44FA0000U, true) ==
                  0x44FA0000U &&
              select_extended_foliage_cull_distance_bits(0x44FA0001U, true) ==
                  0x44FA0001U,
          "cull values already at or above the cap are preserved");
    constexpr std::array<float, 8> normal_values{
        0.25F, 1.0F, 600.0F, 1200.0F, 1500.0F, 3000.0F, 5000.0F, 16000.0F,
    };
    for (const float original : normal_values) {
        const auto bits = std::bit_cast<std::uint32_t>(original);
        check(select_extended_foliage_terminal_range_bits(bits, true) ==
                  std::bit_cast<std::uint32_t>(original * 4.0F),
              "normal terminal bits differ from exact fourfold multiplication");
        const float expected_cull = original >= 2000.0F ? original
            : original * 4.0F < 2000.0F ? original * 4.0F : 2000.0F;
        check(select_extended_foliage_cull_distance_bits(bits, true) ==
                  std::bit_cast<std::uint32_t>(expected_cull),
              "normal cull bits differ from the bounded float policy");
    }
}

// Equivalent to the verified native first-strictly-greater range selection;
// terminal range equality rejects rather than returning the last LOD.
int native_lod_for_distance(
    const std::span<const float> ranges, const float distance) {
    for (std::size_t lod = 0; lod < ranges.size(); ++lod) {
        if (ranges[lod] > distance) {
            return static_cast<int>(lod);
        }
    }
    return -1;
}

void test_near_lod_equivalence_and_far_extension() {
    constexpr std::array<float, 1> zero_range{0.0F};
    check(native_lod_for_distance(zero_range, 0.0F) == -1,
          "exact native inline selector has no zero-range fallback");
    constexpr std::array<float, 4> original{600.0F, 1200.0F, 2000.0F, 3000.0F};
    auto extended = original;
    extended.back() = select_extended_foliage_terminal_range(original.back(), true);
    for (std::size_t lod = 0; lod + 1U < original.size(); ++lod) {
        check_same_bits(extended[lod], original[lod],
                        "high-detail transition range changed");
    }
    constexpr std::array<float, 9> near_distances{
        0.0F, 599.0F, 600.0F, 1199.0F, 1200.0F, 1999.0F,
        2000.0F, 2500.0F, 2999.0F,
    };
    for (const float distance : near_distances) {
        check(native_lod_for_distance(original, distance) ==
                  native_lod_for_distance(extended, distance),
              "near-camera LOD selection changed");
    }
    check(native_lod_for_distance(original, 3000.0F) == -1 &&
              native_lod_for_distance(extended, 3000.0F) == 3,
          "old terminal equality now selects only the last LOD");
    check(native_lod_for_distance(extended, 11999.0F) == 3 &&
              native_lod_for_distance(extended, 12000.0F) == -1,
          "extended terminal cutoff retains native strict boundary");

    constexpr float placement_scale = 0.2F;
    const float original_cull = original.back() * placement_scale;
    const float extended_cull =
        select_extended_foliage_cull_distance(original_cull);
    check(original_cull == 600.0F && extended_cull == 2000.0F,
          "short instance cutoff extends within the performance cap");
    check(599.0F < original_cull && 599.0F < extended_cull,
          "near instance visibility is preserved");
    check(!(600.0F < original_cull) && 600.0F < extended_cull &&
              native_lod_for_distance(extended, 600.0F / placement_scale) == 3,
          "newly visible far instance has a valid terminal LOD");
    check(!(2000.0F < extended_cull) &&
              native_lod_for_distance(extended, 2000.0F / placement_scale) == 3,
          "instance cap rejects before the more distant terminal LOD cutoff");
    check(native_lod_for_distance(extended, 2400.0F / placement_scale) == -1,
          "terminal LOD retains the exact scaled rejection boundary");
}

void test_terminal_extension_requires_short_instance_cutoff() {
    constexpr auto terminal_bits = std::bit_cast<std::uint32_t>(3000.0F);
    constexpr std::array<float, 4> culls{600.0F, 1999.0F, 2000.0F, 3000.0F};
    for (const float cull : culls) {
        const auto cull_bits = std::bit_cast<std::uint32_t>(cull);
        const bool eligible = wawvr::mod::positive_normal_foliage_distance_bits(cull_bits) &&
            cull_bits < wawvr::mod::kMaximumExtendedFoliageCullDistanceBits;
        const auto selected =
            select_extended_foliage_terminal_range_bits(terminal_bits, eligible);
        check(selected == (cull < 2000.0F
                              ? std::bit_cast<std::uint32_t>(12000.0F) : terminal_bits),
              "terminal extension escaped the short-instance cutoff gate");
    }
}

} // namespace

int main() {
    try {
        test_groundcover_classification();
        test_terminal_range_bounds_and_surface_gate();
        test_cull_cap_and_existing_long_distances();
        test_invalid_distances_are_preserved();
        test_bit_selectors_preserve_all_ineligible_values();
        test_active_bit_selectors_preserve_non_normal_values();
        test_normal_bit_selector_boundaries_and_float_equivalence();
        test_near_lod_equivalence_and_far_extension();
        test_terminal_extension_requires_short_instance_cutoff();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
