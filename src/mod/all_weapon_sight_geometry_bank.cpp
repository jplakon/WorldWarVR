// SPDX-License-Identifier: GPL-3.0-only
// Mechanically emitted typed mesh bases. Barrel axes are NOT iron-sight zeros.
#include "all_weapon_sight_geometry_bank.hpp"
#include <array>
namespace wawvr::mod {
namespace {
constexpr std::array<std::string_view,9> r0_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_bipod_left",
    "tag_bipod_right",
    "tag_brass",
    "tag_flash",
    "tag_iron_sights",
    "tag_scope"
}};
constexpr std::array<std::uint8_t,8> r0_parents{{
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8
}};
constexpr std::array<SightGeometryBone,9> r0_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{7.66399622F,-1.96479797F,1.89817905F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{0.760868073F,0.981266975F,2.55579901F},2.0F},
    {{-0.27060762F,0.653279185F,-0.27060762F,0.653279185F},{12.5021124F,0.472169995F,1.71357F},1.99999118F},
    {{-0.27060762F,0.653279185F,-0.27060762F,0.653279185F},{12.5021124F,-0.897131026F,1.71357F},1.99999118F},
    {{-0.0922879726F,-0.0922879726F,-0.701071203F,0.701040685F},{0.408693075F,-0.723205984F,2.82986116F},2.00001407F},
    {{0.0F,0.0F,0.0F,1.0F},{26.6686726F,-0.123690002F,2.76158404F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{0.630241036F,-0.123690002F,4.39427471F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{1.17592406F,-0.183037996F,3.40144014F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r0_s3_ranges{{
    {7,735,0,594}
}};
constexpr std::array<SightGeometryVertex,11> r0_s3_vertices{{
    {200,{23.3239422F,-0.151950002F,5.31532001F}},
    {201,{23.0150852F,-0.128667995F,5.34185505F}},
    {202,{23.3239422F,-0.128667995F,5.31532001F}},
    {203,{23.0150852F,-0.151950002F,5.34185505F}},
    {370,{0.454560995F,-0.136759996F,5.16037893F}},
    {371,{0.454560995F,-0.105732001F,5.16483974F}},
    {372,{0.588176012F,-0.136759996F,5.16037893F}},
    {390,{0.454560995F,-0.105732001F,5.37618637F}},
    {391,{0.588176012F,-0.105732001F,5.37618637F}},
    {392,{0.454560995F,-0.136759996F,5.38063526F}},
    {393,{0.588176012F,-0.136759996F,5.38063526F}}
}};
constexpr std::array<SightGeometryTriangle,5> r0_s3_triangles{{
    {150,{200,201,202}},
    {151,{201,200,203}},
    {283,{370,371,372}},
    {303,{390,392,391}},
    {304,{392,393,391}}
}};
constexpr std::array<SightGeometrySurface,1> r0_surfaces{{
    {3,735,594,"mc/mtl_weapon_fg42",r0_s3_ranges,r0_s3_vertices,r0_s3_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r0_front{{
    {3,201,0.5F},
    {3,203,0.5F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r0_rear{{
    {3,370,0.5F},
    {3,392,0.5F}
}};
const SightGeometryRecipe r0_recipe=[] {
    SightGeometryRecipe value{"fg42_bipod-reviewed-landmarks-v1","viewmodel_ger_fg42_bipod_lmg",r0_names,r0_parents,r0_binds,7,0,6,r0_surfaces,{7,SightGeometryReferenceMethod::weighted_centroid,r0_front},{7,SightGeometryReferenceMethod::weighted_centroid,r0_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,7> r1_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_bayonet",
    "tag_brass",
    "tag_flash",
    "tag_iron_sights"
}};
constexpr std::array<std::uint8_t,6> r1_parents{{
    1,
    2,
    3,
    4,
    5,
    6
}};
constexpr std::array<SightGeometryBone,7> r1_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-2.57778811F,0.78857702F,1.97639894F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.02067602F,0.00236900011F,-0.272053003F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{24.5387211F,0.199437007F,0.166539967F},2.0F},
    {{-0.22046572F,-0.201269567F,-0.704824984F,0.643513262F},{-4.04119587F,-0.341722012F,1.42825699F},1.99999571F},
    {{0.0F,0.0F,0.0F,1.0F},{28.6659603F,0.199437007F,0.873004019F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{33.6333275F,0.0F,0.675538957F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r1_s2_ranges{{
    {6,721,0,560}
}};
constexpr std::array<SightGeometryVertex,6> r1_s2_vertices{{
    {33,{0.873076975F,0.0914660022F,2.24851108F}},
    {34,{0.845227003F,0.109591F,2.44644594F}},
    {35,{0.875756979F,0.0807799995F,2.25901914F}},
    {72,{0.83604902F,-0.123103999F,2.43305111F}},
    {73,{0.875757992F,-0.083401002F,2.25901914F}},
    {74,{0.84522599F,-0.112521999F,2.44644713F}}
}};
constexpr std::array<SightGeometryTriangle,2> r1_s2_triangles{{
    {11,{33,34,35}},
    {24,{72,73,74}}
}};
constexpr std::array<SightGeometryRigidRange,1> r1_s4_ranges{{
    {0,1799,0,1395}
}};
constexpr std::array<SightGeometryVertex,6> r1_s4_vertices{{
    {3,{21.5946541F,0.085763F,2.14024615F}},
    {4,{21.6297894F,0.0444720015F,2.50577807F}},
    {5,{21.5946541F,0.0484399982F,2.476722F}},
    {278,{21.9136486F,-0.0412570015F,2.45152807F}},
    {279,{21.6297894F,0.0444720015F,2.50577807F}},
    {281,{21.6297894F,-0.0412570015F,2.50577807F}}
}};
constexpr std::array<SightGeometryTriangle,2> r1_s4_triangles{{
    {1,{3,4,5}},
    {160,{279,278,281}}
}};
constexpr std::array<SightGeometrySurface,2> r1_surfaces{{
    {2,721,560,"mc/mtl_weapon_g43",r1_s2_ranges,r1_s2_vertices,r1_s2_triangles},
    {4,1799,1396,"mc/mtl_weapon_g43",r1_s4_ranges,r1_s4_vertices,r1_s4_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r1_front{{
    {4,4,1.0F},
    {4,281,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r1_rear{{
    {2,34,1.0F},
    {2,74,1.0F}
}};
const SightGeometryRecipe r1_recipe=[] {
    SightGeometryRecipe value{"gewehr43-reviewed-western-irons-v1","viewmodel_ger_g43_rifle",r1_names,r1_parents,r1_binds,6,0,5,r1_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r1_front},{6,SightGeometryReferenceMethod::weighted_centroid,r1_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,7> r2_names{{
    "j_gun",
    "j_bolt",
    "j_round",
    "j_stripper_clip",
    "j_stripper_rounds",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,6> r2_parents{{
    1,
    2,
    3,
    4,
    5,
    6
}};
constexpr std::array<SightGeometryBone,7> r2_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-7.73876619F,-0.0619830005F,1.10182285F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.21501803F,37.1427116F,8.61235905F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-3.03221989F,37.1474953F,7.33137321F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.21501803F,37.1427116F,7.59117126F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.79524302F,-0.341722012F,1.12209988F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{27.8524189F,0.0F,1.52065086F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r2_s4_ranges{{
    {0,2623,0,2465}
}};
constexpr std::array<SightGeometryVertex,14> r2_s4_vertices{{
    {1116,{28.4723244F,-0.0495010018F,2.00962257F}},
    {1118,{28.8415737F,-0.0495010018F,2.00962257F}},
    {1119,{28.5401554F,0.0F,2.15320396F}},
    {1733,{4.89192915F,-0.463528007F,1.86741734F}},
    {1734,{4.97338581F,-0.160611004F,1.86741734F}},
    {1735,{4.89192915F,-0.160611004F,1.86741734F}},
    {1736,{4.93165302F,-1.99999999e-06F,1.78584194F}},
    {1737,{4.85019684F,-1.99999999e-06F,1.78584194F}},
    {1738,{4.93165302F,-1.99999999e-06F,1.78584194F}},
    {1739,{4.85019684F,-1.99999999e-06F,1.78584194F}},
    {1740,{4.89192915F,0.160607994F,1.86741734F}},
    {1741,{4.97338581F,0.160607994F,1.86741734F}},
    {1742,{4.97338581F,0.463528007F,1.86741734F}},
    {1743,{4.89192915F,0.463528007F,1.86741734F}}
}};
constexpr std::array<SightGeometryTriangle,8> r2_s4_triangles{{
    {1132,{1116,1119,1118}},
    {1624,{1733,1735,1734}},
    {1625,{1734,1735,1736}},
    {1626,{1735,1737,1736}},
    {1627,{1738,1739,1740}},
    {1628,{1741,1738,1740}},
    {1629,{1741,1740,1742}},
    {1630,{1740,1743,1742}}
}};
constexpr std::array<SightGeometrySurface,1> r2_surfaces{{
    {4,2623,2466,"mc/mtl_weapon_kar98",r2_s4_ranges,r2_s4_vertices,r2_s4_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r2_front{{
    {4,1119,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r2_rear{{
    {4,1735,1.0F},
    {4,1740,1.0F}
}};
const SightGeometryRecipe r2_recipe=[] {
    SightGeometryRecipe value{"kar98k-notch-geometry-bank-v1","viewmodel_ger_kar98_rifle",r2_names,r2_parents,r2_binds,6,0,6,r2_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r2_front},{0,SightGeometryReferenceMethod::weighted_centroid,r2_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,7> r3_names{{
    "j_gun",
    "j_bipod",
    "j_bolt",
    "j_clip",
    "tag_brass",
    "tag_flash",
    "tag_bipod"
}};
constexpr std::array<std::uint8_t,6> r3_parents{{
    1,
    2,
    3,
    4,
    5,
    5
}};
constexpr std::array<SightGeometryBone,7> r3_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,0.791131318F,0.0F,0.611651957F},{27.9938583F,0.0F,-2.29364681F},1.99998617F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-7.42869663F,-0.894531012F,-0.0654459F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{0.357589006F,4.37017584F,-0.274828911F},2.0F},
    {{-0.132480845F,-0.115512557F,-0.680593252F,0.711264372F},{-0.0580599308F,-1.81135499F,0.993530273F},2.0000031F},
    {{0.0F,0.0F,0.0F,1.0F},{36.7492218F,0.0678640008F,-0.294047356F},2.0F},
    {{0.0F,0.791131318F,0.0F,0.611651957F},{28.0036144F,0.0678640008F,-2.36925673F},1.99998617F}
}};
constexpr std::array<SightGeometryRigidRange,1> r3_s2_ranges{{
    {0,4882,0,4404}
}};
constexpr std::array<SightGeometryVertex,11> r3_s2_vertices{{
    {235,{27.752079F,-0.0394329987F,3.58259678F}},
    {236,{27.1477737F,-0.0233089998F,3.67789745F}},
    {237,{27.752079F,0.0394299999F,3.58259678F}},
    {238,{27.1477737F,0.0233009998F,3.67789745F}},
    {1070,{6.71400452F,-0.491616011F,3.65767956F}},
    {1071,{6.71400452F,-0.110132001F,3.65767956F}},
    {1072,{6.77240944F,-0.491616011F,3.65594387F}},
    {1073,{6.77240944F,-0.110132001F,3.65594387F}},
    {1075,{6.70612526F,-4.99999987e-06F,3.52846432F}},
    {1078,{6.71400452F,0.491605997F,3.65767956F}},
    {1080,{6.71400452F,0.110122003F,3.65767956F}}
}};
constexpr std::array<SightGeometryTriangle,5> r3_s2_triangles{{
    {195,{235,236,237}},
    {196,{236,238,237}},
    {765,{1070,1071,1072}},
    {766,{1071,1073,1072}},
    {771,{1075,1078,1080}}
}};
constexpr std::array<SightGeometrySurface,1> r3_surfaces{{
    {2,4882,4404,"mc/mtl_ger_mg_mg42",r3_s2_ranges,r3_s2_vertices,r3_s2_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r3_front{{
    {2,236,0.5F},
    {2,238,0.5F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r3_rear{{
    {2,1071,0.5F},
    {2,1080,0.5F}
}};
const SightGeometryRecipe r3_recipe=[] {
    SightGeometryRecipe value{"mg42_bipod-reviewed-landmarks-v1","viewmodel_ger_mg42_bipod_mg",r3_names,r3_parents,r3_binds,4,0,5,r3_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r3_front},{0,SightGeometryReferenceMethod::weighted_centroid,r3_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,5> r4_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,4> r4_parents{{
    1,
    2,
    3,
    4
}};
constexpr std::array<SightGeometryBone,5> r4_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-2.53235793F,1.25252604F,1.000512F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{4.20703793F,0.408818007F,-0.275402993F},2.0F},
    {{-0.294351012F,-0.28305918F,-0.693868816F,0.593096733F},{-2.45448399F,-0.341722012F,1.1221F},2.00003457F},
    {{0.0F,0.0F,0.0F,1.0F},{15.5053186F,0.482452989F,0.912301004F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r4_s2_ranges{{
    {0,3362,0,3029}
}};
constexpr std::array<SightGeometryVertex,13> r4_s2_vertices{{
    {143,{14.1847248F,0.541499972F,1.88795304F}},
    {144,{14.4411421F,0.531361997F,2.12501192F}},
    {145,{14.1847248F,0.51888603F,2.06960607F}},
    {161,{14.4411421F,0.421882004F,1.88795304F}},
    {162,{14.1847248F,0.468075007F,2.06960607F}},
    {163,{14.4411421F,0.455597997F,2.12501192F}},
    {1995,{-1.00056696F,0.648835003F,2.13707495F}},
    {1996,{-0.996999979F,0.659381986F,2.14851189F}},
    {1997,{-0.916701019F,0.630083025F,2.14962196F}},
    {1998,{-0.916701019F,0.651965022F,2.1610589F}},
    {2016,{-0.916701019F,0.358366013F,2.0445509F}},
    {2017,{-1.00056696F,0.339612991F,2.13707495F}},
    {2018,{-0.916701019F,0.358366013F,2.14962196F}}
}};
constexpr std::array<SightGeometryTriangle,5> r4_s2_triangles{{
    {76,{143,144,145}},
    {82,{161,162,163}},
    {1678,{1995,1996,1997}},
    {1679,{1996,1998,1997}},
    {1689,{2016,2018,2017}}
}};
constexpr std::array<SightGeometrySurface,1> r4_surfaces{{
    {2,3362,3030,"mc/mtl_weapon_mp40",r4_s2_ranges,r4_s2_vertices,r4_s2_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r4_front{{
    {2,144,1.0F},
    {2,163,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r4_rear{{
    {2,1997,1.0F},
    {2,2018,1.0F}
}};
const SightGeometryRecipe r4_recipe=[] {
    SightGeometryRecipe value{"mp40-reviewed-western-irons-v1","viewmodel_ger_mp40_smg",r4_names,r4_parents,r4_binds,3,0,4,r4_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r4_front},{0,SightGeometryReferenceMethod::weighted_centroid,r4_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,6> r5_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_brass",
    "tag_flash",
    "tag_iron_sights"
}};
constexpr std::array<std::uint8_t,5> r5_parents{{
    1,
    2,
    3,
    4,
    5
}};
constexpr std::array<SightGeometryBone,6> r5_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{4.69045305F,0.134476006F,3.20595121F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{6.63946009F,0.0150119998F,1.83560097F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{3.43127966F,-0.480022013F,3.52051497F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{23.6595592F,-0.012104F,1.99778092F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{6.29964113F,-0.309505999F,4.25149012F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r5_s0_ranges{{
    {0,2388,0,2335}
}};
constexpr std::array<SightGeometryVertex,3> r5_s0_vertices{{
    {12,{22.5668221F,-0.0102329999F,4.7354002F}},
    {13,{22.1788673F,-0.0102519998F,4.77380514F}},
    {14,{22.2072163F,-0.00105600001F,4.75656128F}}
}};
constexpr std::array<SightGeometryTriangle,1> r5_s0_triangles{{
    {4,{12,13,14}}
}};
constexpr std::array<SightGeometryRigidRange,1> r5_s3_ranges{{
    {5,1014,0,805}
}};
constexpr std::array<SightGeometryVertex,6> r5_s3_vertices{{
    {168,{5.12398767F,0.0696709976F,4.7510891F}},
    {169,{5.12438154F,0.196836993F,4.74774218F}},
    {170,{5.13138914F,0.064586997F,4.76731014F}},
    {171,{5.13178301F,-0.236669004F,4.76731014F}},
    {172,{5.12438154F,-0.223168001F,4.74774218F}},
    {173,{5.13138914F,-0.0909959972F,4.76731014F}}
}};
constexpr std::array<SightGeometryTriangle,2> r5_s3_triangles{{
    {73,{168,169,170}},
    {74,{171,172,173}}
}};
constexpr std::array<SightGeometrySurface,2> r5_surfaces{{
    {0,2388,2336,"mc/mtl_weapon_mp44",r5_s0_ranges,r5_s0_vertices,r5_s0_triangles},
    {3,1014,806,"mc/mtl_weapon_mp44",r5_s3_ranges,r5_s3_vertices,r5_s3_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r5_front{{
    {0,13,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r5_rear{{
    {3,170,1.0F},
    {3,173,1.0F}
}};
const SightGeometryRecipe r5_recipe=[] {
    SightGeometryRecipe value{"stg44-reviewed-western-irons-v1","viewmodel_ger_mp44_lmg",r5_names,r5_parents,r5_binds,5,0,4,r5_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r5_front},{5,SightGeometryReferenceMethod::weighted_centroid,r5_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,7> r6_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "joint1",
    "joint2",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,6> r6_parents{{
    1,
    2,
    3,
    4,
    5,
    6
}};
constexpr std::array<SightGeometryBone,7> r6_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-2.72244215F,0.0953150019F,1.60395074F},2.00003505F},
    {{0.0F,0.0F,0.707113862F,0.707113862F},{-1.90748703F,0.0513009988F,-1.63255215F},1.99995995F},
    {{0.0F,0.0F,0.0F,1.0F},{-3.04882717F,0.079411F,0.599392891F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{0.723712981F,0.472398996F,0.770867825F},2.0F},
    {{-0.190099791F,-0.118106633F,-0.661976993F,0.715323329F},{0.994382977F,-0.242668003F,1.51787496F},2.00002384F},
    {{0.0F,0.0F,0.0F,1.0F},{4.98807526F,-0.00387500226F,1.17504692F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r6_s2_ranges{{
    {1,1305,0,1242}
}};
constexpr std::array<SightGeometryVertex,7> r6_s2_vertices{{
    {933,{-2.53087378F,-0.267439991F,1.71740985F}},
    {934,{-2.53087521F,-0.0974439979F,1.72137189F}},
    {935,{-2.53087378F,-0.105146006F,1.72429299F}},
    {936,{-2.53087521F,0.130351007F,1.6563971F}},
    {943,{-2.53087711F,0.306506008F,1.71797299F}},
    {945,{-2.53087521F,0.149785995F,1.72113204F}},
    {946,{-2.53087521F,0.141773999F,1.71807075F}}
}};
constexpr std::array<SightGeometryTriangle,3> r6_s2_triangles{{
    {808,{933,934,935}},
    {817,{945,936,943}},
    {818,{936,945,946}}
}};
constexpr std::array<SightGeometryRigidRange,1> r6_s3_ranges{{
    {0,2545,0,2375}
}};
constexpr std::array<SightGeometryVertex,4> r6_s3_vertices{{
    {1059,{4.56744814F,0.0456890017F,1.7695477F}},
    {1060,{4.69833899F,-0.0467270017F,1.7235539F}},
    {1061,{4.56744814F,-0.0467270017F,1.76961875F}},
    {1062,{4.69831419F,0.0456890017F,1.72348905F}}
}};
constexpr std::array<SightGeometryTriangle,2> r6_s3_triangles{{
    {774,{1059,1060,1061}},
    {775,{1060,1059,1062}}
}};
constexpr std::array<SightGeometrySurface,2> r6_surfaces{{
    {2,1305,1242,"mc/mtl_weapon_walther",r6_s2_ranges,r6_s2_vertices,r6_s2_triangles},
    {3,2545,2376,"mc/mtl_weapon_walther",r6_s3_ranges,r6_s3_vertices,r6_s3_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r6_front{{
    {3,1059,0.5F},
    {3,1061,0.5F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r6_rear{{
    {2,935,0.5F},
    {2,945,0.5F}
}};
const SightGeometryRecipe r6_recipe=[] {
    SightGeometryRecipe value{"walther-reviewed-landmarks-v1","viewmodel_ger_walther_pistol",r6_names,r6_parents,r6_binds,4,0,6,r6_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r6_front},{1,SightGeometryReferenceMethod::weighted_centroid,r6_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,5> r7_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,4> r7_parents{{
    1,
    2,
    3,
    4
}};
constexpr std::array<SightGeometryBone,5> r7_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.00723288674F,-0.00723288674F,0.707083344F,0.707052827F},{-4.1753087F,0.014210999F,1.15415287F},2.00000978F},
    {{0.0F,0.0F,0.707113862F,0.707113862F},{-1.90748596F,0.051300019F,-1.63255215F},1.99995995F},
    {{-0.190099791F,-0.118106633F,-0.661976993F,0.715323329F},{-0.629908025F,0.000786006451F,1.51787472F},2.00002384F},
    {{0.0F,0.0F,0.0F,1.0F},{4.98807621F,-0.00387498736F,1.17504692F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r7_s1_ranges{{
    {0,2402,0,2323}
}};
constexpr std::array<SightGeometryVertex,12> r7_s1_vertices{{
    {268,{-3.59928203F,-0.0333789885F,1.62706184F}},
    {271,{-3.68166113F,-0.0333779901F,1.80769491F}},
    {272,{-3.6615541F,-0.0333779901F,1.80917597F}},
    {273,{-3.59928203F,-0.0333789885F,1.79739475F}},
    {1612,{4.59971333F,0.0200740099F,1.73871803F}},
    {1615,{4.53936338F,0.0597340167F,1.73330784F}},
    {1616,{4.54291201F,0.0200740099F,1.75465679F}},
    {1618,{4.53936338F,-0.0195859969F,1.73330688F}},
    {1769,{-3.59928298F,0.0735270083F,1.62706184F}},
    {1771,{-3.68166113F,0.0735260248F,1.80769491F}},
    {1772,{-3.6615541F,0.0735260248F,1.80917597F}},
    {1773,{-3.59928298F,0.0735270083F,1.79739571F}}
}};
constexpr std::array<SightGeometryTriangle,6> r7_s1_triangles{{
    {236,{272,271,268}},
    {237,{273,272,268}},
    {1591,{1612,1616,1615}},
    {1593,{1618,1616,1612}},
    {1698,{1772,1769,1771}},
    {1699,{1773,1769,1772}}
}};
constexpr std::array<SightGeometrySurface,1> r7_surfaces{{
    {1,2402,2324,"mc/mtl_weapon_nambu",r7_s1_ranges,r7_s1_vertices,r7_s1_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r7_front{{
    {1,1616,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r7_rear{{
    {1,272,1.0F},
    {1,1772,1.0F}
}};
const SightGeometryRecipe r7_recipe=[] {
    SightGeometryRecipe value{"transferred-nambu-mesh-v1","viewmodel_jap_nambu_pistol",r7_names,r7_parents,r7_binds,4,0,4,r7_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r7_front},{0,SightGeometryReferenceMethod::weighted_centroid,r7_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,5> r8_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,4> r8_parents{{
    1,
    2,
    3,
    4
}};
constexpr std::array<SightGeometryBone,5> r8_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.00723288674F,-0.00723288674F,0.707083344F,0.707052827F},{-4.1753087F,0.014210999F,1.15415287F},2.00000978F},
    {{0.0F,0.0F,0.707113862F,0.707113862F},{-1.90748596F,0.051300019F,-1.63255215F},1.99995995F},
    {{-0.190099791F,-0.118106633F,-0.661976993F,0.715323329F},{-0.629908025F,0.000786006451F,1.51787472F},2.00002384F},
    {{0.0F,0.0F,0.0F,1.0F},{4.98807621F,-0.00387498736F,1.17504692F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r8_s1_ranges{{
    {0,2402,0,2323}
}};
constexpr std::array<SightGeometryVertex,12> r8_s1_vertices{{
    {268,{-3.59928203F,-0.0333789885F,1.62706184F}},
    {271,{-3.68166113F,-0.0333779901F,1.80769491F}},
    {272,{-3.6615541F,-0.0333779901F,1.80917597F}},
    {273,{-3.59928203F,-0.0333789885F,1.79739475F}},
    {1612,{4.59971333F,0.0200740099F,1.73871803F}},
    {1615,{4.53936338F,0.0597340167F,1.73330784F}},
    {1616,{4.54291201F,0.0200740099F,1.75465679F}},
    {1618,{4.53936338F,-0.0195859969F,1.73330688F}},
    {1769,{-3.59928298F,0.0735270083F,1.62706184F}},
    {1771,{-3.68166113F,0.0735260248F,1.80769491F}},
    {1772,{-3.6615541F,0.0735260248F,1.80917597F}},
    {1773,{-3.59928298F,0.0735270083F,1.79739571F}}
}};
constexpr std::array<SightGeometryTriangle,6> r8_s1_triangles{{
    {236,{272,271,268}},
    {237,{273,272,268}},
    {1591,{1612,1616,1615}},
    {1593,{1618,1616,1612}},
    {1698,{1772,1769,1771}},
    {1699,{1773,1769,1772}}
}};
constexpr std::array<SightGeometrySurface,1> r8_surfaces{{
    {1,2402,2324,"mc/mtl_weapon_nambu_wet",r8_s1_ranges,r8_s1_vertices,r8_s1_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r8_front{{
    {1,1616,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r8_rear{{
    {1,272,1.0F},
    {1,1772,1.0F}
}};
const SightGeometryRecipe r8_recipe=[] {
    SightGeometryRecipe value{"transferred-nambu_wet-mesh-v1","viewmodel_jap_nambu_pistol_wet",r8_names,r8_parents,r8_binds,4,0,4,r8_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r8_front},{0,SightGeometryReferenceMethod::weighted_centroid,r8_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,6> r9_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "ring_jnt",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,5> r9_parents{{
    1,
    2,
    3,
    4,
    5
}};
constexpr std::array<SightGeometryBone,6> r9_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-1.46550095F,-0.620103002F,1.75070906F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{2.35108995F,2.12277389F,1.51647103F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{12.4434404F,0.671782017F,1.51981103F},2.0F},
    {{-0.267403185F,-0.238319039F,-0.697012246F,0.621173739F},{-4.5152688F,-0.85709703F,1.85917199F},2.00003338F},
    {{0.0F,0.0F,0.0F,1.0F},{15.6997442F,0.0F,1.53522801F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r9_s0_ranges{{
    {0,4113,0,4386}
}};
constexpr std::array<SightGeometryVertex,12> r9_s0_vertices{{
    {167,{12.7205887F,-0.195023999F,2.57976508F}},
    {168,{12.6226006F,-0.195084006F,2.60204411F}},
    {169,{12.7205496F,-0.165920004F,2.57976508F}},
    {170,{12.6225615F,-0.165979996F,2.60204411F}},
    {3155,{-6.29599714F,-0.103128001F,2.59756994F}},
    {3156,{-6.29594278F,-0.162726F,2.47375393F}},
    {3157,{-6.36723804F,-0.10328F,2.59756994F}},
    {3158,{-6.36718512F,-0.162789002F,2.47375989F}},
    {3216,{-6.36701488F,-0.247302994F,2.24263501F}},
    {3223,{-6.36707401F,-0.200270995F,2.47374797F}},
    {3224,{-6.36702013F,-0.259959996F,2.59756994F}},
    {3226,{-6.36694288F,-0.316128999F,2.60262394F}}
}};
constexpr std::array<SightGeometryTriangle,6> r9_s0_triangles{{
    {91,{167,168,169}},
    {92,{168,170,169}},
    {3256,{3155,3156,3157}},
    {3257,{3156,3158,3157}},
    {3321,{3223,3224,3216}},
    {3324,{3216,3224,3226}}
}};
constexpr std::array<SightGeometrySurface,1> r9_surfaces{{
    {0,4113,4386,"mc/mtl_weapon_type100_metal",r9_s0_ranges,r9_s0_vertices,r9_s0_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r9_front{{
    {0,168,1.0F},
    {0,170,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r9_rear{{
    {0,3157,1.0F},
    {0,3224,1.0F}
}};
const SightGeometryRecipe r9_recipe=[] {
    SightGeometryRecipe value{"transferred-type100_smg-mesh-v1","viewmodel_jap_type100_smg",r9_names,r9_parents,r9_binds,5,0,5,r9_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r9_front},{0,SightGeometryReferenceMethod::weighted_centroid,r9_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,6> r10_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "ring_jnt",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,5> r10_parents{{
    1,
    2,
    3,
    4,
    5
}};
constexpr std::array<SightGeometryBone,6> r10_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-1.46550095F,-0.620103002F,1.75070906F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{2.35108995F,2.12277389F,1.51647103F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{12.4434404F,0.671782017F,1.51981103F},2.0F},
    {{-0.267403185F,-0.238319039F,-0.697012246F,0.621173739F},{-4.5152688F,-0.85709703F,1.85917199F},2.00003338F},
    {{0.0F,0.0F,0.0F,1.0F},{15.6997442F,0.0F,1.53522801F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r10_s0_ranges{{
    {0,4113,0,4386}
}};
constexpr std::array<SightGeometryVertex,12> r10_s0_vertices{{
    {167,{12.7205887F,-0.195023999F,2.57976508F}},
    {168,{12.6226006F,-0.195084006F,2.60204411F}},
    {169,{12.7205496F,-0.165920004F,2.57976508F}},
    {170,{12.6225615F,-0.165979996F,2.60204411F}},
    {3155,{-6.29599714F,-0.103128001F,2.59756994F}},
    {3156,{-6.29594278F,-0.162726F,2.47375393F}},
    {3157,{-6.36723804F,-0.10328F,2.59756994F}},
    {3158,{-6.36718512F,-0.162789002F,2.47375989F}},
    {3216,{-6.36701488F,-0.247302994F,2.24263501F}},
    {3223,{-6.36707401F,-0.200270995F,2.47374797F}},
    {3224,{-6.36702013F,-0.259959996F,2.59756994F}},
    {3226,{-6.36694288F,-0.316128999F,2.60262394F}}
}};
constexpr std::array<SightGeometryTriangle,6> r10_s0_triangles{{
    {91,{167,168,169}},
    {92,{168,170,169}},
    {3256,{3155,3156,3157}},
    {3257,{3156,3158,3157}},
    {3321,{3223,3224,3216}},
    {3324,{3216,3224,3226}}
}};
constexpr std::array<SightGeometrySurface,1> r10_surfaces{{
    {0,4113,4386,"mc/mtl_weapon_type100_metal_wet",r10_s0_ranges,r10_s0_vertices,r10_s0_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r10_front{{
    {0,168,1.0F},
    {0,170,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r10_rear{{
    {0,3157,1.0F},
    {0,3224,1.0F}
}};
const SightGeometryRecipe r10_recipe=[] {
    SightGeometryRecipe value{"transferred-type100_smg_wet-mesh-v1","viewmodel_jap_type100_smg_wet",r10_names,r10_parents,r10_binds,5,0,5,r10_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r10_front},{0,SightGeometryReferenceMethod::weighted_centroid,r10_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,10> r11_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "j_clip_release",
    "j_handle",
    "tag_bipod_le",
    "tag_bipod_ri",
    "tag_brass",
    "tag_flash",
    "j_handle2"
}};
constexpr std::array<std::uint8_t,9> r11_parents{{
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8,
    5
}};
constexpr std::array<SightGeometryBone,10> r11_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-7.42869663F,-0.894531012F,-0.0654459F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{1.55948186F,-0.0478029996F,1.43692303F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-0.489055872F,-0.00130600005F,1.70349216F},2.0F},
    {{0.0F,0.0F,0.969481468F,0.245155185F},{12.0328465F,0.696182013F,0.165527344F},2.0000093F},
    {{-0.674001276F,0.707449555F,0.0852687135F,0.194860682F},{22.4029083F,1.321118F,-0.527758598F},1.99999189F},
    {{0.672780514F,0.708578765F,-0.0799584985F,0.19721061F},{22.4029408F,-1.35679901F,-0.527760506F},1.99999428F},
    {{-0.173589289F,-0.211066008F,-0.611072123F,0.742912054F},{1.50932193F,-0.870971024F,0.0912761688F},1.99998093F},
    {{0.0F,0.0F,0.0F,1.0F},{32.9186363F,0.0678640008F,0.455997467F},2.0F},
    {{0.0F,0.0F,0.0F,0.99999541F},{8.61968613F,0.008853117F,3.33144093F},2.00001836F}
}};
constexpr std::array<SightGeometryRigidRange,1> r11_s0_ranges{{
    {0,2738,0,2525}
}};
constexpr std::array<SightGeometryVertex,40> r11_s0_vertices{{
    {47,{22.2425728F,1.08821595F,1.28971004F}},
    {48,{22.2994099F,1.00989795F,1.66323757F}},
    {49,{22.2425728F,0.93158102F,1.28971004F}},
    {1012,{-5.72719669F,0.620469987F,1.74703121F}},
    {1013,{-5.72719669F,1.10918105F,1.7470293F}},
    {1014,{-5.73421764F,0.86482501F,1.7606554F}},
    {1015,{-5.73562193F,0.821377993F,1.76736736F}},
    {1016,{-5.73605061F,0.669938028F,1.78869343F}},
    {1017,{-5.73562002F,0.908273995F,1.76736736F}},
    {1018,{-5.73968887F,0.782177985F,1.78684902F}},
    {1019,{-5.74602413F,0.751069009F,1.81719303F}},
    {1020,{-5.74721003F,0.681713998F,1.84119511F}},
    {1021,{-5.73605299F,1.05971801F,1.78869343F}},
    {1022,{-5.73968792F,0.94747299F,1.78684902F}},
    {1023,{-5.74602509F,0.978585005F,1.81719303F}},
    {1024,{-5.74721003F,1.04793298F,1.84119511F}},
    {1025,{-5.75400496F,0.998552978F,1.85542965F}},
    {1026,{-5.76284981F,1.00543594F,1.89781284F}},
    {1027,{-5.75957966F,1.05735695F,1.89938831F}},
    {1028,{-5.771698F,0.998552978F,1.94019604F}},
    {1029,{-5.77194881F,1.04793298F,1.95758343F}},
    {1030,{-5.75400496F,0.731095016F,1.85542965F}},
    {1031,{-5.77968216F,0.978582025F,1.97843075F}},
    {1032,{-5.78310871F,1.02058804F,2.01008129F}},
    {1033,{-5.78601456F,0.94747299F,2.00877666F}},
    {1034,{-5.79196501F,0.977990985F,2.05174541F}},
    {1035,{-5.79008198F,0.908273995F,2.02825832F}},
    {1036,{-5.79765081F,0.924318016F,2.07849598F}},
    {1037,{-5.79148293F,0.86482501F,2.03497028F}},
    {1038,{-5.79961014F,0.86482501F,2.0877142F}},
    {1039,{-5.79765081F,0.805329978F,2.07849598F}},
    {1040,{-5.79008198F,0.821375012F,2.02825642F}},
    {1041,{-5.76284981F,0.724214017F,1.89781284F}},
    {1042,{-5.75957966F,0.672293007F,1.89938831F}},
    {1043,{-5.79196501F,0.751658976F,2.05174541F}},
    {1044,{-5.78601456F,0.782177985F,2.00877476F}},
    {1045,{-5.7831068F,0.70906502F,2.01008129F}},
    {1046,{-5.77968311F,0.751069009F,1.97843075F}},
    {1047,{-5.77194881F,0.681713998F,1.95758343F}},
    {1048,{-5.77169895F,0.731095016F,1.94019413F}}
}};
constexpr std::array<SightGeometryTriangle,38> r11_s0_triangles{{
    {29,{47,48,49}},
    {809,{1012,1013,1014}},
    {810,{1014,1015,1012}},
    {811,{1012,1015,1016}},
    {812,{1014,1013,1017}},
    {813,{1015,1018,1016}},
    {814,{1018,1019,1016}},
    {815,{1016,1019,1020}},
    {816,{1013,1021,1017}},
    {817,{1021,1022,1017}},
    {818,{1021,1023,1022}},
    {819,{1021,1024,1023}},
    {820,{1024,1025,1023}},
    {821,{1026,1025,1024}},
    {822,{1027,1026,1024}},
    {823,{1028,1026,1027}},
    {824,{1029,1028,1027}},
    {825,{1019,1030,1020}},
    {826,{1031,1028,1029}},
    {827,{1032,1031,1029}},
    {828,{1033,1031,1032}},
    {829,{1034,1033,1032}},
    {830,{1035,1033,1034}},
    {831,{1036,1035,1034}},
    {832,{1037,1035,1036}},
    {833,{1038,1037,1036}},
    {834,{1038,1039,1037}},
    {835,{1039,1040,1037}},
    {836,{1020,1030,1041}},
    {837,{1042,1020,1041}},
    {838,{1039,1043,1040}},
    {839,{1043,1044,1040}},
    {840,{1043,1045,1044}},
    {841,{1045,1046,1044}},
    {842,{1045,1047,1046}},
    {843,{1047,1048,1046}},
    {844,{1047,1042,1048}},
    {845,{1042,1041,1048}}
}};
constexpr std::array<SightGeometrySurface,1> r11_surfaces{{
    {0,2738,2526,"mc/mtl_weapon_type99_lmg",r11_s0_ranges,r11_s0_vertices,r11_s0_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r11_front{{
    {0,48,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,20> r11_rear{{
    {0,1014,0.0500000007F},
    {0,1015,0.0500000007F},
    {0,1018,0.0500000007F},
    {0,1019,0.0500000007F},
    {0,1030,0.0500000007F},
    {0,1041,0.0500000007F},
    {0,1048,0.0500000007F},
    {0,1046,0.0500000007F},
    {0,1044,0.0500000007F},
    {0,1040,0.0500000007F},
    {0,1037,0.0500000007F},
    {0,1035,0.0500000007F},
    {0,1033,0.0500000007F},
    {0,1031,0.0500000007F},
    {0,1028,0.0500000007F},
    {0,1026,0.0500000007F},
    {0,1025,0.0500000007F},
    {0,1023,0.0500000007F},
    {0,1022,0.0500000007F},
    {0,1017,0.0500000007F}
}};
const SightGeometryRecipe r11_recipe=[] {
    SightGeometryRecipe value{"type99_lmg_bipod-reviewed-landmarks-v1","viewmodel_jap_type99_bipod_lmg",r11_names,r11_parents,r11_binds,7,0,8,r11_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r11_front},{0,SightGeometryReferenceMethod::weighted_centroid,r11_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,8> r12_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "j_clip_release",
    "j_handle",
    "tag_brass",
    "tag_flash",
    "j_handle2"
}};
constexpr std::array<std::uint8_t,7> r12_parents{{
    1,
    2,
    3,
    4,
    5,
    6,
    3
}};
constexpr std::array<SightGeometryBone,8> r12_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-7.42869663F,-0.894531012F,-0.0654459F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{1.55948186F,-0.0478029996F,1.43692303F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-0.489055872F,-0.00130600005F,1.70349216F},2.0F},
    {{0.0F,0.0F,0.969481468F,0.245155185F},{12.0328465F,0.696182013F,0.165527344F},2.0000093F},
    {{-0.173589289F,-0.211066008F,-0.611072123F,0.742912054F},{1.50932193F,-0.870971024F,0.0912761688F},1.99998093F},
    {{0.0F,0.0F,0.0F,1.0F},{32.9186363F,0.0678640008F,0.455997467F},2.0F},
    {{0.0F,0.0F,0.0F,0.99999541F},{8.61968613F,0.008853117F,3.33144093F},2.00001836F}
}};
constexpr std::array<SightGeometryRigidRange,1> r12_s0_ranges{{
    {0,2738,0,2525}
}};
constexpr std::array<SightGeometryVertex,40> r12_s0_vertices{{
    {47,{22.2425728F,1.08821595F,1.28971004F}},
    {48,{22.2994099F,1.00989795F,1.66323757F}},
    {49,{22.2425728F,0.93158102F,1.28971004F}},
    {1012,{-5.72719669F,0.620469987F,1.74703121F}},
    {1013,{-5.72719669F,1.10918105F,1.7470293F}},
    {1014,{-5.73421764F,0.86482501F,1.7606554F}},
    {1015,{-5.73562193F,0.821377993F,1.76736736F}},
    {1016,{-5.73605061F,0.669938028F,1.78869343F}},
    {1017,{-5.73562002F,0.908273995F,1.76736736F}},
    {1018,{-5.73968887F,0.782177985F,1.78684902F}},
    {1019,{-5.74602413F,0.751069009F,1.81719303F}},
    {1020,{-5.74721003F,0.681713998F,1.84119511F}},
    {1021,{-5.73605299F,1.05971801F,1.78869343F}},
    {1022,{-5.73968792F,0.94747299F,1.78684902F}},
    {1023,{-5.74602509F,0.978585005F,1.81719303F}},
    {1024,{-5.74721003F,1.04793298F,1.84119511F}},
    {1025,{-5.75400496F,0.998552978F,1.85542965F}},
    {1026,{-5.76284981F,1.00543594F,1.89781284F}},
    {1027,{-5.75957966F,1.05735695F,1.89938831F}},
    {1028,{-5.771698F,0.998552978F,1.94019604F}},
    {1029,{-5.77194881F,1.04793298F,1.95758343F}},
    {1030,{-5.75400496F,0.731095016F,1.85542965F}},
    {1031,{-5.77968216F,0.978582025F,1.97843075F}},
    {1032,{-5.78310871F,1.02058804F,2.01008129F}},
    {1033,{-5.78601456F,0.94747299F,2.00877666F}},
    {1034,{-5.79196501F,0.977990985F,2.05174541F}},
    {1035,{-5.79008198F,0.908273995F,2.02825832F}},
    {1036,{-5.79765081F,0.924318016F,2.07849598F}},
    {1037,{-5.79148293F,0.86482501F,2.03497028F}},
    {1038,{-5.79961014F,0.86482501F,2.0877142F}},
    {1039,{-5.79765081F,0.805329978F,2.07849598F}},
    {1040,{-5.79008198F,0.821375012F,2.02825642F}},
    {1041,{-5.76284981F,0.724214017F,1.89781284F}},
    {1042,{-5.75957966F,0.672293007F,1.89938831F}},
    {1043,{-5.79196501F,0.751658976F,2.05174541F}},
    {1044,{-5.78601456F,0.782177985F,2.00877476F}},
    {1045,{-5.7831068F,0.70906502F,2.01008129F}},
    {1046,{-5.77968311F,0.751069009F,1.97843075F}},
    {1047,{-5.77194881F,0.681713998F,1.95758343F}},
    {1048,{-5.77169895F,0.731095016F,1.94019413F}}
}};
constexpr std::array<SightGeometryTriangle,38> r12_s0_triangles{{
    {29,{47,48,49}},
    {809,{1012,1013,1014}},
    {810,{1014,1015,1012}},
    {811,{1012,1015,1016}},
    {812,{1014,1013,1017}},
    {813,{1015,1018,1016}},
    {814,{1018,1019,1016}},
    {815,{1016,1019,1020}},
    {816,{1013,1021,1017}},
    {817,{1021,1022,1017}},
    {818,{1021,1023,1022}},
    {819,{1021,1024,1023}},
    {820,{1024,1025,1023}},
    {821,{1026,1025,1024}},
    {822,{1027,1026,1024}},
    {823,{1028,1026,1027}},
    {824,{1029,1028,1027}},
    {825,{1019,1030,1020}},
    {826,{1031,1028,1029}},
    {827,{1032,1031,1029}},
    {828,{1033,1031,1032}},
    {829,{1034,1033,1032}},
    {830,{1035,1033,1034}},
    {831,{1036,1035,1034}},
    {832,{1037,1035,1036}},
    {833,{1038,1037,1036}},
    {834,{1038,1039,1037}},
    {835,{1039,1040,1037}},
    {836,{1020,1030,1041}},
    {837,{1042,1020,1041}},
    {838,{1039,1043,1040}},
    {839,{1043,1044,1040}},
    {840,{1043,1045,1044}},
    {841,{1045,1046,1044}},
    {842,{1045,1047,1046}},
    {843,{1047,1048,1046}},
    {844,{1047,1042,1048}},
    {845,{1042,1041,1048}}
}};
constexpr std::array<SightGeometrySurface,1> r12_surfaces{{
    {0,2738,2526,"mc/mtl_weapon_type99_lmg_wet",r12_s0_ranges,r12_s0_vertices,r12_s0_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r12_front{{
    {0,48,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,20> r12_rear{{
    {0,1014,0.0500000007F},
    {0,1015,0.0500000007F},
    {0,1018,0.0500000007F},
    {0,1019,0.0500000007F},
    {0,1030,0.0500000007F},
    {0,1041,0.0500000007F},
    {0,1048,0.0500000007F},
    {0,1046,0.0500000007F},
    {0,1044,0.0500000007F},
    {0,1040,0.0500000007F},
    {0,1037,0.0500000007F},
    {0,1035,0.0500000007F},
    {0,1033,0.0500000007F},
    {0,1031,0.0500000007F},
    {0,1028,0.0500000007F},
    {0,1026,0.0500000007F},
    {0,1025,0.0500000007F},
    {0,1023,0.0500000007F},
    {0,1022,0.0500000007F},
    {0,1017,0.0500000007F}
}};
const SightGeometryRecipe r12_recipe=[] {
    SightGeometryRecipe value{"transferred-type99_lmg_wet-mesh-v1","viewmodel_jap_type99_lmg_wet",r12_names,r12_parents,r12_binds,5,0,6,r12_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r12_front},{0,SightGeometryReferenceMethod::weighted_centroid,r12_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,8> r13_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "j_round",
    "j_round1",
    "j_stripper",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,7> r13_parents{{
    1,
    2,
    3,
    4,
    5,
    6,
    7
}};
constexpr std::array<SightGeometryBone,8> r13_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-7.73968983F,-0.000422000012F,1.14696789F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{1.93665385F,37.1242371F,8.02200031F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.21501803F,37.1427116F,8.01028633F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.21501803F,37.1427116F,9.57577229F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{0.910748839F,37.1515846F,8.03604794F},2.0F},
    {{-0.332255006F,-0.354594558F,-0.604419053F,0.631305873F},{-2.58193994F,-0.0815270022F,1.75518703F},1.99999952F},
    {{0.0F,0.0F,0.0F,1.0F},{31.2897587F,0.0F,1.15711308F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r13_s3_ranges{{
    {0,2738,0,2558}
}};
constexpr std::array<SightGeometryVertex,21> r13_s3_vertices{{
    {0,{29.3475647F,0.0941030011F,1.85752392F}},
    {1,{29.3475647F,0.0F,2.16704178F}},
    {2,{29.3475647F,-0.0941030011F,1.85752392F}},
    {2213,{3.74763775F,0.0F,1.88805914F}},
    {2214,{3.81006384F,0.0F,1.88805914F}},
    {2215,{3.81006384F,-0.0530809984F,1.89646673F}},
    {2216,{3.74763775F,-0.0530809984F,1.89646673F}},
    {2222,{3.74763775F,-0.163371995F,2.00675583F}},
    {2223,{3.81006384F,-0.171779007F,2.05983877F}},
    {2224,{3.74763775F,-0.171779007F,2.05983877F}},
    {2225,{3.81006384F,-0.163371995F,2.11292076F}},
    {2226,{3.74763775F,-0.163371995F,2.11292076F}},
    {2231,{3.81006384F,-0.0530809984F,2.22321177F}},
    {2232,{3.74763775F,-0.0530809984F,2.22321177F}},
    {2233,{3.74763775F,0.0F,2.23161983F}},
    {2234,{3.81006384F,0.0F,2.23161983F}},
    {2338,{3.74763775F,0.163371995F,2.00675583F}},
    {2339,{3.81006384F,0.171780005F,2.05983877F}},
    {2340,{3.74763775F,0.171780005F,2.05983877F}},
    {2341,{3.81006384F,0.163371995F,2.11292076F}},
    {2342,{3.74763775F,0.163371995F,2.11292076F}}
}};
constexpr std::array<SightGeometryTriangle,11> r13_s3_triangles{{
    {0,{0,1,2}},
    {2023,{2213,2214,2215}},
    {2024,{2216,2213,2215}},
    {2032,{2224,2222,2223}},
    {2033,{2223,2225,2224}},
    {2034,{2225,2226,2224}},
    {2041,{2233,2232,2231}},
    {2042,{2234,2233,2231}},
    {2125,{2340,2339,2338}},
    {2126,{2339,2340,2341}},
    {2127,{2340,2342,2341}}
}};
constexpr std::array<SightGeometrySurface,1> r13_surfaces{{
    {3,2738,2558,"mc/mtl_weapon_type99rifle_metal",r13_s3_ranges,r13_s3_vertices,r13_s3_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r13_front{{
    {3,1,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,4> r13_rear{{
    {3,2213,1.0F},
    {3,2224,1.0F},
    {3,2233,1.0F},
    {3,2340,1.0F}
}};
const SightGeometryRecipe r13_recipe=[] {
    SightGeometryRecipe value{"transferred-type99_rifle-mesh-v1","viewmodel_jap_type99_rifle",r13_names,r13_parents,r13_binds,6,0,7,r13_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r13_front},{0,SightGeometryReferenceMethod::aperture_circle_yz,r13_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,9> r14_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "j_round",
    "j_round1",
    "j_stripper",
    "tag_bayonet",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,8> r14_parents{{
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8
}};
constexpr std::array<SightGeometryBone,9> r14_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-7.73968983F,-0.000422000012F,1.14696789F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{1.93665385F,37.1242371F,8.02200031F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.21501803F,37.1427116F,8.01028633F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.21501803F,37.1427116F,9.57577229F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{0.910748839F,37.1515846F,8.03604794F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{28.161459F,0.0F,-0.494206905F},2.0F},
    {{-0.332255006F,-0.354594558F,-0.604419053F,0.631305873F},{-2.58193994F,-0.0815270022F,1.75518703F},1.99999952F},
    {{0.0F,0.0F,0.0F,1.0F},{31.2897587F,0.0F,1.15711308F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r14_s3_ranges{{
    {0,2738,0,2558}
}};
constexpr std::array<SightGeometryVertex,21> r14_s3_vertices{{
    {0,{29.3475647F,0.0941030011F,1.85752392F}},
    {1,{29.3475647F,0.0F,2.16704178F}},
    {2,{29.3475647F,-0.0941030011F,1.85752392F}},
    {2213,{3.74763775F,0.0F,1.88805914F}},
    {2214,{3.81006384F,0.0F,1.88805914F}},
    {2215,{3.81006384F,-0.0530809984F,1.89646673F}},
    {2216,{3.74763775F,-0.0530809984F,1.89646673F}},
    {2222,{3.74763775F,-0.163371995F,2.00675583F}},
    {2223,{3.81006384F,-0.171779007F,2.05983877F}},
    {2224,{3.74763775F,-0.171779007F,2.05983877F}},
    {2225,{3.81006384F,-0.163371995F,2.11292076F}},
    {2226,{3.74763775F,-0.163371995F,2.11292076F}},
    {2231,{3.81006384F,-0.0530809984F,2.22321177F}},
    {2232,{3.74763775F,-0.0530809984F,2.22321177F}},
    {2233,{3.74763775F,0.0F,2.23161983F}},
    {2234,{3.81006384F,0.0F,2.23161983F}},
    {2338,{3.74763775F,0.163371995F,2.00675583F}},
    {2339,{3.81006384F,0.171780005F,2.05983877F}},
    {2340,{3.74763775F,0.171780005F,2.05983877F}},
    {2341,{3.81006384F,0.163371995F,2.11292076F}},
    {2342,{3.74763775F,0.163371995F,2.11292076F}}
}};
constexpr std::array<SightGeometryTriangle,11> r14_s3_triangles{{
    {0,{0,1,2}},
    {2023,{2213,2214,2215}},
    {2024,{2216,2213,2215}},
    {2032,{2224,2222,2223}},
    {2033,{2223,2225,2224}},
    {2034,{2225,2226,2224}},
    {2041,{2233,2232,2231}},
    {2042,{2234,2233,2231}},
    {2125,{2340,2339,2338}},
    {2126,{2339,2340,2341}},
    {2127,{2340,2342,2341}}
}};
constexpr std::array<SightGeometrySurface,1> r14_surfaces{{
    {3,2738,2558,"mc/mtl_weapon_type99rifle_metal",r14_s3_ranges,r14_s3_vertices,r14_s3_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r14_front{{
    {3,1,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,4> r14_rear{{
    {3,2213,1.0F},
    {3,2224,1.0F},
    {3,2233,1.0F},
    {3,2340,1.0F}
}};
const SightGeometryRecipe r14_recipe=[] {
    SightGeometryRecipe value{"transferred-type99_rifle_bayonet-mesh-v1","viewmodel_jap_type99_rifle_bayonet",r14_names,r14_parents,r14_binds,7,0,8,r14_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r14_front},{0,SightGeometryReferenceMethod::aperture_circle_yz,r14_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,9> r15_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "j_round",
    "j_round1",
    "j_stripper",
    "tag_bayonet",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,8> r15_parents{{
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8
}};
constexpr std::array<SightGeometryBone,9> r15_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-7.73968983F,-0.000422000012F,1.14696789F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{1.93665385F,37.1242371F,8.02200031F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.21501803F,37.1427116F,8.01028633F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.21501803F,37.1427116F,9.57577229F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{0.910748839F,37.1515846F,8.03604794F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{28.161459F,0.0F,-0.494206905F},2.0F},
    {{-0.332255006F,-0.354594558F,-0.604419053F,0.631305873F},{-2.58193994F,-0.0815270022F,1.75518703F},1.99999952F},
    {{0.0F,0.0F,0.0F,1.0F},{31.2897587F,0.0F,1.15711308F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r15_s3_ranges{{
    {0,2738,0,2558}
}};
constexpr std::array<SightGeometryVertex,21> r15_s3_vertices{{
    {0,{29.3475647F,0.0941030011F,1.85752392F}},
    {1,{29.3475647F,0.0F,2.16704178F}},
    {2,{29.3475647F,-0.0941030011F,1.85752392F}},
    {2213,{3.74763775F,0.0F,1.88805914F}},
    {2214,{3.81006384F,0.0F,1.88805914F}},
    {2215,{3.81006384F,-0.0530809984F,1.89646673F}},
    {2216,{3.74763775F,-0.0530809984F,1.89646673F}},
    {2222,{3.74763775F,-0.163371995F,2.00675583F}},
    {2223,{3.81006384F,-0.171779007F,2.05983877F}},
    {2224,{3.74763775F,-0.171779007F,2.05983877F}},
    {2225,{3.81006384F,-0.163371995F,2.11292076F}},
    {2226,{3.74763775F,-0.163371995F,2.11292076F}},
    {2231,{3.81006384F,-0.0530809984F,2.22321177F}},
    {2232,{3.74763775F,-0.0530809984F,2.22321177F}},
    {2233,{3.74763775F,0.0F,2.23161983F}},
    {2234,{3.81006384F,0.0F,2.23161983F}},
    {2338,{3.74763775F,0.163371995F,2.00675583F}},
    {2339,{3.81006384F,0.171780005F,2.05983877F}},
    {2340,{3.74763775F,0.171780005F,2.05983877F}},
    {2341,{3.81006384F,0.163371995F,2.11292076F}},
    {2342,{3.74763775F,0.163371995F,2.11292076F}}
}};
constexpr std::array<SightGeometryTriangle,11> r15_s3_triangles{{
    {0,{0,1,2}},
    {2023,{2213,2214,2215}},
    {2024,{2216,2213,2215}},
    {2032,{2224,2222,2223}},
    {2033,{2223,2225,2224}},
    {2034,{2225,2226,2224}},
    {2041,{2233,2232,2231}},
    {2042,{2234,2233,2231}},
    {2125,{2340,2339,2338}},
    {2126,{2339,2340,2341}},
    {2127,{2340,2342,2341}}
}};
constexpr std::array<SightGeometrySurface,1> r15_surfaces{{
    {3,2738,2558,"mc/mtl_weapon_type99rifle_metal_wet",r15_s3_ranges,r15_s3_vertices,r15_s3_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r15_front{{
    {3,1,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,4> r15_rear{{
    {3,2213,1.0F},
    {3,2224,1.0F},
    {3,2233,1.0F},
    {3,2340,1.0F}
}};
const SightGeometryRecipe r15_recipe=[] {
    SightGeometryRecipe value{"transferred-type99_rifle_bayonet_wet-mesh-v1","viewmodel_jap_type99_rifle_bayonet_wet",r15_names,r15_parents,r15_binds,7,0,8,r15_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r15_front},{0,SightGeometryReferenceMethod::aperture_circle_yz,r15_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,8> r16_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "j_round",
    "j_round1",
    "j_stripper",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,7> r16_parents{{
    1,
    2,
    3,
    4,
    5,
    6,
    7
}};
constexpr std::array<SightGeometryBone,8> r16_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-7.73968983F,-0.000422000012F,1.14696789F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{1.93665385F,37.1242371F,8.02200031F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.21501803F,37.1427116F,8.01028633F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.21501803F,37.1427116F,9.57577229F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{0.910748839F,37.1515846F,8.03604794F},2.0F},
    {{-0.332255006F,-0.354594558F,-0.604419053F,0.631305873F},{-2.58193994F,-0.0815270022F,1.75518703F},1.99999952F},
    {{0.0F,0.0F,0.0F,1.0F},{31.2897587F,0.0F,1.15711308F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r16_s3_ranges{{
    {0,2738,0,2558}
}};
constexpr std::array<SightGeometryVertex,21> r16_s3_vertices{{
    {0,{29.3475647F,0.0941030011F,1.85752392F}},
    {1,{29.3475647F,0.0F,2.16704178F}},
    {2,{29.3475647F,-0.0941030011F,1.85752392F}},
    {2213,{3.74763775F,0.0F,1.88805914F}},
    {2214,{3.81006384F,0.0F,1.88805914F}},
    {2215,{3.81006384F,-0.0530809984F,1.89646673F}},
    {2216,{3.74763775F,-0.0530809984F,1.89646673F}},
    {2222,{3.74763775F,-0.163371995F,2.00675583F}},
    {2223,{3.81006384F,-0.171779007F,2.05983877F}},
    {2224,{3.74763775F,-0.171779007F,2.05983877F}},
    {2225,{3.81006384F,-0.163371995F,2.11292076F}},
    {2226,{3.74763775F,-0.163371995F,2.11292076F}},
    {2231,{3.81006384F,-0.0530809984F,2.22321177F}},
    {2232,{3.74763775F,-0.0530809984F,2.22321177F}},
    {2233,{3.74763775F,0.0F,2.23161983F}},
    {2234,{3.81006384F,0.0F,2.23161983F}},
    {2338,{3.74763775F,0.163371995F,2.00675583F}},
    {2339,{3.81006384F,0.171780005F,2.05983877F}},
    {2340,{3.74763775F,0.171780005F,2.05983877F}},
    {2341,{3.81006384F,0.163371995F,2.11292076F}},
    {2342,{3.74763775F,0.163371995F,2.11292076F}}
}};
constexpr std::array<SightGeometryTriangle,11> r16_s3_triangles{{
    {0,{0,1,2}},
    {2023,{2213,2214,2215}},
    {2024,{2216,2213,2215}},
    {2032,{2224,2222,2223}},
    {2033,{2223,2225,2224}},
    {2034,{2225,2226,2224}},
    {2041,{2233,2232,2231}},
    {2042,{2234,2233,2231}},
    {2125,{2340,2339,2338}},
    {2126,{2339,2340,2341}},
    {2127,{2340,2342,2341}}
}};
constexpr std::array<SightGeometrySurface,1> r16_surfaces{{
    {3,2738,2558,"mc/mtl_weapon_type99rifle_metal_wet",r16_s3_ranges,r16_s3_vertices,r16_s3_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r16_front{{
    {3,1,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,4> r16_rear{{
    {3,2213,1.0F},
    {3,2224,1.0F},
    {3,2233,1.0F},
    {3,2340,1.0F}
}};
const SightGeometryRecipe r16_recipe=[] {
    SightGeometryRecipe value{"transferred-type99_rifle_wet-mesh-v1","viewmodel_jap_type99_rifle_wet",r16_names,r16_parents,r16_binds,6,0,7,r16_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r16_front},{0,SightGeometryReferenceMethod::aperture_circle_yz,r16_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,15> r17_names{{
    "j_gun",
    "j_clip",
    "joint1",
    "joint2",
    "tag_brass",
    "tag_flash",
    "j_bolt",
    "tag_bullets",
    "tag_speedloader",
    "tag_spent0",
    "tag_spent1",
    "tag_spent2",
    "tag_spent3",
    "tag_spent4",
    "tag_spent5"
}};
constexpr std::array<std::uint8_t,14> r17_parents{{
    1,
    2,
    3,
    4,
    5,
    3,
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8
}};
constexpr std::array<SightGeometryBone,15> r17_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.34014499F,0.0716319978F,-1.237288F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-0.622479022F,0.00778699992F,0.598280013F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{3.27272606F,0.156212002F,0.235110998F},2.0F},
    {{-0.245246738F,-0.244636372F,-0.664174318F,0.662465274F},{0.960280001F,-0.341722012F,1.1221F},2.00003862F},
    {{0.0F,0.0F,0.0F,1.0F},{10.394556F,-0.000456000009F,2.20409203F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{1.44532394F,-0.00093999505F,1.59472406F},2.00003505F},
    {{0.0F,0.0F,0.0F,0.999982476F},{1.85321391F,-6.25132895F,1.59472799F},2.0000701F},
    {{0.0F,0.0F,0.0F,0.999982476F},{0.000504067168F,-6.25132895F,1.59471035F},2.0000701F},
    {{0.0F,0.0F,0.0F,0.999982476F},{1.36473954F,-12.5012655F,0.795422256F},2.0000701F},
    {{0.0F,0.0F,0.0F,0.999982476F},{1.36473584F,-11.8240805F,1.18952227F},2.0000701F},
    {{0.0F,0.0F,0.0F,0.999982476F},{1.36472833F,-11.8185301F,1.97772217F},2.0000701F},
    {{0.0F,0.0F,0.0F,0.999982476F},{1.36472476F,-12.4957161F,2.36072016F},2.0000701F},
    {{0.0F,0.0F,0.0F,0.999982476F},{1.36472845F,-13.1784515F,1.97217119F},2.0000701F},
    {{0.0F,0.0F,0.0F,0.999982476F},{1.36473584F,-13.1729021F,1.19507325F},2.0000701F}
}};
constexpr std::array<SightGeometryRigidRange,1> r17_s1_ranges{{
    {0,3220,0,3133}
}};
constexpr std::array<SightGeometryVertex,13> r17_s1_vertices{{
    {2476,{-0.00752600003F,0.096983999F,3.35690188F}},
    {2477,{-0.00625200011F,0.096983999F,3.24118805F}},
    {2478,{-0.00752600003F,0.304167986F,3.35690308F}},
    {2494,{-0.00625200011F,-0.0971089974F,3.24118805F}},
    {2495,{0.0589310005F,-0.0971089974F,3.24238896F}},
    {2496,{-0.00752600003F,-0.0971089974F,3.35690188F}},
    {2497,{0.0576569997F,-0.0971089974F,3.35810304F}},
    {2812,{9.46581173F,0.000633999996F,3.29398394F}},
    {2813,{9.46586227F,-0.0915760025F,3.294626F}},
    {2814,{9.46435738F,-0.0934479982F,3.03991795F}},
    {2815,{9.46421528F,0.0F,3.03524995F}},
    {2816,{9.46435928F,0.0934489965F,3.03993607F}},
    {2817,{9.46599674F,0.0928480029F,3.29440498F}}
}};
constexpr std::array<SightGeometryTriangle,7> r17_s1_triangles{{
    {2398,{2476,2477,2478}},
    {2410,{2494,2495,2496}},
    {2411,{2495,2497,2496}},
    {2662,{2812,2813,2814}},
    {2663,{2815,2812,2814}},
    {2664,{2815,2816,2812}},
    {2665,{2816,2817,2812}}
}};
constexpr std::array<SightGeometrySurface,1> r17_surfaces{{
    {1,3220,3134,"mc/mtl_weapon_sw_357",r17_s1_ranges,r17_s1_vertices,r17_s1_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r17_front{{
    {1,2812,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r17_rear{{
    {1,2476,0.5F},
    {1,2496,0.5F}
}};
const SightGeometryRecipe r17_recipe=[] {
    SightGeometryRecipe value{"factory-zombie-sw357-reindexed-mesh-v1","viewmodel_mp_357magnum",r17_names,r17_parents,r17_binds,7,0,5,r17_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r17_front},{0,SightGeometryReferenceMethod::weighted_centroid,r17_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,15> r18_names{{
    "j_gun",
    "j_clip",
    "joint1",
    "joint2",
    "tag_brass",
    "tag_flash",
    "j_bolt",
    "tag_bullets",
    "tag_speedloader",
    "tag_spent0",
    "tag_spent1",
    "tag_spent2",
    "tag_spent3",
    "tag_spent4",
    "tag_spent5"
}};
constexpr std::array<std::uint8_t,14> r18_parents{{
    1,
    2,
    3,
    4,
    5,
    3,
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8
}};
constexpr std::array<SightGeometryBone,15> r18_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.34014499F,0.0716319978F,-1.237288F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-0.622479022F,0.00778699992F,0.598280013F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{3.27272606F,0.156212002F,0.235110998F},2.0F},
    {{-0.245246738F,-0.244636372F,-0.664174318F,0.662465274F},{0.960280001F,-0.341722012F,1.1221F},2.00003862F},
    {{0.0F,0.0F,0.0F,1.0F},{10.394556F,-0.000456000009F,2.20409203F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{1.44532394F,-0.00093999505F,1.59472406F},2.00003505F},
    {{0.0F,0.0F,0.0F,0.999982476F},{1.85321391F,-6.25132895F,1.59472799F},2.0000701F},
    {{0.0F,0.0F,0.0F,0.999982476F},{0.000504067168F,-6.25132895F,1.59471035F},2.0000701F},
    {{0.0F,0.0F,0.0F,0.999982476F},{1.36473954F,-12.5012655F,0.795422256F},2.0000701F},
    {{0.0F,0.0F,0.0F,0.999982476F},{1.36473584F,-11.8240805F,1.18952227F},2.0000701F},
    {{0.0F,0.0F,0.0F,0.999982476F},{1.36472833F,-11.8185301F,1.97772217F},2.0000701F},
    {{0.0F,0.0F,0.0F,0.999982476F},{1.36472476F,-12.4957161F,2.36072016F},2.0000701F},
    {{0.0F,0.0F,0.0F,0.999982476F},{1.36472845F,-13.1784515F,1.97217119F},2.0000701F},
    {{0.0F,0.0F,0.0F,0.999982476F},{1.36473584F,-13.1729021F,1.19507325F},2.0000701F}
}};
constexpr std::array<SightGeometryRigidRange,1> r18_s1_ranges{{
    {0,3220,0,3133}
}};
constexpr std::array<SightGeometryVertex,13> r18_s1_vertices{{
    {2472,{-0.00752600003F,0.096983999F,3.35690188F}},
    {2473,{-0.00625200011F,0.096983999F,3.24118805F}},
    {2474,{-0.00752600003F,0.304167986F,3.35690308F}},
    {2490,{-0.00625200011F,-0.0971089974F,3.24118805F}},
    {2491,{0.0589310005F,-0.0971089974F,3.24238896F}},
    {2492,{-0.00752600003F,-0.0971089974F,3.35690188F}},
    {2493,{0.0576569997F,-0.0971089974F,3.35810304F}},
    {2808,{9.46581173F,0.000633999996F,3.29398394F}},
    {2809,{9.46586227F,-0.0915760025F,3.294626F}},
    {2810,{9.46435738F,-0.0934479982F,3.03991795F}},
    {2811,{9.46421528F,0.0F,3.03524995F}},
    {2812,{9.46435928F,0.0934489965F,3.03993607F}},
    {2813,{9.46599674F,0.0928480029F,3.29440498F}}
}};
constexpr std::array<SightGeometryTriangle,7> r18_s1_triangles{{
    {2396,{2472,2473,2474}},
    {2408,{2490,2491,2492}},
    {2409,{2491,2493,2492}},
    {2660,{2808,2809,2810}},
    {2661,{2811,2808,2810}},
    {2662,{2811,2812,2808}},
    {2663,{2812,2813,2808}}
}};
constexpr std::array<SightGeometrySurface,1> r18_surfaces{{
    {1,3220,3134,"mc/mtl_weapon_sw_357",r18_s1_ranges,r18_s1_vertices,r18_s1_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r18_front{{
    {1,2808,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r18_rear{{
    {1,2472,0.5F},
    {1,2492,0.5F}
}};
const SightGeometryRecipe r18_recipe=[] {
    SightGeometryRecipe value{"sw_357-reviewed-landmarks-v1","viewmodel_mp_357magnum",r18_names,r18_parents,r18_binds,7,0,5,r18_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r18_front},{0,SightGeometryReferenceMethod::weighted_centroid,r18_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,9> r19_names{{
    "j_gun",
    "j_bolt",
    "j_bolt1",
    "j_clip",
    "tag_brass",
    "tag_flash",
    "tag_round1",
    "tag_round2",
    "tag_stripper"
}};
constexpr std::array<std::uint8_t,8> r19_parents{{
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8
}};
constexpr std::array<SightGeometryBone,9> r19_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.14310801F,-0.00133500004F,1.00245297F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.14142394F,-0.000483000011F,1.00303102F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{2.09333301F,97.5681534F,8.11742496F},2.0F},
    {{-0.244819477F,-0.154881433F,-0.651509166F,0.701162755F},{-0.929933012F,-0.630271971F,1.1221F},1.99996352F},
    {{0.0F,0.0F,0.0F,1.0F},{28.704195F,0.0F,1.08217597F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-0.786285996F,97.5681534F,9.66814137F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-0.786285996F,97.5681534F,8.13062954F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{1.89285398F,97.5681534F,8.11742496F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r19_s5_ranges{{
    {0,1890,0,1772}
}};
constexpr std::array<SightGeometryVertex,10> r19_s5_vertices{{
    {59,{5.77088785F,0.0672189966F,1.977687F}},
    {60,{5.77856493F,0.0672189966F,1.98528504F}},
    {61,{5.77876091F,0.0593440011F,1.97741103F}},
    {62,{5.77876091F,-0.0593440011F,1.97741103F}},
    {63,{5.77856493F,-0.0672189966F,1.98528504F}},
    {64,{5.77088785F,-0.0672189966F,1.977687F}},
    {408,{26.1139183F,-0.00506700017F,1.90765095F}},
    {409,{26.1139183F,0.00506700017F,1.90765095F}},
    {410,{26.4453773F,-0.00506700017F,2.04486394F}},
    {411,{26.4453773F,0.00506700017F,2.04486394F}}
}};
constexpr std::array<SightGeometryTriangle,4> r19_s5_triangles{{
    {43,{59,60,61}},
    {44,{62,63,64}},
    {341,{408,409,410}},
    {342,{409,411,410}}
}};
constexpr std::array<SightGeometrySurface,1> r19_surfaces{{
    {5,1890,1772,"mc/mtl_weapon_mosin",r19_s5_ranges,r19_s5_vertices,r19_s5_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,4> r19_front{{
    {5,408,1.0F},
    {5,409,1.0F},
    {5,410,1.0F},
    {5,411,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r19_rear{{
    {5,64,1.0F},
    {5,59,1.0F}
}};
const SightGeometryRecipe r19_recipe=[] {
    SightGeometryRecipe value{"transferred-mosin_rifle-mesh-v1","viewmodel_rus_mosinnagant_rifle",r19_names,r19_parents,r19_binds,7,0,5,r19_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r19_front},{0,SightGeometryReferenceMethod::weighted_centroid,r19_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,7> r20_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "j_release",
    "tag_brass",
    "tag_flash",
    "tag_iron_sights"
}};
constexpr std::array<std::uint8_t,6> r20_parents{{
    1,
    2,
    3,
    4,
    5,
    6
}};
constexpr std::array<SightGeometryBone,7> r20_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{0.145243004F,-0.907276988F,1.80724895F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{2.16286397F,0.00130899996F,0.558384001F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{1.90295804F,0.076820001F,-0.303932011F},2.0F},
    {{-0.189458907F,-0.233954892F,-0.600146472F,0.74108094F},{1.61995995F,-0.341722012F,2.32218599F},1.99998736F},
    {{0.0F,0.0F,0.0F,1.0F},{16.3198547F,0.076820001F,1.52387404F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{13.7902651F,0.00130899996F,2.68176293F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r20_s0_ranges{{
    {0,3925,0,3934}
}};
constexpr std::array<SightGeometryVertex,6> r20_s0_vertices{{
    {0,{-1.23352098F,0.0507929996F,2.87958193F}},
    {1,{-1.23082101F,0.0507929996F,2.88228202F}},
    {2,{-1.23082101F,0.0480940007F,2.87958193F}},
    {6,{-1.23082101F,-0.0507929996F,2.88228202F}},
    {7,{-1.23352098F,-0.0507929996F,2.87958193F}},
    {8,{-1.23082101F,-0.0480940007F,2.87958193F}}
}};
constexpr std::array<SightGeometryTriangle,2> r20_s0_triangles{{
    {0,{0,1,2}},
    {2,{6,7,8}}
}};
constexpr std::array<SightGeometryRigidRange,1> r20_s4_ranges{{
    {6,468,0,398}
}};
constexpr std::array<SightGeometryVertex,4> r20_s4_vertices{{
    {418,{13.6875782F,-0.0407470018F,2.91121197F}},
    {419,{13.9061785F,-0.0407470018F,2.91121197F}},
    {420,{13.6032791F,-0.0407470018F,2.5175221F}},
    {421,{13.9932795F,-0.0407470018F,2.5175221F}}
}};
constexpr std::array<SightGeometryTriangle,2> r20_s4_triangles{{
    {366,{418,419,420}},
    {367,{419,421,420}}
}};
constexpr std::array<SightGeometrySurface,2> r20_surfaces{{
    {0,3925,3934,"mc/mtl_rus_smg_ppsh41",r20_s0_ranges,r20_s0_vertices,r20_s0_triangles},
    {4,468,398,"mc/mtl_rus_smg_ppsh41",r20_s4_ranges,r20_s4_vertices,r20_s4_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,4> r20_front{{
    {4,418,1.0F},
    {4,419,1.0F},
    {4,420,1.0F},
    {4,421,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r20_rear{{
    {0,2,1.0F},
    {0,8,1.0F}
}};
const SightGeometryRecipe r20_recipe=[] {
    SightGeometryRecipe value{"ppsh-root-reviewed-irons-v1","viewmodel_rus_ppsh_smg",r20_names,r20_parents,r20_binds,6,0,5,r20_surfaces,{6,SightGeometryReferenceMethod::weighted_centroid,r20_front},{0,SightGeometryReferenceMethod::weighted_centroid,r20_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,6> r21_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_brass",
    "tag_flash",
    "tag_iron_sights"
}};
constexpr std::array<std::uint8_t,5> r21_parents{{
    1,
    2,
    3,
    4,
    5
}};
constexpr std::array<SightGeometryBone,6> r21_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-2.575804F,0.0164710004F,1.26191795F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.91700006F,0.103077002F,-0.779568017F},2.0F},
    {{-0.22046572F,-0.201269567F,-0.704824984F,0.643513262F},{-3.491961F,-0.373928994F,1.23536897F},1.99999571F},
    {{0.0F,0.0F,0.0F,1.0F},{28.0327682F,0.199437007F,1.22891605F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{21.9552326F,0.199437007F,1.16029704F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r21_s0_ranges{{
    {5,108,0,86}
}};
constexpr std::array<SightGeometryVertex,7> r21_s0_vertices{{
    {72,{22.1262722F,-0.0217310004F,2.40318203F}},
    {73,{22.1564026F,-0.106288999F,1.90772104F}},
    {74,{21.7554264F,-0.0123330001F,2.32033491F}},
    {76,{22.1262722F,-0.0217310004F,2.40318203F}},
    {77,{21.7554264F,-0.0123330001F,2.32033491F}},
    {78,{22.1262722F,0.0224959999F,2.40318203F}},
    {79,{21.7554264F,0.0130979996F,2.32033491F}}
}};
constexpr std::array<SightGeometryTriangle,3> r21_s0_triangles{{
    {56,{72,73,74}},
    {58,{76,77,78}},
    {59,{77,79,78}}
}};
constexpr std::array<SightGeometryRigidRange,1> r21_s2_ranges{{
    {0,1919,0,1862}
}};
constexpr std::array<SightGeometryVertex,6> r21_s2_vertices{{
    {1515,{-0.705757022F,-0.424115002F,2.09503388F}},
    {1516,{-0.69303298F,-0.135316998F,2.12349796F}},
    {1517,{-0.705716014F,-0.106431998F,2.09503388F}},
    {1519,{-0.69303298F,0.141299993F,2.11919308F}},
    {1520,{-0.704118013F,0.424115002F,2.09503388F}},
    {1521,{-0.705120981F,0.114422001F,2.09503388F}}
}};
constexpr std::array<SightGeometryTriangle,2> r21_s2_triangles{{
    {1563,{1515,1517,1516}},
    {1565,{1519,1521,1520}}
}};
constexpr std::array<SightGeometrySurface,2> r21_surfaces{{
    {0,108,86,"mc/mtl_weapon_svt40",r21_s0_ranges,r21_s0_vertices,r21_s0_triangles},
    {2,1919,1862,"mc/mtl_weapon_svt40",r21_s2_ranges,r21_s2_vertices,r21_s2_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r21_front{{
    {0,72,1.0F},
    {0,78,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r21_rear{{
    {2,1517,1.0F},
    {2,1521,1.0F}
}};
const SightGeometryRecipe r21_recipe=[] {
    SightGeometryRecipe value{"svt40-reviewed-russian-irons-v1","viewmodel_rus_svt40_rifle",r21_names,r21_parents,r21_binds,5,0,4,r21_surfaces,{5,SightGeometryReferenceMethod::weighted_centroid,r21_front},{0,SightGeometryReferenceMethod::weighted_centroid,r21_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,7> r22_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "joint1",
    "joint2",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,6> r22_parents{{
    1,
    2,
    3,
    4,
    5,
    6
}};
constexpr std::array<SightGeometryBone,7> r22_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-1.95220101F,0.0677390024F,1.57221496F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.58178103F,0.0716319978F,-1.74182403F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.23852396F,0.0518350005F,0.526058018F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{0.372808009F,0.444822997F,0.385870993F},2.0F},
    {{-0.245246738F,-0.244636372F,-0.664174318F,0.662465274F},{0.960280001F,-0.341722012F,1.1221F},2.00003862F},
    {{0.0F,0.0F,0.0F,1.0F},{5.43349218F,0.0769210011F,1.175313F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r22_s3_ranges{{
    {1,1220,0,1195}
}};
constexpr std::array<SightGeometryVertex,19> r22_s3_vertices{{
    {678,{-1.71406305F,0.192121997F,1.80290401F}},
    {679,{-1.61973298F,0.192121997F,1.80290401F}},
    {680,{-1.71406305F,0.0399350002F,1.80290401F}},
    {681,{-1.61973298F,0.0399350002F,1.80290401F}},
    {682,{-1.71406305F,0.039694F,1.78919101F}},
    {694,{-1.71406305F,-0.039694F,1.78919101F}},
    {695,{-1.61973298F,-0.039694F,1.78919101F}},
    {696,{-1.71406305F,-0.0399350002F,1.80290401F}},
    {697,{-1.61973298F,-0.0399350002F,1.80290401F}},
    {698,{-1.71406305F,-0.192121997F,1.80290401F}},
    {841,{4.60592318F,-0.0134490002F,1.79050004F}},
    {845,{5.08960009F,-0.00918599963F,1.80669498F}},
    {846,{5.06136084F,-0.00918599963F,1.81231296F}},
    {847,{4.60592318F,0.0134490002F,1.79050004F}},
    {848,{5.06136084F,0.00918599963F,1.81231296F}},
    {849,{4.60592318F,-0.0134490002F,1.79050004F}},
    {850,{5.06136084F,-0.00918599963F,1.81231296F}},
    {851,{5.08960009F,-0.00918599963F,1.80669498F}},
    {852,{5.08960009F,0.00918599963F,1.80669498F}}
}};
constexpr std::array<SightGeometryTriangle,11> r22_s3_triangles{{
    {679,{678,679,680}},
    {680,{679,681,680}},
    {681,{682,680,681}},
    {695,{696,694,695}},
    {696,{697,696,695}},
    {697,{696,697,698}},
    {827,{846,845,841}},
    {828,{847,848,849}},
    {829,{848,850,849}},
    {830,{850,848,851}},
    {831,{848,852,851}}
}};
constexpr std::array<SightGeometrySurface,1> r22_surfaces{{
    {3,1220,1196,"mc/mtl_tokarevtt30_pistol",r22_s3_ranges,r22_s3_vertices,r22_s3_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r22_front{{
    {3,846,1.0F},
    {3,848,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r22_rear{{
    {3,680,1.0F},
    {3,696,1.0F}
}};
const SightGeometryRecipe r22_recipe=[] {
    SightGeometryRecipe value{"tokarev-root-reviewed-irons-v1","viewmodel_rus_tt30_pistol",r22_names,r22_parents,r22_binds,5,0,6,r22_surfaces,{1,SightGeometryReferenceMethod::weighted_centroid,r22_front},{1,SightGeometryReferenceMethod::weighted_centroid,r22_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,8> r23_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_bipod",
    "tag_bipod_left",
    "tag_bipod_right",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,7> r23_parents{{
    1,
    2,
    3,
    4,
    5,
    6,
    7
}};
constexpr std::array<SightGeometryBone,8> r23_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-2.24046206F,1.69735706F,0.228652015F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{-0.218616962F,-0.242084995F,-0.715187967F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{23.6939392F,0.0F,1.06821501F},2.0F},
    {{-0.00268562883F,0.48780787F,0.87292093F,0.00674459059F},{23.6656094F,1.22904503F,0.065598011F},1.99999976F},
    {{9.15555283e-05F,-0.485824138F,0.87405014F,0.00335703604F},{23.6656094F,-1.20881796F,0.0767610222F},2.0F},
    {{-0.296182126F,-0.159855947F,-0.775719464F,0.533829749F},{0.0901520252F,-0.933917999F,1.12210107F},2.00001478F},
    {{0.0F,0.0F,0.0F,1.0F},{29.4949856F,0.0F,0.919674993F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r23_s4_ranges{{
    {0,4170,0,4296}
}};
constexpr std::array<SightGeometryVertex,11> r23_s4_vertices{{
    {258,{24.7059593F,-0.0282959994F,2.39989519F}},
    {259,{24.7067986F,0.0522719994F,2.39989519F}},
    {260,{24.7063789F,0.011988F,2.77093124F}},
    {855,{-6.24839973F,-0.156479999F,2.08868217F}},
    {858,{-6.24842644F,-0.0534759983F,2.15872407F}},
    {859,{-6.24842453F,-0.0500359982F,2.15543199F}},
    {860,{-6.24842739F,-0.0402209982F,2.08868217F}},
    {866,{-6.24842739F,0.053679999F,2.08868217F}},
    {867,{-6.24839973F,0.169938996F,2.08868217F}},
    {868,{-6.24842453F,0.0634950027F,2.15543199F}},
    {870,{-6.24842644F,0.0669350028F,2.15872407F}}
}};
constexpr std::array<SightGeometryTriangle,5> r23_s4_triangles{{
    {269,{258,259,260}},
    {900,{859,858,855}},
    {901,{860,859,855}},
    {909,{866,867,868}},
    {911,{868,867,870}}
}};
constexpr std::array<SightGeometrySurface,1> r23_surfaces{{
    {4,4170,4296,"mc/mtl_weapon_bar",r23_s4_ranges,r23_s4_vertices,r23_s4_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r23_front{{
    {4,260,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r23_rear{{
    {4,859,0.5F},
    {4,868,0.5F}
}};
const SightGeometryRecipe r23_recipe=[] {
    SightGeometryRecipe value{"transferred-bar_bipod-mesh-v1","viewmodel_usa_bar_bipod_lmg",r23_names,r23_parents,r23_binds,7,0,7,r23_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r23_front},{0,SightGeometryReferenceMethod::weighted_centroid,r23_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,8> r24_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_bipod",
    "tag_bipod_left",
    "tag_bipod_right",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,7> r24_parents{{
    1,
    2,
    3,
    4,
    5,
    6,
    7
}};
constexpr std::array<SightGeometryBone,8> r24_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-2.24046206F,1.69735706F,0.228652015F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{-0.218616962F,-0.242084995F,-0.715187967F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{23.6939392F,0.0F,1.06821501F},2.0F},
    {{-0.00268562883F,0.48780787F,0.87292093F,0.00674459059F},{23.6656094F,1.22904503F,0.065598011F},1.99999976F},
    {{9.15555283e-05F,-0.485824138F,0.87405014F,0.00335703604F},{23.6656094F,-1.20881796F,0.0767610222F},2.0F},
    {{-0.296182126F,-0.159855947F,-0.775719464F,0.533829749F},{0.0901520252F,-0.933917999F,1.12210107F},2.00001478F},
    {{0.0F,0.0F,0.0F,1.0F},{29.4949856F,0.0F,0.919674993F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r24_s4_ranges{{
    {0,4170,0,4296}
}};
constexpr std::array<SightGeometryVertex,11> r24_s4_vertices{{
    {258,{24.7059593F,-0.0282959994F,2.39989519F}},
    {259,{24.7067986F,0.0522719994F,2.39989519F}},
    {260,{24.7063789F,0.011988F,2.77093124F}},
    {855,{-6.24839973F,-0.156479999F,2.08868217F}},
    {858,{-6.24842644F,-0.0534759983F,2.15872407F}},
    {859,{-6.24842453F,-0.0500359982F,2.15543199F}},
    {860,{-6.24842739F,-0.0402209982F,2.08868217F}},
    {866,{-6.24842739F,0.053679999F,2.08868217F}},
    {867,{-6.24839973F,0.169938996F,2.08868217F}},
    {868,{-6.24842453F,0.0634950027F,2.15543199F}},
    {870,{-6.24842644F,0.0669350028F,2.15872407F}}
}};
constexpr std::array<SightGeometryTriangle,5> r24_s4_triangles{{
    {269,{258,259,260}},
    {900,{859,858,855}},
    {901,{860,859,855}},
    {909,{866,867,868}},
    {911,{868,867,870}}
}};
constexpr std::array<SightGeometrySurface,1> r24_surfaces{{
    {4,4170,4296,"mc/mtl_weapon_bar",r24_s4_ranges,r24_s4_vertices,r24_s4_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r24_front{{
    {4,260,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r24_rear{{
    {4,859,0.5F},
    {4,868,0.5F}
}};
const SightGeometryRecipe r24_recipe=[] {
    SightGeometryRecipe value{"bar-reviewed-landmarks-v1","viewmodel_usa_bar_lmg",r24_names,r24_parents,r24_binds,7,0,7,r24_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r24_front},{0,SightGeometryReferenceMethod::weighted_centroid,r24_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,7> r25_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "joint1",
    "joint2",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,6> r25_parents{{
    1,
    2,
    3,
    4,
    5,
    6
}};
constexpr std::array<SightGeometryBone,7> r25_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-1.95220101F,0.0677390024F,1.57221496F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.34014499F,0.0716319978F,-1.237288F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.12403011F,0.0518350005F,0.269304991F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{0.898801982F,0.444822997F,0.385870993F},2.0F},
    {{-0.245246738F,-0.244636372F,-0.664174318F,0.662465274F},{0.960280001F,-0.341722012F,1.1221F},2.00003862F},
    {{0.0F,0.0F,0.0F,1.0F},{5.43349218F,0.0769210011F,1.175313F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r25_s2_ranges{{
    {1,1326,0,1050}
}};
constexpr std::array<SightGeometryVertex,15> r25_s2_vertices{{
    {340,{-1.35163701F,0.0289360005F,1.67601299F}},
    {341,{-1.35163701F,0.0255069993F,1.63829505F}},
    {342,{-1.35638702F,0.0348550007F,1.67256498F}},
    {343,{-1.35638702F,0.0311329998F,1.63810098F}},
    {344,{-1.35163701F,-0.0644740015F,1.678702F}},
    {345,{-1.35638702F,-0.0700739995F,1.67684495F}},
    {346,{-1.35163701F,-0.0623680018F,1.64079404F}},
    {347,{-1.35638702F,-0.0672219992F,1.63915396F}},
    {1184,{5.05669785F,-0.0573970005F,1.52226698F}},
    {1185,{5.34775877F,-0.0573959984F,1.68137598F}},
    {1186,{5.53943205F,-0.0573959984F,1.50177598F}},
    {1187,{5.50393677F,-0.0573959984F,1.62471294F}},
    {1189,{5.05669785F,0.0189970005F,1.52226698F}},
    {1190,{5.34775877F,-0.0573959984F,1.68137598F}},
    {1191,{5.34775877F,0.0189970005F,1.68137598F}}
}};
constexpr std::array<SightGeometryTriangle,7> r25_s2_triangles{{
    {229,{340,341,342}},
    {230,{341,343,342}},
    {231,{344,345,346}},
    {232,{345,347,346}},
    {907,{1184,1185,1186}},
    {908,{1185,1187,1186}},
    {910,{1189,1191,1190}}
}};
constexpr std::array<SightGeometrySurface,1> r25_surfaces{{
    {2,1326,1050,"mc/mtl_weapon_colt45",r25_s2_ranges,r25_s2_vertices,r25_s2_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r25_front{{
    {2,1185,1.0F},
    {2,1191,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r25_rear{{
    {2,345,1.0F},
    {2,342,1.0F}
}};
const SightGeometryRecipe r25_recipe=[] {
    SightGeometryRecipe value{"colt-notch-geometry-bank-v1","viewmodel_usa_colt45_pistol",r25_names,r25_parents,r25_binds,5,0,6,r25_surfaces,{1,SightGeometryReferenceMethod::weighted_centroid,r25_front},{1,SightGeometryReferenceMethod::weighted_centroid,r25_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,7> r26_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "joint1",
    "joint2",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,6> r26_parents{{
    1,
    2,
    3,
    4,
    5,
    6
}};
constexpr std::array<SightGeometryBone,7> r26_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-1.95220101F,0.0677390024F,1.57221496F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.34014499F,0.0716319978F,-1.237288F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.12403011F,0.0518350005F,0.269304991F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{0.898801982F,0.444822997F,0.385870993F},2.0F},
    {{-0.245246738F,-0.244636372F,-0.664174318F,0.662465274F},{0.960280001F,-0.341722012F,1.1221F},2.00003862F},
    {{0.0F,0.0F,0.0F,1.0F},{5.43349218F,0.0769210011F,1.175313F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r26_s2_ranges{{
    {1,1326,0,1050}
}};
constexpr std::array<SightGeometryVertex,15> r26_s2_vertices{{
    {340,{-1.35163701F,0.0289360005F,1.67601299F}},
    {341,{-1.35163701F,0.0255069993F,1.63829505F}},
    {342,{-1.35638702F,0.0348550007F,1.67256498F}},
    {343,{-1.35638702F,0.0311329998F,1.63810098F}},
    {344,{-1.35163701F,-0.0644740015F,1.678702F}},
    {345,{-1.35638702F,-0.0700739995F,1.67684495F}},
    {346,{-1.35163701F,-0.0623680018F,1.64079404F}},
    {347,{-1.35638702F,-0.0672219992F,1.63915396F}},
    {1184,{5.05669785F,-0.0573970005F,1.52226698F}},
    {1185,{5.34775877F,-0.0573959984F,1.68137598F}},
    {1186,{5.53943205F,-0.0573959984F,1.50177598F}},
    {1187,{5.50393677F,-0.0573959984F,1.62471294F}},
    {1189,{5.05669785F,0.0189970005F,1.52226698F}},
    {1190,{5.34775877F,-0.0573959984F,1.68137598F}},
    {1191,{5.34775877F,0.0189970005F,1.68137598F}}
}};
constexpr std::array<SightGeometryTriangle,7> r26_s2_triangles{{
    {229,{340,341,342}},
    {230,{341,343,342}},
    {231,{344,345,346}},
    {232,{345,347,346}},
    {907,{1184,1185,1186}},
    {908,{1185,1187,1186}},
    {910,{1189,1191,1190}}
}};
constexpr std::array<SightGeometrySurface,1> r26_surfaces{{
    {2,1326,1050,"mc/mtl_weapon_colt45_wet",r26_s2_ranges,r26_s2_vertices,r26_s2_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r26_front{{
    {2,1185,1.0F},
    {2,1191,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r26_rear{{
    {2,345,1.0F},
    {2,342,1.0F}
}};
const SightGeometryRecipe r26_recipe=[] {
    SightGeometryRecipe value{"transferred-colt_wet-mesh-v1","viewmodel_usa_colt45_pistol_wet",r26_names,r26_parents,r26_binds,5,0,6,r26_surfaces,{1,SightGeometryReferenceMethod::weighted_centroid,r26_front},{1,SightGeometryReferenceMethod::weighted_centroid,r26_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,9> r27_names{{
    "j_gun",
    "tag_barrels",
    "tag_brass",
    "tag_lever",
    "tag_extractor",
    "tag_flash",
    "tag_grip",
    "tag_shell1",
    "tag_shell2"
}};
constexpr std::array<std::uint8_t,8> r27_parents{{
    1,
    2,
    3,
    3,
    4,
    5,
    3,
    4
}};
constexpr std::array<SightGeometryBone,9> r27_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{2.62091112F,0.00678599998F,0.0488209724F},2.0F},
    {{-0.244819477F,-0.154881433F,-0.651509166F,0.701162755F},{-0.929933548F,-0.630271971F,1.12210083F},1.99996352F},
    {{0.0F,-0.0954924151F,0.0F,0.995422244F},{-1.52936459F,0.0F,1.7459569F},2.00003147F},
    {{0.0F,0.0F,0.0F,1.0F},{-0.00319385529F,0.00119099999F,1.18299389F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{35.3361778F,0.0F,1.25504494F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{7.70002127F,-0.000765999779F,0.224842787F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{1.32945395F,-0.623425007F,1.1901319F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{1.32945395F,0.627842009F,1.18909788F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r27_s4_ranges{{
    {1,1988,0,2168}
}};
constexpr std::array<SightGeometryVertex,6> r27_s4_vertices{{
    {776,{22.6552505F,-0.0194579996F,1.93348098F}},
    {777,{22.6314659F,-0.0265180003F,1.93348098F}},
    {778,{22.6314697F,-2.09999998e-05F,1.94017005F}},
    {820,{35.0954742F,-0.0263650008F,1.91469979F}},
    {821,{35.0716896F,-0.0193460006F,1.91469979F}},
    {822,{35.0954742F,-2.09999998e-05F,1.92138886F}}
}};
constexpr std::array<SightGeometryTriangle,2> r27_s4_triangles{{
    {874,{776,777,778}},
    {928,{820,821,822}}
}};
constexpr std::array<SightGeometrySurface,1> r27_surfaces{{
    {4,1988,2168,"mc/mtl_double_barrel",r27_s4_ranges,r27_s4_vertices,r27_s4_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r27_front{{
    {4,822,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r27_rear{{
    {4,778,1.0F}
}};
const SightGeometryRecipe r27_recipe=[] {
    SightGeometryRecipe value{"doublebarrel-reviewed-landmarks-v1","viewmodel_usa_double_barrel",r27_names,r27_parents,r27_binds,8,0,5,r27_surfaces,{1,SightGeometryReferenceMethod::weighted_centroid,r27_front},{1,SightGeometryReferenceMethod::weighted_centroid,r27_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,5> r28_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,4> r28_parents{{
    1,
    2,
    3,
    4
}};
constexpr std::array<SightGeometryBone,5> r28_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-1.946244F,-1.78257596F,0.924210012F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.43627906F,0.0128659997F,-0.551743984F},2.0F},
    {{-0.239814445F,-0.177190468F,-0.631153286F,0.71605581F},{-2.95196199F,-0.662280023F,1.12210095F},2.00000453F},
    {{0.0F,0.0F,0.0F,1.0F},{22.1419277F,0.0F,0.984408975F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r28_s1_ranges{{
    {0,2338,0,2249}
}};
constexpr std::array<SightGeometryVertex,72> r28_s1_vertices{{
    {1936,{20.4204235F,-0.0603840016F,1.821298F}},
    {1937,{19.7439327F,-0.0415010005F,2.17477298F}},
    {1938,{19.7828388F,-0.0404560007F,2.19433308F}},
    {1939,{20.4204235F,-0.0404560007F,2.19433308F}},
    {1944,{20.4204235F,0.0414439999F,1.821298F}},
    {1946,{19.7439327F,0.0225610007F,2.17477298F}},
    {1947,{19.7828388F,0.0215160009F,2.19433308F}},
    {1948,{20.4204235F,0.0215160009F,2.19433308F}},
    {2052,{-6.5552392F,0.117397003F,1.68666196F}},
    {2053,{-6.45820904F,0.097916998F,1.706074F}},
    {2054,{-6.5552392F,0.0999199972F,1.66919506F}},
    {2055,{-6.5552392F,0.137236997F,1.71633697F}},
    {2056,{-6.45820904F,0.111791998F,1.72683895F}},
    {2057,{-6.45820904F,0.121247999F,1.749668F}},
    {2058,{-6.5552392F,0.146697998F,1.73916399F}},
    {2059,{-6.5552392F,0.153652996F,1.77411199F}},
    {2060,{-6.45820904F,0.126120001F,1.77416301F}},
    {2061,{-6.45820904F,0.126120001F,1.79887295F}},
    {2062,{-6.5552392F,0.153652996F,1.79892302F}},
    {2063,{-6.5552392F,0.146706998F,1.83382094F}},
    {2064,{-6.45820904F,0.121247999F,1.823367F}},
    {2065,{-6.45820904F,0.111791998F,1.84619606F}},
    {2066,{-6.5552392F,0.137208F,1.85674202F}},
    {2067,{-6.5552392F,0.117426001F,1.88633001F}},
    {2068,{-6.45820904F,0.097916998F,1.86696196F}},
    {2069,{-6.45820904F,0.0804440007F,1.88443494F}},
    {2070,{-6.5552392F,0.0998760015F,1.90386999F}},
    {2071,{-6.5552392F,0.0702650025F,1.92364299F}},
    {2072,{-6.45820904F,0.0596790016F,1.89830899F}},
    {2073,{-6.45820904F,0.0368489996F,1.90776598F}},
    {2074,{-6.5552392F,0.0473389998F,1.93313396F}},
    {2075,{-6.5552392F,0.0124059999F,1.94007802F}},
    {2076,{-6.45820904F,0.0804440007F,1.68860102F}},
    {2077,{-6.45820904F,0.0123549998F,1.91263795F}},
    {2078,{-6.45820904F,-0.0123549998F,1.91263795F}},
    {2079,{-6.5552392F,-0.0124059999F,1.94007802F}},
    {2080,{-6.5552392F,-0.0473389998F,1.93313396F}},
    {2081,{-6.45820904F,-0.0368489996F,1.90776598F}},
    {2082,{-6.45820904F,-0.0596790016F,1.89830899F}},
    {2083,{-6.5552392F,-0.0702650025F,1.92364299F}},
    {2084,{-6.5552392F,-0.0998760015F,1.90386999F}},
    {2085,{-6.45820904F,-0.0804440007F,1.88443494F}},
    {2086,{-6.45820904F,-0.097916998F,1.86696196F}},
    {2087,{-6.5552392F,-0.117426001F,1.88633001F}},
    {2088,{-6.5552392F,-0.137208F,1.85674202F}},
    {2089,{-6.45820904F,-0.111791998F,1.84619606F}},
    {2090,{-6.45820904F,-0.121247999F,1.823367F}},
    {2091,{-6.5552392F,-0.146706998F,1.83382094F}},
    {2092,{-6.5552392F,-0.153652996F,1.79892302F}},
    {2093,{-6.45820904F,-0.126120001F,1.79887295F}},
    {2094,{-6.45820904F,-0.126120001F,1.77416301F}},
    {2095,{-6.5552392F,-0.153652996F,1.77411199F}},
    {2096,{-6.45820904F,0.0596790016F,1.67472601F}},
    {2097,{-6.5552392F,0.0702219978F,1.64936399F}},
    {2098,{-6.5552392F,-0.146697998F,1.73916399F}},
    {2099,{-6.45820904F,-0.121247999F,1.749668F}},
    {2100,{-6.45820904F,-0.111791998F,1.72683895F}},
    {2101,{-6.5552392F,-0.137236997F,1.71633697F}},
    {2102,{-6.5552392F,-0.117397003F,1.68666196F}},
    {2103,{-6.45820904F,-0.097916998F,1.706074F}},
    {2104,{-6.45820904F,-0.0804440007F,1.68860102F}},
    {2105,{-6.5552392F,-0.0999189988F,1.66919506F}},
    {2106,{-6.5552392F,-0.0702219978F,1.64936399F}},
    {2107,{-6.45820904F,-0.0596790016F,1.67472601F}},
    {2108,{-6.45820904F,-0.0368489996F,1.66526997F}},
    {2109,{-6.5552392F,-0.047389999F,1.63991201F}},
    {2110,{-6.5552392F,-0.0123549998F,1.63294697F}},
    {2111,{-6.45820904F,-0.0123549998F,1.66039801F}},
    {2112,{-6.45820904F,0.0123549998F,1.66039801F}},
    {2113,{-6.5552392F,0.012356F,1.63294697F}},
    {2114,{-6.5552392F,0.047389999F,1.63991201F}},
    {2115,{-6.45820904F,0.0368489996F,1.66526997F}}
}};
constexpr std::array<SightGeometryTriangle,68> r28_s1_triangles{{
    {1836,{1937,1938,1936}},
    {1837,{1936,1938,1939}},
    {1843,{1946,1944,1947}},
    {1844,{1944,1948,1947}},
    {1945,{2052,2053,2054}},
    {1946,{2053,2052,2055}},
    {1947,{2056,2053,2055}},
    {1948,{2057,2056,2055}},
    {1949,{2058,2057,2055}},
    {1950,{2057,2058,2059}},
    {1951,{2060,2057,2059}},
    {1952,{2061,2060,2059}},
    {1953,{2062,2061,2059}},
    {1954,{2061,2062,2063}},
    {1955,{2064,2061,2063}},
    {1956,{2065,2064,2063}},
    {1957,{2066,2065,2063}},
    {1958,{2065,2066,2067}},
    {1959,{2068,2065,2067}},
    {1960,{2069,2068,2067}},
    {1961,{2070,2069,2067}},
    {1962,{2069,2070,2071}},
    {1963,{2072,2069,2071}},
    {1964,{2073,2072,2071}},
    {1965,{2074,2073,2071}},
    {1966,{2073,2074,2075}},
    {1967,{2053,2076,2054}},
    {1968,{2077,2073,2075}},
    {1969,{2078,2077,2075}},
    {1970,{2079,2078,2075}},
    {1971,{2078,2079,2080}},
    {1972,{2081,2078,2080}},
    {1973,{2082,2081,2080}},
    {1974,{2083,2082,2080}},
    {1975,{2082,2083,2084}},
    {1976,{2085,2082,2084}},
    {1977,{2086,2085,2084}},
    {1978,{2087,2086,2084}},
    {1979,{2086,2087,2088}},
    {1980,{2089,2086,2088}},
    {1981,{2090,2089,2088}},
    {1982,{2091,2090,2088}},
    {1983,{2090,2091,2092}},
    {1984,{2093,2090,2092}},
    {1985,{2094,2093,2092}},
    {1986,{2095,2094,2092}},
    {1987,{2076,2096,2054}},
    {1988,{2096,2097,2054}},
    {1989,{2094,2095,2098}},
    {1990,{2099,2094,2098}},
    {1991,{2100,2099,2098}},
    {1992,{2101,2100,2098}},
    {1993,{2100,2101,2102}},
    {1994,{2103,2100,2102}},
    {1995,{2104,2103,2102}},
    {1996,{2105,2104,2102}},
    {1997,{2104,2105,2106}},
    {1998,{2107,2104,2106}},
    {1999,{2108,2107,2106}},
    {2000,{2109,2108,2106}},
    {2001,{2108,2109,2110}},
    {2002,{2111,2108,2110}},
    {2003,{2112,2111,2110}},
    {2004,{2113,2112,2110}},
    {2005,{2112,2113,2114}},
    {2006,{2097,2096,2114}},
    {2007,{2115,2112,2114}},
    {2008,{2096,2115,2114}}
}};
constexpr std::array<SightGeometrySurface,1> r28_surfaces{{
    {1,2338,2250,"mc/mtl_weapon_carbine",r28_s1_ranges,r28_s1_vertices,r28_s1_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r28_front{{
    {1,1938,1.0F},
    {1,1947,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,32> r28_rear{{
    {1,2053,1.0F},
    {1,2056,1.0F},
    {1,2057,1.0F},
    {1,2060,1.0F},
    {1,2061,1.0F},
    {1,2064,1.0F},
    {1,2065,1.0F},
    {1,2068,1.0F},
    {1,2069,1.0F},
    {1,2072,1.0F},
    {1,2073,1.0F},
    {1,2076,1.0F},
    {1,2077,1.0F},
    {1,2078,1.0F},
    {1,2081,1.0F},
    {1,2082,1.0F},
    {1,2085,1.0F},
    {1,2086,1.0F},
    {1,2089,1.0F},
    {1,2090,1.0F},
    {1,2093,1.0F},
    {1,2094,1.0F},
    {1,2096,1.0F},
    {1,2099,1.0F},
    {1,2100,1.0F},
    {1,2103,1.0F},
    {1,2104,1.0F},
    {1,2107,1.0F},
    {1,2108,1.0F},
    {1,2111,1.0F},
    {1,2112,1.0F},
    {1,2115,1.0F}
}};
const SightGeometryRecipe r28_recipe=[] {
    SightGeometryRecipe value{"m1carbine-aperture-geometry-bank-v1","viewmodel_usa_m1carbine_rifle",r28_names,r28_parents,r28_binds,4,0,4,r28_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r28_front},{0,SightGeometryReferenceMethod::aperture_circle_yz,r28_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,5> r29_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,4> r29_parents{{
    1,
    2,
    3,
    4
}};
constexpr std::array<SightGeometryBone,5> r29_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-1.946244F,-1.78257596F,0.924210012F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.43627906F,0.0128659997F,-0.551743984F},2.0F},
    {{-0.239814445F,-0.177190468F,-0.631153286F,0.71605581F},{-2.95196199F,-0.662280023F,1.12210095F},2.00000453F},
    {{0.0F,0.0F,0.0F,1.0F},{22.1419277F,0.0F,0.984408975F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r29_s1_ranges{{
    {0,2338,0,2249}
}};
constexpr std::array<SightGeometryVertex,72> r29_s1_vertices{{
    {1936,{20.4204235F,-0.0603840016F,1.821298F}},
    {1937,{19.7439327F,-0.0415010005F,2.17477298F}},
    {1938,{19.7828388F,-0.0404560007F,2.19433308F}},
    {1939,{20.4204235F,-0.0404560007F,2.19433308F}},
    {1944,{20.4204235F,0.0414439999F,1.821298F}},
    {1946,{19.7439327F,0.0225610007F,2.17477298F}},
    {1947,{19.7828388F,0.0215160009F,2.19433308F}},
    {1948,{20.4204235F,0.0215160009F,2.19433308F}},
    {2052,{-6.5552392F,0.117397003F,1.68666196F}},
    {2053,{-6.45820904F,0.097916998F,1.706074F}},
    {2054,{-6.5552392F,0.0999199972F,1.66919506F}},
    {2055,{-6.5552392F,0.137236997F,1.71633697F}},
    {2056,{-6.45820904F,0.111791998F,1.72683895F}},
    {2057,{-6.45820904F,0.121247999F,1.749668F}},
    {2058,{-6.5552392F,0.146697998F,1.73916399F}},
    {2059,{-6.5552392F,0.153652996F,1.77411199F}},
    {2060,{-6.45820904F,0.126120001F,1.77416301F}},
    {2061,{-6.45820904F,0.126120001F,1.79887295F}},
    {2062,{-6.5552392F,0.153652996F,1.79892302F}},
    {2063,{-6.5552392F,0.146706998F,1.83382094F}},
    {2064,{-6.45820904F,0.121247999F,1.823367F}},
    {2065,{-6.45820904F,0.111791998F,1.84619606F}},
    {2066,{-6.5552392F,0.137208F,1.85674202F}},
    {2067,{-6.5552392F,0.117426001F,1.88633001F}},
    {2068,{-6.45820904F,0.097916998F,1.86696196F}},
    {2069,{-6.45820904F,0.0804440007F,1.88443494F}},
    {2070,{-6.5552392F,0.0998760015F,1.90386999F}},
    {2071,{-6.5552392F,0.0702650025F,1.92364299F}},
    {2072,{-6.45820904F,0.0596790016F,1.89830899F}},
    {2073,{-6.45820904F,0.0368489996F,1.90776598F}},
    {2074,{-6.5552392F,0.0473389998F,1.93313396F}},
    {2075,{-6.5552392F,0.0124059999F,1.94007802F}},
    {2076,{-6.45820904F,0.0804440007F,1.68860102F}},
    {2077,{-6.45820904F,0.0123549998F,1.91263795F}},
    {2078,{-6.45820904F,-0.0123549998F,1.91263795F}},
    {2079,{-6.5552392F,-0.0124059999F,1.94007802F}},
    {2080,{-6.5552392F,-0.0473389998F,1.93313396F}},
    {2081,{-6.45820904F,-0.0368489996F,1.90776598F}},
    {2082,{-6.45820904F,-0.0596790016F,1.89830899F}},
    {2083,{-6.5552392F,-0.0702650025F,1.92364299F}},
    {2084,{-6.5552392F,-0.0998760015F,1.90386999F}},
    {2085,{-6.45820904F,-0.0804440007F,1.88443494F}},
    {2086,{-6.45820904F,-0.097916998F,1.86696196F}},
    {2087,{-6.5552392F,-0.117426001F,1.88633001F}},
    {2088,{-6.5552392F,-0.137208F,1.85674202F}},
    {2089,{-6.45820904F,-0.111791998F,1.84619606F}},
    {2090,{-6.45820904F,-0.121247999F,1.823367F}},
    {2091,{-6.5552392F,-0.146706998F,1.83382094F}},
    {2092,{-6.5552392F,-0.153652996F,1.79892302F}},
    {2093,{-6.45820904F,-0.126120001F,1.79887295F}},
    {2094,{-6.45820904F,-0.126120001F,1.77416301F}},
    {2095,{-6.5552392F,-0.153652996F,1.77411199F}},
    {2096,{-6.45820904F,0.0596790016F,1.67472601F}},
    {2097,{-6.5552392F,0.0702219978F,1.64936399F}},
    {2098,{-6.5552392F,-0.146697998F,1.73916399F}},
    {2099,{-6.45820904F,-0.121247999F,1.749668F}},
    {2100,{-6.45820904F,-0.111791998F,1.72683895F}},
    {2101,{-6.5552392F,-0.137236997F,1.71633697F}},
    {2102,{-6.5552392F,-0.117397003F,1.68666196F}},
    {2103,{-6.45820904F,-0.097916998F,1.706074F}},
    {2104,{-6.45820904F,-0.0804440007F,1.68860102F}},
    {2105,{-6.5552392F,-0.0999189988F,1.66919506F}},
    {2106,{-6.5552392F,-0.0702219978F,1.64936399F}},
    {2107,{-6.45820904F,-0.0596790016F,1.67472601F}},
    {2108,{-6.45820904F,-0.0368489996F,1.66526997F}},
    {2109,{-6.5552392F,-0.047389999F,1.63991201F}},
    {2110,{-6.5552392F,-0.0123549998F,1.63294697F}},
    {2111,{-6.45820904F,-0.0123549998F,1.66039801F}},
    {2112,{-6.45820904F,0.0123549998F,1.66039801F}},
    {2113,{-6.5552392F,0.012356F,1.63294697F}},
    {2114,{-6.5552392F,0.047389999F,1.63991201F}},
    {2115,{-6.45820904F,0.0368489996F,1.66526997F}}
}};
constexpr std::array<SightGeometryTriangle,68> r29_s1_triangles{{
    {1836,{1937,1938,1936}},
    {1837,{1936,1938,1939}},
    {1843,{1946,1944,1947}},
    {1844,{1944,1948,1947}},
    {1945,{2052,2053,2054}},
    {1946,{2053,2052,2055}},
    {1947,{2056,2053,2055}},
    {1948,{2057,2056,2055}},
    {1949,{2058,2057,2055}},
    {1950,{2057,2058,2059}},
    {1951,{2060,2057,2059}},
    {1952,{2061,2060,2059}},
    {1953,{2062,2061,2059}},
    {1954,{2061,2062,2063}},
    {1955,{2064,2061,2063}},
    {1956,{2065,2064,2063}},
    {1957,{2066,2065,2063}},
    {1958,{2065,2066,2067}},
    {1959,{2068,2065,2067}},
    {1960,{2069,2068,2067}},
    {1961,{2070,2069,2067}},
    {1962,{2069,2070,2071}},
    {1963,{2072,2069,2071}},
    {1964,{2073,2072,2071}},
    {1965,{2074,2073,2071}},
    {1966,{2073,2074,2075}},
    {1967,{2053,2076,2054}},
    {1968,{2077,2073,2075}},
    {1969,{2078,2077,2075}},
    {1970,{2079,2078,2075}},
    {1971,{2078,2079,2080}},
    {1972,{2081,2078,2080}},
    {1973,{2082,2081,2080}},
    {1974,{2083,2082,2080}},
    {1975,{2082,2083,2084}},
    {1976,{2085,2082,2084}},
    {1977,{2086,2085,2084}},
    {1978,{2087,2086,2084}},
    {1979,{2086,2087,2088}},
    {1980,{2089,2086,2088}},
    {1981,{2090,2089,2088}},
    {1982,{2091,2090,2088}},
    {1983,{2090,2091,2092}},
    {1984,{2093,2090,2092}},
    {1985,{2094,2093,2092}},
    {1986,{2095,2094,2092}},
    {1987,{2076,2096,2054}},
    {1988,{2096,2097,2054}},
    {1989,{2094,2095,2098}},
    {1990,{2099,2094,2098}},
    {1991,{2100,2099,2098}},
    {1992,{2101,2100,2098}},
    {1993,{2100,2101,2102}},
    {1994,{2103,2100,2102}},
    {1995,{2104,2103,2102}},
    {1996,{2105,2104,2102}},
    {1997,{2104,2105,2106}},
    {1998,{2107,2104,2106}},
    {1999,{2108,2107,2106}},
    {2000,{2109,2108,2106}},
    {2001,{2108,2109,2110}},
    {2002,{2111,2108,2110}},
    {2003,{2112,2111,2110}},
    {2004,{2113,2112,2110}},
    {2005,{2112,2113,2114}},
    {2006,{2097,2096,2114}},
    {2007,{2115,2112,2114}},
    {2008,{2096,2115,2114}}
}};
constexpr std::array<SightGeometrySurface,1> r29_surfaces{{
    {1,2338,2250,"mc/mtl_weapon_carbine_wet",r29_s1_ranges,r29_s1_vertices,r29_s1_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r29_front{{
    {1,1938,1.0F},
    {1,1947,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,32> r29_rear{{
    {1,2053,1.0F},
    {1,2056,1.0F},
    {1,2057,1.0F},
    {1,2060,1.0F},
    {1,2061,1.0F},
    {1,2064,1.0F},
    {1,2065,1.0F},
    {1,2068,1.0F},
    {1,2069,1.0F},
    {1,2072,1.0F},
    {1,2073,1.0F},
    {1,2076,1.0F},
    {1,2077,1.0F},
    {1,2078,1.0F},
    {1,2081,1.0F},
    {1,2082,1.0F},
    {1,2085,1.0F},
    {1,2086,1.0F},
    {1,2089,1.0F},
    {1,2090,1.0F},
    {1,2093,1.0F},
    {1,2094,1.0F},
    {1,2096,1.0F},
    {1,2099,1.0F},
    {1,2100,1.0F},
    {1,2103,1.0F},
    {1,2104,1.0F},
    {1,2107,1.0F},
    {1,2108,1.0F},
    {1,2111,1.0F},
    {1,2112,1.0F},
    {1,2115,1.0F}
}};
const SightGeometryRecipe r29_recipe=[] {
    SightGeometryRecipe value{"transferred-m1carbine_wet-mesh-v1","viewmodel_usa_m1carbine_rifle_wet",r29_names,r29_parents,r29_binds,4,0,4,r29_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r29_front},{0,SightGeometryReferenceMethod::aperture_circle_yz,r29_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,7> r30_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "j_stripper",
    "tag_brass",
    "tag_flash",
    "tag_iron_sights"
}};
constexpr std::array<std::uint8_t,6> r30_parents{{
    1,
    2,
    3,
    4,
    5,
    6
}};
constexpr std::array<SightGeometryBone,7> r30_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-0.273142993F,-1.81032395F,1.10182405F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.88877106F,-0.00603500009F,-7.28390694F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.88877106F,-0.00603500009F,-7.28390694F},2.0F},
    {{-0.173802912F,-0.202703938F,-0.685995042F,0.67683953F},{-1.79524302F,-0.608017027F,1.63733804F},2.00000548F},
    {{0.0F,0.0F,0.0F,1.0F},{27.8524189F,0.0F,1.52065206F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{5.62187719F,-0.409855992F,0.844623983F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,3> r30_s0_ranges{{
    {0,80,0,56},
    {3,65,56,33},
    {6,86,89,58}
}};
constexpr std::array<SightGeometryVertex,4> r30_s0_vertices{{
    {145,{25.7279968F,-0.031578999F,2.64245701F}},
    {146,{25.7279968F,0.031578999F,2.64245701F}},
    {147,{26.221981F,-0.031578999F,2.73525596F}},
    {148,{26.221981F,0.031578999F,2.73525596F}}
}};
constexpr std::array<SightGeometryTriangle,2> r30_s0_triangles{{
    {89,{145,146,147}},
    {90,{146,148,147}}
}};
constexpr std::array<SightGeometryRigidRange,1> r30_s3_ranges{{
    {6,249,0,166}
}};
constexpr std::array<SightGeometryVertex,14> r30_s3_vertices{{
    {9,{-5.7963748F,-0.0512689985F,2.35793805F}},
    {10,{-5.79649687F,0.0201769993F,2.36598706F}},
    {11,{-5.80678177F,-0.0533340015F,2.36368299F}},
    {27,{-5.79383421F,-0.158447996F,2.18738198F}},
    {28,{-5.79489803F,-0.150398001F,2.25882006F}},
    {29,{-5.804142F,-0.164660007F,2.18652892F}},
    {57,{-5.79174709F,0.0382639989F,2.04698801F}},
    {58,{-5.80191278F,0.00334899989F,2.03661108F}},
    {59,{-5.7916851F,0.00208800007F,2.04291201F}},
    {60,{-5.80191422F,0.000827000011F,2.03661108F}},
    {61,{-5.79174519F,-0.0340880007F,2.04698801F}},
    {64,{-5.79437923F,0.162624002F,2.2235539F}},
    {65,{-5.79331398F,0.154574007F,2.15211701F}},
    {66,{-5.804708F,0.168834999F,2.22410011F}}
}};
constexpr std::array<SightGeometryTriangle,6> r30_s3_triangles{{
    {3,{9,10,11}},
    {10,{27,28,29}},
    {24,{57,59,58}},
    {25,{59,60,58}},
    {26,{59,61,60}},
    {29,{64,65,66}}
}};
constexpr std::array<SightGeometrySurface,2> r30_surfaces{{
    {0,231,148,"mc/mtl_weapon_m1garand",r30_s0_ranges,r30_s0_vertices,r30_s0_triangles},
    {3,249,166,"mc/mtl_weapon_m1garand",r30_s3_ranges,r30_s3_vertices,r30_s3_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,4> r30_front{{
    {0,145,1.0F},
    {0,146,1.0F},
    {0,147,1.0F},
    {0,148,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,4> r30_rear{{
    {3,27,1.0F},
    {3,64,1.0F},
    {3,10,1.0F},
    {3,59,1.0F}
}};
const SightGeometryRecipe r30_recipe=[] {
    SightGeometryRecipe value{"transferred-m1garand-mesh-v1","viewmodel_usa_m1garand_rifle",r30_names,r30_parents,r30_binds,6,0,5,r30_surfaces,{6,SightGeometryReferenceMethod::weighted_centroid,r30_front},{6,SightGeometryReferenceMethod::aperture_circle_yz,r30_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,8> r31_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "j_stripper",
    "tag_bayonet",
    "tag_brass",
    "tag_flash",
    "tag_iron_sights"
}};
constexpr std::array<std::uint8_t,7> r31_parents{{
    1,
    2,
    3,
    4,
    5,
    6,
    7
}};
constexpr std::array<SightGeometryBone,8> r31_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-0.273142993F,-1.81032395F,1.10182405F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.88877106F,-0.00603500009F,-7.28390694F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.88877106F,-0.00603500009F,-7.28390694F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{23.6130505F,0.0F,0.302579999F},2.00003505F},
    {{-0.173802912F,-0.202703938F,-0.685995042F,0.67683953F},{-1.79524302F,-0.608017027F,1.63733804F},2.00000548F},
    {{0.0F,0.0F,0.0F,1.0F},{27.8524189F,0.0F,1.52065206F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{5.62187719F,-0.409855992F,0.844623983F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,3> r31_s1_ranges{{
    {0,80,0,56},
    {3,65,56,33},
    {7,86,89,58}
}};
constexpr std::array<SightGeometryVertex,4> r31_s1_vertices{{
    {145,{25.7279968F,-0.031578999F,2.64245701F}},
    {146,{25.7279968F,0.031578999F,2.64245701F}},
    {147,{26.221981F,-0.031578999F,2.73525596F}},
    {148,{26.221981F,0.031578999F,2.73525596F}}
}};
constexpr std::array<SightGeometryTriangle,2> r31_s1_triangles{{
    {89,{145,146,147}},
    {90,{146,148,147}}
}};
constexpr std::array<SightGeometryRigidRange,1> r31_s4_ranges{{
    {7,249,0,166}
}};
constexpr std::array<SightGeometryVertex,14> r31_s4_vertices{{
    {9,{-5.7963748F,-0.0512689985F,2.35793805F}},
    {10,{-5.79649687F,0.0201769993F,2.36598706F}},
    {11,{-5.80678177F,-0.0533340015F,2.36368299F}},
    {27,{-5.79383421F,-0.158447996F,2.18738198F}},
    {28,{-5.79489803F,-0.150398001F,2.25882006F}},
    {29,{-5.804142F,-0.164660007F,2.18652892F}},
    {57,{-5.79174709F,0.0382639989F,2.04698801F}},
    {58,{-5.80191278F,0.00334899989F,2.03661108F}},
    {59,{-5.7916851F,0.00208800007F,2.04291201F}},
    {60,{-5.80191422F,0.000827000011F,2.03661108F}},
    {61,{-5.79174519F,-0.0340880007F,2.04698801F}},
    {64,{-5.79437923F,0.162624002F,2.2235539F}},
    {65,{-5.79331398F,0.154574007F,2.15211701F}},
    {66,{-5.804708F,0.168834999F,2.22410011F}}
}};
constexpr std::array<SightGeometryTriangle,6> r31_s4_triangles{{
    {3,{9,10,11}},
    {10,{27,28,29}},
    {24,{57,59,58}},
    {25,{59,60,58}},
    {26,{59,61,60}},
    {29,{64,65,66}}
}};
constexpr std::array<SightGeometrySurface,2> r31_surfaces{{
    {1,231,148,"mc/mtl_weapon_m1garand",r31_s1_ranges,r31_s1_vertices,r31_s1_triangles},
    {4,249,166,"mc/mtl_weapon_m1garand",r31_s4_ranges,r31_s4_vertices,r31_s4_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,4> r31_front{{
    {1,145,1.0F},
    {1,146,1.0F},
    {1,147,1.0F},
    {1,148,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,4> r31_rear{{
    {4,27,1.0F},
    {4,64,1.0F},
    {4,10,1.0F},
    {4,59,1.0F}
}};
const SightGeometryRecipe r31_recipe=[] {
    SightGeometryRecipe value{"transferred-m1garand_bayonet-mesh-v1","viewmodel_usa_m1garand_rifle_bayonet",r31_names,r31_parents,r31_binds,7,0,6,r31_surfaces,{7,SightGeometryReferenceMethod::weighted_centroid,r31_front},{7,SightGeometryReferenceMethod::aperture_circle_yz,r31_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,8> r32_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "j_stripper",
    "tag_brass",
    "tag_flash",
    "tag_grenade_mount",
    "tag_iron_sights"
}};
constexpr std::array<std::uint8_t,7> r32_parents{{
    1,
    2,
    3,
    4,
    5,
    6,
    7
}};
constexpr std::array<SightGeometryBone,8> r32_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-0.273142993F,-1.81032395F,1.10182405F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.88877106F,-0.00603500009F,-7.28390694F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.88877106F,-0.00603500009F,-7.28390694F},2.0F},
    {{-0.173802912F,-0.202703938F,-0.685995042F,0.67683953F},{-1.79524302F,-0.608017027F,1.63733804F},2.00000548F},
    {{0.0F,0.0F,0.0F,1.0F},{27.8524189F,0.0F,1.52065206F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{37.1134415F,0.0F,1.69165003F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{5.62187719F,-0.409855992F,0.844623983F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,3> r32_s0_ranges{{
    {0,80,0,56},
    {3,65,56,33},
    {7,86,89,58}
}};
constexpr std::array<SightGeometryVertex,4> r32_s0_vertices{{
    {145,{25.7279968F,-0.031578999F,2.64245701F}},
    {146,{25.7279968F,0.031578999F,2.64245701F}},
    {147,{26.221981F,-0.031578999F,2.73525596F}},
    {148,{26.221981F,0.031578999F,2.73525596F}}
}};
constexpr std::array<SightGeometryTriangle,2> r32_s0_triangles{{
    {89,{145,146,147}},
    {90,{146,148,147}}
}};
constexpr std::array<SightGeometryRigidRange,1> r32_s3_ranges{{
    {7,249,0,166}
}};
constexpr std::array<SightGeometryVertex,14> r32_s3_vertices{{
    {9,{-5.7963748F,-0.0512689985F,2.35793805F}},
    {10,{-5.79649687F,0.0201769993F,2.36598706F}},
    {11,{-5.80678177F,-0.0533340015F,2.36368299F}},
    {27,{-5.79383421F,-0.158447996F,2.18738198F}},
    {28,{-5.79489803F,-0.150398001F,2.25882006F}},
    {29,{-5.804142F,-0.164660007F,2.18652892F}},
    {57,{-5.79174709F,0.0382639989F,2.04698801F}},
    {58,{-5.80191278F,0.00334899989F,2.03661108F}},
    {59,{-5.7916851F,0.00208800007F,2.04291201F}},
    {60,{-5.80191422F,0.000827000011F,2.03661108F}},
    {61,{-5.79174519F,-0.0340880007F,2.04698801F}},
    {64,{-5.79437923F,0.162624002F,2.2235539F}},
    {65,{-5.79331398F,0.154574007F,2.15211701F}},
    {66,{-5.804708F,0.168834999F,2.22410011F}}
}};
constexpr std::array<SightGeometryTriangle,6> r32_s3_triangles{{
    {3,{9,10,11}},
    {10,{27,28,29}},
    {24,{57,59,58}},
    {25,{59,60,58}},
    {26,{59,61,60}},
    {29,{64,65,66}}
}};
constexpr std::array<SightGeometrySurface,2> r32_surfaces{{
    {0,231,148,"mc/mtl_weapon_m1garand",r32_s0_ranges,r32_s0_vertices,r32_s0_triangles},
    {3,249,166,"mc/mtl_weapon_m1garand",r32_s3_ranges,r32_s3_vertices,r32_s3_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,4> r32_front{{
    {0,145,1.0F},
    {0,146,1.0F},
    {0,147,1.0F},
    {0,148,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,4> r32_rear{{
    {3,27,1.0F},
    {3,64,1.0F},
    {3,10,1.0F},
    {3,59,1.0F}
}};
const SightGeometryRecipe r32_recipe=[] {
    SightGeometryRecipe value{"transferred-m1garand_gl-mesh-v1","viewmodel_usa_m1garand_rifle_grenade_mount",r32_names,r32_parents,r32_binds,7,0,5,r32_surfaces,{7,SightGeometryReferenceMethod::weighted_centroid,r32_front},{7,SightGeometryReferenceMethod::aperture_circle_yz,r32_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,7> r33_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "j_stripper",
    "tag_brass",
    "tag_flash",
    "tag_iron_sights"
}};
constexpr std::array<std::uint8_t,6> r33_parents{{
    1,
    2,
    3,
    4,
    5,
    6
}};
constexpr std::array<SightGeometryBone,7> r33_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-0.273142993F,-1.81032395F,1.10182405F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.88877106F,-0.00603500009F,-7.28390694F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.88877106F,-0.00603500009F,-7.28390694F},2.0F},
    {{-0.173802912F,-0.202703938F,-0.685995042F,0.67683953F},{-1.79524302F,-0.608017027F,1.63733804F},2.00000548F},
    {{0.0F,0.0F,0.0F,1.0F},{27.8524189F,0.0F,1.52065206F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{5.62187719F,-0.409855992F,0.844623983F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,3> r33_s0_ranges{{
    {0,80,0,56},
    {3,65,56,33},
    {6,86,89,58}
}};
constexpr std::array<SightGeometryVertex,4> r33_s0_vertices{{
    {145,{25.7279968F,-0.031578999F,2.64245701F}},
    {146,{25.7279968F,0.031578999F,2.64245701F}},
    {147,{26.221981F,-0.031578999F,2.73525596F}},
    {148,{26.221981F,0.031578999F,2.73525596F}}
}};
constexpr std::array<SightGeometryTriangle,2> r33_s0_triangles{{
    {89,{145,146,147}},
    {90,{146,148,147}}
}};
constexpr std::array<SightGeometryRigidRange,1> r33_s3_ranges{{
    {6,249,0,166}
}};
constexpr std::array<SightGeometryVertex,14> r33_s3_vertices{{
    {9,{-5.7963748F,-0.0512689985F,2.35793805F}},
    {10,{-5.79649687F,0.0201769993F,2.36598706F}},
    {11,{-5.80678177F,-0.0533340015F,2.36368299F}},
    {27,{-5.79383421F,-0.158447996F,2.18738198F}},
    {28,{-5.79489803F,-0.150398001F,2.25882006F}},
    {29,{-5.804142F,-0.164660007F,2.18652892F}},
    {57,{-5.79174709F,0.0382639989F,2.04698801F}},
    {58,{-5.80191278F,0.00334899989F,2.03661108F}},
    {59,{-5.7916851F,0.00208800007F,2.04291201F}},
    {60,{-5.80191422F,0.000827000011F,2.03661108F}},
    {61,{-5.79174519F,-0.0340880007F,2.04698801F}},
    {64,{-5.79437923F,0.162624002F,2.2235539F}},
    {65,{-5.79331398F,0.154574007F,2.15211701F}},
    {66,{-5.804708F,0.168834999F,2.22410011F}}
}};
constexpr std::array<SightGeometryTriangle,6> r33_s3_triangles{{
    {3,{9,10,11}},
    {10,{27,28,29}},
    {24,{57,59,58}},
    {25,{59,60,58}},
    {26,{59,61,60}},
    {29,{64,65,66}}
}};
constexpr std::array<SightGeometrySurface,2> r33_surfaces{{
    {0,231,148,"mc/mtl_weapon_m1garand_wet",r33_s0_ranges,r33_s0_vertices,r33_s0_triangles},
    {3,249,166,"mc/mtl_weapon_m1garand_wet",r33_s3_ranges,r33_s3_vertices,r33_s3_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,4> r33_front{{
    {0,145,1.0F},
    {0,146,1.0F},
    {0,147,1.0F},
    {0,148,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,4> r33_rear{{
    {3,27,1.0F},
    {3,64,1.0F},
    {3,10,1.0F},
    {3,59,1.0F}
}};
const SightGeometryRecipe r33_recipe=[] {
    SightGeometryRecipe value{"transferred-m1garand_wet-mesh-v1","viewmodel_usa_m1garand_rifle_wet",r33_names,r33_parents,r33_binds,6,0,5,r33_surfaces,{6,SightGeometryReferenceMethod::weighted_centroid,r33_front},{6,SightGeometryReferenceMethod::aperture_circle_yz,r33_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,7> r34_names{{
    "j_gun",
    "j_bolt",
    "j_round2",
    "j_stripper",
    "tag_brass",
    "tag_flash",
    "tag_no_scope"
}};
constexpr std::array<std::uint8_t,6> r34_parents{{
    1,
    2,
    3,
    4,
    5,
    6
}};
constexpr std::array<SightGeometryBone,7> r34_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-7.56023598F,0.00035300001F,1.47832084F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.58611202F,27.7900314F,2.38816714F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.49622393F,27.8010502F,2.27072501F},2.0F},
    {{-0.218970299F,-0.204596087F,-0.697103798F,0.651326001F},{-1.79524302F,-0.446377009F,1.60976672F},2.00002646F},
    {{0.0F,0.0F,0.0F,1.0F},{28.3464851F,0.0190360006F,1.28886294F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{3.29945111F,0.558116972F,1.9363749F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r34_s2_ranges{{
    {6,899,0,699}
}};
constexpr std::array<SightGeometryVertex,11> r34_s2_vertices{{
    {24,{2.74934912F,-0.0806270018F,2.41275096F}},
    {26,{2.74934912F,0.0806270018F,2.41275096F}},
    {27,{2.74934912F,-0.0806270018F,2.54280496F}},
    {28,{2.74934912F,-0.349263012F,2.34382987F}},
    {30,{2.74934912F,0.338367999F,2.54280496F}},
    {31,{2.74934912F,0.0806270018F,2.54280496F}},
    {32,{2.74934912F,-0.349263012F,2.54280496F}},
    {621,{27.3522892F,-0.0207289997F,2.42072082F}},
    {622,{27.3522892F,0.0207289997F,2.42072082F}},
    {623,{27.4478874F,0.0207289997F,2.40374589F}},
    {624,{27.4478874F,-0.0207289997F,2.40374589F}}
}};
constexpr std::array<SightGeometryTriangle,5> r34_s2_triangles{{
    {9,{27,28,24}},
    {12,{26,30,31}},
    {13,{32,28,27}},
    {444,{621,622,623}},
    {445,{624,621,623}}
}};
constexpr std::array<SightGeometrySurface,1> r34_surfaces{{
    {2,899,700,"mc/mtl_weapon_springfield_noscope",r34_s2_ranges,r34_s2_vertices,r34_s2_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r34_front{{
    {2,621,0.5F},
    {2,622,0.5F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r34_rear{{
    {2,27,0.5F},
    {2,31,0.5F}
}};
const SightGeometryRecipe r34_recipe=[] {
    SightGeometryRecipe value{"springfield-reviewed-landmarks-v1","viewmodel_usa_springfield_rifle",r34_names,r34_parents,r34_binds,8,0,5,r34_surfaces,{6,SightGeometryReferenceMethod::weighted_centroid,r34_front},{6,SightGeometryReferenceMethod::weighted_centroid,r34_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,5> r35_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,4> r35_parents{{
    1,
    2,
    3,
    4
}};
constexpr std::array<SightGeometryBone,5> r35_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{1.30412006F,-0.912230015F,2.11571288F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{3.46322989F,0.126403004F,-0.144166946F},2.0F},
    {{-0.214300975F,-0.200140387F,-0.698690772F,0.652577281F},{3.81253481F,-0.904689014F,1.83832395F},1.99998593F},
    {{0.0F,0.0F,0.0F,1.0F},{14.3775959F,0.0504349992F,1.82262993F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r35_s3_ranges{{
    {0,4631,0,3801}
}};
constexpr std::array<SightGeometryVertex,44> r35_s3_vertices{{
    {24,{14.9879951F,0.0622380003F,2.94018793F}},
    {25,{14.938695F,0.119801998F,2.95097685F}},
    {26,{14.9876614F,0.177893996F,2.94080687F}},
    {3900,{-5.90241337F,-0.0302839987F,3.13026905F}},
    {3901,{-5.9021492F,-0.0332869999F,3.14704394F}},
    {3902,{-5.9058485F,-0.0367239974F,3.14573503F}},
    {3903,{-5.90598202F,-0.0336310007F,3.12934494F}},
    {3904,{-5.90627384F,-0.0236429982F,3.11220789F}},
    {3905,{-5.90269756F,-0.0207970031F,3.11399198F}},
    {3906,{-5.90653324F,-0.00815000013F,3.0986371F}},
    {3907,{-5.90294075F,-0.00608300045F,3.10110283F}},
    {3908,{-5.90673637F,0.0113309994F,3.08996105F}},
    {3909,{-5.90313148F,0.0124160014F,3.09286404F}},
    {3910,{-5.90685177F,0.032416001F,3.08709383F}},
    {3911,{-5.90324116F,0.0324200019F,3.09014416F}},
    {3912,{-5.90327454F,0.0533360019F,3.09302187F}},
    {3913,{-5.9068861F,0.0544140004F,3.09012198F}},
    {3914,{-5.90321445F,0.0717540011F,3.10140395F}},
    {3915,{-5.90682316F,0.0738200024F,3.09895396F}},
    {3916,{-5.9030714F,0.0863410011F,3.11440611F}},
    {3917,{-5.90667629F,0.0891790017F,3.11264396F}},
    {3918,{-5.90285873F,0.0956690013F,3.1307559F}},
    {3919,{-5.90644741F,0.0989990011F,3.12985706F}},
    {3920,{-5.90635586F,0.101567999F,3.14527202F}},
    {3921,{-5.90264225F,0.0981969982F,3.14576912F}},
    {3922,{-5.90586853F,0.0988430008F,3.16792583F}},
    {3923,{-5.90231133F,0.0954920053F,3.16692686F}},
    {3924,{-5.90200424F,0.0840720013F,3.18478298F}},
    {3925,{-5.90540123F,0.0868040025F,3.18775797F}},
    {3926,{-5.90523911F,0.0725239962F,3.19902515F}},
    {3927,{-5.90171432F,0.0705979988F,3.19640112F}},
    {3928,{-5.90509796F,0.0511259995F,3.20755386F}},
    {3929,{-5.90157986F,0.0501350015F,3.20455885F}},
    {3930,{-5.90149593F,0.0354110003F,3.20662212F}},
    {3931,{-5.90500259F,0.0351839997F,3.20975304F}},
    {3932,{-5.90145588F,0.0113020018F,3.20391512F}},
    {3933,{-5.90496063F,0.0102429986F,3.20691895F}},
    {3934,{-5.90149498F,-0.00125699863F,3.19819903F}},
    {3935,{-5.90500164F,-0.00320800021F,3.20079708F}},
    {3936,{-5.90161514F,-0.0169480033F,3.18640614F}},
    {3937,{-5.90505981F,-0.0194089971F,3.18941092F}},
    {3938,{-5.90540409F,-0.0338180028F,3.16750288F}},
    {3939,{-5.90186596F,-0.0304610021F,3.16644287F}},
    {3940,{-5.9058485F,-0.0367239974F,3.14573503F}}
}};
constexpr std::array<SightGeometryTriangle,41> r35_s3_triangles{{
    {8,{24,25,26}},
    {3089,{3900,3901,3902}},
    {3090,{3903,3900,3902}},
    {3091,{3900,3903,3904}},
    {3092,{3905,3900,3904}},
    {3093,{3905,3904,3906}},
    {3094,{3907,3905,3906}},
    {3095,{3907,3906,3908}},
    {3096,{3909,3907,3908}},
    {3097,{3909,3908,3910}},
    {3098,{3911,3909,3910}},
    {3099,{3912,3911,3910}},
    {3100,{3913,3912,3910}},
    {3101,{3914,3912,3913}},
    {3102,{3915,3914,3913}},
    {3103,{3916,3914,3915}},
    {3104,{3917,3916,3915}},
    {3105,{3918,3916,3917}},
    {3106,{3919,3918,3917}},
    {3107,{3918,3919,3920}},
    {3108,{3921,3918,3920}},
    {3109,{3921,3920,3922}},
    {3110,{3923,3921,3922}},
    {3111,{3924,3923,3922}},
    {3112,{3925,3924,3922}},
    {3113,{3924,3925,3926}},
    {3114,{3927,3924,3926}},
    {3115,{3927,3926,3928}},
    {3116,{3929,3927,3928}},
    {3117,{3930,3929,3928}},
    {3118,{3931,3930,3928}},
    {3119,{3932,3930,3931}},
    {3120,{3933,3932,3931}},
    {3121,{3934,3932,3933}},
    {3122,{3935,3934,3933}},
    {3123,{3936,3934,3935}},
    {3124,{3937,3936,3935}},
    {3125,{3936,3937,3938}},
    {3126,{3901,3939,3938}},
    {3127,{3939,3936,3938}},
    {3128,{3940,3901,3938}}
}};
constexpr std::array<SightGeometrySurface,1> r35_surfaces{{
    {3,4631,3802,"mc/mtl_usa_smg_thompson",r35_s3_ranges,r35_s3_vertices,r35_s3_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r35_front{{
    {3,25,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,20> r35_rear{{
    {3,3900,0.0500000007F},
    {3,3901,0.0500000007F},
    {3,3905,0.0500000007F},
    {3,3907,0.0500000007F},
    {3,3909,0.0500000007F},
    {3,3911,0.0500000007F},
    {3,3912,0.0500000007F},
    {3,3914,0.0500000007F},
    {3,3916,0.0500000007F},
    {3,3918,0.0500000007F},
    {3,3921,0.0500000007F},
    {3,3923,0.0500000007F},
    {3,3924,0.0500000007F},
    {3,3927,0.0500000007F},
    {3,3929,0.0500000007F},
    {3,3930,0.0500000007F},
    {3,3932,0.0500000007F},
    {3,3934,0.0500000007F},
    {3,3936,0.0500000007F},
    {3,3939,0.0500000007F}
}};
const SightGeometryRecipe r35_recipe=[] {
    SightGeometryRecipe value{"thompson-reviewed-landmarks-v1","viewmodel_usa_thompson_smg",r35_names,r35_parents,r35_binds,4,0,4,r35_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r35_front},{0,SightGeometryReferenceMethod::weighted_centroid,r35_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,5> r36_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,4> r36_parents{{
    1,
    2,
    3,
    4
}};
constexpr std::array<SightGeometryBone,5> r36_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{1.30412006F,-0.912230015F,2.11571288F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{3.46322989F,0.126403004F,-0.144166946F},2.0F},
    {{-0.214300975F,-0.200140387F,-0.698690772F,0.652577281F},{3.81253481F,-0.904689014F,1.83832395F},1.99998593F},
    {{0.0F,0.0F,0.0F,1.0F},{14.3775959F,0.0504349992F,1.82262993F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r36_s3_ranges{{
    {0,4631,0,3801}
}};
constexpr std::array<SightGeometryVertex,59> r36_s3_vertices{{
    {24,{14.9879951F,0.0622380003F,2.94018793F}},
    {25,{14.938695F,0.119801998F,2.95097685F}},
    {26,{14.9876614F,0.177893996F,2.94080687F}},
    {3772,{-5.86262417F,-0.00315999985F,3.13725281F}},
    {3773,{-5.86249352F,-0.00402899832F,3.14690495F}},
    {3774,{-5.90241337F,-0.0302839987F,3.13026905F}},
    {3775,{-5.9021492F,-0.0332869999F,3.14704394F}},
    {3776,{-5.86278439F,0.00227700174F,3.12792397F}},
    {3777,{-5.86262417F,-0.00315999985F,3.13725281F}},
    {3778,{-5.90269756F,-0.0207970031F,3.11399198F}},
    {3779,{-5.90241337F,-0.0302839987F,3.13026905F}},
    {3780,{-5.86292553F,0.0106920004F,3.12055302F}},
    {3781,{-5.86278439F,0.00227700174F,3.12792397F}},
    {3782,{-5.90294075F,-0.00608300045F,3.10110283F}},
    {3783,{-5.90269756F,-0.0207970031F,3.11399198F}},
    {3784,{-5.86303425F,0.0212500002F,3.11584997F}},
    {3785,{-5.86292553F,0.0106920004F,3.12055302F}},
    {3786,{-5.90313148F,0.0124160014F,3.09286404F}},
    {3787,{-5.90294075F,-0.00608300045F,3.10110283F}},
    {3788,{-5.86309719F,0.03266F,3.11429882F}},
    {3789,{-5.86303425F,0.0212500002F,3.11584997F}},
    {3790,{-5.90324116F,0.0324200019F,3.09014416F}},
    {3791,{-5.90313148F,0.0124160014F,3.09286404F}},
    {3792,{-5.86276627F,0.0693830028F,3.14619398F}},
    {3793,{-5.86287594F,0.0687820017F,3.13753104F}},
    {3794,{-5.90264225F,0.0981969982F,3.14576912F}},
    {3795,{-5.90285873F,0.0956690013F,3.1307559F}},
    {3796,{-5.90327454F,0.0533360019F,3.09302187F}},
    {3797,{-5.86311531F,0.0445789993F,3.11593914F}},
    {3798,{-5.90324116F,0.0324200019F,3.09014416F}},
    {3800,{-5.90321445F,0.0717540011F,3.10140395F}},
    {3801,{-5.86308193F,0.055094F,3.12072515F}},
    {3802,{-5.90327454F,0.0533360019F,3.09302187F}},
    {3804,{-5.9030714F,0.0863410011F,3.11440611F}},
    {3805,{-5.86300087F,0.0634360015F,3.12816F}},
    {3806,{-5.90321445F,0.0717540011F,3.10140395F}},
    {3812,{-5.86240864F,0.0614940003F,3.167979F}},
    {3813,{-5.86256409F,0.068690002F,3.15829086F}},
    {3814,{-5.90200424F,0.0840720013F,3.18478298F}},
    {3815,{-5.90231133F,0.0954920053F,3.16692686F}},
    {3820,{-5.86209679F,0.0342690013F,3.18100691F}},
    {3821,{-5.86214447F,0.0427719988F,3.17982388F}},
    {3822,{-5.90149593F,0.0354110003F,3.20662212F}},
    {3823,{-5.90157986F,0.0501350015F,3.20455885F}},
    {3824,{-5.86226463F,0.0548049994F,3.17500997F}},
    {3825,{-5.86240864F,0.0614940003F,3.167979F}},
    {3826,{-5.90171432F,0.0705979988F,3.19640112F}},
    {3827,{-5.90200424F,0.0840720013F,3.18478298F}},
    {3832,{-5.90161514F,-0.0169480033F,3.18640614F}},
    {3833,{-5.86219597F,0.00509399921F,3.16888285F}},
    {3834,{-5.90149498F,-0.00125699863F,3.19819903F}},
    {3835,{-5.86209488F,0.0134130009F,3.17618084F}},
    {3836,{-5.90149498F,-0.00125699863F,3.19819903F}},
    {3837,{-5.86209488F,0.0134130009F,3.17618084F}},
    {3838,{-5.90145588F,0.0113020018F,3.20391512F}},
    {3839,{-5.86207104F,0.0206330009F,3.17946696F}},
    {3845,{-5.8623085F,-0.0032619983F,3.15803909F}},
    {3846,{-5.9021492F,-0.0332869999F,3.14704394F}},
    {3847,{-5.90186596F,-0.0304610021F,3.16644287F}}
}};
constexpr std::array<SightGeometryTriangle,27> r36_s3_triangles{{
    {8,{24,25,26}},
    {3025,{3772,3773,3774}},
    {3026,{3773,3775,3774}},
    {3027,{3776,3777,3778}},
    {3028,{3777,3779,3778}},
    {3029,{3780,3781,3782}},
    {3030,{3781,3783,3782}},
    {3031,{3784,3785,3786}},
    {3032,{3785,3787,3786}},
    {3033,{3788,3789,3790}},
    {3034,{3789,3791,3790}},
    {3035,{3792,3793,3794}},
    {3036,{3793,3795,3794}},
    {3037,{3796,3797,3798}},
    {3039,{3800,3801,3802}},
    {3041,{3804,3805,3806}},
    {3045,{3812,3813,3814}},
    {3046,{3813,3815,3814}},
    {3049,{3820,3821,3822}},
    {3050,{3821,3823,3822}},
    {3051,{3824,3825,3826}},
    {3052,{3825,3827,3826}},
    {3055,{3832,3833,3834}},
    {3056,{3833,3835,3834}},
    {3057,{3836,3837,3838}},
    {3058,{3837,3839,3838}},
    {3062,{3845,3847,3846}}
}};
constexpr std::array<SightGeometrySurface,1> r36_surfaces{{
    {3,4631,3802,"mc/mtl_usa_smg_thompson_wet",r36_s3_ranges,r36_s3_vertices,r36_s3_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r36_front{{
    {3,25,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,20> r36_rear{{
    {3,3774,0.0500000007F},
    {3,3775,0.0500000007F},
    {3,3778,0.0500000007F},
    {3,3782,0.0500000007F},
    {3,3786,0.0500000007F},
    {3,3790,0.0500000007F},
    {3,3796,0.0500000007F},
    {3,3800,0.0500000007F},
    {3,3804,0.0500000007F},
    {3,3795,0.0500000007F},
    {3,3794,0.0500000007F},
    {3,3815,0.0500000007F},
    {3,3814,0.0500000007F},
    {3,3826,0.0500000007F},
    {3,3823,0.0500000007F},
    {3,3822,0.0500000007F},
    {3,3838,0.0500000007F},
    {3,3834,0.0500000007F},
    {3,3832,0.0500000007F},
    {3,3847,0.0500000007F}
}};
const SightGeometryRecipe r36_recipe=[] {
    SightGeometryRecipe value{"transferred-thompson_wet-mesh-v1","viewmodel_usa_thompson_smg_wet",r36_names,r36_parents,r36_binds,4,0,4,r36_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r36_front},{0,SightGeometryReferenceMethod::weighted_centroid,r36_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,15> r37_names{{
    "j_gun",
    "j_clip",
    "joint1",
    "joint2",
    "tag_brass",
    "tag_flash",
    "j_bolt",
    "tag_bullets",
    "tag_speedloader",
    "tag_spent0",
    "tag_spent1",
    "tag_spent2",
    "tag_spent3",
    "tag_spent4",
    "tag_spent5"
}};
constexpr std::array<std::uint8_t,14> r37_parents{{
    1,
    2,
    3,
    4,
    5,
    3,
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8
}};
constexpr std::array<SightGeometryBone,15> r37_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.34014499F,0.0716319978F,-1.237288F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-0.622479022F,0.00778699992F,0.598280013F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{3.27272606F,0.156212002F,0.235110998F},2.0F},
    {{-0.245246738F,-0.244636372F,-0.664174318F,0.662465274F},{0.960280001F,-0.341722012F,1.1221F},2.00003862F},
    {{0.0F,0.0F,0.0F,1.0F},{10.394556F,-0.000456000009F,2.20409203F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{1.44532394F,-0.00093999505F,1.59472406F},2.00003505F},
    {{0.0F,0.0F,0.0F,0.999982476F},{1.85321391F,-6.25132895F,1.59472799F},2.0000701F},
    {{0.0F,0.0F,0.0F,0.999982476F},{0.000504067168F,-6.25132895F,1.59471035F},2.0000701F},
    {{0.0F,0.0F,0.0F,0.999982476F},{1.36473954F,-12.5012655F,0.795422256F},2.0000701F},
    {{0.0F,0.0F,0.0F,0.999982476F},{1.36473584F,-11.8240805F,1.18952227F},2.0000701F},
    {{0.0F,0.0F,0.0F,0.999982476F},{1.36472833F,-11.8185301F,1.97772217F},2.0000701F},
    {{0.0F,0.0F,0.0F,0.999982476F},{1.36472476F,-12.4957161F,2.36072016F},2.0000701F},
    {{0.0F,0.0F,0.0F,0.999982476F},{1.36472845F,-13.1784515F,1.97217119F},2.0000701F},
    {{0.0F,0.0F,0.0F,0.999982476F},{1.36473584F,-13.1729021F,1.19507325F},2.0000701F}
}};
constexpr std::array<SightGeometryRigidRange,1> r37_s1_ranges{{
    {0,3220,0,3133}
}};
constexpr std::array<SightGeometryVertex,13> r37_s1_vertices{{
    {2476,{-0.00752600003F,0.096983999F,3.35690188F}},
    {2477,{-0.00625200011F,0.096983999F,3.24118805F}},
    {2478,{-0.00752600003F,0.304167986F,3.35690308F}},
    {2494,{-0.00625200011F,-0.0971089974F,3.24118805F}},
    {2495,{0.0589310005F,-0.0971089974F,3.24238896F}},
    {2496,{-0.00752600003F,-0.0971089974F,3.35690188F}},
    {2497,{0.0576569997F,-0.0971089974F,3.35810304F}},
    {2812,{9.46581173F,0.000633999996F,3.29398394F}},
    {2813,{9.46586227F,-0.0915760025F,3.294626F}},
    {2814,{9.46435738F,-0.0934479982F,3.03991795F}},
    {2815,{9.46421528F,0.0F,3.03524995F}},
    {2816,{9.46435928F,0.0934489965F,3.03993607F}},
    {2817,{9.46599674F,0.0928480029F,3.29440498F}}
}};
constexpr std::array<SightGeometryTriangle,7> r37_s1_triangles{{
    {2398,{2476,2477,2478}},
    {2410,{2494,2495,2496}},
    {2411,{2495,2497,2496}},
    {2662,{2812,2813,2814}},
    {2663,{2815,2812,2814}},
    {2664,{2815,2816,2812}},
    {2665,{2816,2817,2812}}
}};
constexpr std::array<SightGeometrySurface,1> r37_surfaces{{
    {1,3220,3134,"mc/mtl_weapon_sw_357",r37_s1_ranges,r37_s1_vertices,r37_s1_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r37_front{{
    {1,2812,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r37_rear{{
    {1,2476,0.5F},
    {1,2496,0.5F}
}};
const SightGeometryRecipe r37_recipe=[] {
    SightGeometryRecipe value{"transferred-zombie_sw_357_upgraded-mesh-v1","viewmodel_zombie_357magnum_up",r37_names,r37_parents,r37_binds,11,0,5,r37_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r37_front},{0,SightGeometryReferenceMethod::weighted_centroid,r37_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,8> r38_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_bipod",
    "tag_bipod_left",
    "tag_bipod_right",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,7> r38_parents{{
    1,
    2,
    3,
    4,
    5,
    6,
    7
}};
constexpr std::array<SightGeometryBone,8> r38_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-2.24046206F,1.69735706F,0.228652015F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{-0.218616962F,-0.242084995F,-0.715187967F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{23.6939392F,0.0F,1.06821501F},2.0F},
    {{-0.00268562883F,0.48780787F,0.87292093F,0.00674459059F},{23.6656094F,1.22904503F,0.065598011F},1.99999976F},
    {{9.15555283e-05F,-0.485824138F,0.87405014F,0.00335703604F},{23.6656094F,-1.20881796F,0.0767610222F},2.0F},
    {{-0.296182126F,-0.159855947F,-0.775719464F,0.533829749F},{0.0901520252F,-0.933917999F,1.12210107F},2.00001478F},
    {{0.0F,0.0F,0.0F,1.0F},{29.4949856F,0.0F,0.919674993F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r38_s1_ranges{{
    {0,4190,0,4311}
}};
constexpr std::array<SightGeometryVertex,11> r38_s1_vertices{{
    {257,{24.7059593F,-0.0282959994F,2.39989519F}},
    {258,{24.7067986F,0.0522719994F,2.39989519F}},
    {259,{24.7063789F,0.011988F,2.77093124F}},
    {854,{-6.24839973F,-0.156479999F,2.08868217F}},
    {857,{-6.24842644F,-0.0534759983F,2.15872407F}},
    {858,{-6.24842453F,-0.0500359982F,2.15543199F}},
    {859,{-6.24842739F,-0.0402209982F,2.08868217F}},
    {865,{-6.24842739F,0.053679999F,2.08868217F}},
    {866,{-6.24839973F,0.169938996F,2.08868217F}},
    {867,{-6.24842453F,0.0634950027F,2.15543199F}},
    {869,{-6.24842644F,0.0669350028F,2.15872407F}}
}};
constexpr std::array<SightGeometryTriangle,5> r38_s1_triangles{{
    {270,{257,258,259}},
    {901,{858,857,854}},
    {902,{859,858,854}},
    {910,{865,866,867}},
    {912,{867,866,869}}
}};
constexpr std::array<SightGeometrySurface,1> r38_surfaces{{
    {1,4190,4312,"mc/mtl_weapon_mp_bar",r38_s1_ranges,r38_s1_vertices,r38_s1_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r38_front{{
    {1,259,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r38_rear{{
    {1,858,0.5F},
    {1,867,0.5F}
}};
const SightGeometryRecipe r38_recipe=[] {
    SightGeometryRecipe value{"transferred-zombie_bar-mesh-v1","viewmodel_zombie_bar_lmg",r38_names,r38_parents,r38_binds,6,0,7,r38_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r38_front},{0,SightGeometryReferenceMethod::weighted_centroid,r38_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,8> r39_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_bipod",
    "tag_bipod_left",
    "tag_bipod_right",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,7> r39_parents{{
    1,
    2,
    3,
    4,
    5,
    6,
    7
}};
constexpr std::array<SightGeometryBone,8> r39_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-2.24046206F,1.69735706F,0.228652015F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{-0.218616962F,-0.242084995F,-0.715187967F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{23.6939392F,0.0F,1.06821501F},2.0F},
    {{-0.00268562883F,0.48780787F,0.87292093F,0.00674459059F},{23.6656094F,1.22904503F,0.065598011F},1.99999976F},
    {{9.15555283e-05F,-0.485824138F,0.87405014F,0.00335703604F},{23.6656094F,-1.20881796F,0.0767610222F},2.0F},
    {{-0.296182126F,-0.159855947F,-0.775719464F,0.533829749F},{0.0901520252F,-0.933917999F,1.12210107F},2.00001478F},
    {{0.0F,0.0F,0.0F,1.0F},{29.4949856F,0.0F,0.919674993F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r39_s1_ranges{{
    {0,4190,0,4311}
}};
constexpr std::array<SightGeometryVertex,11> r39_s1_vertices{{
    {257,{24.7059593F,-0.0282959994F,2.39989519F}},
    {258,{24.7067986F,0.0522719994F,2.39989519F}},
    {259,{24.7063789F,0.011988F,2.77093124F}},
    {854,{-6.24839973F,-0.156479999F,2.08868217F}},
    {857,{-6.24842644F,-0.0534759983F,2.15872407F}},
    {858,{-6.24842453F,-0.0500359982F,2.15543199F}},
    {859,{-6.24842739F,-0.0402209982F,2.08868217F}},
    {865,{-6.24842739F,0.053679999F,2.08868217F}},
    {866,{-6.24839973F,0.169938996F,2.08868217F}},
    {867,{-6.24842453F,0.0634950027F,2.15543199F}},
    {869,{-6.24842644F,0.0669350028F,2.15872407F}}
}};
constexpr std::array<SightGeometryTriangle,5> r39_s1_triangles{{
    {270,{257,258,259}},
    {901,{858,857,854}},
    {902,{859,858,854}},
    {910,{865,866,867}},
    {912,{867,866,869}}
}};
constexpr std::array<SightGeometrySurface,1> r39_surfaces{{
    {1,4190,4312,"mc/mtl_weapon_bar_gold",r39_s1_ranges,r39_s1_vertices,r39_s1_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r39_front{{
    {1,259,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r39_rear{{
    {1,858,0.5F},
    {1,867,0.5F}
}};
const SightGeometryRecipe r39_recipe=[] {
    SightGeometryRecipe value{"transferred-zombie_bar_upgraded-mesh-v1","viewmodel_zombie_bar_lmg_up",r39_names,r39_parents,r39_binds,8,0,7,r39_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r39_front},{0,SightGeometryReferenceMethod::weighted_centroid,r39_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,7> r40_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "joint1",
    "joint2",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,6> r40_parents{{
    1,
    2,
    3,
    4,
    5,
    6
}};
constexpr std::array<SightGeometryBone,7> r40_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-1.95220101F,0.0677390024F,1.57221496F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.34014499F,0.0716319978F,-1.237288F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.12403011F,0.0518350005F,0.269304991F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{0.898801982F,0.444822997F,0.385870993F},2.0F},
    {{-0.245246738F,-0.244636372F,-0.664174318F,0.662465274F},{0.960280001F,-0.341722012F,1.1221F},2.00003862F},
    {{0.0F,0.0F,0.0F,1.0F},{5.43349218F,0.0769210011F,1.175313F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r40_s2_ranges{{
    {1,1326,0,1050}
}};
constexpr std::array<SightGeometryVertex,15> r40_s2_vertices{{
    {340,{-1.35163701F,0.0289360005F,1.67601299F}},
    {341,{-1.35163701F,0.0255069993F,1.63829505F}},
    {342,{-1.35638702F,0.0348550007F,1.67256498F}},
    {343,{-1.35638702F,0.0311329998F,1.63810098F}},
    {344,{-1.35163701F,-0.0644740015F,1.678702F}},
    {345,{-1.35638702F,-0.0700739995F,1.67684495F}},
    {346,{-1.35163701F,-0.0623680018F,1.64079404F}},
    {347,{-1.35638702F,-0.0672219992F,1.63915396F}},
    {1184,{5.05669785F,-0.0573970005F,1.52226698F}},
    {1185,{5.34775877F,-0.0573959984F,1.68137598F}},
    {1186,{5.53943205F,-0.0573959984F,1.50177598F}},
    {1187,{5.50393677F,-0.0573959984F,1.62471294F}},
    {1189,{5.05669785F,0.0189970005F,1.52226698F}},
    {1190,{5.34775877F,-0.0573959984F,1.68137598F}},
    {1191,{5.34775877F,0.0189970005F,1.68137598F}}
}};
constexpr std::array<SightGeometryTriangle,7> r40_s2_triangles{{
    {229,{340,341,342}},
    {230,{341,343,342}},
    {231,{344,345,346}},
    {232,{345,347,346}},
    {907,{1184,1185,1186}},
    {908,{1185,1187,1186}},
    {910,{1189,1191,1190}}
}};
constexpr std::array<SightGeometrySurface,1> r40_surfaces{{
    {2,1326,1050,"mc/mtl_weapon_colt45",r40_s2_ranges,r40_s2_vertices,r40_s2_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r40_front{{
    {2,1185,1.0F},
    {2,1191,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r40_rear{{
    {2,345,1.0F},
    {2,342,1.0F}
}};
const SightGeometryRecipe r40_recipe=[] {
    SightGeometryRecipe value{"transferred-zombie_colt-mesh-v1","viewmodel_zombie_colt45_pistol",r40_names,r40_parents,r40_binds,5,0,6,r40_surfaces,{1,SightGeometryReferenceMethod::weighted_centroid,r40_front},{1,SightGeometryReferenceMethod::weighted_centroid,r40_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,7> r41_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "joint1",
    "joint2",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,6> r41_parents{{
    1,
    2,
    3,
    4,
    5,
    6
}};
constexpr std::array<SightGeometryBone,7> r41_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-1.95220101F,0.0677390024F,1.57221496F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.34014499F,0.0716319978F,-1.237288F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.12403011F,0.0518350005F,0.269304991F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{0.898801982F,0.444822997F,0.385870993F},2.0F},
    {{-0.245246738F,-0.244636372F,-0.664174318F,0.662465274F},{0.960280001F,-0.341722012F,1.1221F},2.00003862F},
    {{0.0F,0.0F,0.0F,1.0F},{5.43349218F,0.0769210011F,1.175313F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r41_s2_ranges{{
    {1,1326,0,1050}
}};
constexpr std::array<SightGeometryVertex,15> r41_s2_vertices{{
    {340,{-1.35163701F,0.0289360005F,1.67601299F}},
    {341,{-1.35163701F,0.0255069993F,1.63829505F}},
    {342,{-1.35638702F,0.0348550007F,1.67256498F}},
    {343,{-1.35638702F,0.0311329998F,1.63810098F}},
    {344,{-1.35163701F,-0.0644740015F,1.678702F}},
    {345,{-1.35638702F,-0.0700739995F,1.67684495F}},
    {346,{-1.35163701F,-0.0623680018F,1.64079404F}},
    {347,{-1.35638702F,-0.0672219992F,1.63915396F}},
    {1184,{5.05669785F,-0.0573970005F,1.52226698F}},
    {1185,{5.34775877F,-0.0573959984F,1.68137598F}},
    {1186,{5.53943205F,-0.0573959984F,1.50177598F}},
    {1187,{5.50393677F,-0.0573959984F,1.62471294F}},
    {1189,{5.05669785F,0.0189970005F,1.52226698F}},
    {1190,{5.34775877F,-0.0573959984F,1.68137598F}},
    {1191,{5.34775877F,0.0189970005F,1.68137598F}}
}};
constexpr std::array<SightGeometryTriangle,7> r41_s2_triangles{{
    {229,{340,341,342}},
    {230,{341,343,342}},
    {231,{344,345,346}},
    {232,{345,347,346}},
    {907,{1184,1185,1186}},
    {908,{1185,1187,1186}},
    {910,{1189,1191,1190}}
}};
constexpr std::array<SightGeometrySurface,1> r41_surfaces{{
    {2,1326,1050,"mc/mtl_weapon_colt45_zombie_up",r41_s2_ranges,r41_s2_vertices,r41_s2_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r41_front{{
    {2,1185,1.0F},
    {2,1191,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r41_rear{{
    {2,345,1.0F},
    {2,342,1.0F}
}};
const SightGeometryRecipe r41_recipe=[] {
    SightGeometryRecipe value{"transferred-zombie_colt_upgraded-mesh-v1","viewmodel_zombie_colt45_pistol_up",r41_names,r41_parents,r41_binds,6,0,6,r41_surfaces,{1,SightGeometryReferenceMethod::weighted_centroid,r41_front},{1,SightGeometryReferenceMethod::weighted_centroid,r41_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,13> r42_names{{
    "j_gun",
    "tag_barrels",
    "tag_brass",
    "tag_lever",
    "tag_longstock",
    "tag_shortstock",
    "tag_extractor",
    "tag_flash",
    "tag_grip",
    "tag_long",
    "tag_short",
    "tag_shell1",
    "tag_shell2"
}};
constexpr std::array<std::uint8_t,12> r42_parents{{
    1,
    2,
    3,
    4,
    5,
    5,
    6,
    7,
    8,
    9,
    5,
    6
}};
constexpr std::array<SightGeometryBone,13> r42_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{2.62091112F,0.00678599998F,0.0488209724F},2.0F},
    {{-0.244819477F,-0.154881433F,-0.651509166F,0.701162755F},{-0.929933548F,-0.630271971F,1.12210083F},1.99996352F},
    {{0.0F,-0.0954924151F,0.0F,0.995422244F},{-1.52936459F,0.0F,1.7459569F},2.00003147F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.50521374F,1.19563103F,0.948781013F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.50521374F,1.19563103F,0.948781013F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-0.00319385529F,0.00119099999F,1.18299389F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{35.3361778F,0.0F,1.25504494F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{7.70002127F,-0.000765999779F,0.224842787F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-0.0249948502F,-0.000765999779F,1.1923759F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-0.0249948502F,-0.000765999779F,1.1923759F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{1.32945395F,-0.623425007F,1.1901319F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{1.32945395F,0.627842009F,1.18909788F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r42_s2_ranges{{
    {9,1988,0,2168}
}};
constexpr std::array<SightGeometryVertex,6> r42_s2_vertices{{
    {776,{22.6552505F,-0.0194579996F,1.93348098F}},
    {777,{22.6314659F,-0.0265180003F,1.93348098F}},
    {778,{22.6314697F,-2.09999998e-05F,1.94017005F}},
    {820,{35.0954742F,-0.0263650008F,1.91469979F}},
    {821,{35.0716896F,-0.0193460006F,1.91469979F}},
    {822,{35.0954742F,-2.09999998e-05F,1.92138886F}}
}};
constexpr std::array<SightGeometryTriangle,2> r42_s2_triangles{{
    {874,{776,777,778}},
    {928,{820,821,822}}
}};
constexpr std::array<SightGeometrySurface,1> r42_surfaces{{
    {2,1988,2168,"mc/mtl_weapon_mp_double_barrel",r42_s2_ranges,r42_s2_vertices,r42_s2_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r42_front{{
    {2,822,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r42_rear{{
    {2,778,1.0F}
}};
const SightGeometryRecipe r42_recipe=[] {
    SightGeometryRecipe value{"zombie_doublebarrel-reviewed-landmarks-v1","viewmodel_zombie_db_shotgun",r42_names,r42_parents,r42_binds,8,0,7,r42_surfaces,{9,SightGeometryReferenceMethod::weighted_centroid,r42_front},{9,SightGeometryReferenceMethod::weighted_centroid,r42_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,13> r43_names{{
    "j_gun",
    "tag_barrels",
    "tag_brass",
    "tag_lever",
    "tag_longstock",
    "tag_shortstock",
    "tag_extractor",
    "tag_flash",
    "tag_grip",
    "tag_long",
    "tag_short",
    "tag_shell1",
    "tag_shell2"
}};
constexpr std::array<std::uint8_t,12> r43_parents{{
    1,
    2,
    3,
    4,
    5,
    5,
    6,
    7,
    8,
    9,
    5,
    6
}};
constexpr std::array<SightGeometryBone,13> r43_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{2.62091112F,0.00678599998F,0.0488209724F},2.0F},
    {{-0.244819477F,-0.154881433F,-0.651509166F,0.701162755F},{-0.929933548F,-0.630271971F,1.12210083F},1.99996352F},
    {{0.0F,-0.0954924151F,0.0F,0.995422244F},{-1.52936459F,0.0F,1.7459569F},2.00003147F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.50521374F,1.19563103F,0.948781013F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.50521374F,1.19563103F,0.948781013F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-0.00319385529F,0.00119099999F,1.18299389F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{35.3361778F,0.0F,1.25504494F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{7.70002127F,-0.000765999779F,0.224842787F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-0.0249948502F,-0.000765999779F,1.1923759F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-0.0249948502F,-0.000765999779F,1.1923759F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{1.32945395F,-0.623425007F,1.1901319F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{1.32945395F,0.627842009F,1.18909788F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r43_s1_ranges{{
    {9,1988,0,2168}
}};
constexpr std::array<SightGeometryVertex,6> r43_s1_vertices{{
    {776,{22.6552505F,-0.0194579996F,1.93348098F}},
    {777,{22.6314659F,-0.0265180003F,1.93348098F}},
    {778,{22.6314697F,-2.09999998e-05F,1.94017005F}},
    {820,{35.0954742F,-0.0263650008F,1.91469979F}},
    {821,{35.0716896F,-0.0193460006F,1.91469979F}},
    {822,{35.0954742F,-2.09999998e-05F,1.92138886F}}
}};
constexpr std::array<SightGeometryTriangle,2> r43_s1_triangles{{
    {874,{776,777,778}},
    {928,{820,821,822}}
}};
constexpr std::array<SightGeometrySurface,1> r43_surfaces{{
    {1,1988,2168,"mc/mtl_weapon_double_barrel_gold",r43_s1_ranges,r43_s1_vertices,r43_s1_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r43_front{{
    {1,822,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r43_rear{{
    {1,778,1.0F}
}};
const SightGeometryRecipe r43_recipe=[] {
    SightGeometryRecipe value{"zombie_doublebarrel_upgraded-reviewed-landmarks-v1","viewmodel_zombie_db_shotgun_up",r43_names,r43_parents,r43_binds,11,0,7,r43_surfaces,{9,SightGeometryReferenceMethod::weighted_centroid,r43_front},{9,SightGeometryReferenceMethod::weighted_centroid,r43_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,8> r44_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_bipod_left",
    "tag_bipod_right",
    "tag_brass",
    "tag_flash",
    "tag_iron_sights"
}};
constexpr std::array<std::uint8_t,7> r44_parents{{
    1,
    2,
    3,
    4,
    5,
    6,
    7
}};
constexpr std::array<SightGeometryBone,8> r44_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{7.66399622F,-1.96479797F,1.89817905F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{0.760868073F,0.981266975F,2.55579901F},2.0F},
    {{-0.27060762F,0.653279185F,-0.27060762F,0.653279185F},{12.5021124F,0.472169995F,1.71357F},1.99999118F},
    {{-0.27060762F,0.653279185F,-0.27060762F,0.653279185F},{12.5021124F,-0.897131026F,1.71357F},1.99999118F},
    {{-0.0922879726F,-0.0922879726F,-0.701071203F,0.701040685F},{0.408693075F,-0.723205984F,2.82986116F},2.00001407F},
    {{0.0F,0.0F,0.0F,1.0F},{26.6686726F,-0.123690002F,2.76158404F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{0.630241036F,-0.123690002F,4.39427471F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r44_s4_ranges{{
    {7,735,0,594}
}};
constexpr std::array<SightGeometryVertex,11> r44_s4_vertices{{
    {200,{23.3239422F,-0.151950002F,5.31532001F}},
    {201,{23.0150852F,-0.128667995F,5.34185505F}},
    {202,{23.3239422F,-0.128667995F,5.31532001F}},
    {203,{23.0150852F,-0.151950002F,5.34185505F}},
    {370,{0.454560995F,-0.136759996F,5.16037893F}},
    {371,{0.454560995F,-0.105732001F,5.16483974F}},
    {372,{0.588176012F,-0.136759996F,5.16037893F}},
    {390,{0.454560995F,-0.105732001F,5.37618637F}},
    {391,{0.588176012F,-0.105732001F,5.37618637F}},
    {392,{0.454560995F,-0.136759996F,5.38063526F}},
    {393,{0.588176012F,-0.136759996F,5.38063526F}}
}};
constexpr std::array<SightGeometryTriangle,5> r44_s4_triangles{{
    {150,{200,201,202}},
    {151,{201,200,203}},
    {283,{370,371,372}},
    {303,{390,392,391}},
    {304,{392,393,391}}
}};
constexpr std::array<SightGeometrySurface,1> r44_surfaces{{
    {4,735,594,"mc/mtl_weapon_mp_fg42",r44_s4_ranges,r44_s4_vertices,r44_s4_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r44_front{{
    {4,201,1.0F},
    {4,203,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r44_rear{{
    {4,370,1.0F},
    {4,392,1.0F}
}};
const SightGeometryRecipe r44_recipe=[] {
    SightGeometryRecipe value{"zombie_fg42-independently-reviewed-aperture-v1","viewmodel_zombie_fg42_lmg",r44_names,r44_parents,r44_binds,7,0,6,r44_surfaces,{7,SightGeometryReferenceMethod::weighted_centroid,r44_front},{7,SightGeometryReferenceMethod::weighted_centroid,r44_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,8> r45_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_bipod_left",
    "tag_bipod_right",
    "tag_brass",
    "tag_flash",
    "tag_iron_sights"
}};
constexpr std::array<std::uint8_t,7> r45_parents{{
    1,
    2,
    3,
    4,
    5,
    6,
    7
}};
constexpr std::array<SightGeometryBone,8> r45_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{7.66399622F,-1.96479797F,1.89817905F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{0.760868073F,0.981266975F,2.55579901F},2.0F},
    {{-0.27060762F,0.653279185F,-0.27060762F,0.653279185F},{12.5021124F,0.472169995F,1.71357F},1.99999118F},
    {{-0.27060762F,0.653279185F,-0.27060762F,0.653279185F},{12.5021124F,-0.897131026F,1.71357F},1.99999118F},
    {{-0.0922879726F,-0.0922879726F,-0.701071203F,0.701040685F},{0.408693075F,-0.723205984F,2.82986116F},2.00001407F},
    {{0.0F,0.0F,0.0F,1.0F},{26.6686726F,-0.123690002F,2.76158404F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{0.630241036F,-0.123690002F,4.39427471F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r45_s4_ranges{{
    {7,735,0,594}
}};
constexpr std::array<SightGeometryVertex,11> r45_s4_vertices{{
    {200,{23.3239422F,-0.151950002F,5.31532001F}},
    {201,{23.0150852F,-0.128667995F,5.34185505F}},
    {202,{23.3239422F,-0.128667995F,5.31532001F}},
    {203,{23.0150852F,-0.151950002F,5.34185505F}},
    {370,{0.454560995F,-0.136759996F,5.16037893F}},
    {371,{0.454560995F,-0.105732001F,5.16483974F}},
    {372,{0.588176012F,-0.136759996F,5.16037893F}},
    {390,{0.454560995F,-0.105732001F,5.37618637F}},
    {391,{0.588176012F,-0.105732001F,5.37618637F}},
    {392,{0.454560995F,-0.136759996F,5.38063526F}},
    {393,{0.588176012F,-0.136759996F,5.38063526F}}
}};
constexpr std::array<SightGeometryTriangle,5> r45_s4_triangles{{
    {150,{200,201,202}},
    {151,{201,200,203}},
    {283,{370,371,372}},
    {303,{390,392,391}},
    {304,{392,393,391}}
}};
constexpr std::array<SightGeometrySurface,1> r45_surfaces{{
    {4,735,594,"mc/mtl_weapon_mp_fg42",r45_s4_ranges,r45_s4_vertices,r45_s4_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r45_front{{
    {4,201,1.0F},
    {4,203,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r45_rear{{
    {4,370,1.0F},
    {4,392,1.0F}
}};
const SightGeometryRecipe r45_recipe=[] {
    SightGeometryRecipe value{"zombie_fg42_upgraded-independently-reviewed-aperture-v1","viewmodel_zombie_fg42_lmg_up",r45_names,r45_parents,r45_binds,9,0,6,r45_surfaces,{7,SightGeometryReferenceMethod::weighted_centroid,r45_front},{7,SightGeometryReferenceMethod::weighted_centroid,r45_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,7> r46_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_bayonet",
    "tag_brass",
    "tag_flash",
    "tag_iron_sights"
}};
constexpr std::array<std::uint8_t,6> r46_parents{{
    1,
    2,
    3,
    4,
    5,
    6
}};
constexpr std::array<SightGeometryBone,7> r46_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-2.57778811F,0.78857702F,1.97639894F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.02067602F,0.00236900011F,-0.272053003F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{24.5387211F,0.199437007F,0.166539967F},2.0F},
    {{-0.22046572F,-0.201269567F,-0.704824984F,0.643513262F},{-4.04119587F,-0.341722012F,1.42825699F},1.99999571F},
    {{0.0F,0.0F,0.0F,1.0F},{28.6659603F,0.199437007F,0.873004019F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{33.6333275F,0.0F,0.675538957F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r46_s3_ranges{{
    {6,721,0,560}
}};
constexpr std::array<SightGeometryVertex,6> r46_s3_vertices{{
    {33,{0.873076975F,0.0914660022F,2.24851108F}},
    {34,{0.845227003F,0.109591F,2.44644594F}},
    {35,{0.875756979F,0.0807799995F,2.25901914F}},
    {72,{0.83604902F,-0.123103999F,2.43305111F}},
    {73,{0.875757992F,-0.083401002F,2.25901914F}},
    {74,{0.84522599F,-0.112521999F,2.44644713F}}
}};
constexpr std::array<SightGeometryTriangle,2> r46_s3_triangles{{
    {11,{33,34,35}},
    {24,{72,73,74}}
}};
constexpr std::array<SightGeometryRigidRange,1> r46_s5_ranges{{
    {0,1799,0,1395}
}};
constexpr std::array<SightGeometryVertex,6> r46_s5_vertices{{
    {3,{21.5946541F,0.085763F,2.14024615F}},
    {4,{21.6297894F,0.0444720015F,2.50577807F}},
    {5,{21.5946541F,0.0484399982F,2.476722F}},
    {278,{21.9136486F,-0.0412570015F,2.45152807F}},
    {279,{21.6297894F,0.0444720015F,2.50577807F}},
    {281,{21.6297894F,-0.0412570015F,2.50577807F}}
}};
constexpr std::array<SightGeometryTriangle,2> r46_s5_triangles{{
    {1,{3,4,5}},
    {160,{279,278,281}}
}};
constexpr std::array<SightGeometrySurface,2> r46_surfaces{{
    {3,721,560,"mc/mtl_weapon_mp_g43",r46_s3_ranges,r46_s3_vertices,r46_s3_triangles},
    {5,1799,1396,"mc/mtl_weapon_mp_g43",r46_s5_ranges,r46_s5_vertices,r46_s5_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r46_front{{
    {5,4,1.0F},
    {5,281,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r46_rear{{
    {3,34,1.0F},
    {3,74,1.0F}
}};
const SightGeometryRecipe r46_recipe=[] {
    SightGeometryRecipe value{"transferred-zombie_gewehr43-mesh-v1","viewmodel_zombie_g43_rifle",r46_names,r46_parents,r46_binds,6,0,5,r46_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r46_front},{6,SightGeometryReferenceMethod::weighted_centroid,r46_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,7> r47_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_bayonet",
    "tag_brass",
    "tag_flash",
    "tag_iron_sights"
}};
constexpr std::array<std::uint8_t,6> r47_parents{{
    1,
    2,
    3,
    4,
    5,
    6
}};
constexpr std::array<SightGeometryBone,7> r47_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-2.57778811F,0.78857702F,1.97639894F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.02067602F,0.00236900011F,-0.272053003F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{24.5387211F,0.199437007F,0.166539967F},2.0F},
    {{-0.22046572F,-0.201269567F,-0.704824984F,0.643513262F},{-4.04119587F,-0.341722012F,1.42825699F},1.99999571F},
    {{0.0F,0.0F,0.0F,1.0F},{28.6659603F,0.199437007F,0.873004019F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{33.6333275F,0.0F,0.675538957F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r47_s3_ranges{{
    {6,721,0,560}
}};
constexpr std::array<SightGeometryVertex,6> r47_s3_vertices{{
    {33,{0.873076975F,0.0914660022F,2.24851108F}},
    {34,{0.845227003F,0.109591F,2.44644594F}},
    {35,{0.875756979F,0.0807799995F,2.25901914F}},
    {72,{0.83604902F,-0.123103999F,2.43305111F}},
    {73,{0.875757992F,-0.083401002F,2.25901914F}},
    {74,{0.84522599F,-0.112521999F,2.44644713F}}
}};
constexpr std::array<SightGeometryTriangle,2> r47_s3_triangles{{
    {11,{33,34,35}},
    {24,{72,73,74}}
}};
constexpr std::array<SightGeometryRigidRange,1> r47_s5_ranges{{
    {0,1799,0,1395}
}};
constexpr std::array<SightGeometryVertex,6> r47_s5_vertices{{
    {3,{21.5946541F,0.085763F,2.14024615F}},
    {4,{21.6297894F,0.0444720015F,2.50577807F}},
    {5,{21.5946541F,0.0484399982F,2.476722F}},
    {278,{21.9136486F,-0.0412570015F,2.45152807F}},
    {279,{21.6297894F,0.0444720015F,2.50577807F}},
    {281,{21.6297894F,-0.0412570015F,2.50577807F}}
}};
constexpr std::array<SightGeometryTriangle,2> r47_s5_triangles{{
    {1,{3,4,5}},
    {160,{279,278,281}}
}};
constexpr std::array<SightGeometrySurface,2> r47_surfaces{{
    {3,721,560,"mc/mtl_weapon_g43_gold",r47_s3_ranges,r47_s3_vertices,r47_s3_triangles},
    {5,1799,1396,"mc/mtl_weapon_g43_gold",r47_s5_ranges,r47_s5_vertices,r47_s5_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r47_front{{
    {5,4,1.0F},
    {5,281,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r47_rear{{
    {3,34,1.0F},
    {3,74,1.0F}
}};
const SightGeometryRecipe r47_recipe=[] {
    SightGeometryRecipe value{"transferred-zombie_gewehr43_upgraded-mesh-v1","viewmodel_zombie_g43_rifle_up",r47_names,r47_parents,r47_binds,10,0,5,r47_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r47_front},{6,SightGeometryReferenceMethod::weighted_centroid,r47_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,7> r48_names{{
    "j_gun",
    "j_bolt",
    "j_round",
    "j_stripper_clip",
    "j_stripper_rounds",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,6> r48_parents{{
    1,
    2,
    3,
    4,
    5,
    6
}};
constexpr std::array<SightGeometryBone,7> r48_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-7.73876619F,-0.0619830005F,1.10182285F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.21501803F,37.1427116F,8.61235905F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-3.03221989F,37.1474953F,7.33137321F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.21501803F,37.1427116F,7.59117126F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.79524302F,-0.341722012F,1.12209988F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{27.8524189F,0.0F,1.52065086F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r48_s4_ranges{{
    {0,2623,0,2465}
}};
constexpr std::array<SightGeometryVertex,14> r48_s4_vertices{{
    {1116,{28.4723244F,-0.0495010018F,2.00962257F}},
    {1118,{28.8415737F,-0.0495010018F,2.00962257F}},
    {1119,{28.5401554F,0.0F,2.15320396F}},
    {1733,{4.89192915F,-0.463528007F,1.86741734F}},
    {1734,{4.97338581F,-0.160611004F,1.86741734F}},
    {1735,{4.89192915F,-0.160611004F,1.86741734F}},
    {1736,{4.93165302F,-1.99999999e-06F,1.78584194F}},
    {1737,{4.85019684F,-1.99999999e-06F,1.78584194F}},
    {1738,{4.93165302F,-1.99999999e-06F,1.78584194F}},
    {1739,{4.85019684F,-1.99999999e-06F,1.78584194F}},
    {1740,{4.89192915F,0.160607994F,1.86741734F}},
    {1741,{4.97338581F,0.160607994F,1.86741734F}},
    {1742,{4.97338581F,0.463528007F,1.86741734F}},
    {1743,{4.89192915F,0.463528007F,1.86741734F}}
}};
constexpr std::array<SightGeometryTriangle,8> r48_s4_triangles{{
    {1132,{1116,1119,1118}},
    {1624,{1733,1735,1734}},
    {1625,{1734,1735,1736}},
    {1626,{1735,1737,1736}},
    {1627,{1738,1739,1740}},
    {1628,{1741,1738,1740}},
    {1629,{1741,1740,1742}},
    {1630,{1740,1743,1742}}
}};
constexpr std::array<SightGeometrySurface,1> r48_surfaces{{
    {4,2623,2466,"mc/mtl_weapon_mp_kar98",r48_s4_ranges,r48_s4_vertices,r48_s4_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r48_front{{
    {4,1119,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r48_rear{{
    {4,1735,1.0F},
    {4,1740,1.0F}
}};
const SightGeometryRecipe r48_recipe=[] {
    SightGeometryRecipe value{"transferred-zombie_kar98k-mesh-v1","viewmodel_zombie_kar98k_rifle",r48_names,r48_parents,r48_binds,6,0,6,r48_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r48_front},{0,SightGeometryReferenceMethod::weighted_centroid,r48_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,7> r49_names{{
    "j_gun",
    "j_bolt",
    "j_round",
    "j_stripper_clip",
    "j_stripper_rounds",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,6> r49_parents{{
    1,
    2,
    3,
    4,
    5,
    6
}};
constexpr std::array<SightGeometryBone,7> r49_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-7.73876619F,-0.0619830005F,1.10182285F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.21501803F,37.1427116F,8.61235905F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-3.03221989F,37.1474953F,7.33137321F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.21501803F,37.1427116F,7.59117126F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.79524302F,-0.341722012F,1.12209988F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{27.8524189F,0.0F,1.52065086F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r49_s4_ranges{{
    {0,2623,0,2465}
}};
constexpr std::array<SightGeometryVertex,14> r49_s4_vertices{{
    {1116,{28.4723244F,-0.0495010018F,2.00962257F}},
    {1118,{28.8415737F,-0.0495010018F,2.00962257F}},
    {1119,{28.5401554F,0.0F,2.15320396F}},
    {1733,{4.89192915F,-0.463528007F,1.86741734F}},
    {1734,{4.97338581F,-0.160611004F,1.86741734F}},
    {1735,{4.89192915F,-0.160611004F,1.86741734F}},
    {1736,{4.93165302F,-1.99999999e-06F,1.78584194F}},
    {1737,{4.85019684F,-1.99999999e-06F,1.78584194F}},
    {1738,{4.93165302F,-1.99999999e-06F,1.78584194F}},
    {1739,{4.85019684F,-1.99999999e-06F,1.78584194F}},
    {1740,{4.89192915F,0.160607994F,1.86741734F}},
    {1741,{4.97338581F,0.160607994F,1.86741734F}},
    {1742,{4.97338581F,0.463528007F,1.86741734F}},
    {1743,{4.89192915F,0.463528007F,1.86741734F}}
}};
constexpr std::array<SightGeometryTriangle,8> r49_s4_triangles{{
    {1132,{1116,1119,1118}},
    {1624,{1733,1735,1734}},
    {1625,{1734,1735,1736}},
    {1626,{1735,1737,1736}},
    {1627,{1738,1739,1740}},
    {1628,{1741,1738,1740}},
    {1629,{1741,1740,1742}},
    {1630,{1740,1743,1742}}
}};
constexpr std::array<SightGeometrySurface,1> r49_surfaces{{
    {4,2623,2466,"mc/mtl_weapon_mp_kar98",r49_s4_ranges,r49_s4_vertices,r49_s4_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r49_front{{
    {4,1119,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r49_rear{{
    {4,1735,1.0F},
    {4,1740,1.0F}
}};
const SightGeometryRecipe r49_recipe=[] {
    SightGeometryRecipe value{"transferred-zombie_kar98k_upgraded-mesh-v1","viewmodel_zombie_kar98k_rifle_up",r49_names,r49_parents,r49_binds,8,0,6,r49_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r49_front},{0,SightGeometryReferenceMethod::weighted_centroid,r49_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,5> r50_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,4> r50_parents{{
    1,
    2,
    3,
    4
}};
constexpr std::array<SightGeometryBone,5> r50_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-1.946244F,-1.78257596F,0.924210012F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.43627906F,0.0128659997F,-0.551743984F},2.0F},
    {{-0.239814445F,-0.177190468F,-0.631153286F,0.71605581F},{-2.95196199F,-0.662280023F,1.12210095F},2.00000453F},
    {{0.0F,0.0F,0.0F,1.0F},{22.1419277F,0.0F,0.984408975F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r50_s1_ranges{{
    {0,2338,0,2249}
}};
constexpr std::array<SightGeometryVertex,72> r50_s1_vertices{{
    {1936,{20.4204235F,-0.0603840016F,1.821298F}},
    {1937,{19.7439327F,-0.0415010005F,2.17477298F}},
    {1938,{19.7828388F,-0.0404560007F,2.19433308F}},
    {1939,{20.4204235F,-0.0404560007F,2.19433308F}},
    {1944,{20.4204235F,0.0414439999F,1.821298F}},
    {1946,{19.7439327F,0.0225610007F,2.17477298F}},
    {1947,{19.7828388F,0.0215160009F,2.19433308F}},
    {1948,{20.4204235F,0.0215160009F,2.19433308F}},
    {2052,{-6.5552392F,0.117397003F,1.68666196F}},
    {2053,{-6.45820904F,0.097916998F,1.706074F}},
    {2054,{-6.5552392F,0.0999199972F,1.66919506F}},
    {2055,{-6.5552392F,0.137236997F,1.71633697F}},
    {2056,{-6.45820904F,0.111791998F,1.72683895F}},
    {2057,{-6.45820904F,0.121247999F,1.749668F}},
    {2058,{-6.5552392F,0.146697998F,1.73916399F}},
    {2059,{-6.5552392F,0.153652996F,1.77411199F}},
    {2060,{-6.45820904F,0.126120001F,1.77416301F}},
    {2061,{-6.45820904F,0.126120001F,1.79887295F}},
    {2062,{-6.5552392F,0.153652996F,1.79892302F}},
    {2063,{-6.5552392F,0.146706998F,1.83382094F}},
    {2064,{-6.45820904F,0.121247999F,1.823367F}},
    {2065,{-6.45820904F,0.111791998F,1.84619606F}},
    {2066,{-6.5552392F,0.137208F,1.85674202F}},
    {2067,{-6.5552392F,0.117426001F,1.88633001F}},
    {2068,{-6.45820904F,0.097916998F,1.86696196F}},
    {2069,{-6.45820904F,0.0804440007F,1.88443494F}},
    {2070,{-6.5552392F,0.0998760015F,1.90386999F}},
    {2071,{-6.5552392F,0.0702650025F,1.92364299F}},
    {2072,{-6.45820904F,0.0596790016F,1.89830899F}},
    {2073,{-6.45820904F,0.0368489996F,1.90776598F}},
    {2074,{-6.5552392F,0.0473389998F,1.93313396F}},
    {2075,{-6.5552392F,0.0124059999F,1.94007802F}},
    {2076,{-6.45820904F,0.0804440007F,1.68860102F}},
    {2077,{-6.45820904F,0.0123549998F,1.91263795F}},
    {2078,{-6.45820904F,-0.0123549998F,1.91263795F}},
    {2079,{-6.5552392F,-0.0124059999F,1.94007802F}},
    {2080,{-6.5552392F,-0.0473389998F,1.93313396F}},
    {2081,{-6.45820904F,-0.0368489996F,1.90776598F}},
    {2082,{-6.45820904F,-0.0596790016F,1.89830899F}},
    {2083,{-6.5552392F,-0.0702650025F,1.92364299F}},
    {2084,{-6.5552392F,-0.0998760015F,1.90386999F}},
    {2085,{-6.45820904F,-0.0804440007F,1.88443494F}},
    {2086,{-6.45820904F,-0.097916998F,1.86696196F}},
    {2087,{-6.5552392F,-0.117426001F,1.88633001F}},
    {2088,{-6.5552392F,-0.137208F,1.85674202F}},
    {2089,{-6.45820904F,-0.111791998F,1.84619606F}},
    {2090,{-6.45820904F,-0.121247999F,1.823367F}},
    {2091,{-6.5552392F,-0.146706998F,1.83382094F}},
    {2092,{-6.5552392F,-0.153652996F,1.79892302F}},
    {2093,{-6.45820904F,-0.126120001F,1.79887295F}},
    {2094,{-6.45820904F,-0.126120001F,1.77416301F}},
    {2095,{-6.5552392F,-0.153652996F,1.77411199F}},
    {2096,{-6.45820904F,0.0596790016F,1.67472601F}},
    {2097,{-6.5552392F,0.0702219978F,1.64936399F}},
    {2098,{-6.5552392F,-0.146697998F,1.73916399F}},
    {2099,{-6.45820904F,-0.121247999F,1.749668F}},
    {2100,{-6.45820904F,-0.111791998F,1.72683895F}},
    {2101,{-6.5552392F,-0.137236997F,1.71633697F}},
    {2102,{-6.5552392F,-0.117397003F,1.68666196F}},
    {2103,{-6.45820904F,-0.097916998F,1.706074F}},
    {2104,{-6.45820904F,-0.0804440007F,1.68860102F}},
    {2105,{-6.5552392F,-0.0999189988F,1.66919506F}},
    {2106,{-6.5552392F,-0.0702219978F,1.64936399F}},
    {2107,{-6.45820904F,-0.0596790016F,1.67472601F}},
    {2108,{-6.45820904F,-0.0368489996F,1.66526997F}},
    {2109,{-6.5552392F,-0.047389999F,1.63991201F}},
    {2110,{-6.5552392F,-0.0123549998F,1.63294697F}},
    {2111,{-6.45820904F,-0.0123549998F,1.66039801F}},
    {2112,{-6.45820904F,0.0123549998F,1.66039801F}},
    {2113,{-6.5552392F,0.012356F,1.63294697F}},
    {2114,{-6.5552392F,0.047389999F,1.63991201F}},
    {2115,{-6.45820904F,0.0368489996F,1.66526997F}}
}};
constexpr std::array<SightGeometryTriangle,68> r50_s1_triangles{{
    {1836,{1937,1938,1936}},
    {1837,{1936,1938,1939}},
    {1843,{1946,1944,1947}},
    {1844,{1944,1948,1947}},
    {1945,{2052,2053,2054}},
    {1946,{2053,2052,2055}},
    {1947,{2056,2053,2055}},
    {1948,{2057,2056,2055}},
    {1949,{2058,2057,2055}},
    {1950,{2057,2058,2059}},
    {1951,{2060,2057,2059}},
    {1952,{2061,2060,2059}},
    {1953,{2062,2061,2059}},
    {1954,{2061,2062,2063}},
    {1955,{2064,2061,2063}},
    {1956,{2065,2064,2063}},
    {1957,{2066,2065,2063}},
    {1958,{2065,2066,2067}},
    {1959,{2068,2065,2067}},
    {1960,{2069,2068,2067}},
    {1961,{2070,2069,2067}},
    {1962,{2069,2070,2071}},
    {1963,{2072,2069,2071}},
    {1964,{2073,2072,2071}},
    {1965,{2074,2073,2071}},
    {1966,{2073,2074,2075}},
    {1967,{2053,2076,2054}},
    {1968,{2077,2073,2075}},
    {1969,{2078,2077,2075}},
    {1970,{2079,2078,2075}},
    {1971,{2078,2079,2080}},
    {1972,{2081,2078,2080}},
    {1973,{2082,2081,2080}},
    {1974,{2083,2082,2080}},
    {1975,{2082,2083,2084}},
    {1976,{2085,2082,2084}},
    {1977,{2086,2085,2084}},
    {1978,{2087,2086,2084}},
    {1979,{2086,2087,2088}},
    {1980,{2089,2086,2088}},
    {1981,{2090,2089,2088}},
    {1982,{2091,2090,2088}},
    {1983,{2090,2091,2092}},
    {1984,{2093,2090,2092}},
    {1985,{2094,2093,2092}},
    {1986,{2095,2094,2092}},
    {1987,{2076,2096,2054}},
    {1988,{2096,2097,2054}},
    {1989,{2094,2095,2098}},
    {1990,{2099,2094,2098}},
    {1991,{2100,2099,2098}},
    {1992,{2101,2100,2098}},
    {1993,{2100,2101,2102}},
    {1994,{2103,2100,2102}},
    {1995,{2104,2103,2102}},
    {1996,{2105,2104,2102}},
    {1997,{2104,2105,2106}},
    {1998,{2107,2104,2106}},
    {1999,{2108,2107,2106}},
    {2000,{2109,2108,2106}},
    {2001,{2108,2109,2110}},
    {2002,{2111,2108,2110}},
    {2003,{2112,2111,2110}},
    {2004,{2113,2112,2110}},
    {2005,{2112,2113,2114}},
    {2006,{2097,2096,2114}},
    {2007,{2115,2112,2114}},
    {2008,{2096,2115,2114}}
}};
constexpr std::array<SightGeometrySurface,1> r50_surfaces{{
    {1,2338,2250,"mc/mtl_weapon_mp_carbine",r50_s1_ranges,r50_s1_vertices,r50_s1_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r50_front{{
    {1,1938,1.0F},
    {1,1947,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,32> r50_rear{{
    {1,2053,1.0F},
    {1,2056,1.0F},
    {1,2057,1.0F},
    {1,2060,1.0F},
    {1,2061,1.0F},
    {1,2064,1.0F},
    {1,2065,1.0F},
    {1,2068,1.0F},
    {1,2069,1.0F},
    {1,2072,1.0F},
    {1,2073,1.0F},
    {1,2076,1.0F},
    {1,2077,1.0F},
    {1,2078,1.0F},
    {1,2081,1.0F},
    {1,2082,1.0F},
    {1,2085,1.0F},
    {1,2086,1.0F},
    {1,2089,1.0F},
    {1,2090,1.0F},
    {1,2093,1.0F},
    {1,2094,1.0F},
    {1,2096,1.0F},
    {1,2099,1.0F},
    {1,2100,1.0F},
    {1,2103,1.0F},
    {1,2104,1.0F},
    {1,2107,1.0F},
    {1,2108,1.0F},
    {1,2111,1.0F},
    {1,2112,1.0F},
    {1,2115,1.0F}
}};
const SightGeometryRecipe r50_recipe=[] {
    SightGeometryRecipe value{"transferred-zombie_m1carbine-mesh-v1","viewmodel_zombie_m1carbine_rifle",r50_names,r50_parents,r50_binds,4,0,4,r50_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r50_front},{0,SightGeometryReferenceMethod::aperture_circle_yz,r50_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,5> r51_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,4> r51_parents{{
    1,
    2,
    3,
    4
}};
constexpr std::array<SightGeometryBone,5> r51_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-1.946244F,-1.78257596F,0.924210012F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.43627906F,0.0128659997F,-0.551743984F},2.0F},
    {{-0.239814445F,-0.177190468F,-0.631153286F,0.71605581F},{-2.95196199F,-0.662280023F,1.12210095F},2.00000453F},
    {{0.0F,0.0F,0.0F,1.0F},{22.1419277F,0.0F,0.984408975F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r51_s1_ranges{{
    {0,2338,0,2249}
}};
constexpr std::array<SightGeometryVertex,72> r51_s1_vertices{{
    {1936,{20.4204235F,-0.0603840016F,1.821298F}},
    {1937,{19.7439327F,-0.0415010005F,2.17477298F}},
    {1938,{19.7828388F,-0.0404560007F,2.19433308F}},
    {1939,{20.4204235F,-0.0404560007F,2.19433308F}},
    {1944,{20.4204235F,0.0414439999F,1.821298F}},
    {1946,{19.7439327F,0.0225610007F,2.17477298F}},
    {1947,{19.7828388F,0.0215160009F,2.19433308F}},
    {1948,{20.4204235F,0.0215160009F,2.19433308F}},
    {2052,{-6.5552392F,0.117397003F,1.68666196F}},
    {2053,{-6.45820904F,0.097916998F,1.706074F}},
    {2054,{-6.5552392F,0.0999199972F,1.66919506F}},
    {2055,{-6.5552392F,0.137236997F,1.71633697F}},
    {2056,{-6.45820904F,0.111791998F,1.72683895F}},
    {2057,{-6.45820904F,0.121247999F,1.749668F}},
    {2058,{-6.5552392F,0.146697998F,1.73916399F}},
    {2059,{-6.5552392F,0.153652996F,1.77411199F}},
    {2060,{-6.45820904F,0.126120001F,1.77416301F}},
    {2061,{-6.45820904F,0.126120001F,1.79887295F}},
    {2062,{-6.5552392F,0.153652996F,1.79892302F}},
    {2063,{-6.5552392F,0.146706998F,1.83382094F}},
    {2064,{-6.45820904F,0.121247999F,1.823367F}},
    {2065,{-6.45820904F,0.111791998F,1.84619606F}},
    {2066,{-6.5552392F,0.137208F,1.85674202F}},
    {2067,{-6.5552392F,0.117426001F,1.88633001F}},
    {2068,{-6.45820904F,0.097916998F,1.86696196F}},
    {2069,{-6.45820904F,0.0804440007F,1.88443494F}},
    {2070,{-6.5552392F,0.0998760015F,1.90386999F}},
    {2071,{-6.5552392F,0.0702650025F,1.92364299F}},
    {2072,{-6.45820904F,0.0596790016F,1.89830899F}},
    {2073,{-6.45820904F,0.0368489996F,1.90776598F}},
    {2074,{-6.5552392F,0.0473389998F,1.93313396F}},
    {2075,{-6.5552392F,0.0124059999F,1.94007802F}},
    {2076,{-6.45820904F,0.0804440007F,1.68860102F}},
    {2077,{-6.45820904F,0.0123549998F,1.91263795F}},
    {2078,{-6.45820904F,-0.0123549998F,1.91263795F}},
    {2079,{-6.5552392F,-0.0124059999F,1.94007802F}},
    {2080,{-6.5552392F,-0.0473389998F,1.93313396F}},
    {2081,{-6.45820904F,-0.0368489996F,1.90776598F}},
    {2082,{-6.45820904F,-0.0596790016F,1.89830899F}},
    {2083,{-6.5552392F,-0.0702650025F,1.92364299F}},
    {2084,{-6.5552392F,-0.0998760015F,1.90386999F}},
    {2085,{-6.45820904F,-0.0804440007F,1.88443494F}},
    {2086,{-6.45820904F,-0.097916998F,1.86696196F}},
    {2087,{-6.5552392F,-0.117426001F,1.88633001F}},
    {2088,{-6.5552392F,-0.137208F,1.85674202F}},
    {2089,{-6.45820904F,-0.111791998F,1.84619606F}},
    {2090,{-6.45820904F,-0.121247999F,1.823367F}},
    {2091,{-6.5552392F,-0.146706998F,1.83382094F}},
    {2092,{-6.5552392F,-0.153652996F,1.79892302F}},
    {2093,{-6.45820904F,-0.126120001F,1.79887295F}},
    {2094,{-6.45820904F,-0.126120001F,1.77416301F}},
    {2095,{-6.5552392F,-0.153652996F,1.77411199F}},
    {2096,{-6.45820904F,0.0596790016F,1.67472601F}},
    {2097,{-6.5552392F,0.0702219978F,1.64936399F}},
    {2098,{-6.5552392F,-0.146697998F,1.73916399F}},
    {2099,{-6.45820904F,-0.121247999F,1.749668F}},
    {2100,{-6.45820904F,-0.111791998F,1.72683895F}},
    {2101,{-6.5552392F,-0.137236997F,1.71633697F}},
    {2102,{-6.5552392F,-0.117397003F,1.68666196F}},
    {2103,{-6.45820904F,-0.097916998F,1.706074F}},
    {2104,{-6.45820904F,-0.0804440007F,1.68860102F}},
    {2105,{-6.5552392F,-0.0999189988F,1.66919506F}},
    {2106,{-6.5552392F,-0.0702219978F,1.64936399F}},
    {2107,{-6.45820904F,-0.0596790016F,1.67472601F}},
    {2108,{-6.45820904F,-0.0368489996F,1.66526997F}},
    {2109,{-6.5552392F,-0.047389999F,1.63991201F}},
    {2110,{-6.5552392F,-0.0123549998F,1.63294697F}},
    {2111,{-6.45820904F,-0.0123549998F,1.66039801F}},
    {2112,{-6.45820904F,0.0123549998F,1.66039801F}},
    {2113,{-6.5552392F,0.012356F,1.63294697F}},
    {2114,{-6.5552392F,0.047389999F,1.63991201F}},
    {2115,{-6.45820904F,0.0368489996F,1.66526997F}}
}};
constexpr std::array<SightGeometryTriangle,68> r51_s1_triangles{{
    {1836,{1937,1938,1936}},
    {1837,{1936,1938,1939}},
    {1843,{1946,1944,1947}},
    {1844,{1944,1948,1947}},
    {1945,{2052,2053,2054}},
    {1946,{2053,2052,2055}},
    {1947,{2056,2053,2055}},
    {1948,{2057,2056,2055}},
    {1949,{2058,2057,2055}},
    {1950,{2057,2058,2059}},
    {1951,{2060,2057,2059}},
    {1952,{2061,2060,2059}},
    {1953,{2062,2061,2059}},
    {1954,{2061,2062,2063}},
    {1955,{2064,2061,2063}},
    {1956,{2065,2064,2063}},
    {1957,{2066,2065,2063}},
    {1958,{2065,2066,2067}},
    {1959,{2068,2065,2067}},
    {1960,{2069,2068,2067}},
    {1961,{2070,2069,2067}},
    {1962,{2069,2070,2071}},
    {1963,{2072,2069,2071}},
    {1964,{2073,2072,2071}},
    {1965,{2074,2073,2071}},
    {1966,{2073,2074,2075}},
    {1967,{2053,2076,2054}},
    {1968,{2077,2073,2075}},
    {1969,{2078,2077,2075}},
    {1970,{2079,2078,2075}},
    {1971,{2078,2079,2080}},
    {1972,{2081,2078,2080}},
    {1973,{2082,2081,2080}},
    {1974,{2083,2082,2080}},
    {1975,{2082,2083,2084}},
    {1976,{2085,2082,2084}},
    {1977,{2086,2085,2084}},
    {1978,{2087,2086,2084}},
    {1979,{2086,2087,2088}},
    {1980,{2089,2086,2088}},
    {1981,{2090,2089,2088}},
    {1982,{2091,2090,2088}},
    {1983,{2090,2091,2092}},
    {1984,{2093,2090,2092}},
    {1985,{2094,2093,2092}},
    {1986,{2095,2094,2092}},
    {1987,{2076,2096,2054}},
    {1988,{2096,2097,2054}},
    {1989,{2094,2095,2098}},
    {1990,{2099,2094,2098}},
    {1991,{2100,2099,2098}},
    {1992,{2101,2100,2098}},
    {1993,{2100,2101,2102}},
    {1994,{2103,2100,2102}},
    {1995,{2104,2103,2102}},
    {1996,{2105,2104,2102}},
    {1997,{2104,2105,2106}},
    {1998,{2107,2104,2106}},
    {1999,{2108,2107,2106}},
    {2000,{2109,2108,2106}},
    {2001,{2108,2109,2110}},
    {2002,{2111,2108,2110}},
    {2003,{2112,2111,2110}},
    {2004,{2113,2112,2110}},
    {2005,{2112,2113,2114}},
    {2006,{2097,2096,2114}},
    {2007,{2115,2112,2114}},
    {2008,{2096,2115,2114}}
}};
constexpr std::array<SightGeometrySurface,1> r51_surfaces{{
    {1,2338,2250,"mc/mtl_weapon_carbine_gold",r51_s1_ranges,r51_s1_vertices,r51_s1_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r51_front{{
    {1,1938,1.0F},
    {1,1947,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,32> r51_rear{{
    {1,2053,1.0F},
    {1,2056,1.0F},
    {1,2057,1.0F},
    {1,2060,1.0F},
    {1,2061,1.0F},
    {1,2064,1.0F},
    {1,2065,1.0F},
    {1,2068,1.0F},
    {1,2069,1.0F},
    {1,2072,1.0F},
    {1,2073,1.0F},
    {1,2076,1.0F},
    {1,2077,1.0F},
    {1,2078,1.0F},
    {1,2081,1.0F},
    {1,2082,1.0F},
    {1,2085,1.0F},
    {1,2086,1.0F},
    {1,2089,1.0F},
    {1,2090,1.0F},
    {1,2093,1.0F},
    {1,2094,1.0F},
    {1,2096,1.0F},
    {1,2099,1.0F},
    {1,2100,1.0F},
    {1,2103,1.0F},
    {1,2104,1.0F},
    {1,2107,1.0F},
    {1,2108,1.0F},
    {1,2111,1.0F},
    {1,2112,1.0F},
    {1,2115,1.0F}
}};
const SightGeometryRecipe r51_recipe=[] {
    SightGeometryRecipe value{"transferred-zombie_m1carbine_upgraded-mesh-v1","viewmodel_zombie_m1carbine_rifle_up",r51_names,r51_parents,r51_binds,6,0,4,r51_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r51_front},{0,SightGeometryReferenceMethod::aperture_circle_yz,r51_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,7> r52_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "j_stripper",
    "tag_brass",
    "tag_flash",
    "tag_iron_sights"
}};
constexpr std::array<std::uint8_t,6> r52_parents{{
    1,
    2,
    3,
    4,
    5,
    6
}};
constexpr std::array<SightGeometryBone,7> r52_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-0.273142993F,-1.81032395F,1.10182405F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.88877106F,-0.00603500009F,-7.28390694F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.88877106F,-0.00603500009F,-7.28390694F},2.0F},
    {{-0.173802912F,-0.202703938F,-0.685995042F,0.67683953F},{-1.79524302F,-0.608017027F,1.63733804F},2.00000548F},
    {{0.0F,0.0F,0.0F,1.0F},{27.8524189F,0.0F,1.52065206F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{5.62187719F,-0.409855992F,0.844623983F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,3> r52_s0_ranges{{
    {0,84,0,58},
    {3,65,58,33},
    {6,86,91,58}
}};
constexpr std::array<SightGeometryVertex,4> r52_s0_vertices{{
    {149,{25.7279968F,-0.031578999F,2.64245701F}},
    {150,{25.7279968F,0.031578999F,2.64245701F}},
    {151,{26.221981F,-0.031578999F,2.73525596F}},
    {152,{26.221981F,0.031578999F,2.73525596F}}
}};
constexpr std::array<SightGeometryTriangle,2> r52_s0_triangles{{
    {91,{149,150,151}},
    {92,{150,152,151}}
}};
constexpr std::array<SightGeometryRigidRange,1> r52_s2_ranges{{
    {6,249,0,166}
}};
constexpr std::array<SightGeometryVertex,14> r52_s2_vertices{{
    {9,{-5.7963748F,-0.0512689985F,2.35793805F}},
    {10,{-5.79649687F,0.0201769993F,2.36598706F}},
    {11,{-5.80678177F,-0.0533340015F,2.36368299F}},
    {27,{-5.79383421F,-0.158447996F,2.18738198F}},
    {28,{-5.79489803F,-0.150398001F,2.25882006F}},
    {29,{-5.804142F,-0.164660007F,2.18652892F}},
    {57,{-5.79174709F,0.0382639989F,2.04698801F}},
    {58,{-5.80191278F,0.00334899989F,2.03661108F}},
    {59,{-5.7916851F,0.00208800007F,2.04291201F}},
    {60,{-5.80191422F,0.000827000011F,2.03661108F}},
    {61,{-5.79174519F,-0.0340880007F,2.04698801F}},
    {64,{-5.79437923F,0.162624002F,2.2235539F}},
    {65,{-5.79331398F,0.154574007F,2.15211701F}},
    {66,{-5.804708F,0.168834999F,2.22410011F}}
}};
constexpr std::array<SightGeometryTriangle,6> r52_s2_triangles{{
    {3,{9,10,11}},
    {10,{27,28,29}},
    {24,{57,59,58}},
    {25,{59,60,58}},
    {26,{59,61,60}},
    {29,{64,65,66}}
}};
constexpr std::array<SightGeometrySurface,2> r52_surfaces{{
    {0,235,150,"mc/mtl_weapon_mp_m1garand",r52_s0_ranges,r52_s0_vertices,r52_s0_triangles},
    {2,249,166,"mc/mtl_weapon_mp_m1garand",r52_s2_ranges,r52_s2_vertices,r52_s2_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,4> r52_front{{
    {0,149,1.0F},
    {0,150,1.0F},
    {0,151,1.0F},
    {0,152,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,4> r52_rear{{
    {2,27,1.0F},
    {2,64,1.0F},
    {2,10,1.0F},
    {2,59,1.0F}
}};
const SightGeometryRecipe r52_recipe=[] {
    SightGeometryRecipe value{"transferred-zombie_m1garand-mesh-v1","viewmodel_zombie_m1garand_rifle",r52_names,r52_parents,r52_binds,6,0,5,r52_surfaces,{6,SightGeometryReferenceMethod::weighted_centroid,r52_front},{6,SightGeometryReferenceMethod::aperture_circle_yz,r52_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,7> r53_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "j_stripper",
    "tag_brass",
    "tag_flash",
    "tag_iron_sights"
}};
constexpr std::array<std::uint8_t,6> r53_parents{{
    1,
    2,
    3,
    4,
    5,
    6
}};
constexpr std::array<SightGeometryBone,7> r53_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-0.273142993F,-1.81032395F,1.10182405F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.88877106F,-0.00603500009F,-7.28390694F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.88877106F,-0.00603500009F,-7.28390694F},2.0F},
    {{-0.173802912F,-0.202703938F,-0.685995042F,0.67683953F},{-1.79524302F,-0.608017027F,1.63733804F},2.00000548F},
    {{0.0F,0.0F,0.0F,1.0F},{27.8524189F,0.0F,1.52065206F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{5.62187719F,-0.409855992F,0.844623983F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,3> r53_s0_ranges{{
    {0,84,0,58},
    {3,65,58,33},
    {6,86,91,58}
}};
constexpr std::array<SightGeometryVertex,4> r53_s0_vertices{{
    {149,{25.7279968F,-0.031578999F,2.64245701F}},
    {150,{25.7279968F,0.031578999F,2.64245701F}},
    {151,{26.221981F,-0.031578999F,2.73525596F}},
    {152,{26.221981F,0.031578999F,2.73525596F}}
}};
constexpr std::array<SightGeometryTriangle,2> r53_s0_triangles{{
    {91,{149,150,151}},
    {92,{150,152,151}}
}};
constexpr std::array<SightGeometryRigidRange,1> r53_s2_ranges{{
    {6,249,0,166}
}};
constexpr std::array<SightGeometryVertex,14> r53_s2_vertices{{
    {9,{-5.7963748F,-0.0512689985F,2.35793805F}},
    {10,{-5.79649687F,0.0201769993F,2.36598706F}},
    {11,{-5.80678177F,-0.0533340015F,2.36368299F}},
    {27,{-5.79383421F,-0.158447996F,2.18738198F}},
    {28,{-5.79489803F,-0.150398001F,2.25882006F}},
    {29,{-5.804142F,-0.164660007F,2.18652892F}},
    {57,{-5.79174709F,0.0382639989F,2.04698801F}},
    {58,{-5.80191278F,0.00334899989F,2.03661108F}},
    {59,{-5.7916851F,0.00208800007F,2.04291201F}},
    {60,{-5.80191422F,0.000827000011F,2.03661108F}},
    {61,{-5.79174519F,-0.0340880007F,2.04698801F}},
    {64,{-5.79437923F,0.162624002F,2.2235539F}},
    {65,{-5.79331398F,0.154574007F,2.15211701F}},
    {66,{-5.804708F,0.168834999F,2.22410011F}}
}};
constexpr std::array<SightGeometryTriangle,6> r53_s2_triangles{{
    {3,{9,10,11}},
    {10,{27,28,29}},
    {24,{57,59,58}},
    {25,{59,60,58}},
    {26,{59,61,60}},
    {29,{64,65,66}}
}};
constexpr std::array<SightGeometrySurface,2> r53_surfaces{{
    {0,235,150,"mc/mtl_weapon_m1garand_gold",r53_s0_ranges,r53_s0_vertices,r53_s0_triangles},
    {2,249,166,"mc/mtl_weapon_m1garand_gold",r53_s2_ranges,r53_s2_vertices,r53_s2_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,4> r53_front{{
    {0,149,1.0F},
    {0,150,1.0F},
    {0,151,1.0F},
    {0,152,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,4> r53_rear{{
    {2,27,1.0F},
    {2,64,1.0F},
    {2,10,1.0F},
    {2,59,1.0F}
}};
const SightGeometryRecipe r53_recipe=[] {
    SightGeometryRecipe value{"transferred-zombie_m1garand_upgraded-mesh-v1","viewmodel_zombie_m1garand_rifle_up",r53_names,r53_parents,r53_binds,7,0,5,r53_surfaces,{6,SightGeometryReferenceMethod::weighted_centroid,r53_front},{6,SightGeometryReferenceMethod::aperture_circle_yz,r53_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,5> r54_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,4> r54_parents{{
    1,
    2,
    3,
    4
}};
constexpr std::array<SightGeometryBone,5> r54_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-7.42869663F,-0.894531012F,-0.0654459F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{0.357589006F,4.37017584F,-0.274828911F},2.0F},
    {{0.113010041F,0.113010041F,-0.698019326F,0.698019326F},{-0.0580599308F,-1.81135499F,0.993530273F},1.99999094F},
    {{0.0F,0.0F,0.0F,1.0F},{36.7492218F,0.0678640008F,-0.294047356F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r54_s1_ranges{{
    {0,4882,0,4404}
}};
constexpr std::array<SightGeometryVertex,11> r54_s1_vertices{{
    {235,{27.752079F,-0.0394329987F,3.58259678F}},
    {236,{27.1477737F,-0.0233089998F,3.67789745F}},
    {237,{27.752079F,0.0394299999F,3.58259678F}},
    {238,{27.1477737F,0.0233009998F,3.67789745F}},
    {1070,{6.71400452F,-0.491616011F,3.65767956F}},
    {1071,{6.71400452F,-0.110132001F,3.65767956F}},
    {1072,{6.77240944F,-0.491616011F,3.65594387F}},
    {1073,{6.77240944F,-0.110132001F,3.65594387F}},
    {1075,{6.70612526F,-4.99999987e-06F,3.52846432F}},
    {1078,{6.71400452F,0.491605997F,3.65767956F}},
    {1080,{6.71400452F,0.110122003F,3.65767956F}}
}};
constexpr std::array<SightGeometryTriangle,5> r54_s1_triangles{{
    {195,{235,236,237}},
    {196,{236,238,237}},
    {765,{1070,1071,1072}},
    {766,{1071,1073,1072}},
    {771,{1075,1078,1080}}
}};
constexpr std::array<SightGeometrySurface,1> r54_surfaces{{
    {1,4882,4404,"mc/mtl_weapon_mp_mg42",r54_s1_ranges,r54_s1_vertices,r54_s1_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r54_front{{
    {1,236,0.5F},
    {1,238,0.5F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r54_rear{{
    {1,1071,0.5F},
    {1,1080,0.5F}
}};
const SightGeometryRecipe r54_recipe=[] {
    SightGeometryRecipe value{"transferred-zombie_mg42-mesh-v1","viewmodel_zombie_mg42_mg",r54_names,r54_parents,r54_binds,3,0,4,r54_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r54_front},{0,SightGeometryReferenceMethod::weighted_centroid,r54_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,5> r55_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,4> r55_parents{{
    1,
    2,
    3,
    4
}};
constexpr std::array<SightGeometryBone,5> r55_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-7.42869663F,-0.894531012F,-0.0654459F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{0.357589006F,4.37017584F,-0.274828911F},2.0F},
    {{0.113010041F,0.113010041F,-0.698019326F,0.698019326F},{-0.0580599308F,-1.81135499F,0.993530273F},1.99999094F},
    {{0.0F,0.0F,0.0F,1.0F},{36.7492218F,0.0678640008F,-0.294047356F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r55_s1_ranges{{
    {0,4882,0,4404}
}};
constexpr std::array<SightGeometryVertex,11> r55_s1_vertices{{
    {235,{27.752079F,-0.0394329987F,3.58259678F}},
    {236,{27.1477737F,-0.0233089998F,3.67789745F}},
    {237,{27.752079F,0.0394299999F,3.58259678F}},
    {238,{27.1477737F,0.0233009998F,3.67789745F}},
    {1070,{6.71400452F,-0.491616011F,3.65767956F}},
    {1071,{6.71400452F,-0.110132001F,3.65767956F}},
    {1072,{6.77240944F,-0.491616011F,3.65594387F}},
    {1073,{6.77240944F,-0.110132001F,3.65594387F}},
    {1075,{6.70612526F,-4.99999987e-06F,3.52846432F}},
    {1078,{6.71400452F,0.491605997F,3.65767956F}},
    {1080,{6.71400452F,0.110122003F,3.65767956F}}
}};
constexpr std::array<SightGeometryTriangle,5> r55_s1_triangles{{
    {195,{235,236,237}},
    {196,{236,238,237}},
    {765,{1070,1071,1072}},
    {766,{1071,1073,1072}},
    {771,{1075,1078,1080}}
}};
constexpr std::array<SightGeometrySurface,1> r55_surfaces{{
    {1,4882,4404,"mc/mtl_weapon_mp_mg42",r55_s1_ranges,r55_s1_vertices,r55_s1_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r55_front{{
    {1,236,0.5F},
    {1,238,0.5F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r55_rear{{
    {1,1071,0.5F},
    {1,1080,0.5F}
}};
const SightGeometryRecipe r55_recipe=[] {
    SightGeometryRecipe value{"transferred-zombie_mg42_upgraded-mesh-v1","viewmodel_zombie_mg42_mg_up",r55_names,r55_parents,r55_binds,5,0,4,r55_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r55_front},{0,SightGeometryReferenceMethod::weighted_centroid,r55_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,5> r56_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,4> r56_parents{{
    1,
    2,
    3,
    4
}};
constexpr std::array<SightGeometryBone,5> r56_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-2.53235793F,1.25252604F,1.000512F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{4.20703793F,0.408818007F,-0.275402993F},2.0F},
    {{-0.294351012F,-0.28305918F,-0.693868816F,0.593096733F},{-2.45448399F,-0.341722012F,1.1221F},2.00003457F},
    {{0.0F,0.0F,0.0F,1.0F},{15.5053186F,0.482452989F,0.912301004F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r56_s0_ranges{{
    {0,3362,0,3029}
}};
constexpr std::array<SightGeometryVertex,13> r56_s0_vertices{{
    {143,{14.1847248F,0.541499972F,1.88795304F}},
    {144,{14.4411421F,0.531361997F,2.12501192F}},
    {145,{14.1847248F,0.51888603F,2.06960607F}},
    {161,{14.4411421F,0.421882004F,1.88795304F}},
    {162,{14.1847248F,0.468075007F,2.06960607F}},
    {163,{14.4411421F,0.455597997F,2.12501192F}},
    {1995,{-1.00056696F,0.648835003F,2.13707495F}},
    {1996,{-0.996999979F,0.659381986F,2.14851189F}},
    {1997,{-0.916701019F,0.630083025F,2.14962196F}},
    {1998,{-0.916701019F,0.651965022F,2.1610589F}},
    {2016,{-0.916701019F,0.358366013F,2.0445509F}},
    {2017,{-1.00056696F,0.339612991F,2.13707495F}},
    {2018,{-0.916701019F,0.358366013F,2.14962196F}}
}};
constexpr std::array<SightGeometryTriangle,5> r56_s0_triangles{{
    {76,{143,144,145}},
    {82,{161,162,163}},
    {1678,{1995,1996,1997}},
    {1679,{1996,1998,1997}},
    {1689,{2016,2018,2017}}
}};
constexpr std::array<SightGeometrySurface,1> r56_surfaces{{
    {0,3362,3030,"mc/mtl_weapon_mp_mp40",r56_s0_ranges,r56_s0_vertices,r56_s0_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r56_front{{
    {0,144,1.0F},
    {0,163,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r56_rear{{
    {0,1997,1.0F},
    {0,2018,1.0F}
}};
const SightGeometryRecipe r56_recipe=[] {
    SightGeometryRecipe value{"transferred-zombie_mp40-mesh-v1","viewmodel_zombie_mp40_smg",r56_names,r56_parents,r56_binds,3,0,4,r56_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r56_front},{0,SightGeometryReferenceMethod::weighted_centroid,r56_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,5> r57_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,4> r57_parents{{
    1,
    2,
    3,
    4
}};
constexpr std::array<SightGeometryBone,5> r57_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-2.53235793F,1.25252604F,1.000512F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{4.20703793F,0.408818007F,-0.275402993F},2.0F},
    {{-0.294351012F,-0.28305918F,-0.693868816F,0.593096733F},{-2.45448399F,-0.341722012F,1.1221F},2.00003457F},
    {{0.0F,0.0F,0.0F,1.0F},{15.5053186F,0.482452989F,0.912301004F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r57_s0_ranges{{
    {0,3362,0,3029}
}};
constexpr std::array<SightGeometryVertex,13> r57_s0_vertices{{
    {143,{14.1847248F,0.541499972F,1.88795304F}},
    {144,{14.4411421F,0.531361997F,2.12501192F}},
    {145,{14.1847248F,0.51888603F,2.06960607F}},
    {161,{14.4411421F,0.421882004F,1.88795304F}},
    {162,{14.1847248F,0.468075007F,2.06960607F}},
    {163,{14.4411421F,0.455597997F,2.12501192F}},
    {1995,{-1.00056696F,0.648835003F,2.13707495F}},
    {1996,{-0.996999979F,0.659381986F,2.14851189F}},
    {1997,{-0.916701019F,0.630083025F,2.14962196F}},
    {1998,{-0.916701019F,0.651965022F,2.1610589F}},
    {2016,{-0.916701019F,0.358366013F,2.0445509F}},
    {2017,{-1.00056696F,0.339612991F,2.13707495F}},
    {2018,{-0.916701019F,0.358366013F,2.14962196F}}
}};
constexpr std::array<SightGeometryTriangle,5> r57_s0_triangles{{
    {76,{143,144,145}},
    {82,{161,162,163}},
    {1678,{1995,1996,1997}},
    {1679,{1996,1998,1997}},
    {1689,{2016,2018,2017}}
}};
constexpr std::array<SightGeometrySurface,1> r57_surfaces{{
    {0,3362,3030,"mc/mtl_weapon_mp40_gold",r57_s0_ranges,r57_s0_vertices,r57_s0_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r57_front{{
    {0,144,1.0F},
    {0,163,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r57_rear{{
    {0,1997,1.0F},
    {0,2018,1.0F}
}};
const SightGeometryRecipe r57_recipe=[] {
    SightGeometryRecipe value{"transferred-zombie_mp40_upgraded-mesh-v1","viewmodel_zombie_mp40_smg_up",r57_names,r57_parents,r57_binds,5,0,4,r57_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r57_front},{0,SightGeometryReferenceMethod::weighted_centroid,r57_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,6> r58_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_brass",
    "tag_flash",
    "tag_iron_sights"
}};
constexpr std::array<std::uint8_t,5> r58_parents{{
    1,
    2,
    3,
    4,
    5
}};
constexpr std::array<SightGeometryBone,6> r58_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{4.69045305F,0.134476006F,3.20595121F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{6.63946009F,0.0150119998F,1.83560097F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{3.43127966F,-0.480022013F,3.52051497F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{23.6595592F,-0.012104F,1.99778092F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{6.29964113F,-0.309505999F,4.25149012F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r58_s0_ranges{{
    {0,3402,0,3140}
}};
constexpr std::array<SightGeometryVertex,9> r58_s0_vertices{{
    {12,{22.5668221F,-0.0102329999F,4.7354002F}},
    {13,{22.1788673F,-0.0102519998F,4.77380514F}},
    {14,{22.2072163F,-0.00105600001F,4.75656128F}},
    {222,{5.13178301F,0.210339993F,4.76731014F}},
    {223,{5.13138914F,0.064586997F,4.76731014F}},
    {224,{5.12438154F,0.196836993F,4.74774218F}},
    {228,{5.13178301F,-0.236669004F,4.76731014F}},
    {229,{5.12438154F,-0.223168001F,4.74774218F}},
    {230,{5.13138914F,-0.0909959972F,4.76731014F}}
}};
constexpr std::array<SightGeometryTriangle,3> r58_s0_triangles{{
    {4,{12,13,14}},
    {91,{222,223,224}},
    {93,{228,229,230}}
}};
constexpr std::array<SightGeometrySurface,1> r58_surfaces{{
    {0,3402,3140,"mc/mtl_weapon_mp_mp44",r58_s0_ranges,r58_s0_vertices,r58_s0_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r58_front{{
    {0,13,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r58_rear{{
    {0,223,1.0F},
    {0,230,1.0F}
}};
const SightGeometryRecipe r58_recipe=[] {
    SightGeometryRecipe value{"zombie_stg44-reviewed-root-owned-irons-v1","viewmodel_zombie_mp44_lmg",r58_names,r58_parents,r58_binds,4,0,4,r58_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r58_front},{0,SightGeometryReferenceMethod::weighted_centroid,r58_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,6> r59_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_brass",
    "tag_flash",
    "tag_iron_sights"
}};
constexpr std::array<std::uint8_t,5> r59_parents{{
    1,
    2,
    3,
    4,
    5
}};
constexpr std::array<SightGeometryBone,6> r59_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{4.69045305F,0.134476006F,3.20595121F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{6.63946009F,0.0150119998F,1.83560097F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{3.43127966F,-0.480022013F,3.52051497F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{23.6595592F,-0.012104F,1.99778092F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{6.29964113F,-0.309505999F,4.25149012F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r59_s0_ranges{{
    {0,3402,0,3140}
}};
constexpr std::array<SightGeometryVertex,9> r59_s0_vertices{{
    {12,{22.5668221F,-0.0102329999F,4.7354002F}},
    {13,{22.1788673F,-0.0102519998F,4.77380514F}},
    {14,{22.2072163F,-0.00105600001F,4.75656128F}},
    {222,{5.13178301F,0.210339993F,4.76731014F}},
    {223,{5.13138914F,0.064586997F,4.76731014F}},
    {224,{5.12438154F,0.196836993F,4.74774218F}},
    {228,{5.13178301F,-0.236669004F,4.76731014F}},
    {229,{5.12438154F,-0.223168001F,4.74774218F}},
    {230,{5.13138914F,-0.0909959972F,4.76731014F}}
}};
constexpr std::array<SightGeometryTriangle,3> r59_s0_triangles{{
    {4,{12,13,14}},
    {91,{222,223,224}},
    {93,{228,229,230}}
}};
constexpr std::array<SightGeometrySurface,1> r59_surfaces{{
    {0,3402,3140,"mc/mtl_weapon_mp_mp44",r59_s0_ranges,r59_s0_vertices,r59_s0_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r59_front{{
    {0,13,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r59_rear{{
    {0,223,1.0F},
    {0,230,1.0F}
}};
const SightGeometryRecipe r59_recipe=[] {
    SightGeometryRecipe value{"zombie_stg44_upgraded-reviewed-root-owned-irons-v1","viewmodel_zombie_mp44_lmg_up",r59_names,r59_parents,r59_binds,5,0,4,r59_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r59_front},{0,SightGeometryReferenceMethod::weighted_centroid,r59_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,7> r60_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "j_release",
    "tag_brass",
    "tag_flash",
    "tag_iron_sights"
}};
constexpr std::array<std::uint8_t,6> r60_parents{{
    1,
    2,
    3,
    4,
    5,
    6
}};
constexpr std::array<SightGeometryBone,7> r60_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{0.145243004F,-0.907276988F,1.80724895F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{2.16286397F,0.00130899996F,0.558384001F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{1.90295804F,0.076820001F,-0.303932011F},2.0F},
    {{-0.189458907F,-0.233954892F,-0.600146472F,0.74108094F},{1.61995995F,-0.341722012F,2.32218599F},1.99998736F},
    {{0.0F,0.0F,0.0F,1.0F},{16.3198547F,0.076820001F,1.52387404F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{13.7902651F,0.00130899996F,2.68176293F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r60_s0_ranges{{
    {0,4221,0,4366}
}};
constexpr std::array<SightGeometryVertex,6> r60_s0_vertices{{
    {0,{-1.23352098F,0.0507929996F,2.87958193F}},
    {1,{-1.23082101F,0.0507929996F,2.88228202F}},
    {2,{-1.23082101F,0.0480940007F,2.87958193F}},
    {6,{-1.23082101F,-0.0507929996F,2.88228202F}},
    {7,{-1.23352098F,-0.0507929996F,2.87958193F}},
    {8,{-1.23082101F,-0.0480940007F,2.87958193F}}
}};
constexpr std::array<SightGeometryTriangle,2> r60_s0_triangles{{
    {0,{0,1,2}},
    {2,{6,7,8}}
}};
constexpr std::array<SightGeometryRigidRange,1> r60_s4_ranges{{
    {6,468,0,398}
}};
constexpr std::array<SightGeometryVertex,11> r60_s4_vertices{{
    {410,{13.6875782F,0.0407470018F,2.91121197F}},
    {411,{13.9061785F,0.0407470018F,2.91121197F}},
    {412,{13.6875782F,-0.0407470018F,2.91121197F}},
    {413,{13.9061785F,-0.0407470018F,2.91121197F}},
    {414,{13.6032791F,0.0407470018F,2.5175221F}},
    {415,{13.6875782F,0.0407470018F,2.91121197F}},
    {416,{13.6032791F,-0.0407470018F,2.5175221F}},
    {417,{13.6875782F,-0.0407470018F,2.91121197F}},
    {419,{13.9061785F,-0.0407470018F,2.91121197F}},
    {420,{13.6032791F,-0.0407470018F,2.5175221F}},
    {421,{13.9932795F,-0.0407470018F,2.5175221F}}
}};
constexpr std::array<SightGeometryTriangle,5> r60_s4_triangles{{
    {362,{410,411,412}},
    {363,{411,413,412}},
    {364,{414,415,416}},
    {365,{415,417,416}},
    {367,{419,421,420}}
}};
constexpr std::array<SightGeometrySurface,2> r60_surfaces{{
    {0,4221,4366,"mc/mtl_weapon_mp_ppsh41",r60_s0_ranges,r60_s0_vertices,r60_s0_triangles},
    {4,468,398,"mc/mtl_weapon_mp_ppsh41",r60_s4_ranges,r60_s4_vertices,r60_s4_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,4> r60_front{{
    {4,412,1.0F},
    {4,413,1.0F},
    {4,416,1.0F},
    {4,421,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r60_rear{{
    {0,2,1.0F},
    {0,8,1.0F}
}};
const SightGeometryRecipe r60_recipe=[] {
    SightGeometryRecipe value{"transferred-zombie_ppsh-mesh-v1","viewmodel_zombie_ppsh_smg",r60_names,r60_parents,r60_binds,5,0,5,r60_surfaces,{6,SightGeometryReferenceMethod::weighted_centroid,r60_front},{0,SightGeometryReferenceMethod::weighted_centroid,r60_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,7> r61_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "j_release",
    "tag_brass",
    "tag_flash",
    "tag_iron_sights"
}};
constexpr std::array<std::uint8_t,6> r61_parents{{
    1,
    2,
    3,
    4,
    5,
    6
}};
constexpr std::array<SightGeometryBone,7> r61_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{0.145243004F,-0.907276988F,1.80724895F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{2.16286397F,0.00130899996F,0.558384001F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{1.90295804F,0.076820001F,-0.303932011F},2.0F},
    {{-0.189458907F,-0.233954892F,-0.600146472F,0.74108094F},{1.61995995F,-0.341722012F,2.32218599F},1.99998736F},
    {{0.0F,0.0F,0.0F,1.0F},{16.3198547F,0.076820001F,1.52387404F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{13.7902651F,0.00130899996F,2.68176293F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r61_s0_ranges{{
    {0,4221,0,4366}
}};
constexpr std::array<SightGeometryVertex,6> r61_s0_vertices{{
    {0,{-1.23352098F,0.0507929996F,2.87958193F}},
    {1,{-1.23082101F,0.0507929996F,2.88228202F}},
    {2,{-1.23082101F,0.0480940007F,2.87958193F}},
    {6,{-1.23082101F,-0.0507929996F,2.88228202F}},
    {7,{-1.23352098F,-0.0507929996F,2.87958193F}},
    {8,{-1.23082101F,-0.0480940007F,2.87958193F}}
}};
constexpr std::array<SightGeometryTriangle,2> r61_s0_triangles{{
    {0,{0,1,2}},
    {2,{6,7,8}}
}};
constexpr std::array<SightGeometryRigidRange,1> r61_s4_ranges{{
    {6,468,0,398}
}};
constexpr std::array<SightGeometryVertex,11> r61_s4_vertices{{
    {410,{13.6875782F,0.0407470018F,2.91121197F}},
    {411,{13.9061785F,0.0407470018F,2.91121197F}},
    {412,{13.6875782F,-0.0407470018F,2.91121197F}},
    {413,{13.9061785F,-0.0407470018F,2.91121197F}},
    {414,{13.6032791F,0.0407470018F,2.5175221F}},
    {415,{13.6875782F,0.0407470018F,2.91121197F}},
    {416,{13.6032791F,-0.0407470018F,2.5175221F}},
    {417,{13.6875782F,-0.0407470018F,2.91121197F}},
    {419,{13.9061785F,-0.0407470018F,2.91121197F}},
    {420,{13.6032791F,-0.0407470018F,2.5175221F}},
    {421,{13.9932795F,-0.0407470018F,2.5175221F}}
}};
constexpr std::array<SightGeometryTriangle,5> r61_s4_triangles{{
    {362,{410,411,412}},
    {363,{411,413,412}},
    {364,{414,415,416}},
    {365,{415,417,416}},
    {367,{419,421,420}}
}};
constexpr std::array<SightGeometrySurface,2> r61_surfaces{{
    {0,4221,4366,"mc/mtl_weapon_ppsh41_gold",r61_s0_ranges,r61_s0_vertices,r61_s0_triangles},
    {4,468,398,"mc/mtl_weapon_ppsh41_gold",r61_s4_ranges,r61_s4_vertices,r61_s4_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,4> r61_front{{
    {4,412,1.0F},
    {4,413,1.0F},
    {4,416,1.0F},
    {4,421,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r61_rear{{
    {0,2,1.0F},
    {0,8,1.0F}
}};
const SightGeometryRecipe r61_recipe=[] {
    SightGeometryRecipe value{"transferred-zombie_ppsh_upgraded-mesh-v1","viewmodel_zombie_ppsh_smg_up",r61_names,r61_parents,r61_binds,7,0,5,r61_surfaces,{6,SightGeometryReferenceMethod::weighted_centroid,r61_front},{0,SightGeometryReferenceMethod::weighted_centroid,r61_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,5> r62_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,4> r62_parents{{
    1,
    2,
    3,
    4
}};
constexpr std::array<SightGeometryBone,5> r62_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{1.30412006F,-0.912230015F,2.11571288F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{3.46322989F,0.126403004F,-0.144166946F},2.0F},
    {{-0.214300975F,-0.200140387F,-0.698690772F,0.652577281F},{3.81253481F,-0.904689014F,1.83832395F},1.99998593F},
    {{0.0F,0.0F,0.0F,1.0F},{14.3775959F,0.0504349992F,1.82262993F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r62_s3_ranges{{
    {0,4633,0,3805}
}};
constexpr std::array<SightGeometryVertex,59> r62_s3_vertices{{
    {24,{14.9879951F,0.0622380003F,2.94018793F}},
    {25,{14.938695F,0.119801998F,2.95097685F}},
    {26,{14.9876614F,0.177893996F,2.94080687F}},
    {3734,{-5.86262417F,-0.00315999985F,3.13725281F}},
    {3735,{-5.86249352F,-0.00402899832F,3.14690495F}},
    {3736,{-5.90241337F,-0.0302839987F,3.13026905F}},
    {3737,{-5.9021492F,-0.0332869999F,3.14704394F}},
    {3738,{-5.86278439F,0.00227700174F,3.12792397F}},
    {3739,{-5.86262417F,-0.00315999985F,3.13725281F}},
    {3740,{-5.90269756F,-0.0207970031F,3.11399198F}},
    {3741,{-5.90241337F,-0.0302839987F,3.13026905F}},
    {3742,{-5.86292553F,0.0106920004F,3.12055302F}},
    {3743,{-5.86278439F,0.00227700174F,3.12792397F}},
    {3744,{-5.90294075F,-0.00608300045F,3.10110283F}},
    {3745,{-5.90269756F,-0.0207970031F,3.11399198F}},
    {3746,{-5.86303425F,0.0212500002F,3.11584997F}},
    {3747,{-5.86292553F,0.0106920004F,3.12055302F}},
    {3748,{-5.90313148F,0.0124160014F,3.09286404F}},
    {3749,{-5.90294075F,-0.00608300045F,3.10110283F}},
    {3750,{-5.86309719F,0.03266F,3.11429882F}},
    {3751,{-5.86303425F,0.0212500002F,3.11584997F}},
    {3752,{-5.90324116F,0.0324200019F,3.09014416F}},
    {3753,{-5.90313148F,0.0124160014F,3.09286404F}},
    {3754,{-5.86276627F,0.0693830028F,3.14619398F}},
    {3755,{-5.86287594F,0.0687820017F,3.13753104F}},
    {3756,{-5.90264225F,0.0981969982F,3.14576912F}},
    {3757,{-5.90285873F,0.0956690013F,3.1307559F}},
    {3758,{-5.90327454F,0.0533360019F,3.09302187F}},
    {3759,{-5.86311531F,0.0445789993F,3.11593914F}},
    {3760,{-5.90324116F,0.0324200019F,3.09014416F}},
    {3762,{-5.90321445F,0.0717540011F,3.10140395F}},
    {3763,{-5.86308193F,0.055094F,3.12072515F}},
    {3764,{-5.90327454F,0.0533360019F,3.09302187F}},
    {3766,{-5.9030714F,0.0863410011F,3.11440611F}},
    {3767,{-5.86300087F,0.0634360015F,3.12816F}},
    {3768,{-5.90321445F,0.0717540011F,3.10140395F}},
    {3774,{-5.86240864F,0.0614940003F,3.167979F}},
    {3775,{-5.86256409F,0.068690002F,3.15829086F}},
    {3776,{-5.90200424F,0.0840720013F,3.18478298F}},
    {3777,{-5.90231133F,0.0954920053F,3.16692686F}},
    {3782,{-5.86209679F,0.0342690013F,3.18100691F}},
    {3783,{-5.86214447F,0.0427719988F,3.17982388F}},
    {3784,{-5.90149593F,0.0354110003F,3.20662212F}},
    {3785,{-5.90157986F,0.0501350015F,3.20455885F}},
    {3786,{-5.86226463F,0.0548049994F,3.17500997F}},
    {3787,{-5.86240864F,0.0614940003F,3.167979F}},
    {3788,{-5.90171432F,0.0705979988F,3.19640112F}},
    {3789,{-5.90200424F,0.0840720013F,3.18478298F}},
    {3794,{-5.90161514F,-0.0169480033F,3.18640614F}},
    {3795,{-5.86219597F,0.00509399921F,3.16888285F}},
    {3796,{-5.90149498F,-0.00125699863F,3.19819903F}},
    {3797,{-5.86209488F,0.0134130009F,3.17618084F}},
    {3798,{-5.90149498F,-0.00125699863F,3.19819903F}},
    {3799,{-5.86209488F,0.0134130009F,3.17618084F}},
    {3800,{-5.90145588F,0.0113020018F,3.20391512F}},
    {3801,{-5.86207104F,0.0206330009F,3.17946696F}},
    {3807,{-5.8623085F,-0.0032619983F,3.15803909F}},
    {3808,{-5.9021492F,-0.0332869999F,3.14704394F}},
    {3809,{-5.90186596F,-0.0304610021F,3.16644287F}}
}};
constexpr std::array<SightGeometryTriangle,27> r62_s3_triangles{{
    {8,{24,25,26}},
    {3001,{3734,3735,3736}},
    {3002,{3735,3737,3736}},
    {3003,{3738,3739,3740}},
    {3004,{3739,3741,3740}},
    {3005,{3742,3743,3744}},
    {3006,{3743,3745,3744}},
    {3007,{3746,3747,3748}},
    {3008,{3747,3749,3748}},
    {3009,{3750,3751,3752}},
    {3010,{3751,3753,3752}},
    {3011,{3754,3755,3756}},
    {3012,{3755,3757,3756}},
    {3013,{3758,3759,3760}},
    {3015,{3762,3763,3764}},
    {3017,{3766,3767,3768}},
    {3021,{3774,3775,3776}},
    {3022,{3775,3777,3776}},
    {3025,{3782,3783,3784}},
    {3026,{3783,3785,3784}},
    {3027,{3786,3787,3788}},
    {3028,{3787,3789,3788}},
    {3031,{3794,3795,3796}},
    {3032,{3795,3797,3796}},
    {3033,{3798,3799,3800}},
    {3034,{3799,3801,3800}},
    {3038,{3807,3809,3808}}
}};
constexpr std::array<SightGeometrySurface,1> r62_surfaces{{
    {3,4633,3806,"mc/mtl_weapon_mp_thompson",r62_s3_ranges,r62_s3_vertices,r62_s3_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r62_front{{
    {3,25,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,20> r62_rear{{
    {3,3736,0.0500000007F},
    {3,3737,0.0500000007F},
    {3,3740,0.0500000007F},
    {3,3744,0.0500000007F},
    {3,3748,0.0500000007F},
    {3,3752,0.0500000007F},
    {3,3758,0.0500000007F},
    {3,3762,0.0500000007F},
    {3,3766,0.0500000007F},
    {3,3757,0.0500000007F},
    {3,3756,0.0500000007F},
    {3,3777,0.0500000007F},
    {3,3776,0.0500000007F},
    {3,3788,0.0500000007F},
    {3,3785,0.0500000007F},
    {3,3784,0.0500000007F},
    {3,3800,0.0500000007F},
    {3,3796,0.0500000007F},
    {3,3794,0.0500000007F},
    {3,3809,0.0500000007F}
}};
const SightGeometryRecipe r62_recipe=[] {
    SightGeometryRecipe value{"transferred-zombie_thompson-mesh-v1","viewmodel_zombie_thompson_smg",r62_names,r62_parents,r62_binds,4,0,4,r62_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r62_front},{0,SightGeometryReferenceMethod::weighted_centroid,r62_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,5> r63_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,4> r63_parents{{
    1,
    2,
    3,
    4
}};
constexpr std::array<SightGeometryBone,5> r63_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{1.30412006F,-0.912230015F,2.11571288F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{3.46322989F,0.126403004F,-0.144166946F},2.0F},
    {{-0.214300975F,-0.200140387F,-0.698690772F,0.652577281F},{3.81253481F,-0.904689014F,1.83832395F},1.99998593F},
    {{0.0F,0.0F,0.0F,1.0F},{14.3775959F,0.0504349992F,1.82262993F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r63_s3_ranges{{
    {0,4633,0,3805}
}};
constexpr std::array<SightGeometryVertex,59> r63_s3_vertices{{
    {24,{14.9879951F,0.0622380003F,2.94018793F}},
    {25,{14.938695F,0.119801998F,2.95097685F}},
    {26,{14.9876614F,0.177893996F,2.94080687F}},
    {3734,{-5.86262417F,-0.00315999985F,3.13725281F}},
    {3735,{-5.86249352F,-0.00402899832F,3.14690495F}},
    {3736,{-5.90241337F,-0.0302839987F,3.13026905F}},
    {3737,{-5.9021492F,-0.0332869999F,3.14704394F}},
    {3738,{-5.86278439F,0.00227700174F,3.12792397F}},
    {3739,{-5.86262417F,-0.00315999985F,3.13725281F}},
    {3740,{-5.90269756F,-0.0207970031F,3.11399198F}},
    {3741,{-5.90241337F,-0.0302839987F,3.13026905F}},
    {3742,{-5.86292553F,0.0106920004F,3.12055302F}},
    {3743,{-5.86278439F,0.00227700174F,3.12792397F}},
    {3744,{-5.90294075F,-0.00608300045F,3.10110283F}},
    {3745,{-5.90269756F,-0.0207970031F,3.11399198F}},
    {3746,{-5.86303425F,0.0212500002F,3.11584997F}},
    {3747,{-5.86292553F,0.0106920004F,3.12055302F}},
    {3748,{-5.90313148F,0.0124160014F,3.09286404F}},
    {3749,{-5.90294075F,-0.00608300045F,3.10110283F}},
    {3750,{-5.86309719F,0.03266F,3.11429882F}},
    {3751,{-5.86303425F,0.0212500002F,3.11584997F}},
    {3752,{-5.90324116F,0.0324200019F,3.09014416F}},
    {3753,{-5.90313148F,0.0124160014F,3.09286404F}},
    {3754,{-5.86276627F,0.0693830028F,3.14619398F}},
    {3755,{-5.86287594F,0.0687820017F,3.13753104F}},
    {3756,{-5.90264225F,0.0981969982F,3.14576912F}},
    {3757,{-5.90285873F,0.0956690013F,3.1307559F}},
    {3758,{-5.90327454F,0.0533360019F,3.09302187F}},
    {3759,{-5.86311531F,0.0445789993F,3.11593914F}},
    {3760,{-5.90324116F,0.0324200019F,3.09014416F}},
    {3762,{-5.90321445F,0.0717540011F,3.10140395F}},
    {3763,{-5.86308193F,0.055094F,3.12072515F}},
    {3764,{-5.90327454F,0.0533360019F,3.09302187F}},
    {3766,{-5.9030714F,0.0863410011F,3.11440611F}},
    {3767,{-5.86300087F,0.0634360015F,3.12816F}},
    {3768,{-5.90321445F,0.0717540011F,3.10140395F}},
    {3774,{-5.86240864F,0.0614940003F,3.167979F}},
    {3775,{-5.86256409F,0.068690002F,3.15829086F}},
    {3776,{-5.90200424F,0.0840720013F,3.18478298F}},
    {3777,{-5.90231133F,0.0954920053F,3.16692686F}},
    {3782,{-5.86209679F,0.0342690013F,3.18100691F}},
    {3783,{-5.86214447F,0.0427719988F,3.17982388F}},
    {3784,{-5.90149593F,0.0354110003F,3.20662212F}},
    {3785,{-5.90157986F,0.0501350015F,3.20455885F}},
    {3786,{-5.86226463F,0.0548049994F,3.17500997F}},
    {3787,{-5.86240864F,0.0614940003F,3.167979F}},
    {3788,{-5.90171432F,0.0705979988F,3.19640112F}},
    {3789,{-5.90200424F,0.0840720013F,3.18478298F}},
    {3794,{-5.90161514F,-0.0169480033F,3.18640614F}},
    {3795,{-5.86219597F,0.00509399921F,3.16888285F}},
    {3796,{-5.90149498F,-0.00125699863F,3.19819903F}},
    {3797,{-5.86209488F,0.0134130009F,3.17618084F}},
    {3798,{-5.90149498F,-0.00125699863F,3.19819903F}},
    {3799,{-5.86209488F,0.0134130009F,3.17618084F}},
    {3800,{-5.90145588F,0.0113020018F,3.20391512F}},
    {3801,{-5.86207104F,0.0206330009F,3.17946696F}},
    {3807,{-5.8623085F,-0.0032619983F,3.15803909F}},
    {3808,{-5.9021492F,-0.0332869999F,3.14704394F}},
    {3809,{-5.90186596F,-0.0304610021F,3.16644287F}}
}};
constexpr std::array<SightGeometryTriangle,27> r63_s3_triangles{{
    {8,{24,25,26}},
    {3001,{3734,3735,3736}},
    {3002,{3735,3737,3736}},
    {3003,{3738,3739,3740}},
    {3004,{3739,3741,3740}},
    {3005,{3742,3743,3744}},
    {3006,{3743,3745,3744}},
    {3007,{3746,3747,3748}},
    {3008,{3747,3749,3748}},
    {3009,{3750,3751,3752}},
    {3010,{3751,3753,3752}},
    {3011,{3754,3755,3756}},
    {3012,{3755,3757,3756}},
    {3013,{3758,3759,3760}},
    {3015,{3762,3763,3764}},
    {3017,{3766,3767,3768}},
    {3021,{3774,3775,3776}},
    {3022,{3775,3777,3776}},
    {3025,{3782,3783,3784}},
    {3026,{3783,3785,3784}},
    {3027,{3786,3787,3788}},
    {3028,{3787,3789,3788}},
    {3031,{3794,3795,3796}},
    {3032,{3795,3797,3796}},
    {3033,{3798,3799,3800}},
    {3034,{3799,3801,3800}},
    {3038,{3807,3809,3808}}
}};
constexpr std::array<SightGeometrySurface,1> r63_surfaces{{
    {3,4633,3806,"mc/mtl_weapon_thompson_gold",r63_s3_ranges,r63_s3_vertices,r63_s3_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,1> r63_front{{
    {3,25,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,20> r63_rear{{
    {3,3736,0.0500000007F},
    {3,3737,0.0500000007F},
    {3,3740,0.0500000007F},
    {3,3744,0.0500000007F},
    {3,3748,0.0500000007F},
    {3,3752,0.0500000007F},
    {3,3758,0.0500000007F},
    {3,3762,0.0500000007F},
    {3,3766,0.0500000007F},
    {3,3757,0.0500000007F},
    {3,3756,0.0500000007F},
    {3,3777,0.0500000007F},
    {3,3776,0.0500000007F},
    {3,3788,0.0500000007F},
    {3,3785,0.0500000007F},
    {3,3784,0.0500000007F},
    {3,3800,0.0500000007F},
    {3,3796,0.0500000007F},
    {3,3794,0.0500000007F},
    {3,3809,0.0500000007F}
}};
const SightGeometryRecipe r63_recipe=[] {
    SightGeometryRecipe value{"transferred-zombie_thompson_upgraded-mesh-v1","viewmodel_zombie_thompson_smg_up",r63_names,r63_parents,r63_binds,5,0,4,r63_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r63_front},{0,SightGeometryReferenceMethod::weighted_centroid,r63_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,6> r64_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "ring_jnt",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,5> r64_parents{{
    1,
    2,
    3,
    4,
    5
}};
constexpr std::array<SightGeometryBone,6> r64_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-1.46550095F,-0.620103002F,1.75070906F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{2.35108995F,2.12277389F,1.51647103F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{12.4434404F,0.671782017F,1.51981103F},2.0F},
    {{-0.267403185F,-0.238319039F,-0.697012246F,0.621173739F},{-4.5152688F,-0.85709703F,1.85917199F},2.00003338F},
    {{0.0F,0.0F,0.0F,1.0F},{15.6997442F,0.0F,1.53522801F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r64_s0_ranges{{
    {0,4280,0,4602}
}};
constexpr std::array<SightGeometryVertex,12> r64_s0_vertices{{
    {1558,{-6.29599714F,-0.103128001F,2.59756994F}},
    {1559,{-6.29594278F,-0.162726F,2.47375393F}},
    {1560,{-6.36723804F,-0.10328F,2.59756994F}},
    {1561,{-6.36718512F,-0.162789002F,2.47375989F}},
    {1619,{-6.36701488F,-0.247302994F,2.24263501F}},
    {1626,{-6.36707401F,-0.200270995F,2.47374797F}},
    {1628,{-6.36702013F,-0.259959996F,2.59756994F}},
    {1629,{-6.36694288F,-0.316128999F,2.60262394F}},
    {1752,{12.7205887F,-0.195023999F,2.57976508F}},
    {1753,{12.6226006F,-0.195084006F,2.60204411F}},
    {1754,{12.7205496F,-0.165920004F,2.57976508F}},
    {1755,{12.6225615F,-0.165979996F,2.60204411F}}
}};
constexpr std::array<SightGeometryTriangle,6> r64_s0_triangles{{
    {1646,{1558,1559,1560}},
    {1647,{1559,1561,1560}},
    {1713,{1626,1628,1619}},
    {1714,{1619,1628,1629}},
    {1818,{1752,1753,1754}},
    {1819,{1753,1755,1754}}
}};
constexpr std::array<SightGeometrySurface,1> r64_surfaces{{
    {0,4280,4602,"mc/mtl_weapon_mp_type100_metal",r64_s0_ranges,r64_s0_vertices,r64_s0_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r64_front{{
    {0,1753,1.0F},
    {0,1755,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r64_rear{{
    {0,1560,1.0F},
    {0,1628,1.0F}
}};
const SightGeometryRecipe r64_recipe=[] {
    SightGeometryRecipe value{"transferred-zombie_type100_smg-mesh-v1","viewmodel_zombie_type100_smg",r64_names,r64_parents,r64_binds,4,0,5,r64_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r64_front},{0,SightGeometryReferenceMethod::weighted_centroid,r64_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,6> r65_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "ring_jnt",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,5> r65_parents{{
    1,
    2,
    3,
    4,
    5
}};
constexpr std::array<SightGeometryBone,6> r65_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-1.46550095F,-0.620103002F,1.75070906F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{2.35108995F,2.12277389F,1.51647103F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{12.4434404F,0.671782017F,1.51981103F},2.0F},
    {{-0.267403185F,-0.238319039F,-0.697012246F,0.621173739F},{-4.5152688F,-0.85709703F,1.85917199F},2.00003338F},
    {{0.0F,0.0F,0.0F,1.0F},{15.6997442F,0.0F,1.53522801F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r65_s0_ranges{{
    {0,4280,0,4602}
}};
constexpr std::array<SightGeometryVertex,12> r65_s0_vertices{{
    {1558,{-6.29599714F,-0.103128001F,2.59756994F}},
    {1559,{-6.29594278F,-0.162726F,2.47375393F}},
    {1560,{-6.36723804F,-0.10328F,2.59756994F}},
    {1561,{-6.36718512F,-0.162789002F,2.47375989F}},
    {1619,{-6.36701488F,-0.247302994F,2.24263501F}},
    {1626,{-6.36707401F,-0.200270995F,2.47374797F}},
    {1628,{-6.36702013F,-0.259959996F,2.59756994F}},
    {1629,{-6.36694288F,-0.316128999F,2.60262394F}},
    {1752,{12.7205887F,-0.195023999F,2.57976508F}},
    {1753,{12.6226006F,-0.195084006F,2.60204411F}},
    {1754,{12.7205496F,-0.165920004F,2.57976508F}},
    {1755,{12.6225615F,-0.165979996F,2.60204411F}}
}};
constexpr std::array<SightGeometryTriangle,6> r65_s0_triangles{{
    {1646,{1558,1559,1560}},
    {1647,{1559,1561,1560}},
    {1713,{1626,1628,1619}},
    {1714,{1619,1628,1629}},
    {1818,{1752,1753,1754}},
    {1819,{1753,1755,1754}}
}};
constexpr std::array<SightGeometrySurface,1> r65_surfaces{{
    {0,4280,4602,"mc/mtl_weapon_type100_gold",r65_s0_ranges,r65_s0_vertices,r65_s0_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r65_front{{
    {0,1753,1.0F},
    {0,1755,1.0F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r65_rear{{
    {0,1560,1.0F},
    {0,1628,1.0F}
}};
const SightGeometryRecipe r65_recipe=[] {
    SightGeometryRecipe value{"transferred-zombie_type100_smg_upgraded-mesh-v1","viewmodel_zombie_type100_smg_up",r65_names,r65_parents,r65_binds,6,0,5,r65_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r65_front},{0,SightGeometryReferenceMethod::weighted_centroid,r65_rear},0ULL,SightGeometryBasisKind::iron_sight};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,27> r66_names{{
    "j_gun",
    "ammo01_jnt",
    "ammo02_jnt",
    "ammo03_jnt",
    "ammo04_jnt",
    "ammo05_jnt",
    "ammo06_jnt",
    "ammo07_jnt",
    "ammo08_jnt",
    "ammo09_jnt",
    "ammo10_jnt",
    "ammo11_jnt",
    "ammo12_jnt",
    "ammo13_jnt",
    "ammo14_jnt",
    "ammo15_jnt",
    "ammo16_jnt",
    "ammo17_jnt",
    "ammo18_jnt",
    "ammo19_jnt",
    "ammo20_jnt",
    "j_bolt",
    "j_clip",
    "tag_bipod",
    "tag_brass",
    "tag_flash",
    "tag_top"
}};
constexpr std::array<std::uint8_t,26> r66_parents{{
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8,
    9,
    10,
    11,
    12,
    13,
    14,
    15,
    16,
    17,
    18,
    19,
    20,
    21,
    22,
    23,
    24,
    25,
    26
}};
constexpr std::array<SightGeometryBone,27> r66_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,0.927002013F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,1.52200198F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,2.11700201F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,2.71200204F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,3.30700207F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,3.9020021F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,4.49700212F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,5.09200191F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,5.68700218F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,6.28200197F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,6.87700176F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,7.47200203F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,8.0670023F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,8.66200161F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,9.25700188F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,9.85200214F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,10.4470024F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,11.0420027F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,11.637002F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,12.2320023F,1.67187607F},1.99995995F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{2.22591019F,-1.48468602F,1.33532596F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{4.54192734F,1.35894799F,1.82284701F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{24.5133324F,0.0F,0.338861018F},2.0F},
    {{-0.296182126F,-0.159855947F,-0.775719464F,0.533829749F},{0.0901520252F,-0.933917999F,1.12210107F},2.00001478F},
    {{0.0F,0.0F,0.0F,1.0F},{29.1933041F,0.0F,0.309920013F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{6.845397F,0.0F,2.34534597F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r66_s0_ranges{{
    {0,1863,0,1538}
}};
constexpr std::array<SightGeometryVertex,20> r66_s0_vertices{{
    {57,{23.7654266F,-0.93512702F,-0.0500799716F}},
    {58,{25.3082409F,-1.01217401F,0.337262005F}},
    {59,{23.7654266F,-1.01217401F,0.337262005F}},
    {60,{25.3082409F,-0.93512702F,0.724604011F}},
    {61,{23.7654266F,-0.93512702F,0.724604011F}},
    {73,{23.7654266F,0.93512702F,0.724604011F}},
    {74,{25.3082409F,1.01217401F,0.337262005F}},
    {75,{23.7654266F,1.01217401F,0.337262005F}},
    {76,{25.3082409F,0.93512702F,-0.0500799716F}},
    {77,{23.7654266F,0.93512702F,-0.0500799716F}},
    {483,{29.5741825F,0.776376009F,0.01567702F}},
    {484,{29.5065689F,0.838842988F,-0.0101979971F}},
    {485,{29.5741825F,0.840342999F,0.337262005F}},
    {486,{29.5065689F,0.907957971F,0.337262005F}},
    {487,{29.5741825F,0.776376009F,0.658846974F}},
    {499,{29.5741825F,-0.776376009F,0.658846974F}},
    {500,{29.5065689F,-0.838842988F,0.684722006F}},
    {501,{29.5741825F,-0.840342999F,0.337262005F}},
    {502,{29.5065689F,-0.907957971F,0.337262005F}},
    {503,{29.5741825F,-0.776376009F,0.0156760216F}}
}};
constexpr std::array<SightGeometryTriangle,12> r66_s0_triangles{{
    {41,{57,59,58}},
    {42,{58,59,60}},
    {43,{59,61,60}},
    {57,{73,75,74}},
    {58,{74,75,76}},
    {59,{75,77,76}},
    {361,{483,485,484}},
    {362,{485,486,484}},
    {363,{486,485,487}},
    {377,{500,499,501}},
    {378,{502,500,501}},
    {379,{502,501,503}}
}};
constexpr std::array<SightGeometrySurface,1> r66_surfaces{{
    {0,1863,1538,"mc/mtl_usa_30cal_m1919",r66_s0_ranges,r66_s0_vertices,r66_s0_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r66_front{{
    {0,485,0.5F},
    {0,501,0.5F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r66_rear{{
    {0,59,0.5F},
    {0,75,0.5F}
}};
const SightGeometryRecipe r66_recipe=[] {
    SightGeometryRecipe value{"30cal_bipod-physical-barrel-axis-candidate-v1","viewmodel_usa_30cal_bipod_lmg",r66_names,r66_parents,r66_binds,25,0,25,r66_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r66_front},{0,SightGeometryReferenceMethod::weighted_centroid,r66_rear},0ULL,SightGeometryBasisKind::barrel_axis};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,26> r67_names{{
    "j_gun",
    "ammo01_jnt",
    "ammo02_jnt",
    "ammo03_jnt",
    "ammo04_jnt",
    "ammo05_jnt",
    "ammo06_jnt",
    "ammo07_jnt",
    "ammo08_jnt",
    "ammo09_jnt",
    "ammo10_jnt",
    "ammo11_jnt",
    "ammo12_jnt",
    "ammo13_jnt",
    "ammo14_jnt",
    "ammo15_jnt",
    "ammo16_jnt",
    "ammo17_jnt",
    "ammo18_jnt",
    "ammo19_jnt",
    "ammo20_jnt",
    "j_bolt",
    "j_clip",
    "tag_brass",
    "tag_flash",
    "tag_top"
}};
constexpr std::array<std::uint8_t,25> r67_parents{{
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8,
    9,
    10,
    11,
    12,
    13,
    14,
    15,
    16,
    17,
    18,
    19,
    20,
    21,
    22,
    23,
    24,
    25
}};
constexpr std::array<SightGeometryBone,26> r67_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,0.927002013F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,1.52200198F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,2.11700201F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,2.71200204F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,3.30700207F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,3.9020021F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,4.49700212F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,5.09200191F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,5.68700218F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,6.28200197F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,6.87700176F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,7.47200203F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,8.0670023F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,8.66200161F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,9.25700188F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,9.85200214F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,10.4470024F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,11.0420027F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,11.637002F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,12.2320023F,1.67187607F},1.99995995F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{2.22591019F,-1.48468602F,1.33532596F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{4.54192734F,1.35894799F,1.82284701F},2.0F},
    {{-0.296182126F,-0.159855947F,-0.775719464F,0.533829749F},{0.0901520252F,-0.933917999F,1.12210107F},2.00001478F},
    {{0.0F,0.0F,0.0F,1.0F},{29.1933041F,0.0F,0.309920013F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{6.845397F,0.0F,2.34534597F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r67_s2_ranges{{
    {0,1808,0,1492}
}};
constexpr std::array<SightGeometryVertex,20> r67_s2_vertices{{
    {401,{29.5741825F,0.776376009F,0.01567702F}},
    {402,{29.5065689F,0.838842988F,-0.0101979971F}},
    {403,{29.5741825F,0.840342999F,0.337262005F}},
    {404,{29.5065689F,0.907957971F,0.337262005F}},
    {405,{29.5741825F,0.776376009F,0.658846974F}},
    {417,{29.5741825F,-0.776376009F,0.658846974F}},
    {418,{29.5065689F,-0.838842988F,0.684722006F}},
    {419,{29.5741825F,-0.840342999F,0.337262005F}},
    {420,{29.5065689F,-0.907957971F,0.337262005F}},
    {421,{29.5741825F,-0.776376009F,0.0156760216F}},
    {436,{26.511961F,0.857885003F,-0.0180849731F}},
    {437,{7.8634634F,0.928569019F,0.337262005F}},
    {438,{26.511961F,0.928569019F,0.337262005F}},
    {439,{7.8634634F,0.857886016F,0.692610025F}},
    {440,{26.511961F,0.857886016F,0.692610025F}},
    {452,{26.511961F,-0.857886016F,0.692610025F}},
    {453,{7.8634634F,-0.928568006F,0.337262005F}},
    {454,{26.511961F,-0.928568006F,0.337262005F}},
    {455,{7.8634634F,-0.857885003F,-0.0180859864F}},
    {456,{26.511961F,-0.857885003F,-0.0180859864F}}
}};
constexpr std::array<SightGeometryTriangle,12> r67_s2_triangles{{
    {291,{401,403,402}},
    {292,{403,404,402}},
    {293,{404,403,405}},
    {307,{418,417,419}},
    {308,{420,418,419}},
    {309,{420,419,421}},
    {324,{438,437,436}},
    {325,{439,437,438}},
    {326,{440,439,438}},
    {340,{454,453,452}},
    {341,{455,453,454}},
    {342,{456,455,454}}
}};
constexpr std::array<SightGeometrySurface,1> r67_surfaces{{
    {2,1808,1492,"mc/mtl_usa_30cal_m1919_wet",r67_s2_ranges,r67_s2_vertices,r67_s2_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r67_front{{
    {2,403,0.5F},
    {2,419,0.5F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r67_rear{{
    {2,438,0.5F},
    {2,454,0.5F}
}};
const SightGeometryRecipe r67_recipe=[] {
    SightGeometryRecipe value{"30cal_wet-physical-barrel-axis-candidate-v1","viewmodel_usa_30cal_lmg_wet",r67_names,r67_parents,r67_binds,24,0,24,r67_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r67_front},{0,SightGeometryReferenceMethod::weighted_centroid,r67_rear},0ULL,SightGeometryBasisKind::barrel_axis};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,9> r68_names{{
    "j_gun",
    "tag_barrels",
    "tag_brass",
    "tag_lever",
    "tag_extractor",
    "tag_flash",
    "tag_grip",
    "tag_shell1",
    "tag_shell2"
}};
constexpr std::array<std::uint8_t,8> r68_parents{{
    1,
    2,
    3,
    3,
    4,
    5,
    3,
    4
}};
constexpr std::array<SightGeometryBone,9> r68_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{2.62091112F,0.00678599998F,0.0488209724F},2.0F},
    {{-0.244819477F,-0.154881433F,-0.651509166F,0.701162755F},{-0.929933548F,-0.630271971F,1.12210083F},1.99996352F},
    {{0.0F,-0.0954924151F,0.0F,0.995422244F},{-1.52936459F,0.0F,1.7459569F},2.00003147F},
    {{0.0F,0.0F,0.0F,1.0F},{-0.00319385529F,0.00119099999F,1.18299389F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{35.3361778F,0.0F,1.25504494F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{7.70002127F,-0.000765999779F,0.224842787F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{1.32945395F,-0.623425007F,1.1901319F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{1.32945395F,0.627842009F,1.18909788F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r68_s4_ranges{{
    {1,1687,0,1814}
}};
constexpr std::array<SightGeometryVertex,40> r68_s4_vertices{{
    {406,{10.4907207F,-0.64093101F,1.45597386F}},
    {407,{0.0894398689F,-0.705326021F,1.64117098F}},
    {408,{10.4907207F,-0.597347975F,1.45940399F}},
    {409,{0.0894398689F,-0.632411003F,1.64690995F}},
    {410,{10.4907207F,-0.553764999F,1.45597386F}},
    {446,{10.4907207F,-0.553764999F,0.905629873F}},
    {447,{0.0894398689F,-0.559495986F,0.720432997F}},
    {448,{10.4907207F,-0.597347975F,0.902199984F}},
    {449,{0.0894398689F,-0.632411003F,0.714693785F}},
    {450,{10.4907207F,-0.64093101F,0.905629873F}},
    {474,{15.0739021F,-0.454136997F,0.653746843F}},
    {477,{10.6637659F,-0.554145992F,0.631978989F}},
    {479,{15.0739021F,-0.537613988F,0.647176981F}},
    {493,{10.6637659F,-0.640000999F,0.63873601F}},
    {502,{15.0739021F,-0.621092021F,0.653746843F}},
    {636,{10.6637659F,-0.554145992F,1.72962499F}},
    {643,{10.6637659F,-0.468291014F,1.72286892F}},
    {645,{15.0739021F,-0.621091008F,1.70785689F}},
    {649,{15.0739021F,-0.537613988F,1.7144258F}},
    {653,{15.0739021F,-0.454136997F,1.70785689F}},
    {1145,{10.4907207F,0.640614986F,1.45597386F}},
    {1146,{0.0894398689F,0.705009997F,1.64117098F}},
    {1147,{10.4907207F,0.597032011F,1.45940399F}},
    {1148,{0.0894398689F,0.632094979F,1.64690995F}},
    {1149,{10.4907207F,0.553448975F,1.45597386F}},
    {1185,{10.4907207F,0.553448975F,0.905629873F}},
    {1186,{0.0894398689F,0.559180021F,0.720432997F}},
    {1187,{10.4907207F,0.597032011F,0.902199984F}},
    {1188,{0.0894398689F,0.632094979F,0.714693785F}},
    {1189,{10.4907207F,0.640614986F,0.905629873F}},
    {1287,{10.6637659F,0.553830028F,0.631978989F}},
    {1289,{15.0739021F,0.453821003F,0.653746843F}},
    {1292,{10.6637659F,0.639684975F,0.63873601F}},
    {1293,{15.0739021F,0.537298024F,0.647176981F}},
    {1295,{15.0739021F,0.620775998F,0.653746843F}},
    {1419,{15.0739021F,0.620774984F,1.70785689F}},
    {1421,{10.6637659F,0.553830028F,1.72962499F}},
    {1426,{15.0739021F,0.537298024F,1.7144258F}},
    {1428,{10.6637659F,0.467974991F,1.72286797F}},
    {1433,{15.0739021F,0.453821003F,1.70785689F}}
}};
constexpr std::array<SightGeometryTriangle,24> r68_s4_triangles{{
    {368,{407,406,408}},
    {369,{409,407,408}},
    {370,{409,408,410}},
    {408,{447,446,448}},
    {409,{449,447,448}},
    {410,{449,448,450}},
    {441,{479,474,477}},
    {465,{479,477,493}},
    {481,{502,479,493}},
    {730,{649,636,643}},
    {731,{649,645,636}},
    {738,{653,649,643}},
    {1177,{1147,1145,1146}},
    {1178,{1148,1147,1146}},
    {1179,{1149,1147,1148}},
    {1217,{1187,1185,1186}},
    {1218,{1188,1187,1186}},
    {1219,{1189,1187,1188}},
    {1331,{1293,1287,1289}},
    {1332,{1292,1287,1293}},
    {1335,{1295,1292,1293}},
    {1557,{1426,1421,1419}},
    {1560,{1428,1421,1426}},
    {1569,{1433,1428,1426}}
}};
constexpr std::array<SightGeometrySurface,1> r68_surfaces{{
    {4,1687,1814,"mc/mtl_double_barrel",r68_s4_ranges,r68_s4_vertices,r68_s4_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,4> r68_front{{
    {4,479,0.25F},
    {4,649,0.25F},
    {4,1293,0.25F},
    {4,1426,0.25F}
}};
constexpr std::array<SightGeometryReferenceVertex,4> r68_rear{{
    {4,408,0.25F},
    {4,448,0.25F},
    {4,1147,0.25F},
    {4,1187,0.25F}
}};
const SightGeometryRecipe r68_recipe=[] {
    SightGeometryRecipe value{"doublebarrel_sawed_grip-physical-barrel-axis-candidate-v1","viewmodel_usa_double_barrel_sawed_off_grips",r68_names,r68_parents,r68_binds,8,0,5,r68_surfaces,{1,SightGeometryReferenceMethod::weighted_centroid,r68_front},{1,SightGeometryReferenceMethod::weighted_centroid,r68_rear},0ULL,SightGeometryBasisKind::barrel_axis};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,11> r69_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "j_hammer",
    "j_internals01",
    "j_internals02",
    "j_load",
    "j_pump",
    "j_ring",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,10> r69_parents{{
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8,
    9,
    10
}};
constexpr std::array<SightGeometryBone,11> r69_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-1.06923795F,-0.0334869996F,1.98790097F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{1.18568897F,37.0687523F,7.9510622F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.48178196F,-0.00129100005F,0.655438006F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{0.449611992F,-0.0907709971F,1.37115705F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.23345399F,-0.0204860009F,1.22250295F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.62664604F,-0.203063995F,-0.218970999F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{12.9955626F,0.0F,0.105162002F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{23.8593254F,0.0147510003F,-0.234695002F},2.0F},
    {{0.0F,0.0F,-0.707113862F,0.707113862F},{-0.342052013F,-0.902574003F,1.59456396F},1.99995995F},
    {{0.0F,0.0F,0.0F,1.0F},{29.3343544F,0.0F,1.81703699F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r69_s1_ranges{{
    {0,3463,0,3039}
}};
constexpr std::array<SightGeometryVertex,20> r69_s1_vertices{{
    {1309,{8.4725008F,0.622328997F,1.52180398F}},
    {1310,{2.95165706F,0.624495983F,1.51903605F}},
    {1311,{8.4725008F,0.639573991F,1.71193802F}},
    {1312,{2.95178509F,0.641785979F,1.71045601F}},
    {1313,{8.47249889F,0.613362014F,1.89470994F}},
    {1331,{8.4725008F,-0.580609977F,1.89470994F}},
    {1332,{2.95740199F,-0.582744002F,1.89454806F}},
    {1333,{8.47249794F,-0.606821001F,1.71193802F}},
    {1334,{2.95732093F,-0.609032989F,1.71053898F}},
    {1335,{8.4725008F,-0.58801502F,1.52180398F}},
    {1355,{23.5704136F,-0.580612004F,1.89471102F}},
    {1356,{8.4725008F,-0.580609977F,1.89470994F}},
    {1357,{23.5704174F,-0.606823027F,1.71193898F}},
    {1358,{8.47249794F,-0.606821001F,1.71193802F}},
    {1359,{23.5704136F,-0.588016987F,1.521806F}},
    {1377,{23.5704136F,0.62232703F,1.521806F}},
    {1378,{8.4725008F,0.622328997F,1.52180398F}},
    {1379,{23.5704136F,0.639572024F,1.71193898F}},
    {1380,{8.4725008F,0.639573991F,1.71193802F}},
    {1381,{23.5704155F,0.613361001F,1.89471102F}}
}};
constexpr std::array<SightGeometryTriangle,12> r69_s1_triangles{{
    {1001,{1310,1309,1311}},
    {1002,{1312,1310,1311}},
    {1003,{1312,1311,1313}},
    {1023,{1332,1331,1333}},
    {1024,{1334,1332,1333}},
    {1025,{1334,1333,1335}},
    {1045,{1356,1355,1357}},
    {1046,{1358,1356,1357}},
    {1047,{1358,1357,1359}},
    {1067,{1378,1377,1379}},
    {1068,{1380,1378,1379}},
    {1069,{1380,1379,1381}}
}};
constexpr std::array<SightGeometrySurface,1> r69_surfaces{{
    {1,3463,3040,"mc/mtl_weapon_trenchgun",r69_s1_ranges,r69_s1_vertices,r69_s1_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r69_front{{
    {1,1357,0.5F},
    {1,1379,0.5F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r69_rear{{
    {1,1311,0.5F},
    {1,1333,0.5F}
}};
const SightGeometryRecipe r69_recipe=[] {
    SightGeometryRecipe value{"shotgun-physical-barrel-axis-candidate-v1","viewmodel_usa_trenchgun_rifle",r69_names,r69_parents,r69_binds,7,0,10,r69_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r69_front},{0,SightGeometryReferenceMethod::weighted_centroid,r69_rear},0ULL,SightGeometryBasisKind::barrel_axis};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,26> r70_names{{
    "j_gun",
    "ammo01_jnt",
    "ammo02_jnt",
    "ammo03_jnt",
    "ammo04_jnt",
    "ammo05_jnt",
    "ammo06_jnt",
    "ammo07_jnt",
    "ammo08_jnt",
    "ammo09_jnt",
    "ammo10_jnt",
    "ammo11_jnt",
    "ammo12_jnt",
    "ammo13_jnt",
    "ammo14_jnt",
    "ammo15_jnt",
    "ammo16_jnt",
    "ammo17_jnt",
    "ammo18_jnt",
    "ammo19_jnt",
    "ammo20_jnt",
    "j_bolt",
    "j_clip",
    "tag_brass",
    "tag_flash",
    "tag_top"
}};
constexpr std::array<std::uint8_t,25> r70_parents{{
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8,
    9,
    10,
    11,
    12,
    13,
    14,
    15,
    16,
    17,
    18,
    19,
    20,
    21,
    22,
    23,
    24,
    25
}};
constexpr std::array<SightGeometryBone,26> r70_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,0.927002013F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,1.52200198F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,2.11700201F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,2.71200204F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,3.30700207F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,3.9020021F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,4.49700212F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,5.09200191F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,5.68700218F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,6.28200197F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,6.87700176F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,7.47200203F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,8.0670023F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,8.66200161F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,9.25700188F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,9.85200214F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,10.4470024F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,11.0420027F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,11.637002F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,12.2320023F,1.67187607F},1.99995995F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{2.22591019F,-1.48468602F,1.33532596F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{4.54192734F,1.35894799F,1.82284701F},2.0F},
    {{-0.296182126F,-0.159855947F,-0.775719464F,0.533829749F},{0.0901520252F,-0.933917999F,1.12210107F},2.00001478F},
    {{0.0F,0.0F,0.0F,1.0F},{29.1933041F,0.0F,0.309920013F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{6.845397F,0.0F,2.34534597F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r70_s1_ranges{{
    {0,1808,0,1492}
}};
constexpr std::array<SightGeometryVertex,20> r70_s1_vertices{{
    {401,{29.5741825F,0.776376009F,0.01567702F}},
    {402,{29.5065689F,0.838842988F,-0.0101979971F}},
    {403,{29.5741825F,0.840342999F,0.337262005F}},
    {404,{29.5065689F,0.907957971F,0.337262005F}},
    {405,{29.5741825F,0.776376009F,0.658846974F}},
    {417,{29.5741825F,-0.776376009F,0.658846974F}},
    {418,{29.5065689F,-0.838842988F,0.684722006F}},
    {419,{29.5741825F,-0.840342999F,0.337262005F}},
    {420,{29.5065689F,-0.907957971F,0.337262005F}},
    {421,{29.5741825F,-0.776376009F,0.0156760216F}},
    {436,{26.511961F,0.857885003F,-0.0180849731F}},
    {437,{7.8634634F,0.928569019F,0.337262005F}},
    {438,{26.511961F,0.928569019F,0.337262005F}},
    {439,{7.8634634F,0.857886016F,0.692610025F}},
    {440,{26.511961F,0.857886016F,0.692610025F}},
    {452,{26.511961F,-0.857886016F,0.692610025F}},
    {453,{7.8634634F,-0.928568006F,0.337262005F}},
    {454,{26.511961F,-0.928568006F,0.337262005F}},
    {455,{7.8634634F,-0.857885003F,-0.0180859864F}},
    {456,{26.511961F,-0.857885003F,-0.0180859864F}}
}};
constexpr std::array<SightGeometryTriangle,12> r70_s1_triangles{{
    {291,{401,403,402}},
    {292,{403,404,402}},
    {293,{404,403,405}},
    {307,{418,417,419}},
    {308,{420,418,419}},
    {309,{420,419,421}},
    {324,{438,437,436}},
    {325,{439,437,438}},
    {326,{440,439,438}},
    {340,{454,453,452}},
    {341,{455,453,454}},
    {342,{456,455,454}}
}};
constexpr std::array<SightGeometrySurface,1> r70_surfaces{{
    {1,1808,1492,"mc/mtl_weapon_mp_30cal_m1919",r70_s1_ranges,r70_s1_vertices,r70_s1_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r70_front{{
    {1,403,0.5F},
    {1,419,0.5F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r70_rear{{
    {1,438,0.5F},
    {1,454,0.5F}
}};
const SightGeometryRecipe r70_recipe=[] {
    SightGeometryRecipe value{"zombie_30cal-physical-barrel-axis-candidate-v1","viewmodel_zombie_30cal_lmg",r70_names,r70_parents,r70_binds,23,0,24,r70_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r70_front},{0,SightGeometryReferenceMethod::weighted_centroid,r70_rear},0ULL,SightGeometryBasisKind::barrel_axis};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,26> r71_names{{
    "j_gun",
    "ammo01_jnt",
    "ammo02_jnt",
    "ammo03_jnt",
    "ammo04_jnt",
    "ammo05_jnt",
    "ammo06_jnt",
    "ammo07_jnt",
    "ammo08_jnt",
    "ammo09_jnt",
    "ammo10_jnt",
    "ammo11_jnt",
    "ammo12_jnt",
    "ammo13_jnt",
    "ammo14_jnt",
    "ammo15_jnt",
    "ammo16_jnt",
    "ammo17_jnt",
    "ammo18_jnt",
    "ammo19_jnt",
    "ammo20_jnt",
    "j_bolt",
    "j_clip",
    "tag_brass",
    "tag_flash",
    "tag_top"
}};
constexpr std::array<std::uint8_t,25> r71_parents{{
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8,
    9,
    10,
    11,
    12,
    13,
    14,
    15,
    16,
    17,
    18,
    19,
    20,
    21,
    22,
    23,
    24,
    25
}};
constexpr std::array<SightGeometryBone,26> r71_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,0.927002013F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,1.52200198F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,2.11700201F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,2.71200204F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,3.30700207F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,3.9020021F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,4.49700212F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,5.09200191F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,5.68700218F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,6.28200197F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,6.87700176F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,7.47200203F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,8.0670023F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,8.66200161F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,9.25700188F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,9.85200214F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,10.4470024F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,11.0420027F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,11.637002F,1.67187607F},1.99995995F},
    {{0.0F,-0.707113862F,0.0F,0.707113862F},{4.31315804F,12.2320023F,1.67187607F},1.99995995F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{2.22591019F,-1.48468602F,1.33532596F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{4.54192734F,1.35894799F,1.82284701F},2.0F},
    {{-0.296182126F,-0.159855947F,-0.775719464F,0.533829749F},{0.0901520252F,-0.933917999F,1.12210107F},2.00001478F},
    {{0.0F,0.0F,0.0F,1.0F},{29.1933041F,0.0F,0.309920013F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{6.845397F,0.0F,2.34534597F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r71_s1_ranges{{
    {0,1808,0,1492}
}};
constexpr std::array<SightGeometryVertex,20> r71_s1_vertices{{
    {401,{29.5741825F,0.776376009F,0.01567702F}},
    {402,{29.5065689F,0.838842988F,-0.0101979971F}},
    {403,{29.5741825F,0.840342999F,0.337262005F}},
    {404,{29.5065689F,0.907957971F,0.337262005F}},
    {405,{29.5741825F,0.776376009F,0.658846974F}},
    {417,{29.5741825F,-0.776376009F,0.658846974F}},
    {418,{29.5065689F,-0.838842988F,0.684722006F}},
    {419,{29.5741825F,-0.840342999F,0.337262005F}},
    {420,{29.5065689F,-0.907957971F,0.337262005F}},
    {421,{29.5741825F,-0.776376009F,0.0156760216F}},
    {436,{26.511961F,0.857885003F,-0.0180849731F}},
    {437,{7.8634634F,0.928569019F,0.337262005F}},
    {438,{26.511961F,0.928569019F,0.337262005F}},
    {439,{7.8634634F,0.857886016F,0.692610025F}},
    {440,{26.511961F,0.857886016F,0.692610025F}},
    {452,{26.511961F,-0.857886016F,0.692610025F}},
    {453,{7.8634634F,-0.928568006F,0.337262005F}},
    {454,{26.511961F,-0.928568006F,0.337262005F}},
    {455,{7.8634634F,-0.857885003F,-0.0180859864F}},
    {456,{26.511961F,-0.857885003F,-0.0180859864F}}
}};
constexpr std::array<SightGeometryTriangle,12> r71_s1_triangles{{
    {291,{401,403,402}},
    {292,{403,404,402}},
    {293,{404,403,405}},
    {307,{418,417,419}},
    {308,{420,418,419}},
    {309,{420,419,421}},
    {324,{438,437,436}},
    {325,{439,437,438}},
    {326,{440,439,438}},
    {340,{454,453,452}},
    {341,{455,453,454}},
    {342,{456,455,454}}
}};
constexpr std::array<SightGeometrySurface,1> r71_surfaces{{
    {1,1808,1492,"mc/mtl_weapon_30cal_gold",r71_s1_ranges,r71_s1_vertices,r71_s1_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r71_front{{
    {1,403,0.5F},
    {1,419,0.5F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r71_rear{{
    {1,438,0.5F},
    {1,454,0.5F}
}};
const SightGeometryRecipe r71_recipe=[] {
    SightGeometryRecipe value{"zombie_30cal_upgraded-physical-barrel-axis-candidate-v1","viewmodel_zombie_30cal_lmg_up",r71_names,r71_parents,r71_binds,24,0,24,r71_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r71_front},{0,SightGeometryReferenceMethod::weighted_centroid,r71_rear},0ULL,SightGeometryBasisKind::barrel_axis};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,11> r72_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "j_hammer",
    "j_internals01",
    "j_internals02",
    "j_load",
    "j_pump",
    "j_ring",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,10> r72_parents{{
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8,
    9,
    10
}};
constexpr std::array<SightGeometryBone,11> r72_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-1.06923795F,-0.0334869996F,1.98790097F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{1.18568897F,37.0687523F,7.9510622F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.48178196F,-0.00129100005F,0.655438006F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{0.449611992F,-0.0907709971F,1.37115705F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.23345399F,-0.0204860009F,1.22250295F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.62664604F,-0.203063995F,-0.218970999F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{12.9955626F,0.0F,0.105162002F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{23.8593254F,0.0147510003F,-0.234695002F},2.0F},
    {{0.0F,0.0F,-0.707113862F,0.707113862F},{-0.342052013F,-0.902574003F,1.59456396F},1.99995995F},
    {{0.0F,0.0F,0.0F,1.0F},{29.3343544F,0.0F,1.81703699F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r72_s1_ranges{{
    {0,3912,0,3451}
}};
constexpr std::array<SightGeometryVertex,20> r72_s1_vertices{{
    {1605,{8.4725008F,0.622328997F,1.52180398F}},
    {1606,{2.95165706F,0.624495983F,1.51903605F}},
    {1607,{8.4725008F,0.639573991F,1.71193802F}},
    {1608,{2.95178509F,0.641785979F,1.71045601F}},
    {1609,{8.47249889F,0.613362014F,1.89470994F}},
    {1627,{8.4725008F,-0.580609977F,1.89470994F}},
    {1628,{2.95740199F,-0.582744002F,1.89454806F}},
    {1629,{8.47249794F,-0.606821001F,1.71193802F}},
    {1630,{2.95732093F,-0.609032989F,1.71053898F}},
    {1631,{8.4725008F,-0.58801502F,1.52180398F}},
    {1651,{23.5704136F,-0.580612004F,1.89471102F}},
    {1652,{8.4725008F,-0.580609977F,1.89470994F}},
    {1653,{23.5704174F,-0.606823027F,1.71193898F}},
    {1654,{8.47249794F,-0.606821001F,1.71193802F}},
    {1655,{23.5704136F,-0.588016987F,1.521806F}},
    {1673,{23.5704136F,0.62232703F,1.521806F}},
    {1674,{8.4725008F,0.622328997F,1.52180398F}},
    {1675,{23.5704136F,0.639572024F,1.71193898F}},
    {1676,{8.4725008F,0.639573991F,1.71193802F}},
    {1677,{23.5704155F,0.613361001F,1.89471102F}}
}};
constexpr std::array<SightGeometryTriangle,12> r72_s1_triangles{{
    {1253,{1606,1605,1607}},
    {1254,{1608,1606,1607}},
    {1255,{1608,1607,1609}},
    {1275,{1628,1627,1629}},
    {1276,{1630,1628,1629}},
    {1277,{1630,1629,1631}},
    {1297,{1652,1651,1653}},
    {1298,{1654,1652,1653}},
    {1299,{1654,1653,1655}},
    {1319,{1674,1673,1675}},
    {1320,{1676,1674,1675}},
    {1321,{1676,1675,1677}}
}};
constexpr std::array<SightGeometrySurface,1> r72_surfaces{{
    {1,3912,3452,"mc/mtl_weapon_mp_trenchgun",r72_s1_ranges,r72_s1_vertices,r72_s1_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r72_front{{
    {1,1653,0.5F},
    {1,1675,0.5F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r72_rear{{
    {1,1607,0.5F},
    {1,1629,0.5F}
}};
const SightGeometryRecipe r72_recipe=[] {
    SightGeometryRecipe value{"zombie_shotgun-physical-barrel-axis-candidate-v1","viewmodel_zombie_trenchgun_rifle",r72_names,r72_parents,r72_binds,6,0,10,r72_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r72_front},{0,SightGeometryReferenceMethod::weighted_centroid,r72_rear},0ULL,SightGeometryBasisKind::barrel_axis};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();

constexpr std::array<std::string_view,11> r73_names{{
    "j_gun",
    "j_bolt",
    "j_clip",
    "j_hammer",
    "j_internals01",
    "j_internals02",
    "j_load",
    "j_pump",
    "j_ring",
    "tag_brass",
    "tag_flash"
}};
constexpr std::array<std::uint8_t,10> r73_parents{{
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8,
    9,
    10
}};
constexpr std::array<SightGeometryBone,11> r73_binds{{
    {{0.0F,0.0F,0.0F,1.0F},{0.0F,0.0F,0.0F},2.0F},
    {{0.0F,-0.0102237007F,0.0F,0.999938965F},{-1.06923795F,-0.0334869996F,1.98790097F},2.00003505F},
    {{0.0F,0.0F,0.0F,1.0F},{1.18568897F,37.0687523F,7.9510622F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.48178196F,-0.00129100005F,0.655438006F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{0.449611992F,-0.0907709971F,1.37115705F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-1.23345399F,-0.0204860009F,1.22250295F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{-2.62664604F,-0.203063995F,-0.218970999F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{12.9955626F,0.0F,0.105162002F},2.0F},
    {{0.0F,0.0F,0.0F,1.0F},{23.8593254F,0.0147510003F,-0.234695002F},2.0F},
    {{0.0F,0.0F,-0.707113862F,0.707113862F},{-0.342052013F,-0.902574003F,1.59456396F},1.99995995F},
    {{0.0F,0.0F,0.0F,1.0F},{29.3343544F,0.0F,1.81703699F},2.0F}
}};
constexpr std::array<SightGeometryRigidRange,1> r73_s1_ranges{{
    {0,3912,0,3451}
}};
constexpr std::array<SightGeometryVertex,20> r73_s1_vertices{{
    {1605,{8.4725008F,0.622328997F,1.52180398F}},
    {1606,{2.95165706F,0.624495983F,1.51903605F}},
    {1607,{8.4725008F,0.639573991F,1.71193802F}},
    {1608,{2.95178509F,0.641785979F,1.71045601F}},
    {1609,{8.47249889F,0.613362014F,1.89470994F}},
    {1627,{8.4725008F,-0.580609977F,1.89470994F}},
    {1628,{2.95740199F,-0.582744002F,1.89454806F}},
    {1629,{8.47249794F,-0.606821001F,1.71193802F}},
    {1630,{2.95732093F,-0.609032989F,1.71053898F}},
    {1631,{8.4725008F,-0.58801502F,1.52180398F}},
    {1651,{23.5704136F,-0.580612004F,1.89471102F}},
    {1652,{8.4725008F,-0.580609977F,1.89470994F}},
    {1653,{23.5704174F,-0.606823027F,1.71193898F}},
    {1654,{8.47249794F,-0.606821001F,1.71193802F}},
    {1655,{23.5704136F,-0.588016987F,1.521806F}},
    {1673,{23.5704136F,0.62232703F,1.521806F}},
    {1674,{8.4725008F,0.622328997F,1.52180398F}},
    {1675,{23.5704136F,0.639572024F,1.71193898F}},
    {1676,{8.4725008F,0.639573991F,1.71193802F}},
    {1677,{23.5704155F,0.613361001F,1.89471102F}}
}};
constexpr std::array<SightGeometryTriangle,12> r73_s1_triangles{{
    {1253,{1606,1605,1607}},
    {1254,{1608,1606,1607}},
    {1255,{1608,1607,1609}},
    {1275,{1628,1627,1629}},
    {1276,{1630,1628,1629}},
    {1277,{1630,1629,1631}},
    {1297,{1652,1651,1653}},
    {1298,{1654,1652,1653}},
    {1299,{1654,1653,1655}},
    {1319,{1674,1673,1675}},
    {1320,{1676,1674,1675}},
    {1321,{1676,1675,1677}}
}};
constexpr std::array<SightGeometrySurface,1> r73_surfaces{{
    {1,3912,3452,"mc/mtl_weapon_trenchgun_gold",r73_s1_ranges,r73_s1_vertices,r73_s1_triangles}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r73_front{{
    {1,1653,0.5F},
    {1,1675,0.5F}
}};
constexpr std::array<SightGeometryReferenceVertex,2> r73_rear{{
    {1,1607,0.5F},
    {1,1629,0.5F}
}};
const SightGeometryRecipe r73_recipe=[] {
    SightGeometryRecipe value{"zombie_shotgun_upgraded-physical-barrel-axis-candidate-v1","viewmodel_zombie_trenchgun_rifle_up",r73_names,r73_parents,r73_binds,8,0,10,r73_surfaces,{0,SightGeometryReferenceMethod::weighted_centroid,r73_front},{0,SightGeometryReferenceMethod::weighted_centroid,r73_rear},0ULL,SightGeometryBasisKind::barrel_axis};
    // Pure digest of BAKED immutable witnesses only, never learned from native memory.
    value.fingerprint=sight_geometry_recipe_fingerprint(value);
    return value;
}();
const std::array<SightGeometryProfile,78> kProfiles{{
    {"fg42_bipod","viewmodel_ger_fg42_bipod_lmg",&r0_recipe},
    {"gewehr43","viewmodel_ger_g43_rifle",&r1_recipe},
    {"kar98k","viewmodel_ger_kar98_rifle",&r2_recipe},
    {"mg42_bipod","viewmodel_ger_mg42_bipod_mg",&r3_recipe},
    {"mp40","viewmodel_ger_mp40_smg",&r4_recipe},
    {"stg44","viewmodel_ger_mp44_lmg",&r5_recipe},
    {"walther","viewmodel_ger_walther_pistol",&r6_recipe},
    {"nambu","viewmodel_jap_nambu_pistol",&r7_recipe},
    {"nambu_wet","viewmodel_jap_nambu_pistol_wet",&r8_recipe},
    {"type100_smg","viewmodel_jap_type100_smg",&r9_recipe},
    {"type100_smg_wet","viewmodel_jap_type100_smg_wet",&r10_recipe},
    {"type99_lmg_bipod","viewmodel_jap_type99_bipod_lmg",&r11_recipe},
    {"type99_lmg_wet","viewmodel_jap_type99_lmg_wet",&r12_recipe},
    {"type99_rifle","viewmodel_jap_type99_rifle",&r13_recipe},
    {"type99_rifle_bayonet","viewmodel_jap_type99_rifle_bayonet",&r14_recipe},
    {"type99_rifle_bayonet_wet","viewmodel_jap_type99_rifle_bayonet_wet",&r15_recipe},
    {"type99_rifle_wet","viewmodel_jap_type99_rifle_wet",&r16_recipe},
    {"zombie_sw_357","viewmodel_mp_357magnum",&r17_recipe},
    {"sw_357","viewmodel_mp_357magnum",&r18_recipe},
    {"mosin_rifle","viewmodel_rus_mosinnagant_rifle",&r19_recipe},
    {"ppsh","viewmodel_rus_ppsh_smg",&r20_recipe},
    {"svt40","viewmodel_rus_svt40_rifle",&r21_recipe},
    {"tokarev","viewmodel_rus_tt30_pistol",&r22_recipe},
    {"bar_bipod","viewmodel_usa_bar_bipod_lmg",&r23_recipe},
    {"bar","viewmodel_usa_bar_lmg",&r24_recipe},
    {"colt","viewmodel_usa_colt45_pistol",&r25_recipe},
    {"zombie_colt","viewmodel_usa_colt45_pistol",&r25_recipe},
    {"colt_dirty_harry","viewmodel_usa_colt45_pistol",&r25_recipe},
    {"colt_wet","viewmodel_usa_colt45_pistol_wet",&r26_recipe},
    {"doublebarrel","viewmodel_usa_double_barrel",&r27_recipe},
    {"m1carbine","viewmodel_usa_m1carbine_rifle",&r28_recipe},
    {"m1carbine_wet","viewmodel_usa_m1carbine_rifle_wet",&r29_recipe},
    {"m1garand","viewmodel_usa_m1garand_rifle",&r30_recipe},
    {"m1garand_bayonet","viewmodel_usa_m1garand_rifle_bayonet",&r31_recipe},
    {"m1garand_gl","viewmodel_usa_m1garand_rifle_grenade_mount",&r32_recipe},
    {"m1garand_wet","viewmodel_usa_m1garand_rifle_wet",&r33_recipe},
    {"springfield","viewmodel_usa_springfield_rifle",&r34_recipe},
    {"thompson","viewmodel_usa_thompson_smg",&r35_recipe},
    {"thompson_wet","viewmodel_usa_thompson_smg_wet",&r36_recipe},
    {"zombie_sw_357_upgraded","viewmodel_zombie_357magnum_up",&r37_recipe},
    {"zombie_bar","viewmodel_zombie_bar_lmg",&r38_recipe},
    {"zombie_bar_upgraded","viewmodel_zombie_bar_lmg_up",&r39_recipe},
    {"zombie_colt","viewmodel_zombie_colt45_pistol",&r40_recipe},
    {"zombie_colt_upgraded","viewmodel_zombie_colt45_pistol_up",&r41_recipe},
    {"zombie_doublebarrel","viewmodel_zombie_db_shotgun",&r42_recipe},
    {"zombie_doublebarrel_upgraded","viewmodel_zombie_db_shotgun_up",&r43_recipe},
    {"zombie_fg42","viewmodel_zombie_fg42_lmg",&r44_recipe},
    {"zombie_fg42_upgraded","viewmodel_zombie_fg42_lmg_up",&r45_recipe},
    {"zombie_gewehr43","viewmodel_zombie_g43_rifle",&r46_recipe},
    {"zombie_gewehr43_upgraded","viewmodel_zombie_g43_rifle_up",&r47_recipe},
    {"zombie_kar98k","viewmodel_zombie_kar98k_rifle",&r48_recipe},
    {"zombie_kar98k_upgraded","viewmodel_zombie_kar98k_rifle_up",&r49_recipe},
    {"zombie_m1carbine","viewmodel_zombie_m1carbine_rifle",&r50_recipe},
    {"zombie_m1carbine_upgraded","viewmodel_zombie_m1carbine_rifle_up",&r51_recipe},
    {"zombie_m1garand","viewmodel_zombie_m1garand_rifle",&r52_recipe},
    {"m1garand_gl_zombie","viewmodel_zombie_m1garand_rifle",&r52_recipe},
    {"zombie_m1garand_upgraded","viewmodel_zombie_m1garand_rifle_up",&r53_recipe},
    {"m1garand_gl_zombie_upgraded","viewmodel_zombie_m1garand_rifle_up",&r53_recipe},
    {"zombie_mg42","viewmodel_zombie_mg42_mg",&r54_recipe},
    {"zombie_mg42_upgraded","viewmodel_zombie_mg42_mg_up",&r55_recipe},
    {"zombie_mp40","viewmodel_zombie_mp40_smg",&r56_recipe},
    {"zombie_mp40_upgraded","viewmodel_zombie_mp40_smg_up",&r57_recipe},
    {"zombie_stg44","viewmodel_zombie_mp44_lmg",&r58_recipe},
    {"zombie_stg44_upgraded","viewmodel_zombie_mp44_lmg_up",&r59_recipe},
    {"zombie_ppsh","viewmodel_zombie_ppsh_smg",&r60_recipe},
    {"zombie_ppsh_upgraded","viewmodel_zombie_ppsh_smg_up",&r61_recipe},
    {"zombie_thompson","viewmodel_zombie_thompson_smg",&r62_recipe},
    {"zombie_thompson_upgraded","viewmodel_zombie_thompson_smg_up",&r63_recipe},
    {"zombie_type100_smg","viewmodel_zombie_type100_smg",&r64_recipe},
    {"zombie_type100_smg_upgraded","viewmodel_zombie_type100_smg_up",&r65_recipe},
    {"30cal_bipod","viewmodel_usa_30cal_bipod_lmg",&r66_recipe},
    {"30cal_wet","viewmodel_usa_30cal_lmg_wet",&r67_recipe},
    {"doublebarrel_sawed_grip","viewmodel_usa_double_barrel_sawed_off_grips",&r68_recipe},
    {"shotgun","viewmodel_usa_trenchgun_rifle",&r69_recipe},
    {"zombie_30cal","viewmodel_zombie_30cal_lmg",&r70_recipe},
    {"zombie_30cal_upgraded","viewmodel_zombie_30cal_lmg_up",&r71_recipe},
    {"zombie_shotgun","viewmodel_zombie_trenchgun_rifle",&r72_recipe},
    {"zombie_shotgun_upgraded","viewmodel_zombie_trenchgun_rifle_up",&r73_recipe}
}};
}
std::span<const SightGeometryProfile> all_weapon_sight_geometry_profiles() noexcept { return kProfiles; }
}
