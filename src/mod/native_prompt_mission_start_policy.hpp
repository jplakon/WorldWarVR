// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "../xr/xr_types.h"

#include <array>
#include <cstdint>
#include <limits>
#include <string_view>

namespace wawvr::mod {
inline constexpr std::string_view kMissionStartKey="MENU_CLICK_TO_START_THE_MISSION";
inline constexpr std::string_view kMissionStartInlineSource="@MENU_CLICK_TO_START_THE_MISSION";
inline constexpr std::string_view kMissionStartNativeEnglish="Click to Start the Mission";

enum class MissionStartSettingSource : std::uint8_t { default_on,explicit_on,explicit_off,invalid };
struct MissionStartSetting final {
    bool enabled{};
    MissionStartSettingSource source{MissionStartSettingSource::invalid};
};
// Only a positively established ABSENT variable enables the reviewed default.
// An empty, malformed, overlong or failed read is explicit uncertainty: OFF.
[[nodiscard]] constexpr MissionStartSetting resolve_mission_start_setting(
    const bool present,const std::wstring_view value) noexcept {
    if(!present)return {true,MissionStartSettingSource::default_on};
    if(value==L"1")return {true,MissionStartSettingSource::explicit_on};
    if(value==L"0")return {false,MissionStartSettingSource::explicit_off};
    return {false,MissionStartSettingSource::invalid};
}
[[nodiscard]] constexpr const char* mission_start_setting_source_name(const MissionStartSettingSource source) noexcept {
    switch(source){
    case MissionStartSettingSource::default_on:return "default-on";
    case MissionStartSettingSource::explicit_on:return "explicit-on";
    case MissionStartSettingSource::explicit_off:return "explicit-off";
    default:return "invalid";
    }
}
[[nodiscard]] constexpr bool mission_start_key_bridge_requested(const bool diagnostics,
    const bool loading_tips,const MissionStartSetting setting) noexcept {
    return diagnostics || loading_tips || setting.enabled;
}

// Separate from the four loading-tip policy. The real right-primary adapter
// was observed dispatching SP Mouse1 at CA6/catchers16; CA7 is the same owned
// pregame caption route. No gameplay/menu-input state is changed here.
struct MissionStartFramePolicy final {
    bool enabled{},presentation_valid{},frame_current{},views_valid{},focused{},primary_active{};
    std::int32_t connection{};
    std::uint32_t key_catchers{};
    wawvr::xr::ControllerBindingProfile right_profile{};
};
enum MissionStartFrameRejection : std::uint32_t {
    mission_start_disabled=1U,mission_start_presentation=2U,
    mission_start_stale=4U,mission_start_views=8U,mission_start_unfocused=16U,
    mission_start_primary_inactive=32U,mission_start_connection=64U,
    mission_start_catchers=128U,mission_start_profile=256U,
};
inline bool mission_start_profile_supported(const wawvr::xr::ControllerBindingProfile profile) noexcept {
    using wawvr::xr::ControllerBindingProfile;
    return profile==ControllerBindingProfile::generic || profile==ControllerBindingProfile::touch ||
        profile==ControllerBindingProfile::index;
}
inline std::uint32_t mission_start_frame_rejections(const MissionStartFramePolicy& p) noexcept {
    std::uint32_t result{};
    if(!p.enabled)result|=mission_start_disabled;
    if(!p.presentation_valid)result|=mission_start_presentation;
    if(!p.frame_current)result|=mission_start_stale;
    if(!p.views_valid)result|=mission_start_views;
    if(!p.focused)result|=mission_start_unfocused;
    if(!p.primary_active)result|=mission_start_primary_inactive;
    if(p.connection!=6 && p.connection!=7)result|=mission_start_connection;
    if(p.key_catchers!=16)result|=mission_start_catchers;
    if(!mission_start_profile_supported(p.right_profile))result|=mission_start_profile;
    return result;
}

// Owned sparse copies of the exact native inline-source route. No native
// pointer/result cache, unbounded string, dvar lookup or subtitle adaptation.
struct MissionStartItemSnapshot final {
    std::uint32_t type{},flags{},text{},dvar{},expression_count{},local_client{};
    std::array<char,64> source{},key{};
    bool item_read{},context_read{},source_read{},key_read{};
    bool operator==(const MissionStartItemSnapshot&) const = default;
};
inline std::string_view mission_start_bounded_text(const std::array<char,64>& text) noexcept {
    for(std::size_t index=0;index<text.size();++index)
        if(text[index]==0)return {text.data(),index};
    return {};
}
inline bool mission_start_item_readable(const MissionStartItemSnapshot& s) noexcept {
    return s.item_read && s.context_read && s.source_read && s.key_read;
}
inline bool mission_start_inline_pointer(const std::uint32_t text,const std::uintptr_t key) noexcept {
    return text!=0 && text<(std::numeric_limits<std::uint32_t>::max)() &&
        key==static_cast<std::uintptr_t>(text)+1U;
}
enum MissionStartItemRejection : std::uint32_t {
    mission_start_item_unreadable=1U,mission_start_item_unstable=2U,
    mission_start_wrong_call=4U,mission_start_wrong_parent=8U,
    mission_start_wrong_type=16U,mission_start_nonplain=32U,
    mission_start_expression=64U,mission_start_other_client=128U,
    mission_start_other_source=256U,mission_start_other_key=512U,
    mission_start_noninline_pointer=1024U,
};
inline std::uint32_t mission_start_item_rejections(
    const MissionStartItemSnapshot& first,const MissionStartItemSnapshot& second,
    const std::uintptr_t key,const std::uint32_t call_return,
    const std::uint32_t expected_call_return,const std::uint32_t parent_return,
    const std::string_view observed_key) noexcept {
    std::uint32_t result{};
    if(!mission_start_item_readable(first) || !mission_start_item_readable(second))
        result|=mission_start_item_unreadable;
    if(!(first==second))result|=mission_start_item_unstable;
    if(expected_call_return==0 || call_return!=expected_call_return)result|=mission_start_wrong_call;
    if(parent_return!=0x005C98DBU)result|=mission_start_wrong_parent;
    if(first.type>1)result|=mission_start_wrong_type;
    if((first.flags&3U)!=0)result|=mission_start_nonplain;
    if(first.expression_count!=0)result|=mission_start_expression;
    if(first.local_client!=0)result|=mission_start_other_client;
    if(mission_start_bounded_text(first.source)!=kMissionStartInlineSource)result|=mission_start_other_source;
    if(observed_key!=kMissionStartKey || mission_start_bounded_text(first.key)!=kMissionStartKey)
        result|=mission_start_other_key;
    if(!mission_start_inline_pointer(first.text,key))result|=mission_start_noninline_pointer;
    return result;
}

// Immutable complete display strings only; no sentence/token replacement.
// An unknown language/custom override retains the exact native return pointer.
inline std::string_view resolve_vr_mission_start_text(const std::string_view key,
    const std::string_view native_english,const wawvr::xr::ControllerBindingProfile profile) noexcept {
    using wawvr::xr::ControllerBindingProfile;
    if(key!=kMissionStartKey || native_english!=kMissionStartNativeEnglish)return {};
    switch(profile){
    case ControllerBindingProfile::generic:return "Press R-PRIMARY to Start the Mission";
    case ControllerBindingProfile::touch:
    case ControllerBindingProfile::index:return "Press RIGHT A to Start the Mission";
    default:return {};
    }
}
} // namespace wawvr::mod
