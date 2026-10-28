// SPDX-License-Identifier: GPL-3.0-only
#include "field_rifle_sight_basis.hpp"
#include "field_rifle_sight_recipe.hpp"
#include "weapon_ballistics_logic.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace wawvr::mod {
namespace {
using wawvr::xr::Vec3f; using wawvr::xr::Basis3f;
using field_rifle_detail::Bone; using field_rifle_detail::AssetWitness;
static_assert(sizeof(Vec3f)==12);
// Native T4 pointers are32-bit even when this read-only parser is unit-tested
// on a64-bit host. Reject wrap rather than allowing a widened fake address.
bool read(const FieldRifleReader& r,std::uintptr_t base,std::size_t offset,
    void* output,std::size_t count) noexcept {
    constexpr auto maximum=(std::numeric_limits<std::uint32_t>::max)();
    return base&&base<=maximum&&offset<=maximum-base&&count<=maximum-(base+offset)&&
        r.read&&r.read(r.context,base+offset,output,count);
}
template<class T> bool get(const FieldRifleReader& r,std::uintptr_t base,
    std::size_t offset,T& value) noexcept {return read(r,base,offset,&value,sizeof(value));}
template<class T,std::size_t N> T at(const std::array<std::byte,N>& bytes,std::size_t offset) noexcept {
    T value{};if(offset<=N&&sizeof(T)<=N-offset)std::memcpy(&value,bytes.data()+offset,sizeof(T));
    return value;
}
bool text(const FieldRifleReader& r,std::uint32_t address,const char* expected) noexcept {
    std::array<char,64> actual{};const auto count=std::strlen(expected)+1;
    return count<=actual.size()&&read(r,address,0,actual.data(),count)&&
        std::memcmp(actual.data(),expected,count)==0;
}
bool finite(Vec3f p) noexcept {return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
float dot(Vec3f a,Vec3f b) noexcept {return a.x*b.x+a.y*b.y+a.z*b.z;}
bool valid(Basis3f a) noexcept {
    const Vec3f cross{a.forward.y*a.left.z-a.forward.z*a.left.y,
        a.forward.z*a.left.x-a.forward.x*a.left.z,a.forward.x*a.left.y-a.forward.y*a.left.x};
    return finite(a.forward)&&finite(a.left)&&finite(a.up)&&
        std::abs(dot(a.forward,a.forward)-1.F)<.002F&&std::abs(dot(a.left,a.left)-1.F)<.002F&&
        std::abs(dot(a.up,a.up)-1.F)<.002F&&std::abs(dot(a.forward,a.left))<.002F&&
        std::abs(dot(a.forward,a.up))<.002F&&std::abs(dot(a.left,a.up))<.002F&&dot(cross,a.up)>.998F;
}
bool near(Vec3f a,Vec3f b) noexcept {
    return std::abs(a.x-b.x)<.00002F&&std::abs(a.y-b.y)<.00002F&&std::abs(a.z-b.z)<.00002F;
}
bool bone_basis(const Bone& b,Basis3f& output) noexcept {
    float norm{};for(float q:b.quaternion){if(!std::isfinite(q))return false;norm+=q*q;}
    if(!finite(b.translation)||!std::isfinite(b.weight)||std::abs(norm-1.F)>.002F||
        b.weight<1.9F||b.weight>2.1F||std::abs(b.weight-2.F/norm)>.0001F)return false;
    const float x=b.quaternion[0],y=b.quaternion[1],z=b.quaternion[2],w=b.quaternion[3],s=b.weight;
    const Basis3f axis{{1-s*(y*y+z*z),s*(x*y+z*w),s*(x*z-y*w)},
        {s*(x*y-z*w),1-s*(x*x+z*z),s*(y*z+x*w)},
        {s*(x*z+y*w),s*(y*z-x*w),1-s*(x*x+y*y)}};
    if(!valid(axis))return false;output=axis;return true;
}
void append(std::uint64_t& hash,const void* value,std::size_t count) noexcept {
    const auto* bytes=static_cast<const unsigned char*>(value);
    for(std::size_t i=0;i<count;++i)hash=(hash^bytes[i])*1099511628211ULL;
}
const AssetWitness* recipe(FieldRifleAsset asset) noexcept {
    if(asset==FieldRifleAsset::carbine)return &field_rifle_detail::kCarbine;
    if(asset==FieldRifleAsset::kar98)return &field_rifle_detail::kKar98;
    return nullptr;
}
struct AssetRead final {
    std::array<std::byte,16> definition{};std::array<std::byte,0x68> dobj{};
    std::array<std::byte,0xE4> model{};std::array<std::uint32_t,2> models{};
    std::array<std::uint16_t,7> bone_ids{};std::array<std::uint8_t,6> parents{};
    std::array<Bone,7> binds{};std::array<Bone,2> evaluated{};
    std::array<std::byte,0x40> surface{};std::array<std::uint16_t,6> rigid{};
    std::array<std::array<Vec3f,64>,3> points{};
    std::array<std::array<std::array<std::uint16_t,3>,64>,2> triangles{};
    std::uint32_t pool{},material{},material_name{},model_address{},matrices{},timestamp{};
    std::uint8_t hands_bones{};Vec3f origin{};
    std::uint64_t fingerprint{14695981039346656037ULL};
};
FieldRifleStatus read_asset(const FieldRifleReader& r,const FieldRifleRequest& request,
    const AssetWitness& p,AssetRead& a) noexcept {
    if(!get(r,request.definition,0,a.definition)||!get(r,request.dobj,0,a.dobj))
        return FieldRifleStatus::inaccessible;
    const auto model=at<std::uint32_t>(a.definition,12),table=at<std::uint32_t>(a.dobj,0x64);
    if(at<std::uint8_t>(a.dobj,9)!=2||at<std::uint8_t>(a.dobj,10)!=70+p.bones||
        !get(r,table,0,a.models)||!a.models[0]||!a.models[1]||a.models[1]!=model||
        !text(r,at<std::uint32_t>(a.definition,0),p.weapon)||!get(r,model,0,a.model)||
        !text(r,at<std::uint32_t>(a.model,0),p.model))return FieldRifleStatus::identity;
    a.model_address=model;
    if(!get(r,a.models[0],4,a.hands_bones)||a.hands_bones!=70||
        at<std::uint8_t>(a.model,4)!=p.bones||at<std::uint8_t>(a.model,5)!=1||
        at<std::uint8_t>(a.model,6)!=p.surfaces||!get(r,0x03702390U,0,a.pool))return FieldRifleStatus::binding;
    if(!read(r,at<std::uint32_t>(a.model,8),0,a.bone_ids.data(),p.bones*2U)||
        !read(r,at<std::uint32_t>(a.model,12),0,a.parents.data(),p.bones-1U)||
        !read(r,at<std::uint32_t>(a.model,0x1C),0,a.binds.data(),p.bones*32U))return FieldRifleStatus::inaccessible;
    for(unsigned i=0;i<p.bones;++i){
        const auto id=a.bone_ids[i];
        if(!id||a.pool>(std::numeric_limits<std::uint32_t>::max)()-id*12U-4U||
            !text(r,a.pool+id*12U+4U,p.bone_names[i])||(i&&a.parents[i-1]!=i))return FieldRifleStatus::binding;
    }
    if(std::memcmp(a.binds.data(),p.binds.data(),p.bones*32U))return FieldRifleStatus::binding;
    append(a.fingerprint,a.binds.data(),p.bones*32U);
    if(!get(r,at<std::uint32_t>(a.model,0x20),p.surface*0x40U,a.surface)||
        !get(r,at<std::uint32_t>(a.model,0x24),p.surface*4U,a.material)||
        !get(r,a.material,0,a.material_name))return FieldRifleStatus::inaccessible;
    if(!text(r,a.material_name,p.material))return FieldRifleStatus::binding;
    if(at<std::uint8_t>(a.surface,1)||at<std::uint16_t>(a.surface,2)!=p.vertices||
        at<std::uint16_t>(a.surface,4)!=p.triangles||at<std::uint32_t>(a.surface,0x24)!=1)
        return FieldRifleStatus::geometry;
    if(!get(r,at<std::uint32_t>(a.surface,0x28),0,a.rigid))return FieldRifleStatus::inaccessible;
    if(a.rigid[0]!=0||a.rigid[1]!=p.vertices||a.rigid[2]!=0||a.rigid[3]!=p.rigid_triangles)
        return FieldRifleStatus::geometry;
    const auto vertices=at<std::uint32_t>(a.surface,0x1C),indices=at<std::uint32_t>(a.surface,0x0C);
    for(unsigned block=0;block<p.vertex_blocks.size();++block){
        const auto& spec=p.vertex_blocks[block];if(!spec.count)continue;
        std::array<std::byte,64*32> bytes{};
        if(spec.count>64||spec.first>=p.vertices||spec.count>p.vertices-spec.first||
            !read(r,vertices,spec.first*32U,bytes.data(),spec.count*32U))return FieldRifleStatus::inaccessible;
        for(unsigned i=0;i<spec.count;++i){
            auto& point=a.points[block][i];std::memcpy(&point,bytes.data()+i*32U,sizeof(point));
            if(!finite(point)||std::memcmp(&point,&spec.points[i],sizeof(point)))return FieldRifleStatus::geometry;
            append(a.fingerprint,&point,sizeof(point));
        }
    }
    for(unsigned block=0;block<p.triangle_blocks.size();++block){
        const auto& spec=p.triangle_blocks[block];if(!spec.count)continue;
        auto& triangles=a.triangles[block];
        if(spec.count>64||spec.first>=p.rigid_triangles||spec.count>p.rigid_triangles-spec.first||
            !read(r,indices,spec.first*6U,triangles.data(),spec.count*6U))return FieldRifleStatus::inaccessible;
        if(std::memcmp(triangles.data(),spec.triangles.data(),spec.count*6U))return FieldRifleStatus::geometry;
        append(a.fingerprint,triangles.data(),spec.count*6U);
    }
    if(a.fingerprint!=p.fingerprint)return FieldRifleStatus::geometry;
    a.timestamp=at<std::uint32_t>(a.dobj,0x44);a.matrices=at<std::uint32_t>(a.dobj,0x48);
    if(!a.timestamp||!a.matrices)return FieldRifleStatus::unevaluated;
    const std::array<unsigned,2> locals{0U,unsigned(p.bones)-1U};
    for(unsigned i=0;i<locals.size();++i){
        const unsigned global=70U+locals[i],mask=0x80000000U>>(global&31U),word=(global>>5U)*4U;
        if(at<std::uint32_t>(a.dobj,0x50+word)&mask)return FieldRifleStatus::hidden;
        if(!(at<std::uint32_t>(a.dobj,0x34+word)&mask))return FieldRifleStatus::unevaluated;
        if(!get(r,a.matrices,global*32U,a.evaluated[i]))return FieldRifleStatus::inaccessible;
        Basis3f checked{};if(!bone_basis(a.evaluated[i],checked))return FieldRifleStatus::invalid_axis;
    }
    Basis3f flash{};
    if(!bone_basis(a.evaluated[1],flash)||!near(flash.forward,request.evaluated_flash_basis.forward)||
        !near(flash.left,request.evaluated_flash_basis.left)||!near(flash.up,request.evaluated_flash_basis.up))
        return FieldRifleStatus::invalid_axis;
    Basis3f root{};if(!bone_basis(a.evaluated[0],root))return FieldRifleStatus::invalid_axis;
    if(!near(root.forward,flash.forward)||!near(root.left,flash.left)||!near(root.up,flash.up))
        return FieldRifleStatus::unstable;
    // Both exact authored tag_flash binds have identity rotation and are
    // parented directly to j_gun. A mixed/stale evaluated pair cannot qualify.
    const auto local=p.binds[p.bones-1].translation;
    const Vec3f flash_expected{a.evaluated[0].translation.x+root.forward.x*local.x+root.left.x*local.y+root.up.x*local.z,
        a.evaluated[0].translation.y+root.forward.y*local.x+root.left.y*local.y+root.up.y*local.z,
        a.evaluated[0].translation.z+root.forward.z*local.x+root.left.z*local.y+root.up.z*local.z};
    const auto observed=a.evaluated[1].translation;
    if(std::abs(flash_expected.x-observed.x)>.005F||std::abs(flash_expected.y-observed.y)>.005F||
        std::abs(flash_expected.z-observed.z)>.005F)return FieldRifleStatus::unstable;
    if(!get(r,request.native_skeleton_origin_address,0,a.origin))return FieldRifleStatus::inaccessible;
    if(!finite(a.origin))return FieldRifleStatus::invalid_axis;
    const auto expected=request.evaluated_flash_world_position;
    const Vec3f actual{a.origin.x+a.evaluated[1].translation.x,a.origin.y+a.evaluated[1].translation.y,
        a.origin.z+a.evaluated[1].translation.z};
    if(!finite(actual)||std::abs(actual.x-expected.x)>.005F||std::abs(actual.y-expected.y)>.005F||
        std::abs(actual.z-expected.z)>.005F)return FieldRifleStatus::world_position_mismatch;
    return FieldRifleStatus::accepted;
}
bool same(const AssetRead& a,const AssetRead& b) noexcept {
    return a.definition==b.definition&&a.dobj==b.dobj&&a.model==b.model&&a.models==b.models&&
        a.bone_ids==b.bone_ids&&a.parents==b.parents&&a.surface==b.surface&&a.rigid==b.rigid&&a.triangles==b.triangles&&
        !std::memcmp(a.binds.data(),b.binds.data(),sizeof(a.binds))&&
        !std::memcmp(a.evaluated.data(),b.evaluated.data(),sizeof(a.evaluated))&&
        !std::memcmp(a.points.data(),b.points.data(),sizeof(a.points))&&
        !std::memcmp(&a.origin,&b.origin,sizeof(a.origin))&&a.pool==b.pool&&a.material==b.material&&
        a.material_name==b.material_name&&a.model_address==b.model_address&&a.matrices==b.matrices&&
        a.timestamp==b.timestamp&&a.hands_bones==b.hands_bones&&a.fingerprint==b.fingerprint;
}
Vec3f point(const AssetRead& a,const AssetWitness& p,unsigned index) noexcept {
    for(unsigned i=0;i<p.vertex_blocks.size();++i){const auto& b=p.vertex_blocks[i];
        if(index>=b.first&&index-b.first<b.count)return a.points[i][index-b.first];}
    return {};
}
Vec3f mean(Vec3f a,Vec3f b) noexcept {return {(a.x+b.x)*.5F,(a.y+b.y)*.5F,(a.z+b.z)*.5F};}
bool references(const AssetRead& a,const AssetWitness& p,Vec3f& front,Vec3f& rear) noexcept {
    if(p.asset==FieldRifleAsset::kar98){
        front=point(a,p,1119);rear=mean(point(a,p,1735),point(a,p,1740));
        return point(a,p,1737).z<rear.z&&point(a,p,1116).z<front.z&&point(a,p,1120).z<front.z;
    }
    const auto& ring=field_rifle_detail::kCarbineRing;
    // A real closed aperture, not an average of the protective outer ears.
    std::array<std::array<bool,32>,32> edges{};
    for(const auto& b:a.triangles)for(const auto& t:b)for(unsigned e=0;e<3;++e){
        unsigned ia=32,ib=32;
        for(unsigned i=0;i<ring.size();++i){if(t[e]==ring[i])ia=i;if(t[(e+1)%3]==ring[i])ib=i;}
        if(ia<32&&ib<32&&ia!=ib)edges[ia][ib]=edges[ib][ia]=true;
    }
    std::array<bool,32> reached{};reached[0]=true;
    for(unsigned pass=0;pass<32;++pass)for(unsigned i=0;i<32;++i)if(reached[i])
        for(unsigned j=0;j<32;++j)if(edges[i][j])reached[j]=true;
    double x{},y{},z{};
    for(unsigned i=0;i<32;++i){unsigned degree{};for(bool edge:edges[i])degree+=unsigned(edge);
        if(degree!=2||!reached[i])return false;
        const auto q=point(a,p,ring[i]);x+=q.x;y+=q.y;z+=q.z;}
    rear={float(x/32.),float(y/32.),float(z/32.)};front=mean(point(a,p,1938),point(a,p,1947));
    return finite(front)&&finite(rear);
}
Vec3f world_point(const AssetRead& a,const Basis3f& basis,Vec3f local) noexcept {
    return {a.origin.x+a.evaluated[0].translation.x+basis.forward.x*local.x+basis.left.x*local.y+basis.up.x*local.z,
        a.origin.y+a.evaluated[0].translation.y+basis.forward.y*local.x+basis.left.y*local.y+basis.up.y*local.z,
        a.origin.z+a.evaluated[0].translation.z+basis.forward.z*local.x+basis.left.z*local.y+basis.up.z*local.z};
}
} // namespace
FieldRifleAsset field_rifle_asset_for_weapon(std::string_view weapon) noexcept {
    if(weapon==field_rifle_detail::kCarbine.weapon)return FieldRifleAsset::carbine;
    if(weapon==field_rifle_detail::kKar98.weapon)return FieldRifleAsset::kar98;
    return FieldRifleAsset::none;
}
std::string_view field_rifle_model_for_asset(FieldRifleAsset asset) noexcept {
    const auto* p=recipe(asset);return p?std::string_view(p->model):std::string_view{};
}
std::string_view field_rifle_recipe_id(FieldRifleAsset asset) noexcept {
    const auto* p=recipe(asset);return p?std::string_view(p->recipe):std::string_view{};
}
FieldRifleStatus derive_field_rifle_sight_basis(const FieldRifleReader& reader,
    const FieldRifleRequest& request,Basis3f* output,FieldRifleProof* proof) noexcept {
    const auto asset=field_rifle_asset_for_weapon(request.weapon);const auto* p=recipe(asset);
    if(!p)return FieldRifleStatus::not_applicable;
    if(!output||!request.definition||!request.dobj||!request.native_skeleton_origin_address)return FieldRifleStatus::identity;
    if(!request.controller_generation||!request.controller_publication_milliseconds||!request.now_milliseconds||
        request.controller_publication_milliseconds>request.now_milliseconds||
        request.now_milliseconds-request.controller_publication_milliseconds>kMaximumPublishedMuzzleAgeMilliseconds)
        return FieldRifleStatus::stale;
    if(!valid(request.evaluated_flash_basis))return FieldRifleStatus::invalid_axis;
    if(!request.evaluated_flash_world_position_valid)return FieldRifleStatus::unevaluated;
    if(!finite(request.evaluated_flash_world_position))return FieldRifleStatus::invalid_axis;
    AssetRead first{},second{};
    auto status=read_asset(reader,request,*p,first);if(status!=FieldRifleStatus::accepted)return status;
    status=read_asset(reader,request,*p,second);if(status!=FieldRifleStatus::accepted)return status;
    if(!same(first,second))return FieldRifleStatus::unstable;
    FieldRifleProof measured{};measured.asset=asset;measured.model=first.model_address;
    measured.root_bone=70;measured.skeleton_timestamp=first.timestamp;measured.fingerprint=first.fingerprint;
    if(!references(first,*p,measured.front_reference,measured.rear_reference))return FieldRifleStatus::geometry;
    if(!bone_basis(first.evaluated[0],measured.evaluated_root_basis))return FieldRifleStatus::invalid_axis;
    const Vec3f d{measured.front_reference.x-measured.rear_reference.x,
        measured.front_reference.y-measured.rear_reference.y,measured.front_reference.z-measured.rear_reference.z};
    const float square=dot(d,d);if(!std::isfinite(square)||square<1.e-8F)return FieldRifleStatus::geometry;
    const float inverse=1.F/std::sqrt(square);measured.local_direction={d.x*inverse,d.y*inverse,d.z*inverse};
    measured.world_front=world_point(first,measured.evaluated_root_basis,measured.front_reference);
    measured.world_rear=world_point(first,measured.evaluated_root_basis,measured.rear_reference);
    if(!finite(measured.world_front)||!finite(measured.world_rear))return FieldRifleStatus::invalid_axis;
    const auto& w=measured.evaluated_root_basis;const auto& l=measured.local_direction;
    const Vec3f direction{w.forward.x*l.x+w.left.x*l.y+w.up.x*l.z,
        w.forward.y*l.x+w.left.y*l.y+w.up.y*l.z,w.forward.z*l.x+w.left.z*l.y+w.up.z*l.z};
    Basis3f result{};if(!build_evaluated_projectile_basis(direction,request.evaluated_flash_basis,&result))
        return FieldRifleStatus::invalid_axis;
    *output=result;if(proof)*proof=measured;return FieldRifleStatus::accepted;
}
} // namespace wawvr::mod
