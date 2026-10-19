// SPDX-License-Identifier: GPL-3.0-only
#include "calibrated_rifle_basis.hpp"
#include "calibrated_rifle_recipe.hpp"
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
void require(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
float dot(Vec3f a,Vec3f b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
bool near(Vec3f a,Vec3f b,float epsilon=.00001F) {
    return std::abs(a.x-b.x)<epsilon&&std::abs(a.y-b.y)<epsilon&&std::abs(a.z-b.z)<epsilon;
}
struct Memory final {
    std::map<std::uintptr_t,std::vector<std::byte>> blocks;
    std::size_t reads{},bytes{}; unsigned model_reads{};
    bool deny{},unstable{};
    template<class T> void set(std::uintptr_t address,const T& value) {
        const auto* begin=reinterpret_cast<const std::byte*>(&value); blocks[address]={begin,begin+sizeof(value)};
    }
    void string(std::uintptr_t address,const char* text) {
        const auto* begin=reinterpret_cast<const std::byte*>(text); blocks[address]={begin,begin+std::strlen(text)+1};
    }
    template<class T> void patch(std::uintptr_t address,std::size_t offset,const T& value) {
        auto& data=blocks.at(address); require(offset+sizeof(value)<=data.size(),"fixture patch exceeded allocation");
        std::memcpy(data.data()+offset,&value,sizeof(value));
    }
    static bool read(void* context,std::uintptr_t address,void* output,std::size_t size) noexcept {
        auto& self=*static_cast<Memory*>(context); ++self.reads; self.bytes+=size;
        if (self.deny) return false;
        for (const auto& [base,data]:self.blocks)
            if (address>=base&&address-base<=data.size()&&size<=data.size()-(address-base)) {
                std::memcpy(output,data.data()+address-base,size);
                if (self.unstable&&address==0x40000&&++self.model_reads==2)
                    static_cast<std::byte*>(output)[0x60]^=std::byte{1};
                return true;
            }
        return false;
    }
};
struct Bone final { std::array<float,4> q{0,0,0,1}; Vec3f translation{}; float weight{2}; };
struct Fixture final {
    Memory memory{};
    const rifle_detail::AssetWitness* profile{};
    CalibratedRifleRequest request{};
    explicit Fixture(CalibratedRifleAsset asset):profile(rifle_detail::profile(asset)) {
        require(profile!=nullptr,"invalid fixture profile"); const auto& p=*profile;
        request={0x20000,0x10000,p.weapon,{}};
        memory.set(0x20000,std::array<std::byte,16>{}); memory.patch(0x20000,0,std::uint32_t{0x21000});
        memory.patch(0x20000,12,std::uint32_t{0x40000}); memory.string(0x21000,p.weapon);
        memory.set(0x10000,std::array<std::byte,0x68>{}); memory.patch(0x10000,9,std::uint8_t{2});
        memory.patch(0x10000,10,static_cast<std::uint8_t>(70+p.bones)); memory.patch(0x10000,0x64,std::uint32_t{0x50000});
        memory.set(0x50000,std::array<std::uint32_t,2>{0x30000,0x40000});
        memory.set(0x30000,std::array<std::byte,8>{}); memory.patch(0x30000,4,std::uint8_t{70});
        memory.set(0x40000,std::array<std::byte,0xE4>{}); memory.patch(0x40000,0,std::uint32_t{0x41000});
        memory.patch(0x40000,4,static_cast<std::uint8_t>(p.bones)); memory.patch(0x40000,5,std::uint8_t{1});
        memory.patch(0x40000,6,static_cast<std::uint8_t>(p.surfaces)); memory.patch(0x40000,8,std::uint32_t{0x60000});
        memory.patch(0x40000,12,std::uint32_t{0x60100}); memory.patch(0x40000,0x1C,std::uint32_t{0x61000});
        memory.patch(0x40000,0x20,std::uint32_t{0x70000}); memory.string(0x41000,p.model);
        std::array<std::uint16_t,9> ids{}; std::array<std::uint8_t,8> parents{}; std::array<Bone,9> binds{};
        memory.set(0x03702390,std::uint32_t{0x100000});
        for (unsigned i=0;i<p.bones;++i) {
            ids[i]=static_cast<std::uint16_t>(100+i*10); memory.string(0x100000+ids[i]*12+4,p.bone_names[i]);
            if (i) parents[i-1]=static_cast<std::uint8_t>(i);
        }
        binds[p.sight_bone].translation=p.sight_bind; binds[p.flash_bone].translation=p.flash_bind;
        memory.set(0x60000,ids); memory.set(0x60100,parents); memory.set(0x61000,binds);
        memory.set(0x70000,std::array<std::byte,7*0x40>{});
        for (unsigned n=0;n<p.meshes.size();++n) {
            const auto& mesh=p.meshes[n]; const unsigned base=mesh.surface*0x40;
            const unsigned va=0x800000+n*0x20000,ia=va+0x10000,ra=va+0x18000;
            memory.patch(0x70000,base+2,static_cast<std::uint16_t>(mesh.vertices));
            memory.patch(0x70000,base+4,static_cast<std::uint16_t>(mesh.triangles));
            memory.patch(0x70000,base+0x1C,va); memory.patch(0x70000,base+0x0C,ia);
            memory.patch(0x70000,base+0x24,static_cast<std::uint32_t>(mesh.ranges.size())); memory.patch(0x70000,base+0x28,ra);
            std::vector<std::byte> vertices(mesh.vertices*32); std::vector<bool> used(mesh.vertices);
            for (const auto& anchor:mesh.anchors) {
                std::memcpy(vertices.data()+anchor.index*32,&anchor.point,12); used[anchor.index]=true;
            }
            std::vector<std::uint16_t> indices(mesh.triangles*3);
            for (const auto& triangle:mesh.topology) for (unsigned j=0;j<3;++j) {
                const auto point=triangle.points[j]; unsigned found=mesh.vertices;
                for (unsigned i=mesh.owned_first;i<mesh.owned_first+mesh.owned_count;++i)
                    if (used[i]&&std::memcmp(vertices.data()+i*32,&point,12)==0) { found=i; break; }
                if (found==mesh.vertices) for (unsigned i=mesh.owned_first;i<mesh.owned_first+mesh.owned_count;++i)
                    if (!used[i]) { found=i; used[i]=true; std::memcpy(vertices.data()+i*32,&point,12); break; }
                require(found<mesh.vertices,"fixture exhausted owned range");
                indices[triangle.index*3+j]=static_cast<std::uint16_t>(found);
            }
            memory.blocks[va]=vertices;
            const auto* first=reinterpret_cast<const std::byte*>(indices.data()); memory.blocks[ia]={first,first+indices.size()*2};
            std::array<std::uint16_t,18> ranges{};
            for (unsigned i=0;i<mesh.ranges.size();++i) {
                ranges[i*6]=static_cast<std::uint16_t>(mesh.ranges[i].bone*64);
                ranges[i*6+1]=static_cast<std::uint16_t>(mesh.ranges[i].vertices);
                ranges[i*6+2]=static_cast<std::uint16_t>(mesh.ranges[i].first_triangle);
                ranges[i*6+3]=static_cast<std::uint16_t>(mesh.ranges[i].triangles);
            }
            memory.set(ra,ranges);
        }
    }
    CalibratedRifleStatus derive(Basis3f* out,CalibratedRifleProof* proof=nullptr) {
        return derive_calibrated_rifle_basis({&memory,&Memory::read},request,out,proof);
    }
};
void positive_tests() {
    for (const auto asset:{CalibratedRifleAsset::garand,CalibratedRifleAsset::mosin}) {
        Fixture f(asset); Basis3f basis{}; CalibratedRifleProof proof{};
        require(f.derive(&basis,&proof)==CalibratedRifleStatus::accepted,"native small geometry fixture rejected");
        require(proof.asset==asset&&proof.witness_fingerprint==f.profile->fingerprint,"wrong exact geometry witness");
        require(f.memory.bytes<10000&&f.memory.reads<250,"bounded production reader became a full mesh hot-path scan");
        if (asset==CalibratedRifleAsset::garand) {
            require(near(proof.rear_reference,{-5.794098854F,.00216050646F,2.204948779F},.000002F),"native aperture fit changed");
            require(near(proof.front_reference,{25.974988937F,0,2.688856483F},.000002F),"central post bevel midpoint changed");
            require(basis.forward.z>.015F&&basis.forward.z<.016F&&basis.forward.x>.9998F,
                "Garand geometry ray replaced by either grip segment or uncorrected flash axis");
        } else require(near(basis.forward,{1,0,0}),"Mosin false root-to-elevated-muzzle pitch remains");
        const auto local=proof.local_direction;
        for (const auto axis:std::array<Basis3f,4>{{{{0,1,0},{-1,0,0},{0,0,1}},
            {{0,-1,0},{1,0,0},{0,0,1}},{{0,1,0},{0,0,1},{1,0,0}},{{0,1,0},{0,0,-1},{-1,0,0}}}}) {
            f.request.evaluated_flash_basis=axis;
            require(f.derive(&basis)==CalibratedRifleStatus::accepted,"off-axis/rolled native rifle rejected");
            const Vec3f expected{axis.forward.x*local.x+axis.left.x*local.y+axis.up.x*local.z,
                axis.forward.y*local.x+axis.left.y*local.y+axis.up.y*local.z,
                axis.forward.z*local.x+axis.left.z*local.y+axis.up.z*local.z};
            require(near(basis.forward,expected)&&std::abs(dot(basis.forward,basis.left))<.00001F&&
                std::abs(dot(basis.forward,basis.up))<.00001F,"correction is world-pitch dependent or lost weapon roll");
        }
    }
}
void negative_tests() {
    for (const auto asset:{CalibratedRifleAsset::garand,CalibratedRifleAsset::mosin}) for (unsigned i=0;i<16;++i) {
        Fixture f(asset); const auto& mesh=f.profile->meshes[0];
        switch(i) {
        case 0:f.memory.deny=true;break;
        case 1:f.memory.patch(0x20000,12,std::uint32_t{0x30000});break;
        case 2:f.memory.string(0x41000,"viewmodel_other_rifle");break;
        case 3:f.memory.patch(0x40000,4,std::uint8_t{6});break;
        case 4:f.memory.patch(0x60100,0,std::uint8_t{2});break;
        case 5:f.memory.patch(0x61000,f.profile->flash_bone*32+16,123.F);break;
        case 6:f.memory.patch(0x61000,f.profile->sight_bone*32,.001F);break;
        case 7:f.memory.patch(0x70000,mesh.surface*0x40+1,std::uint8_t{1});break;
        case 8:f.memory.patch(0x70000,mesh.surface*0x40+2,std::uint16_t{1});break;
        case 9:f.memory.patch(0x818000,0,std::uint16_t{64});break;
        case 10:f.memory.patch(0x800000,mesh.anchors[0].index*32,mesh.anchors[0].point.x+.0001F);break;
        case 11:f.memory.patch(0x810000,mesh.topology[0].index*6,std::uint16_t{0xFFFF});break;
        case 12:f.memory.unstable=true;break;
        case 13:f.request.evaluated_flash_basis.forward.x=std::numeric_limits<float>::quiet_NaN();break;
        case 14:f.memory.patch(0x10000,9,std::uint8_t{1});break;
        default:f.memory.string(0x21000,"unsupported_variant");break;
        }
        Basis3f sentinel{{.3F,.4F,.5F},{.6F,.7F,.8F},{.9F,1.F,1.1F}},out=sentinel;
        CalibratedRifleProof proof{}; proof.witness_fingerprint=0xABCD;
        require(f.derive(&out,&proof)!=CalibratedRifleStatus::accepted,"mismatched native asset accepted");
        require(std::memcmp(&out,&sentinel,sizeof(out))==0&&proof.witness_fingerprint==0xABCD,
            "rejection corrupted accepted prior basis/proof");
    }
}
void identity_tests() {
    require(calibrated_rifle_asset_for_identity("m1garand","viewmodel_usa_m1garand_rifle")==CalibratedRifleAsset::garand,
        "exact Garand identity not selected");
    require(calibrated_rifle_asset_for_identity("mosin_rifle","viewmodel_rus_mosinnagant_rifle")==CalibratedRifleAsset::mosin,
        "exact unscoped Mosin identity not selected");
    for (const auto* name:{"mosinrifle","mosin_rifle_scoped","m1garand_gl","m1garand_bayonet","ppsh","svt40","M1GARAND",""}) {
        Fixture f(CalibratedRifleAsset::garand); f.request.weapon=name; Basis3f out{};
        require(f.derive(&out)==CalibratedRifleStatus::not_applicable&&f.memory.reads==0,
            "protected/unknown profile entered production geometry reader");
        require(calibrated_rifle_asset_for_identity(name,"viewmodel_usa_m1garand_rifle")==CalibratedRifleAsset::none,
            "scope/attachment/accepted other weapon was selected");
    }
    require(hitscan_uses_tag_flash_forward("ppsh")&&hitscan_uses_tag_flash_forward("svt40")&&
        !hitscan_uses_tag_flash_forward("m1garand")&&!hitscan_uses_tag_flash_forward("mosin_rifle_scoped"),
        "accepted flash-axis allowlist or scoped policy changed");
}
} // namespace
int main() {
    try { positive_tests(); negative_tests(); identity_tests(); std::cout<<"Calibrated rifle native geometry/basis/fallback tests passed\n"; }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
