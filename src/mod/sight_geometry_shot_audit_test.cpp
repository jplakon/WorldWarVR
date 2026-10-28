// SPDX-License-Identifier: GPL-3.0-only
#include "sight_geometry_shot_audit.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace wawvr::mod;using wawvr::xr::Basis3f;
unsigned checks{};
void check(bool v,const char* text){++checks;if(!v)throw std::runtime_error(text);}
Basis3f pitch(float degrees){const float a=degrees*.0174532925199433F,c=std::cos(a),s=std::sin(a);
    return {{c,0,s},{0,1,0},{-s,0,c}};}
struct Fixture {
    SightGeometryRecipe recipe{};SightGeometryProfile profile{};SightGeometryShotAuditSource source{};
    SightGeometryShotAuditFire fire{};
    Fixture(){recipe.model="viewmodel_synthetic";recipe.fingerprint=12345;profile={"synthetic",recipe.model,&recipe};
        auto& g=source.geometry;g.profile=&profile;g.recipe=&recipe;g.definition=0x100;g.dobj=0x200;g.model=0x300;
        g.skeleton_timestamp=77;g.fingerprint=recipe.fingerprint;g.controller_generation=10;g.publication_milliseconds=1000;
        g.world_rear={10,20,30};g.world_front={20,20,30};g.evaluated_root_basis=pitch(0);
        source.valid=true;source.definition=g.definition;source.dobj=g.dobj;source.weapon_index=2;
        source.controller_generation=10;source.controller_published_ms=1000;source.publication_ms=1010;
        source.publication_serial=1;source.captured_ms=1010;source.selected_basis=pitch(0);source.muzzle={21,20,29};
        source.grip_mode=WeaponGripMode::RightHand;
        fire={&profile,0x100,0x999,0x200,0x300,2,recipe.fingerprint,11,10,1010,1,1015,source.muzzle,pitch(0)};
    }
};
void positive(){Fixture f;SightGeometryShotAuditHistory h{};SightGeometryShotAuditMatch match{};
    check(sight_geometry_shot_audit_source_valid(f.source),"valid source rejected");
    check(publish_sight_geometry_shot_audit_source(f.source,&h),"valid publication rejected");
    check(match_sight_geometry_shot_audit_source(h,f.fire,&match)==SightGeometryShotAuditStatus::accepted,"same source did not match");
    check(match.native_definition==0x999&&match.source.definition==0x100,"native alias confused with canonical identity");
    check(!match.sight_mismatch&&match.generation_lag==1&&match.source_age_ms==5&&match.controller_age_ms==15,
        "wrong sight/lag/source-age receipt");
    check(match.actual_sight_degrees<.00001F&&match.selected_sight_degrees<.00001F,"zero ray angle wrong");
    // The correlation may honestly reveal an off-sight selected/native basis.
    // It must report the mismatch instead of manufacturing a zero or failing
    // to record that this exact published source reached native parameters.
    Fixture off;off.source.selected_basis=pitch(2);off.fire.actual_basis=pitch(2);SightGeometryShotAuditHistory ho{};
    check(publish_sight_geometry_shot_audit_source(off.source,&ho),"off-sight comparison source refused");
    check(match_sight_geometry_shot_audit_source(ho,off.fire,&match)==SightGeometryShotAuditStatus::accepted&&match.sight_mismatch,
        "off-sight matching native ray mislabeled as sighted");
    check(std::abs(match.actual_sight_degrees-2.F)<.0001F&&match.selected_actual_degrees<.00001F,"small-angle receipt wrong");
    f.fire.now_ms=1150;check(match_sight_geometry_shot_audit_source(h,f.fire,&match)==SightGeometryShotAuditStatus::accepted,
        "inclusive original150ms boundary refused");
    for(std::uint64_t i=2;i<=80;++i){f.source.publication_serial=i;
        check(publish_sight_geometry_shot_audit_source(f.source,&h),"finite ring publication failed");}
    f.fire.source_publication_serial=80;f.fire.now_ms=1015;
    check(match_sight_geometry_shot_audit_source(h,f.fire,&match)==SightGeometryShotAuditStatus::accepted,"wrapped ring latest source absent");
    f.fire.source_publication_serial=1;check(match_sight_geometry_shot_audit_source(h,f.fire,&match)==SightGeometryShotAuditStatus::missing,
        "overwritten historical source incorrectly matched");
}
void negative(){for(unsigned n=0;n<25;++n){Fixture f;SightGeometryShotAuditHistory h{};
    check(publish_sight_geometry_shot_audit_source(f.source,&h),"negative fixture publication failed");
    switch(n){case 0:f.fire.profile=nullptr;break;case 1:f.fire.definition=0;break;case 2:f.fire.native_definition=0;break;
    case 3:f.fire.dobj=0;break;case 4:f.fire.model=0;break;case 5:f.fire.weapon_index=0;break;
    case 6:f.fire.generation=0;break;case 7:f.fire.source_generation=0;break;case 8:f.fire.source_published_ms=0;break;
    case 9:f.fire.source_publication_serial=0;break;case 10:f.fire.now_ms=0;break;
    case 11:f.fire.origin.x=std::numeric_limits<float>::quiet_NaN();break;case 12:f.fire.actual_basis.up.z=-1;break;
    case 13:f.fire.definition++;break;case 14:f.fire.dobj++;break;case 15:f.fire.model++;break;
    case 16:f.fire.weapon_index++;break;case 17:f.fire.fingerprint++;break;case 18:f.fire.source_generation=9;break;
    case 19:f.fire.source_published_ms++;break;case 20:f.fire.generation=15;break;
    case 21:f.fire.now_ms=1151;break;case 22:f.fire.now_ms=1009;break;
    case 23:f.fire.origin.x+=.01F;break;case 24:f.fire.actual_basis=pitch(.02F);break;}
    SightGeometryShotAuditMatch out{};out.native_definition=12345;out.actual_sight_degrees=66;const auto original=out;
    check(match_sight_geometry_shot_audit_source(h,f.fire,&out)!=SightGeometryShotAuditStatus::accepted,"bad native fire matched");
    check(std::memcmp(&out,&original,sizeof(out))==0,"nonaccepted match altered caller output");
}
    // Re-publishing an almost stale original controller source does not buy
    // a second150ms freshness window at the actual native fire.
    Fixture old;old.source.publication_ms=1149;old.source.captured_ms=1149;old.fire.source_published_ms=1149;old.fire.now_ms=1151;
    SightGeometryShotAuditHistory h{};SightGeometryShotAuditMatch out{};
    check(publish_sight_geometry_shot_audit_source(old.source,&h),"bounded old-source fixture refused");
    check(match_sight_geometry_shot_audit_source(h,old.fire,&out)==SightGeometryShotAuditStatus::stale,"republish extended original source age");
}
void publication_negative(){for(unsigned n=0;n<17;++n){Fixture f;
    switch(n){case 0:f.source.valid=false;break;case 1:f.source.geometry.profile=nullptr;break;
    case 2:f.source.geometry.recipe=nullptr;break;case 3:f.source.geometry.definition++;break;
    case 4:f.source.geometry.dobj++;break;case 5:f.source.geometry.fingerprint++;break;
    case 6:f.source.geometry.model=0;break;case 7:f.source.geometry.skeleton_timestamp=0;break;
    case 8:f.source.controller_generation++;break;case 9:f.source.controller_published_ms++;break;
    case 10:f.source.publication_serial=0;break;case 11:f.source.captured_ms=1151;break;
    case 12:f.source.publication_ms=999;break;case 13:f.source.selected_basis.up.z=-1;break;
    case 14:f.source.geometry.world_front=f.source.geometry.world_rear;break;
    case 15:f.source.grip_mode=WeaponGripMode::Chest;break;case 16:f.source.muzzle.x=std::numeric_limits<float>::infinity();break;}
    SightGeometryShotAuditHistory h{};const auto original=h;
    check(!publish_sight_geometry_shot_audit_source(f.source,&h),"invalid publication accepted");
    check(std::memcmp(&h,&original,sizeof(h))==0,"invalid publication altered history");}
    Fixture f;SightGeometryShotAuditHistory h{};check(publish_sight_geometry_shot_audit_source(f.source,&h),"replay fixture failed");
    const auto original=h;check(!publish_sight_geometry_shot_audit_source(f.source,&h)&&std::memcmp(&h,&original,sizeof(h))==0,
        "replayed serial modified ring");
}
void basis_kinds(){Fixture f;SightGeometryShotAuditHistory history{};SightGeometryShotAuditMatch output{};
    f.recipe.basis_kind=SightGeometryBasisKind::barrel_axis;
    check(!sight_geometry_shot_audit_source_valid(f.source),"iron proof published as a barrel witness");
    f.source.geometry.basis_kind=SightGeometryBasisKind::barrel_axis;
    check(publish_sight_geometry_shot_audit_source(f.source,&history),"matching barrel source rejected");
    check(match_sight_geometry_shot_audit_source(history,f.fire,&output)==SightGeometryShotAuditStatus::accepted&&
        output.source.geometry.basis_kind==SightGeometryBasisKind::barrel_axis,"barrel category lost during native match");
    f.recipe.basis_kind=static_cast<SightGeometryBasisKind>(255);f.source.geometry.basis_kind=f.recipe.basis_kind;
    check(!sight_geometry_shot_audit_source_valid(f.source),"unknown matching category passed source validation");
    const auto before=output;
    check(match_sight_geometry_shot_audit_source(history,f.fire,&output)==SightGeometryShotAuditStatus::invalid_fire&&
        std::memcmp(&output,&before,sizeof(output))==0,"unknown category matched or altered output");
}
}
int main(){try{positive();negative();publication_negative();basis_kinds();std::cout<<"generic sight shot audit: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
