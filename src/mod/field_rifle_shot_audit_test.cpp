// SPDX-License-Identifier: GPL-3.0-only
#include "field_rifle_shot_audit.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace wawvr::mod;
using wawvr::xr::Basis3f;
unsigned checks{};
void check(bool passed,std::string_view message) {
    ++checks; if (!passed) throw std::runtime_error(std::string(message));
}
Basis3f pitch(float degrees) {
    const float radians=degrees*3.14159265358979323846F/180.F;
    const float c=std::cos(radians),s=std::sin(radians);
    return {{c,0,s},{0,1,0},{-s,0,c}};
}
FieldRifleShotAuditSource source(std::uint64_t serial=10) {
    FieldRifleShotAuditSource s{};
    s.valid=true; s.geometry.asset=FieldRifleAsset::carbine;
    s.geometry.model=0x123400; s.geometry.skeleton_timestamp=90;
    s.geometry.fingerprint=0x1122334455667788ULL;
    s.geometry.world_rear={-6.5F,0,1.7865F};
    s.geometry.world_front={19.783F,0,2.1943F};
    s.definition=0x123000; s.dobj=0x124000; s.weapon_index=16;
    s.controller_generation=20; s.controller_published_ms=1000;
    s.publication_ms=1002; s.publication_serial=serial; s.captured_ms=1003;
    s.frame_id=30; s.action_sequence=40;
    const auto delta=wawvr::xr::Vec3f{
        s.geometry.world_front.x-s.geometry.world_rear.x,0,
        s.geometry.world_front.z-s.geometry.world_rear.z};
    s.selected_basis=pitch(std::atan2(delta.z,delta.x)*180.F/3.14159265358979323846F);
    s.muzzle={22,0,.8F}; s.grip_mode=WeaponGripMode::RightHand;
    s.tracked_live=true; s.raw_poses_valid=true;
    s.weapon_state=0; s.weapon_time=0; s.weapon_delay=0;
    s.trigger_active=true; s.trigger=0;
    return s;
}
FieldRifleShotAuditFire fire(const FieldRifleShotAuditSource& s) {
    FieldRifleShotAuditFire f{};
    f.asset=s.geometry.asset; f.definition=s.definition; f.native_definition=0x999000;
    f.weapon_index=s.weapon_index; f.generation=s.controller_generation+1;
    f.source_generation=s.controller_generation; f.source_published_ms=s.publication_ms;
    f.source_publication_serial=s.publication_serial; f.now_ms=1010;
    f.origin=s.muzzle; f.actual_basis=s.selected_basis;
    return f;
}
void correlation() {
    auto s=source(); const auto f=fire(s);
    FieldRifleShotAuditHistory h{}; FieldRifleShotAuditMatch result{};
    check(publish_field_rifle_shot_audit_source(s,&h),"valid source publication rejected");
    check(match_field_rifle_shot_audit_source(h,f,&result)==FieldRifleShotAuditStatus::accepted,
        "exact muzzle source and real native basis did not match");
    check(result.generation_lag==1 && result.source_age_ms==8 &&
        result.selected_sight_degrees<.0001F && result.actual_sight_degrees<.0001F &&
        !result.selected_sight_mismatch,"source lag or same-shot mesh angle was relabelled");
    auto lag=f; lag.generation=s.controller_generation+4; lag.now_ms=1152;
    check(match_field_rifle_shot_audit_source(h,lag,&result)==FieldRifleShotAuditStatus::accepted,
        "existing150ms/4-generation boundary changed");
    lag.now_ms=1153;
    check(match_field_rifle_shot_audit_source(h,lag,&result)==FieldRifleShotAuditStatus::stale,
        "diagnostic widened production age boundary");
    lag=f; lag.generation=s.controller_generation+5;
    check(match_field_rifle_shot_audit_source(h,lag,&result)==FieldRifleShotAuditStatus::stale,
        "diagnostic widened generation lag");
    // A wrong selected ray is measurable, not magically accepted as zeroed.
    s.selected_basis=pitch(2.55F); h={};
    check(publish_field_rifle_shot_audit_source(s,&h),"observation of wrong basis was lost");
    auto wrong=fire(s);
    check(match_field_rifle_shot_audit_source(h,wrong,&result)==FieldRifleShotAuditStatus::accepted &&
        result.selected_sight_mismatch && result.actual_sight_degrees>1.6F,
        "grip-segment high bias was hidden by source correlation");
    wrong.actual_basis=pitch(2.75F);
    check(match_field_rifle_shot_audit_source(h,wrong,&result)==FieldRifleShotAuditStatus::basis_mismatch &&
        result.selected_actual_degrees>.19F,"different native fire basis falsely matched");
    wrong=fire(s); wrong.origin.z+=.01F;
    check(match_field_rifle_shot_audit_source(h,wrong,&result)==FieldRifleShotAuditStatus::muzzle_mismatch,
        "a different native muzzle matched");
    // Existing accepted rifle audits are outside this new exact-asset observer.
    auto foreign=source(); foreign.geometry.asset=FieldRifleAsset::none;
    check(!field_rifle_shot_audit_source_valid(foreign),"unrelated asset admitted");
}
void rejection() {
    for (unsigned kind=0;kind<14;++kind) {
        auto s=source(); FieldRifleShotAuditHistory h{};
        check(publish_field_rifle_shot_audit_source(s,&h),"fixture source rejected");
        auto f=fire(s);
        switch(kind) {
        case 0:++f.definition;break;
        case 1:++f.weapon_index;break;
        case 2:f.asset=FieldRifleAsset::kar98;break;
        case 3:++f.source_generation;break;
        case 4:++f.source_published_ms;break;
        case 5:++f.source_publication_serial;break;
        case 6:f.generation=f.source_generation-1;break;
        case 7:f.now_ms=s.captured_ms-1;break;
        case 8:f.source_publication_serial=0;break;
        case 9:f.definition=0;break;
        case 10:f.actual_basis.up=f.actual_basis.forward;break;
        case 11:f.origin.x=std::numeric_limits<float>::quiet_NaN();break;
        case 12:f.now_ms=0;break;
        case 13:f.source_generation=0;break;
        }
        FieldRifleShotAuditMatch out{}; out.source.definition=0xDEADBEEF;
        const auto before=out;
        check(match_field_rifle_shot_audit_source(h,f,&out)!=FieldRifleShotAuditStatus::accepted,
            "foreign/replayed/stale/invalid fire accepted");
        check(!std::memcmp(&out,&before,sizeof(out)),"identity/time rejection changed output");
    }
    for(unsigned kind=0;kind<11;++kind) {
        auto s=source(); FieldRifleShotAuditHistory h{}; const auto before=h;
        switch(kind) {
        case 0:s.geometry.fingerprint=0;break;
        case 1:s.geometry.model=0;break;
        case 2:s.geometry.skeleton_timestamp=0;break;
        case 3:s.geometry.world_front=s.geometry.world_rear;break;
        case 4:s.geometry.world_front.z=std::numeric_limits<float>::infinity();break;
        case 5:s.publication_ms=s.controller_published_ms-1;break;
        case 6:s.captured_ms=s.publication_ms-1;break;
        case 7:s.captured_ms=1151;break;
        case 8:s.selected_basis.up.z=-1;break;
        case 9:s.right_hand.orientation={0,0,0,0};break;
        case 10:s.grip_mode=WeaponGripMode::Chest;break;
        }
        check(!publish_field_rifle_shot_audit_source(s,&h)&&
            !std::memcmp(&h,&before,sizeof(h)),"invalid source changed bounded history");
    }
    FieldRifleShotAuditHistory h{}; auto s=source();
    check(!publish_field_rifle_shot_audit_source(s,nullptr),"null history accepted");
    check(publish_field_rifle_shot_audit_source(s,&h),"valid history fixture failed");
    const auto before=h;
    check(!publish_field_rifle_shot_audit_source(s,&h)&&!std::memcmp(&h,&before,sizeof(h)),
        "duplicate serial replaced a publication");
    for(std::uint64_t i=11;i<=43;++i) {
        s.publication_serial=i;
        check(publish_field_rifle_shot_audit_source(s,&h),"bounded ring refused newer publication");
    }
    FieldRifleShotAuditMatch out{};
    check(match_field_rifle_shot_audit_source(h,fire(source()),&out)==FieldRifleShotAuditStatus::missing,
        "evicted source survived or was substituted by newer same-generation geometry");
    check(match_field_rifle_shot_audit_source(h,fire(s),&out)==FieldRifleShotAuditStatus::accepted,
        "ring wrap lost exact latest publication");
    check(match_field_rifle_shot_audit_source(h,fire(s),nullptr)==FieldRifleShotAuditStatus::invalid_fire,
        "null result admitted");
}
void temporal() {
    auto previous=source(1); previous.controller_generation=19;
    previous.controller_published_ms=990; previous.publication_ms=991; previous.captured_ms=992;
    previous.frame_id=29; previous.action_sequence=39;
    auto current=source(2); current.trigger=.9F; current.weapon_state=5; current.weapon_time=75;
    FieldRifleShotAuditHistory h{};
    check(publish_field_rifle_shot_audit_source(previous,&h)&&
        publish_field_rifle_shot_audit_source(current,&h),"temporal fixture rejected");
    FieldRifleShotAuditMatch match{}; auto f=fire(current);
    check(match_field_rifle_shot_audit_source(h,f,&match)==FieldRifleShotAuditStatus::accepted,
        "temporal exact source failed");
    auto comparison=compare_field_rifle_pretrigger(h,match,f);
    check(comparison.available&&comparison.qualified&&comparison.preceding.publication_serial==1&&
        comparison.preceding.controller_generation==19&&match.source.controller_generation==20,
        "preceding rested witness was absent or falsely relabelled same-shot");
    for (unsigned kind=0;kind<6;++kind) {
        auto changed=current;
        switch(kind) {
        case 0:changed.right_hand.position.x+=.01F;break;
        case 1:changed.parent_camera_origin.x+=.01F;break;
        case 2:changed.parent_camera_basis=pitch(.1F);break;
        case 3:changed.trigger=0;break;
        case 4:changed.grip_mode=WeaponGripMode::TwoHand;break;
        case 5:changed.raw_poses_valid=false;break;
        }
        match.source=changed;
        comparison=compare_field_rifle_pretrigger(h,match,f);
        check(comparison.available&&!comparison.qualified&&comparison.rejection_bits,
            "moved/camera-changed/unpressed evidence falsely accepted as settled");
    }
    // Equivalent quaternion signs must not invent movement.
    match.source=current; match.source.right_hand.orientation.w=-1.F;
    check(compare_field_rifle_pretrigger(h,match,f).qualified,"quaternion sign flip became physical movement");
    // Native firing pitch is measured from real endpoint geometry, not root F.
    match.source=current;
    match.source.geometry.world_front.z+=1.F;
    comparison=compare_field_rifle_pretrigger(h,match,f);
    check(comparison.available&&comparison.qualified&&comparison.sight_change_degrees>2.F,
        "predicted animation sight movement disappeared from temporal observation");
    FieldRifleShotAuditHistory empty{};
    check(!compare_field_rifle_pretrigger(empty,match,f).available,
        "missing pre-trigger observation was synthesized");
    auto foreign_fire=f; ++foreign_fire.source_publication_serial;
    check(!compare_field_rifle_pretrigger(h,match,foreign_fire).available,
        "foreign shot acquired a qualified pre-trigger claim");
}
} // namespace

int main() {
    try { correlation(); rejection(); temporal();
        std::cout<<checks<<" field-rifle shot audit checks passed\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
