// SPDX-License-Identifier: GPL-3.0-only
#include "sight_geometry_basis.hpp"
#include "weapon_ballistics_logic.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace wawvr::mod {
namespace {
using wawvr::xr::Vec3f;
using wawvr::xr::Basis3f;
constexpr std::uint64_t kFnvOffset=14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime=1099511628211ULL;
bool finite(Vec3f p) noexcept {return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
float dot(Vec3f a,Vec3f b) noexcept {return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec3f add(Vec3f a,Vec3f b) noexcept {return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3f subtract(Vec3f a,Vec3f b) noexcept {return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3f compose(Basis3f a,Vec3f p) noexcept {
    return {a.forward.x*p.x+a.left.x*p.y+a.up.x*p.z,
        a.forward.y*p.x+a.left.y*p.y+a.up.y*p.z,
        a.forward.z*p.x+a.left.z*p.y+a.up.z*p.z};
}
bool near(Vec3f a,Vec3f b,float tolerance=.00002F) noexcept {
    return finite(a)&&finite(b)&&std::abs(a.x-b.x)<=tolerance&&
        std::abs(a.y-b.y)<=tolerance&&std::abs(a.z-b.z)<=tolerance;
}
bool valid_axis(Basis3f a) noexcept {
    const Vec3f cross{a.forward.y*a.left.z-a.forward.z*a.left.y,
        a.forward.z*a.left.x-a.forward.x*a.left.z,a.forward.x*a.left.y-a.forward.y*a.left.x};
    return finite(a.forward)&&finite(a.left)&&finite(a.up)&&
        std::abs(dot(a.forward,a.forward)-1.F)<.002F&&std::abs(dot(a.left,a.left)-1.F)<.002F&&
        std::abs(dot(a.up,a.up)-1.F)<.002F&&std::abs(dot(a.forward,a.left))<.002F&&
        std::abs(dot(a.forward,a.up))<.002F&&std::abs(dot(a.left,a.up))<.002F&&dot(cross,a.up)>.998F;
}
bool bone_basis(const SightGeometryBone& b,Basis3f& result) noexcept {
    float norm{};for(float q:b.quaternion){if(!std::isfinite(q))return false;norm+=q*q;}
    if(!finite(b.translation)||!std::isfinite(b.weight)||std::abs(norm-1.F)>.002F||
        b.weight<1.9F||b.weight>2.1F||std::abs(b.weight-2.F/norm)>.0001F)return false;
    const auto& q=b.quaternion;const float x=q[0],y=q[1],z=q[2],w=q[3],s=b.weight;
    const Basis3f a{{1-s*(y*y+z*z),s*(x*y+z*w),s*(x*z-y*w)},
        {s*(x*y-z*w),1-s*(x*x+z*z),s*(y*z+x*w)},
        {s*(x*z+y*w),s*(y*z-x*w),1-s*(x*x+y*y)}};
    if(!valid_axis(a))return false;result=a;return true;
}
bool string_valid(std::string_view s) noexcept {
    return !s.empty()&&s.size()<96&&s.find('\0')==std::string_view::npos;
}
void bytes(std::uint64_t& hash,const void* p,std::size_t count) noexcept {
    const auto* data=static_cast<const unsigned char*>(p);
    for(std::size_t i=0;i<count;++i)hash=(hash^data[i])*kFnvPrime;
}
template<class T>void value(std::uint64_t& hash,const T& p) noexcept {bytes(hash,&p,sizeof(p));}
void string_hash(std::uint64_t& hash,std::string_view s) noexcept {
    const auto size=static_cast<std::uint32_t>(s.size());value(hash,size);bytes(hash,s.data(),s.size());
}
const SightGeometrySurface* surface_for(const SightGeometryRecipe& p,std::uint8_t surface) noexcept {
    for(const auto& s:p.surfaces)if(s.index==surface)return &s;return nullptr;
}
const SightGeometryVertex* vertex_for(const SightGeometrySurface& p,std::uint16_t vertex) noexcept {
    for(const auto& v:p.vertices)if(v.index==vertex)return &v;return nullptr;
}
const SightGeometryRigidRange* vertex_owner(const SightGeometrySurface& p,std::uint16_t vertex) noexcept {
    std::uint32_t first{};
    for(const auto& range:p.rigid_ranges){
        if(vertex>=first&&vertex-first<range.vertices)return &range;first+=range.vertices;
    }return nullptr;
}
const SightGeometryRigidRange* triangle_owner(const SightGeometrySurface& p,std::uint16_t triangle) noexcept {
    for(const auto& range:p.rigid_ranges)
        if(triangle>=range.first_triangle&&triangle-range.first_triangle<range.triangles)return &range;
    return nullptr;
}
bool reference_valid(const SightGeometryRecipe& p,const SightGeometryReference& ref) noexcept {
    if(ref.owner_bone>=p.binds.size()||ref.vertices.empty()||ref.vertices.size()>kSightGeometryMaximumReferenceVertices||
        (ref.method!=SightGeometryReferenceMethod::weighted_centroid&&
         ref.method!=SightGeometryReferenceMethod::aperture_circle_yz)||
        (ref.method==SightGeometryReferenceMethod::aperture_circle_yz&&ref.vertices.size()<3))return false;
    double total{};
    for(std::size_t i=0;i<ref.vertices.size();++i){
        const auto& anchor=ref.vertices[i];
        if(!std::isfinite(anchor.weight)||anchor.weight<=0.F||anchor.weight>1.e6F)return false;
        total+=anchor.weight;
        const auto* surface=surface_for(p,anchor.surface);
        if(!surface||!vertex_for(*surface,anchor.vertex))return false;
        const auto* range=vertex_owner(*surface,anchor.vertex);
        if(!range||range->bone!=ref.owner_bone)return false;
        bool backed=false;
        for(const auto& triangle:surface->triangles){
            const auto* owner=triangle_owner(*surface,triangle.index);
            for(auto v:triangle.vertices)if(v==anchor.vertex&&owner==range)backed=true;
        }
        if(!backed)return false;
        for(std::size_t j=0;j<i;++j)if(ref.vertices[j].surface==anchor.surface&&
            ref.vertices[j].vertex==anchor.vertex)return false;
    }
    return std::isfinite(total)&&total>0.;
}
bool recipe_valid(const SightGeometryRecipe& p) noexcept {
    const auto bones=p.binds.size();
    if(!sight_geometry_basis_kind_valid(p.basis_kind)||!string_valid(p.id)||!string_valid(p.model)||!bones||bones>kSightGeometryMaximumBones||
        p.bone_names.size()!=bones||p.parent_offsets.size()!=bones-1||p.root_bone>=bones||p.flash_bone>=bones||
        p.surface_count==0||p.surface_count>32||p.surfaces.empty()||p.surfaces.size()>kSightGeometryMaximumSurfaces||
        p.bone_names[p.root_bone]!="j_gun"||p.bone_names[p.flash_bone]!="tag_flash")return false;
    for(std::size_t i=0;i<bones;++i){
        Basis3f ignored{};
        if(!string_valid(p.bone_names[i])||!bone_basis(p.binds[i],ignored)||
            (i&&(!p.parent_offsets[i-1]||p.parent_offsets[i-1]>i)))return false;
        for(std::size_t j=0;j<i;++j)if(p.bone_names[j]==p.bone_names[i])return false;
    }
    for(std::size_t si=0;si<p.surfaces.size();++si){
        const auto& s=p.surfaces[si];
        if(s.index>=p.surface_count||!s.vertex_count||!s.triangle_count||!string_valid(s.material)||
            s.rigid_ranges.empty()||s.rigid_ranges.size()>kSightGeometryMaximumRanges||
            s.vertices.empty()||s.vertices.size()>kSightGeometryMaximumVertices||
            s.triangles.empty()||s.triangles.size()>kSightGeometryMaximumTriangles)return false;
        for(std::size_t sj=0;sj<si;++sj)if(p.surfaces[sj].index==s.index)return false;
        std::uint32_t vertices{},previous_end{};
        for(const auto& r:s.rigid_ranges){
            if(r.bone>=bones||!r.vertices||!r.triangles||r.vertices>s.vertex_count-vertices||
                r.first_triangle<previous_end||r.first_triangle>=s.triangle_count||
                r.triangles>s.triangle_count-r.first_triangle)return false;
            vertices+=r.vertices;previous_end=r.first_triangle+r.triangles;
        }
        for(std::size_t i=0;i<s.vertices.size();++i){
            if(s.vertices[i].index>=s.vertex_count||!finite(s.vertices[i].point)||
                !vertex_owner(s,s.vertices[i].index))return false;
            for(std::size_t j=0;j<i;++j)if(s.vertices[i].index==s.vertices[j].index)return false;
        }
        for(std::size_t i=0;i<s.triangles.size();++i){
            const auto& t=s.triangles[i];const auto* owner=triangle_owner(s,t.index);
            if(!owner||t.vertices[0]==t.vertices[1]||t.vertices[1]==t.vertices[2]||t.vertices[0]==t.vertices[2])return false;
            for(auto v:t.vertices)if(!vertex_for(s,v)||vertex_owner(s,v)!=owner)return false;
            for(std::size_t j=0;j<i;++j)if(t.index==s.triangles[j].index)return false;
        }
    }
    return reference_valid(p,p.front)&&reference_valid(p,p.rear);
}
// Native pointers are32-bit, including host64 unit-test reads. Never widen
// wrapped native offsets into valid-looking host pointers.
bool read(const SightGeometryReader& r,std::uintptr_t base,std::size_t offset,
    void* output,std::size_t count) noexcept {
    constexpr auto maximum=(std::numeric_limits<std::uint32_t>::max)();
    return output&&count&&base&&base<=maximum&&offset<=maximum-base&&count<=maximum-(base+offset)&&
        r.read&&r.read(r.context,base+offset,output,count);
}
template<class T>bool get(const SightGeometryReader& r,std::uintptr_t base,std::size_t offset,T& out) noexcept {
    return read(r,base,offset,&out,sizeof(out));
}
template<class T,std::size_t N>T at(const std::array<std::byte,N>& data,std::size_t offset) noexcept {
    T out{};if(offset<=N&&sizeof(T)<=N-offset)std::memcpy(&out,data.data()+offset,sizeof(out));return out;
}
bool text(const SightGeometryReader& r,std::uintptr_t address,std::string_view expected) noexcept {
    std::array<char,96> actual{};
    return string_valid(expected)&&read(r,address,0,actual.data(),expected.size()+1)&&
        actual[expected.size()]=='\0'&&std::memcmp(actual.data(),expected.data(),expected.size())==0;
}
struct SurfaceRead final {
    std::array<std::byte,0x40> header{};
    std::array<std::byte,kSightGeometryMaximumRanges*12> rigid{};
    std::array<Vec3f,kSightGeometryMaximumVertices> vertices{};
    std::array<std::array<std::uint16_t,3>,kSightGeometryMaximumTriangles> triangles{};
    std::uint32_t material{},material_name{};
};
struct AssetRead final {
    std::array<std::byte,16> definition{};
    std::array<std::byte,0x68> dobj{};
    std::array<std::byte,0xE4> model{};
    std::array<std::uint32_t,8> models{};
    std::array<std::uint8_t,8> model_bones{};
    std::array<std::uint16_t,kSightGeometryMaximumBones> bone_ids{};
    std::array<std::uint8_t,kSightGeometryMaximumBones-1> parents{};
    std::array<SightGeometryBone,kSightGeometryMaximumBones> binds{},evaluated{};
    std::array<SurfaceRead,kSightGeometryMaximumSurfaces> surfaces{};
    std::uint32_t pool{},model_address{},matrices{},timestamp{};
    std::uint16_t preceding_bones{};
    Vec3f origin{};
};
SightGeometryStatus read_asset(const SightGeometryReader& r,const SightGeometryRequest& request,
    const SightGeometryProfile& profile,AssetRead& a) noexcept {
    const auto& p=*profile.recipe;
    if(!get(r,request.definition,0,a.definition)||!get(r,request.dobj,0,a.dobj))return SightGeometryStatus::inaccessible;
    const auto model=at<std::uint32_t>(a.definition,12),table=at<std::uint32_t>(a.dobj,0x64);
    const auto model_count=at<std::uint8_t>(a.dobj,9),total=at<std::uint8_t>(a.dobj,10);
    if(!model_count||model_count>a.models.size()||!total||total>128||
        !read(r,table,0,a.models.data(),model_count*4U)||
        !text(r,at<std::uint32_t>(a.definition,0),profile.weapon)||!get(r,model,0,a.model)||
        !text(r,at<std::uint32_t>(a.model,0),profile.model))return SightGeometryStatus::identity;
    unsigned selected{},matches{},sum{},preceding{};
    for(unsigned i=0;i<model_count;++i){
        if(!a.models[i]||!get(r,a.models[i],4,a.model_bones[i])||!a.model_bones[i]||sum+a.model_bones[i]>128)
            return SightGeometryStatus::binding;
        if(a.models[i]==model){selected=i;preceding=sum;++matches;}sum+=a.model_bones[i];
    }
    if(matches!=1||sum!=total||a.model_bones[selected]!=p.binds.size())return SightGeometryStatus::binding;
    a.preceding_bones=static_cast<std::uint16_t>(preceding);a.model_address=model;
    if(at<std::uint8_t>(a.model,4)!=p.binds.size()||at<std::uint8_t>(a.model,5)!=1||
        at<std::uint8_t>(a.model,6)!=p.surface_count||!get(r,request.script_string_pool_address,0,a.pool))
        return SightGeometryStatus::binding;
    if(!read(r,at<std::uint32_t>(a.model,8),0,a.bone_ids.data(),p.binds.size()*2U)||
        (p.binds.size()>1&&!read(r,at<std::uint32_t>(a.model,12),0,a.parents.data(),p.binds.size()-1U))||
        !read(r,at<std::uint32_t>(a.model,0x1C),0,a.binds.data(),p.binds.size()*32U))return SightGeometryStatus::inaccessible;
    for(unsigned i=0;i<p.binds.size();++i){
        const auto id=a.bone_ids[i];
        if(!id||a.pool>(std::numeric_limits<std::uint32_t>::max)()-std::uint32_t(id)*12U-4U||
            !text(r,a.pool+std::uint32_t(id)*12U+4U,p.bone_names[i])||
            (i&&a.parents[i-1]!=p.parent_offsets[i-1]))return SightGeometryStatus::binding;
    }
    if(std::memcmp(a.binds.data(),p.binds.data(),p.binds.size()*32U))return SightGeometryStatus::binding;
    const auto surfaces=at<std::uint32_t>(a.model,0x20),materials=at<std::uint32_t>(a.model,0x24);
    for(std::size_t si=0;si<p.surfaces.size();++si){
        const auto& spec=p.surfaces[si];auto& observed=a.surfaces[si];
        if(!get(r,surfaces,spec.index*0x40U,observed.header)||!get(r,materials,spec.index*4U,observed.material)||
            !get(r,observed.material,0,observed.material_name))return SightGeometryStatus::inaccessible;
        if(!text(r,observed.material_name,spec.material))return SightGeometryStatus::binding;
        if(at<std::uint8_t>(observed.header,1)||at<std::uint16_t>(observed.header,2)!=spec.vertex_count||
            at<std::uint16_t>(observed.header,4)!=spec.triangle_count||
            at<std::uint32_t>(observed.header,0x24)!=spec.rigid_ranges.size())return SightGeometryStatus::geometry;
        if(!read(r,at<std::uint32_t>(observed.header,0x28),0,observed.rigid.data(),spec.rigid_ranges.size()*12U))
            return SightGeometryStatus::inaccessible;
        for(std::size_t ri=0;ri<spec.rigid_ranges.size();++ri){
            const auto& range=spec.rigid_ranges[ri];const auto off=ri*12U;
            if(at<std::uint16_t>(observed.rigid,off)!=range.bone*64U||
                at<std::uint16_t>(observed.rigid,off+2)!=range.vertices||
                at<std::uint16_t>(observed.rigid,off+4)!=range.first_triangle||
                at<std::uint16_t>(observed.rigid,off+6)!=range.triangles)return SightGeometryStatus::geometry;
        }
        const auto vertices=at<std::uint32_t>(observed.header,0x1C),indices=at<std::uint32_t>(observed.header,0x0C);
        for(std::size_t vi=0;vi<spec.vertices.size();++vi){
            if(!get(r,vertices,spec.vertices[vi].index*32U,observed.vertices[vi]))return SightGeometryStatus::inaccessible;
            if(std::memcmp(&observed.vertices[vi],&spec.vertices[vi].point,sizeof(Vec3f)))return SightGeometryStatus::geometry;
        }
        for(std::size_t ti=0;ti<spec.triangles.size();++ti){
            if(!get(r,indices,spec.triangles[ti].index*6U,observed.triangles[ti]))return SightGeometryStatus::inaccessible;
            if(observed.triangles[ti]!=spec.triangles[ti].vertices)return SightGeometryStatus::geometry;
        }
    }
    a.timestamp=at<std::uint32_t>(a.dobj,0x44);a.matrices=at<std::uint32_t>(a.dobj,0x48);
    if(!a.timestamp||!a.matrices)return SightGeometryStatus::unevaluated;
    const std::array<unsigned,4> required{p.root_bone,p.front.owner_bone,p.rear.owner_bone,p.flash_bone};
    for(unsigned local:required){
        const auto global=a.preceding_bones+local,mask=0x80000000U>>(global&31U),word=(global>>5U)*4U;
        if(at<std::uint32_t>(a.dobj,0x50+word)&mask)return SightGeometryStatus::hidden;
        if(!(at<std::uint32_t>(a.dobj,0x34+word)&mask))return SightGeometryStatus::unevaluated;
        if(!get(r,a.matrices,global*32U,a.evaluated[local]))return SightGeometryStatus::inaccessible;
        Basis3f ignored{};if(!bone_basis(a.evaluated[local],ignored))return SightGeometryStatus::invalid_axis;
    }
    Basis3f flash{};if(!bone_basis(a.evaluated[p.flash_bone],flash)||
        !near(flash.forward,request.evaluated_flash_basis.forward)||!near(flash.left,request.evaluated_flash_basis.left)||
        !near(flash.up,request.evaluated_flash_basis.up))return SightGeometryStatus::unstable;
    if(!get(r,request.native_skeleton_origin_address,0,a.origin))return SightGeometryStatus::inaccessible;
    if(!finite(a.origin))return SightGeometryStatus::invalid_axis;
    if(!near(add(a.origin,a.evaluated[p.flash_bone].translation),request.evaluated_flash_world,.005F))
        return SightGeometryStatus::world_position_mismatch;
    return SightGeometryStatus::accepted;
}
bool same(const AssetRead& a,const AssetRead& b) noexcept {
    if(a.definition!=b.definition||a.dobj!=b.dobj||a.model!=b.model||a.models!=b.models||
        a.model_bones!=b.model_bones||a.bone_ids!=b.bone_ids||a.parents!=b.parents||
        std::memcmp(a.binds.data(),b.binds.data(),sizeof(a.binds))||
        std::memcmp(a.evaluated.data(),b.evaluated.data(),sizeof(a.evaluated))||
        std::memcmp(&a.origin,&b.origin,sizeof(Vec3f))||a.pool!=b.pool||a.model_address!=b.model_address||
        a.matrices!=b.matrices||a.timestamp!=b.timestamp||a.preceding_bones!=b.preceding_bones)return false;
    for(std::size_t i=0;i<a.surfaces.size();++i){const auto& x=a.surfaces[i];const auto& y=b.surfaces[i];
        if(x.header!=y.header||x.rigid!=y.rigid||x.triangles!=y.triangles||
            std::memcmp(x.vertices.data(),y.vertices.data(),sizeof(x.vertices))||
            x.material!=y.material||x.material_name!=y.material_name)return false;
    }return true;
}
bool authored_reference(const SightGeometryRecipe& p,const SightGeometryReference& ref,Vec3f& result) noexcept {
    double x{},y{},z{},total{};
    for(const auto& anchor:ref.vertices){const auto* s=surface_for(p,anchor.surface);
        if(!s)return false;const auto* v=vertex_for(*s,anchor.vertex);if(!v)return false;
        total+=anchor.weight;x+=double(v->point.x)*anchor.weight;y+=double(v->point.y)*anchor.weight;z+=double(v->point.z)*anchor.weight;
    }
    x/=total;y/=total;z/=total;
    if(ref.method==SightGeometryReferenceMethod::aperture_circle_yz){
        double m[3][4]{};
        for(const auto& anchor:ref.vertices){const auto* s=surface_for(p,anchor.surface);const auto* v=vertex_for(*s,anchor.vertex);
            if(std::abs(double(v->point.x)-x)>.15)return false;
            const double dy=v->point.y-y,dz=v->point.z-z,row[3]{2.*dy,2.*dz,1.},rhs=dy*dy+dz*dz;
            for(unsigned i=0;i<3;++i){for(unsigned j=0;j<3;++j)m[i][j]+=anchor.weight*row[i]*row[j];
                m[i][3]+=anchor.weight*row[i]*rhs;}
        }
        for(unsigned col=0;col<3;++col){unsigned pivot=col;
            for(unsigned row=col+1;row<3;++row)if(std::abs(m[row][col])>std::abs(m[pivot][col]))pivot=row;
            if(!std::isfinite(m[pivot][col])||std::abs(m[pivot][col])<1.e-12)return false;
            for(unsigned j=col;j<4;++j)std::swap(m[col][j],m[pivot][j]);
            const double divisor=m[col][col];for(unsigned j=col;j<4;++j)m[col][j]/=divisor;
            for(unsigned row=0;row<3;++row)if(row!=col){const double scale=m[row][col];
                for(unsigned j=col;j<4;++j)m[row][j]-=scale*m[col][j];}
        }
        const double cy=m[0][3],cz=m[1][3],radius_sq=m[2][3]+cy*cy+cz*cz;
        if(!std::isfinite(radius_sq)||radius_sq<1.e-8||radius_sq>25.)return false;
        const double radius=std::sqrt(radius_sq);
        for(const auto& anchor:ref.vertices){const auto* s=surface_for(p,anchor.surface);const auto* v=vertex_for(*s,anchor.vertex);
            if(std::abs(std::hypot(v->point.y-y-cy,v->point.z-z-cz)-radius)>.015)return false;}
        y+=cy;z+=cz;
    }
    result={float(x),float(y),float(z)};return finite(result);
}
bool to_world(const AssetRead& a,const SightGeometryRecipe& p,const SightGeometryReference& ref,
    Vec3f model_reference,Vec3f& result) noexcept {
    Basis3f bind{},evaluated{};const auto bone=ref.owner_bone;
    if(!bone_basis(p.binds[bone],bind)||!bone_basis(a.evaluated[bone],evaluated))return false;
    const Vec3f d=subtract(model_reference,p.binds[bone].translation);
    const Vec3f local{dot(d,bind.forward),dot(d,bind.left),dot(d,bind.up)};
    result=add(add(a.origin,a.evaluated[bone].translation),compose(evaluated,local));return finite(result);
}
} // namespace

std::uint64_t sight_geometry_recipe_fingerprint(const SightGeometryRecipe& p) noexcept {
    if(!recipe_valid(p))return 0;
    std::uint64_t hash=kFnvOffset;string_hash(hash,p.id);string_hash(hash,p.model);
    const auto bone_count=static_cast<std::uint32_t>(p.binds.size());value(hash,bone_count);
    for(std::size_t i=0;i<p.binds.size();++i){string_hash(hash,p.bone_names[i]);
        bytes(hash,&p.binds[i],sizeof(SightGeometryBone));if(i)value(hash,p.parent_offsets[i-1]);}
    value(hash,p.surface_count);value(hash,p.root_bone);value(hash,p.flash_bone);
    const auto surface_count=static_cast<std::uint32_t>(p.surfaces.size());value(hash,surface_count);
    for(const auto& s:p.surfaces){value(hash,s.index);value(hash,s.vertex_count);value(hash,s.triangle_count);string_hash(hash,s.material);
        const auto ranges=static_cast<std::uint32_t>(s.rigid_ranges.size());value(hash,ranges);
        for(const auto& r:s.rigid_ranges){value(hash,r.bone);value(hash,r.vertices);value(hash,r.first_triangle);value(hash,r.triangles);}
        const auto vertices=static_cast<std::uint32_t>(s.vertices.size());value(hash,vertices);
        for(const auto& v:s.vertices){value(hash,v.index);bytes(hash,&v.point,sizeof(Vec3f));}
        const auto triangles=static_cast<std::uint32_t>(s.triangles.size());value(hash,triangles);
        for(const auto& t:s.triangles){value(hash,t.index);bytes(hash,t.vertices.data(),sizeof(t.vertices));}
    }
    for(const auto* ref:{&p.front,&p.rear}){value(hash,ref->owner_bone);value(hash,ref->method);
        const auto count=static_cast<std::uint32_t>(ref->vertices.size());value(hash,count);
        for(const auto& v:ref->vertices){value(hash,v.surface);value(hash,v.vertex);value(hash,v.weight);}}
    value(hash,p.basis_kind);
    return hash;
}
const SightGeometryProfile* find_sight_geometry_profile(std::span<const SightGeometryProfile> profiles,
    std::string_view weapon,std::string_view model) noexcept {
    if(profiles.size()>512||!string_valid(weapon)||!string_valid(model))return nullptr;
    const SightGeometryProfile* match{};
    for(const auto& p:profiles)if(p.weapon==weapon&&p.model==model){if(match)return nullptr;match=&p;}
    return match;
}
SightGeometryStatus derive_sight_geometry_basis(const SightGeometryReader& reader,const SightGeometryRequest& request,
    std::span<const SightGeometryProfile> profiles,Basis3f* output,SightGeometryProof* proof) noexcept {
    const auto* profile=find_sight_geometry_profile(profiles,request.weapon,request.model);
    if(!profile)return SightGeometryStatus::not_applicable;
    if(!profile->recipe||profile->recipe->model!=profile->model)return SightGeometryStatus::invalid_recipe;
    const auto& p=*profile->recipe;const auto fingerprint=sight_geometry_recipe_fingerprint(p);
    if(!fingerprint||!p.fingerprint||fingerprint!=p.fingerprint)return SightGeometryStatus::invalid_recipe;
    if(!output||!request.definition||!request.dobj||!request.native_skeleton_origin_address||!request.script_string_pool_address)
        return SightGeometryStatus::identity;
    if(!request.controller_generation||!request.controller_publication_milliseconds||!request.now_milliseconds||
        request.controller_publication_milliseconds>request.now_milliseconds||
        request.now_milliseconds-request.controller_publication_milliseconds>kMaximumPublishedMuzzleAgeMilliseconds)
        return SightGeometryStatus::stale;
    if(!valid_axis(request.evaluated_flash_basis)||!finite(request.evaluated_flash_world))return SightGeometryStatus::invalid_axis;
    if(!request.evaluated_flash_world_valid)return SightGeometryStatus::unevaluated;
    AssetRead first{},second{};
    auto status=read_asset(reader,request,*profile,first);if(status!=SightGeometryStatus::accepted)return status;
    status=read_asset(reader,request,*profile,second);if(status!=SightGeometryStatus::accepted)return status;
    if(!same(first,second))return SightGeometryStatus::unstable;
    SightGeometryProof measured{};measured.profile=profile;measured.recipe=&p;measured.definition=request.definition;
    measured.dobj=request.dobj;measured.model=first.model_address;measured.skeleton_timestamp=first.timestamp;
    measured.root_bone=first.preceding_bones+p.root_bone;measured.front_bone=first.preceding_bones+p.front.owner_bone;
    measured.rear_bone=first.preceding_bones+p.rear.owner_bone;measured.flash_bone=first.preceding_bones+p.flash_bone;
    measured.fingerprint=fingerprint;measured.controller_generation=request.controller_generation;
    measured.basis_kind=p.basis_kind;
    measured.publication_milliseconds=request.controller_publication_milliseconds;
    if(!authored_reference(p,p.front,measured.front_reference)||!authored_reference(p,p.rear,measured.rear_reference))
        return SightGeometryStatus::geometry;
    if(!bone_basis(first.evaluated[p.root_bone],measured.evaluated_root_basis)||
        !to_world(first,p,p.front,measured.front_reference,measured.world_front)||
        !to_world(first,p,p.rear,measured.rear_reference,measured.world_rear))return SightGeometryStatus::invalid_axis;
    const Vec3f direction=subtract(measured.world_front,measured.world_rear);
    const float length_sq=dot(direction,direction);
    if(!std::isfinite(length_sq)||length_sq<.01F||length_sq>10000.F)return SightGeometryStatus::geometry;
    Basis3f result{};
    if(!build_evaluated_projectile_basis(direction,request.evaluated_flash_basis,&result))return SightGeometryStatus::invalid_axis;
    *output=result;if(proof)*proof=measured;return SightGeometryStatus::accepted;
}
const char* sight_geometry_status_name(SightGeometryStatus s) noexcept {
    switch(s){
    case SightGeometryStatus::accepted:return "accepted";case SightGeometryStatus::not_applicable:return "not-applicable";
    case SightGeometryStatus::invalid_recipe:return "invalid-recipe";case SightGeometryStatus::inaccessible:return "inaccessible";
    case SightGeometryStatus::identity:return "identity";case SightGeometryStatus::binding:return "binding";
    case SightGeometryStatus::geometry:return "geometry";case SightGeometryStatus::hidden:return "hidden";
    case SightGeometryStatus::unevaluated:return "unevaluated";case SightGeometryStatus::stale:return "stale";
    case SightGeometryStatus::unstable:return "unstable";case SightGeometryStatus::invalid_axis:return "invalid-axis";
    case SightGeometryStatus::world_position_mismatch:return "world-position-mismatch";
    }return "unknown";
}
} // namespace wawvr::mod
