// SPDX-License-Identifier: GPL-3.0-only
#include "sight_pixel_projection_logic.hpp"
#include "sight_pixel_geometry_recipes.hpp"
#include "sight_audit_recipes.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace wawvr::mod;
using wawvr::xr::Vec3f;
void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
bool near(float a,float b,float tolerance=.0001F) {return std::abs(a-b)<tolerance;}
bool near(Vec3f a,Vec3f b) {return near(a.x,b.x)&&near(a.y,b.y)&&near(a.z,b.z);}
wawvr::xr::Basis3f quaternion_basis(std::array<float,4> q,float scale) {
    const auto x=q[0],y=q[1],z=q[2],w=q[3];
    return {{1-scale*(y*y+z*z),scale*(x*y+z*w),scale*(x*z-y*w)},
        {scale*(x*y-z*w),1-scale*(x*x+z*z),scale*(y*z+x*w)},
        {scale*(x*z+y*w),scale*(y*z-x*w),1-scale*(x*x+y*y)}};
}
Vec3f turn(Vec3f p) {return {-p.y,p.x,p.z};}
SightAuditSnapshot snapshot(SightAuditAsset asset) {
    SightAuditSnapshot s{};s.valid=true;s.rested_eligible=true;s.model=1;s.skeleton_timestamp=2;
    s.metadata.asset=asset;s.metadata.definition=3;s.metadata.dobj=4;s.metadata.weapon_index=17;
    s.metadata.frame_id=10;s.metadata.generation=11;s.metadata.pose_serial=12;s.metadata.pose_published_ms=13;
    const auto* recipe=detail::sight_asset_recipe(asset);
    s.geometry_fingerprint=recipe->fingerprint;s.front[0]=recipe->front[0];s.rear[0]=recipe->rear[0];
    if(asset==SightAuditAsset::colt) {
        s.front_bone_basis=quaternion_basis(recipe->front_bind_orientation,recipe->front_bind_weight);
        s.sight_bone_basis=quaternion_basis(recipe->rear_bind_orientation,recipe->rear_bind_weight);
    }
    return s;
}
void projection_tests() {
    T4SceneView v{};v.width=500;v.height=600;v.tan_half_fov_x=1;v.tan_half_fov_y=1;v.near_clip=.1F;
    wawvr::xr::NormalizedViewport dest{0,0,1,1};SightPixelProjection p{};
    check(project_sight_pixel_anchor({10,0,0},v,dest,1000,600,&p)&&
        near(p.packed_x,250)&&near(p.packed_y,300)&&near(p.eye_u,.5F)&&near(p.eye_v,.5F)&&p.inside_eye,
        "symmetric native source projection failed");
    check(project_sight_pixel_anchor({10,5,5},v,dest,1000,600,&p)&&
        near(p.packed_x,125)&&near(p.packed_y,150),"IW left/up signs or top-down BMP convention inverted");
    dest={-.1F,-.2F,1.3F,1.4F};
    check(project_sight_pixel_anchor({10,0,0},v,dest,1000,600,&p)&&
        near(p.eye_u,.55F)&&near(p.eye_v,.5F),"asymmetric compositor destination not applied");
    v.x=500;v.width=501;v.origin.y=-1;dest={0,0,1,1};
    check(project_sight_pixel_anchor({10,0,0},v,dest,1001,600,&p)&&
        near(p.packed_x,725.45F)&&near(p.eye_u,.45F),"actual right-eye IPD/odd packed width ignored");
    v.axis={{0,1,0},{-1,0,0},{0,0,1}};v.origin={};v.x=0;v.width=500;
    check(project_sight_pixel_anchor({0,10,0},v,dest,1000,600,&p)&&near(p.packed_x,250),
        "actual scene axis was replaced by head-forward assumption");
    const auto expect_rejected=[&](Vec3f point,T4SceneView view,wawvr::xr::NormalizedViewport destination) {
        SightPixelProjection sentinel{91,92,93,94,95,true};
        check(!project_sight_pixel_anchor(point,view,destination,1000,600,&sentinel)&&
            sentinel.packed_x==91&&sentinel.depth==95,"bad projection accepted or outputs mutated");
    };
    expect_rejected({0,-10,0},v,dest);expect_rejected({0,.05F,0},v,dest);
    auto bad=v;bad.tan_half_fov_y=0;expect_rejected({0,10,0},bad,dest);
    bad=v;bad.axis.up.z=-1;expect_rejected({0,10,0},bad,dest);
    bad=v;bad.x=900;expect_rejected({0,10,0},bad,dest);
    bad=v;bad.origin.x=std::numeric_limits<float>::quiet_NaN();expect_rejected({0,10,0},bad,dest);
    check(project_sight_pixel_anchor({100,10,0},v,dest,1000,600,&p)&&!p.inside_eye,
        "offscreen projection falsely declared viewport-inside");
}
void geometry_tests() {
    for(const auto asset:{SightAuditAsset::garand,SightAuditAsset::mosin,SightAuditAsset::svt,SightAuditAsset::colt}) {
        auto s=snapshot(asset);std::array<Vec3f,kSightPixelAnchorCount> points{};
        check(build_sight_pixel_world_anchors(s,&points),"exact recipe pixel anchors rejected");
        const auto* recipe=detail::sight_pixel_geometry_recipe(asset);
        for(std::size_t i=0;i<points.size();++i) check(near(points[i],(*recipe)[i].local),"actual mesh endpoint changed");
        auto bad=s;bad.geometry_fingerprint++;
        check(!build_sight_pixel_world_anchors(bad,&points),"foreign topology accepted as pixel proof");
        check(same_sight_pixel_source(s,s),"exact source rejected");
        for(unsigned i=0;i<8;++i) {
            auto changed=s;
            switch(i) {
            case 0:changed.metadata.frame_id++;break;case 1:changed.metadata.pose_serial++;break;
            case 2:changed.skeleton_timestamp++;break;case 3:changed.model++;break;
            case 4:changed.metadata.dobj++;break;case 5:changed.front[0].x+=.001F;break;
            case 6:changed.rear[0].z+=.001F;break;default:changed.front_bone_basis.forward.x=.9F;break;
            }
            check(!same_sight_pixel_source(s,changed),"pixel comparison bridged source/geometry drift");
        }
        if(asset==SightAuditAsset::colt) {
            auto rotated=s;rotated.front[0]=turn(s.front[0]);rotated.rear[0]=turn(s.rear[0]);
            rotated.front_bone_basis={turn(s.front_bone_basis.forward),turn(s.front_bone_basis.left),turn(s.front_bone_basis.up)};
            rotated.sight_bone_basis={turn(s.sight_bone_basis.forward),turn(s.sight_bone_basis.left),turn(s.sight_bone_basis.up)};
            check(build_sight_pixel_world_anchors(rotated,&points),"rotated Colt inverse-bind pixel anchors rejected");
            for(std::size_t i=0;i<points.size();++i)
                check(near(points[i],turn((*recipe)[i].local)),"Colt pixel geometry used authored coordinates as bone-local");
        }
    }
    auto svt=snapshot(SightAuditAsset::svt);const auto* profile=detail::sight_asset_recipe(SightAuditAsset::svt);
    svt.front[0]={10,20,30};svt.front_bone_basis={{0,1,0},{-1,0,0},{0,0,1}};
    std::array<Vec3f,kSightPixelAnchorCount> points{};
    check(build_sight_pixel_world_anchors(svt,&points),"separate front/rear owner transforms rejected");
    const auto& first=detail::kSvtPixelAnchors[0].local;
    check(near(points[0],{10-(first.y-profile->front[0].y),20+(first.x-profile->front[0].x),
        30+(first.z-profile->front[0].z)})&&near(points[4],detail::kSvtPixelAnchors[4].local),
        "SVT front rotation incorrectly moved rear sight");
}
void budget_tests() {
    SightPixelFrameBudget budget{};
    check(!admit_sight_pixel_frame(0,&budget),"zero diagnostic frame admitted");
    for(std::uint64_t i=1;i<=512;++i) {
        check(admit_sight_pixel_frame(i,&budget)&&admit_sight_pixel_frame(i,&budget),
            "paired eyes incorrectly consume two frame slots");
    }
    check(!admit_sight_pixel_frame(513,&budget)&&!admit_sight_pixel_frame(513,&budget)&&
        !admit_sight_pixel_frame(514,&budget)&&!admit_sight_pixel_frame(10,&budget),
        "512-frame bound or monotonic ordering failed");
}
}
int main() {
    try {projection_tests();geometry_tests();budget_tests();std::cout<<"Sight pixel projection tests passed\n";}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
