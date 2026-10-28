// SPDX-License-Identifier: GPL-3.0-only
// Immutable bank/geometry smoke checks only. Not native discharge or headset acceptance.
#include "all_weapon_sight_geometry_bank.hpp"
#include <array>
#include <cstdio>
#include <span>

namespace {
unsigned checks{},failures{};
void check(bool value,const char* message) {
    ++checks;
    if(!value){++failures;std::fprintf(stderr,"FAIL: %s\n",message);}
}
const wawvr::mod::SightGeometrySurface* surface_for(
    const wawvr::mod::SightGeometryRecipe& recipe,unsigned index) {
    for(const auto& surface:recipe.surfaces)if(surface.index==index)return &surface;
    return nullptr;
}
bool reference_backed(const wawvr::mod::SightGeometryRecipe& recipe,
    const wawvr::mod::SightGeometryReference& reference) {
    if(reference.owner_bone>=recipe.binds.size()||reference.vertices.empty()||
        reference.vertices.size()>wawvr::mod::kSightGeometryMaximumReferenceVertices)return false;
    for(const auto& selected:reference.vertices){
        const auto* surface=surface_for(recipe,selected.surface);
        if(!surface||selected.vertex>=surface->vertex_count||selected.weight<=0)return false;
        bool stored=false,owned=false,backed=false;
        for(const auto& vertex:surface->vertices)if(vertex.index==selected.vertex)stored=true;
        unsigned first{};
        for(const auto& range:surface->rigid_ranges){
            if(selected.vertex>=first&&selected.vertex-first<range.vertices&&
                range.bone==reference.owner_bone){
                owned=true;
                for(const auto& triangle:surface->triangles){
                    if(triangle.index<range.first_triangle||
                        triangle.index-range.first_triangle>=range.triangles)continue;
                    for(auto vertex:triangle.vertices)if(vertex==selected.vertex)backed=true;
                }
            }
            first+=range.vertices;
        }
        if(!stored||!owned||!backed)return false;
    }
    return true;
}
}

int main() {
    using namespace wawvr::mod;
    const auto profiles=all_weapon_sight_geometry_profiles();
    check(profiles.size()>=40&&profiles.size()<=256,"finite reviewed profile coverage bounds");
    std::size_t iron_profiles{},barrel_profiles{};
    for(std::size_t i=0;i<profiles.size();++i){
        const auto& profile=profiles[i];
        check(!profile.weapon.empty()&&!profile.model.empty()&&profile.recipe,"exact profile identity present");
        if(!profile.recipe)continue;
        const auto& recipe=*profile.recipe;
        check(sight_geometry_basis_kind_valid(recipe.basis_kind),"profile category is explicit and valid");
        if(recipe.basis_kind==SightGeometryBasisKind::iron_sight)++iron_profiles;
        if(recipe.basis_kind==SightGeometryBasisKind::barrel_axis)++barrel_profiles;
        check(profile.model==recipe.model,"profile model equals own recipe model");
        const auto digest=sight_geometry_recipe_fingerprint(recipe);
        check(digest!=0&&recipe.fingerprint!=0&&digest==recipe.fingerprint,"pure baked recipe digest is valid and exact");
        check(recipe.root_bone<recipe.binds.size()&&recipe.flash_bone<recipe.binds.size(),"root/flash are bounded actual model bones");
        if(recipe.root_bone<recipe.bone_names.size()&&recipe.flash_bone<recipe.bone_names.size()){
            check(recipe.bone_names[recipe.root_bone]=="j_gun","root is actual j_gun");
            check(recipe.bone_names[recipe.flash_bone]=="tag_flash","flash is actual tag_flash");
        }else check(false,"bone identity spans cover root/flash");
        check(reference_backed(recipe,recipe.front),"front reference is bounded owned and triangle-backed");
        check(reference_backed(recipe,recipe.rear),"rear reference is bounded owned and triangle-backed");
        check(find_sight_geometry_profile(profiles,profile.weapon,profile.model)==&profile,"exact profile lookup is unambiguous");
        for(std::size_t j=i+1;j<profiles.size();++j)
            check(profile.weapon!=profiles[j].weapon||profile.model!=profiles[j].model,"no duplicate exact weapon/model pair");
        const std::array<SightGeometryProfile,2> ambiguous{{profile,profile}};
        check(find_sight_geometry_profile(ambiguous,profile.weapon,profile.model)==nullptr,"ambiguous pair rejected instead of first-match");
        auto invalid=recipe;invalid.front.vertices={};
        check(sight_geometry_recipe_fingerprint(invalid)==0,"missing front witness rejected");
        invalid=recipe;invalid.rear.owner_bone=static_cast<std::uint8_t>(recipe.binds.size());
        check(sight_geometry_recipe_fingerprint(invalid)==0,"out-of-range rear owner rejected");
    }
    check(iron_profiles==70,"70 exact iron-sight profiles are retained");
    check(barrel_profiles==8,"eight barrel profiles are separate from iron-sight zeros");
    check(find_sight_geometry_profile(profiles,"","")==nullptr,"empty identity rejected");
    check(find_sight_geometry_profile(profiles,"not_a_stock_weapon","not_a_stock_model")==nullptr,"unknown exact identity rejected");
    check(find_sight_geometry_profile(profiles,"zombie_melee","viewmodel_usa_colt45_pistol")==nullptr,"non-aiming shared-model role excluded");
    std::printf("Immutable bank smoke: %u checks, %u failures, %zu profiles; no native/headset qualification claimed\n",checks,failures,profiles.size());
    return failures?1:0;
}
