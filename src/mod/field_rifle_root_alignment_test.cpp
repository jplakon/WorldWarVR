// SPDX-License-Identifier: GPL-3.0-only
#include "field_rifle_root_alignment.hpp"
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace {
using namespace wawvr::mod;
using wawvr::xr::Vec3f;using wawvr::xr::Basis3f;
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
Vec3f add(Vec3f a,Vec3f b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3f transform(Basis3f a,Vec3f p){return {a.forward.x*p.x+a.left.x*p.y+a.up.x*p.z,
    a.forward.y*p.x+a.left.y*p.y+a.up.y*p.z,a.forward.z*p.x+a.left.z*p.y+a.up.z*p.z};}
Basis3f compose(Basis3f a,Basis3f b){return {transform(a,b.forward),transform(a,b.left),transform(a,b.up)};}
bool near(Vec3f a,Vec3f b){return std::abs(a.x-b.x)<.0001F&&std::abs(a.y-b.y)<.0001F&&std::abs(a.z-b.z)<.0001F;}
Basis3f pitch(float d){float a=d*3.14159265358979323846F/180.F,c=std::cos(a),s=std::sin(a);return {{c,0,s},{0,1,0},{-s,0,c}};}
FieldRifleRootGate gate(bool kar){return {kar?"kar98k":"m1carbine",kar?"viewmodel_ger_kar98_rifle":"viewmodel_usa_m1carbine_rifle",true,true,true,false,false,0};}
void geometry(){
    const std::array<Basis3f,3> axes{{{},{{0,1,0},{-1,0,0},{0,0,1}},{{0,0,1},{0,1,0},{-1,0,0}}}};
    for(bool kar:{false,true})for(auto target:axes)for(float kick:{0.F,.1F,1.2F,-4.F,14.F}){
        const Vec3f origin{3,-2,8},localGrip{-2,.3F,-1};const auto grip=add(origin,transform(target,localGrip));
        const auto animated=pitch(kick),observed=compose(target,animated);
        BazookaViewmodelAlignment a{},b{};
        check(calculate_field_rifle_root_alignment(gate(kar),origin,target,grip,observed,target,&a),"exact field rifle root rejected");
        check(near(compose(a.corrected_root_axis,animated).forward,target.forward),"firing tilt not cancelled in controller frame");
        check(near(add(a.corrected_root_origin,transform(a.corrected_root_axis,localGrip)),grip),"rifle grip pivot moved");
        check(calculate_field_rifle_root_alignment(gate(kar),origin,target,grip,observed,target,&b)&&
            near(a.corrected_root_origin,b.corrected_root_origin)&&near(a.corrected_root_axis.forward,b.corrected_root_axis.forward),"correction accumulates");
        auto retained=gate(kar);retained.current_tracking=false;retained.retained_render_only=true;
        check(calculate_field_rifle_root_alignment(retained,origin,target,grip,observed,target,&b)&&
            near(a.corrected_root_axis.forward,b.corrected_root_axis.forward),"qualified retained render snaps");
    }
}
void rejection(){
    for(bool kar:{false,true})for(int state=-1;state<40;++state){auto g=gate(kar);g.native_weapon_state=state;check(field_rifle_root_allowed(g)==(state==0||state==5),"unmeasured native state enabled");}
    for(int kind=0;kind<12;++kind){auto g=gate(false);Basis3f root{},observed=pitch(3),target{};Vec3f origin{};
        switch(kind){case 0:g.weapon="zombie_m1carbine";break;case 1:g.model="viewmodel_ger_kar98_rifle";break;
        case 2:g.weapon="m1garand";break;case 3:g.weapon="zombie_colt";break;case 4:g.exact_asset_witness=false;break;
        case 5:g.current_tracking=false;break;case 6:g.right_hand_owner=false;break;case 7:g.transition_pending=true;break;
        case 8:g.blocks_attack=true;break;case 9:observed=pitch(16);break;case 10:root.left=root.forward;break;
        case 11:origin.z=std::numeric_limits<float>::quiet_NaN();break;}
        BazookaViewmodelAlignment out{};std::memset(&out,0x3f,sizeof(out));const auto before=out;
        check(!calculate_field_rifle_root_alignment(g,origin,root,{},observed,target,&out),"foreign or malformed rifle enabled");
        check(std::memcmp(&before,&out,sizeof(out))==0,"rejection partially writes output");
    }
}
}
int main(){geometry();rejection();}
