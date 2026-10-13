#pragma once

#include <array>
#include <cstdint>

namespace wawvr::mod {

inline constexpr std::int32_t kPhysicalScopePanelPixels = 1024;
inline constexpr std::int32_t kNativeStereoRegionWidth = 4992;
inline constexpr std::int32_t kPhysicalScopePackedWidth =
    kNativeStereoRegionWidth + kPhysicalScopePanelPixels;

// Shared by the HUD producer and scene consumer. The optical source occupies
// a separate strip, not part of either normal eye's HUD coordinate system.
[[nodiscard]] constexpr std::int32_t physical_scope_reserved_width(
    bool active, std::int32_t width, std::int32_t height) noexcept {
    return active && width >= kPhysicalScopePackedWidth &&
            height >= kPhysicalScopePanelPixels
        ? kPhysicalScopePanelPixels : 0;
}

// Scope rendering keeps the established scope,left,right order. Native HUD
// commands belong to both ordinary eyes, never to the magnified optical feed.
[[nodiscard]] constexpr std::array<bool, 3> stereo_hud_command_views(
    bool scope_active) noexcept {
    return scope_active ? std::array<bool, 3>{false, true, true}
                        : std::array<bool, 3>{true, true, false};
}

} // namespace wawvr::mod
