// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <string_view>

namespace wawvr::mod {
// Diagnostic facts only. No result pointer, translation or native state write.
struct LoadingItemSnapshot final {
    std::uint32_t type{}, flags{}, text{}, dvar{}, expression_count{}, local_client{};
    std::array<char,64> dvar_name{}, key{};
    bool item_read{}, context_read{}, dvar_read{}, key_read{};
    bool operator==(const LoadingItemSnapshot&) const = default;
};
inline std::string_view loading_item_bounded_text(const std::array<char,64>& text) noexcept {
    for (std::size_t index=0;index<text.size();++index)
        if (text[index]==0) return {text.data(),index};
    return {};
}
inline bool loading_item_key_is_exact(const std::string_view key) noexcept {
    return key=="PLATFORM_DYK_MSG33" || key=="PLATFORM_DYK_MSG34" ||
        key=="PLATFORM_DYK_MSG35" || key=="PLATFORM_DYK_MSG36";
}
inline bool loading_item_snapshot_readable(const LoadingItemSnapshot& s) noexcept {
    return s.item_read && s.context_read && s.dvar_read && s.key_read;
}
inline bool loading_item_key_on_dvar_stack(const std::uintptr_t key,
    const std::uintptr_t native_stack) noexcept {
    return native_stack!=0 && native_stack<=(std::numeric_limits<std::uintptr_t>::max)()-0x2D &&
        key==native_stack+0x2D;
}
enum LoadingItemObservationRejection : std::uint32_t {
    loading_item_unreadable=1U, loading_item_unstable=2U,
    loading_item_wrong_call=4U, loading_item_wrong_parent=8U,
    loading_item_wrong_type=16U, loading_item_nonplain_flags=32U,
    loading_item_explicit_text=64U, loading_item_expression=128U,
    loading_item_other_dvar=256U, loading_item_nonlocal_key=512U,
    loading_item_other_client=1024U, loading_item_other_key=2048U,
};
inline std::uint32_t loading_item_observation_rejections(
    const LoadingItemSnapshot& first,const LoadingItemSnapshot& second,
    const std::uintptr_t key,const std::uintptr_t native_stack,
    const std::uint32_t call_return,const std::uint32_t expected_call_return,
    const std::uint32_t parent_return,const std::string_view observed_key) noexcept {
    std::uint32_t result{};
    if (!loading_item_snapshot_readable(first) || !loading_item_snapshot_readable(second))
        result|=loading_item_unreadable;
    if (!(first==second)) result|=loading_item_unstable;
    if (call_return!=expected_call_return || expected_call_return==0) result|=loading_item_wrong_call;
    if (parent_return!=0x005C98DBU) result|=loading_item_wrong_parent;
    if (first.type>1) result|=loading_item_wrong_type;
    if ((first.flags&3U)!=0) result|=loading_item_nonplain_flags;
    if (first.text!=0) result|=loading_item_explicit_text;
    if (first.expression_count!=0) result|=loading_item_expression;
    if (first.dvar==0 || loading_item_bounded_text(first.dvar_name)!="didyouknow")
        result|=loading_item_other_dvar;
    if (!loading_item_key_on_dvar_stack(key,native_stack)) result|=loading_item_nonlocal_key;
    if (first.local_client!=0) result|=loading_item_other_client;
    if (!loading_item_key_is_exact(observed_key) ||
        loading_item_bounded_text(first.key)!=observed_key) result|=loading_item_other_key;
    return result;
}
} // namespace wawvr::mod
