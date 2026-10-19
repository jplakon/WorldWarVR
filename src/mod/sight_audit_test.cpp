// SPDX-License-Identifier: GPL-3.0-only
#include "sight_audit_logic.hpp"
#include "sight_audit_recipes.hpp"
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
using wawvr::xr::Vec3f;
void require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
struct Memory final {
    std::map<std::uintptr_t,std::vector<std::byte>> blocks;
    bool deny{},unstable{},unstable_geometry{}; unsigned dobj_reads{},geometry_reads{};
    template<class T> void set(std::uintptr_t address,const T& value) {
        const auto* begin=reinterpret_cast<const std::byte*>(&value);
        blocks[address]={begin,begin+sizeof(value)};
    }
    template<class T> void patch(std::uintptr_t address,std::size_t offset,const T& value) {
        auto& bytes=blocks.at(address); require(offset+sizeof(value)<=bytes.size(),"fixture patch out of range");
        std::memcpy(bytes.data()+offset,&value,sizeof(value));
    }
    void string(std::uintptr_t address,const char* value) {
        const auto* begin=reinterpret_cast<const std::byte*>(value);
        blocks[address]={begin,begin+std::strlen(value)+1};
    }
    static bool read(void* context,std::uintptr_t address,void* output,std::size_t size) noexcept {
        auto& m=*static_cast<Memory*>(context);
        if (m.deny) return false;
        for (const auto& [base,bytes]:m.blocks) if (address>=base&&address-base<=bytes.size()&&size<=bytes.size()-(address-base)) {
            std::memcpy(output,bytes.data()+(address-base),size);
            if (m.unstable&&address==0x10000&&++m.dobj_reads==2)
                static_cast<std::byte*>(output)[0x44]^=std::byte{1};
            if (m.unstable_geometry&&address==0x71000&&++m.geometry_reads==2)
                static_cast<std::byte*>(output)[0]^=std::byte{1};
            return true;
        }
        return false;
    }
};
struct Bone { std::array<float,4> q{0,0,0,1}; Vec3f t{}; float scale{2}; };
void hash(std::uint64_t& value,const void* p,std::size_t n) {
    const auto* b=static_cast<const unsigned char*>(p);
    for (std::size_t i=0;i<n;++i) value=(value^b[i])*1099511628211ULL;
}
struct Fixture final {
    Memory memory;
    SightAuditMetadata metadata{};
    SightAuditRecipe synthetic_recipe{};
    Fixture() {
        std::array<std::byte,0x68> dobj{}; memory.set(0x10000,dobj);
        memory.patch(0x10000,9,std::uint8_t{2}); memory.patch(0x10000,10,std::uint8_t{77});
        memory.patch(0x10000,0x64,std::uint32_t{0x50000});
        memory.patch(0x10000,0x44,std::uint32_t{70}); memory.patch(0x10000,0x48,std::uint32_t{0x200000});
        memory.patch(0x10000,0x34+8,std::uint32_t{(0x80000000U>>12)|(0x80000000U>>11)|(0x80000000U>>6)});
        std::array<std::byte,0x10> definition{}; memory.set(0x20000,definition);
        memory.patch(0x20000,0,std::uint32_t{0x21000}); memory.patch(0x20000,12,std::uint32_t{0x40000});
        memory.string(0x21000,"m1garand");
        std::array<std::byte,8> hands{}; memory.set(0x30000,hands); memory.patch(0x30000,4,std::uint8_t{70});
        std::array<std::byte,0xE4> model{}; memory.set(0x40000,model);
        memory.patch(0x40000,0,std::uint32_t{0x41000}); memory.patch(0x40000,4,std::uint8_t{7});
        memory.patch(0x40000,5,std::uint8_t{1}); memory.patch(0x40000,6,std::uint8_t{6});
        memory.patch(0x40000,8,std::uint32_t{0x60000}); memory.patch(0x40000,12,std::uint32_t{0x60100});
        memory.patch(0x40000,0x1C,std::uint32_t{0x61000}); memory.patch(0x40000,0x20,std::uint32_t{0x70000});
        memory.string(0x41000,"viewmodel_usa_m1garand_rifle");
        memory.set(0x50000,std::array<std::uint32_t,2>{0x30000,0x40000});
        memory.set(0x60000,std::array<std::uint16_t,7>{1,2,3,4,5,6,7});
        memory.set(0x60100,std::array<std::uint8_t,6>{1,2,3,4,5,6});
        std::array<Bone,7> bind{}; bind[6].t={5.621877193450928F,-.409855991601944F,.844623982906342F};
        bind[5].t={27.8524188995361F,0,1.52065205574036F};
        memory.set(0x61000,bind); memory.set(0x03702390,std::uint32_t{0x100000});
        constexpr std::array names{"j_gun","j_bolt","j_clip","j_stripper","tag_brass","tag_flash","tag_iron_sights"};
        for (unsigned i=0;i<7;++i) memory.string(0x100000+(i+1)*12+4,names[i]);
        // String entries overlap natively; use disjoint native IDs in the
        // synthetic pool so our independent block reader can model long text.
        std::array<std::uint16_t,7> ids{};
        for (unsigned i=0;i<7;++i) { ids[i]=static_cast<std::uint16_t>(100+i*10); memory.string(0x100000+ids[i]*12+4,names[i]); }
        memory.set(0x60000,ids);
        std::array<Bone,77> matrices{}; matrices[75].t={12,3,4}; matrices[76].t=bind[6].t;
        memory.set(0x200000,matrices); memory.set(0x03520388,Vec3f{100,200,300});
        std::array<std::byte,0x40*6> surfaces{}; memory.set(0x70000,surfaces);
        synthetic_recipe.geometry_fingerprint=14695981039346656037ULL;
        for (unsigned mesh=0;mesh<2;++mesh) {
            const unsigned index=mesh?3U:0U,count=mesh?249U:231U,first=mesh?0U:145U,owned=mesh?249U:86U;
            const unsigned tc=mesh?166U:148U,tf=mesh?0U:89U,to=mesh?166U:58U;
            const unsigned base=index*0x40,vertices_address=mesh?0x74000U:0x71000U;
            const unsigned indices_address=mesh?0x78000U:0x77000U,rigid_address=mesh?0x7A000U:0x79000U;
            memory.patch(0x70000,base+2,static_cast<std::uint16_t>(count));
            memory.patch(0x70000,base+4,static_cast<std::uint16_t>(tc));
            memory.patch(0x70000,base+0x1C,vertices_address); memory.patch(0x70000,base+0x0C,indices_address);
            memory.patch(0x70000,base+0x24,mesh?1U:3U); memory.patch(0x70000,base+0x28,rigid_address);
            std::array<std::byte,249*32> vertices{};
            for (unsigned i=first;i<first+owned;++i) {
                const Vec3f point{static_cast<float>(i),static_cast<float>(mesh),.5F};
                std::memcpy(vertices.data()+i*32,&point,12);
                hash(synthetic_recipe.geometry_fingerprint,&point,12);
            }
            memory.set(vertices_address,vertices);
            std::array<std::uint16_t,166*3> indices{};
            for (unsigned i=tf*3;i<(tf+to)*3;++i) {
                indices[i]=static_cast<std::uint16_t>(first+(i-tf*3)%owned);
                hash(synthetic_recipe.geometry_fingerprint,vertices.data()+indices[i]*32,12);
            }
            memory.set(indices_address,indices);
            std::array<std::uint16_t,18> rigid{};
            if (!mesh) { rigid[0]=0; rigid[1]=80; rigid[2]=0; rigid[3]=56;
                rigid[6]=3*64; rigid[7]=65; rigid[8]=56; rigid[9]=33; }
            const unsigned start=mesh?0U:12U;
            rigid[start]=6*64; rigid[start+1]=static_cast<std::uint16_t>(owned);
            rigid[start+2]=static_cast<std::uint16_t>(tf); rigid[start+3]=static_cast<std::uint16_t>(to);
            memory.set(rigid_address,rigid);
        }
        metadata.definition=0x20000; metadata.dobj=0x10000; metadata.weapon_index=17;
        metadata.generation=20; metadata.controller_published_ms=1000; metadata.pose_published_ms=1005;
        metadata.pose_serial=100;
        metadata.captured_ms=1006; metadata.frame_id=40; metadata.action_sequence=60;
        metadata.predicted_display_time=100000; metadata.tracked_live=true; metadata.grip_mode=1;
        metadata.tracking_evidence=kRequiredSightTrackingEvidence;
        metadata.right_grip=1; metadata.muzzle={112,203,304}; metadata.head_world_forward={1,0,0};
        std::memcpy(metadata.grip_tag.data(),"tag_weapon",11);
    }
    SightAuditStatus capture(SightAuditSnapshot* output) {
        return capture_rendered_sights({&memory,&Memory::read},synthetic_recipe,metadata,output);
    }
    void notch(SightAuditAsset asset) {
        const auto* p=detail::sight_asset_recipe(asset); require(p&&asset!=SightAuditAsset::garand,"invalid notch fixture");
        metadata.asset=asset; synthetic_recipe.asset=asset;
        memory.string(0x21000,p->weapon); memory.string(0x41000,p->model);
        memory.patch(0x10000,10,static_cast<std::uint8_t>(70+p->bone_count));
        memory.patch(0x40000,4,static_cast<std::uint8_t>(p->bone_count));
        memory.patch(0x40000,6,static_cast<std::uint8_t>(p->surface_count));
        std::array<std::uint16_t,9> ids{}; std::array<std::uint8_t,8> parents{};
        std::array<Bone,9> bind{};
        for (unsigned i=0;i<p->bone_count;++i) {
            ids[i]=static_cast<std::uint16_t>(100+i*10);
            memory.string(0x100000+ids[i]*12+4,p->bone_names[i]);
            if (i) parents[i-1]=static_cast<std::uint8_t>(i);
        }
        bind[p->front_bone]={p->front_bind_orientation,p->front_bind,p->front_bind_weight};
        bind[p->rear_bone]={p->rear_bind_orientation,p->rear_bind,p->rear_bind_weight};
        bind[p->flash_bone]={p->flash_bind_orientation,p->flash_bind,p->flash_bind_weight};
        memory.set(0x60000,ids); memory.set(0x60100,parents); memory.set(0x61000,bind);
        std::array<Bone,79> matrices{};
        matrices[70+p->front_bone]=bind[p->front_bone]; matrices[70+p->rear_bone]=bind[p->rear_bone];
        matrices[70+p->flash_bone].t={12,3,4}; memory.set(0x200000,matrices);
        std::array<std::uint32_t,4> bits{};
        for (const auto local:{p->front_bone,p->rear_bone,p->flash_bone}) {
            const auto global=70+local; bits[global>>5]|=0x80000000U>>(global&31U);
        }
        bits[70>>5]|=0x80000000U>>(70&31U);
        memory.patch(0x10000,0x34,bits);
        std::array<std::byte,7*0x40> surfaces{}; memory.set(0x70000,surfaces);
        synthetic_recipe.geometry_fingerprint=14695981039346656037ULL;
        for (unsigned mesh=0;mesh<p->mesh_count;++mesh) {
            const auto spec=p->meshes[mesh]; const auto base=spec.surface*0x40;
            const unsigned va=0x800000+mesh*0x20000,ia=va+0x10000,ra=va+0x18000;
            memory.patch(0x70000,base+2,static_cast<std::uint16_t>(spec.vertices));
            memory.patch(0x70000,base+4,static_cast<std::uint16_t>(spec.triangles));
            memory.patch(0x70000,base+0x1C,va); memory.patch(0x70000,base+0x0C,ia);
            memory.patch(0x70000,base+0x24,spec.range_count); memory.patch(0x70000,base+0x28,ra);
            std::vector<std::byte> vertices(spec.vertices*32);
            for (unsigned i=0;i<spec.vertices;++i) {
                const Vec3f point{static_cast<float>(i)/8.F,static_cast<float>(mesh),.5F};
                std::memcpy(vertices.data()+i*32,&point,12);
                hash(synthetic_recipe.geometry_fingerprint,&point,12);
            }
            memory.blocks[va]=vertices;
            std::vector<std::uint16_t> indices(spec.triangles*3);
            for (unsigned i=0;i<indices.size();++i) {
                indices[i]=static_cast<std::uint16_t>(i%spec.vertices);
                hash(synthetic_recipe.geometry_fingerprint,vertices.data()+indices[i]*32,12);
            }
            const auto* first=reinterpret_cast<const std::byte*>(indices.data());
            memory.blocks[ia]={first,first+indices.size()*2};
            std::array<std::uint16_t,6> rigid{static_cast<std::uint16_t>(spec.owner_bone*64),
                static_cast<std::uint16_t>(spec.owned_vertices),static_cast<std::uint16_t>(spec.first_triangle),
                static_cast<std::uint16_t>(spec.owned_triangles),0,0};
            memory.set(ra,rigid);
        }
    }
};
void capture_tests() {
    Fixture f; SightAuditSnapshot output{};
    require(f.capture(&output)==SightAuditStatus::accepted,"production reader rejected valid synthetic layout");
    require(output.valid&&output.sight_bone==76&&output.model==0x40000&&output.skeleton_timestamp==70,"native identity lost");
    require(std::abs(output.front[0].x-125.727997F)<.00002F&&std::abs(output.rear[1].z-302.205988F)<.00003F,"mesh bind transform incorrect");
    require(output.front[0].z<output.front[1].z,"front bevel uncertainty collapsed");
    Fixture firing; firing.metadata.weapon_state=5; firing.metadata.weapon_time=135;
    firing.metadata.shot_count=1;
    SightAuditSnapshot busy{};
    require(firing.capture(&busy)==SightAuditStatus::accepted&&busy.valid&&!busy.rested_eligible&&
        busy.rested_reason_bits==0x6000,"predicted busy source lost geometric receipt or gained rest eligibility");
    require(output.rested_eligible&&output.rested_reason_bits==0,"rested geometric receipt incorrectly classified");
    Fixture rotated;
    rotated.memory.patch(0x200000,76*32,std::array<float,4>{0,0,.7071067812F,.7071067812F});
    SightAuditSnapshot turn{};
    require(rotated.capture(&turn)==SightAuditStatus::accepted,"rotated evaluated pose rejected");
    require(std::abs(turn.front[0].x-105.21202F)<.0001F&&std::abs(turn.front[0].y-219.69626F)<.0001F,
        "evaluated quaternion rows or bind undo are reversed");
    require(capture_garand_rendered_sights({&f.memory,&Memory::read},{},f.metadata,&output)==SightAuditStatus::geometry,
        "synthetic recipe must never pass frozen production geometry fingerprint");
    for (unsigned test=0;test<16;++test) {
        Fixture bad; SightAuditSnapshot sentinel{}; sentinel.model=0x12345678;
        SightAuditStatus expected{};
        switch(test) {
        case 0: bad.metadata.generation=0; expected=SightAuditStatus::invalid_metadata; break;
        case 1: bad.metadata.transition_pending=true; expected=SightAuditStatus::invalid_metadata; break;
        case 2: bad.metadata.right_trigger=1.1F; expected=SightAuditStatus::invalid_metadata; break;
        case 3: bad.memory.deny=true; expected=SightAuditStatus::inaccessible; break;
        case 4: bad.memory.string(0x21000,"m1garand_gl"); expected=SightAuditStatus::identity; break;
        case 5: bad.memory.patch(0x20000,12,std::uint32_t{0x30000}); expected=SightAuditStatus::identity; break;
        case 6: bad.memory.patch(0x10000,0x50+8,std::uint32_t{0x80000000U>>12}); expected=SightAuditStatus::hidden; break;
        case 7: bad.memory.patch(0x10000,0x34+8,std::uint32_t{}); expected=SightAuditStatus::unevaluated; break;
        case 8: bad.memory.unstable=true; expected=SightAuditStatus::unstable; break;
        case 9: bad.synthetic_recipe.geometry_fingerprint^=1; expected=SightAuditStatus::geometry; break;
        case 10: bad.metadata.muzzle.x+=1; expected=SightAuditStatus::muzzle_mismatch; break;
        case 11: bad.memory.patch(0x79000,2,std::uint16_t{79}); expected=SightAuditStatus::geometry; break;
        case 12: bad.memory.patch(0x78000,0,std::uint16_t{300}); expected=SightAuditStatus::geometry; break;
        case 13: bad.metadata.head.orientation.w=0; expected=SightAuditStatus::invalid_metadata; break;
        case 14: bad.memory.unstable_geometry=true; expected=SightAuditStatus::unstable; break;
        default: bad.metadata.pose_serial=0; expected=SightAuditStatus::invalid_metadata; break;
        }
        require(bad.capture(&sentinel)==expected,"reader rejection fixture failed");
        require(sentinel.model==0x12345678&&!sentinel.valid,"failed capture mutated output");
    }
}
void binding_tests() {
    Fixture f; SightAuditSnapshot s{}; require(f.capture(&s)==SightAuditStatus::accepted,"binding fixture capture failed");
    SightAuditFireRequest request{.weapon_parms=0x300000,.definition=0x20000,.weapon_index=17,
        .generation=22,.source_generation=20,.source_published_ms=1005,.now_ms=1020,
        .source_pose_serial=100,
        .origin=f.metadata.muzzle,.basis={}};
    require(match_sight_audit_source(s,request)==SightAuditStatus::accepted,"exact older source association failed");
    for (unsigned i=0;i<9;++i) {
        auto bad=request;
        switch(i) {
        case 0: bad.definition++; break;
        case 1: bad.weapon_index++; break;
        case 2: bad.source_generation++; break;
        case 3: bad.source_published_ms++; break;
        case 4: bad.now_ms=1005; break;
        case 5: bad.generation=25; break;
        case 6: bad.origin.y+=.1F; break;
        case 7: bad.basis.forward.x=std::numeric_limits<float>::quiet_NaN(); break;
        default: bad.source_pose_serial++; break;
        }
        require(match_sight_audit_source(s,bad)!=SightAuditStatus::accepted,"mismatched source was accepted");
    }
    auto fired=request; fired.now_ms++;
    require(match_sight_audit_discharge(request,fired)==SightAuditStatus::accepted,"synchronous fire token rejected");
    for (unsigned i=0;i<5;++i) {
        auto bad=fired;
        switch(i) { case 0: bad.weapon_parms++; break; case 1: bad.generation++; break;
        case 2: bad.definition++; break; case 3: bad.now_ms+=25; break; default: bad.basis.up.y=.1F; break; }
        require(match_sight_audit_discharge(request,bad)!=SightAuditStatus::accepted,"mismatched fire token accepted");
    }
}
void notch_tests() {
    for (const auto asset:{SightAuditAsset::mosin,SightAuditAsset::svt,SightAuditAsset::colt}) {
        Fixture f; f.notch(asset); SightAuditSnapshot output{};
        require(f.capture(&output)==SightAuditStatus::accepted,"production reader rejected notch fixture");
        const auto* p=detail::sight_asset_recipe(asset);
        require(output.rear_count==p->rear_count&&output.floor_count==p->floor_count&&
            output.front_bone==70+p->front_bone&&output.sight_bone==70+p->rear_bone,"notch ownership/counts lost");
        require(std::abs(output.front[0].x-(100+p->front[0].x))<.00003F&&
            std::abs(output.rear[0].z-(300+p->rear[0].z))<.00003F,"notch bind transform wrong");
        require(output.floor[0].z<output.rear[0].z,"notch floor silently replaced top shoulders");
        require(capture_garand_rendered_sights({&f.memory,&Memory::read},f.synthetic_recipe,f.metadata,&output)==SightAuditStatus::identity,
            "Garand-only compatibility wrapper accepted another asset");
        require(capture_rendered_sights({&f.memory,&Memory::read},{p->fingerprint,asset},f.metadata,&output)==SightAuditStatus::geometry,
            "synthetic notch geometry passed frozen native fingerprint");
        if (asset==SightAuditAsset::svt) {
            f.memory.patch(0x200000,75*32,std::array<float,4>{0,0,.7071067812F,.7071067812F});
            require(f.capture(&output)==SightAuditStatus::accepted,"SVT separate front bone rotation rejected");
            require(std::abs(output.front[0].x-122.15429F)<.0001F&&
                std::abs(output.front[0].y-199.99963F)<.0001F&&
                std::abs(output.rear[0].x-99.29458F)<.0001F,"SVT incorrectly reused one evaluated bone");
            require(std::abs(output.front_bone_basis.left.x+1.F)<.00001F&&
                std::abs(output.sight_bone_basis.left.y-1.F)<.00001F,"SVT front/rear orientation collapsed");
        }
        for (const auto bone:{p->front_bone,p->rear_bone}) {
            Fixture hidden; hidden.notch(asset);
            const unsigned global=70+bone;
            hidden.memory.patch(0x10000,0x50+(global>>5)*4,0x80000000U>>(global&31U));
            require(hidden.capture(&output)==SightAuditStatus::hidden,"hidden notch owner accepted");
        }
        Fixture missing; missing.notch(asset);
        missing.memory.patch(0x10000,0x34+8,std::uint32_t{});
        require(missing.capture(&output)==SightAuditStatus::unevaluated,"uncomputed notch transform accepted");
        if (asset==SightAuditAsset::colt) {
            Fixture changed; changed.notch(asset);
            changed.memory.patch(0x61000,32+4,0.F);
            require(changed.capture(&output)==SightAuditStatus::geometry,"Colt changed bind orientation accepted");
            Fixture seed_fixture; seed_fixture.notch(asset); SightAuditSourceWitness seed{};
            const SightAuditReader reader{&seed_fixture.memory,&Memory::read};
            require(capture_sight_audit_source_witness(reader,seed_fixture.metadata,&seed)==SightAuditStatus::accepted&&
                seed.sight_bone==71,"Colt slide source witness missing");
            auto deferred=seed_fixture.metadata; deferred.deferred_capture=true;
            deferred.observed_frame=deferred.frame_id; deferred.captured_ms+=2;
            require(capture_deferred_rendered_sights(reader,seed_fixture.synthetic_recipe,deferred,seed,
                deferred.frame_id,&output)==SightAuditStatus::accepted,"exact Colt slide seed rejected");
            seed_fixture.memory.patch(0x200000,71*32+16,p->rear_bind.x+.01F);
            require(capture_deferred_rendered_sights(reader,seed_fixture.synthetic_recipe,deferred,seed,
                deferred.frame_id,&output)==SightAuditStatus::seed_mismatch,"Colt deferred read bridged changed slide");
        }
    }
}
void metadata_reason_tests() {
    const Fixture fixture;
    require(sight_audit_metadata_rejection_bits(fixture.metadata)==0,"valid metadata gained a rejection reason");
    for (unsigned bit=0;bit<17;++bit) {
        auto m=fixture.metadata;
        switch (bit) {
        case 0:m.definition=0;break;
        case 1:m.generation=0;break;
        case 2:m.frame_id=0;break;
        case 3:m.tracking_evidence&=~2U;break;
        case 4:m.transition_pending=true;break;
        case 5:m.grip_mode=0;break;
        case 6:m.captured_ms=m.pose_published_ms-1;break;
        case 7:m.projectile_basis.forward.x=2;break;
        case 8:m.head_world_forward.x=2;break;
        case 9:m.grip_world.x=std::numeric_limits<float>::quiet_NaN();break;
        case 10:m.head.orientation.w=0;break;
        case 11:m.right_grip=1.1F;break;
        case 12:m.grip_tag[0]=0;break;
        case 13:m.weapon_state=2;break;
        case 14:m.weapon_time=1;break;
        case 15:m.weapon_delay=1;break;
        default:m.right_trigger=std::numeric_limits<float>::quiet_NaN();break;
        }
        require(sight_audit_metadata_rejection_bits(m)==(1U<<bit),"exact metadata reason bit fixture failed");
    }
}
void pretrigger_tests() {
    Fixture f; f.metadata.trigger_active=true;
    SightAuditSnapshot pre{}; require(f.capture(&pre)==SightAuditStatus::accepted,"pre-trigger fixture failed");
    f.metadata.generation=21; f.metadata.controller_published_ms=1015;
    f.metadata.pose_published_ms=1016; f.metadata.captured_ms=1017; f.metadata.pose_serial=101;
    f.metadata.right_trigger=1; f.metadata.trigger_changed=true;
    f.metadata.weapon_state=5; f.metadata.weapon_time=135; f.metadata.shot_count=1;
    SightAuditSnapshot source{}; require(f.capture(&source)==SightAuditStatus::accepted,"busy source fixture failed");
    require(sight_audit_observation_continuous(pre,source),"normal rested-to-busy continuity lost");
    for (unsigned i=0;i<9;++i) {
        auto broken=source;
        switch (i) {
        case 0:broken.metadata.generation=pre.metadata.generation+2;break;
        case 1:broken.metadata.pose_serial=pre.metadata.pose_serial;break;
        case 2:broken.metadata.weapon_index++;break;
        case 3:broken.model++;break;
        case 4:broken.metadata.dobj++;break;
        case 5:broken.metadata.grip_mode=3;break;
        case 6:broken.metadata.grip_tag[0]='x';break;
        case 7:broken.metadata.pose_published_ms=pre.metadata.pose_published_ms-1;break;
        default:broken.valid=false;break;
        }
        require(!sight_audit_observation_continuous(pre,broken),"history bridged a publication discontinuity");
    }
    SightAuditFireRequest fire{.weapon_parms=0x300000,.definition=0x20000,.weapon_index=17,
        .generation=22,.source_generation=21,.source_published_ms=1016,.now_ms=1030,
        .source_pose_serial=101,.origin=f.metadata.muzzle,.basis={}};
    require(sight_audit_pretrigger_rejection_bits(pre,source,fire)==0,"released/rested independent predecessor rejected");
    auto moving_sights=source; moving_sights.front[0].z+=1;
    moving_sights.rear[1].z+=.25F; moving_sights.metadata.muzzle.z+=2;
    require(sight_audit_pretrigger_rejection_bits(pre,moving_sights,fire)==0,
        "pre-trigger witness improperly requires sight/bore equality and hides recoil");
    require(match_sight_audit_source(pre,fire)==SightAuditStatus::source_mismatch,
        "independent predecessor was accepted as exact firing source");
    for (unsigned i=0;i<22;++i) {
        auto bad=pre; auto current=source; auto request=fire;
        switch(i) {
        case 0:bad.valid=false;break;
        case 1:bad.rested_eligible=false;break;
        case 2:bad.metadata.weapon_time=1;break;
        case 3:bad.metadata.right_trigger=.5F;break;
        case 4:bad.metadata.trigger_active=false;break;
        case 5:bad.metadata.definition++;break;
        case 6:bad.model++;break;
        case 7:bad.metadata.dobj++;break;
        case 8:bad.metadata.grip_mode=3;break;
        case 9:bad.metadata.grip_tag[0]='x';break;
        case 10:bad.metadata.pose_serial=current.metadata.pose_serial;break;
        case 11:bad.metadata.generation=current.metadata.generation;break;
        case 12:bad.metadata.pose_published_ms=current.metadata.pose_published_ms+1;break;
        case 13:request.now_ms=1256;break;
        case 14:request.generation=29;break;
        case 15:bad.metadata.head.position.x+=.001F;break;
        case 16:bad.metadata.right_hand.orientation={0,0,.1F,.994987437F};break;
        case 17:bad.metadata.left_hand.position.z+=.001F;break;
        case 18:bad.metadata.parent_camera_origin.x+=.01F;break;
        case 19:bad.metadata.parent_camera_basis={{0,1,0},{-1,0,0},{0,0,1}};break;
        case 20:current.metadata.right_trigger=0;break;
        default:bad.metadata.tracking_evidence&=~2U;break;
        }
        require(sight_audit_pretrigger_rejection_bits(bad,current,request)!=0,
            "unsafe independent predecessor passed qualification");
    }
    auto click_pre=pre; click_pre.metadata.trigger_active=false;
    click_pre.metadata.trigger_click_active=true;
    auto click_source=source; click_source.metadata.trigger_active=false;
    click_source.metadata.trigger_click_active=true; click_source.metadata.trigger_click=true;
    require(sight_audit_pretrigger_rejection_bits(click_pre,click_source,fire)==0,
        "active click-only release/press evidence rejected");
    click_pre.metadata.trigger_click=true;
    require(sight_audit_pretrigger_rejection_bits(click_pre,click_source,fire)!=0,
        "held click mislabeled pre-trigger released");
}
void deferred_capture_tests() {
    Fixture f; SightAuditSourceWitness seed{};
    f.memory.patch(0x10000,0x34+8,std::uint32_t{(0x80000000U>>11)|(0x80000000U>>6)});
    SightAuditSnapshot output{};
    require(f.capture(&output)==SightAuditStatus::unevaluated,"missing sight bit unexpectedly accepted");
    const SightAuditReader reader{&f.memory,&Memory::read};
    require(capture_sight_audit_source_witness(reader,f.metadata,&seed)==SightAuditStatus::accepted&&seed.valid,
        "read-only partial-evaluation source witness rejected");
    auto observed=f.metadata; observed.deferred_capture=true; observed.observed_frame=observed.frame_id;
    observed.staged_ms=f.metadata.captured_ms; observed.captured_ms+=2;
    require(capture_deferred_rendered_sights(reader,f.synthetic_recipe,observed,seed,observed.frame_id,&output)==
        SightAuditStatus::unevaluated,"deferred observer manufactured missing skeleton evaluation");
    f.memory.patch(0x10000,0x34+8,std::uint32_t{(0x80000000U>>12)|(0x80000000U>>11)|(0x80000000U>>6)});
    require(capture_deferred_rendered_sights(reader,f.synthetic_recipe,observed,seed,observed.frame_id,&output)==
        SightAuditStatus::accepted&&output.metadata.pose_serial==f.metadata.pose_serial&&
        output.metadata.generation==f.metadata.generation&&output.skeleton_timestamp==seed.skeleton_timestamp,
        "naturally completed same-seed geometry lost exact source identity");
    for (unsigned i=0;i<15;++i) {
        Fixture bad; SightAuditSourceWitness before{};
        require(capture_sight_audit_source_witness({&bad.memory,&Memory::read},bad.metadata,&before)==
            SightAuditStatus::accepted,"deferred rejection seed fixture failed");
        auto metadata=bad.metadata; metadata.deferred_capture=true; metadata.observed_frame=metadata.frame_id;
        metadata.captured_ms+=2;
        auto frame=metadata.frame_id;
        switch (i) {
        case 0:frame++;break;
        case 1:metadata.observed_frame++;break;
        case 2:metadata.deferred_capture=false;break;
        case 3:bad.memory.patch(0x10000,0x44,std::uint32_t{71});break;
        case 4:bad.memory.patch(0x200000,70*32+16,1.F);break;
        case 5:bad.memory.patch(0x200000,75*32+16,13.F);break;
        case 6:bad.memory.set(0x03520388,Vec3f{101,200,300});break;
        case 7:bad.memory.patch(0x10000,0x50,std::uint32_t{1});break;
        case 8:before.matrices++;break;
        case 9:before.model++;break;
        case 10:metadata.captured_ms=metadata.pose_published_ms+26;break;
        case 11:bad.memory.patch(0x10000,0x34+8,std::uint32_t{0x80000000U>>11});break;
        case 12:metadata.generation++;break;
        case 13:metadata.pose_serial++;break;
        default:metadata.pose_published_ms++;break;
        }
        SightAuditSnapshot sentinel{}; sentinel.model=0xDEADBEEF;
        require(capture_deferred_rendered_sights({&bad.memory,&Memory::read},bad.synthetic_recipe,
            metadata,before,frame,&sentinel)!=SightAuditStatus::accepted,"deferred source drift was accepted");
        require(!sentinel.valid&&sentinel.model==0xDEADBEEF,"failed deferred read mutated caller output");
    }
}
} // namespace
int main() {
    try { capture_tests(); binding_tests(); notch_tests(); metadata_reason_tests(); pretrigger_tests(); deferred_capture_tests(); std::cout<<"Sight audit production reader/association tests passed\n"; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
