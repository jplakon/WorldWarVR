// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sight_audit_logic.hpp"

namespace wawvr::mod::detail {
struct SightPixelGeometryAnchor final {
    const char* name{};
    std::uint32_t surface{},vertex{},bone{};
    wawvr::xr::Vec3f local{};
};
using SightPixelGeometryRecipe=std::array<SightPixelGeometryAnchor,8>;
// Actual Float32 mesh vertices, NOT virtual aperture/notch centers. Native
// indices are retained so the frozen geometry captures can independently
// reproduce every value. Existing full-mesh fingerprints gate their use.
// Inputs: analysis/sight-calibration/{garand-metadata10,mosin-static-sights10,
// svt-static-sights10}.json. No point here is an aiming/zero correction.
inline constexpr SightPixelGeometryRecipe kGarandPixelAnchors{{
    {"front-near-left",0,145,6,{25.727996826171875F,-.031578999012708664F,2.6424570083618164F}},
    {"front-near-right",0,146,6,{25.727996826171875F,.031578999012708664F,2.6424570083618164F}},
    {"front-far-left",0,147,6,{26.221981048583984F,-.031578999012708664F,2.7352559566497803F}},
    {"front-far-right",0,148,6,{26.221981048583984F,.031578999012708664F,2.7352559566497803F}},
    {"rear-ring-left",3,27,6,{-5.793834209442139F,-.1584479957818985F,2.1873819828033447F}},
    {"rear-ring-right",3,64,6,{-5.794379234313965F,.16262400150299072F,2.2235538959503174F}},
    {"rear-ring-top",3,80,6,{-5.796496868133545F,.020176999270915985F,2.3659870624542236F}},
    {"rear-ring-bottom",3,59,6,{-5.791685104370117F,.002088000066578388F,2.042912006378174F}},
}};
inline constexpr SightPixelGeometryRecipe kMosinPixelAnchors{{
    {"front-near-left",5,408,0,{26.11391830444336F,-.005067000165581703F,1.9076509475708008F}},
    {"front-near-right",5,409,0,{26.11391830444336F,.005067000165581703F,1.9076509475708008F}},
    {"front-far-left",5,410,0,{26.445377349853516F,-.005067000165581703F,2.0448639392852783F}},
    {"front-far-right",5,411,0,{26.445377349853516F,.005067000165581703F,2.0448639392852783F}},
    {"rear-inner-left",5,64,0,{5.770887851715088F,-.06721899658441544F,1.9776870012283325F}},
    {"rear-inner-right",5,59,0,{5.770887851715088F,.06721899658441544F,1.9776870012283325F}},
    {"rear-top-left",5,63,0,{5.778564929962158F,-.06721899658441544F,1.9852850437164307F}},
    {"rear-top-right",5,60,0,{5.778564929962158F,.06721899658441544F,1.9852850437164307F}},
}};
inline constexpr SightPixelGeometryRecipe kSvtPixelAnchors{{
    {"front-near-left",0,68,5,{21.75542640686035F,-.01233300007879734F,2.3203349113464355F}},
    {"front-near-right",0,70,5,{21.75542640686035F,.01309799961745739F,2.3203349113464355F}},
    {"front-far-left",0,72,5,{22.126272201538086F,-.021731000393629074F,2.403182029724121F}},
    {"front-far-right",0,78,5,{22.126272201538086F,.022495999932289124F,2.403182029724121F}},
    {"rear-inner-left",2,1517,0,{-.7057160139083862F,-.1064319983124733F,2.095033884048462F}},
    {"rear-inner-right",2,1521,0,{-.7051209807395935F,.11442200094461441F,2.095033884048462F}},
    {"rear-top-left",2,1516,0,{-.69303297996521F,-.13531699776649475F,2.12349796295166F}},
    {"rear-top-right",2,1519,0,{-.69303297996521F,.1412999927997589F,2.1191930770874023F}},
}};
inline constexpr SightPixelGeometryRecipe kColtPixelAnchors{{
    {"front-peak-left",2,1185,1,{5.347758769989014F,-.057395998388528824F,1.6813759803771973F}},
    {"front-peak-right",2,1191,1,{5.347758769989014F,.01899700053036213F,1.6813759803771973F}},
    {"front-slope-left",2,1187,1,{5.503936767578125F,-.057395998388528824F,1.6247129440307617F}},
    {"front-slope-right",2,1195,1,{5.503936767578125F,.01899700053036213F,1.6247129440307617F}},
    {"rear-inner-shoulder-left",2,345,1,{-1.3563870191574097F,-.07007399946451187F,1.6768449544906616F}},
    {"rear-inner-shoulder-right",2,342,1,{-1.3563870191574097F,.03485500067472458F,1.67256498336792F}},
    {"rear-notch-floor-left",2,309,1,{-1.3563870191574097F,-.05775900185108185F,1.6284689903259277F}},
    {"rear-notch-floor-right",2,306,1,{-1.3563870191574097F,.02249399945139885F,1.6284270286560059F}},
}};
[[nodiscard]] inline const SightPixelGeometryRecipe* sight_pixel_geometry_recipe(SightAuditAsset asset) noexcept {
    switch(asset) {
    case SightAuditAsset::garand:return &kGarandPixelAnchors;
    case SightAuditAsset::mosin:return &kMosinPixelAnchors;
    case SightAuditAsset::svt:return &kSvtPixelAnchors;
    case SightAuditAsset::colt:return &kColtPixelAnchors;
    }
    return nullptr;
}
} // namespace wawvr::mod::detail
