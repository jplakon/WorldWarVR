// SPDX-License-Identifier: GPL-3.0-only
#include "shot_line_audit.hpp"
#include "weapon_ballistics_logic.hpp"
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace wawvr::mod;
void require(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
int main(){
    using wawvr::xr::Vec3f; using wawvr::xr::Basis3f;
    unsigned checks=0;
    // Pure routing regression across near/mid/far and head-independent weapon
    // directions. This does not certify an asset's physical iron-sight geometry.
    for(float yaw : {-150.F,-90.F,-45.F,0.F,45.F,90.F,150.F})
    for(float pitch : {-45.F,0.F,35.F})
    for(float range : {120.F,1200.F,6000.F}) {
        const float y=yaw/57.295779513F,p=pitch/57.295779513F;
        Basis3f basis{{std::cos(p)*std::cos(y),std::cos(p)*std::sin(y),std::sin(p)},
            {-std::sin(y),std::cos(y),0},
            {-std::sin(p)*std::cos(y),-std::sin(p)*std::sin(y),std::cos(p)}};
        Vec3f f{},r{},u{};
        require(apply_authoritative_weapon_basis_override({true,true,true},basis,&f,&r,&u),"valid routing");
        const Vec3f origin{12,-27,65};
        const Vec3f hit{origin.x+range*f.x,origin.y+range*f.y,origin.z+range*f.z};
        ShotLineMetrics metrics{};
        require(measure_shot_line(origin,basis,hit,&metrics),"finite forward impact");
        require(metrics.angular_error_degrees<.001F,"rotation/range does not introduce aiming bias");
        const Vec3f shifted{hit.x+4*basis.left.x+3*basis.up.x,hit.y+4*basis.left.y+3*basis.up.y,hit.z+4*basis.left.z+3*basis.up.z};
        require(measure_shot_line(origin,basis,shifted,&metrics),"offset impact");
        require(std::abs(metrics.left_world_units-4)<.002F && std::abs(metrics.up_world_units-3)<.002F,"signed lateral and vertical bias measured separately");
        ++checks;
    }
    Basis3f b{{1,0,0},{0,1,0},{0,0,1}}; ShotLineMetrics m{1,2,3,4};
    require(!measure_shot_line({},b,{-1,0,0},&m)&&m.angular_error_degrees==4,"behind impact rejected atomically");
    b.forward.x=2;require(!measure_shot_line({},b,{10,0,0},&m),"scaled basis rejected");
    b.forward.x=std::numeric_limits<float>::quiet_NaN();require(!measure_shot_line({},b,{10,0,0},&m),"NaN rejected");
    require(!measure_shot_line({},b,{10,0,0},nullptr),"null output rejected");
    std::cout<<checks<<" head-independent near/mid/far routing cases passed; asset sights require live evidence\n";
}
