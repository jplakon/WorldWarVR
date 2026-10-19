// SPDX-License-Identifier: GPL-3.0-only
#include "calibrated_rifle_basis.hpp"
#include "calibrated_rifle_recipe.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace wawvr::mod {
namespace {
using wawvr::xr::Vec3f; using wawvr::xr::Basis3f;
static_assert(sizeof(Vec3f)==12);
template<class T> bool get(const CalibratedRifleReader& r,std::uintptr_t base,
    std::size_t offset,T& value) noexcept {
    constexpr auto maximum=(std::numeric_limits<std::uintptr_t>::max)();
    return base&&offset<=maximum-base&&sizeof(T)<=maximum-(base+offset)&&r.read&&
        r.read(r.context,base+offset,&value,sizeof(value));
}
template<class T,std::size_t N> T at(const std::array<std::byte,N>& data,std::size_t offset) noexcept {
    T result{}; if (offset<=N&&sizeof(T)<=N-offset) std::memcpy(&result,data.data()+offset,sizeof(T));
    return result;
}
template<std::size_t N> bool exact_text(const CalibratedRifleReader& r,std::uint32_t address,
    const char* expected,std::array<char,N>& output) noexcept {
    const auto length=std::strlen(expected)+1;
    return address&&length<=N&&length<=(std::numeric_limits<std::uintptr_t>::max)()-address&&
        r.read&&r.read(r.context,address,output.data(),length)&&std::memcmp(output.data(),expected,length)==0;
}
bool finite(Vec3f p) noexcept { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
float dot(Vec3f a,Vec3f b) noexcept { return a.x*b.x+a.y*b.y+a.z*b.z; }
bool valid_axis(Basis3f a) noexcept {
    return finite(a.forward)&&finite(a.left)&&finite(a.up)&&std::abs(dot(a.forward,a.forward)-1.F)<.002F&&
        std::abs(dot(a.left,a.left)-1.F)<.002F&&std::abs(dot(a.up,a.up)-1.F)<.002F&&
        std::abs(dot(a.forward,a.left))<.002F&&std::abs(dot(a.forward,a.up))<.002F&&std::abs(dot(a.left,a.up))<.002F;
}
void append(std::uint64_t& hash,const Vec3f& p) noexcept {
    const auto* bytes=reinterpret_cast<const unsigned char*>(&p);
    for (std::size_t i=0;i<sizeof(p);++i) hash=(hash^bytes[i])*1099511628211ULL;
}
struct MeshRead final {
    std::array<std::byte,0x40> header{};
    std::array<std::byte,36> rigid{};
    std::array<Vec3f,14> anchors{};
    std::array<std::uint16_t,21> indices{};
    std::array<Vec3f,21> corners{};
};
struct AssetRead final {
    std::array<std::byte,16> definition{};
    std::array<std::byte,0x68> dobj{};
    std::array<std::byte,0xE4> model{};
    std::array<std::uint32_t,2> models{};
    std::array<std::uint16_t,9> bone_ids{};
    std::array<std::uint8_t,8> parents{};
    std::array<std::byte,9*32> binds{};
    std::array<MeshRead,2> meshes{};
    std::uint32_t pool{};
    std::uint8_t hands_bones{};
    std::uint64_t fingerprint{14695981039346656037ULL};
};
bool bind_matches(const AssetRead& a,std::uint32_t bone,Vec3f translation) noexcept {
    const auto q=at<std::array<float,4>>(a.binds,bone*32);
    const auto p=at<Vec3f>(a.binds,bone*32+16);
    return q==std::array{0.F,0.F,0.F,1.F}&&std::memcmp(&p,&translation,sizeof(p))==0;
}
CalibratedRifleStatus read_asset(const CalibratedRifleReader& r,const CalibratedRifleRequest& request,
    const rifle_detail::AssetWitness& p,AssetRead& a) noexcept {
    if (!get(r,request.definition,0,a.definition)||!get(r,request.dobj,0,a.dobj))
        return CalibratedRifleStatus::inaccessible;
    const auto model=at<std::uint32_t>(a.definition,12),table=at<std::uint32_t>(a.dobj,0x64);
    std::array<char,64> weapon{},model_name{};
    const auto total=at<std::uint8_t>(a.dobj,0x0A);
    if (at<std::uint8_t>(a.dobj,9)!=2||total<8||total>128||!get(r,table,0,a.models)||
        !a.models[0]||!a.models[1]||a.models[1]!=model||
        !exact_text(r,at<std::uint32_t>(a.definition,0),p.weapon,weapon)||!get(r,model,0,a.model)||
        !exact_text(r,at<std::uint32_t>(a.model,0),p.model,model_name)||
        calibrated_rifle_asset_for_identity(request.weapon,model_name.data())!=p.asset)
        return CalibratedRifleStatus::identity;
    if (!get(r,a.models[0],4,a.hands_bones)||!a.hands_bones||a.hands_bones+p.bones!=total||
        at<std::uint8_t>(a.model,4)!=p.bones||at<std::uint8_t>(a.model,5)!=1||
        at<std::uint8_t>(a.model,6)!=p.surfaces||!get(r,0x03702390U,0,a.pool))
        return CalibratedRifleStatus::binding;
    const auto names=at<std::uint32_t>(a.model,8),parents=at<std::uint32_t>(a.model,12),
        binds=at<std::uint32_t>(a.model,0x1C);
    if (!r.read||!names||!parents||!binds||
        p.bones*2>(std::numeric_limits<std::uintptr_t>::max)()-names||
        p.bones-1>(std::numeric_limits<std::uintptr_t>::max)()-parents||
        p.bones*32>(std::numeric_limits<std::uintptr_t>::max)()-binds||
        !r.read(r.context,names,a.bone_ids.data(),p.bones*2)||
        !r.read(r.context,parents,a.parents.data(),p.bones-1)||
        !r.read(r.context,binds,a.binds.data(),p.bones*32)) return CalibratedRifleStatus::inaccessible;
    for (unsigned i=0;i<p.bones;++i) {
        std::array<char,32> name{};
        const auto id=a.bone_ids[i];
        if (!id||a.pool>(std::numeric_limits<std::uint32_t>::max)()-id*12U-4U||
            !exact_text(r,a.pool+id*12U+4U,p.bone_names[i],name)||(i&&a.parents[i-1]!=i))
            return CalibratedRifleStatus::binding;
    }
    if (!bind_matches(a,0,{})||!bind_matches(a,p.sight_bone,p.sight_bind)||
        !bind_matches(a,p.flash_bone,p.flash_bind)) return CalibratedRifleStatus::binding;
    const auto surfaces=at<std::uint32_t>(a.model,0x20);
    for (std::size_t mesh=0;mesh<p.meshes.size();++mesh) {
        const auto& spec=p.meshes[mesh]; auto& observed=a.meshes[mesh];
        if (!get(r,surfaces,spec.surface*0x40,observed.header)) return CalibratedRifleStatus::inaccessible;
        if (at<std::uint8_t>(observed.header,1)!=0||at<std::uint16_t>(observed.header,2)!=spec.vertices||
            at<std::uint16_t>(observed.header,4)!=spec.triangles||
            at<std::uint32_t>(observed.header,0x24)!=spec.ranges.size()) return CalibratedRifleStatus::geometry;
        const auto rigid=at<std::uint32_t>(observed.header,0x28);
        if (!rigid||spec.ranges.size()*12>(std::numeric_limits<std::uintptr_t>::max)()-rigid||
            !r.read(r.context,rigid,observed.rigid.data(),spec.ranges.size()*12))
            return CalibratedRifleStatus::inaccessible;
        for (std::size_t range=0;range<spec.ranges.size();++range) {
            const auto& expected=spec.ranges[range]; const auto off=range*12;
            if (at<std::uint16_t>(observed.rigid,off)!=expected.bone*64||
                at<std::uint16_t>(observed.rigid,off+2)!=expected.vertices||
                at<std::uint16_t>(observed.rigid,off+4)!=expected.first_triangle||
                at<std::uint16_t>(observed.rigid,off+6)!=expected.triangles) return CalibratedRifleStatus::geometry;
        }
        const auto vertices=at<std::uint32_t>(observed.header,0x1C),indices=at<std::uint32_t>(observed.header,0x0C);
        for (std::size_t i=0;i<spec.anchors.size();++i) {
            const auto index=spec.anchors[i].index;
            if (index<spec.owned_first||index-spec.owned_first>=spec.owned_count||
                !get(r,vertices,index*32,observed.anchors[i])||!finite(observed.anchors[i]))
                return CalibratedRifleStatus::geometry;
            append(a.fingerprint,observed.anchors[i]);
        }
        for (std::size_t i=0;i<spec.topology.size();++i) {
            std::array<std::uint16_t,3> triangle{};
            if (spec.topology[i].index>=spec.triangles||!get(r,indices,spec.topology[i].index*6,triangle))
                return CalibratedRifleStatus::geometry;
            for (unsigned j=0;j<3;++j) {
                const auto index=triangle[j]; observed.indices[i*3+j]=index;
                if (index<spec.owned_first||index-spec.owned_first>=spec.owned_count||
                    !get(r,vertices,index*32,observed.corners[i*3+j])||!finite(observed.corners[i*3+j]))
                    return CalibratedRifleStatus::geometry;
                append(a.fingerprint,observed.corners[i*3+j]);
            }
        }
    }
    return a.fingerprint==p.fingerprint?CalibratedRifleStatus::accepted:CalibratedRifleStatus::geometry;
}
bool same_read(const AssetRead& a,const AssetRead& b) noexcept {
    if (a.definition!=b.definition||a.dobj!=b.dobj||a.model!=b.model||a.models!=b.models||
        a.bone_ids!=b.bone_ids||a.parents!=b.parents||a.binds!=b.binds||a.pool!=b.pool||
        a.hands_bones!=b.hands_bones||a.fingerprint!=b.fingerprint) return false;
    for (unsigned i=0;i<2;++i) {
        const auto& x=a.meshes[i]; const auto& y=b.meshes[i];
        if (x.header!=y.header||x.rigid!=y.rigid||x.indices!=y.indices||
            std::memcmp(x.anchors.data(),y.anchors.data(),sizeof(x.anchors))||
            std::memcmp(x.corners.data(),y.corners.data(),sizeof(x.corners))) return false;
    }
    return true;
}
Vec3f mean_pair(Vec3f a,Vec3f b) noexcept { return {(a.x+b.x)*.5F,(a.y+b.y)*.5F,(a.z+b.z)*.5F}; }
bool aperture_center(const std::array<Vec3f,14>& points,Vec3f& center) noexcept {
    double x{},guess_y{},guess_z{};
    for (unsigned i=0;i<4;++i) { x+=points[i].x; guess_y+=points[i].y; guess_z+=points[i].z; }
    x/=4; guess_y/=4; guess_z/=4;
    double matrix[3][4]{};
    for (unsigned i=0;i<4;++i) {
        const double y=points[i].y-guess_y,z=points[i].z-guess_z,row[3]{2*y,2*z,1},value=y*y+z*z;
        for (unsigned j=0;j<3;++j) {
            for (unsigned k=0;k<3;++k) matrix[j][k]+=row[j]*row[k];
            matrix[j][3]+=row[j]*value;
        }
    }
    for (unsigned column=0;column<3;++column) {
        unsigned pivot=column;
        for (unsigned row=column+1;row<3;++row)
            if (std::abs(matrix[row][column])>std::abs(matrix[pivot][column])) pivot=row;
        if (!std::isfinite(matrix[pivot][column])||std::abs(matrix[pivot][column])<1.e-10) return false;
        if (pivot!=column) for (unsigned k=0;k<4;++k) std::swap(matrix[pivot][k],matrix[column][k]);
        const double scale=matrix[column][column];
        for (unsigned k=0;k<4;++k) matrix[column][k]/=scale;
        for (unsigned row=0;row<3;++row) if (row!=column) {
            const double factor=matrix[row][column];
            for (unsigned k=0;k<4;++k) matrix[row][k]-=factor*matrix[column][k];
        }
    }
    const double cy=matrix[0][3]+guess_y,cz=matrix[1][3]+guess_z,
        radius_squared=matrix[2][3]+matrix[0][3]*matrix[0][3]+matrix[1][3]*matrix[1][3];
    if (!std::isfinite(radius_squared)||radius_squared<.15*.15||radius_squared>.17*.17) return false;
    const double radius=std::sqrt(radius_squared);
    for (unsigned i=0;i<4;++i)
        if (std::abs(std::hypot(points[i].y-cy,points[i].z-cz)-radius)>.002) return false;
    center={static_cast<float>(x),static_cast<float>(cy),static_cast<float>(cz)}; return finite(center);
}
} // namespace

CalibratedRifleStatus derive_calibrated_rifle_basis(const CalibratedRifleReader& reader,
    const CalibratedRifleRequest& request,Basis3f* output,CalibratedRifleProof* proof) noexcept {
    const auto* profile=request.weapon=="m1garand"?&rifle_detail::kGarand:
        request.weapon=="mosin_rifle"?&rifle_detail::kMosin:nullptr;
    if (!profile) return CalibratedRifleStatus::not_applicable;
    if (!output||!request.definition||!request.dobj) return CalibratedRifleStatus::identity;
    if (!valid_axis(request.evaluated_flash_basis)) return CalibratedRifleStatus::invalid_axis;
    AssetRead first{},second{};
    auto status=read_asset(reader,request,*profile,first);
    if (status!=CalibratedRifleStatus::accepted) return status;
    status=read_asset(reader,request,*profile,second);
    if (status!=CalibratedRifleStatus::accepted) return status;
    if (!same_read(first,second)) return CalibratedRifleStatus::unstable;
    CalibratedRifleProof measured{}; measured.asset=profile->asset; measured.witness_fingerprint=first.fingerprint;
    if (profile->asset==CalibratedRifleAsset::garand) {
        if (!aperture_center(first.meshes[1].anchors,measured.rear_reference)) return CalibratedRifleStatus::geometry;
        const auto& front=first.meshes[0].anchors;
        measured.front_reference=mean_pair(mean_pair(front[0],front[1]),mean_pair(front[2],front[3]));
        measured.local_direction={measured.front_reference.x-measured.rear_reference.x,
            measured.front_reference.y-measured.rear_reference.y,measured.front_reference.z-measured.rear_reference.z};
    } else {
        const auto& points=first.meshes[0].anchors;
        measured.front_reference=mean_pair(mean_pair(points[0],points[1]),mean_pair(points[2],points[3]));
        measured.rear_reference=mean_pair(points[4],points[5]);
        float minimum=1.F,maximum=-1.F;
        for (unsigned rear=0;rear<5;++rear) {
            const auto reference=mean_pair(points[4+rear*2],points[5+rear*2]);
            for (unsigned edge=0;edge<2;++edge) {
                const auto tip=mean_pair(points[edge*2],points[edge*2+1]);
                if (tip.x<=reference.x) return CalibratedRifleStatus::geometry;
                minimum=(std::min)(minimum,tip.z-reference.z); maximum=(std::max)(maximum,tip.z-reference.z);
            }
        }
        if (minimum>0||maximum<0) return CalibratedRifleStatus::geometry;
        measured.local_direction={1,0,0}; // actual identity-bind tag_flash +X, inside measured notch bracket
    }
    const float squared=dot(measured.local_direction,measured.local_direction);
    if (!std::isfinite(squared)||squared<1.e-8F) return CalibratedRifleStatus::geometry;
    const float scale=1.F/std::sqrt(squared);
    measured.local_direction={measured.local_direction.x*scale,measured.local_direction.y*scale,measured.local_direction.z*scale};
    const auto& a=request.evaluated_flash_basis; const auto d=measured.local_direction;
    const Vec3f world{a.forward.x*d.x+a.left.x*d.y+a.up.x*d.z,
        a.forward.y*d.x+a.left.y*d.y+a.up.y*d.z,a.forward.z*d.x+a.left.z*d.y+a.up.z*d.z};
    Basis3f result{};
    if (!build_evaluated_projectile_basis(world,a,&result)) return CalibratedRifleStatus::invalid_axis;
    *output=result; if (proof) *proof=measured; return CalibratedRifleStatus::accepted;
}
} // namespace wawvr::mod
