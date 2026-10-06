// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>

namespace wawvr::mod {

inline constexpr std::uint64_t kControllerMenuHoldMilliseconds = 1'000;

struct MenuButtonGestureState final {
    bool holding{};
    bool long_hold_recognized{};
    bool recenter_captured{};
    bool waiting_for_valid_pose_reported{};
    std::uint64_t started_milliseconds{};
};

struct MenuButtonGestureUpdate final {
    bool hold_started{};
    bool hold_released{};
    bool short_tap_released{};
    bool long_hold_recognized{};
    bool waiting_for_valid_pose{};
    bool recenter_capture_requested{};
};

struct MenuButtonSourceSelection final {
    bool pressed{};
    bool using_secondary_fallback{};
};

// SteamVR's Quest driver exposes the physical left Menu button as its reserved
// system/dashboard input and reports the OpenXR menu/click action inactive.
// Use B only when that native Menu action is unavailable; runtimes that expose
// Menu retain the established button and B remains their gameplay crouch key.
[[nodiscard]] MenuButtonSourceSelection select_menu_button_source(
    bool menu_action_active,
    bool menu_action_current,
    bool secondary_action_active,
    bool secondary_action_current) noexcept;

// One shared tap-versus-hold decision for native Escape and recenter. As soon
// as the threshold elapses, the gesture becomes a consumed long hold even if
// tracking is temporarily invalid, so its eventual release can never open or
// close a menu by mistake.
[[nodiscard]] MenuButtonGestureUpdate update_menu_button_gesture(
    bool menu_pressed,
    bool hmd_pose_valid,
    std::uint64_t now_milliseconds,
    std::uint64_t hold_milliseconds,
    MenuButtonGestureState* state) noexcept;

}  // namespace wawvr::mod
