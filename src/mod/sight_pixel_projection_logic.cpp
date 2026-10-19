// SPDX-License-Identifier: GPL-3.0-only
#include "sight_pixel_projection_logic.hpp"
#include "sight_audit_recipes.hpp"
#include "sight_pixel_geometry_recipes.hpp"
#include "sight_bind_transform.hpp"
#include <cmath>

namespace wawvr::mod {
namespace {
using wawvr::xr::Vec3f; using wawvr::xr::Basis3f;
bool finite(Vec3f v) noexcept {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
float dot(Vec3f a,Vec3f b) noexcept {return a.x*b.x+a.y*b.y+a.z*b.z;}
bool same(Vec3f a,Vec3f b) noexcept {return a.x==b.x&&a.y==b.y&&a.z==b.z;}
bool same(Basis3f a,Basis3f b) noexcept {return same(a.forward,b.forward)&&same(a.left,b.left)&&same(a.up,b.up);}
bool valid(Basis3f b) noexcept {
    const Vec3f cross{b.forward.y*b.left.z-b.forward.z*b.left.y,
        b.forward.z*b.left.x-b.forward.x*b.left.z,b.forward.x*b.left.y-b.forward.y*b.left.x};
    return finite(b.forward)&&finite(b.left)&&finite(b.up)&&
        std::abs(dot(b.forward,b.forward)-1.F)<.002F&&std::abs(dot(b.left,b.left)-1.F)<.002F&&
        std::abs(dot(b.up,b.up)-1.F)<.002F&&std::abs(dot(b.forward,b.left))<.002F&&
        std::abs(dot(b.forward,b.up))<.002F&&std::abs(dot(b.left,b.up))<.002F&&dot(cross,b.up)>.998F;
}
Vec3f transformed(Vec3f local,Vec3f reference,Vec3f world,Basis3f axis) noexcept {
    const Vec3f d{local.x-reference.x,local.y-reference.y,local.z-reference.z};
    return {world.x+d.x*axis.forward.x+d.y*axis.left.x+d.z*axis.up.x,
        world.y+d.x*axis.forward.y+d.y*axis.left.y+d.z*axis.up.y,
        world.z+d.x*axis.forward.z+d.y*axis.left.z+d.z*axis.up.z};
}
}
bool project_sight_pixel_anchor(const Vec3f& p,const T4SceneView& v,
    const wawvr::xr::NormalizedViewport& dest,std::uint32_t width,std::uint32_t height,
    SightPixelProjection* output) noexcept {
    if (!output||!finite(p)||!finite(v.origin)||!valid(v.axis)||!width||!height||width>16384||height>16384||
        v.x<0||v.y<0||v.width<=0||v.height<=0||std::uint32_t(v.x)>width||std::uint32_t(v.y)>height||
        std::uint32_t(v.width)>width-std::uint32_t(v.x)||std::uint32_t(v.height)>height-std::uint32_t(v.y)||
        !std::isfinite(v.tan_half_fov_x)||!std::isfinite(v.tan_half_fov_y)||
        v.tan_half_fov_x<=0||v.tan_half_fov_y<=0||!std::isfinite(v.near_clip)||v.near_clip<0||
        !std::isfinite(dest.x)||!std::isfinite(dest.y)||!std::isfinite(dest.width)||!std::isfinite(dest.height)||
        dest.width<=0||dest.height<=0||dest.width>8||dest.height>8||std::abs(dest.x)>8||std::abs(dest.y)>8)
        return false;
    const Vec3f d{p.x-v.origin.x,p.y-v.origin.y,p.z-v.origin.z};
    const float depth=dot(d,v.axis.forward);
    if (!std::isfinite(depth)||depth<=v.near_clip||depth<=.00001F) return false;
    // Native T4 source view is symmetric, IW row1 is LEFT, and BMP rows are
    // top-down. Do not use raw runtime-eye orientation in place of this view.
    const float u=.5F*(1.F-dot(d,v.axis.left)/(depth*v.tan_half_fov_x));
    const float t=.5F*(1.F-dot(d,v.axis.up)/(depth*v.tan_half_fov_y));
    SightPixelProjection result{float(v.x)+u*float(v.width),float(v.y)+t*float(v.height),
        dest.x+u*dest.width,dest.y+t*dest.height,depth,false};
    if (!std::isfinite(result.packed_x)||!std::isfinite(result.packed_y)||
        !std::isfinite(result.eye_u)||!std::isfinite(result.eye_v)) return false;
    result.inside_eye=u>=0&&u<=1&&t>=0&&t<=1&&result.eye_u>=0&&result.eye_u<=1&&result.eye_v>=0&&result.eye_v<=1;
    *output=result; return true;
}
bool build_sight_pixel_world_anchors(const SightAuditSnapshot& s,
    std::array<Vec3f,kSightPixelAnchorCount>* output) noexcept {
    const auto* profile=detail::sight_asset_recipe(s.metadata.asset);
    const auto* anchors=detail::sight_pixel_geometry_recipe(s.metadata.asset);
    if (!output||!s.valid||!profile||!anchors||s.geometry_fingerprint!=profile->fingerprint||
        !s.metadata.frame_id||!s.metadata.pose_serial||!s.skeleton_timestamp||
        !finite(s.front[0])||!finite(s.rear[0])||!valid(s.front_bone_basis)||!valid(s.sight_bone_basis)) return false;
    std::array<Vec3f,kSightPixelAnchorCount> result{};
    for (std::size_t i=0;i<result.size();++i) {
        const auto& a=(*anchors)[i];
        const bool front=i<4;
        if (a.bone!=(front?profile->front_bone:profile->rear_bone)) return false;
        if (s.metadata.asset==SightAuditAsset::colt) {
            const auto reference=front?profile->front[0]:profile->rear[0];
            Vec3f delta{};
            if (!inverse_sight_bind_vector({a.local.x-reference.x,a.local.y-reference.y,a.local.z-reference.z},
                front?profile->front_bind_orientation:profile->rear_bind_orientation,
                front?profile->front_bind_weight:profile->rear_bind_weight,&delta)) return false;
            result[i]=transformed(delta,{},front?s.front[0]:s.rear[0],front?s.front_bone_basis:s.sight_bone_basis);
            if (!finite(result[i])) return false;
            continue;
        }
        result[i]=transformed(a.local,front?profile->front[0]:profile->rear[0],
            front?s.front[0]:s.rear[0],front?s.front_bone_basis:s.sight_bone_basis);
        if (!finite(result[i])) return false;
    }
    *output=result; return true;
}
bool same_sight_pixel_source(const SightAuditSnapshot& a,const SightAuditSnapshot& b) noexcept {
    const auto& x=a.metadata; const auto& y=b.metadata;
    if (!a.valid||!b.valid||!x.definition||!x.dobj||!a.model||!a.skeleton_timestamp||
        !x.frame_id||!x.generation||!x.pose_serial||!x.pose_published_ms||
        x.asset!=y.asset||x.definition!=y.definition||x.dobj!=y.dobj||
        x.weapon_index!=y.weapon_index||a.model!=b.model||a.skeleton_timestamp!=b.skeleton_timestamp||
        a.geometry_fingerprint!=b.geometry_fingerprint||x.frame_id!=y.frame_id||x.generation!=y.generation||
        x.pose_serial!=y.pose_serial||x.pose_published_ms!=y.pose_published_ms||
        !same(a.front_bone_basis,b.front_bone_basis)||!same(a.sight_bone_basis,b.sight_bone_basis)||
        !same(x.muzzle,y.muzzle)||!same(x.projectile_basis,y.projectile_basis)) return false;
    for (std::size_t i=0;i<a.front.size();++i) if (!same(a.front[i],b.front[i])) return false;
    for (std::size_t i=0;i<a.rear.size();++i) if (!same(a.rear[i],b.rear[i])) return false;
    return true;
}
} // namespace wawvr::mod
