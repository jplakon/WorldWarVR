#pragma once

#include <cstdint>
#include <cstddef>

namespace wawvr::mod {

inline constexpr std::int32_t kHudActiveConnectionState = 10;
inline constexpr std::int32_t kHudMaximumViewportDimension = 16'384;
// The gameplay HUD command list has zero binocular disparity, so its apparent
// closeness is governed by angular size rather than a separate depth plane.
// Keep the status HUD compact enough to read as a distant overlay while the
// high-resolution source preserves text clarity. Edge-aligned ammo, map,
// points, and round widgets are constrained to the central binocular field so
// reading them never requires an uncomfortable glance into the lens edges.
inline constexpr float kHudComfortScale = 0.38F;
inline constexpr float kHudSafeAreaHorizontalFraction = 0.46F;
inline constexpr float kHudSafeAreaVerticalFraction = 0.60F;

// Exact 18-float T4 ScreenPlacement. The final subScreenTop field is present
// in WaW even though it is absent from the older IW3-family comparison
// structure; exact setup and ApplyRect disassembly independently establish it.
struct HudScreenPlacement final {
    float scale_virtual_to_real[2]{};
    float scale_virtual_to_full[2]{};
    float scale_real_to_virtual[2]{};
    float virtual_viewable_min[2]{};
    float virtual_viewable_max[2]{};
    float real_viewport_size[2]{};
    float real_viewable_min[2]{};
    float real_viewable_max[2]{};
    float sub_screen_left{};
    float sub_screen_top{};
};

static_assert(sizeof(HudScreenPlacement) == 0x48);

enum class HudPlacementMode : std::uint8_t {
    full_frame,
    packed_eye,
};

struct HudPlacementInput final {
    bool presentation_state_valid{};
    bool stereo_packing_ready{};
    std::int32_t connection_state{};
    std::int32_t active_connection_state{kHudActiveConnectionState};
    std::uint32_t key_catchers{};
    std::int32_t full_width{};
    std::int32_t full_height{};
    // Validated same-frame optional optical strip; zero for ordinary stereo.
    std::int32_t scope_reserved_width{};
};

struct HudPlacementPlan final {
    bool valid{};
    HudPlacementMode mode{HudPlacementMode::full_frame};
    std::int32_t width{};
    std::int32_t height{};
};

// The native gameplay HUD is queued once and replayed inside two half-width
// backend viewports. Only unpaused CA_ACTIVE gameplay may therefore use a
// half-width ScreenPlacement, and only while the exact scene/backend hooks have
// a valid XR publication for this Com_Frame. Every unavailable-XR,
// menu/catcher, or non-active state restores the complete backbuffer placement.
[[nodiscard]] HudPlacementPlan plan_hud_placement(
    const HudPlacementInput& input) noexcept;

// Shrinks ordinary gameplay HUD elements around their alignment anchors and
// pulls edge-aligned elements into a binocular-safe rectangle. Full-screen
// fades/damage overlays use scale_virtual_to_full, which is intentionally
// preserved. Menus/cinematics never call this function.
[[nodiscard]] bool apply_packed_hud_comfort(
    HudScreenPlacement* placement,
    std::int32_t eye_width,
    std::int32_t eye_height) noexcept;

}  // namespace wawvr::mod
