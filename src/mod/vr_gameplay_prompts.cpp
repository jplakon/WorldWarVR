// SPDX-License-Identifier: GPL-3.0-only
#include "vr_gameplay_prompts.hpp"

#include <algorithm>
#include <string_view>

namespace wawvr::mod {
namespace {
using wawvr::xr::ControllerBindingProfile;

constexpr std::string_view kCampaignHelpManualPhysical =
    "L-PRIMARY: USE | R-PRIMARY: MANUAL RELOAD | LEFT STICK CLICK: SPRINT | MELEE: DELIBERATE ATTACK";
constexpr std::string_view kCampaignHelpManualButton =
    "L-PRIMARY: USE | R-PRIMARY: MANUAL RELOAD | LEFT STICK CLICK: SPRINT | MELEE: RIGHT STICK CLICK";
constexpr std::string_view kCampaignHelpAutoPhysical =
    "L-PRIMARY: USE | R-PRIMARY: AUTO RELOAD | LEFT STICK CLICK: SPRINT | MELEE: DELIBERATE ATTACK";
constexpr std::string_view kCampaignHelpAutoButton =
    "L-PRIMARY: USE | R-PRIMARY: AUTO RELOAD | LEFT STICK CLICK: SPRINT | MELEE: RIGHT STICK CLICK";
constexpr std::string_view kCampaignHelpHipGrenade =
    "LEFT TRIGGER AT LEFT HIP: GRENADE | HOLD L-SECONDARY + LEFT STICK LEFT: RADIO; THEN RELEASE";
constexpr std::string_view kCampaignHelpButtonGrenade =
    "LEFT TRIGGER: GRENADE | HOLD L-SECONDARY + LEFT STICK LEFT: RADIO; THEN RELEASE";
static_assert(kCampaignHelpManualPhysical.size() < kVrPromptLineBytes &&
    kCampaignHelpManualButton.size() < kVrPromptLineBytes &&
    kCampaignHelpAutoPhysical.size() < kVrPromptLineBytes &&
    kCampaignHelpAutoButton.size() < kVrPromptLineBytes &&
    kCampaignHelpHipGrenade.size() < kVrPromptLineBytes &&
    kCampaignHelpButtonGrenade.size() < kVrPromptLineBytes);

constexpr std::array kUseHintKeys{
    std::string_view{"MAK_TRUCK_TRIGGER"},
    std::string_view{"SCRIPT_PLATFORM_HINT_PLANTEXPLOSIVES"},
    std::string_view{"ZOMBIE_BUTTON_BUY_OPEN_DOOR_100"},
    std::string_view{"ZOMBIE_BUTTON_BUY_OPEN_DOOR_200"},
    std::string_view{"ZOMBIE_BUTTON_BUY_OPEN_DOOR_250"},
    std::string_view{"ZOMBIE_BUTTON_BUY_OPEN_DOOR_500"},
    std::string_view{"ZOMBIE_BUTTON_BUY_OPEN_DOOR_750"},
    std::string_view{"ZOMBIE_BUTTON_BUY_OPEN_DOOR_1000"},
    std::string_view{"ZOMBIE_BUTTON_BUY_OPEN_DOOR_1250"},
    std::string_view{"ZOMBIE_BUTTON_BUY_OPEN_DOOR_1500"},
    std::string_view{"ZOMBIE_BUTTON_BUY_OPEN_DOOR_1750"},
    std::string_view{"ZOMBIE_BUTTON_BUY_OPEN_DOOR_2000"},
};
constexpr std::array kUseHintLabels{
    std::string_view{"L-PRIMARY"},std::string_view{"LEFT X"},std::string_view{"LEFT A"}};
consteval auto make_use_hint_copies() {
    std::array<std::array<char,64>,kUseHintKeys.size()*2*kUseHintLabels.size()> copies{};
    for (std::size_t key=0;key<kUseHintKeys.size();++key)
        for (std::size_t prefix=0;prefix<2;++prefix)
            for (std::size_t profile=0;profile<kUseHintLabels.size();++profile) {
                auto& copy=copies[(key*2+prefix)*kUseHintLabels.size()+profile];
                std::size_t cursor=0;
                if (prefix!=0) copy[cursor++]=char(0x14);
                for (const char value : kUseHintKeys[key]) copy[cursor++]=value;
                copy[cursor++]=char(0x15);
                for (const char value : kUseHintLabels[profile]) copy[cursor++]=value;
            }
    return copies;
}
// Immutable storage avoids a mutable TLS/ring-buffer lifetime across the
// native formatter. It owns final localized output exactly as before.
constexpr auto kUseHintCopies=make_use_hint_copies();

std::string_view left_primary(const ControllerBindingProfile profile) noexcept {
    if (profile == ControllerBindingProfile::touch) return "LEFT X";
    if (profile == ControllerBindingProfile::index) return "LEFT A";
    return "L-PRIMARY";
}

std::string_view left_secondary(const ControllerBindingProfile profile) noexcept {
    if (profile == ControllerBindingProfile::touch) return "LEFT Y";
    if (profile == ControllerBindingProfile::index) return "LEFT B";
    return "L-SECONDARY";
}

std::string_view right_primary(const ControllerBindingProfile profile) noexcept {
    if (profile == ControllerBindingProfile::touch ||
        profile == ControllerBindingProfile::index) return "RIGHT A";
    return "R-PRIMARY";
}

void text(std::array<char, kVrPromptLineBytes>& out,
          const std::string_view in) noexcept {
    const auto count = (std::min)(in.size(), out.size() - 1);
    std::copy_n(in.data(), count, out.data());
    out[count] = '\0';
}

VrGameplayPrompt prompt(const VrGameplayPromptKind kind,
                        const std::string_view first,
                        const std::string_view second) noexcept {
    VrGameplayPrompt out{.kind = kind};
    text(out.first, first);
    text(out.second, second);
    return out;
}

// Re-label only our own bounded, generated action tokens. Never accepts a
// native/localized sentence, and never performs global text replacement.
void label_generated_line(std::array<char, kVrPromptLineBytes>& line,
    const ControllerBindingProfile left,
    const ControllerBindingProfile right) noexcept {
    const auto source = line;
    line = {};
    const std::string_view in(source.data());
    std::size_t cursor = 0, output = 0;
    while (cursor < in.size() && output < line.size()-1) {
        std::string_view replacement;
        std::size_t consumed = 0;
        for (const auto token : {std::string_view{"L-PRIMARY"},
                 std::string_view{"R-PRIMARY"},std::string_view{"L-SECONDARY"}}) {
            if (in.substr(cursor).starts_with(token)) {
                replacement = token == "L-PRIMARY" ? left_primary(left)
                    : token == "R-PRIMARY" ? right_primary(right)
                    : left_secondary(left);
                consumed = token.size();
                break;
            }
        }
        if (consumed == 0) { line[output++] = in[cursor++]; continue; }
        const auto count = (std::min)(replacement.size(),line.size()-1-output);
        std::copy_n(replacement.data(),count,line.data()+output);
        output += count;
        cursor += consumed;
    }
}
} // namespace

std::string_view resolve_vr_native_binding_prompt(
    const std::string_view command,
    const VrNativeBindingPromptSettings& settings) noexcept {
    if (command == "+activate" || command == "+usereload")
        return left_primary(settings.left_binding_profile);
    if (command == "+reload") {
        if (settings.right_binding_profile == ControllerBindingProfile::touch ||
            settings.right_binding_profile == ControllerBindingProfile::index)
            return settings.automatic_reload ? "RIGHT A (AUTO RELOAD)"
                                             : "RIGHT A (MANUAL RELOAD)";
        return settings.automatic_reload ? "R-PRIMARY (AUTO RELOAD)"
                                         : "R-PRIMARY (MANUAL RELOAD)";
    }
    if (command == "+sprint") return "LEFT STICK CLICK";
    if (command == "+melee")
        return settings.right_stick_melee ? "RIGHT STICK CLICK"
                                          : "DELIBERATE HAND ATTACK";
    if (command == "+frag")
        return settings.button_grenades ? "LEFT TRIGGER"
                                        : "LEFT TRIGGER AT LEFT HIP";
    if (command == "+actionslot 4") {
        // Native tutorial sentences already supply an imperative ("Press").
        // Keep the hold requirement in the binding label without "Press HOLD".
        if (settings.left_binding_profile == ControllerBindingProfile::touch)
            return "LEFT Y (HELD) + LEFT STICK LEFT";
        if (settings.left_binding_profile == ControllerBindingProfile::index)
            return "LEFT B (HELD) + LEFT STICK LEFT";
        return "L-SECONDARY (HELD) + LEFT STICK LEFT";
    }
    // This resolver is called only by the guarded active-SP-stereo display
    // adapter. The right trigger is the fire action before and after support
    // selection; the Little Resistance tutorial displays both instructions
    // before the radio is equipped. Requiring it to be selected leaves that
    // first sentence advertising the desktop mouse binding instead.
    if (command == "+attack")
        return "RIGHT TRIGGER";
    // In particular +melee_breath/+breath_sprint also mean hold breath, so
    // mapping their names to the knife/sprint control would be misleading.
    return {};
}

std::uint32_t observed_vr_localized_hint_bit(
    const std::string_view exact_key) noexcept {
    constexpr std::array keys{
        std::string_view{"MAK_TRUCK_TRIGGER"},
        std::string_view{"MAK_TRUCK_HINT"},
        std::string_view{"SCRIPT_PLATFORM_HINT_PLANTEXPLOSIVES"},
        std::string_view{"SCRIPT_PLATFORM_HINT_RELOAD"},
        std::string_view{"SCRIPT_PLATFORM_HINT_MELEEATTACK"},
        std::string_view{"PEL1_ROCKET_HOWTO"},
        std::string_view{"PLATFORM_DYK_MSG33"},
        std::string_view{"PLATFORM_DYK_MSG34"},
        std::string_view{"PLATFORM_DYK_MSG35"},
        std::string_view{"PLATFORM_DYK_MSG36"},
        std::string_view{"PLATFORM_THROWBACKGRENADE"},
        // Exact keys/values read from unchanged English Nacht fastfile
        // B9CEDBAB...49F4FE84. No prefix-based zombie string matching.
        std::string_view{"ZOMBIE_BUTTON_BUY_OPEN_DOOR_100"},
        std::string_view{"ZOMBIE_BUTTON_BUY_OPEN_DOOR_200"},
        std::string_view{"ZOMBIE_BUTTON_BUY_OPEN_DOOR_250"},
        std::string_view{"ZOMBIE_BUTTON_BUY_OPEN_DOOR_500"},
        std::string_view{"ZOMBIE_BUTTON_BUY_OPEN_DOOR_750"},
        std::string_view{"ZOMBIE_BUTTON_BUY_OPEN_DOOR_1000"},
        std::string_view{"ZOMBIE_BUTTON_BUY_OPEN_DOOR_1250"},
        std::string_view{"ZOMBIE_BUTTON_BUY_OPEN_DOOR_1500"},
        std::string_view{"ZOMBIE_BUTTON_BUY_OPEN_DOOR_1750"},
        std::string_view{"ZOMBIE_BUTTON_BUY_OPEN_DOOR_2000"},
        std::string_view{"ZOMBIE_WEAPONCOSTAMMO"},
    };
    for (std::size_t index=0;index<keys.size();++index)
        if (exact_key==keys[index]) return 1U<<index;
    return 0;
}

std::uint64_t observed_vr_coop_loading_hint_bit(
    const std::string_view exact_key) noexcept {
    // Exact references read from unchanged English code_post_gfx.ff
    // A56AC336...439D3, table maps/didyouknow_coop.csv. No numeric-prefix
    // parsing: malformed/extra/zero-padded variants remain unknown.
    constexpr std::array keys{
        std::string_view{"PLATFORM_DYK_COOP_MSG1"},
        std::string_view{"PLATFORM_DYK_COOP_MSG2"},
        std::string_view{"PLATFORM_DYK_COOP_MSG3"},
        std::string_view{"PLATFORM_DYK_COOP_MSG4"},
        std::string_view{"PLATFORM_DYK_COOP_MSG5"},
        std::string_view{"PLATFORM_DYK_COOP_MSG6"},
        std::string_view{"PLATFORM_DYK_COOP_MSG7"},
        std::string_view{"PLATFORM_DYK_COOP_MSG8"},
        std::string_view{"PLATFORM_DYK_COOP_MSG9"},
        std::string_view{"PLATFORM_DYK_COOP_MSG10"},
        std::string_view{"PLATFORM_DYK_COOP_MSG11"},
        std::string_view{"PLATFORM_DYK_COOP_MSG12"},
        std::string_view{"PLATFORM_DYK_COOP_MSG13"},
        std::string_view{"PLATFORM_DYK_COOP_MSG14"},
        std::string_view{"PLATFORM_DYK_COOP_MSG15"},
        std::string_view{"PLATFORM_DYK_COOP_MSG16"},
        std::string_view{"PLATFORM_DYK_COOP_MSG17"},
        std::string_view{"PLATFORM_DYK_COOP_MSG18"},
        std::string_view{"PLATFORM_DYK_COOP_MSG19"},
        std::string_view{"PLATFORM_DYK_COOP_MSG20"},
        std::string_view{"PLATFORM_DYK_COOP_MSG21"},
        std::string_view{"PLATFORM_DYK_COOP_MSG22"},
        std::string_view{"PLATFORM_DYK_COOP_MSG23"},
        std::string_view{"PLATFORM_DYK_COOP_MSG24"},
        std::string_view{"PLATFORM_DYK_COOP_MSG25"},
        std::string_view{"PLATFORM_DYK_COOP_MSG26"},
        std::string_view{"PLATFORM_DYK_COOP_MSG27"},
        std::string_view{"PLATFORM_DYK_COOP_MSG28"},
        std::string_view{"PLATFORM_DYK_COOP_MSG29"},
        std::string_view{"PLATFORM_DYK_COOP_MSG30"},
        std::string_view{"PLATFORM_DYK_COOP_MSG31"},
        std::string_view{"PLATFORM_DYK_COOP_MSG32"},
        std::string_view{"PLATFORM_DYK_COOP_MSG33"},
    };
    static_assert(keys.size()==33 && keys.size()<=64);
    for (std::size_t index=0;index<keys.size();++index)
        if (exact_key==keys[index]) return std::uint64_t{1}<<index;
    return 0;
}

VrEncodedPromptObservation observe_vr_encoded_prompt(
    const std::string_view encoded) noexcept {
    VrEncodedPromptObservation out{};
    if (encoded.size()>=1024) return out;
    bool localized=true,marker16=false;
    std::size_t start=0;
    const auto append=[&](const std::size_t end) noexcept {
        if (end==start) return true;
        if (out.segment_count>=out.segments.size()) return false;
        const auto value=encoded.substr(start,end-start);
        auto& segment=out.segments[out.segment_count++];
        segment.offset=static_cast<std::uint16_t>(start);
        segment.bytes=static_cast<std::uint16_t>(value.size());
        segment.localized_key=localized;
        segment.conversion_marker16=marker16;
        segment.hash=2166136261U;
        for (const unsigned char byte : value)
            segment.hash=(segment.hash^byte)*16777619U;
        if (localized) {
            ++out.localized_segments;
            segment.known_key_bit=observed_vr_localized_hint_bit(value);
            out.known_key_mask|=segment.known_key_bit;
        } else ++out.literal_arguments;
        return true;
    };
    for (std::size_t index=0;index<encoded.size();++index) {
        const auto byte=static_cast<unsigned char>(encoded[index]);
        if (byte==0x14 || byte==0x15 || byte==0x16) {
            if (!append(index)) return {};
            if (byte==0x14) localized=true;
            else if (byte==0x15) localized=false;
            // Native marker16 changes conversion scanning, not key/literal
            // status;14/15 reset it for the following segment.
            marker16=byte==0x16;
            start=index+1;
        } else if (byte<0x20 || byte==0x7F) return {};
    }
    if (!append(encoded.size())) return {};
    out.valid=true;
    out.exact_single_target_key=out.segment_count!=0 &&
        out.localized_segments==1 && out.segments[0].localized_key &&
        out.segments[0].known_key_bit!=0;
    return out;
}

std::string_view resolve_vr_encoded_use_hint_copy(
    const std::string_view encoded,const VrNativeBindingPromptSettings& settings) noexcept {
    std::size_t profile=0;
    switch (settings.left_binding_profile) {
    case ControllerBindingProfile::generic: break;
    case ControllerBindingProfile::touch: profile=1; break;
    case ControllerBindingProfile::index: profile=2; break;
    default: return {};
    }
    const auto observation=observe_vr_encoded_prompt(encoded);
    if (!observation.valid || !observation.exact_single_target_key ||
        observation.segment_count!=2 || observation.literal_arguments!=1) return {};
    const auto& first=observation.segments[0];
    const auto& argument=observation.segments[1];
    if (first.conversion_marker16 || argument.conversion_marker16 ||
        first.offset>1 || (first.offset==1 && encoded[0]!=char(0x14)) ||
        argument.offset!=first.offset+first.bytes+1 ||
        encoded[argument.offset-1]!=char(0x15) || argument.localized_key ||
        static_cast<std::size_t>(argument.offset)+argument.bytes!=encoded.size() ||
        encoded.substr(argument.offset,argument.bytes)!="F") return {};
    const auto key=encoded.substr(first.offset,first.bytes);
    for (std::size_t index=0;index<kUseHintKeys.size();++index)
        if (key==kUseHintKeys[index]) {
            const auto& copy=kUseHintCopies[(index*2+first.offset)*kUseHintLabels.size()+profile];
            return {copy.data()};
        }
    return {};
}

static VrGameplayPrompt build_semantic_gameplay_prompt(
    const VrGameplayPromptInput& in) noexcept {
    if (!in.active_stereo_gameplay || !in.input_focused) return {};
    if (in.enemy_grenade_available) {
        return prompt(VrGameplayPromptKind::enemy_grenade,
            "ENEMY GRENADE: LEFT TRIGGER TO GRAB; RELEASE TO THROW",
            "FUSE KEEPS RUNNING - THROW IMMEDIATELY");
    }
    if (!in.multiplayer && in.support_weapon_selected) {
        return prompt(VrGameplayPromptKind::support_aim,
            "SUPPORT/RADIO: HOLD RIGHT GRIP; AIM WITH RIGHT HAND",
            "RIGHT TRIGGER: MARK TARGET / CALL STRIKE");
    }
    if (!in.multiplayer && in.help_held) {
        // Retrievable campaign controls, not just the radio selector. These
        // are our own bounded lines; native/localized text remains untouched.
        return prompt(VrGameplayPromptKind::support_selector,
            in.automatic_reload
                ? (in.right_stick_melee ? kCampaignHelpAutoButton : kCampaignHelpAutoPhysical)
                : (in.right_stick_melee ? kCampaignHelpManualButton : kCampaignHelpManualPhysical),
            in.button_grenades ? kCampaignHelpButtonGrenade : kCampaignHelpHipGrenade);
    }
    if (in.reload_stage != VrReloadPromptStage::none &&
        in.reload_stage != VrReloadPromptStage::ready) {
        if (in.automatic_reload) {
            return prompt(VrGameplayPromptKind::reload,
                "R-PRIMARY: COMPLETE AUTOMATIC RELOAD",
                "PHYSICAL AMMO AND ACTION STEPS ARE DISABLED");
        }
        switch (in.reload_stage) {
        case VrReloadPromptStage::ammunition:
            return prompt(VrGameplayPromptKind::reload,
                "RELEASE SUPPORT GRIP; FREE-HAND GRIP AT HIP: TAKE AMMO",
                "KEEP THE OTHER HAND GRIPPING THE WEAPON");
        case VrReloadPromptStage::insert:
            return prompt(VrGameplayPromptKind::reload,
                in.reload_family == VrReloadPromptFamily::stripper_clip
                    ? "BRING CLIP TO OPEN RECEIVER; PRESS IT DOWN"
                    : "BRING AMMO TO RECEIVER / MAGAZINE WELL; RELEASE GRIP",
                "ONLY ONE AMMO TRANSFER PER INSERTION");
        case VrReloadPromptStage::chamber:
            return prompt(VrGameplayPromptKind::reload,
                "FREE-HAND TRIGGER AT BOLT / SLIDE: PULL AND RELEASE",
                "CLOSE THE ACTION; RE-GRIP SUPPORT HAND TO FIRE");
        case VrReloadPromptStage::begin:
            return prompt(VrGameplayPromptKind::reload,
                in.reload_family == VrReloadPromptFamily::stripper_clip
                    ? "FREE-HAND TRIGGER AT BOLT: OPEN THE ACTION"
                    : "R-PRIMARY: EJECT / BEGIN RELOAD; RELEASE SUPPORT GRIP",
                "FREE-HAND GRIP AT HIP: TAKE REPLACEMENT AMMO");
        default: break;
        }
    }
    if (in.introduction_visible || in.help_held) {
        return prompt(in.help_held ? VrGameplayPromptKind::controls
                                  : VrGameplayPromptKind::introduction,
            "L-PRIMARY: USE | R-PRIMARY: RELOAD | LEFT STICK CLICK: SPRINT",
            in.right_stick_melee
                ? "RIGHT STICK CLICK: MELEE | HOLD L-SECONDARY: VR HELP"
                : "MELEE: DELIBERATE HAND ATTACK | HOLD L-SECONDARY: VR HELP");
    }
    return {};
}

VrGameplayPrompt build_vr_gameplay_prompt(
    const VrGameplayPromptInput& in) noexcept {
    auto result = build_semantic_gameplay_prompt(in);
    label_generated_line(result.first,in.left_binding_profile,in.right_binding_profile);
    label_generated_line(result.second,in.left_binding_profile,in.right_binding_profile);
    return result;
}
} // namespace wawvr::mod
