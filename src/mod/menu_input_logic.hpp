// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>

namespace wawvr::mod {

inline constexpr float kMenuCursorDeadzone = 0.20F;
inline constexpr float kMenuCursorPixelsPerSecond = 1100.0F;
inline constexpr std::uint64_t kMaximumMenuCursorDeltaMilliseconds = 50;
inline constexpr float kMenuTriggerPressThreshold = 0.55F;
inline constexpr float kMenuTriggerReleaseThreshold = 0.45F;

enum class NativeMenuConfirmKey : std::uint8_t {
    mouse1,
    enter,
};

// WaW MP can deliberately bypass Mouse1-down while an in-match UI catcher is
// active, leaving only an ineffective key-up. The frontend and SP do not use
// that route. UI_MouseEvent has already focused the pointed item before this
// selection is consumed, so Enter activates that exact team/class item.
[[nodiscard]] NativeMenuConfirmKey choose_native_menu_confirm_key(
    bool multiplayer_profile,
    bool active_gameplay_menu,
    bool ui_active) noexcept;

// Schmitt-trigger policy for analog Touch triggers. A single physical hold
// cannot become multiple native clicks when the value chatters near 0.55.
[[nodiscard]] bool update_menu_analog_trigger(
    bool action_active,
    float value,
    bool was_held) noexcept;

enum class MenuPointerTriggerSource : std::uint8_t {
    none,
    boolean_click,
    analog,
};

struct MenuPointerTriggerState final {
    MenuPointerTriggerSource source{MenuPointerTriggerSource::none};
    bool armed{};
    bool held{};
};

// Chooses the boolean click binding when available and otherwise uses the
// analog trigger. Focus loss, action inactivity, and source changes disarm the
// trigger until a physical release sample is observed; reconnecting while
// held can therefore never synthesize a menu click.
[[nodiscard]] bool update_menu_pointer_trigger(
    bool input_focused,
    bool click_action_active,
    bool click_current,
    bool analog_action_active,
    float analog_value,
    MenuPointerTriggerState* state) noexcept;

struct MenuCursorRegion final {
    std::uint32_t origin_x{};
    std::uint32_t width{};
    std::uint32_t height{};

    friend constexpr bool operator==(
        const MenuCursorRegion&, const MenuCursorRegion&) = default;
};

struct MenuNavigationFrame final {
    bool ui_active{};
    bool input_focused{};
    bool active_gameplay_menu{};
    bool stick_valid{};
    float stick_x{};
    float stick_y{};
    bool confirm_held{};
    bool pointer_valid{};
    float pointer_u{};
    float pointer_v{};
    bool pointer_confirm_held{};
    bool back_held{};
    std::uint64_t now_milliseconds{};
    MenuCursorRegion region{};
};

struct MenuNavigationState final {
    bool ui_active{};
    bool input_focused{};
    bool confirm_was_held{};
    bool pointer_confirm_was_held{};
    bool back_was_held{};
    float cursor_x{};
    float cursor_y{};
    std::uint64_t last_update_milliseconds{};
    MenuCursorRegion region{};
};

struct MenuNavigationUpdate final {
    bool cursor_position_valid{};
    bool cursor_moved{};
    bool pointer_position_used{};
    std::int32_t cursor_x{};
    std::int32_t cursor_y{};
    bool confirm_tap{};
    bool pointer_confirm_tap{};
    bool back_tap{};
};

// Pure controller-to-native-menu policy. Coordinates returned here are in the
// complete D3D9 backbuffer, including an active packed-eye source offset.
[[nodiscard]] MenuNavigationUpdate update_menu_navigation(
    const MenuNavigationFrame& frame,
    MenuNavigationState* state) noexcept;

}  // namespace wawvr::mod
