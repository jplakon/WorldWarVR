// SPDX-License-Identifier: GPL-3.0-only
#include "sight_geometry_basis.hpp"
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
using namespace wawvr::mod;
using wawvr::xr::Vec3f;using wawvr::xr::Basis3f;
unsigned checks{};
void check(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
bool near(Vec3f a,Vec3f b,float t=.00005F){return std::abs(a.x-b.x)<t&&std::abs(a.y-b.y)<t&&std::abs(a.z-b.z)<t;}
Basis3f axis(std::array<float,4> q){const float x=q[0],y=q[1],z=q[2],w=q[3],s=2.F/(x*x+y*y+z*z+w*w);
    return {{1-s*(y*y+z*z),s*(x*y+z*w),s*(x*z-y*w)},
        {s*(x*y-z*w),1-s*(x*x+z*z),s*(y*z+x*w)},
        {s*(x*z+y*w),s*(y*z-x*w),1-s*(x*x+y*y)}};}
Vec3f compose(Basis3f a,Vec3f p){return {a.forward.x*p.x+a.left.x*p.y+a.up.x*p.z,
    a.forward.y*p.x+a.left.y*p.y+a.up.y*p.z,a.forward.z*p.x+a.left.z*p.y+a.up.z*p.z};}
Vec3f add(Vec3f a,Vec3f b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3f subtract(Vec3f a,Vec3f b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
struct Memory {
    std::map<std::uintptr_t,std::vector<std::byte>> blocks;
    std::size_t reads{},bytes{},dobj_reads{};bool deny{},unstable{};
    template<class T>void set(std::uintptr_t address,const T& v){const auto* p=reinterpret_cast<const std::byte*>(&v);blocks[address]={p,p+sizeof(v)};}
    template<class T>void patch(std::uintptr_t address,std::size_t offset,const T& v){auto& b=blocks.at(address);
        check(offset+sizeof(v)<=b.size(),"fixture patch overflow");std::memcpy(b.data()+offset,&v,sizeof(v));}
    void text(std::uintptr_t address,std::string_view s){const auto* p=reinterpret_cast<const std::byte*>(s.data());
        blocks[address]={p,p+s.size()};blocks[address].push_back(std::byte{});}
    static bool read(void* context,std::uintptr_t address,void* output,std::size_t count)noexcept{
        auto& m=*static_cast<Memory*>(context);++m.reads;m.bytes+=count;if(m.deny)return false;
        for(const auto& [base,data]:m.blocks)if(address>=base&&address-base<=data.size()&&count<=data.size()-(address-base)){
            std::memcpy(output,data.data()+address-base,count);
            if(m.unstable&&address==0x10000&&++m.dobj_reads==2)static_cast<std::byte*>(output)[0x60]^=std::byte{1};
            return true;
        }return false;
    }
};
// Deliberately synthetic geometry and addresses. It validates parser/math
// behavior only; these points must never enter a production weapon bank.
struct Fixture {
    Memory memory;
    std::array<std::string_view,4> names{"j_gun","sight_front","sight_rear","tag_flash"};
    std::array<std::uint8_t,3> parents{1,2,3};
    std::array<SightGeometryBone,4> binds{{{{0,0,0,1},{},2},{{0,0,0,1},{2,0,0},2},
        {{0,0,0,1},{0,0,0},2},{{0,0,0,1},{10,0,0},2}}};
    std::array<SightGeometryVertex,6> vertices{{{0,{10,-.1F,1}}, {1,{10,.1F,1}}, {2,{10,0,.8F}},
        {3,{0,-.1F,.5F}}, {4,{0,.1F,.5F}}, {5,{0,0,.3F}}}};
    std::array<SightGeometryTriangle,2> triangles{{{0,{0,1,2}},{1,{3,4,5}}}};
    std::array<SightGeometryRigidRange,2> ranges{{{1,3,0,1},{2,3,1,1}}};
    std::array<SightGeometrySurface,1> surfaces{};
    std::array<SightGeometryReferenceVertex,2> front{{{0,0,1},{0,1,1}}},rear{{{0,3,1},{0,4,1}}};
    SightGeometryRecipe recipe{};
    std::array<SightGeometryProfile,1> profiles{};
    SightGeometryRequest request{};
    std::uint16_t offset{69};
    explicit Fixture(unsigned model_count=2,unsigned before=1){
        surfaces[0]={0,6,2,"mc/synthetic_sight_fixture",ranges,vertices,triangles};
        recipe={"synthetic-test-only","viewmodel_synthetic_test_only",names,parents,binds,1,0,3,surfaces,
            {1,SightGeometryReferenceMethod::weighted_centroid,front},
            {2,SightGeometryReferenceMethod::weighted_centroid,rear},0};
        seal();profiles[0]={"synthetic_weapon",recipe.model,&recipe};
        offset=static_cast<std::uint16_t>(before?69:0);
        request={0x20000,0x10000,profiles[0].weapon,profiles[0].model,axis({0,0,0,1}),{110,200,300},true,
            10,1000,1010,0x22000,0x23000};
        memory.set(0x22000,Vec3f{100,200,300});memory.set(0x23000,std::uint32_t{0x100000});
        memory.set(0x20000,std::array<std::byte,16>{});memory.patch(0x20000,0,std::uint32_t{0x21000});
        memory.patch(0x20000,12,std::uint32_t{0x40000});memory.text(0x21000,profiles[0].weapon);
        memory.set(0x10000,std::array<std::byte,0x68>{});memory.patch(0x10000,9,std::uint8_t(model_count));
        memory.patch(0x10000,0x64,std::uint32_t{0x50000});memory.patch(0x10000,0x44,std::uint32_t{77});
        memory.patch(0x10000,0x48,std::uint32_t{0x200000});
        std::array<std::uint32_t,8> models{};unsigned total{};
        for(unsigned i=0;i<model_count;++i){models[i]=i==before?0x40000:0x30000+i*0x100;
            if(i!=before){memory.set(models[i],std::array<std::byte,8>{});
                const std::uint8_t n=i==0&&before?69:1;memory.patch(models[i],4,n);total+=n;}else total+=4;}
        memory.set(0x50000,models);memory.patch(0x10000,10,std::uint8_t(total));
        memory.set(0x40000,std::array<std::byte,0xE4>{});memory.patch(0x40000,0,std::uint32_t{0x41000});
        memory.patch(0x40000,4,std::uint8_t{4});memory.patch(0x40000,5,std::uint8_t{1});
        memory.patch(0x40000,6,std::uint8_t{1});memory.patch(0x40000,8,std::uint32_t{0x60000});
        memory.patch(0x40000,12,std::uint32_t{0x60100});memory.patch(0x40000,0x1C,std::uint32_t{0x61000});
        memory.patch(0x40000,0x20,std::uint32_t{0x70000});memory.patch(0x40000,0x24,std::uint32_t{0x90000});
        memory.text(0x41000,recipe.model);
        std::array<std::uint16_t,4> ids{};for(unsigned i=0;i<4;++i){ids[i]=std::uint16_t(100+i*10);
            memory.text(0x100000+ids[i]*12+4,names[i]);}
        memory.set(0x60000,ids);memory.set(0x60100,parents);memory.set(0x61000,binds);
        memory.set(0x70000,std::array<std::byte,0x40>{});memory.patch(0x70000,2,std::uint16_t{6});
        memory.patch(0x70000,4,std::uint16_t{2});memory.patch(0x70000,0x0C,std::uint32_t{0x830000});
        memory.patch(0x70000,0x1C,std::uint32_t{0x800000});memory.patch(0x70000,0x24,std::uint32_t{2});
        memory.patch(0x70000,0x28,std::uint32_t{0x820000});
        memory.set(0x820000,std::array<std::uint16_t,12>{64,3,0,1,0,0,128,3,1,1,0,0});
        memory.set(0x800000,std::array<std::byte,6*32>{});for(unsigned i=0;i<vertices.size();++i)
            memory.patch(0x800000,i*32,vertices[i].point);
        memory.set(0x830000,std::array<std::uint16_t,6>{0,1,2,3,4,5});
        memory.set(0x90000,std::uint32_t{0x91000});memory.set(0x91000,std::uint32_t{0x92000});
        memory.text(0x92000,surfaces[0].material);
        std::array<SightGeometryBone,128> matrices{};for(auto& b:matrices)b={{0,0,0,1},{},2};
        for(unsigned i=0;i<4;++i)matrices[offset+i]=binds[i];memory.set(0x200000,matrices);
        for(unsigned i=0;i<4;++i){const auto global=offset+i,word=(global>>5U)*4U;
            auto& block=memory.blocks.at(0x10000);std::uint32_t mask{};std::memcpy(&mask,block.data()+0x34+word,4);
            mask|=0x80000000U>>(global&31U);memory.patch(0x10000,0x34+word,mask);}
    }
    void seal(){recipe.fingerprint=sight_geometry_recipe_fingerprint(recipe);check(recipe.fingerprint!=0,"fixture invalid recipe");}
    SightGeometryStatus derive(Basis3f* out,SightGeometryProof* proof=nullptr){return derive_sight_geometry_basis(
        {&memory,Memory::read},request,profiles,out,proof);}
    void rotate(std::array<float,4> q,Vec3f translation={3,4,5}){
        const auto a=axis(q);float norm{};for(float v:q)norm+=v*v;
        for(unsigned i=0;i<4;++i)memory.patch(0x200000,(offset+i)*32,
            SightGeometryBone{q,add(translation,compose(a,binds[i].translation)),2.F/norm});
        request.evaluated_flash_basis=a;
        request.evaluated_flash_world=add(add({100,200,300},translation),compose(a,binds[3].translation));
    }
};
void positive(){
    Fixture f;Basis3f out{};SightGeometryProof proof{};
    check(f.derive(&out,&proof)==SightGeometryStatus::accepted,"synthetic exact witness rejected");
    check(near(proof.front_reference,{10,0,1})&&near(proof.rear_reference,{0,0,.5F}),"reference centroid wrong");
    check(near(proof.world_front,{110,200,301})&&near(proof.world_rear,{100,200,300.5F}),"independent bone world points wrong");
    check(proof.root_bone==69&&proof.front_bone==70&&proof.rear_bone==71&&proof.flash_bone==72,
        "dynamic preceding model bones ignored");
    check(proof.fingerprint==f.recipe.fingerprint&&proof.profile==&f.profiles[0]&&proof.recipe==&f.recipe,
        "proof identity or immutable fingerprint wrong");
    const auto baseline=out.forward;
    check(std::abs(std::atan2(baseline.z,baseline.x)*57.2957795F-2.862405F)<.0001F,"sight ray wrong");
    check(f.memory.reads<110&&f.memory.bytes<10000,"small sparse witness unexpectedly unbounded");
    const std::array<std::array<float,4>,8> rotations{{{0,0,0,1},{0,0,.70710678F,.70710678F},{0,0,-.70710678F,.70710678F},
        {0,.70710678F,0,.70710678F},{.70710678F,0,0,.70710678F},{.5F,.5F,.5F,.5F},{0,1,0,0},{.25F,.5F,.25F,.7905694F}}};
    for(const auto& q:rotations){Fixture r;r.rotate(q);check(r.derive(&out,&proof)==SightGeometryStatus::accepted,"rotated exact witness rejected");
        check(near(out.forward,compose(axis(q),baseline)),"local sight line became a global pitch offset");
        check(near(proof.evaluated_root_basis.forward,axis(q).forward),"root bone proof wrong");}
    for(unsigned models:{1U,3U,8U}){Fixture r(models,models==1?0:1);
        check(r.derive(&out,&proof)==SightGeometryStatus::accepted,"valid multi-model DObj rejected");
        check(proof.front_bone==(models==1?1:70),"DObj model offset guessed");}
    Fixture age;age.request.now_milliseconds=1150;check(age.derive(&out)==SightGeometryStatus::accepted,"inclusive150ms rejected");
    // Move ONLY the front evaluated owner. The world sight must change even
    // though rear/root/flash stay unchanged; borrowing a common root is wrong.
    Fixture independent;independent.memory.patch(0x200000,(independent.offset+1)*32+24,1.F);
    check(independent.derive(&out,&proof)==SightGeometryStatus::accepted,"independently animated front owner rejected");
    check(near(proof.world_front,{110,200,302})&&near(proof.world_rear,{100,200,300.5F}),"front borrowed rear/root bone");
    // A nonidentity absolute bind rotation must be removed before applying
    // the current native owner. All geometry remains authored model-space.
    Fixture bind;const std::array<float,4> q{0,0,.70710678F,.70710678F};bind.binds[1].quaternion=q;
    bind.binds[1].weight=2.F;bind.memory.patch(0x61000,32,bind.binds[1]);bind.seal();
    check(bind.derive(&out,&proof)==SightGeometryStatus::accepted,"nonidentity front bind rejected");
    const auto d=subtract(Vec3f{10,0,1},Vec3f{2,0,0});const auto b=axis(q);
    const Vec3f expected{b.forward.x*d.x+b.forward.y*d.y+b.forward.z*d.z,
        b.left.x*d.x+b.left.y*d.y+b.left.z*d.z,b.up.x*d.x+b.up.y*d.y+b.up.z*d.z};
    check(near(proof.world_front,add({102,200,300},expected)),"authored bind rotation not inversed");
}
void negative(){
    for(unsigned n=0;n<43;++n){Fixture f;const auto global=unsigned(f.offset)+1U,word=(global>>5U)*4U,mask=0x80000000U>>(global&31U);
        switch(n){
        case 0:f.memory.deny=true;break;case 1:f.memory.text(0x21000,"different_weapon");break;
        case 2:f.memory.text(0x41000,"viewmodel_custom_test");break;case 3:f.memory.patch(0x10000,9,std::uint8_t{9});break;
        case 4:f.memory.patch(0x10000,10,std::uint8_t{72});break;case 5:f.memory.patch(0x50000,0,std::uint32_t{0x40000});break;
        case 6:f.memory.patch(0x40000,4,std::uint8_t{5});break;case 7:f.memory.patch(0x40000,5,std::uint8_t{2});break;
        case 8:f.memory.patch(0x60100,0,std::uint8_t{2});break;case 9:f.memory.patch(0x61000,16,1.F);break;
        case 10:f.memory.text(0x92000,"mc/unrelated");break;case 11:f.memory.patch(0x70000,1,std::uint8_t{1});break;
        case 12:f.memory.patch(0x70000,2,std::uint16_t{7});break;case 13:f.memory.patch(0x70000,4,std::uint16_t{3});break;
        case 14:f.memory.patch(0x820000,0,std::uint16_t{0});break;case 15:f.memory.patch(0x800000,0,99.F);break;
        case 16:f.memory.patch(0x830000,0,std::uint16_t{3});break;case 17:f.memory.patch(0x10000,0x44,std::uint32_t{0});break;
        case 18:f.memory.patch(0x10000,0x50+word,mask);break;case 19:f.memory.patch(0x10000,0x34+word,std::uint32_t{});break;
        case 20:f.memory.patch(0x200000,global*32,std::numeric_limits<float>::quiet_NaN());break;
        case 21:f.memory.patch(0x200000,global*32+28,0.F);break;case 22:f.memory.unstable=true;break;
        case 23:f.request.evaluated_flash_basis.up.z=-1;break;case 24:f.request.evaluated_flash_basis=axis({0,0,.70710678F,.70710678F});break;
        case 25:f.request.evaluated_flash_world.y+=.01F;break;case 26:f.request.evaluated_flash_world_valid=false;break;
        case 27:f.request.controller_generation=0;break;case 28:f.request.controller_publication_milliseconds=1011;break;
        case 29:f.request.now_milliseconds=1151;break;case 30:f.request.now_milliseconds=0;break;
        case 31:f.request.controller_publication_milliseconds=0;break;case 32:f.request.native_skeleton_origin_address=0;break;
        case 33:f.request.script_string_pool_address=0;break;case 34:f.memory.patch(0x23000,0,std::uint32_t{0xFFFFFFF0});break;
        case 35:f.memory.patch(0x70000,0x1C,std::uint32_t{0xFFFFFFF0});break;
        case 36:f.request.evaluated_flash_world.x=std::numeric_limits<float>::infinity();break;
        case 37:f.memory.patch(0x22000,0,std::numeric_limits<float>::quiet_NaN());break;
        case 38:f.memory.patch(0x200000,(f.offset+3)*32+16,11.F);break;
        case 39:f.recipe.fingerprint^=1;break;case 40:f.recipe.fingerprint=0;break;
        case 41:f.recipe.model="different_model";break;case 42:f.profiles[0].recipe=nullptr;break;
        }
        Basis3f out=axis({0,0,.70710678F,.70710678F});const auto original=out;
        SightGeometryProof proof{};proof.model=0xABCDEF;proof.fingerprint=12345;
        check(f.derive(&out,&proof)!=SightGeometryStatus::accepted,"malformed/stale/mixed native witness accepted");
        check(std::memcmp(&out,&original,sizeof(out))==0&&proof.model==0xABCDEF&&proof.fingerprint==12345,
            "rejection modified output/proof");
    }
    Fixture f;Basis3f out{};
    for(const auto name:{"synthetic_weapon_scoped","zombie_synthetic_weapon","synthetic_weapon_upgraded","synthetic"}){
        f.request.weapon=name;check(f.derive(&out)==SightGeometryStatus::not_applicable,"name prefix or variant matched");}
    f.request.weapon=f.profiles[0].weapon;
    const std::array<SightGeometryProfile,2> duplicate{f.profiles[0],f.profiles[0]};
    check(find_sight_geometry_profile(duplicate,f.request.weapon,f.request.model)==nullptr,"duplicate exact identities guessed");
}
void recipe_negatives(){
    for(unsigned n=0;n<15;++n){Fixture f;
        switch(n){case 0:f.names[0]="root_not_gun";break;case 1:f.names[3]="not_flash";break;
        case 2:f.parents[0]=0;break;case 3:f.parents[0]=2;break;case 4:f.ranges[0].bone=8;break;
        case 5:f.ranges[0].vertices=7;break;case 6:f.ranges[1].first_triangle=0;break;
        case 7:f.vertices[0].index=99;break;case 8:f.vertices[0].point.x=std::numeric_limits<float>::quiet_NaN();break;
        case 9:f.triangles[0].vertices[0]=3;break;case 10:f.front[0].weight=0;break;
        case 11:f.front[0].vertex=3;break;case 12:f.front[1]=f.front[0];break;
        case 13:f.recipe.front.owner_bone=2;break;case 14:f.triangles[0].vertices[1]=f.triangles[0].vertices[0];break;}
        check(sight_geometry_recipe_fingerprint(f.recipe)==0,"invalid/unbacked immutable recipe sealed");
    }
}
void circle(){
    Fixture f;
    // Four explicitly selected inner-aperture points on y/z circle center
    // (0,.5), radius.1, with an extra center point used only for triangles.
    std::array<SightGeometryVertex,8> vertices{{{0,{10,-.1F,1}},{1,{10,.1F,1}},{2,{10,0,.8F}},
        {3,{0,-.1F,.5F}},{4,{0,.1F,.5F}},{5,{0,0,.4F}},{6,{0,0,.6F}},{7,{0,0,.5F}}}};
    std::array<SightGeometryTriangle,5> triangles{{{0,{0,1,2}},{1,{3,5,7}},{2,{5,4,7}},{3,{4,6,7}},{4,{6,3,7}}}};
    std::array<SightGeometryRigidRange,2> ranges{{{1,3,0,1},{2,5,1,4}}};
    std::array<SightGeometryReferenceVertex,4> ring{{{0,3,1},{0,4,1},{0,5,1},{0,6,1}}};
    f.surfaces[0]={0,8,5,f.surfaces[0].material,ranges,vertices,triangles};
    f.recipe.rear={2,SightGeometryReferenceMethod::aperture_circle_yz,ring};f.seal();
    f.memory.patch(0x70000,2,std::uint16_t{8});f.memory.patch(0x70000,4,std::uint16_t{5});
    f.memory.set(0x820000,std::array<std::uint16_t,12>{64,3,0,1,0,0,128,5,1,4,0,0});
    f.memory.set(0x800000,std::array<std::byte,8*32>{});for(unsigned i=0;i<vertices.size();++i)f.memory.patch(0x800000,i*32,vertices[i].point);
    f.memory.set(0x830000,std::array<std::uint16_t,15>{0,1,2,3,5,7,5,4,7,4,6,7,6,3,7});
    Basis3f out{};SightGeometryProof proof{};
    check(f.derive(&out,&proof)==SightGeometryStatus::accepted,"exact aperture witness rejected");
    check(near(proof.rear_reference,{0,0,.5F}),"aperture fit chose hood or wrong center");
}
void basis_kinds(){
    Fixture f;Basis3f out{};SightGeometryProof proof{};
    check(f.recipe.basis_kind==SightGeometryBasisKind::iron_sight,"legacy recipe aggregate default changed");
    check(f.derive(&out,&proof)==SightGeometryStatus::accepted&&proof.basis_kind==SightGeometryBasisKind::iron_sight,
        "iron-sight category was not carried into proof");
    const auto iron_fingerprint=f.recipe.fingerprint;
    f.recipe.basis_kind=SightGeometryBasisKind::barrel_axis;
    check(sight_geometry_recipe_fingerprint(f.recipe)!=iron_fingerprint,"basis kind absent from immutable fingerprint");
    const auto before=out;proof.model=0xABCDEF;
    check(f.derive(&out,&proof)==SightGeometryStatus::invalid_recipe&&proof.model==0xABCDEF&&
        std::memcmp(&out,&before,sizeof(out))==0,"changed category accepted old digest or altered output");
    f.seal();check(f.derive(&out,&proof)==SightGeometryStatus::accepted&&proof.basis_kind==SightGeometryBasisKind::barrel_axis,
        "explicit witnessed barrel category not accepted/carried");
    f.recipe.basis_kind=static_cast<SightGeometryBasisKind>(255);
    check(sight_geometry_recipe_fingerprint(f.recipe)==0,"unknown basis kind received valid digest");
    check(f.derive(&out,&proof)==SightGeometryStatus::invalid_recipe,"unknown basis kind derived a ray");
    check(std::string_view(sight_geometry_basis_kind_name(SightGeometryBasisKind::iron_sight))=="iron_sight"&&
        std::string_view(sight_geometry_basis_kind_name(SightGeometryBasisKind::barrel_axis))=="barrel_axis"&&
        std::string_view(sight_geometry_basis_kind_name(static_cast<SightGeometryBasisKind>(255)))=="unknown",
        "basis kind diagnostic names changed");
}
}
int main(){try{positive();negative();recipe_negatives();circle();basis_kinds();
    std::cout<<"generic sight geometry basis: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
