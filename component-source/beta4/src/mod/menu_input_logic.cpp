// SPDX-License-Identifier: GPL-3.0-only
#include "menu_input_logic.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace wawvr::mod {
namespace {

[[nodiscard]] bool valid_region(const MenuCursorRegion& region) noexcept {
    return region.width != 0 && region.height != 0 &&
           region.origin_x <=
               static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) &&
           region.width - 1U <=
               static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) -
                   region.origin_x &&
           region.height - 1U <=
               static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
}

[[nodiscard]] bool finite_stick(const MenuNavigationFrame& frame) noexcept {
    return frame.stick_valid && std::isfinite(frame.stick_x) &&
           std::isfinite(frame.stick_y);
}

[[nodiscard]] bool valid_pointer(const MenuNavigationFrame& frame) noexcept {
    return frame.pointer_valid && std::isfinite(frame.pointer_u) &&
           std::isfinite(frame.pointer_v) && frame.pointer_u >= 0.0F &&
           frame.pointer_u <= 1.0F && frame.pointer_v >= 0.0F &&
           frame.pointer_v <= 1.0F;
}

void publish_cursor(
    const MenuNavigationState& state,
    MenuNavigationUpdate* const update) noexcept {
    update->cursor_position_valid = true;
    update->cursor_x = static_cast<std::int32_t>(state.region.origin_x) +
                       static_cast<std::int32_t>(std::lround(state.cursor_x));
    update->cursor_y =
        static_cast<std::int32_t>(std::lround(state.cursor_y));
}

void apply_pointer(
    const MenuNavigationFrame& frame,
    MenuNavigationState* const state,
    MenuNavigationUpdate* const update) noexcept {
    state->cursor_x = frame.pointer_u *
                      static_cast<float>(frame.region.width - 1U);
    state->cursor_y = frame.pointer_v *
                      static_cast<float>(frame.region.height - 1U);
    update->cursor_moved = true;
    update->pointer_position_used = true;
    publish_cursor(*state, update);
}

}  // namespace

NativeMenuConfirmKey choose_native_menu_confirm_key(
    const bool multiplayer_profile,
    const bool active_gameplay_menu,
    const bool ui_active) noexcept {
    return multiplayer_profile && active_gameplay_menu && ui_active
               ? NativeMenuConfirmKey::enter
               : NativeMenuConfirmKey::mouse1;
}

bool update_menu_analog_trigger(
    const bool action_active,
    const float value,
    const bool was_held) noexcept {
    if (!action_active || !std::isfinite(value)) {
        return false;
    }
    return was_held
        ? value > kMenuTriggerReleaseThreshold
        : value >= kMenuTriggerPressThreshold;
}

bool update_menu_pointer_trigger(
    const bool input_focused,
    const bool click_action_active,
    const bool click_current,
    const bool analog_action_active,
    const float analog_value,
    MenuPointerTriggerState* const state) noexcept {
    if (state == nullptr) {
        return false;
    }

    MenuPointerTriggerSource source = MenuPointerTriggerSource::none;
    if (input_focused && click_action_active) {
        source = MenuPointerTriggerSource::boolean_click;
    } else if (input_focused && analog_action_active &&
               std::isfinite(analog_value)) {
        source = MenuPointerTriggerSource::analog;
    }

    if (source == MenuPointerTriggerSource::none) {
        *state = {};
        return false;
    }
    if (source != state->source) {
        *state = {.source = source};
    }

    const bool released = source == MenuPointerTriggerSource::boolean_click
        ? !click_current
        : analog_value <= kMenuTriggerReleaseThreshold;
    if (!state->armed) {
        // The first low sample proves that a real release occurred after
        // activation/reconnection/source selection. A high first sample is
        // ignored until that release arrives.
        state->armed = released;
        state->held = false;
        return false;
    }

    state->held = source == MenuPointerTriggerSource::boolean_click
        ? click_current
        : update_menu_analog_trigger(true, analog_value, state->held);
    return state->held;
}

MenuNavigationUpdate update_menu_navigation(
    const MenuNavigationFrame& frame,
    MenuNavigationState* const state) noexcept {
    MenuNavigationUpdate update{};
    if (state == nullptr) {
        return update;
    }

    if (!frame.ui_active || !valid_region(frame.region)) {
        *state = {};
        state->last_update_milliseconds = frame.now_milliseconds;
        return update;
    }

    const bool entered_ui = !state->ui_active || state->region != frame.region;
    if (entered_ui) {
        state->ui_active = true;
        state->input_focused = frame.input_focused;
        state->confirm_was_held = frame.confirm_held;
        state->pointer_confirm_was_held = frame.pointer_confirm_held;
        state->back_was_held = frame.back_held;
        state->region = frame.region;
        state->cursor_x =
            (frame.active_gameplay_menu ? 0.25F : 0.50F) *
            static_cast<float>(frame.region.width);
        state->cursor_y =
            (frame.active_gameplay_menu ? 0.26F : 0.50F) *
            static_cast<float>(frame.region.height);
        state->cursor_x = std::clamp(
            state->cursor_x, 0.0F,
            static_cast<float>(frame.region.width - 1U));
        state->cursor_y = std::clamp(
            state->cursor_y, 0.0F,
            static_cast<float>(frame.region.height - 1U));
        state->last_update_milliseconds = frame.now_milliseconds;
        if (frame.input_focused && valid_pointer(frame)) {
            apply_pointer(frame, state, &update);
        } else {
            publish_cursor(*state, &update);
        }
        return update;
    }

    if (!frame.input_focused) {
        state->input_focused = false;
        state->confirm_was_held = false;
        state->pointer_confirm_was_held = false;
        state->back_was_held = false;
        state->last_update_milliseconds = frame.now_milliseconds;
        return update;
    }
    if (!state->input_focused) {
        // Regaining focus while a button is already held must not activate a
        // menu item. Require a release and a fresh rising edge.
        state->input_focused = true;
        state->confirm_was_held = frame.confirm_held;
        state->pointer_confirm_was_held = frame.pointer_confirm_held;
        state->back_was_held = frame.back_held;
        state->last_update_milliseconds = frame.now_milliseconds;
        if (valid_pointer(frame)) {
            apply_pointer(frame, state, &update);
        } else {
            publish_cursor(*state, &update);
        }
        return update;
    }

    std::uint64_t elapsed_milliseconds = 0;
    if (frame.now_milliseconds >= state->last_update_milliseconds) {
        elapsed_milliseconds = std::min(
            frame.now_milliseconds - state->last_update_milliseconds,
            kMaximumMenuCursorDeltaMilliseconds);
    }
    state->last_update_milliseconds = frame.now_milliseconds;

    float stick_x = 0.0F;
    float stick_y = 0.0F;
    if (finite_stick(frame)) {
        stick_x = std::clamp(frame.stick_x, -1.0F, 1.0F);
        stick_y = std::clamp(frame.stick_y, -1.0F, 1.0F);
        if (std::abs(stick_x) < kMenuCursorDeadzone) {
            stick_x = 0.0F;
        }
        if (std::abs(stick_y) < kMenuCursorDeadzone) {
            stick_y = 0.0F;
        }
    }

    if (valid_pointer(frame)) {
        // Direct pointing owns the cursor while its ray is on the panel.
        // The accepted left-stick path resumes immediately on a ray miss.
        apply_pointer(frame, state, &update);
    } else if (stick_x != 0.0F || stick_y != 0.0F) {
        const float elapsed_seconds =
            static_cast<float>(elapsed_milliseconds) / 1000.0F;
        state->cursor_x +=
            stick_x * kMenuCursorPixelsPerSecond * elapsed_seconds;
        // OpenXR +Y is up while the native desktop cursor +Y is down.
        state->cursor_y -=
            stick_y * kMenuCursorPixelsPerSecond * elapsed_seconds;
        state->cursor_x = std::clamp(
            state->cursor_x, 0.0F,
            static_cast<float>(frame.region.width - 1U));
        state->cursor_y = std::clamp(
            state->cursor_y, 0.0F,
            static_cast<float>(frame.region.height - 1U));
        update.cursor_moved = true;
        publish_cursor(*state, &update);
    }

    update.back_tap = frame.back_held && !state->back_was_held;
    if (!update.back_tap) {
        // At most one native page mutation may run per engine service. Back
        // wins a simultaneous edge so Mouse1 cannot synchronously open an
        // unseen page immediately before Escape is dispatched to it.
        update.pointer_confirm_tap =
            valid_pointer(frame) && frame.pointer_confirm_held &&
            !state->pointer_confirm_was_held;
        update.confirm_tap =
            (frame.confirm_held && !state->confirm_was_held) ||
            update.pointer_confirm_tap;
    }
    state->confirm_was_held = frame.confirm_held;
    state->pointer_confirm_was_held = frame.pointer_confirm_held;
    state->back_was_held = frame.back_held;
    return update;
}

}  // namespace wawvr::mod
