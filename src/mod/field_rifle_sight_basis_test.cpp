// SPDX-License-Identifier: GPL-3.0-only
#include "field_rifle_sight_basis.hpp"
#include "field_rifle_sight_recipe.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
using namespace wawvr::mod;using field_rifle_detail::Bone;using field_rifle_detail::AssetWitness;
using wawvr::xr::Vec3f;using wawvr::xr::Basis3f;
unsigned checks{};
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
bool near(Vec3f a,Vec3f b,float t=.00002F){return std::abs(a.x-b.x)<t&&std::abs(a.y-b.y)<t&&std::abs(a.z-b.z)<t;}
Basis3f axis(std::array<float,4> q){const float x=q[0],y=q[1],z=q[2],w=q[3],s=2.F/(x*x+y*y+z*z+w*w);
    return {{1-s*(y*y+z*z),s*(x*y+z*w),s*(x*z-y*w)},
        {s*(x*y-z*w),1-s*(x*x+z*z),s*(y*z+x*w)},
        {s*(x*z+y*w),s*(y*z-x*w),1-s*(x*x+y*y)}};}
Vec3f compose(Basis3f a,Vec3f p){return {a.forward.x*p.x+a.left.x*p.y+a.up.x*p.z,
    a.forward.y*p.x+a.left.y*p.y+a.up.y*p.z,a.forward.z*p.x+a.left.z*p.y+a.up.z*p.z};}
Vec3f add(Vec3f a,Vec3f b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
struct Memory {
    std::map<std::uintptr_t,std::vector<std::byte>> blocks;
    std::size_t reads{},bytes{},dobj_reads{},origin_reads{};bool deny{},unstable{},moving_origin{};
    template<class T>void set(std::uintptr_t address,const T& value){
        const auto* p=reinterpret_cast<const std::byte*>(&value);blocks[address]={p,p+sizeof(T)};}
    template<class T>void patch(std::uintptr_t address,std::size_t offset,const T& value){
        auto& b=blocks.at(address);check(offset+sizeof(T)<=b.size(),"fixture patch overflow");
        std::memcpy(b.data()+offset,&value,sizeof(T));}
    void string(std::uintptr_t address,const char* value){const auto* p=reinterpret_cast<const std::byte*>(value);
        blocks[address]={p,p+std::strlen(value)+1};}
    static bool read(void* context,std::uintptr_t address,void* output,std::size_t size)noexcept{
        auto& m=*static_cast<Memory*>(context);++m.reads;m.bytes+=size;if(m.deny)return false;
        for(const auto& [base,data]:m.blocks)if(address>=base&&address-base<=data.size()&&size<=data.size()-(address-base)){
            std::memcpy(output,data.data()+address-base,size);
            if(m.unstable&&address==0x10000&&++m.dobj_reads==2)static_cast<std::byte*>(output)[0x60]^=std::byte{1};
            if(m.moving_origin&&address==0x22000&&++m.origin_reads==2)static_cast<float*>(output)[0]+=.01F;
            return true;}
        return false;}
};
struct Fixture {
    const AssetWitness& p;Memory memory;FieldRifleRequest request{};
    explicit Fixture(const AssetWitness& spec):p(spec){
        request={0x20000,0x10000,p.weapon,axis({0,0,0,1}),
            add({100,200,300},p.binds[p.bones-1].translation),true,10,1000,1010,0x22000};
        memory.set(0x22000,Vec3f{100,200,300});
        memory.set(0x20000,std::array<std::byte,16>{});memory.patch(0x20000,0,std::uint32_t{0x21000});
        memory.patch(0x20000,12,std::uint32_t{0x40000});memory.string(0x21000,p.weapon);
        memory.set(0x10000,std::array<std::byte,0x68>{});memory.patch(0x10000,9,std::uint8_t{2});
        memory.patch(0x10000,10,std::uint8_t(70+p.bones));memory.patch(0x10000,0x64,std::uint32_t{0x50000});
        memory.patch(0x10000,0x44,std::uint32_t{77});memory.patch(0x10000,0x48,std::uint32_t{0x200000});
        memory.patch(0x10000,0x34+8,(0x80000000U>>6)|(0x80000000U>>(6+p.bones-1)));
        memory.set(0x50000,std::array<std::uint32_t,2>{0x30000,0x40000});
        memory.set(0x30000,std::array<std::byte,8>{});memory.patch(0x30000,4,std::uint8_t{70});
        memory.set(0x40000,std::array<std::byte,0xE4>{});memory.patch(0x40000,0,std::uint32_t{0x41000});
        memory.patch(0x40000,4,p.bones);memory.patch(0x40000,5,std::uint8_t{1});memory.patch(0x40000,6,p.surfaces);
        memory.patch(0x40000,8,std::uint32_t{0x60000});memory.patch(0x40000,12,std::uint32_t{0x60100});
        memory.patch(0x40000,0x1C,std::uint32_t{0x61000});memory.patch(0x40000,0x20,std::uint32_t{0x70000});
        memory.patch(0x40000,0x24,std::uint32_t{0x90000});memory.string(0x41000,p.model);
        std::array<std::uint16_t,7> ids{};std::array<std::uint8_t,6> parents{};
        memory.set(0x03702390,std::uint32_t{0x100000});
        for(unsigned i=0;i<p.bones;++i){ids[i]=std::uint16_t(100+i*10);if(i)parents[i-1]=std::uint8_t(i);
            memory.string(0x100000+ids[i]*12+4,p.bone_names[i]);}
        memory.set(0x60000,ids);memory.set(0x60100,parents);memory.set(0x61000,p.binds);
        memory.set(0x70000,std::array<std::byte,6*0x40>{});
        const auto off=p.surface*0x40U;
        memory.patch(0x70000,off+2,p.vertices);memory.patch(0x70000,off+4,p.triangles);
        memory.patch(0x70000,off+0x1C,std::uint32_t{0x800000});memory.patch(0x70000,off+0x0C,std::uint32_t{0x830000});
        memory.patch(0x70000,off+0x24,std::uint32_t{1});memory.patch(0x70000,off+0x28,std::uint32_t{0x820000});
        memory.set(0x820000,std::array<std::uint16_t,6>{0,p.vertices,0,p.rigid_triangles,0,0});
        std::vector<std::byte> vertices(p.vertices*32);std::vector<std::uint16_t> indices(p.triangles*3);
        for(const auto& b:p.vertex_blocks)for(unsigned i=0;i<b.count;++i)
            std::memcpy(vertices.data()+(b.first+i)*32,&b.points[i],12);
        for(const auto& b:p.triangle_blocks)for(unsigned i=0;i<b.count;++i)for(unsigned j=0;j<3;++j)
            indices[(b.first+i)*3+j]=b.triangles[i][j];
        memory.blocks[0x800000]=vertices;const auto* ip=reinterpret_cast<const std::byte*>(indices.data());
        // These rifle vertex arrays exceed64KiB; keep index storage disjoint.
        memory.blocks[0x830000]={ip,ip+indices.size()*2};
        std::array<std::uint32_t,6> materials{};materials[p.surface]=0x91000;memory.set(0x90000,materials);
        memory.set(0x91000,std::uint32_t{0x92000});memory.string(0x92000,p.material);
        std::array<Bone,77> matrices{};for(auto& b:matrices)b={{0,0,0,1},{},2};
        matrices[70+p.bones-1]=p.binds[p.bones-1];memory.set(0x200000,matrices);
    }
    FieldRifleStatus derive(Basis3f* out,FieldRifleProof* proof=nullptr){
        return derive_field_rifle_sight_basis({&memory,&Memory::read},request,out,proof);}
    void rotate(std::array<float,4> q,Vec3f translation={3,4,5}){
        const float norm=q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3];const auto a=axis(q);
        memory.patch(0x200000,70*32,Bone{q,translation,2.F/norm});
        memory.patch(0x200000,(70+p.bones-1)*32,Bone{q,add(translation,compose(a,p.binds[p.bones-1].translation)),2.F/norm});
        request.evaluated_flash_basis=a;
        request.evaluated_flash_world_position=add(add({100,200,300},translation),compose(a,p.binds[p.bones-1].translation));
    }
};
void positives(const AssetWitness& p){
    Fixture f(p);Basis3f out{};FieldRifleProof proof{};
    const auto status=f.derive(&out,&proof);
    if(status!=FieldRifleStatus::accepted)throw std::runtime_error(std::string(p.weapon)+
        " exact rigid rifle mesh witness refused, status="+std::to_string(unsigned(status)));
    check(status==FieldRifleStatus::accepted,"exact rigid rifle mesh witness refused");
    check(proof.asset==p.asset&&proof.fingerprint==p.fingerprint&&proof.model==0x40000&&
        proof.root_bone==70&&proof.skeleton_timestamp==77,"wrong rifle source proof");
    const auto baseline=out.forward;
    const float degrees=std::atan2(baseline.z,std::hypot(baseline.x,baseline.y))*57.2957795F;
    check(degrees>(p.asset==FieldRifleAsset::carbine?.889F:.691F)&&
        degrees<(p.asset==FieldRifleAsset::carbine?.892F:.694F),"wrong actual central blade/aperture/notch slope");
    check(f.memory.reads<100&&f.memory.bytes<12500,"bounded block witness became per-vertex or full mesh read");
    if(p.asset==FieldRifleAsset::carbine){
        check(near(proof.front_reference,{19.782838821411133F,-.009469999931752682F,2.194333076477051F}),"used guard ears/front slab instead of rear blade edge");
        check(near(proof.rear_reference,{-6.458209037780762F,0,1.7865177989F}),"used outer ring/shoulders instead of closed inner aperture");
    }else{
        check(near(proof.front_reference,{28.5401554107666F,0,2.1532039642333984F}),"used protective hood instead of front apex");
        check(near(proof.rear_reference,{4.8919291496276855F,-.0000015050172805786133F,1.867417335510254F}),"used V floor instead of notch shoulder line");
    }
    const std::array<std::array<float,4>,8> rotations{{{0,0,0,1},{0,0,.70710678F,.70710678F},{0,0,-.70710678F,.70710678F},
        {0,.70710678F,0,.70710678F},{.70710678F,0,0,.70710678F},{.5F,.5F,.5F,.5F},{0,1,0,0},{.25F,.5F,.25F,.7905694F}}};
    for(const auto& q:rotations){Fixture r(p);r.rotate(q);
        check(r.derive(&out,&proof)==FieldRifleStatus::accepted,"rotated/rolled/off-head-forward rifle refused");
        check(near(out.forward,compose(axis(q),baseline)),"rifle local sight line became global/controller pitch");
        check(near(proof.evaluated_root_basis.forward,axis(q).forward),"root proof is not same evaluated native skeleton");
        check(near(proof.world_front,add(add({100,200,300},{3,4,5}),compose(axis(q),proof.front_reference)),.00005F),"world front lost native origin/root translation");
        check(near(proof.world_rear,add(add({100,200,300},{3,4,5}),compose(axis(q),proof.rear_reference)),.00005F),"world rear lost native origin/root translation");
    }
    Fixture age(p);age.request.now_milliseconds=1150;check(age.derive(&out)==FieldRifleStatus::accepted,"inclusive150ms boundary refused");
}
void negatives(const AssetWitness& p){
    for(unsigned n=0;n<44;++n){Fixture f(p);const auto off=p.surface*0x40U;const auto flash=(70+p.bones-1)*32U;
        switch(n){
        case 0:f.memory.deny=true;break;
        case 1:f.memory.string(0x41000,"viewmodel_custom_rifle");break;
        case 2:f.memory.string(0x21000,"unrelated");break;
        case 3:f.memory.patch(0x10000,10,std::uint8_t{74});break;
        case 4:f.memory.patch(0x40000,6,std::uint8_t{7});break;
        case 5:f.memory.patch(0x60100,0,std::uint8_t{2});break;
        case 6:f.memory.patch(0x61000,32+4,.01F);break;
        case 7:f.memory.patch(0x61000,32+16,0.F);break;
        case 8:f.memory.patch(0x70000,off+2,std::uint16_t{2});break;
        case 9:f.memory.patch(0x820000,0,std::uint16_t{64});break;
        case 10:f.memory.patch(0x800000,p.vertex_blocks[0].first*32,5.34F);break;
        case 11:f.memory.patch(0x830000,p.triangle_blocks[1].first*6,std::uint16_t{0});break;
        case 12:f.memory.string(0x92000,"mc/mtl_weapon_custom");break;
        case 13:f.memory.patch(0x10000,0x44,std::uint32_t{});break;
        case 14:f.memory.patch(0x10000,0x34+8,0x80000000U>>6);break;
        case 15:f.memory.patch(0x10000,0x50+8,0x80000000U>>6);break;
        case 16:f.memory.unstable=true;break;
        case 17:f.memory.patch(0x200000,70*32,std::numeric_limits<float>::quiet_NaN());break;
        case 18:f.memory.patch(0x200000,70*32+28,0.F);break;
        case 19:f.request.evaluated_flash_basis.up.z=-1;break;
        case 20:f.request.evaluated_flash_basis=axis({0,0,.70710678F,.70710678F});break;
        case 21:f.memory.patch(0x70000,off+0x1C,std::uint32_t{0xFFFFFFF0});break;
        case 22:f.memory.patch(0x40000,8,std::uint32_t{});break;
        case 23:f.memory.patch(0x200000,flash+16,std::numeric_limits<float>::infinity());break;
        case 24:f.request.controller_generation=0;break;
        case 25:f.request.controller_publication_milliseconds=1011;break;
        case 26:f.request.now_milliseconds=1151;break;
        case 27:f.request.now_milliseconds=0;break;
        case 28:f.request.controller_publication_milliseconds=0;break;
        case 29:f.memory.patch(0x200000,flash+16,1.F);break;
        case 30:f.memory.patch(0x22000,0,std::numeric_limits<float>::quiet_NaN());break;
        case 31:f.memory.moving_origin=true;break;
        case 32:f.memory.patch(0x820000,6,p.triangles);break;
        case 33:f.memory.patch(0x70000,off+1,std::uint8_t{1});break;
        case 34:f.memory.patch(0x30000,4,std::uint8_t{69});break;
        case 35:f.memory.patch(0x50000,4,std::uint32_t{0x30000});break;
        case 36:f.memory.patch(0x10000,9,std::uint8_t{1});break;
        case 37:f.memory.patch(0x10000,0x50+8,0x80000000U>>(6+p.bones-1));break;
        case 38:f.request.native_skeleton_origin_address=0;break;
        case 39:f.memory.patch(0x200000,flash+12,.2F);break;
        case 40:f.request.evaluated_flash_world_position_valid=false;break;
        case 41:f.request.evaluated_flash_world_position.x=std::numeric_limits<float>::quiet_NaN();break;
        case 42:f.request.evaluated_flash_world_position.y+=.01F;break;
        case 43:f.memory.patch(0x22000,8,305.F);break;
        }
        Basis3f out=axis({0,0,.70710678F,.70710678F});const auto original=out;
        FieldRifleProof proof{};proof.model=0xABCDEF;proof.fingerprint=12345;
        check(f.derive(&out,&proof)!=FieldRifleStatus::accepted,"malformed/stale/mixed native rifle accepted");
        check(near(out.forward,original.forward)&&near(out.left,original.left)&&near(out.up,original.up)&&
            proof.model==0xABCDEF&&proof.fingerprint==12345,"rejection altered caller basis/proof");
    }
    Fixture f(p);Basis3f out{};
    for(const auto weapon:{"zombie_m1carbine","m1carbine_aperture","kar98k_scoped","zombie_kar98k","zombie_colt","garand","mosin_nagant"}){
        f.request.weapon=weapon;check(f.derive(&out)==FieldRifleStatus::not_applicable,"unrelated/variant weapon opted into rifle calibration");}
    // Regression: animated pose-placement origin can yield a perfectly
    // parallel mesh ray at the WRONG world position. Real tag-world getter
    // agreement is mandatory, not merely correct orientation/topology.
    Fixture wrong_origin(p);wrong_origin.memory.set(0x22100,Vec3f{100,205.2F,294.2F});
    wrong_origin.request.native_skeleton_origin_address=0x22100;
    check(wrong_origin.derive(&out)==FieldRifleStatus::world_position_mismatch,
        "wrong animated-pose origin accepted parallel but displaced world sight endpoints");
    Fixture missing_world(p);missing_world.request.evaluated_flash_world_position_valid=false;
    check(missing_world.derive(&out)==FieldRifleStatus::unevaluated,
        "missing actual getter world position was not rejected explicitly");
}
}
int main(){try{
    positives(field_rifle_detail::kCarbine);positives(field_rifle_detail::kKar98);
    negatives(field_rifle_detail::kCarbine);negatives(field_rifle_detail::kKar98);
    check(field_rifle_asset_for_weapon("m1carbine")==FieldRifleAsset::carbine&&
        field_rifle_asset_for_weapon("kar98k")==FieldRifleAsset::kar98,"exact identity lookup failed");
    check(field_rifle_model_for_asset(FieldRifleAsset::none).empty()&&field_rifle_recipe_id(FieldRifleAsset::none).empty(),"unknown asset has recipe");
    std::cout<<"field rifle sight basis: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
