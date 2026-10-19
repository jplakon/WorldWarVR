// SPDX-License-Identifier: GPL-3.0-only
#include "calibrated_pistol_basis.hpp"
#include "calibrated_pistol_recipe.hpp"
#include "sight_bind_transform.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace wawvr::mod {
namespace {
using wawvr::xr::Vec3f; using wawvr::xr::Basis3f;
using pistol_detail::PistolBone;
template<class T> bool get(const CalibratedPistolReader& r,std::uintptr_t base,
    std::size_t offset,T& value) noexcept {
    constexpr auto maximum=(std::numeric_limits<std::uintptr_t>::max)();
    return base&&offset<=maximum-base&&sizeof(T)<=maximum-(base+offset)&&r.read&&
        r.read(r.context,base+offset,&value,sizeof(value));
}
template<class T,std::size_t N> T at(const std::array<std::byte,N>& data,std::size_t offset) noexcept {
    T value{}; if (offset<=N&&sizeof(T)<=N-offset) std::memcpy(&value,data.data()+offset,sizeof(T));
    return value;
}
bool text(const CalibratedPistolReader& r,std::uint32_t address,const char* expected) noexcept {
    std::array<char,64> actual{}; const auto count=std::strlen(expected)+1;
    return address&&count<=actual.size()&&count<=(std::numeric_limits<std::uintptr_t>::max)()-address&&
        r.read&&r.read(r.context,address,actual.data(),count)&&std::memcmp(actual.data(),expected,count)==0;
}
bool finite(Vec3f p) noexcept { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
float dot(Vec3f a,Vec3f b) noexcept { return a.x*b.x+a.y*b.y+a.z*b.z; }
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
bool bone_basis(const PistolBone& b,Basis3f& output) noexcept {
    float norm{};
    for (float q:b.quaternion) { if (!std::isfinite(q)) return false; norm+=q*q; }
    if (!finite(b.translation)||!std::isfinite(b.weight)||std::abs(norm-1.F)>.002F||
        b.weight<1.9F||b.weight>2.1F||std::abs(b.weight-2.F/norm)>.0001F) return false;
    const float x=b.quaternion[0],y=b.quaternion[1],z=b.quaternion[2],w=b.quaternion[3],s=b.weight;
    const Basis3f axis{{1-s*(y*y+z*z),s*(x*y+z*w),s*(x*z-y*w)},
        {s*(x*y-z*w),1-s*(x*x+z*z),s*(y*z+x*w)},
        {s*(x*z+y*w),s*(y*z-x*w),1-s*(x*x+y*y)}};
    if (!valid(axis)) return false;
    output=axis; return true;
}
void append(std::uint64_t& hash,Vec3f p) noexcept {
    const auto* bytes=reinterpret_cast<const unsigned char*>(&p);
    for (unsigned i=0;i<sizeof(p);++i) hash=(hash^bytes[i])*1099511628211ULL;
}
struct AssetRead final {
    std::array<std::byte,16> definition{};
    std::array<std::byte,0x68> dobj{};
    std::array<std::byte,0xE4> model{};
    std::array<std::uint32_t,2> models{};
    std::array<std::uint16_t,7> bone_ids{};
    std::array<std::uint8_t,6> parents{};
    std::array<PistolBone,7> binds{};
    std::array<PistolBone,3> evaluated{};
    std::array<std::byte,0x40> surface{};
    std::array<std::uint16_t,6> rigid{};
    std::array<Vec3f,12> anchors{};
    std::array<std::uint16_t,36> indices{};
    std::array<Vec3f,36> corners{};
    std::uint32_t pool{},material{},material_name{},model_address{},matrices{},timestamp{};
    std::uint8_t hands_bones{};
    std::uint64_t fingerprint{14695981039346656037ULL};
};
CalibratedPistolStatus read_asset(const CalibratedPistolReader& r,const CalibratedPistolRequest& request,
    AssetRead& a) noexcept {
    if (!get(r,request.definition,0,a.definition)||!get(r,request.dobj,0,a.dobj))
        return CalibratedPistolStatus::inaccessible;
    const auto model=at<std::uint32_t>(a.definition,12),table=at<std::uint32_t>(a.dobj,0x64);
    const auto total=at<std::uint8_t>(a.dobj,0x0A);
    if (at<std::uint8_t>(a.dobj,9)!=2||total!=77||!get(r,table,0,a.models)||
        !a.models[0]||!a.models[1]||a.models[1]!=model||
        !text(r,at<std::uint32_t>(a.definition,0),pistol_detail::kWeapon)||!get(r,model,0,a.model)||
        !text(r,at<std::uint32_t>(a.model,0),pistol_detail::kModel)) return CalibratedPistolStatus::identity;
    a.model_address=model;
    if (!get(r,a.models[0],4,a.hands_bones)||a.hands_bones!=70||
        at<std::uint8_t>(a.model,4)!=7||at<std::uint8_t>(a.model,5)!=1||
        at<std::uint8_t>(a.model,6)!=5||!get(r,0x03702390U,0,a.pool)) return CalibratedPistolStatus::binding;
    if (!get(r,at<std::uint32_t>(a.model,8),0,a.bone_ids)||
        !get(r,at<std::uint32_t>(a.model,12),0,a.parents)||
        !get(r,at<std::uint32_t>(a.model,0x1C),0,a.binds)) return CalibratedPistolStatus::inaccessible;
    for (unsigned i=0;i<7;++i) {
        const auto id=a.bone_ids[i];
        if (!id||a.pool>(std::numeric_limits<std::uint32_t>::max)()-id*12U-4U||
            !text(r,a.pool+id*12U+4U,pistol_detail::kBoneNames[i])||(i&&a.parents[i-1]!=i))
            return CalibratedPistolStatus::binding;
    }
    if (a.bone_ids[1]!=request.resolved_sight_tag) return CalibratedPistolStatus::binding;
    if (std::memcmp(a.binds.data(),pistol_detail::kBinds.data(),sizeof(a.binds)))
        return CalibratedPistolStatus::binding;
    const auto surfaces=at<std::uint32_t>(a.model,0x20),materials=at<std::uint32_t>(a.model,0x24);
    if (!get(r,surfaces,2*0x40,a.surface)||!get(r,materials,2*4,a.material)||
        !get(r,a.material,0,a.material_name)) return CalibratedPistolStatus::inaccessible;
    if (!text(r,a.material_name,pistol_detail::kMaterial)) return CalibratedPistolStatus::binding;
    if (at<std::uint8_t>(a.surface,1)||at<std::uint16_t>(a.surface,2)!=1326||
        at<std::uint16_t>(a.surface,4)!=1050||at<std::uint32_t>(a.surface,0x24)!=1)
        return CalibratedPistolStatus::geometry;
    if (!get(r,at<std::uint32_t>(a.surface,0x28),0,a.rigid)) return CalibratedPistolStatus::inaccessible;
    if (a.rigid[0]!=64||a.rigid[1]!=1326||a.rigid[2]!=0||a.rigid[3]!=1050)
        return CalibratedPistolStatus::geometry;
    const auto vertices=at<std::uint32_t>(a.surface,0x1C),indices=at<std::uint32_t>(a.surface,0x0C);
    for (unsigned i=0;i<pistol_detail::kAnchors.size();++i) {
        const auto& expected=pistol_detail::kAnchors[i];
        if (!get(r,vertices,expected.index*32U,a.anchors[i])) return CalibratedPistolStatus::inaccessible;
        if (!finite(a.anchors[i])||std::memcmp(&a.anchors[i],&expected.point,sizeof(Vec3f)))
            return CalibratedPistolStatus::geometry;
        append(a.fingerprint,a.anchors[i]);
    }
    for (unsigned i=0;i<pistol_detail::kTriangles.size();++i) {
        const auto& expected=pistol_detail::kTriangles[i];
        std::array<std::uint16_t,3> triangle{};
        if (!get(r,indices,expected.index*6U,triangle)) return CalibratedPistolStatus::inaccessible;
        if (triangle!=expected.indices) return CalibratedPistolStatus::geometry;
        for (unsigned j=0;j<3;++j) {
            a.indices[i*3+j]=triangle[j];
            if (!get(r,vertices,triangle[j]*32U,a.corners[i*3+j])) return CalibratedPistolStatus::inaccessible;
            if (!finite(a.corners[i*3+j])||std::memcmp(&a.corners[i*3+j],&expected.points[j],sizeof(Vec3f)))
                return CalibratedPistolStatus::geometry;
            append(a.fingerprint,a.corners[i*3+j]);
        }
    }
    if (a.fingerprint!=pistol_detail::kSmallFingerprint) return CalibratedPistolStatus::geometry;
    a.timestamp=at<std::uint32_t>(a.dobj,0x44); a.matrices=at<std::uint32_t>(a.dobj,0x48);
    if (!a.timestamp||!a.matrices) return CalibratedPistolStatus::unevaluated;
    constexpr std::array<unsigned,3> locals{0,1,6};
    for (unsigned i=0;i<locals.size();++i) {
        const unsigned global=a.hands_bones+locals[i],mask=0x80000000U>>(global&31U),word=(global>>5U)*4U;
        if (at<std::uint32_t>(a.dobj,0x50+word)&mask) return CalibratedPistolStatus::hidden;
        if (!(at<std::uint32_t>(a.dobj,0x34+word)&mask)) return CalibratedPistolStatus::unevaluated;
        if (!get(r,a.matrices,global*32U,a.evaluated[i])) return CalibratedPistolStatus::inaccessible;
        Basis3f checked{}; if (!bone_basis(a.evaluated[i],checked)) return CalibratedPistolStatus::invalid_axis;
    }
    Basis3f flash{};
    if (!bone_basis(a.evaluated[2],flash)||!near(flash.forward,request.evaluated_flash_basis.forward)||
        !near(flash.left,request.evaluated_flash_basis.left)||!near(flash.up,request.evaluated_flash_basis.up))
        return CalibratedPistolStatus::invalid_axis;
    Basis3f sight{};
    if (!bone_basis(a.evaluated[1],sight)||!near(sight.forward,request.evaluated_sight_basis.forward)||
        !near(sight.left,request.evaluated_sight_basis.left)||!near(sight.up,request.evaluated_sight_basis.up))
        return CalibratedPistolStatus::invalid_axis;
    return CalibratedPistolStatus::accepted;
}
bool same(const AssetRead& a,const AssetRead& b) noexcept {
    return a.definition==b.definition&&a.dobj==b.dobj&&a.model==b.model&&a.models==b.models&&
        a.bone_ids==b.bone_ids&&a.parents==b.parents&&a.surface==b.surface&&a.rigid==b.rigid&&a.indices==b.indices&&
        !std::memcmp(a.binds.data(),b.binds.data(),sizeof(a.binds))&&
        !std::memcmp(a.evaluated.data(),b.evaluated.data(),sizeof(a.evaluated))&&
        !std::memcmp(a.anchors.data(),b.anchors.data(),sizeof(a.anchors))&&
        !std::memcmp(a.corners.data(),b.corners.data(),sizeof(a.corners))&&
        a.pool==b.pool&&a.material==b.material&&a.material_name==b.material_name&&a.model_address==b.model_address&&
        a.matrices==b.matrices&&a.timestamp==b.timestamp&&a.hands_bones==b.hands_bones&&a.fingerprint==b.fingerprint;
}
Vec3f mean(Vec3f a,Vec3f b) noexcept {return {(a.x+b.x)*.5F,(a.y+b.y)*.5F,(a.z+b.z)*.5F};}
} // namespace

CalibratedPistolStatus derive_calibrated_pistol_basis(const CalibratedPistolReader& reader,
    const CalibratedPistolRequest& request,Basis3f* output,CalibratedPistolProof* proof) noexcept {
    if (request.weapon!=pistol_detail::kWeapon) return CalibratedPistolStatus::not_applicable;
    if (!output||!request.definition||!request.dobj) return CalibratedPistolStatus::identity;
    if (!request.controller_generation||!request.controller_publication_milliseconds||!request.now_milliseconds||
        request.controller_publication_milliseconds>request.now_milliseconds||
        request.now_milliseconds-request.controller_publication_milliseconds>kMaximumPublishedMuzzleAgeMilliseconds)
        return CalibratedPistolStatus::stale;
    if (!request.evaluated_sight_basis_valid||!request.resolved_sight_tag)
        return CalibratedPistolStatus::unevaluated;
    if (!valid(request.evaluated_flash_basis)||!valid(request.evaluated_sight_basis))
        return CalibratedPistolStatus::invalid_axis;
    AssetRead first{},second{};
    auto status=read_asset(reader,request,first); if (status!=CalibratedPistolStatus::accepted) return status;
    status=read_asset(reader,request,second); if (status!=CalibratedPistolStatus::accepted) return status;
    if (!same(first,second)) return CalibratedPistolStatus::unstable;
    CalibratedPistolProof measured{};
    measured.witness_fingerprint=first.fingerprint; measured.model=first.model_address;
    measured.sight_bone=first.hands_bones+1; measured.skeleton_timestamp=first.timestamp;
    measured.front_reference=mean(first.anchors[0],first.anchors[1]);
    measured.rear_reference=mean(first.anchors[4],first.anchors[5]);
    const Vec3f authored{measured.front_reference.x-measured.rear_reference.x,
        measured.front_reference.y-measured.rear_reference.y,measured.front_reference.z-measured.rear_reference.z};
    if (!inverse_sight_bind_vector(authored,first.binds[1].quaternion,first.binds[1].weight,
        &measured.local_direction)) return CalibratedPistolStatus::invalid_axis;
    const float square=dot(measured.local_direction,measured.local_direction);
    if (!std::isfinite(square)||square<1.e-8F) return CalibratedPistolStatus::geometry;
    const float inverse=1.F/std::sqrt(square);
    measured.local_direction={measured.local_direction.x*inverse,measured.local_direction.y*inverse,measured.local_direction.z*inverse};
    Basis3f sight{}; if (!bone_basis(first.evaluated[1],sight)) return CalibratedPistolStatus::invalid_axis;
    const auto d=measured.local_direction;
    const Vec3f world{sight.forward.x*d.x+sight.left.x*d.y+sight.up.x*d.z,
        sight.forward.y*d.x+sight.left.y*d.y+sight.up.y*d.z,sight.forward.z*d.x+sight.left.z*d.y+sight.up.z*d.z};
    Basis3f result{};
    if (!build_evaluated_projectile_basis(world,request.evaluated_flash_basis,&result))
        return CalibratedPistolStatus::invalid_axis;
    measured.world_direction=result.forward;
    *output=result; if (proof) *proof=measured; return CalibratedPistolStatus::accepted;
}
} // namespace wawvr::mod
