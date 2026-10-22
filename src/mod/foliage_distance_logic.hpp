#pragma once

#include <bit>
#include <array>
#include <cstdint>
#include <string_view>

namespace wawvr::mod {

inline constexpr float kFoliageDistanceExtensionMultiplier = 4.0F;
inline constexpr float kMaximumOriginalFoliageTerminalRange = 16000.0F;
inline constexpr float kMaximumExtendedFoliageCullDistance = 8000.0F;
inline constexpr std::uint32_t kMaximumOriginalFoliageTerminalRangeBits = 0x467A0000U;
inline constexpr std::uint32_t kMaximumExtendedFoliageCullDistanceBits = 0x45FA0000U;

static_assert(std::bit_cast<std::uint32_t>(kMaximumOriginalFoliageTerminalRange) ==
              kMaximumOriginalFoliageTerminalRangeBits);
static_assert(std::bit_cast<std::uint32_t>(kMaximumExtendedFoliageCullDistance) ==
              kMaximumExtendedFoliageCullDistanceBits);

// The pel2 inventory and observed native LOD0->1 transition identify exactly
// this composite mesh. This is not a name-only/global foliage LOD setting.
inline constexpr std::string_view kShrubNearModel = "foliage_pacific_forest_shrubs03";
inline constexpr std::array<std::uint32_t,4> kShrubNearRanges{
    std::bit_cast<std::uint32_t>(600.0F),std::bit_cast<std::uint32_t>(1200.0F),
    std::bit_cast<std::uint32_t>(2600.0F),std::bit_cast<std::uint32_t>(8000.0F)};
inline constexpr std::array<std::uint32_t,4> kShrubNearSpans{2U,0x00020002U,0x00040002U,0x00060001U};
inline constexpr std::array<std::uint16_t,7> kShrubNearVertices{241,4230,241,84,54,184,47};
inline constexpr std::array<std::uint16_t,7> kShrubNearTriangles{170,3074,170,28,18,92,24};
inline constexpr std::array<std::string_view,7> kShrubNearMaterials{
    "mc/mtl_foliage_pacific_forest_shrubs01","mc/mtl_foliage_cod5_grass05",
    "mc/mtl_foliage_pacific_forest_shrubs01","mc/mtl_foliage_cod5_grass05a_card",
    "mc/mtl_foliage_cod5_grass05a_card","mc/mtl_foliage_pacific_forest_shrubs01_low",
    "mc/mtl_foliage_pacific_forest_shrubs01_low"};
inline constexpr std::string_view kShrubNearVariantModel="foliage_pacific_forest_shrubs01";
inline constexpr std::array<std::uint32_t,4> kShrubNearVariantRanges{
    std::bit_cast<std::uint32_t>(600.0F),std::bit_cast<std::uint32_t>(1200.0F),
    std::bit_cast<std::uint32_t>(2400.0F),std::bit_cast<std::uint32_t>(10000.0F)};
inline constexpr std::array<std::uint16_t,7> kShrubNearVariantVertices{274,4230,175,84,54,116,52};
inline constexpr std::array<std::uint16_t,7> kShrubNearVariantTriangles{228,3074,104,28,18,58,26};

[[nodiscard]] constexpr bool shrub_near_profile_matches(std::string_view name,
    const std::array<std::uint32_t,4>& ranges,const std::array<std::uint32_t,4>& spans,
    const std::array<std::uint16_t,7>& vertices,const std::array<std::uint16_t,7>& triangles,
    const std::array<std::string_view,7>& materials) noexcept {
    return spans == kShrubNearSpans && materials == kShrubNearMaterials &&
        ((name == kShrubNearModel && ranges == kShrubNearRanges && vertices == kShrubNearVertices && triangles == kShrubNearTriangles) ||
         (name == kShrubNearVariantModel && ranges == kShrubNearVariantRanges && vertices == kShrubNearVariantVertices && triangles == kShrubNearVariantTriangles));
}

[[nodiscard]] constexpr std::uint32_t select_shrub_near_range_bits(
    std::uint32_t original,unsigned lod,bool eligible,bool variant01=false) noexcept {
    const auto& ranges=variant01?kShrubNearVariantRanges:kShrubNearRanges;
    return eligible && lod == 0 && original == ranges[0] ? ranges[3] : original;
}

// This independently proven tree loses every canopy material at native LOD2.
// Keep the lowest leaf-bearing LOD1, never LOD0, until its original LOD-out
// range. Unlike groundcover policy this must not extend any instance cutoff.
inline constexpr std::string_view kCanopyModel="foliage_cod5_tree_mgrve_2";
inline constexpr std::array<std::uint32_t,4> kCanopyRanges{
    std::bit_cast<std::uint32_t>(1000.0F),std::bit_cast<std::uint32_t>(2000.0F),
    std::bit_cast<std::uint32_t>(9750.0F),std::bit_cast<std::uint32_t>(10000.0F)};
inline constexpr std::array<std::uint32_t,4> kCanopySpans{3U,0x00030003U,0x00060001U,0x00070001U};
inline constexpr std::array<std::uint16_t,8> kCanopyVertices{3968,456,33,2849,410,33,2007,1112};
inline constexpr std::array<std::uint16_t,8> kCanopyTriangles{4080,228,32,2538,192,32,1760,992};
inline constexpr std::array<std::string_view,8> kCanopyMaterials{
    "mc/mtl_foliage_cod5_mgrve_bark01a","mc/mtl_foliage_cod5_mgrve_leaf01","mc/mtl_mangrove2_gobo",
    "mc/mtl_foliage_cod5_mgrve_bark01a","mc/mtl_foliage_cod5_mgrve_leaf01","mc/mtl_mangrove2_gobo",
    "mc/mtl_foliage_cod5_mgrve_bark01a","mc/mtl_foliage_cod5_mgrve_bark01a"};

[[nodiscard]] constexpr bool canopy_profile_matches(std::string_view name,
    const std::array<std::uint32_t,4>& ranges,const std::array<std::uint32_t,4>& spans,
    const std::array<std::uint16_t,8>& vertices,const std::array<std::uint16_t,8>& triangles,
    const std::array<std::string_view,8>& materials) noexcept {
    return name==kCanopyModel && ranges==kCanopyRanges && spans==kCanopySpans &&
        vertices==kCanopyVertices && triangles==kCanopyTriangles && materials==kCanopyMaterials;
}

[[nodiscard]] constexpr std::uint32_t select_canopy_range_bits(
    std::uint32_t original,unsigned lod,bool eligible) noexcept {
    return eligible && lod==1 && original==kCanopyRanges[1] ? kCanopyRanges[3] : original;
}

// Full, bounded asset identity. Only an exact profile can enable stable dense
// or leaf-bearing retention; membership in generic groundcover policy cannot.
inline constexpr unsigned kMaximumProfileLods=4,kMaximumProfileSurfaces=16;
enum class FoliageRetention : std::uint8_t { dense,canopy };
struct FullFoliageProfile {
    std::string_view name;
    unsigned lod_count{},surface_count{},retained_lod{};
    FoliageRetention retention{};
    bool active{},allow_groundcover_extension{};
    std::array<std::uint32_t,kMaximumProfileLods> ranges{},spans{};
    std::array<std::uint16_t,kMaximumProfileSurfaces> vertices{},triangles{};
    std::array<std::string_view,kMaximumProfileSurfaces> materials{};
};
template<std::size_t L,std::size_t S>
[[nodiscard]] constexpr FullFoliageProfile make_foliage_profile(std::string_view name,
    const std::array<std::uint32_t,L>& ranges,const std::array<std::uint32_t,L>& spans,
    const std::array<std::uint16_t,S>& vertices,const std::array<std::uint16_t,S>& triangles,
    const std::array<std::string_view,S>& materials,unsigned retained_lod,FoliageRetention retention,
    bool active,bool groundcover) noexcept {
    static_assert(L>0 && L<=kMaximumProfileLods && S>0 && S<=kMaximumProfileSurfaces);
    FullFoliageProfile result{name,static_cast<unsigned>(L),static_cast<unsigned>(S),retained_lod,retention,active,groundcover};
    for(unsigned i=0;i<L;++i){result.ranges[i]=ranges[i];result.spans[i]=spans[i];}
    for(unsigned i=0;i<S;++i){result.vertices[i]=vertices[i];result.triangles[i]=triangles[i];result.materials[i]=materials[i];}
    return result;
}
inline constexpr std::array<std::uint32_t,4> kGrassFourRanges{
    std::bit_cast<std::uint32_t>(600.0F),std::bit_cast<std::uint32_t>(1200.0F),
    std::bit_cast<std::uint32_t>(2000.0F),std::bit_cast<std::uint32_t>(3000.0F)};
inline constexpr std::array<std::uint32_t,3> kGrassThreeRanges{
    std::bit_cast<std::uint32_t>(600.0F),std::bit_cast<std::uint32_t>(1500.0F),std::bit_cast<std::uint32_t>(3000.0F)};
inline constexpr std::array<std::uint32_t,4> kGrassFourSpans{1U,0x00010001U,0x00020001U,0x00030001U};
inline constexpr std::array<std::uint32_t,3> kGrassThreeSpans{1U,0x00010001U,0x00020001U};
inline constexpr std::array<std::uint16_t,4> kGrassFourVertices{2115,42,27,18},kGrassFourTriangles{1538,14,10,6};
inline constexpr std::array<std::uint16_t,3> kGrassThreeVertices{2115,42,27},kGrassThreeTriangles{1538,14,10};
inline constexpr std::array<std::string_view,4> kGrass07OneMaterials{
    "mc/mtl_foliage_cod5_grass05c","mc/mtl_foliage_cod5_grass05c_card",
    "mc/mtl_foliage_cod5_grass05c_card_static","mc/mtl_foliage_cod5_grass05c_card_static"};
inline constexpr std::array<std::string_view,3> kGrass07TwoMaterials{
    "mc/mtl_foliage_cod5_grass05d","mc/mtl_foliage_cod5_grass05d_card","mc/mtl_foliage_cod5_grass05d_card"};
inline constexpr std::array<std::string_view,3> kGrass01Materials{
    "mc/mtl_foliage_cod5_grass05","mc/mtl_foliage_cod5_grass05a_card","mc/mtl_foliage_cod5_grass05a_card"};
inline constexpr std::array<std::uint32_t,4> kCanopyNoShadowSpans{2U,0x00020002U,0x00040001U,0x00050001U};
inline constexpr std::array<std::uint16_t,6> kCanopyNoShadowVertices{3968,456,2849,410,2007,1112};
inline constexpr std::array<std::uint16_t,6> kCanopyNoShadowTriangles{4080,228,2538,192,1760,992};
inline constexpr std::array<std::string_view,6> kCanopyNoShadowMaterials{
    "mc/mtl_foliage_cod5_mgrve_bark01a_low","mc/mtl_foliage_cod5_mgrve_leaf01",
    "mc/mtl_foliage_cod5_mgrve_bark01a_low","mc/mtl_foliage_cod5_mgrve_leaf01",
    "mc/mtl_foliage_cod5_mgrve_bark01a_low","mc/mtl_foliage_cod5_mgrve_bark01a_low"};
// Catalogued terminal canopy-loss identities. The normal mgrve1 near-detail
// experiment overrides its retained index below; other profiles keep LOD2.
inline constexpr std::array<std::uint32_t,4> kMgrveOneSpans{3U,0x00030003U,0x00060002U,0x00080001U};
inline constexpr std::array<std::uint16_t,9> kMgrveOneVertices{4624,600,16,3361,510,16,2361,168,841};
inline constexpr std::array<std::uint16_t,9> kMgrveOneTriangles{4690,300,14,2968,222,14,2058,84,560};
inline constexpr std::array<std::string_view,9> kMgrveOneMaterials{
    "mc/mtl_foliage_cod5_mgrve_bark01a","mc/mtl_foliage_cod5_mgrve_leaf01","mc/mtl_mangrove1_gobo",
    "mc/mtl_foliage_cod5_mgrve_bark01a","mc/mtl_foliage_cod5_mgrve_leaf01","mc/mtl_mangrove1_gobo",
    "mc/mtl_foliage_cod5_mgrve_bark01a","mc/mtl_foliage_cod5_mgrve_leaf01","mc/mtl_foliage_cod5_mgrve_bark01a"};
inline constexpr std::array<std::uint32_t,4> kMgrveOneNoShadowSpans{2U,0x00020002U,0x00040002U,0x00060001U};
inline constexpr std::array<std::uint16_t,7> kMgrveOneNoShadowVertices{4624,600,3361,510,2361,168,841};
inline constexpr std::array<std::uint16_t,7> kMgrveOneNoShadowTriangles{4690,300,2968,222,2058,84,560};
inline constexpr std::array<std::string_view,7> kMgrveOneNoShadowMaterials{
    "mc/mtl_foliage_cod5_mgrve_bark01a_low","mc/mtl_foliage_cod5_mgrve_leaf01",
    "mc/mtl_foliage_cod5_mgrve_bark01a_low","mc/mtl_foliage_cod5_mgrve_leaf01",
    "mc/mtl_foliage_cod5_mgrve_bark01a_low","mc/mtl_foliage_cod5_mgrve_leaf01","mc/mtl_foliage_cod5_mgrve_bark01a_low"};
inline constexpr std::array<std::uint32_t,4> kBirchThreeRanges{
    std::bit_cast<std::uint32_t>(1000.0F),std::bit_cast<std::uint32_t>(2000.0F),
    std::bit_cast<std::uint32_t>(15000.0F),std::bit_cast<std::uint32_t>(16000.0F)};
inline constexpr std::array<std::uint32_t,4> kBirchThreeSpans{3U,0x00030003U,0x00060003U,0x00090002U};
inline constexpr std::array<std::uint16_t,11> kBirchThreeVertices{1009,72,1044,773,65,826,577,48,661,153,30};
inline constexpr std::array<std::uint16_t,11> kBirchThreeTriangles{890,104,540,586,90,394,400,62,306,154,38};
inline constexpr std::array<std::string_view,11> kBirchThreeMaterials{
    "mc/mtl_foliage_cod5_birch_bark01a","mc/mtl_foliage_cod5_birch_bark01b","mc/mtl_foliage_cod5_birch_leaf01",
    "mc/mtl_foliage_cod5_birch_bark01a","mc/mtl_foliage_cod5_birch_bark01b","mc/mtl_foliage_cod5_birch_leaf01",
    "mc/mtl_foliage_cod5_birch_bark01a","mc/mtl_foliage_cod5_birch_bark01b","mc/mtl_foliage_cod5_birch_leaf01",
    "mc/mtl_foliage_cod5_birch_bark01a","mc/mtl_foliage_cod5_birch_bark01b"};
inline constexpr std::array<FullFoliageProfile,11> kFullFoliageProfiles{
    make_foliage_profile(kShrubNearModel,kShrubNearRanges,kShrubNearSpans,kShrubNearVertices,kShrubNearTriangles,kShrubNearMaterials,0,FoliageRetention::dense,true,true),
    make_foliage_profile(kShrubNearVariantModel,kShrubNearVariantRanges,kShrubNearSpans,kShrubNearVariantVertices,kShrubNearVariantTriangles,kShrubNearMaterials,0,FoliageRetention::dense,true,true),
    // Exact clump01 native/card crossing is pixel reproduced in oki3. Other
    // three exact family identities are experimental: scene coverage pending.
    make_foliage_profile("foliage_pacific_grass07_clump01",kGrassFourRanges,kGrassFourSpans,kGrassFourVertices,kGrassFourTriangles,kGrass07OneMaterials,0,FoliageRetention::dense,true,true),
    make_foliage_profile("foliage_pacific_grass07_clump02",kGrassThreeRanges,kGrassThreeSpans,kGrassThreeVertices,kGrassThreeTriangles,kGrass07TwoMaterials,0,FoliageRetention::dense,true,true),
    make_foliage_profile("foliage_pacific_grass01_clump02",kGrassThreeRanges,kGrassThreeSpans,kGrassThreeVertices,kGrassThreeTriangles,kGrass01Materials,0,FoliageRetention::dense,true,true),
    make_foliage_profile("foliage_pacific_grass01_clump04",kGrassThreeRanges,kGrassThreeSpans,kGrassThreeVertices,kGrassThreeTriangles,kGrass01Materials,0,FoliageRetention::dense,true,true),
    make_foliage_profile(kCanopyModel,kCanopyRanges,kCanopySpans,kCanopyVertices,kCanopyTriangles,kCanopyMaterials,1,FoliageRetention::canopy,true,false),
    make_foliage_profile("foliage_cod5_tree_mgrve_2_noshadow",kCanopyRanges,kCanopyNoShadowSpans,kCanopyNoShadowVertices,kCanopyNoShadowTriangles,kCanopyNoShadowMaterials,1,FoliageRetention::canopy,true,false),
    // Tree-only near-detail experiment for the measured pel2 instance9552
    // native LOD1/0 crossing. Keep this exact normal model at LOD0 through its
    // unchanged authored endpoint; no other canopy/fern profile is broadened.
    make_foliage_profile("foliage_cod5_tree_mgrve_1",kCanopyRanges,kMgrveOneSpans,kMgrveOneVertices,kMgrveOneTriangles,kMgrveOneMaterials,0,FoliageRetention::canopy,true,false),
    make_foliage_profile("foliage_cod5_tree_mgrve_1_noshadow",kCanopyRanges,kMgrveOneNoShadowSpans,kMgrveOneNoShadowVertices,kMgrveOneNoShadowTriangles,kMgrveOneNoShadowMaterials,2,FoliageRetention::canopy,true,false),
    make_foliage_profile("foliage_cod5_tree_birch_03",kBirchThreeRanges,kBirchThreeSpans,kBirchThreeVertices,kBirchThreeTriangles,kBirchThreeMaterials,2,FoliageRetention::canopy,true,false)};
[[nodiscard]] constexpr const FullFoliageProfile* find_foliage_profile(std::string_view name,FoliageRetention retention) noexcept {
    for(const auto& profile:kFullFoliageProfiles)
        if(profile.active && profile.name==name && profile.retention==retention) return &profile;
    return nullptr;
}
[[nodiscard]] constexpr bool full_foliage_profile_matches(const FullFoliageProfile& profile,
    std::string_view name,unsigned lod_count,unsigned surface_count,
    const std::array<std::uint32_t,kMaximumProfileLods>& ranges,const std::array<std::uint32_t,kMaximumProfileLods>& spans,
    const std::array<std::uint16_t,kMaximumProfileSurfaces>& vertices,const std::array<std::uint16_t,kMaximumProfileSurfaces>& triangles,
    const std::array<std::string_view,kMaximumProfileSurfaces>& materials) noexcept {
    if(name!=profile.name || lod_count!=profile.lod_count || surface_count!=profile.surface_count ||
        !lod_count || lod_count>kMaximumProfileLods || !surface_count || surface_count>kMaximumProfileSurfaces) return false;
    for(unsigned i=0;i<lod_count;++i)if(ranges[i]!=profile.ranges[i] || spans[i]!=profile.spans[i])return false;
    for(unsigned i=0;i<surface_count;++i)if(vertices[i]!=profile.vertices[i] || triangles[i]!=profile.triangles[i] || materials[i]!=profile.materials[i])return false;
    return true;
}

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
           name.find("hedgerow") != std::string_view::npos ||
           name == "foliage_pacific_brush01" ||
           name == "foliage_pacific_ground_cover01";
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

// Retain one exact dense/complete-leaf LOD through the SAME endpoint that the
// native terminal policy already uses for this instance. Later comparisons
// remain at or below that endpoint, so strict equality still rejects all LODs.
// This removes intermediate swaps; it does not create a new draw-distance cap.
[[nodiscard]] constexpr std::uint32_t select_profile_retained_range_bits(
    const FullFoliageProfile& profile,std::uint32_t original,unsigned lod,
    std::uint32_t instance_cull,bool eligible) noexcept {
    if(!eligible || !positive_normal_foliage_distance_bits(instance_cull) ||
        !profile.lod_count || profile.lod_count>kMaximumProfileLods ||
        profile.retained_lod>=profile.lod_count || lod!=profile.retained_lod ||
        original!=profile.ranges[lod]) return original;
    const auto endpoint=profile.ranges[profile.lod_count-1];
    const bool short_instance=profile.allow_groundcover_extension &&
        positive_normal_foliage_distance_bits(instance_cull) && instance_cull<kMaximumExtendedFoliageCullDistanceBits;
    return select_extended_foliage_terminal_range_bits(endpoint,short_instance);
}

// The runtime must validate the terminal LOD's nonempty surface span and exact
// model/instance ownership before using either distance selector. Only the
// terminal comparison range changes; high-detail transitions remain intact
// except the separately validated exact full-profile retention above.
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
