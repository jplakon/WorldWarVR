#include "native_prompt_loading_item_observation.hpp"
#include "native_prompt_loading_tip_policy.hpp"
#include <cstdio>
#include <cstring>
#include <limits>
using namespace wawvr::mod;
namespace {
unsigned assertions{};
bool check(bool value,const char* name){++assertions;if(!value)std::printf("FAIL %s\n",name);return value;}
LoadingItemSnapshot good(const char* key="PLATFORM_DYK_MSG33"){
    LoadingItemSnapshot s{};s.dvar=0x18000000;s.item_read=s.context_read=s.dvar_read=s.key_read=true;
    strcpy_s(s.dvar_name.data(),s.dvar_name.size(),"didyouknow");strcpy_s(s.key.data(),s.key.size(),key);return s;
}
std::uint32_t rejection(const LoadingItemSnapshot& first,const LoadingItemSnapshot& second,
    std::uintptr_t key=0x10002D,std::uintptr_t stack=0x100000,
    std::uint32_t call=0x5C6FAE,std::uint32_t parent=0x5C98DB,std::string_view observed="PLATFORM_DYK_MSG33"){
    return loading_item_observation_rejections(first,second,key,stack,call,0x5C6FAE,parent,observed);
}
}
int main(){
    const LoadingTipFramePolicy allowed{true,true,true,true,true,2,16};
    if(!check(loading_tip_frame_allows(allowed),"initial witnessed frontend policy"))return 1;
    for(unsigned field=0;field<7;++field){
        auto p=allowed;
        switch(field){case 0:p.opt_in=false;break;case 1:p.presentation_valid=false;break;
        case 2:p.frame_current=false;break;case 3:p.views_valid=false;break;
        case 4:p.focused=false;break;case 5:p.connection=10;break;default:p.key_catchers=17;break;}
        if(!check(!loading_tip_frame_allows(p),"each frontend/focus/optin gate required"))return 1;
    }
    for(const auto connection:{0,1,3,9,10}){auto p=allowed;p.connection=connection;
        if(!check(!loading_tip_frame_allows(p),"unwitnessed connection stays native"))return 1;}
    for(const auto catchers:{0U,1U,17U,32U}){auto p=allowed;p.key_catchers=catchers;
        if(!check(!loading_tip_frame_allows(p),"console or nonmenu catchers stay native"))return 1;}

    for(const auto key:{"PLATFORM_DYK_MSG33","PLATFORM_DYK_MSG34","PLATFORM_DYK_MSG35","PLATFORM_DYK_MSG36"}){
        auto s=good(key);if(!check(loading_item_key_is_exact(key),"exact loading key")||
            !check(rejection(s,s,0x10002D,0x100000,0x5C6FAE,0x5C98DB,key)==0,"exact stable synthetic provenance"))return 1;
        s.type=1;if(!check(rejection(s,s,0x10002D,0x100000,0x5C6FAE,0x5C98DB,key)==0,"type1 plain dvar"))return 1;
    }
    for(const auto key:{"","PLATFORM_DYK_MSG32","PLATFORM_DYK_MSG37","PLATFORM_DYK_MSG033","platform_dyk_msg33","PLATFORM_DYK_COOP_MSG33","subtitle PLATFORM_DYK_MSG33"})
        if(!check(!loading_item_key_is_exact(key),"unrelated key not observed"))return 1;
    const auto base=good();
    for(unsigned field=0;field<12;++field){
        auto changed=base;
        switch(field){
        case 0:changed.type=13;break;case 1:changed.flags=2;break;case 2:changed.text=1;break;
        case 3:changed.dvar++;break;case 4:changed.expression_count=1;break;case 5:changed.local_client=1;break;
        case 6:changed.dvar_name[0]='X';break;case 7:changed.key[0]='X';break;
        case 8:changed.item_read=false;break;case 9:changed.context_read=false;break;
        case 10:changed.dvar_read=false;break;default:changed.key_read=false;break;
        }
        if(!check((rejection(base,changed)&loading_item_unstable)!=0,"every changing field/read receipt rejects stability"))return 1;
    }
    for(unsigned field=0;field<4;++field){auto s=base;
        if(field==0)s.item_read=false;else if(field==1)s.context_read=false;else if(field==2)s.dvar_read=false;else s.key_read=false;
        if(!check((rejection(s,s)&loading_item_unreadable)!=0&&!loading_item_snapshot_readable(s),"matching unreadable snapshots never qualify"))return 1;
    }
    for(const auto flag:{1U,2U,3U}){auto s=base;s.flags=flag;if(!check((rejection(s,s)&loading_item_nonplain_flags)!=0,"encoded/cinematic excluded"))return 1;}
    auto s=base;s.type=13;if(!check((rejection(s,s)&loading_item_wrong_type)!=0,"other parent-sharing item type excluded"))return 1;
    s=base;s.text=0x123;if(!check((rejection(s,s)&loading_item_explicit_text)!=0,"literal text not dvar path"))return 1;
    s=base;s.expression_count=1;if(!check((rejection(s,s)&loading_item_expression)!=0,"expression not dvar path"))return 1;
    s=base;s.local_client=1;if(!check((rejection(s,s)&loading_item_other_client)!=0,"other client excluded"))return 1;
    s=base;s.dvar=0;if(!check((rejection(s,s)&loading_item_other_dvar)!=0,"null dvar excluded"))return 1;
    s=base;s.dvar_name.fill('x');if(!check(loading_item_bounded_text(s.dvar_name).empty()&&(rejection(s,s)&loading_item_other_dvar)!=0,"unterminated bounded name refused"))return 1;
    s=base;s.dvar_name[0]='D';if(!check((rejection(s,s)&loading_item_other_dvar)!=0,"dvar exact case required"))return 1;
    s=base;s.key[0]='x';if(!check((rejection(s,s)&loading_item_other_key)!=0,"changed source key excluded"))return 1;
    if(!check((rejection(base,base,0x10002E)&loading_item_nonlocal_key)!=0,"wrong key offset excluded")||
        !check((rejection(base,base,0,0)&loading_item_nonlocal_key)!=0,"null stack excluded")||
        !check(!loading_item_key_on_dvar_stack(1,(std::numeric_limits<std::uintptr_t>::max)()-0x10),"address overflow refused")||
        !check((rejection(base,base,0x10002D,0x100000,0x5C6FAF)&loading_item_wrong_call)!=0,"wrong call return")||
        !check((rejection(base,base,0x10002D,0x100000,0x5C6FAE,0x5C98BE)&loading_item_wrong_parent)!=0,"other painter caller")||
        !check((rejection(base,base,0x10002D,0x100000,0x5C6FAE,0x5C98DB,"UNKNOWN")&loading_item_other_key)!=0,"unknown key refused"))return 1;
    std::printf("PASS %u pure loading-item observation assertions; no native reads/output changes\n",assertions);
}
