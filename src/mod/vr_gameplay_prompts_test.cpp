// SPDX-License-Identifier: GPL-3.0-only
#include "vr_gameplay_prompts.hpp"
#include "cursor_hint_display_contract.hpp"
#include "../xr/controller_binding_profile.hpp"

#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace wawvr::mod;
void check(const bool pass, const std::string_view message) {
    if (!pass) throw std::runtime_error(std::string(message));
}
bool contains(const std::array<char,kVrPromptLineBytes>& line,
              const std::string_view text) {
    return std::string_view(line.data()).find(text) != std::string_view::npos;
}
void bounded(const VrGameplayPrompt& prompt) {
    check(prompt.first.back() == '\0' && prompt.second.back() == '\0',
        "all prompt storage stays terminated and bounded");
    for (const auto& line : {prompt.first,prompt.second}) {
        check(!contains(line,"PRESS F") && !contains(line,"KEYBOARD") &&
            !contains(line,"PRESS 6"), "guidance never offers a keyboard route");
        for (const char c : std::string_view(line.data()))
            check(c==' ' || c==':' || c==';' || c=='/' || c=='-' || c=='+' || c=='|' ||
                (c>='A' && c<='Z') || (c>='0' && c<='9'),
                "all emitted characters have a bitmap glyph or space");
    }
}
}

int main() {
    using Profile = wawvr::xr::ControllerBindingProfile;
    CursorHintDisplayWitness cursor_witness{
        .call_return=0x004512E9,.parent_return=0x00451562,.context=0x0084F274,
        .policy=0,.local_client=0,.stack_readable=true,.message_readable=true};
    for (const auto parent : {0x00451562U,0x004515AEU,0x00451952U}) {
        cursor_witness.parent_return=parent;
        check(cursor_hint_display_rejection_mask(cursor_witness)==0,
            "only the three exact native cursor use-string parents qualify");
    }
    for (unsigned field=0;field<7;++field) {
        auto bad=cursor_witness;
        switch (field) {
        case 0:bad.stack_readable=false;break;
        case 1:bad.call_return=0x00437FEA;break;
        case 2:bad.parent_return=0x0043892D;break;
        case 3:bad.context=0x0084D3C0;break;
        case 4:bad.policy=1;break;
        case 5:bad.message_readable=false;break;
        case 6:bad.local_client=1;break;
        }
        check(cursor_hint_display_rejection_mask(bad)==(1U<<field),
            "cursor ABI witness rejects each wrong/unreadable field without generic fallback");
    }
    check(cursor_hint_display_rejection_mask({.policy=1,.local_client=1})==0x7F,
        "incomplete cursor ABI witness cannot accidentally qualify");
    for (const auto plain_key : {"MAK_TRUCK_TRIGGER","SCRIPT_PLATFORM_HINT_PLANTEXPLOSIVES",
            "ZOMBIE_BUTTON_BUY_OPEN_DOOR_1000"})
        check(resolve_vr_encoded_use_hint_copy(plain_key,{}).empty(),
            "plain cursor hint key remains native until actual argument grammar is witnessed");
    std::uint32_t observed_bits=0;
    for (const auto key : {"MAK_TRUCK_TRIGGER","MAK_TRUCK_HINT",
            "SCRIPT_PLATFORM_HINT_PLANTEXPLOSIVES","SCRIPT_PLATFORM_HINT_RELOAD",
            "SCRIPT_PLATFORM_HINT_MELEEATTACK","PEL1_ROCKET_HOWTO",
            "PLATFORM_DYK_MSG33","PLATFORM_DYK_MSG34","PLATFORM_DYK_MSG35",
            "PLATFORM_DYK_MSG36","PLATFORM_THROWBACKGRENADE"}) {
        const auto bit=observed_vr_localized_hint_bit(key);
        check(bit!=0 && (bit&(bit-1))==0 && (observed_bits&bit)==0,
            "diagnostic authored-key allowlist has unique one-hot identities");
        observed_bits|=bit;
        check(resolve_vr_native_binding_prompt(key,{}).empty(),
            "authored localization keys never enter action-label replacement");
    }
    for (const auto untouched : {"", "+activate", "+attack", "@MAK_TRUCK_TRIGGER",
            "MAK_TRUCK_TRIGGER extra","mak_truck_trigger","PLATFORM_DYK_MSG3",
            "PLATFORM_DYK_MSG330","SCRIPT_PLATFORM_HINT_RELOAD\n",
            "Press F to Activate Truck", "A subtitle mentions MAK_TRUCK_TRIGGER"})
        check(observed_vr_localized_hint_bit(untouched)==0,
            "observer rejects actions, prefixed keys, aliases and whole sentences");
    for (const auto key : {"ZOMBIE_BUTTON_BUY_OPEN_DOOR_100","ZOMBIE_BUTTON_BUY_OPEN_DOOR_200",
            "ZOMBIE_BUTTON_BUY_OPEN_DOOR_250","ZOMBIE_BUTTON_BUY_OPEN_DOOR_500",
            "ZOMBIE_BUTTON_BUY_OPEN_DOOR_750","ZOMBIE_BUTTON_BUY_OPEN_DOOR_1000",
            "ZOMBIE_BUTTON_BUY_OPEN_DOOR_1250","ZOMBIE_BUTTON_BUY_OPEN_DOOR_1500",
            "ZOMBIE_BUTTON_BUY_OPEN_DOOR_1750","ZOMBIE_BUTTON_BUY_OPEN_DOOR_2000",
            "ZOMBIE_WEAPONCOSTAMMO"}) {
        const auto bit=observed_vr_localized_hint_bit(key);
        check(std::string_view(key).size()<64 && bit!=0 && (bit&(bit-1))==0 &&
            (observed_bits&bit)==0,"source-backed exact Nacht keys have distinct bounded identities");
        observed_bits|=bit;
        check(resolve_vr_native_binding_prompt(key,{}).empty(),
            "Nacht key observation never changes action bindings or native display text");
    }
    for (const auto key : {"ZOMBIE_BUTTON_BUY_OPEN_DOOR_10000","ZOMBIE_BUTTON_BUY_OPEN_DOOR_",
            "ZOMBIE_WEAPONCOSTAMMO extra","ZOMBIE_BUTTON_BUY_OPEN_DOOR_1000\n"})
        check(observed_vr_localized_hint_bit(key)==0,"Nacht observer accepts no key family or price prefix");
    std::uint64_t coop_observed_bits=0;
    for (unsigned number=1;number<=33;++number) {
        const auto key=std::string("PLATFORM_DYK_COOP_MSG")+std::to_string(number);
        const auto preserved=key;
        const auto bit=observed_vr_coop_loading_hint_bit(key);
        check(bit==(std::uint64_t{1}<<(number-1)) &&
            (coop_observed_bits&bit)==0 && key.size()<64,
            "all33 exact stock CoOp loading keys have distinct bounded64bit observation identities");
        coop_observed_bits|=bit;
        check(observed_vr_localized_hint_bit(key)==0 &&
            !observe_vr_encoded_prompt(key).exact_single_target_key &&
            resolve_vr_native_binding_prompt(key,{}).empty() &&
            resolve_vr_encoded_use_hint_copy(key+char(0x15)+"F",{}).empty() && key==preserved,
            "loading-key classification never translates text or expands either binding/use replacement allowlist");
    }
    check(coop_observed_bits==0x00000001FFFFFFFFULL &&
        observed_vr_coop_loading_hint_bit("PLATFORM_DYK_COOP_MSG33")==0x0000000100000000ULL,
        "the33rd loading-key bit is retained independently of the existing32bit gameplay mask");
    for (const auto untouched : {"", "PLATFORM_DYK_COOP_MSG", "PLATFORM_DYK_COOP_MSG0",
            "PLATFORM_DYK_COOP_MSG01", "PLATFORM_DYK_COOP_MSG34", "PLATFORM_DYK_COOP_MSG330",
            "@PLATFORM_DYK_COOP_MSG1", "platform_dyk_coop_msg1", "PLATFORM_DYK_COOP_MSG1 extra",
            "PLATFORM_DYK_COOP_MSG33\n", "PLATFORM_DYK_MSG33", "PEL1_ROCKET_HOWTO",
            "+actionslot 4", "A subtitle mentions PLATFORM_DYK_COOP_MSG1"})
        check(observed_vr_coop_loading_hint_bit(untouched)==0,
            "loading observer accepts only exact source keys, not a numeric family, action or arbitrary sentence");
    check(observed_vr_coop_loading_hint_bit(std::string("PLATFORM_DYK_COOP_MSG1")+'\0'+"extra")==0,
        "embedded NUL suffix cannot become an exact loading key");
    const auto encoded=std::string("MAK_TRUCK_TRIGGER")+char(0x15)+"F";
    auto observed=observe_vr_encoded_prompt(encoded);
    check(observed.valid && observed.exact_single_target_key &&
        observed.segment_count==2 && observed.localized_segments==1 &&
        observed.literal_arguments==1 && observed.segments[0].localized_key &&
        !observed.segments[1].localized_key && observed.segments[1].bytes==1 &&
        observed.known_key_mask==observed_vr_localized_hint_bit("MAK_TRUCK_TRIGGER"),
        "encoded observer retains exact implicit key plus one literal formatting argument");
    check(encoded==std::string("MAK_TRUCK_TRIGGER")+char(0x15)+"F",
        "encoded observation does not replace or modify key/argument bytes");
    observed=observe_vr_encoded_prompt(std::string(1,char(0x14))+"MAK_TRUCK_TRIGGER"+
        char(0x15)+"F"+char(0x14)+"UNKNOWN_ARGUMENT_KEY");
    check(observed.valid && !observed.exact_single_target_key &&
        observed.localized_segments==2 && observed.literal_arguments==1 &&
        observed.segments[2].known_key_bit==0,
        "additional localized argument stays distinct and cannot prove one-key replacement grammar");
    observed=observe_vr_encoded_prompt(std::string(1,char(0x15))+"MAK_TRUCK_TRIGGER");
    check(observed.valid && observed.known_key_mask==0 && !observed.exact_single_target_key,
        "literal payload matching a target key never becomes key identity");
    observed=observe_vr_encoded_prompt(std::string("MAK_TRUCK_TRIGGER")+char(0x15)+
        char(0x16)+"&&1"+char(0x15)+"F");
    check(observed.valid && observed.segment_count==3 &&
        observed.segments[1].conversion_marker16 && !observed.segments[1].localized_key &&
        !observed.segments[2].conversion_marker16,
        "conversion-marker role is observed without changing native substitution behavior");
    observed=observe_vr_encoded_prompt(std::string(1,char(0x15))+"intro"+
        char(0x14)+"MAK_TRUCK_TRIGGER");
    check(observed.valid && observed.known_key_mask!=0 && !observed.exact_single_target_key,
        "a later target key is observable but is not the exact first-key grammar");
    for (const auto& invalid : {std::string("MAK_TRUCK_TRIGGER\n"),
            std::string("MAK_TRUCK_TRIGGER\0extra",23),std::string(1024,'A')})
        check(!observe_vr_encoded_prompt(invalid).valid,
            "embedded NUL, unsupported control and oversized encoded input fail closed");
    auto many=std::string("MAK_TRUCK_TRIGGER");
    for (int i=0;i<7;++i) many+=std::string(1,char(0x15))+"argument";
    check(observe_vr_encoded_prompt(many).valid &&
        observe_vr_encoded_prompt(many).segment_count==8,
        "exact eight-segment bound is supported without retaining payload text");
    many+=std::string(1,char(0x15))+"ninth";
    check(!observe_vr_encoded_prompt(many).valid,
        "too many encoded segments cannot yield a partial key witness");
    for (const auto key : {"MAK_TRUCK_TRIGGER","SCRIPT_PLATFORM_HINT_PLANTEXPLOSIVES",
            "ZOMBIE_BUTTON_BUY_OPEN_DOOR_100","ZOMBIE_BUTTON_BUY_OPEN_DOOR_200",
            "ZOMBIE_BUTTON_BUY_OPEN_DOOR_250","ZOMBIE_BUTTON_BUY_OPEN_DOOR_500",
            "ZOMBIE_BUTTON_BUY_OPEN_DOOR_750","ZOMBIE_BUTTON_BUY_OPEN_DOOR_1000",
            "ZOMBIE_BUTTON_BUY_OPEN_DOOR_1250","ZOMBIE_BUTTON_BUY_OPEN_DOOR_1500",
            "ZOMBIE_BUTTON_BUY_OPEN_DOOR_1750","ZOMBIE_BUTTON_BUY_OPEN_DOOR_2000"})
        for (const bool prefix : {false,true})
            for (const auto profile : {Profile::generic,Profile::touch,Profile::index}) {
                const auto original=(prefix?std::string(1,char(0x14)):std::string{})+
                    key+char(0x15)+"F";
                const auto preserved=original;
                const auto copy=resolve_vr_encoded_use_hint_copy(original,
                    {.left_binding_profile=profile});
                const auto expected=(prefix?std::string(1,char(0x14)):std::string{})+
                    key+char(0x15)+(profile==Profile::touch?"LEFT X":
                        profile==Profile::index?"LEFT A":"L-PRIMARY");
                check(copy==expected && original==preserved && copy.data()!=original.data(),
                    "exact scoped use copy preserves native key/price/markers/source and changes only binding literal");
            }
    const auto held=resolve_vr_encoded_use_hint_copy(encoded,{.left_binding_profile=Profile::touch});
    const auto held_pointer=held.data();
    const auto held_text=std::string(held);
    check(!resolve_vr_encoded_use_hint_copy(std::string("ZOMBIE_BUTTON_BUY_OPEN_DOOR_1000")+
        char(0x15)+"F",{.left_binding_profile=Profile::index}).empty() &&
        held.data()==held_pointer && held==held_text,
        "private encoded display copies are immutable with process-lifetime storage");
    for (const auto& native : {std::string("MAK_TRUCK_TRIGGER")+char(0x15)+"E",
            std::string("MAK_TRUCK_TRIGGER")+char(0x15)+"FF",
            std::string("MAK_TRUCK_TRIGGER")+char(0x15)+"^3F^7",
            std::string("MAK_TRUCK_TRIGGER")+char(0x15)+char(0x16)+"F",
            std::string("MAK_TRUCK_TRIGGER")+char(0x15)+char(0x15)+"F",
            std::string("MAK_TRUCK_TRIGGER")+char(0x15)+"F"+char(0x15)+"1000",
            std::string("PLATFORM_DYK_MSG34")+char(0x15)+"F",
            std::string("ZOMBIE_WEAPONCOSTAMMO")+char(0x15)+"F",
            std::string("ZOMBIE_BUTTON_BUY_OPEN_DOOR_10000")+char(0x15)+"F",
            std::string("A subtitle mentions F"),std::string("@MAK_TRUCK_TRIGGER")+char(0x15)+"F"})
        check(resolve_vr_encoded_use_hint_copy(native,{}).empty(),
            "unknown/custom binding, role/arity/conversion ambiguity and unrelated text are unchanged");
    check(resolve_vr_encoded_use_hint_copy(encoded,
        {.left_binding_profile=static_cast<Profile>(255)}).empty(),
        "unrecognized active controller profile cannot select a private use-label copy");
    for (const auto profile : {Profile::generic,Profile::touch,Profile::index,
                              static_cast<Profile>(255)})
        for (const bool primary : {false,true})
            for (const bool secondary : {false,true}) {
                const auto verified = wawvr::xr::verified_controller_binding_profile(
                    profile,primary,secondary);
                check(verified == ((primary && secondary &&
                    (profile == Profile::touch || profile == Profile::index))
                    ? profile : Profile::generic),
                    "missing/remapped/unknown controller sources retain semantic labels");
            }
    VrNativeBindingPromptSettings settings{};
    check(resolve_vr_native_binding_prompt("+activate",settings)=="L-PRIMARY",
        "native interaction display maps exact activate action");
    check(resolve_vr_native_binding_prompt("+usereload",settings)=="L-PRIMARY",
        "native combined-use label follows actual VR use action");
    check(resolve_vr_native_binding_prompt("+sprint",settings)=="LEFT STICK CLICK",
        "native sprint label does not advertise Shift");
    check(resolve_vr_native_binding_prompt("+reload",settings).find("MANUAL")!=std::string_view::npos,
        "native reload label follows manual preference");
    check(resolve_vr_native_binding_prompt("+frag",settings)=="LEFT TRIGGER AT LEFT HIP",
        "native frag label preserves accepted physical belt route");
    check(resolve_vr_native_binding_prompt("+actionslot 4",settings)==
        "L-SECONDARY (HELD) + LEFT STICK LEFT","native support label retains held modifier without a second imperative");
    check(resolve_vr_native_binding_prompt("+attack",settings)=="RIGHT TRIGGER",
        "radio tutorial fire display is correct before selecting support");
    settings.support_weapon_selected=true;
    check(resolve_vr_native_binding_prompt("+attack",settings)=="RIGHT TRIGGER",
        "equipped radio retains the same controller fire display");
    settings.automatic_reload=true; settings.button_grenades=true;
    settings.right_stick_melee=true;
    check(resolve_vr_native_binding_prompt("+reload",settings).find("AUTO")!=std::string_view::npos &&
        resolve_vr_native_binding_prompt("+frag",settings)=="LEFT TRIGGER" &&
        resolve_vr_native_binding_prompt("+melee",settings)=="RIGHT STICK CLICK",
        "all opt-in native labels follow settings");
    for (const auto untouched : {"+melee_breath","+holdbreath","+breath_sprint",
            "+sprint extra","+reload\nquit","Press F to reload","PLATFORM_RELOAD",
            "A subtitle mentions reload", "Reload", "6", "+actionslot 5",
            "+attack extra", "+attack\nquit", "+Attack", "PLATFORM_ATTACK",
            "Press Mouse 1 to fire"})
        check(resolve_vr_native_binding_prompt(untouched,settings).empty(),
            "unknown actions, aliases and whole sentences remain unchanged");

    for (const auto profile : {Profile::touch,Profile::index}) {
        settings = {.left_binding_profile=profile,.right_binding_profile=profile};
        check(resolve_vr_native_binding_prompt("+activate",settings) ==
            (profile == Profile::touch ? "LEFT X" : "LEFT A"),
            "native use label identifies verified physical controller button");
        check(resolve_vr_native_binding_prompt("+actionslot 4",settings) ==
            (profile == Profile::touch ? "LEFT Y (HELD) + LEFT STICK LEFT"
                                      : "LEFT B (HELD) + LEFT STICK LEFT"),
            "radio selector identifies verified physical controller modifier");
        check(resolve_vr_native_binding_prompt("+reload",settings)=="RIGHT A (MANUAL RELOAD)",
            "manual reload label identifies physical right primary");
        settings.automatic_reload=true;
        check(resolve_vr_native_binding_prompt("+reload",settings)=="RIGHT A (AUTO RELOAD)",
            "physical reload label retains automatic preference");
        for (const auto untouched : {"Press F to reload","A subtitle mentions reload",
            "+reload\nquit","+actionslot 4 extra","+melee_breath","PLATFORM_RELOAD"})
            check(resolve_vr_native_binding_prompt(untouched,settings).empty(),
                "controller aliases never accept arbitrary native text or command prefixes");
    }
    for (const auto profile : {Profile::generic,Profile::touch,Profile::index,
                              static_cast<Profile>(255)})
        for (const bool support_selected : {false,true}) {
            settings = {.support_weapon_selected=support_selected,
                .left_binding_profile=profile,.right_binding_profile=profile};
            check(resolve_vr_native_binding_prompt("+attack",settings)=="RIGHT TRIGGER",
                "semantic fire action is stable across support state and controller aliases");
            check(resolve_vr_native_binding_prompt("+attack extra",settings).empty() &&
                resolve_vr_native_binding_prompt("A subtitle mentions fire",settings).empty(),
                "fire display never accepts command prefixes or arbitrary sentences");
            const std::string selector(resolve_vr_native_binding_prompt("+actionslot 4",settings));
            const auto expected_selector=profile==Profile::touch ? "LEFT Y (HELD) + LEFT STICK LEFT"
                : profile==Profile::index ? "LEFT B (HELD) + LEFT STICK LEFT"
                : "L-SECONDARY (HELD) + LEFT STICK LEFT";
            // Compose the audited PEL1 template only in this test. Production
            // still returns action labels and leaves native localization intact.
            const auto sentence="Press "+selector+" then "+
                std::string(resolve_vr_native_binding_prompt("+attack",settings))+
                " to radio in a rocket strike on Japanese defenses.";
            check(selector==expected_selector && selector.size()<128 &&
                sentence==std::string("Press ")+expected_selector+
                    " then RIGHT TRIGGER to radio in a rocket strike on Japanese defenses." &&
                sentence.find("Press HOLD")==std::string::npos,
                "native radio template composes grammatical per-profile labels before and after selection");
            for (const auto native : {"+actionslot 4 extra", "+actionslot 40", "+Actionslot 4",
                    "+actionslot 4\nquit", "PEL1_ROCKET_HOWTO", "Press HOLD L-SECONDARY",
                    "Press [{+actionslot 4}] then [{+attack}] to radio in a rocket strike on Japanese defenses."})
                check(resolve_vr_native_binding_prompt(native,settings).empty(),
                    "radio wording never accepts whole native sentences, key names or command prefixes");
            const auto help=build_vr_gameplay_prompt({.active_stereo_gameplay=true,
                .input_focused=true,.help_held=true,.left_binding_profile=profile});
            check(contains(help.second,profile==Profile::touch ? "HOLD LEFT Y + LEFT STICK LEFT"
                    : profile==Profile::index ? "HOLD LEFT B + LEFT STICK LEFT"
                    : "HOLD L-SECONDARY + LEFT STICK LEFT") && !contains(help.second,"(HELD)"),
                "owned held-help retains its existing imperative HOLD instruction unchanged");
        }
    VrGameplayPromptInput in{.active_stereo_gameplay=true,.input_focused=true};
    check(build_vr_gameplay_prompt(in).kind == VrGameplayPromptKind::none,
        "ordinary aiming is not covered by a permanent help panel");
    in.introduction_visible=true;
    auto prompt=build_vr_gameplay_prompt(in);
    check(prompt.kind == VrGameplayPromptKind::introduction &&
        contains(prompt.first,"L-PRIMARY: USE") &&
        contains(prompt.first,"R-PRIMARY: RELOAD") &&
        contains(prompt.first,"LEFT STICK CLICK: SPRINT"),
        "initial guidance identifies actual semantic VR actions");
    check(contains(prompt.second,"DELIBERATE HAND ATTACK") &&
        !contains(prompt.second,"CLICK: MELEE"),"default melee binding is physical only");
    in.right_stick_melee=true;
    check(contains(build_vr_gameplay_prompt(in).second,"RIGHT STICK CLICK: MELEE"),
        "explicit optional melee setting is reflected");
    in.introduction_visible=false;
    in.help_held=true;
    check(build_vr_gameplay_prompt(in).kind == VrGameplayPromptKind::support_selector,
        "held campaign modifier reveals exact selector gesture");
    check(contains(build_vr_gameplay_prompt(in).first,"MANUAL RELOAD") &&
        contains(build_vr_gameplay_prompt(in).first,"RIGHT STICK CLICK") &&
        contains(build_vr_gameplay_prompt(in).first,"LEFT STICK CLICK: SPRINT") &&
        contains(build_vr_gameplay_prompt(in).second,"LEFT TRIGGER AT LEFT HIP: GRENADE") &&
        contains(build_vr_gameplay_prompt(in).second,"THEN RELEASE"),
        "campaign held help retrieves controls and real hip-grenade/radio route after introduction");
    in.automatic_reload=true;in.button_grenades=true;in.right_stick_melee=false;
    auto held_help=build_vr_gameplay_prompt(in);
    check(contains(held_help.first,"AUTO RELOAD") &&
        contains(held_help.first,"DELIBERATE ATTACK") &&
        !contains(held_help.first,"RIGHT STICK CLICK") &&
        contains(held_help.second,"LEFT TRIGGER: GRENADE") &&
        !contains(held_help.second,"HIP"),
        "campaign held-help labels follow automatic reload, explicit grenade mode and default physical melee");
    bounded(held_help);
    in.automatic_reload=false;in.button_grenades=false;in.right_stick_melee=true;
    in.multiplayer=true;
    check(build_vr_gameplay_prompt(in).kind == VrGameplayPromptKind::controls,
        "multiplayer does not advertise campaign selector");
    check(!contains(build_vr_gameplay_prompt(in).first,"RADIO") &&
        !contains(build_vr_gameplay_prompt(in).second,"RADIO") &&
        !contains(build_vr_gameplay_prompt(in).second,"GRENADE"),
        "campaign held-help expansion leaves multiplayer guide unchanged");
    in.multiplayer=false;
    in.support_weapon_selected=true;
    check(build_vr_gameplay_prompt(in).kind == VrGameplayPromptKind::support_aim,
        "selected radio replaces selector help with aim and trigger");
    in.enemy_grenade_available=true;
    check(build_vr_gameplay_prompt(in).kind == VrGameplayPromptKind::enemy_grenade &&
        contains(build_vr_gameplay_prompt(in).second,"FUSE KEEPS RUNNING"),
        "live enemy grenade outranks all lower urgency guidance");
    for (const bool focused : {false,true}) {
        in.input_focused=focused;
        in.active_stereo_gameplay=!focused;
        check(build_vr_gameplay_prompt(in).kind == VrGameplayPromptKind::none,
            "menu/mono/inactive or unfocused states never draw guidance");
    }
    in={.active_stereo_gameplay=true,.input_focused=true};
    for (const auto family : {VrReloadPromptFamily::native,
            VrReloadPromptFamily::stripper_clip,VrReloadPromptFamily::detachable_magazine,
            VrReloadPromptFamily::en_bloc}) {
        in.reload_family=family;
        for (const auto stage : {VrReloadPromptStage::begin,VrReloadPromptStage::ammunition,
                VrReloadPromptStage::insert,VrReloadPromptStage::chamber}) {
            in.reload_stage=stage;
            prompt=build_vr_gameplay_prompt(in);
            check(prompt.kind == VrGameplayPromptKind::reload,"active reload phase has guidance");
            bounded(prompt);
            in.automatic_reload=true;
            prompt=build_vr_gameplay_prompt(in);
            check(contains(prompt.first,"COMPLETE AUTOMATIC RELOAD") &&
                contains(prompt.second,"DISABLED"),"auto reload never instructs physical insertion");
            bounded(prompt);
            in.automatic_reload=false;
        }
    }
    in.reload_stage=VrReloadPromptStage::ready;
    check(build_vr_gameplay_prompt(in).kind == VrGameplayPromptKind::none,
        "finished reload clears hint instead of covering aiming");
    in.enemy_grenade_available=true;
    in.support_weapon_selected=true;
    in.help_held=true;
    in.introduction_visible=true;
    for (const bool auto_reload : {false,true})
        for (const bool button_melee : {false,true})
            for (const bool multiplayer : {false,true}) {
                in.automatic_reload=auto_reload;
                in.right_stick_melee=button_melee;
                in.multiplayer=multiplayer;
                bounded(build_vr_gameplay_prompt(in));
            }
    for (const auto left : {Profile::generic,Profile::touch,Profile::index,
                           static_cast<Profile>(255)})
        for (const auto right : {Profile::generic,Profile::touch,Profile::index,
                                static_cast<Profile>(255)}) {
            in={.active_stereo_gameplay=true,.input_focused=true,
                .introduction_visible=true,.left_binding_profile=left,
                .right_binding_profile=right};
            auto controls=build_vr_gameplay_prompt(in);
            check(contains(controls.first,left==Profile::touch ? "LEFT X: USE"
                : left==Profile::index ? "LEFT A: USE" : "L-PRIMARY: USE"),
                "per-hand controller family determines only its own use label");
            check(contains(controls.first,right==Profile::touch || right==Profile::index
                ? "RIGHT A: RELOAD" : "R-PRIMARY: RELOAD"),
                "mixed/unknown hand profiles cannot leak the other hand alias");
            bounded(controls);
            in.help_held=true;
            auto radio=build_vr_gameplay_prompt(in);
            check(contains(radio.second,left==Profile::touch ? "HOLD LEFT Y"
                : left==Profile::index ? "HOLD LEFT B" : "HOLD L-SECONDARY"),
                "campaign radio hint follows actual left controller profile");
            check(contains(radio.first,left==Profile::touch ? "LEFT X: USE"
                : left==Profile::index ? "LEFT A: USE" : "L-PRIMARY: USE"),
                "campaign held-help use label follows the active left controller profile");
            bounded(radio);
            in.help_held=false;
            in.reload_stage=VrReloadPromptStage::begin;
            bounded(build_vr_gameplay_prompt(in));
        }
    return 0;
}
