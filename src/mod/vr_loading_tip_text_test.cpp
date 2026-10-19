// SPDX-License-Identifier: GPL-3.0-only
#include "vr_loading_tip_text.hpp"

#include <cstdio>
#include <string>

int main() {
    using namespace wawvr::mod;
    unsigned checks=0;
    const auto check=[&](const bool ok,const char* label) {
        ++checks;
        if (!ok) std::printf("FAIL %s\n",label);
        return ok;
    };
    for (const auto& tip:kVrLoadingTipText) {
        const auto output=resolve_vr_loading_tip_text(tip.key,tip.original_english);
        if (!check(output==tip.vr_text,"four exact shipped texts") ||
            !check(output.data()==tip.vr_text.data(),"immutable process-lifetime storage") ||
            !check(output.data()[output.size()]=='\0',"native NUL terminated result") ||
            !check(output.size()<128,"short display text") ||
            !check(resolve_vr_loading_tip_text(tip.key,"German/custom localized text").empty(),"custom localization unchanged") ||
            !check(resolve_vr_loading_tip_text("UNKNOWN_KEY",tip.original_english).empty(),"unknown key unchanged") ||
            !check(resolve_vr_loading_tip_text(std::string("@")+std::string(tip.key),tip.original_english).empty(),"only exact unprefixed key") ||
            !check(resolve_vr_loading_tip_text(tip.key,std::string(tip.original_english)+" ").empty(),"no sentence substring matching") ||
            !check(resolve_vr_loading_tip_text(tip.key,tip.original_english.substr(0,tip.original_english.size()-1)).empty(),"truncation rejected")) return 1;
        for (const auto& other:kVrLoadingTipText)
            if (other.key!=tip.key && !check(resolve_vr_loading_tip_text(tip.key,other.original_english).empty(),"wrong text for key rejected")) return 1;
    }
    const auto& melee=kVrLoadingTipText[0];
    if (!check(resolve_vr_loading_tip_text(melee.key,melee.original_english,true).find("right stick")!=std::string_view::npos,"optional actual melee setting") ||
        !check(resolve_vr_loading_tip_text(melee.key,melee.original_english,false).find("stick")==std::string_view::npos,"default no stick-click promise") ||
        !check(resolve_vr_loading_tip_text({},{}).empty(),"empty values pass through") ||
        !check(resolve_vr_loading_tip_text("PLATFORM_DYK_COOP_MSG1",melee.original_english).empty(),"coop table not relabelled") ||
        !check(kVrLoadingTipText[2].vr_text.find("no assigned VR control")!=std::string_view::npos,"no invented breath binding") ||
        !check(kVrLoadingTipText[3].vr_text.find("shortcut is disabled")!=std::string_view::npos,"no unsafe grenade shortcut advertised")) return 1;
    std::printf("PASS %u exact loading-tip text assertions\n",checks);
    return 0;
}
