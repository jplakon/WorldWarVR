// SPDX-License-Identifier: GPL-3.0-only
#include "native_prompt_mission_start_policy.hpp"
#include "native_prompt_loading_tip_policy.hpp"

#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

using namespace wawvr::mod;
using wawvr::xr::ControllerBindingProfile;
namespace {
unsigned assertions{};
bool check(const bool value,const char* name){++assertions;if(!value)std::printf("FAIL %s\n",name);return value;}
MissionStartItemSnapshot good(){
    MissionStartItemSnapshot s{};s.text=0x18000000;s.type=1;
    s.item_read=s.context_read=s.source_read=s.key_read=true;
    strcpy_s(s.source.data(),s.source.size(),kMissionStartInlineSource.data());
    strcpy_s(s.key.data(),s.key.size(),kMissionStartKey.data());return s;
}
std::uint32_t rejection(const MissionStartItemSnapshot& a,const MissionStartItemSnapshot& b,
    const std::uintptr_t key=0x18000001,const std::uint32_t call=0x5C6FAE,
    const std::uint32_t parent=0x5C98DB,const std::string_view observed=kMissionStartKey,
    const std::uint32_t expected=0x5C6FAE){
    return mission_start_item_rejections(a,b,key,call,expected,parent,observed);
}
}
int main(){
    const auto absent=resolve_mission_start_setting(false,{});
    const auto explicit_on=resolve_mission_start_setting(true,L"1");
    const auto explicit_off=resolve_mission_start_setting(true,L"0");
    if(!check(absent.enabled && absent.source==MissionStartSettingSource::default_on,"absent option defaults ON") ||
        !check(explicit_on.enabled && explicit_on.source==MissionStartSettingSource::explicit_on,"exact explicit1 ON") ||
        !check(!explicit_off.enabled && explicit_off.source==MissionStartSettingSource::explicit_off,"exact explicit0 OFF") ||
        !check(mission_start_key_bridge_requested(false,false,absent),"no diagnostics/loading flags needed for default bridge") ||
        !check(!mission_start_key_bridge_requested(false,false,explicit_off),"explicitOFF without diagnostics needs no key bridge"))return 1;
    for(const auto value:{std::wstring_view{},std::wstring_view{L"true"},std::wstring_view{L"false"},std::wstring_view{L"01"},
            std::wstring_view{L" 1"},std::wstring_view{L"1 "},std::wstring_view{L"-1"},std::wstring_view{L"2"},
            std::wstring_view{L"0x1"},std::wstring_view{L"1111111111111111"},std::wstring_view{L"1\0x",3}}){
        const auto setting=resolve_mission_start_setting(true,value);
        if(!check(!setting.enabled && setting.source==MissionStartSettingSource::invalid,"empty/malformed values fail CLOSED") ||
            !check(!mission_start_key_bridge_requested(false,false,setting),"invalid option cannot request bridge alone"))return 1;
    }
    for(const auto setting:{absent,explicit_on,explicit_off,resolve_mission_start_setting(true,L"invalid")}){
        for(const bool diagnostics:{false,true})for(const bool loading:{false,true}){
            if(!check(mission_start_key_bridge_requested(diagnostics,loading,setting)==
                (diagnostics||loading||setting.enabled),"exact shared bridge installation dependency"))return 1;
        }
    }
    if(!check(std::string_view(mission_start_setting_source_name(absent.source))=="default-on","truthful default source") ||
        !check(std::string_view(mission_start_setting_source_name(explicit_on.source))=="explicit-on","truthful explicit ON source") ||
        !check(std::string_view(mission_start_setting_source_name(explicit_off.source))=="explicit-off","truthful explicit OFF source") ||
        !check(std::string_view(mission_start_setting_source_name(MissionStartSettingSource::invalid))=="invalid","truthful invalid source"))return 1;
    const MissionStartFramePolicy allowed{true,true,true,true,true,true,6,16,ControllerBindingProfile::generic};
    for(const auto profile:{ControllerBindingProfile::generic,ControllerBindingProfile::touch,ControllerBindingProfile::index}){
        for(const auto connection:{6,7}){
            auto p=allowed;p.right_profile=profile;p.connection=connection;
            if(!check(mission_start_frame_rejections(p)==0,"exact current loading policy") ||
                !check(!resolve_vr_mission_start_text(kMissionStartKey,kMissionStartNativeEnglish,profile).empty(),"three actual/semantic labels"))return 1;
        }
        const auto text=resolve_vr_mission_start_text(kMissionStartKey,kMissionStartNativeEnglish,profile);
        if(!check(text.data()[text.size()]==0,"immutable NUL terminated output") ||
            !check(text.size()<64,"bounded compact caption") ||
            !check(text.find("TRIGGER")==std::string_view::npos,"no unqualified trigger binding") ||
            !check(text==(profile==ControllerBindingProfile::generic ? "Press R-PRIMARY to Start the Mission" : "Press RIGHT A to Start the Mission"),"exact family label"))return 1;
    }
    for(unsigned field=0;field<9;++field){auto p=allowed;
        switch(field){case 0:p.enabled=false;break;case 1:p.presentation_valid=false;break;
        case 2:p.frame_current=false;break;case 3:p.views_valid=false;break;case 4:p.focused=false;break;
        case 5:p.primary_active=false;break;case 6:p.connection=10;break;case 7:p.key_catchers=17;break;
        default:p.right_profile=static_cast<ControllerBindingProfile>(255);break;}
        if(!check(mission_start_frame_rejections(p)!=0,"every frame/action/focus gate required"))return 1;
    }
    for(const auto connection:{-1,0,1,2,3,4,5,8,9,10,11,2147483647}){
        auto p=allowed;p.connection=connection;if(!check(mission_start_frame_rejections(p)&mission_start_connection,"other connections stay native"))return 1;
    }
    for(const auto catchers:{0U,1U,15U,17U,32U,0xFFFFFFFFU}){
        auto p=allowed;p.key_catchers=catchers;if(!check(mission_start_frame_rejections(p)&mission_start_catchers,"exact menu-only catcher"))return 1;
    }
    for(const auto profile:{3U,255U}){
        const auto p=static_cast<ControllerBindingProfile>(profile);
        if(!check(!mission_start_profile_supported(p),"unknown profile fails") ||
            !check(resolve_vr_mission_start_text(kMissionStartKey,kMissionStartNativeEnglish,p).empty(),"unknown profile no output"))return 1;
    }
    for(const auto key:{std::string_view{},std::string_view{"@MENU_CLICK_TO_START_THE_MISSION"},std::string_view{"menu_click_to_start_the_mission"},std::string_view{"PLATFORM_DYK_MSG33"},std::string_view{"MENU_CLICK_TO_START_THE_MISSION_EXTRA"}})
        if(!check(resolve_vr_mission_start_text(key,kMissionStartNativeEnglish,ControllerBindingProfile::touch).empty(),"only exact key"))return 1;
    for(const auto text:{std::string_view{},std::string_view{"Klicken, um die Mission zu starten"},std::string_view{"Click to Start the Mission "},std::string_view{"Click to Start the Missio"},std::string_view{"Press F to Start the Mission"}})
        if(!check(resolve_vr_mission_start_text(kMissionStartKey,text,ControllerBindingProfile::touch).empty(),"complete shipped English only"))return 1;
    const auto base=good();
    if(!check(rejection(base,base)==0,"stable exact inline source") ||
        !check(mission_start_item_readable(base),"owned readable facts"))return 1;
    auto s=base;s.type=0;if(!check(rejection(s,s)==0,"direct type0 source"))return 1;
    s=base;s.dvar=0x12345678;if(!check(rejection(s,s)==0,"unused dvar does not change native inline branch"))return 1;
    for(unsigned field=0;field<12;++field){s=base;
        switch(field){case 0:s.type=13;break;case 1:s.flags=1;break;case 2:s.text++;break;
        case 3:s.dvar++;break;case 4:s.expression_count=1;break;case 5:s.local_client=1;break;
        case 6:s.source[0]='X';break;case 7:s.key[0]='X';break;case 8:s.item_read=false;break;
        case 9:s.context_read=false;break;case 10:s.source_read=false;break;default:s.key_read=false;break;}
        if(!check(rejection(base,s)&mission_start_item_unstable,"all sparse fact/read changes refuse"))return 1;
    }
    for(unsigned field=0;field<4;++field){s=base;
        switch(field){case 0:s.item_read=false;break;case 1:s.context_read=false;break;case 2:s.source_read=false;break;default:s.key_read=false;break;}
        if(!check(!mission_start_item_readable(s) && (rejection(s,s)&mission_start_item_unreadable),"matching unreadable sources refuse"))return 1;
    }
    for(const auto flags:{1U,2U,3U}){s=base;s.flags=flags;if(!check(rejection(s,s)&mission_start_nonplain,"encoded/cinematic routes excluded"))return 1;}
    s=base;s.type=13;if(!check(rejection(s,s)&mission_start_wrong_type,"derived type13 excluded"))return 1;
    s=base;s.expression_count=1;if(!check(rejection(s,s)&mission_start_expression,"expression source excluded"))return 1;
    s=base;s.local_client=1;if(!check(rejection(s,s)&mission_start_other_client,"other context excluded"))return 1;
    s=base;s.text=0;if(!check(rejection(s,s)&mission_start_noninline_pointer,"dvar/stack/null source excluded"))return 1;
    s=base;s.text=0xFFFFFFFFU;if(!check(rejection(s,s,0)&mission_start_noninline_pointer,"PE32 source increment overflow refused"))return 1;
    s=base;s.source.fill('x');if(!check(rejection(s,s)&mission_start_other_source,"unterminated source refused"))return 1;
    s=base;s.key.fill('x');if(!check(rejection(s,s)&mission_start_other_key,"unterminated key refused"))return 1;
    s=base;s.source[0]='x';if(!check(rejection(s,s)&mission_start_other_source,"unrelated source refused"))return 1;
    s=base;s.key[0]='x';if(!check(rejection(s,s)&mission_start_other_key,"unrelated key refused"))return 1;
    if(!check(rejection(base,base,0x18000002)&mission_start_noninline_pointer,"one byte wrong source pointer refused") ||
        !check(rejection(base,base,0x18000001,0x5C6FAF)&mission_start_wrong_call,"exact call return") ||
        !check(rejection(base,base,0x18000001,0x5C6FAE,0x5C98BE)&mission_start_wrong_parent,"exact parent return") ||
        !check(rejection(base,base,0x18000001,0x5C6FAE,0x5C98DB,"UNKNOWN")&mission_start_other_key,"exact observed key") ||
        !check(rejection(base,base,0x18000001,0x5C6FAE,0x5C98DB,kMissionStartKey,0)&mission_start_wrong_call,"missing expected target refused"))return 1;
    const LoadingTipFramePolicy old{true,true,true,true,true,2,16};
    if(!check(loading_tip_frame_allows(old),"four-tip frontend unchanged"))return 1;
    for(const auto connection:{6,7}){auto p=old;p.connection=connection;if(!check(!loading_tip_frame_allows(p),"mission policy never widens four-tip gate"))return 1;}
    std::printf("PASS %u pure mission-caption assertions; no native reads/output hooks\n",assertions);
    return 0;
}
