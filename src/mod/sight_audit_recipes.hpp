// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sight_audit_logic.hpp"

namespace wawvr::mod::detail {
struct SightAuditMesh final {
    std::uint32_t surface{},vertices{},triangles{},range_count{},owner_bone{};
    std::uint32_t first_vertex{},owned_vertices{},first_triangle{},owned_triangles{};
};
struct SightAssetRecipe final {
    const char* weapon{};
    const char* model{};
    const char* id{};
    const char* rear_reference{};
    std::uint64_t fingerprint{};
    std::uint32_t bone_count{},surface_count{},front_bone{},rear_bone{},flash_bone{};
    wawvr::xr::Vec3f front_bind{},rear_bind{},flash_bind{};
    std::array<const char*,9> bone_names{};
    std::array<SightAuditMesh,2> meshes{};
    std::uint32_t mesh_count{},rear_count{},floor_count{};
    std::array<wawvr::xr::Vec3f,5> rear{};
    std::array<wawvr::xr::Vec3f,2> front{};
    std::array<wawvr::xr::Vec3f,3> floor{};
    // Appended identity defaults preserve the original three asset paths.
    std::array<float,4> front_bind_orientation{0,0,0,1},rear_bind_orientation{0,0,0,1},flash_bind_orientation{0,0,0,1};
    float front_bind_weight{2},rear_bind_weight{2},flash_bind_weight{2};
};
// Exact Float32 asset positions/topology, independently inspected offline.
// No value here is an aim offset or a declared intended zero distance.
inline constexpr SightAssetRecipe kGarandSightRecipe{
    "m1garand","viewmodel_usa_m1garand_rifle","garand-mesh-v1","aperture-center",
    0xF65F992B98170A63ULL,7,6,6,6,5,
    {5.621877193450928F,-.409855991601944F,.844623982906342F},
    {5.621877193450928F,-.409855991601944F,.844623982906342F},
    {27.8524188995361F,0,1.52065205574036F},
    {"j_gun","j_bolt","j_clip","j_stripper","tag_brass","tag_flash","tag_iron_sights"},
    {{{0,231,148,3,6,145,86,89,58},{3,249,166,1,6,0,249,0,166}}},2,3,0,
    {{{-5.76F,.00221763405F,2.20540371309F},{-5.72F,.00225404504F,2.20598795961F},
      {-5.68F,.00223448194F,2.20671356438F}}},
    {{{25.7279968262F,0,2.64245700836F},{26.2219810486F,0,2.73525595665F}}},{},
};
inline constexpr SightAssetRecipe kMosinSightRecipe{
    "mosin_rifle","viewmodel_rus_mosinnagant_rifle","mosin-notch-mesh-v1",
    "flush-post-top-shoulders-unverified",0x0F3A6AFAFF69027FULL,9,7,0,0,5,
    {},{},{28.704195022583008F,0,1.0821759700775146F},
    {"j_gun","j_bolt","j_bolt1","j_clip","tag_brass","tag_flash","tag_round1","tag_round2","tag_stripper"},
    {{{5,1890,1772,1,0,0,1890,0,1772},{}}},1,5,3,
    {{{5.770887851715088F,0,1.9776870012283325F},{5.778564929962158F,0,1.9852850437164307F},
      {5.77876091003418F,0,1.9774110317230225F},{5.8668718338012695F,0,1.9822020530700684F},
      {5.876163005828857F,0,1.9741829633712769F}}},
    {{{26.11391830444336F,0,1.9076509475708008F},{26.445377349853516F,0,2.0448639392852783F}}},
    {{{5.774785995483398F,0,1.8960020542144775F},{5.780296802520752F,0,1.9052499532699585F},
      {5.884666919708252F,0,1.9280129671096802F}}},
};
inline constexpr SightAssetRecipe kSvtSightRecipe{
    "svt40","viewmodel_rus_svt40_rifle","svt-notch-mesh-v1",
    "flush-post-top-shoulders-unverified",0xCB9A2C0EF0FB6399ULL,6,5,5,0,4,
    {21.955232620239258F,.1994370073080063F,1.1602970361709595F},{},
    {28.03276824951172F,.1994370073080063F,1.228916049003601F},
    {"j_gun","j_bolt","j_clip","tag_brass","tag_flash","tag_iron_sights"},
    {{{0,108,86,1,5,0,108,0,86},{2,1919,1862,1,0,0,1919,0,1862}}},2,4,2,
    {{{-.7054184973239899F,.003995001316070557F,2.095033884048462F},
      {-.69303297996521F,.00299149751663208F,2.1213455200195312F},
      {-.42703500390052795F,.003995001316070557F,2.0934715270996094F},
      {-.42703500390052795F,.00299149751663208F,2.1213455200195312F}}},
    {{{21.75542640686035F,.000382499769330025F,2.3203349113464355F},
      {22.126272201538086F,.000382499769330025F,2.403182029724121F}}},
    {{{-.6829280257225037F,.003995001316070557F,1.9363269805908203F},
      {-.42703500390052795F,.003995001316070557F,1.9601709842681885F}}},
};
// Actual Nacht zombie_colt capture4660EA14...D994E7. Front blade peak,
// rear U-notch INNER shoulders and floor are triangle-backed, not tag/root
// approximations. The flush-top sight picture is independently testable.
inline constexpr SightAssetRecipe kColtSightRecipe{
    "zombie_colt","viewmodel_usa_colt45_pistol","colt-notch-mesh-v1",
    "flush-front-peak-to-inner-notch-shoulders",0x23826C4308A596D6ULL,7,5,1,1,6,
    {-1.952201008796692F,.06773900240659714F,1.572214961051941F},
    {-1.952201008796692F,.06773900240659714F,1.572214961051941F},
    {5.433492183685303F,.0769210010766983F,1.1753129959106445F},
    {"j_gun","j_bolt","j_clip","joint1","joint2","tag_brass","tag_flash"},
    {{{2,1326,1050,1,1,0,1326,0,1050},{}}},1,2,2,
    {{{-1.3563870191574097F,-.07007399946451187F,1.6768449544906616F},
      {-1.3563870191574097F,.03485500067472458F,1.67256498336792F}}},
    {{{5.347758769989014F,-.057395998388528824F,1.6813759803771973F},
      {5.347758769989014F,.01899700053036213F,1.6813759803771973F}}},
    {{{-1.3563870191574097F,-.05775900185108185F,1.6284689903259277F},
      {-1.3563870191574097F,.02249399945139885F,1.6284270286560059F}}},
    {0,-.010223700664937496F,0,.99993896484375F},
    {0,-.010223700664937496F,0,.99993896484375F},{0,0,0,1},
    2.000035047531128F,2.000035047531128F,2.F,
};
[[nodiscard]] inline constexpr const SightAssetRecipe* sight_asset_recipe(SightAuditAsset asset) noexcept {
    switch (asset) {
    case SightAuditAsset::garand:return &kGarandSightRecipe;
    case SightAuditAsset::mosin:return &kMosinSightRecipe;
    case SightAuditAsset::svt:return &kSvtSightRecipe;
    case SightAuditAsset::colt:return &kColtSightRecipe;
    }
    return nullptr;
}
} // namespace wawvr::mod::detail
