// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>

namespace wawvr::mod {
// Exact Steam7329 mapped text30: the formatter in CG_GetUseString has three
// callers inside CG_DrawCursorhint. No generic localization caller qualifies.
struct CursorHintDisplayWitness final {
    std::uint32_t call_return{},parent_return{},context{};
    std::int32_t policy{},local_client{};
    bool stack_readable{},message_readable{};
};
[[nodiscard]] constexpr std::uint32_t cursor_hint_display_rejection_mask(
    const CursorHintDisplayWitness& value) noexcept {
    const bool parent=value.parent_return==0x00451562U ||
        value.parent_return==0x004515AEU || value.parent_return==0x00451952U;
    return (value.stack_readable?0U:1U)|
        (value.call_return==0x004512E9U?0U:2U)|(parent?0U:4U)|
        (value.context==0x0084F274U?0U:8U)|(value.policy==0?0U:0x10U)|
        (value.message_readable?0U:0x20U)|(value.local_client==0?0U:0x40U);
}
} // namespace wawvr::mod
