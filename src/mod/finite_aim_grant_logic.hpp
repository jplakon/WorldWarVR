// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "all_weapon_aim_test_catalogue.hpp"
#include <cstdint>
#include <string_view>

namespace wawvr::mod {
// Test-only policy. The caller additionally owns validated T4 addresses and
// performs fresh native reads; none of these pure helpers accesses the game.
[[nodiscard]] inline bool finite_aim_local_command_path_valid(std::wstring_view path) noexcept {
    return path.size()>=4&&path.size()<1024&&
        ((path[0]>=L'A'&&path[0]<=L'Z')||(path[0]>=L'a'&&path[0]<=L'z'))&&
        path[1]==L':'&&(path[2]==L'\\'||path[2]==L'/')&&
        path.find(L'\0')==std::wstring_view::npos&&path.find_first_of(L"\r\n") ==std::wstring_view::npos&&
        path.substr(2).find(L':')==std::wstring_view::npos;
}
struct FiniteAimGrantGate final {
    std::uint16_t catalogue_index{kInvalidAllWeaponAimTestIndex};
    bool single_player{},command_path_bound{},actual_simulator_loaded{},post_com_frame{},presentation_valid{},bindings_ready{};
    std::uint32_t key_catchers{};
    std::int32_t connection_state{};
};
[[nodiscard]] inline bool finite_aim_grant_allowed(const FiniteAimGrantGate& gate) noexcept {
    return gate.catalogue_index<kAllWeaponAimTestDefinitions.size()&&gate.single_player&&
        gate.command_path_bound&&gate.actual_simulator_loaded&&gate.post_com_frame&&
        gate.presentation_valid&&gate.bindings_ready&&gate.key_catchers==0&&gate.connection_state==10;
}
struct FiniteAimRegisteredIdentity final {
    std::string_view weapon{};
    std::uint32_t index{},last_index{},definition{},model{};
    std::int32_t ammo_index{-1},clip_index{-1},clip_size{};
};
[[nodiscard]] inline bool finite_aim_registered_identity_valid(std::uint16_t request,
    const FiniteAimRegisteredIdentity& value) noexcept {
    return request<kAllWeaponAimTestDefinitions.size()&&value.weapon==kAllWeaponAimTestDefinitions[request].weapon&&
        value.index>0&&value.index<128&&value.last_index>=value.index&&value.last_index<128&&
        value.definition&&value.model&&value.ammo_index>=0&&value.ammo_index<128&&
        value.clip_index>=0&&value.clip_index<128&&value.clip_size>0&&value.clip_size<=1024;
}
[[nodiscard]] inline bool finite_aim_registered_identity_same(std::uint16_t request,
    const FiniteAimRegisteredIdentity& a,const FiniteAimRegisteredIdentity& b) noexcept {
    return finite_aim_registered_identity_valid(request,a)&&finite_aim_registered_identity_valid(request,b)&&
        a.weapon==b.weapon&&a.index==b.index&&a.last_index==b.last_index&&a.definition==b.definition&&a.model==b.model&&
        a.ammo_index==b.ammo_index&&a.clip_index==b.clip_index&&a.clip_size==b.clip_size;
}
struct FiniteAimAmmoPlan final {std::int32_t ammo_index{},clip_index{},reserve{},clip{};};
[[nodiscard]] inline bool finite_aim_ammo_plan(std::uint16_t request,
    const FiniteAimRegisteredIdentity& identity,FiniteAimAmmoPlan* output) noexcept {
    if(!output||!finite_aim_registered_identity_valid(request,identity))return false;
    *output={identity.ammo_index,identity.clip_index,identity.clip_size*8,identity.clip_size};return true;
}
enum class FiniteAimGrantResult : std::uint8_t {
    rejected,unavailable,target_not_registered,source_changed,give_failed,selection_failed,selection_submitted
};
[[nodiscard]] inline const char* finite_aim_grant_result_name(FiniteAimGrantResult result) noexcept {
    switch(result){
    case FiniteAimGrantResult::rejected:return "rejected";
    case FiniteAimGrantResult::unavailable:return "unavailable";
    case FiniteAimGrantResult::target_not_registered:return "target-not-registered";
    case FiniteAimGrantResult::source_changed:return "source-changed";
    case FiniteAimGrantResult::give_failed:return "give-failed";
    case FiniteAimGrantResult::selection_failed:return "selection-failed";
    case FiniteAimGrantResult::selection_submitted:return "selection-submitted";
    }
    return "unknown";
}
} // namespace wawvr::mod
