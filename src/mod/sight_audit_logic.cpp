// SPDX-License-Identifier: GPL-3.0-only
#include "sight_audit_logic.hpp"
#include "sight_audit_recipes.hpp"
#include "sight_bind_transform.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <new>

namespace wawvr::mod {
namespace {
using wawvr::xr::Vec3f; using wawvr::xr::Basis3f;
template<class T> bool read(const SightAuditReader& r,std::uintptr_t a,T& v) noexcept {
    return r.read && a && sizeof(T)<=(std::numeric_limits<std::uintptr_t>::max)()-a &&
        r.read(r.context,a,&v,sizeof(v));
}
template<class T,std::size_t N> T at(const std::array<std::byte,N>& b,std::size_t o) noexcept {
    T v{}; if (o<=N && sizeof(T)<=N-o) std::memcpy(&v,b.data()+o,sizeof(v)); return v;
}
bool finite(Vec3f v) noexcept { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
float dot(Vec3f a,Vec3f b) noexcept { return a.x*b.x+a.y*b.y+a.z*b.z; }
bool near(Vec3f a,Vec3f b,float tolerance=.001F) noexcept {
    return finite(a)&&finite(b)&&std::abs(a.x-b.x)<=tolerance&&
        std::abs(a.y-b.y)<=tolerance&&std::abs(a.z-b.z)<=tolerance;
}
bool basis_valid(Basis3f b) noexcept {
    return finite(b.forward)&&finite(b.left)&&finite(b.up)&&
        std::abs(dot(b.forward,b.forward)-1.F)<.002F&&
        std::abs(dot(b.left,b.left)-1.F)<.002F&&std::abs(dot(b.up,b.up)-1.F)<.002F&&
        std::abs(dot(b.forward,b.left))<.002F&&std::abs(dot(b.forward,b.up))<.002F&&
        std::abs(dot(b.left,b.up))<.002F;
}
bool same_basis(Basis3f a,Basis3f b) noexcept {
    return basis_valid(a)&&basis_valid(b)&&near(a.forward,b.forward,.00001F)&&
        near(a.left,b.left,.00001F)&&near(a.up,b.up,.00001F);
}
bool pose_valid(const wawvr::xr::Posef& p) noexcept {
    const auto q=p.orientation;
    const float length=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    return finite(p.position)&&std::isfinite(length)&&std::abs(length-1.F)<.002F;
}
bool stationary_pose(const wawvr::xr::Posef& a,const wawvr::xr::Posef& b) noexcept {
    const auto p=a.orientation,q=b.orientation;
    const float alignment=p.x*q.x+p.y*q.y+p.z*q.z+p.w*q.w;
    return pose_valid(a)&&pose_valid(b)&&near(a.position,b.position,.0001F)&&
        std::abs(alignment)>=.99999F;
}
bool text(const SightAuditReader& r,std::uint32_t a,const char* expected) noexcept {
    std::array<char,64> actual{}; const auto n=std::strlen(expected)+1;
    return n<=actual.size()&&r.read&&r.read(r.context,a,actual.data(),n)&&
        std::memcmp(actual.data(),expected,n)==0;
}
void hash_bytes(std::uint64_t& h,const void* p,std::size_t n) noexcept {
    const auto* b=static_cast<const std::uint8_t*>(p);
    for (std::size_t i=0;i<n;++i) h=(h^b[i])*1099511628211ULL;
}
struct Bone final { std::array<float,4> quaternion{}; Vec3f translation{}; float weight{}; };
static_assert(sizeof(Bone)==32);
bool bone_basis(const Bone& b,Basis3f& out) noexcept {
    const float x=b.quaternion[0],y=b.quaternion[1],z=b.quaternion[2],w=b.quaternion[3],s=b.weight;
    if (!std::isfinite(s)||!std::isfinite(w)||s<1.9F||s>2.1F||!finite(b.translation)) return false;
    out={{1-s*(y*y+z*z),s*(x*y+z*w),s*(x*z-y*w)},
        {s*(x*y-z*w),1-s*(x*x+z*z),s*(y*z+x*w)},
        {s*(x*z+y*w),s*(y*z-x*w),1-s*(x*x+y*y)}};
    return basis_valid(out);
}
Vec3f world(Vec3f p,const Bone& bone,Basis3f basis,Vec3f origin,Vec3f bind) noexcept {
    p={p.x-bind.x,p.y-bind.y,p.z-bind.z};
    return {origin.x+bone.translation.x+p.x*basis.forward.x+p.y*basis.left.x+p.z*basis.up.x,
        origin.y+bone.translation.y+p.x*basis.forward.y+p.y*basis.left.y+p.z*basis.up.y,
        origin.z+bone.translation.z+p.x*basis.forward.z+p.y*basis.left.z+p.z*basis.up.z};
}
bool world_nonidentity_bind(Vec3f p,const Bone& bone,Basis3f basis,Vec3f origin,
    const Bone& bind,Vec3f& output) noexcept {
    Vec3f local{};
    if (!inverse_sight_bind_vector({p.x-bind.translation.x,p.y-bind.translation.y,p.z-bind.translation.z},
        bind.quaternion,bind.weight,&local)) return false;
    output=world(local,bone,basis,origin,{}); return finite(output);
}
struct SightReadScratch final {
    bool active{};
    std::array<std::byte,2048*32> vertices{},vertices_after{};
    std::array<std::uint16_t,2048*3> indices{},indices_after{};
    std::array<std::byte,3*12> rigid{},rigid_after{};
};
// Allocate only when the opt-in observer actually captures. Embedding this
// large buffer directly in DLL TLS would tax every native thread even during
// ordinary non-diagnostic gameplay.
thread_local std::unique_ptr<SightReadScratch> g_scratch;
struct ScratchLease final {
    ~ScratchLease() { g_scratch->active=false; }
};
} // namespace

std::uint32_t sight_audit_metadata_rejection_bits(const SightAuditMetadata& m) noexcept {
    std::uint32_t bits{};
    if (!m.definition||!m.dobj||m.weapon_index<=0) bits|=0x1;
    if (!m.generation) bits|=0x2;
    if (!m.controller_published_ms||!m.pose_published_ms||!m.pose_serial||!m.frame_id||
        !m.action_sequence||!m.predicted_display_time) bits|=0x4;
    if (!m.tracked_live||(m.tracking_evidence&kRequiredSightTrackingEvidence)!=kRequiredSightTrackingEvidence) bits|=0x8;
    if (m.transition_pending) bits|=0x10;
    if (m.grip_mode<1||m.grip_mode>3) bits|=0x20;
    if (m.controller_published_ms>m.pose_published_ms||m.pose_published_ms>m.captured_ms||
        m.captured_ms-m.pose_published_ms>25) bits|=0x40;
    if (!finite(m.muzzle)||!basis_valid(m.projectile_basis)) bits|=0x80;
    if (!finite(m.head_world_forward)||std::abs(dot(m.head_world_forward,m.head_world_forward)-1.F)>=.002F||
        !finite(m.head_world_origin)||!finite(m.parent_camera_origin)||!basis_valid(m.parent_camera_basis)) bits|=0x100;
    if (!finite(m.grip_world)) bits|=0x200;
    if (!pose_valid(m.head)||!pose_valid(m.right_hand)||!pose_valid(m.left_hand)) bits|=0x400;
    if (!std::isfinite(m.right_grip)||!std::isfinite(m.left_grip)||m.right_grip<0||m.right_grip>1||
        m.left_grip<0||m.left_grip>1) bits|=0x800;
    if (!m.grip_tag[0]||m.grip_tag.back()!='\0') bits|=0x1000;
    if (m.weapon_state!=0) bits|=0x2000;
    if (m.weapon_time>0) bits|=0x4000;
    if (m.weapon_delay>0) bits|=0x8000;
    if (!std::isfinite(m.right_trigger)||m.right_trigger<0||m.right_trigger>1) bits|=0x10000;
    return bits;
}
bool sight_audit_trigger_released(const SightAuditMetadata& m) noexcept {
    return (m.trigger_active||m.trigger_click_active)&&
        (!m.trigger_active||m.right_trigger<=.1F)&&(!m.trigger_click_active||!m.trigger_click);
}
bool sight_audit_observation_continuous(const SightAuditSnapshot& previous,
    const SightAuditSnapshot& current) noexcept {
    const auto& a=previous.metadata; const auto& b=current.metadata;
    return previous.valid&&current.valid&&a.definition==b.definition&&a.weapon_index==b.weapon_index&&
        a.asset==b.asset&&previous.model==current.model&&a.dobj==b.dobj&&a.grip_mode==b.grip_mode&&
        a.grip_tag==b.grip_tag&&previous.geometry_fingerprint==current.geometry_fingerprint&&
        b.generation>=a.generation&&b.generation-a.generation<=1&&b.pose_serial>a.pose_serial&&
        b.controller_published_ms>=a.controller_published_ms&&b.pose_published_ms>=a.pose_published_ms&&
        b.captured_ms>=a.captured_ms;
}

const char* sight_audit_status_name(SightAuditStatus s) noexcept {
    constexpr std::array names{"accepted","invalid-metadata","inaccessible","identity","geometry",
        "hidden","unevaluated","unstable","transform","muzzle-mismatch","weapon-mismatch",
        "source-mismatch","stale","fire-mismatch","missing","seed-mismatch","scene-frame-mismatch"};
    const auto i=static_cast<std::size_t>(s); return i<names.size()?names[i]:"unknown";
}

SightAuditStatus capture_sight_audit_source_witness(const SightAuditReader& r,
    const SightAuditMetadata& m,SightAuditSourceWitness* output) noexcept {
    if (!output||(sight_audit_metadata_rejection_bits(m)&~kSightAuditRestedReasonMask))
        return SightAuditStatus::invalid_metadata;
    const auto* p=detail::sight_asset_recipe(m.asset);
    if (!p) return SightAuditStatus::identity;
    std::array<std::byte,0x68> dobj{},after{};
    std::array<std::byte,0xE4> model{},model_after{};
    std::uint32_t def_name{},def_model{},def_model_after{};
    SightAuditSourceWitness seed{}; seed.asset=m.asset; seed.definition=m.definition; seed.dobj=m.dobj;
    seed.generation=m.generation; seed.pose_published_ms=m.pose_published_ms;
    seed.pose_serial=m.pose_serial; seed.frame_id=m.frame_id;
    if (!read(r,m.dobj,dobj)||!read(r,m.definition,def_name)||!read(r,m.definition+0x0C,def_model))
        return SightAuditStatus::inaccessible;
    seed.model_table=at<std::uint32_t>(dobj,0x64); seed.total_bones=at<std::uint8_t>(dobj,0x0A);
    if (at<std::uint8_t>(dobj,9)!=2||seed.total_bones<8||seed.total_bones>128||
        !read(r,seed.model_table,seed.models)||!seed.models[0]||!seed.models[1]||
        seed.models[1]!=def_model||!text(r,def_name,p->weapon)||!read(r,def_model,model))
        return SightAuditStatus::identity;
    std::uint8_t hands_bones{};
    if (!read(r,seed.models[0]+4,hands_bones)||!hands_bones||hands_bones+p->bone_count!=seed.total_bones||
        at<std::uint8_t>(model,4)!=p->bone_count||at<std::uint8_t>(model,5)!=1||
        at<std::uint8_t>(model,6)!=p->surface_count||!text(r,at<std::uint32_t>(model,0),p->model))
        return SightAuditStatus::identity;
    seed.model=def_model; seed.root_bone=hands_bones; seed.flash_bone=hands_bones+p->flash_bone;
    if (m.asset==SightAuditAsset::colt) seed.sight_bone=hands_bones+p->rear_bone;
    seed.matrices=at<std::uint32_t>(dobj,0x48); seed.skeleton_timestamp=at<std::uint32_t>(dobj,0x44);
    if (!seed.skeleton_timestamp||!seed.matrices) return SightAuditStatus::unevaluated;
    for (const auto bone:{seed.root_bone,seed.flash_bone})
        if (!(at<std::uint32_t>(dobj,0x34+(bone>>5U)*4)&(0x80000000U>>(bone&31U))))
            return SightAuditStatus::unevaluated;
    if (m.asset==SightAuditAsset::colt) {
        const auto mask=0x80000000U>>(seed.sight_bone&31U),word=(seed.sight_bone>>5U)*4U;
        if (at<std::uint32_t>(dobj,0x50+word)&mask) return SightAuditStatus::hidden;
        if (!(at<std::uint32_t>(dobj,0x34+word)&mask)) return SightAuditStatus::unevaluated;
    }
    for (unsigned i=0;i<4;++i) seed.hidden[i]=at<std::uint32_t>(dobj,0x50+i*4);
    const auto root_address=static_cast<std::uintptr_t>(seed.matrices)+seed.root_bone*32U;
    const auto flash_address=static_cast<std::uintptr_t>(seed.matrices)+seed.flash_bone*32U;
    Bone root{},flash{},root_after{},flash_after{},sight{},sight_after{}; Vec3f origin_after{};
    std::array<std::uint32_t,2> models_after{};
    if (!read(r,root_address,root)||!read(r,flash_address,flash)||!read(r,0x03520388U,seed.native_origin))
        return SightAuditStatus::inaccessible;
    const auto sight_address=static_cast<std::uintptr_t>(seed.matrices)+seed.sight_bone*32U;
    if (m.asset==SightAuditAsset::colt&&!read(r,sight_address,sight)) return SightAuditStatus::inaccessible;
    Basis3f root_basis{},flash_basis{};
    if (!bone_basis(root,root_basis)||!bone_basis(flash,flash_basis)||!finite(seed.native_origin))
        return SightAuditStatus::transform;
    if (m.asset==SightAuditAsset::colt) {
        Basis3f checked{}; if (!bone_basis(sight,checked)) return SightAuditStatus::transform;
    }
    if (!near({seed.native_origin.x+flash.translation.x,seed.native_origin.y+flash.translation.y,
        seed.native_origin.z+flash.translation.z},m.muzzle)) return SightAuditStatus::muzzle_mismatch;
    if (!read(r,m.dobj,after)||!read(r,seed.model,model_after)||!read(r,m.definition+0x0C,def_model_after)||
        !read(r,seed.model_table,models_after)||!read(r,root_address,root_after)||
        !read(r,flash_address,flash_after)||!read(r,0x03520388U,origin_after)||
        dobj!=after||model!=model_after||def_model!=def_model_after||seed.models!=models_after||
        std::memcmp(&root,&root_after,sizeof(root))||std::memcmp(&flash,&flash_after,sizeof(flash))||
        std::memcmp(&seed.native_origin,&origin_after,sizeof(origin_after))) return SightAuditStatus::unstable;
    if (m.asset==SightAuditAsset::colt&&(!read(r,sight_address,sight_after)||
        std::memcmp(&sight,&sight_after,sizeof(sight)))) return SightAuditStatus::unstable;
    std::memcpy(seed.root_flash_transforms.data(),&root,32);
    std::memcpy(seed.root_flash_transforms.data()+32,&flash,32);
    if (m.asset==SightAuditAsset::colt) std::memcpy(seed.sight_transform.data(),&sight,32);
    seed.valid=true; *output=seed; return SightAuditStatus::accepted;
}

SightAuditStatus capture_deferred_rendered_sights(const SightAuditReader& r,const SightAuditRecipe& recipe,
    const SightAuditMetadata& m,const SightAuditSourceWitness& seed,std::uint64_t frame,
    SightAuditSnapshot* output) noexcept {
    if (!output||!seed.valid) return SightAuditStatus::missing;
    if (!frame||frame!=m.frame_id||!m.deferred_capture||m.observed_frame!=frame)
        return SightAuditStatus::scene_frame_mismatch;
    if (seed.generation!=m.generation||seed.pose_published_ms!=m.pose_published_ms||
        seed.pose_serial!=m.pose_serial||seed.frame_id!=m.frame_id) return SightAuditStatus::source_mismatch;
    const auto matches=[&](const SightAuditSourceWitness& current) noexcept {
        return current.valid&&seed.asset==current.asset&&seed.definition==current.definition&&seed.dobj==current.dobj&&
            seed.model==current.model&&seed.model_table==current.model_table&&seed.matrices==current.matrices&&
            seed.skeleton_timestamp==current.skeleton_timestamp&&seed.total_bones==current.total_bones&&
            seed.root_bone==current.root_bone&&seed.flash_bone==current.flash_bone&&seed.models==current.models&&
            seed.hidden==current.hidden&&seed.root_flash_transforms==current.root_flash_transforms&&
            seed.sight_bone==current.sight_bone&&seed.sight_transform==current.sight_transform&&
            std::memcmp(&seed.native_origin,&current.native_origin,sizeof(seed.native_origin))==0;
    };
    SightAuditSourceWitness before{},after{};
    auto status=capture_sight_audit_source_witness(r,m,&before);
    if (status!=SightAuditStatus::accepted) return status;
    if (!matches(before)) return SightAuditStatus::seed_mismatch;
    SightAuditSnapshot sample{};
    status=capture_rendered_sights(r,recipe,m,&sample);
    if (status!=SightAuditStatus::accepted) return status;
    status=capture_sight_audit_source_witness(r,m,&after);
    if (status!=SightAuditStatus::accepted) return status;
    if (!matches(after)||sample.model!=seed.model||sample.skeleton_timestamp!=seed.skeleton_timestamp)
        return SightAuditStatus::seed_mismatch;
    *output=sample; return SightAuditStatus::accepted;
}

SightAuditStatus capture_garand_rendered_sights(const SightAuditReader& r,const SightAuditRecipe& recipe,
    const SightAuditMetadata& m,SightAuditSnapshot* output) noexcept {
    if (recipe.asset!=SightAuditAsset::garand||m.asset!=SightAuditAsset::garand)
        return SightAuditStatus::identity;
    return capture_rendered_sights(r,recipe,m,output);
}

SightAuditStatus capture_rendered_sights(const SightAuditReader& r,const SightAuditRecipe& recipe,
    const SightAuditMetadata& m,SightAuditSnapshot* output) noexcept {
    const auto metadata_bits=sight_audit_metadata_rejection_bits(m);
    if (!output||(metadata_bits&~kSightAuditRestedReasonMask)) return SightAuditStatus::invalid_metadata;
    const auto* profile=detail::sight_asset_recipe(recipe.asset);
    if (!profile||m.asset!=recipe.asset) return SightAuditStatus::identity;
    // The largest pinned mesh is SVT1919 vertices/1862 triangles. Keep bounded
    // scratch off T4's x86 stack, separate per thread, and reject re-entry.
    if (!g_scratch) g_scratch.reset(new(std::nothrow) SightReadScratch{});
    if (!g_scratch) return SightAuditStatus::inaccessible;
    if (g_scratch->active) return SightAuditStatus::unstable;
    g_scratch->active=true; const ScratchLease scratch_lease{};
    auto& scratch=*g_scratch;
    std::array<std::byte,0x68> dobj{},after{}; std::array<std::byte,0xE4> model{},model_after{};
    std::array<std::uint32_t,2> models{}; std::uint32_t def_name{},def_model{},pool{};
    if (!read(r,m.definition,def_name)||!read(r,m.definition+0x0C,def_model)||
        !read(r,m.dobj,dobj)||!read(r,0x03702390U,pool)) return SightAuditStatus::inaccessible;
    const auto model_table=at<std::uint32_t>(dobj,0x64);
    const auto total=at<std::uint8_t>(dobj,0x0A);
    if (at<std::uint8_t>(dobj,9)!=2||total<8||total>128||!read(r,model_table,models)||
        !models[0]||!models[1]||def_model!=models[1]||!text(r,def_name,profile->weapon)||
        !read(r,models[1],model)) return SightAuditStatus::identity;
    std::uint8_t hands_bones{};
    if (!read(r,models[0]+4,hands_bones)||!hands_bones||hands_bones+profile->bone_count!=total||
        at<std::uint8_t>(model,4)!=profile->bone_count||at<std::uint8_t>(model,5)!=1||
        at<std::uint8_t>(model,6)!=profile->surface_count||
        !text(r,at<std::uint32_t>(model,0),profile->model)) return SightAuditStatus::identity;
    std::array<std::uint16_t,9> ids{}; std::array<std::uint8_t,8> parents{};
    std::array<Bone,9> bind{},bones{},bones_after{};
    const auto bone_bytes=profile->bone_count*sizeof(Bone);
    if (!r.read||!r.read(r.context,at<std::uint32_t>(model,8),ids.data(),profile->bone_count*2)||
        !r.read(r.context,at<std::uint32_t>(model,12),parents.data(),profile->bone_count-1)||
        !r.read(r.context,at<std::uint32_t>(model,0x1C),bind.data(),bone_bytes)) return SightAuditStatus::inaccessible;
    for (std::size_t i=0;i<profile->bone_count;++i)
        if (!ids[i]||!text(r,pool+static_cast<std::uint32_t>(ids[i])*12+4,profile->bone_names[i])||
            (i&&parents[i-1]!=i)) return SightAuditStatus::identity;
    const auto bind_matches=[&](std::uint32_t bone,Vec3f expected) noexcept {
        return bind[bone].quaternion==std::array{0.F,0.F,0.F,1.F}&&near(bind[bone].translation,expected,.000001F);
    };
    if (recipe.asset==SightAuditAsset::colt) {
        const auto exact_bind=[&](std::uint32_t bone,Vec3f expected,const std::array<float,4>& q,float weight) noexcept {
            return bind[bone].quaternion==q&&std::memcmp(&bind[bone].translation,&expected,sizeof(expected))==0&&
                bind[bone].weight==weight;
        };
        if (!exact_bind(profile->front_bone,profile->front_bind,profile->front_bind_orientation,profile->front_bind_weight)||
            !exact_bind(profile->rear_bone,profile->rear_bind,profile->rear_bind_orientation,profile->rear_bind_weight)||
            !exact_bind(profile->flash_bone,profile->flash_bind,profile->flash_bind_orientation,profile->flash_bind_weight))
            return SightAuditStatus::geometry;
    } else if (!bind_matches(profile->front_bone,profile->front_bind)||
        !bind_matches(profile->rear_bone,profile->rear_bind)||
        !bind_matches(profile->flash_bone,profile->flash_bind)) return SightAuditStatus::geometry;
    for (const auto local_bone:{profile->front_bone,profile->rear_bone,profile->flash_bone}) {
        const auto bone=hands_bones+local_bone,mask=0x80000000U>>(bone&31U);
        if (local_bone!=profile->flash_bone&&
            (at<std::uint32_t>(dobj,0x50+(bone>>5U)*4)&mask)) return SightAuditStatus::hidden;
        if (!(at<std::uint32_t>(dobj,0x34+(bone>>5U)*4)&mask)||
            !at<std::uint32_t>(dobj,0x44)) return SightAuditStatus::unevaluated;
    }
    const auto matrices=at<std::uint32_t>(dobj,0x48);
    Vec3f native_origin{},origin_after{};
    if (!r.read(r.context,matrices+hands_bones*32U,bones.data(),bone_bytes)||!read(r,0x03520388U,native_origin))
        return SightAuditStatus::inaccessible;
    const auto surfaces=at<std::uint32_t>(model,0x20);
    std::uint64_t hash=14695981039346656037ULL;
    for (unsigned mesh=0;mesh<profile->mesh_count;++mesh) {
        const auto spec=profile->meshes[mesh];
        const auto surface_index=spec.surface;
        std::array<std::byte,0x40> surface{},surface_after{};
        if (!read(r,surfaces+surface_index*0x40,surface)) return SightAuditStatus::inaccessible;
        const auto count=spec.vertices,first=spec.first_vertex,owned=spec.owned_vertices;
        const auto triangle_count=spec.triangles,tri_first=spec.first_triangle,tri_owned=spec.owned_triangles;
        if (count>2048||triangle_count>2048||spec.range_count>3) return SightAuditStatus::geometry;
        if (at<std::uint8_t>(surface,1)||at<std::uint16_t>(surface,2)!=count||
            at<std::uint16_t>(surface,4)!=triangle_count||at<std::uint32_t>(surface,0x24)!=spec.range_count)
            return SightAuditStatus::geometry;
        auto& vertices=scratch.vertices; auto& indices=scratch.indices; auto& rigid=scratch.rigid;
        if (!r.read||!r.read(r.context,at<std::uint32_t>(surface,0x1C),vertices.data(),count*32)||
            !r.read(r.context,at<std::uint32_t>(surface,0x0C),indices.data(),triangle_count*6)||
            !r.read(r.context,at<std::uint32_t>(surface,0x28),rigid.data(),spec.range_count*12))
            return SightAuditStatus::inaccessible;
        const unsigned entry=spec.range_count==3?24U:0U;
        if (spec.range_count==3&&(at<std::uint16_t>(rigid,0)!=0||at<std::uint16_t>(rigid,2)!=80||
            at<std::uint16_t>(rigid,4)!=0||at<std::uint16_t>(rigid,6)!=56||
            at<std::uint16_t>(rigid,12)!=3*64||at<std::uint16_t>(rigid,14)!=65||
            at<std::uint16_t>(rigid,16)!=56||at<std::uint16_t>(rigid,18)!=33))
            return SightAuditStatus::geometry;
        if (at<std::uint16_t>(rigid,entry)!=spec.owner_bone*64||at<std::uint16_t>(rigid,entry+2)!=owned||
            at<std::uint16_t>(rigid,entry+4)!=tri_first||at<std::uint16_t>(rigid,entry+6)!=tri_owned)
            return SightAuditStatus::geometry;
        for (unsigned i=first;i<first+owned;++i) hash_bytes(hash,vertices.data()+i*32,12);
        for (unsigned i=tri_first*3;i<(tri_first+tri_owned)*3;++i) {
            const auto vertex=indices[i];
            if (vertex<first||vertex>=first+owned) return SightAuditStatus::geometry;
            hash_bytes(hash,vertices.data()+vertex*32,12);
        }
        auto& vertices_after=scratch.vertices_after;
        auto& indices_after=scratch.indices_after; auto& rigid_after=scratch.rigid_after;
        if (!r.read(r.context,at<std::uint32_t>(surface,0x1C),vertices_after.data(),count*32)||
            !r.read(r.context,at<std::uint32_t>(surface,0x0C),indices_after.data(),triangle_count*6)||
            !r.read(r.context,at<std::uint32_t>(surface,0x28),rigid_after.data(),spec.range_count*12)||
            std::memcmp(vertices.data(),vertices_after.data(),count*32)||
            std::memcmp(indices.data(),indices_after.data(),triangle_count*6)||
            std::memcmp(rigid.data(),rigid_after.data(),spec.range_count*12)) return SightAuditStatus::unstable;
        if (!read(r,surfaces+surface_index*0x40,surface_after)||surface!=surface_after)
            return SightAuditStatus::unstable;
    }
    if (hash!=recipe.geometry_fingerprint) return SightAuditStatus::geometry;
    if (!read(r,m.dobj,after)||!read(r,models[1],model_after)||
        !r.read(r.context,matrices+hands_bones*32U,bones_after.data(),bone_bytes)||!read(r,0x03520388U,origin_after)||
        dobj!=after||model!=model_after||std::memcmp(bones.data(),bones_after.data(),bone_bytes)||
        std::memcmp(&native_origin,&origin_after,sizeof(native_origin))) return SightAuditStatus::unstable;
    Basis3f front_basis{},rear_basis{},flash_basis{};
    if (!bone_basis(bones[profile->front_bone],front_basis)||!bone_basis(bones[profile->rear_bone],rear_basis)||
        !bone_basis(bones[profile->flash_bone],flash_basis)||!finite(native_origin))
        return SightAuditStatus::transform;
    const auto& flash=bones[profile->flash_bone];
    const Vec3f flash_world{native_origin.x+flash.translation.x,
        native_origin.y+flash.translation.y,native_origin.z+flash.translation.z};
    if (!near(flash_world,m.muzzle)) return SightAuditStatus::muzzle_mismatch;
    SightAuditSnapshot result{}; result.valid=true; result.metadata=m;
    result.rested_reason_bits=metadata_bits&kSightAuditRestedReasonMask;
    result.rested_eligible=result.rested_reason_bits==0;
    result.model=models[1]; result.skeleton_timestamp=at<std::uint32_t>(dobj,0x44);
    result.sight_bone=hands_bones+profile->rear_bone; result.front_bone=hands_bones+profile->front_bone;
    result.geometry_fingerprint=hash; result.sight_bone_basis=rear_basis; result.front_bone_basis=front_basis;
    result.rear_count=profile->rear_count; result.floor_count=profile->floor_count;
    if (recipe.asset==SightAuditAsset::colt) {
        for (std::size_t i=0;i<profile->rear_count;++i)
            if (!world_nonidentity_bind(profile->rear[i],bones[profile->rear_bone],rear_basis,native_origin,
                bind[profile->rear_bone],result.rear[i])) return SightAuditStatus::transform;
        for (std::size_t i=0;i<profile->front.size();++i)
            if (!world_nonidentity_bind(profile->front[i],bones[profile->front_bone],front_basis,native_origin,
                bind[profile->front_bone],result.front[i])) return SightAuditStatus::transform;
        for (std::size_t i=0;i<profile->floor_count;++i)
            if (!world_nonidentity_bind(profile->floor[i],bones[profile->rear_bone],rear_basis,native_origin,
                bind[profile->rear_bone],result.floor[i])) return SightAuditStatus::transform;
        *output=result; return SightAuditStatus::accepted;
    }
    for (std::size_t i=0;i<profile->rear_count;++i)
        result.rear[i]=world(profile->rear[i],bones[profile->rear_bone],rear_basis,native_origin,profile->rear_bind);
    for (std::size_t i=0;i<profile->front.size();++i)
        result.front[i]=world(profile->front[i],bones[profile->front_bone],front_basis,native_origin,profile->front_bind);
    for (std::size_t i=0;i<profile->floor_count;++i)
        result.floor[i]=world(profile->floor[i],bones[profile->rear_bone],rear_basis,native_origin,profile->rear_bind);
    *output=result; return SightAuditStatus::accepted;
}

SightAuditStatus match_sight_audit_source(const SightAuditSnapshot& s,const SightAuditFireRequest& f) noexcept {
    if (!s.valid||!f.weapon_parms) return SightAuditStatus::missing;
    const auto& m=s.metadata;
    if (f.definition!=m.definition||f.weapon_index!=m.weapon_index) return SightAuditStatus::weapon_mismatch;
    if (!f.source_generation||!f.source_pose_serial||!m.pose_serial||f.source_generation!=m.generation||
        f.source_published_ms!=m.pose_published_ms||f.source_pose_serial!=m.pose_serial)
        return SightAuditStatus::source_mismatch;
    if (f.now_ms<m.captured_ms||f.now_ms<m.pose_published_ms||f.now_ms-m.pose_published_ms>kSightAuditMaximumSourceAgeMs||
        f.generation<m.generation||f.generation-m.generation>kSightAuditMaximumGenerationLag)
        return SightAuditStatus::stale;
    if (!near(f.origin,m.muzzle)||!same_basis(f.basis,m.projectile_basis)) return SightAuditStatus::fire_mismatch;
    return SightAuditStatus::accepted;
}
SightAuditStatus match_sight_audit_discharge(const SightAuditFireRequest& a,const SightAuditFireRequest& f) noexcept {
    if (!a.weapon_parms||a.weapon_parms!=f.weapon_parms||a.definition!=f.definition||
        a.weapon_index!=f.weapon_index||a.generation!=f.generation) return SightAuditStatus::weapon_mismatch;
    if (f.now_ms<a.now_ms||f.now_ms-a.now_ms>kSightAuditMaximumTokenAgeMs) return SightAuditStatus::stale;
    if (!near(a.origin,f.origin)||!same_basis(a.basis,f.basis)) return SightAuditStatus::fire_mismatch;
    return SightAuditStatus::accepted;
}
std::uint32_t sight_audit_pretrigger_rejection_bits(const SightAuditSnapshot& pre,
    const SightAuditSnapshot& source,const SightAuditFireRequest& fire) noexcept {
    const auto& a=pre.metadata; const auto& b=source.metadata;
    std::uint32_t bits{};
    if (!pre.valid||!source.valid||!pre.rested_eligible||pre.rested_reason_bits||
        sight_audit_metadata_rejection_bits(a)||
        (sight_audit_metadata_rejection_bits(b)&~kSightAuditRestedReasonMask)||
        !sight_audit_trigger_released(a)) bits|=1U;
    if (a.definition!=b.definition||a.weapon_index!=b.weapon_index||a.asset!=b.asset||
        pre.model!=source.model||a.dobj!=b.dobj||a.grip_mode!=b.grip_mode||a.grip_tag!=b.grip_tag||
        std::abs(a.right_grip-b.right_grip)>.001F||std::abs(a.left_grip-b.left_grip)>.001F||
        pre.geometry_fingerprint!=source.geometry_fingerprint) bits|=2U;
    if (!a.pose_serial||a.pose_serial>=b.pose_serial||!a.generation||a.generation>=b.generation||
        a.pose_published_ms>b.pose_published_ms||a.controller_published_ms>b.controller_published_ms) bits|=4U;
    if (fire.now_ms<a.captured_ms||fire.now_ms<a.pose_published_ms||
        fire.now_ms-a.pose_published_ms>kSightAuditMaximumPreTriggerAgeMs||
        fire.generation<a.generation||fire.generation-a.generation>kSightAuditMaximumPreTriggerLag) bits|=8U;
    if (!stationary_pose(a.head,b.head)||!stationary_pose(a.right_hand,b.right_hand)||
        !stationary_pose(a.left_hand,b.left_hand)) bits|=0x10U;
    if (!near(a.parent_camera_origin,b.parent_camera_origin,.005F)||
        !same_basis(a.parent_camera_basis,b.parent_camera_basis)) bits|=0x20U;
    if (!((b.trigger_active&&b.right_trigger>=.7F)||(b.trigger_click_active&&b.trigger_click))) bits|=0x40U;
    return bits;
}
} // namespace wawvr::mod
