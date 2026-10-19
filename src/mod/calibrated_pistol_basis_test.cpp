// SPDX-License-Identifier: GPL-3.0-only
#include "calibrated_pistol_basis.hpp"
#include "calibrated_pistol_recipe.hpp"
#include "sight_bind_transform.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>
namespace {
using namespace wawvr::mod;
using wawvr::xr::Vec3f; using wawvr::xr::Basis3f;
using pistol_detail::PistolBone;
unsigned checks{};
void check(bool ok,const char* message) {++checks;if(!ok)throw std::runtime_error(message);}
bool near(Vec3f a,Vec3f b,float tolerance=.00001F) {
    return std::abs(a.x-b.x)<tolerance&&std::abs(a.y-b.y)<tolerance&&std::abs(a.z-b.z)<tolerance;
}
Basis3f basis(std::array<float,4> q) {
    const float x=q[0],y=q[1],z=q[2],w=q[3],s=2.F/(x*x+y*y+z*z+w*w);
    return {{1-s*(y*y+z*z),s*(x*y+z*w),s*(x*z-y*w)},
        {s*(x*y-z*w),1-s*(x*x+z*z),s*(y*z+x*w)},
        {s*(x*z+y*w),s*(y*z-x*w),1-s*(x*x+y*y)}};
}
Vec3f compose(Basis3f a,Vec3f p) {return {a.forward.x*p.x+a.left.x*p.y+a.up.x*p.z,
    a.forward.y*p.x+a.left.y*p.y+a.up.y*p.z,a.forward.z*p.x+a.left.z*p.y+a.up.z*p.z};}
std::array<float,4> multiply(std::array<float,4> a,std::array<float,4> b) {
    return {a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1],
        a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0],
        a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3],
        a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]};
}
struct Memory {
    std::map<std::uintptr_t,std::vector<std::byte>> blocks;
    std::size_t reads{},bytes{},dobj_reads{}; bool deny{},unstable{};
    template<class T> void set(std::uintptr_t address,const T& value) {
        const auto* p=reinterpret_cast<const std::byte*>(&value); blocks[address]={p,p+sizeof(T)};
    }
    template<class T> void patch(std::uintptr_t address,std::size_t offset,const T& value) {
        auto& b=blocks.at(address); check(offset+sizeof(T)<=b.size(),"fixture patch exceeded allocation");
        std::memcpy(b.data()+offset,&value,sizeof(T));
    }
    void string(std::uintptr_t address,const char* value) {
        const auto* p=reinterpret_cast<const std::byte*>(value); blocks[address]={p,p+std::strlen(value)+1};
    }
    static bool read(void* context,std::uintptr_t address,void* output,std::size_t size) noexcept {
        auto& m=*static_cast<Memory*>(context);++m.reads;m.bytes+=size;
        if(m.deny)return false;
        for(const auto& [base,data]:m.blocks) if(address>=base&&address-base<=data.size()&&size<=data.size()-(address-base)) {
            std::memcpy(output,data.data()+address-base,size);
            if(m.unstable&&address==0x10000&&++m.dobj_reads==2)
                static_cast<std::byte*>(output)[0x60]^=std::byte{1};
            return true;
        }
        return false;
    }
};
struct Fixture {
    Memory memory;
    CalibratedPistolRequest request{0x20000,0x10000,"zombie_colt",{},10,1000,1010};
    Fixture() {
        memory.set(0x20000,std::array<std::byte,16>{});memory.patch(0x20000,0,std::uint32_t{0x21000});
        memory.patch(0x20000,12,std::uint32_t{0x40000});memory.string(0x21000,pistol_detail::kWeapon);
        memory.set(0x10000,std::array<std::byte,0x68>{});memory.patch(0x10000,9,std::uint8_t{2});
        memory.patch(0x10000,10,std::uint8_t{77});memory.patch(0x10000,0x64,std::uint32_t{0x50000});
        memory.patch(0x10000,0x44,std::uint32_t{77});memory.patch(0x10000,0x48,std::uint32_t{0x200000});
        memory.patch(0x10000,0x34+8,(0x80000000U>>6)|(0x80000000U>>7)|(0x80000000U>>12));
        memory.set(0x50000,std::array<std::uint32_t,2>{0x30000,0x40000});
        memory.set(0x30000,std::array<std::byte,8>{});memory.patch(0x30000,4,std::uint8_t{70});
        memory.set(0x40000,std::array<std::byte,0xE4>{});memory.patch(0x40000,0,std::uint32_t{0x41000});
        memory.patch(0x40000,4,std::uint8_t{7});memory.patch(0x40000,5,std::uint8_t{1});memory.patch(0x40000,6,std::uint8_t{5});
        memory.patch(0x40000,8,std::uint32_t{0x60000});memory.patch(0x40000,12,std::uint32_t{0x60100});
        memory.patch(0x40000,0x1C,std::uint32_t{0x61000});memory.patch(0x40000,0x20,std::uint32_t{0x70000});
        memory.patch(0x40000,0x24,std::uint32_t{0x90000});memory.string(0x41000,pistol_detail::kModel);
        std::array<std::uint16_t,7> ids{};std::array<std::uint8_t,6> parents{};
        memory.set(0x03702390,std::uint32_t{0x100000});
        for(unsigned i=0;i<7;++i) {ids[i]=std::uint16_t(100+i*10);if(i)parents[i-1]=std::uint8_t(i);
            memory.string(0x100000+ids[i]*12+4,pistol_detail::kBoneNames[i]);}
        memory.set(0x60000,ids);memory.set(0x60100,parents);memory.set(0x61000,pistol_detail::kBinds);
        memory.set(0x70000,std::array<std::byte,5*0x40>{});
        memory.patch(0x70000,2*0x40+2,std::uint16_t{1326});memory.patch(0x70000,2*0x40+4,std::uint16_t{1050});
        memory.patch(0x70000,2*0x40+0x1C,std::uint32_t{0x800000});memory.patch(0x70000,2*0x40+0x0C,std::uint32_t{0x810000});
        memory.patch(0x70000,2*0x40+0x24,std::uint32_t{1});memory.patch(0x70000,2*0x40+0x28,std::uint32_t{0x820000});
        memory.set(0x820000,std::array<std::uint16_t,6>{64,1326,0,1050,0,0});
        std::vector<std::byte> vertices(1326*32);std::vector<std::uint16_t> indices(1050*3);
        const auto vertex=[&](unsigned i,Vec3f p) {std::memcpy(vertices.data()+i*32,&p,12);};
        for(const auto& a:pistol_detail::kAnchors)vertex(a.index,a.point);
        for(const auto& t:pistol_detail::kTriangles)for(unsigned j=0;j<3;++j) {
            vertex(t.indices[j],t.points[j]);indices[t.index*3+j]=t.indices[j];}
        memory.blocks[0x800000]=vertices;
        const auto* ip=reinterpret_cast<const std::byte*>(indices.data());memory.blocks[0x810000]={ip,ip+indices.size()*2};
        memory.set(0x90000,std::array<std::uint32_t,5>{0,0,0x91000,0,0});
        memory.set(0x91000,std::uint32_t{0x92000});memory.string(0x92000,pistol_detail::kMaterial);
        std::array<PistolBone,77> matrices{};for(auto& b:matrices)b={{0,0,0,1},{},2};
        matrices[71]=pistol_detail::kBinds[1];memory.set(0x200000,matrices);
        request.evaluated_sight_basis=basis(pistol_detail::kBinds[1].quaternion);
        request.evaluated_sight_basis_valid=true; request.resolved_sight_tag=110;
    }
    CalibratedPistolStatus derive(Basis3f* output,CalibratedPistolProof* proof=nullptr) {
        return derive_calibrated_pistol_basis({&memory,&Memory::read},request,output,proof);
    }
    void world_rotation(std::array<float,4> q) {
        request.evaluated_flash_basis=basis(q);
        for(unsigned i:{70U,71U,76U}) {
            auto current=q;if(i==71)current=multiply(q,pistol_detail::kBinds[1].quaternion);
            if(i==71)request.evaluated_sight_basis=basis(current);
            const float norm=current[0]*current[0]+current[1]*current[1]+current[2]*current[2]+current[3]*current[3];
            memory.patch(0x200000,i*32,PistolBone{current,{3,4,5},2.F/norm});
        }
    }
};
void positive() {
    Fixture f;Basis3f out{};CalibratedPistolProof proof{};
    check(f.derive(&out,&proof)==CalibratedPistolStatus::accepted,"exact native Colt witness refused");
    check(proof.witness_fingerprint==0xF5FD9FB14247F922ULL&&proof.model==0x40000&&proof.sight_bone==71&&proof.skeleton_timestamp==77,"wrong proof identity");
    check(out.forward.x>.99999F&&out.forward.z>.0009F&&out.forward.z<.0011F,"Colt inverse-bind cancellation or measured slope is wrong");
    check(f.memory.bytes<12000&&f.memory.reads<300,"small production witness became unbounded full mesh read");
    const auto baseline=out.forward;
    for(const auto q:std::array<std::array<float,4>,7>{{{0,0,0,1},{0,0,.70710678F,.70710678F},{0,0,-.70710678F,.70710678F},
        {0,.70710678F,0,.70710678F},{.70710678F,0,0,.70710678F},{.5F,.5F,.5F,.5F},{0,1,0,0}}}) {
        Fixture rotated;rotated.world_rotation(q);
        check(rotated.derive(&out,&proof)==CalibratedPistolStatus::accepted,"rotated/rolled Colt refused");
        check(near(out.forward,compose(basis(q),baseline)),"Colt correction became global pitch/head forward or lost bind inverse");
    }
    Fixture age;age.request.now_milliseconds=1150;
    check(age.derive(&out)==CalibratedPistolStatus::accepted,"inclusive original150ms freshness boundary refused");
}
void negatives() {
    for(unsigned n=0;n<30;++n) {
        Fixture f;
        switch(n) {
        case 0:f.memory.deny=true;break;
        case 1:f.memory.string(0x41000,"viewmodel_zombie_colt45_pistol");break;
        case 2:f.memory.string(0x21000,"colt");break;
        case 3:f.memory.patch(0x10000,10,std::uint8_t{76});break;
        case 4:f.memory.patch(0x40000,6,std::uint8_t{6});break;
        case 5:f.memory.patch(0x60100,0,std::uint8_t{2});break;
        case 6:f.memory.patch(0x61000,32+4,.01F);break;
        case 7:f.memory.patch(0x61000,32+16,0.F);break;
        case 8:f.memory.patch(0x70000,2*0x40+2,std::uint16_t{1325});break;
        case 9:f.memory.patch(0x820000,0,std::uint16_t{0});break;
        case 10:f.memory.patch(0x800000,1185*32,5.34F);break;
        case 11:f.memory.patch(0x810000,907*6,std::uint16_t{1185});break;
        case 12:f.memory.string(0x92000,"mc/mtl_weapon_colt45_wet");break;
        case 13:f.memory.patch(0x10000,0x44,std::uint32_t{});break;
        case 14:f.memory.patch(0x10000,0x34+8,(0x80000000U>>6)|(0x80000000U>>12));break;
        case 15:f.memory.patch(0x10000,0x50+8,0x80000000U>>7);break;
        case 16:f.memory.unstable=true;break;
        case 17:f.memory.patch(0x200000,71*32,std::numeric_limits<float>::quiet_NaN());break;
        case 18:f.memory.patch(0x200000,71*32+28,0.F);break;
        case 19:f.request.evaluated_flash_basis.up.z=-1;break;
        case 20:f.request.evaluated_flash_basis=basis({0,0,.70710678F,.70710678F});break;
        case 21:f.memory.patch(0x70000,2*0x40+0x1C,std::uint32_t{0xFFFFFFF0});break;
        case 22:f.memory.patch(0x40000,8,std::uint32_t{});break;
        case 23:f.memory.patch(0x200000,76*32+16,std::numeric_limits<float>::infinity());break;
        case 24:f.memory.patch(0x61000,6*32+28,2.001F);break;
        case 25:f.request.evaluated_sight_basis_valid=false;break;
        case 26:f.request.resolved_sight_tag=0;break;
        case 27:f.request.resolved_sight_tag=120;break;
        case 28:f.request.evaluated_sight_basis={};break;
        case 29:f.request.evaluated_sight_basis.up.z=-1;break;
        }
        Basis3f out{{91,92,93},{94,95,96},{97,98,99}},before=out;
        CalibratedPistolProof proof{};proof.model=0xDEADBEEF;
        check(f.derive(&out,&proof)!=CalibratedPistolStatus::accepted,"foreign/stale/hidden/unstable geometry accepted");
        check(!std::memcmp(&out,&before,sizeof(out))&&proof.model==0xDEADBEEF,"rejection mutated caller basis/proof");
    }
    for(const auto name:{"colt","zombie_colt_upgraded","Zombie_colt","zombie_colt_extra",""}) {
        Fixture f;f.request.weapon=name;Basis3f out{};
        check(f.derive(&out)==CalibratedPistolStatus::not_applicable&&f.memory.reads==0,"other weapons changed or performed native reads");
    }
    for(unsigned n=0;n<5;++n) {
        Fixture f;switch(n) {case 0:f.request.now_milliseconds=1151;break;case 1:f.request.controller_generation=0;break;
        case 2:f.request.controller_publication_milliseconds=0;break;case 3:f.request.controller_publication_milliseconds=1011;break;
        case 4:f.request.now_milliseconds=0;break;}Basis3f out{};
        check(f.derive(&out)==CalibratedPistolStatus::stale&&f.memory.reads==0,"stale/future source got native authority");
    }
    Fixture f;check(f.derive(nullptr)==CalibratedPistolStatus::identity,"null output admitted");
    Vec3f sentinel{3,4,5};
    check(!inverse_sight_bind_vector({1,0,0},{0,0,0,0},2,&sentinel)&&near(sentinel,{3,4,5}),"bad inverse bind mutated output");
    check(!inverse_sight_bind_vector({1,0,0},{0,0,0,1},1,&sentinel),"inconsistent native bind weight admitted");
}
}
int main() {try {positive();negatives();std::cout<<checks<<" calibrated pistol checks passed\n";}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
