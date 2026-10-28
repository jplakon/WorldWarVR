// SPDX-License-Identifier: GPL-3.0-only
#include "sight_geometry_root_alignment.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace wawvr::mod;using wawvr::xr::Basis3f;using wawvr::xr::Vec3f;
unsigned checks{};
void check(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
bool near(Vec3f a,Vec3f b,float t=.00003F){return std::abs(a.x-b.x)<t&&std::abs(a.y-b.y)<t&&std::abs(a.z-b.z)<t;}
Basis3f pitch(float degrees){const float a=degrees*.0174532925199433F,c=std::cos(a),s=std::sin(a);
    return {{c,0,s},{0,1,0},{-s,0,c}};}
Vec3f compose(Basis3f a,Vec3f p){return {a.forward.x*p.x+a.left.x*p.y+a.up.x*p.z,
    a.forward.y*p.x+a.left.y*p.y+a.up.y*p.z,a.forward.z*p.x+a.left.z*p.y+a.up.z*p.z};}
struct Fixture {
    // Gate fixtures are deliberately opaque synthetic parser proofs, not
    // production geometry. Actual proof creation is tested in basis_test.
    SightGeometryRecipe recipe{};
    SightGeometryProfile profile{};
    SightGeometryProof proof{};
    SightGeometryRootGate gate{};
    Fixture(){recipe.model="viewmodel_synthetic";recipe.fingerprint=12345;
        profile={"synthetic_weapon",recipe.model,&recipe};
        proof.profile=&profile;proof.recipe=&recipe;proof.definition=0x10000;proof.dobj=0x20000;proof.model=0x30000;
        proof.skeleton_timestamp=77;proof.fingerprint=recipe.fingerprint;proof.controller_generation=10;
        proof.publication_milliseconds=1000;proof.evaluated_root_basis=pitch(5);
        gate={&profile,&proof,proof.definition,proof.dobj,proof.model,10,1010,true,true,false,false,0,false,false,0};
    }
};
void positive(){
    Fixture f;BazookaViewmodelAlignment output{};
    check(sight_geometry_root_allowed(f.gate),"valid exact proof gate rejected");
    check(calculate_sight_geometry_root_alignment(f.gate,{3,4,5},pitch(0),{1,2,3},pitch(0),&output),"valid root correction rejected");
    check(std::abs(output.correction_degrees-5.F)<.0001F,"wrong bounded root correction");
    check(near(output.corrected_root_axis.forward,pitch(-5).forward),"wrong shortest-arc root correction");
    const Vec3f initial_offset{2,2,2};const auto pivot_offset=compose(output.corrected_root_axis,initial_offset);
    check(near({1+pivot_offset.x,2+pivot_offset.y,3+pivot_offset.z},output.corrected_root_origin),"correction translated grip pivot");
    for(int state:{0,5}){f.gate.native_weapon_state=state;check(sight_geometry_root_allowed(f.gate),"ready/firing gate rejected");}
    f.gate.current_tracking=false;f.gate.retained_render_only=true;f.gate.retained_same_owner=true;
    f.gate.retained_publication_milliseconds=760;
    check(sight_geometry_root_allowed(f.gate),"inclusive250ms same-owner render retention rejected");
    // Root render retention never extends an old shot geometry publication.
    f.proof.publication_milliseconds=860;check(sight_geometry_root_allowed(f.gate),"inclusive150ms source gate rejected");
    f.proof.publication_milliseconds=859;check(!sight_geometry_root_allowed(f.gate),"retention widened150ms geometry authority");
}
void negative(){
    for(unsigned n=0;n<25;++n){Fixture f;
        switch(n){case 0:f.gate.profile=nullptr;break;case 1:f.gate.proof=nullptr;break;case 2:f.profile.recipe=nullptr;break;
        case 3:f.gate.definition=0;break;case 4:f.gate.dobj=0;break;case 5:f.gate.model=0;break;
        case 6:f.gate.controller_generation=0;break;case 7:f.gate.now_milliseconds=0;break;
        case 8:f.gate.right_hand_owner=false;break;case 9:f.gate.transition_pending=true;break;case 10:f.gate.blocks_attack=true;break;
        case 11:f.gate.native_weapon_state=6;break;case 12:f.proof.definition++;break;case 13:f.proof.dobj++;break;
        case 14:f.proof.model++;break;case 15:f.proof.fingerprint++;break;case 16:f.proof.skeleton_timestamp=0;break;
        case 17:f.proof.controller_generation--;break;case 18:f.proof.publication_milliseconds=1011;break;
        case 19:f.proof.publication_milliseconds=859;break;case 20:f.gate.current_tracking=false;break;
        case 21:f.profile.model="different_model";break;case 22:f.profile.weapon={};break;
        case 23:f.recipe.fingerprint=0;break;case 24:f.proof.recipe=nullptr;break;}
        BazookaViewmodelAlignment out{{123,456,789},pitch(30),42};const auto original=out;
        check(!sight_geometry_root_allowed(f.gate),"mismatched/stale/blocking gate accepted");
        check(!calculate_sight_geometry_root_alignment(f.gate,{},{}, {},pitch(0),&out),"rejected gate corrected root");
        check(std::memcmp(&out,&original,sizeof(out))==0,"failed correction altered caller output");
    }
    for(unsigned n=0;n<5;++n){Fixture f;f.gate.current_tracking=false;f.gate.retained_render_only=true;
        f.gate.retained_same_owner=true;f.gate.retained_publication_milliseconds=900;
        switch(n){case 0:f.gate.retained_render_only=false;break;case 1:f.gate.retained_same_owner=false;break;
        case 2:f.gate.retained_publication_milliseconds=0;break;case 3:f.gate.retained_publication_milliseconds=1011;break;
        case 4:f.gate.retained_publication_milliseconds=759;break;}
        check(!sight_geometry_root_allowed(f.gate),"invalid retained render proof accepted");
    }
    for(unsigned n=0;n<5;++n){Fixture f;auto outer=pitch(0),target=pitch(0);Vec3f origin{3,4,5},grip{1,2,3};
        switch(n){case 0:f.proof.evaluated_root_basis=pitch(16);break;case 1:outer.up.z=-1;break;
        case 2:target.forward.x=2;break;case 3:origin.x=std::numeric_limits<float>::quiet_NaN();break;
        case 4:grip.z=std::numeric_limits<float>::infinity();break;}
        BazookaViewmodelAlignment out{{123,456,789},pitch(30),42};const auto original=out;
        check(!calculate_sight_geometry_root_alignment(f.gate,origin,outer,grip,target,&out),"invalid axis/oversized correction accepted");
        check(std::memcmp(&out,&original,sizeof(out))==0,"invalid math altered caller output");
    }
}
void basis_kinds(){Fixture f;check(f.recipe.basis_kind==SightGeometryBasisKind::iron_sight&&
    f.proof.basis_kind==SightGeometryBasisKind::iron_sight,"legacy gate proof kind default changed");
    f.recipe.basis_kind=SightGeometryBasisKind::barrel_axis;
    check(!sight_geometry_root_allowed(f.gate),"iron proof authorized barrel recipe render correction");
    f.proof.basis_kind=SightGeometryBasisKind::barrel_axis;
    check(sight_geometry_root_allowed(f.gate),"matching explicit barrel proof rejected");
    f.recipe.basis_kind=static_cast<SightGeometryBasisKind>(9);f.proof.basis_kind=f.recipe.basis_kind;
    check(!sight_geometry_root_allowed(f.gate),"matching invalid category authorized root correction");
}
}
int main(){try{positive();negative();basis_kinds();std::cout<<"generic sight root alignment: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
