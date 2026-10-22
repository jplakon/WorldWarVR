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
    constexpr std::array<std::string_view, 8> accepted{
        "foliage_pacific_grass07_clump01", "foliage_pacific_bush01",
        "foliage_shrub_small", "foliage_fern02", "foliage_hedgerow01",
        "foliage_small_bush_grass",
        "foliage_pacific_brush01", "foliage_pacific_ground_cover01",
    };
    for (const auto name : accepted) {
        check(foliage_model_is_groundcover(name), "groundcover name rejected");
    }
    constexpr std::array<std::string_view, 28> rejected{
        "", "grass01", "prop_foliage_grass01", "foliage_rock01",
        "foliage_pacific_tree01", "foliage_pacific_palm01",
        "foliage_bamboo_grass", "foliage_bush_branch01",
        "foliage_snapped_bush", "foliage_grass_tree", "foliage_fern_palm",
        "foliage_hedgerow_branch", "foliage_shrub_snapped",
        "foliage_pacific_brush02", "foliage_pacific_brush010",
        "foliage_pacific_brush01_tree", "foliage_pacific_brush01_palm",
        "foliage_pacific_brush01_branch", "foliage_pacific_brush01_bamboo",
        "foliage_pacific_ground_cover02", "foliage_pacific_ground_cover010",
        "foliage_pacific_ground_cover01_tree", "foliage_pacific_ground_cover01_palm",
        "foliage_pacific_ground_cover01_branch", "foliage_pacific_ground_cover01_snapped",
        "foliage_other_brush01", "foliage_ground_cover01",
        "prop_foliage_pacific_ground_cover01",
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
          "authored 500-unit cutoff extends fourfold");
    check(select_extended_foliage_cull_distance(2000.0F) == 8000.0F,
          "fourfold cutoff reaches cap exactly");
    check(select_extended_foliage_cull_distance(2001.0F) == 8000.0F,
          "cutoff extension is capped");
    check(select_extended_foliage_cull_distance(600.0F) == 2400.0F,
          "previously capped 600-unit cutoff now extends fourfold");
    constexpr std::array<float, 12> originals{
        0.25F, 100.0F, 500.0F, 600.0F, 1999.0F, 2000.0F,
        2001.0F, 7500.0F, 7999.0F, 8000.0F, 8001.0F, 20000.0F,
    };
    for (const float original : originals) {
        const float selected = select_extended_foliage_cull_distance(original);
        check(selected >= original, "cull policy must never reduce a cutoff");
        if (original >= 8000.0F) {
            check_same_bits(selected, original,
                            "already-long instance cutoff is preserved");
        } else {
            check(selected <= 8000.0F, "extended cutoff exceeds cap");
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
    check(select_extended_foliage_cull_distance_bits(0x44F9FFFFU, true) ==
              0x45F9FFFFU &&
              select_extended_foliage_cull_distance_bits(0x44FA0000U, true) ==
                  0x45FA0000U &&
              select_extended_foliage_cull_distance_bits(0x44FA0001U, true) ==
                  0x45FA0000U,
          "cull cap covers the floats below, at, and above its fourfold source");
    check(select_extended_foliage_cull_distance_bits(0x45F9FFFFU, true) ==
              0x45FA0000U &&
              select_extended_foliage_cull_distance_bits(0x45FA0000U, true) ==
                  0x45FA0000U &&
              select_extended_foliage_cull_distance_bits(0x45FA0001U, true) ==
                  0x45FA0001U,
          "cull values already at or above the cap are preserved");
    constexpr std::array<float, 14> normal_values{
        0.25F, 1.0F, 600.0F, 1200.0F, 1500.0F, 1999.0F, 2000.0F,
        2001.0F, 3000.0F, 5000.0F, 7999.0F, 8000.0F, 8001.0F, 16000.0F,
    };
    for (const float original : normal_values) {
        const auto bits = std::bit_cast<std::uint32_t>(original);
        check(select_extended_foliage_terminal_range_bits(bits, true) ==
                  std::bit_cast<std::uint32_t>(original * 4.0F),
              "normal terminal bits differ from exact fourfold multiplication");
        const float expected_cull = original >= 8000.0F ? original
            : original * 4.0F < 8000.0F ? original * 4.0F : 8000.0F;
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
    check(original_cull == 600.0F && extended_cull == 2400.0F,
          "short instance cutoff extends within the performance cap");
    check(599.0F < original_cull && 599.0F < extended_cull,
          "near instance visibility is preserved");
    check(!(600.0F < original_cull) && 600.0F < extended_cull &&
              native_lod_for_distance(extended, 600.0F / placement_scale) == 3,
          "newly visible far instance has a valid terminal LOD");
    check(2399.0F < extended_cull &&
              native_lod_for_distance(extended, 2399.0F / placement_scale) == 3,
          "newly visible instance retains a terminal LOD up to its cutoff");
    check(!(2400.0F < extended_cull) &&
              native_lod_for_distance(extended, 2400.0F / placement_scale) == -1,
          "instance and terminal LOD retain their exact scaled rejection boundary");

    const float capped_cull = select_extended_foliage_cull_distance(16000.0F * placement_scale);
    check(capped_cull == 8000.0F &&
              capped_cull < select_extended_foliage_terminal_range(16000.0F, true) * placement_scale,
          "longer authored terminal range remains bounded by the finite instance cap");
}

void test_terminal_extension_requires_short_instance_cutoff() {
    constexpr auto terminal_bits = std::bit_cast<std::uint32_t>(3000.0F);
    constexpr std::array<float, 9> culls{
        600.0F, 1999.0F, 2000.0F, 2001.0F, 3000.0F, 7500.0F,
        7999.0F, 8000.0F, 8001.0F};
    for (const float cull : culls) {
        const auto cull_bits = std::bit_cast<std::uint32_t>(cull);
        const bool eligible = wawvr::mod::positive_normal_foliage_distance_bits(cull_bits) &&
            cull_bits < wawvr::mod::kMaximumExtendedFoliageCullDistanceBits;
        const auto selected =
            select_extended_foliage_terminal_range_bits(terminal_bits, eligible);
        check(selected == (cull < 8000.0F
                              ? std::bit_cast<std::uint32_t>(12000.0F) : terminal_bits),
              "terminal extension escaped the short-instance cutoff gate");
    }
}

void test_exact_shrub_near_profile() {
    using namespace wawvr::mod;
    check(shrub_near_profile_matches(kShrubNearModel,kShrubNearRanges,kShrubNearSpans,
        kShrubNearVertices,kShrubNearTriangles,kShrubNearMaterials),"audited shrub near profile rejected");
    check(!shrub_near_profile_matches("foliage_pacific_forest_shrubs01",kShrubNearRanges,kShrubNearSpans,
        kShrubNearVertices,kShrubNearTriangles,kShrubNearMaterials),"unprofiled shrub variant admitted");
    auto ranges=kShrubNearRanges;auto spans=kShrubNearSpans;
    auto vertices=kShrubNearVertices;auto triangles=kShrubNearTriangles;auto materials=kShrubNearMaterials;
    for (unsigned i=0;i<4;++i) {
        ranges[i]^=1U;
        check(!shrub_near_profile_matches(kShrubNearModel,ranges,spans,vertices,triangles,materials),"changed range admitted");
        ranges[i]^=1U;spans[i]^=1U;
        check(!shrub_near_profile_matches(kShrubNearModel,ranges,spans,vertices,triangles,materials),"changed span admitted");
        spans[i]^=1U;
        const auto selected=select_shrub_near_range_bits(ranges[i],i,true);
        check(selected==(i==0?ranges[3]:ranges[i]),"dense retention changed terminal or wrong LOD");
        check(select_shrub_near_range_bits(ranges[i],i,false)==ranges[i],"disabled near selector changed input");
        check(select_shrub_near_range_bits(ranges[i]^1U,i,true)==(ranges[i]^1U),"nonexact near source changed");
    }
    for (unsigned i=0;i<7;++i) {
        vertices[i]^=1U;
        check(!shrub_near_profile_matches(kShrubNearModel,ranges,spans,vertices,triangles,materials),"changed vertices admitted");
        vertices[i]^=1U;triangles[i]^=1U;
        check(!shrub_near_profile_matches(kShrubNearModel,ranges,spans,vertices,triangles,materials),"changed triangles admitted");
        triangles[i]^=1U;const auto original=materials[i];materials[i]="mc/unprofiled";
        check(!shrub_near_profile_matches(kShrubNearModel,ranges,spans,vertices,triangles,materials),"changed material admitted");
        materials[i]=original;
    }
    constexpr std::array<std::uint32_t,8> invalid{0U,1U,0x80000000U,0xBF800000U,0x7F800000U,0xFF800000U,0x7F800001U,0x7FC12345U};
    for (const auto value:invalid) for (unsigned lod=0;lod<5;++lod)
        check(select_shrub_near_range_bits(value,lod,true)==value,"near selector changed invalid source bits");
    const std::array<float,4> native{600,1200,2600,8000},extended{8000,1200,2600,8000};
    check(native_lod_for_distance(native,624)==1 && native_lod_for_distance(extended,624)==0,"observed near transition was not corrected");
    check(native_lod_for_distance(native,7999)==3 && native_lod_for_distance(extended,7999)==0 &&
        native_lod_for_distance(native,8000)==-1 && native_lod_for_distance(extended,8000)==-1,"independent near policy changed terminal cutoff");
}

void test_shrub_variant01_and_canopy_profiles() {
    using namespace wawvr::mod;
    check(shrub_near_profile_matches(kShrubNearVariantModel,kShrubNearVariantRanges,kShrubNearSpans,
        kShrubNearVariantVertices,kShrubNearVariantTriangles,kShrubNearMaterials),"exact shrubs01 profile rejected");
    check(!shrub_near_profile_matches(kShrubNearModel,kShrubNearVariantRanges,kShrubNearSpans,
        kShrubNearVariantVertices,kShrubNearVariantTriangles,kShrubNearMaterials),"shrub variant identities crossed");
    auto ranges01=kShrubNearVariantRanges;auto spans01=kShrubNearSpans;
    auto vertices01=kShrubNearVariantVertices;auto triangles01=kShrubNearVariantTriangles;auto materials01=kShrubNearMaterials;
    for (unsigned lod=0;lod<4;++lod) {
        const auto selected=select_shrub_near_range_bits(ranges01[lod],lod,true,true);
        check(selected==(lod==0?ranges01[3]:ranges01[lod]),"shrubs01 dense/terminal selection incorrect");
        ranges01[lod]^=1U;
        check(!shrub_near_profile_matches(kShrubNearVariantModel,ranges01,spans01,vertices01,triangles01,materials01),"changed shrubs01 range admitted");
        ranges01[lod]^=1U;spans01[lod]^=1U;
        check(!shrub_near_profile_matches(kShrubNearVariantModel,ranges01,spans01,vertices01,triangles01,materials01),"changed shrubs01 span admitted");
        spans01[lod]^=1U;
    }
    for (unsigned i=0;i<7;++i) {
        vertices01[i]^=1U;
        check(!shrub_near_profile_matches(kShrubNearVariantModel,ranges01,spans01,vertices01,triangles01,materials01),"changed shrubs01 vertices admitted");
        vertices01[i]^=1U;triangles01[i]^=1U;
        check(!shrub_near_profile_matches(kShrubNearVariantModel,ranges01,spans01,vertices01,triangles01,materials01),"changed shrubs01 triangles admitted");
        triangles01[i]^=1U;const auto original=materials01[i];materials01[i]="mc/unprofiled";
        check(!shrub_near_profile_matches(kShrubNearVariantModel,ranges01,spans01,vertices01,triangles01,materials01),"changed shrubs01 material admitted");
        materials01[i]=original;
    }
    check(!foliage_model_is_groundcover(kCanopyModel),"canopy tree incorrectly gained groundcover policy");
    auto ranges=kCanopyRanges;auto spans=kCanopySpans;auto vertices=kCanopyVertices;
    auto triangles=kCanopyTriangles;auto materials=kCanopyMaterials;
    check(canopy_profile_matches(kCanopyModel,ranges,spans,vertices,triangles,materials),"exact canopy profile rejected");
    for (const auto name:{"foliage_cod5_tree_mgrve_1","foliage_cod5_tree_mgrve_2_noshadow","foliage_cod5_tree_mgrve_20"})
        check(!canopy_profile_matches(name,ranges,spans,vertices,triangles,materials),"unprofiled tree admitted");
    for (unsigned lod=0;lod<4;++lod) {
        check(select_canopy_range_bits(ranges[lod],lod,true)==(lod==1?ranges[3]:ranges[lod]),"canopy changed more than lowest leaf-bearing LOD1");
        check(select_canopy_range_bits(ranges[lod],lod,false)==ranges[lod],"inactive canopy selector changed source");
        ranges[lod]^=1U;
        check(!canopy_profile_matches(kCanopyModel,ranges,spans,vertices,triangles,materials),"changed canopy range admitted");
        check(select_canopy_range_bits(ranges[lod],lod,true)==ranges[lod],"nonexact canopy range changed");
        ranges[lod]^=1U;spans[lod]^=1U;
        check(!canopy_profile_matches(kCanopyModel,ranges,spans,vertices,triangles,materials),"changed canopy span admitted");
        spans[lod]^=1U;
    }
    for (unsigned i=0;i<8;++i) {
        vertices[i]^=1U;
        check(!canopy_profile_matches(kCanopyModel,ranges,spans,vertices,triangles,materials),"changed canopy vertices admitted");
        vertices[i]^=1U;triangles[i]^=1U;
        check(!canopy_profile_matches(kCanopyModel,ranges,spans,vertices,triangles,materials),"changed canopy triangles admitted");
        triangles[i]^=1U;const auto original=materials[i];materials[i]="mc/unprofiled";
        check(!canopy_profile_matches(kCanopyModel,ranges,spans,vertices,triangles,materials),"changed canopy material admitted");
        materials[i]=original;
    }
    constexpr std::array<std::uint32_t,8> invalid{0U,1U,0x80000000U,0xBF800000U,0x7F800000U,0xFF800000U,0x7F800001U,0x7FC12345U};
    for (const auto value:invalid) for (unsigned lod=0;lod<5;++lod)
        check(select_canopy_range_bits(value,lod,true)==value,"canopy changed invalid source bits");
    const std::array<float,4> native{1000,2000,9750,10000},retained{1000,10000,9750,10000};
    for (const float distance:{0.0F,999.0F})
        check(native_lod_for_distance(native,distance)==native_lod_for_distance(retained,distance),"canopy changed native LOD0");
    for (const float distance:{1000.0F,1999.0F,2000.0F,9750.0F,9999.0F})
        check(native_lod_for_distance(retained,distance)==1,"lowest leaf-bearing canopy LOD1 not retained");
    check(native_lod_for_distance(native,2000)==2 && native_lod_for_distance(retained,2000)==1,"observed canopy loss not corrected");
    check(native_lod_for_distance(retained,10000)==-1 && native_lod_for_distance(retained,10001)==-1,"canopy extended original terminal cutoff");
    check(select_extended_foliage_cull_distance_bits(std::bit_cast<std::uint32_t>(7000.0F),false)==std::bit_cast<std::uint32_t>(7000.0F),
        "tree-disabled groundcover policy must keep original instance cull");
}

void test_full_profile_identity_and_endpoints() {
    using namespace wawvr::mod;
    constexpr auto bits=[](float value){return std::bit_cast<std::uint32_t>(value);};
    constexpr std::array<std::uint32_t,12> culls{0U,1U,0x80000000U,0xBF800000U,0x7F800000U,0x7F812345U,
        bits(600.0F),bits(7999.0F),bits(8000.0F),bits(8001.0F),bits(12000.0F),0x007FFFFFU};
    for(const auto& profile:kFullFoliageProfiles) {
        check(profile.lod_count>0 && profile.lod_count<=4 && profile.surface_count>0 && profile.surface_count<=16,
            "full profile exceeds bounded native layout");
        check(find_foliage_profile(profile.name,profile.retention)==&profile,"active exact profile lookup failed");
        check(!find_foliage_profile("foliage_pacific_grass07_clump010",profile.retention),"lookalike full profile admitted");
        auto ranges=profile.ranges,spans=profile.spans;
        auto vertices=profile.vertices,triangles=profile.triangles;auto materials=profile.materials;
        const auto matches=[&](){return full_foliage_profile_matches(profile,profile.name,profile.lod_count,
            profile.surface_count,ranges,spans,vertices,triangles,materials);};
        check(matches(),"full exact profile rejected");
        check(!full_foliage_profile_matches(profile,profile.name,profile.lod_count-1,profile.surface_count,
            ranges,spans,vertices,triangles,materials),"wrong LOD count admitted");
        check(!full_foliage_profile_matches(profile,profile.name,profile.lod_count,profile.surface_count-1,
            ranges,spans,vertices,triangles,materials),"wrong surface count admitted");
        for(unsigned lod=0;lod<profile.lod_count;++lod){
            ranges[lod]^=1U;check(!matches(),"full changed range admitted");ranges[lod]^=1U;
            spans[lod]^=1U;check(!matches(),"full changed span admitted");spans[lod]^=1U;
        }
        for(unsigned i=0;i<profile.surface_count;++i){
            vertices[i]^=1U;check(!matches(),"full changed vertices admitted");vertices[i]^=1U;
            triangles[i]^=1U;check(!matches(),"full changed triangles admitted");triangles[i]^=1U;
            const auto original=materials[i];materials[i]="mc/unprofiled";check(!matches(),"full changed material admitted");materials[i]=original;
        }
        for(const auto cull:culls){
            const bool valid_instance=positive_normal_foliage_distance_bits(cull);
            const bool short_instance=profile.allow_groundcover_extension && positive_normal_foliage_distance_bits(cull) && cull<bits(8000.0F);
            const auto endpoint=select_extended_foliage_terminal_range_bits(profile.ranges[profile.lod_count-1],short_instance);
            std::array<float,4> selected{};
            for(unsigned lod=0;lod<profile.lod_count;++lod){
                auto value=select_profile_retained_range_bits(profile,profile.ranges[lod],lod,cull,true);
                check(value==(valid_instance && lod==profile.retained_lod?endpoint:profile.ranges[lod]),"retention changed wrong comparison or invalid instance");
                check(select_profile_retained_range_bits(profile,profile.ranges[lod],lod,cull,false)==profile.ranges[lod],"disabled full selector changed source");
                check(select_profile_retained_range_bits(profile,profile.ranges[lod]^1U,lod,cull,true)==(profile.ranges[lod]^1U),"nonexact full source changed");
                if(lod+1==profile.lod_count && lod!=profile.retained_lod)value=select_extended_foliage_terminal_range_bits(value,short_instance);
                selected[lod]=std::bit_cast<float>(value);
            }
            const auto select=[&](float distance){for(unsigned i=0;i<profile.lod_count;++i)if(distance<selected[i])return static_cast<int>(i);return -1;};
            const auto end=std::bit_cast<float>(endpoint);
            check(select(end)==-1 && select(end+1)==-1,"retention created endpoint fallback gap/extension");
            if(valid_instance)check(select(end-1)==static_cast<int>(profile.retained_lod),"retained LOD did not reach existing endpoint");
            if(profile.retention==FoliageRetention::canopy){
                check(!profile.allow_groundcover_extension && endpoint==profile.ranges[profile.lod_count-1],"tree gained groundcover endpoint extension");
                check(select(999.0F)==0,"tree retention changed native LOD0");
            }else check(profile.retained_lod==0 && select(0)==0,"dense profile not stable LOD0");
        }
        constexpr std::array<std::uint32_t,8> invalid{0U,1U,0x80000000U,0xBF800000U,0x7F800000U,0xFF800000U,0x7F800001U,0x7FC12345U};
        for(auto value:invalid)for(unsigned lod=0;lod<6;++lod)
            check(select_profile_retained_range_bits(profile,value,lod,bits(6000.0F),true)==value,"full selector changed invalid source");
    }
}

void test_terminal_leaf_loss_profiles() {
    using namespace wawvr::mod;
    constexpr auto bits=[](float value){return std::bit_cast<std::uint32_t>(value);};
    for(const auto name:{"foliage_cod5_tree_mgrve_1_noshadow","foliage_cod5_tree_birch_03"}){
        const auto* profile=find_foliage_profile(name,FoliageRetention::canopy);
        check(profile && profile->lod_count==4 && profile->retained_lod==2 && !profile->allow_groundcover_extension,
            "terminal leaf profile broadened native/tree policy");
        const auto has_leaf=[&](unsigned lod){
            const auto span=profile->spans[lod],first=span>>16,count=span&0xFFFFU;
            check(count && first+count<=profile->surface_count,"terminal leaf profile span invalid");
            for(unsigned i=first;i<first+count;++i)if(profile->materials[i].find("leaf")!=std::string_view::npos)return true;
            return false;
        };
        check(has_leaf(0) && has_leaf(1) && has_leaf(2) && !has_leaf(3),"retained index is not lowest leaf-bearing LOD");
        const auto endpoint=profile->ranges[3];
        for(const auto cull:{bits(600.0F),bits(7000.0F),bits(8000.0F),bits(16000.0F)}){
            std::array<float,4> selected{},native{};
            for(unsigned lod=0;lod<4;++lod){
                native[lod]=std::bit_cast<float>(profile->ranges[lod]);
                const auto result=select_profile_retained_range_bits(*profile,profile->ranges[lod],lod,cull,true);
                check(result==(lod==2?endpoint:profile->ranges[lod]),"terminal leaf profile changed earlier LOD/endpoint");
                selected[lod]=std::bit_cast<float>(result);
            }
            for(const auto distance:{0.0F,999.0F,1000.0F,1999.0F})
                check(native_lod_for_distance(native,distance)==native_lod_for_distance(selected,distance),"terminal leaf profile changed native near LOD");
            check(native_lod_for_distance(native,native[2])==3 && native_lod_for_distance(selected,native[2])==2,
                "terminal leaf removal still selects bark-only LOD");
            check(native_lod_for_distance(selected,native[3]-1)==2 && native_lod_for_distance(selected,native[3])==-1,
                "terminal leaf profile did not preserve strict authored endpoint");
            check(select_extended_foliage_cull_distance_bits(cull,profile->allow_groundcover_extension)==cull,
                "terminal leaf tree gained instance cull extension");
        }
    }
}

void test_mgrve1_tree_only_near_retention() {
    using namespace wawvr::mod;
    constexpr auto bits=[](float value){return std::bit_cast<std::uint32_t>(value);};
    const auto* profile=find_foliage_profile("foliage_cod5_tree_mgrve_1",FoliageRetention::canopy);
    check(profile && profile->active && profile->retained_lod==0 && !profile->allow_groundcover_extension,
        "normal mgrve1 tree-only detail experiment not exact/default deny");
    constexpr auto previous=make_foliage_profile("foliage_cod5_tree_mgrve_1",kCanopyRanges,kMgrveOneSpans,
        kMgrveOneVertices,kMgrveOneTriangles,kMgrveOneMaterials,2,FoliageRetention::canopy,true,false);
    check(profile->name==previous.name && profile->lod_count==previous.lod_count && profile->surface_count==previous.surface_count &&
        profile->retention==previous.retention && profile->active==previous.active &&
        profile->allow_groundcover_extension==previous.allow_groundcover_extension && profile->ranges==previous.ranges &&
        profile->spans==previous.spans && profile->vertices==previous.vertices && profile->triangles==previous.triangles &&
        profile->materials==previous.materials,"tree-only experiment changed asset identity or policy beyond retained index");
    constexpr std::array<unsigned,11> retained_indices{0,0,0,0,0,0,1,1,0,2,2};
    check(kFullFoliageProfiles.size()==retained_indices.size(),"tree-only experiment added other profiles");
    for(unsigned i=0;i<retained_indices.size();++i)
        check(kFullFoliageProfiles[i].retained_lod==retained_indices[i],"tree-only experiment broadened an untouched profile");
    check(!find_foliage_profile("foliage_cod5_tree_mgrve_10",FoliageRetention::canopy) &&
        !find_foliage_profile("foliage_cod5_tree_mgrve_1x",FoliageRetention::canopy) &&
        !find_foliage_profile("foliage_pacific_fern01",FoliageRetention::dense) &&
        !find_foliage_profile("foliage_pacific_fern02",FoliageRetention::dense),"tree-only experiment admitted lookalike or fern");
    const std::array<float,4> native{1000,2000,9750,10000};
    for(const auto cull:{bits(600.0F),bits(6500.0F),bits(7000.0F),bits(8000.0F),bits(16000.0F)}){
        std::array<float,4> selected{};
        for(unsigned lod=0;lod<4;++lod){
            const auto result=select_profile_retained_range_bits(*profile,profile->ranges[lod],lod,cull,true);
            check(result==(lod==0?bits(10000.0F):profile->ranges[lod]),"normal mgrve1 changed wrong comparison or endpoint");
            check(select_profile_retained_range_bits(*profile,profile->ranges[lod],lod,cull,false)==profile->ranges[lod],
                "disabled normal mgrve1 experiment changed source");
            selected[lod]=std::bit_cast<float>(result);
        }
        for(const auto distance:{0.0F,999.0F,1000.0F,1999.0F,2000.0F,9749.0F,9750.0F,9999.0F})
            check(native_lod_for_distance(selected,distance)==0,"normal mgrve1 still changes detail inside authored endpoint");
        check(native_lod_for_distance(selected,10000)==-1 && native_lod_for_distance(selected,10001)==-1,
            "normal mgrve1 created strict endpoint fallback gap or extension");
        constexpr std::array<float,3> normalized_samples{1056.58F,965.16F,1056.58F};
        constexpr std::array<int,3> previous_indices{1,0,1};
        for(unsigned i=0;i<normalized_samples.size();++i){
            check(native_lod_for_distance(native,normalized_samples[i])==previous_indices[i],"recorded near crossing reference incorrect");
            check(native_lod_for_distance(selected,normalized_samples[i])==0,"tree-only experiment did not pin measured crossing");
        }
        check(select_extended_foliage_cull_distance_bits(cull,profile->allow_groundcover_extension)==cull,
            "normal mgrve1 gained an instance cull extension");
    }
    for(const auto& other:kFullFoliageProfiles){
        if(other.retention!=FoliageRetention::canopy || other.name==profile->name)continue;
        std::array<float,4> selected{};
        for(unsigned lod=0;lod<4;++lod)selected[lod]=std::bit_cast<float>(
            select_profile_retained_range_bits(other,other.ranges[lod],lod,bits(6500.0F),true));
        check(select_profile_retained_range_bits(other,other.ranges[0],0,bits(6500.0F),true)==other.ranges[0],
            "normal mgrve1 experiment changed another canopy's native LOD0 range");
        const auto boundary=std::bit_cast<float>(other.ranges[0]);
        check(native_lod_for_distance(selected,boundary-1)==0 && native_lod_for_distance(selected,boundary)==1,
            "untouched canopy near LOD0/1 transition changed");
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
        test_exact_shrub_near_profile();
        test_shrub_variant01_and_canopy_profiles();
        test_full_profile_identity_and_endpoints();
        test_terminal_leaf_loss_profiles();
        test_mgrve1_tree_only_near_retention();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
