// SPDX-License-Identifier: GPL-3.0-only
#include "colt_root_alignment.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
namespace {
using namespace wawvr::mod;
using wawvr::xr::Basis3f;
using wawvr::xr::Vec3f;
void check(bool value,std::string_view why) { if(!value) throw std::runtime_error(std::string(why)); }
float dot(Vec3f a,Vec3f b) {return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec3f add(Vec3f a,Vec3f b) {return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3f sub(Vec3f a,Vec3f b) {return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3f compose(Basis3f a,Vec3f p) {return {a.forward.x*p.x+a.left.x*p.y+a.up.x*p.z,a.forward.y*p.x+a.left.y*p.y+a.up.y*p.z,a.forward.z*p.x+a.left.z*p.y+a.up.z*p.z};}
Basis3f compose(Basis3f a,Basis3f b) {return {compose(a,b.forward),compose(a,b.left),compose(a,b.up)};}
bool near(Vec3f a,Vec3f b,float epsilon=.0001F) {return std::abs(a.x-b.x)<epsilon&&std::abs(a.y-b.y)<epsilon&&std::abs(a.z-b.z)<epsilon;}
Basis3f pitch(float degrees) {const float a=degrees*3.14159265358979323846F/180.F,c=std::cos(a),s=std::sin(a);return {{c,0,s},{0,1,0},{-s,0,c}};}
ColtRootAlignmentGate gate() {return {"zombie_colt","viewmodel_usa_colt45_pistol",true,true,true,false,false,0};}
void geometry() {
    const std::array<Basis3f,4> directions{{{},{{0,1,0},{-1,0,0},{0,0,1}},
        {{0,-1,0},{0,0,1},{-1,0,0}},{{0,0,1},{1,0,0},{0,1,0}}}};
    for(auto target:directions) for(float kick:std::array{0.F,1.375F,3.495F,-8.F,14.F}) {
        const auto animated=pitch(kick);
        const auto observed=compose(target,animated);
        const Vec3f origin{13,-7,4},gripLocal{-2.5F,.4F,-1.2F};
        const auto grip=add(origin,compose(target,gripLocal));
        BazookaViewmodelAlignment out{};
        check(calculate_colt_root_alignment(gate(),origin,target,grip,observed,target,&out),"exact current Colt stabilization refused");
        const auto finalRoot=compose(out.corrected_root_axis,animated);
        check(near(finalRoot.forward,target.forward),"firing kick remains or becomes global pitch/headset-forward");
        check(near(add(out.corrected_root_origin,compose(out.corrected_root_axis,gripLocal)),grip),"the correction moves the grip pivot");
        const Vec3f boltLocal{1.3F,-.5F,.9F},flashLocal{6.1F,.02F,1.1F};
        const auto correctedBolt=add(out.corrected_root_origin,compose(out.corrected_root_axis,boltLocal));
        const auto correctedFlash=add(out.corrected_root_origin,compose(out.corrected_root_axis,flashLocal));
        const auto relative=sub(correctedFlash,correctedBolt),authored=sub(flashLocal,boltLocal);
        check(std::abs(dot(relative,relative)-dot(authored,authored))<.0001F,"rigid outer correction changes relative slide/muzzle geometry");
        // The next current pose is computed from the same immutable controller
        // attachment, not by feeding the compensating outer axis back as aim.
        BazookaViewmodelAlignment repeated{};
        check(calculate_colt_root_alignment(gate(),origin,target,grip,observed,target,&repeated)&&
            near(out.corrected_root_origin,repeated.corrected_root_origin)&&
            near(out.corrected_root_axis.forward,repeated.corrected_root_axis.forward),"repeated frames accumulate steering");
    }
}
void rejection() {
    for(int state=-1;state<40;++state){auto g=gate();g.native_weapon_state=state;check(colt_root_alignment_allowed(g)==(state==0||state==5),"unmeasured equip/reload/offhand state was enabled");}
    for(unsigned kind=0;kind<11;++kind) {
        auto g=gate();Basis3f outer{},observed=pitch(3),target{};Vec3f origin{},grip{};
        switch(kind){
        case 0:g.weapon="colt";break;
        case 1:g.viewmodel="viewmodel_zombie_colt45_pistol";break;
        case 2:g.weapon="m1carbine";break;
        case 3:g.exact_asset_witness=false;break;
        case 4:g.current_tracking=false;break;
        case 5:g.right_hand_owner=false;break;
        case 6:g.transition_pending=true;break;
        case 7:g.manual_reload_blocks_attack=true;break;
        case 8:observed=pitch(16);break;
        case 9:outer.left=outer.forward;break;
        case 10:origin.x=std::numeric_limits<float>::quiet_NaN();break;
        }
        BazookaViewmodelAlignment output{};std::memset(&output,0x3f,sizeof(output));
        const auto before=output;
        check(!calculate_colt_root_alignment(g,origin,outer,grip,observed,target,&output),"invalid/unrelated Colt root request accepted");
        check(!std::memcmp(&before,&output,sizeof(output)),"rejected root request partially changed its output");
    }
    auto invalid=Basis3f{};invalid.up.z=std::numeric_limits<float>::infinity();
    BazookaViewmodelAlignment output{};
    check(!calculate_colt_root_alignment(gate(),{}, {},{},invalid,{},&output),"invalid current evaluated root accepted");
    check(!calculate_colt_root_alignment(gate(),{}, {},{}, {},invalid,&output),"invalid controller basis accepted");
    check(!calculate_colt_root_alignment(gate(),{}, {},{}, {},{},nullptr),"null correction output accepted");
}
void retention() {
    check(colt_retained_render_allowed(true,true,1250,1000),"bounded render retention boundary rejected");
    check(!colt_retained_render_allowed(true,true,1251,1000)&&
        !colt_retained_render_allowed(true,true,999,1000)&&
        !colt_retained_render_allowed(false,true,1100,1000)&&
        !colt_retained_render_allowed(true,false,1100,1000)&&
        !colt_retained_render_allowed(true,true,1100,0),"stale/foreign/absent retained owner accepted");
    auto retained=gate();retained.current_tracking=false;retained.retained_render_only=true;
    BazookaViewmodelAlignment frozen{},live{};
    check(calculate_colt_root_alignment(retained,{}, {},{0,1,-1},pitch(3),{},&frozen),"qualified render-only owner lost stabilization during brief dropout");
    check(calculate_colt_root_alignment(gate(),{}, {},{0,1,-1},pitch(3),{},&live)&&
        near(frozen.corrected_root_axis.forward,live.corrected_root_axis.forward)&&
        near(frozen.corrected_root_origin,live.corrected_root_origin),"tracking recovery changes render compensation for the same controller root");
    PublishedWeaponMuzzleSnapshot stale{};stale.valid=true;
    stale.controller_generation=10;stale.publication_milliseconds=1000;
    check(!published_weapon_muzzle_is_fresh(stale,10,1250),
        "render-only grace accidentally widens the production shot freshness policy");
}
} // namespace
int main(){geometry();rejection();retention();}
