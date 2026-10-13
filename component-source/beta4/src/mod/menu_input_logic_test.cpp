#include "menu_input_logic.hpp"
#include "native_menu_mouse_gate_logic.hpp"

#include <cmath>
#include <iostream>

namespace {

int failures = 0;

void expect(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_frontend_cursor_entry_and_motion() {
    wawvr::mod::MenuNavigationState state{};
    wawvr::mod::MenuNavigationFrame frame{
        .ui_active = true,
        .input_focused = true,
        .region = {.origin_x = 0, .width = 1024, .height = 768},
    };
    frame.now_milliseconds = 100;
    auto update = wawvr::mod::update_menu_navigation(frame, &state);
    expect(update.cursor_position_valid && update.cursor_x == 512 &&
               update.cursor_y == 384,
           "frontend menu cursor starts in the full-frame centre");

    frame.stick_valid = true;
    frame.stick_x = 1.0F;
    frame.stick_y = 1.0F;
    frame.now_milliseconds = 150;
    update = wawvr::mod::update_menu_navigation(frame, &state);
    expect(update.cursor_moved && update.cursor_x == 567 &&
               update.cursor_y == 329,
           "left stick moves native pixels and inverts OpenXR Y");
}

void test_native_confirm_key_routing() {
    using wawvr::mod::NativeMenuConfirmKey;
    using wawvr::mod::choose_native_menu_confirm_key;

    expect(choose_native_menu_confirm_key(true, true, true) ==
               NativeMenuConfirmKey::enter,
           "active in-match MP team/class UI confirms with Enter");
    expect(choose_native_menu_confirm_key(true, false, true) ==
               NativeMenuConfirmKey::mouse1,
           "MP frontend keeps native Mouse1 confirmation");
    expect(choose_native_menu_confirm_key(false, true, true) ==
               NativeMenuConfirmKey::mouse1,
           "SP active menus keep native Mouse1 confirmation");
    expect(choose_native_menu_confirm_key(true, true, false) ==
               NativeMenuConfirmKey::mouse1,
           "catcher-off gameplay never selects the MP Enter workaround");
}

void test_active_right_eye_offset_and_clamp() {
    wawvr::mod::MenuNavigationState state{};
    wawvr::mod::MenuNavigationFrame frame{
        .ui_active = true,
        .input_focused = true,
        .active_gameplay_menu = true,
        .now_milliseconds = 10,
        .region = {.origin_x = 800, .width = 800, .height = 900},
    };
    auto update = wawvr::mod::update_menu_navigation(frame, &state);
    expect(update.cursor_position_valid && update.cursor_x == 1000 &&
               update.cursor_y == 234,
           "active pause cursor starts eye-local then adds right-eye origin");

    frame.stick_valid = true;
    frame.stick_x = -1.0F;
    frame.stick_y = -1.0F;
    frame.now_milliseconds = 10'000;
    update = wawvr::mod::update_menu_navigation(frame, &state);
    expect(update.cursor_x == 945 && update.cursor_y == 289,
           "menu cursor dt is capped at 50 ms");

    state.cursor_x = 0.0F;
    state.cursor_y = 899.0F;
    frame.now_milliseconds = 10'050;
    update = wawvr::mod::update_menu_navigation(frame, &state);
    expect(update.cursor_x == 800 && update.cursor_y == 899,
           "active source clamp never crosses into the other packed eye");
}

void test_deadzone_buttons_and_focus_edges() {
    wawvr::mod::MenuNavigationState state{};
    wawvr::mod::MenuNavigationFrame frame{
        .ui_active = true,
        .input_focused = true,
        .stick_valid = true,
        .stick_x = 0.19F,
        .stick_y = -0.19F,
        .confirm_held = true,
        .back_held = true,
        .now_milliseconds = 100,
        .region = {.origin_x = 0, .width = 640, .height = 480},
    };
    auto update = wawvr::mod::update_menu_navigation(frame, &state);
    expect(!update.confirm_tap && !update.back_tap,
           "buttons already held on UI entry do not activate an item");

    frame.confirm_held = false;
    frame.back_held = false;
    frame.now_milliseconds = 116;
    update = wawvr::mod::update_menu_navigation(frame, &state);
    expect(!update.cursor_moved,
           "independent 0.20 stick deadzones suppress cursor drift");

    frame.confirm_held = true;
    frame.back_held = true;
    frame.now_milliseconds = 132;
    update = wawvr::mod::update_menu_navigation(frame, &state);
    expect(!update.confirm_tap && update.back_tap,
           "Back exclusively wins simultaneous A/B edges so only one page can mutate");

    frame.now_milliseconds = 148;
    update = wawvr::mod::update_menu_navigation(frame, &state);
    expect(!update.confirm_tap && !update.back_tap,
           "held A and B do not repeat every frame");

    frame.input_focused = false;
    frame.now_milliseconds = 164;
    static_cast<void>(wawvr::mod::update_menu_navigation(frame, &state));
    frame.input_focused = true;
    frame.now_milliseconds = 180;
    update = wawvr::mod::update_menu_navigation(frame, &state);
    expect(!update.confirm_tap && !update.back_tap,
           "focus regain while held requires release before activation");
}

void test_leaving_ui_resets_navigation() {
    wawvr::mod::MenuNavigationState state{};
    wawvr::mod::MenuNavigationFrame frame{
        .ui_active = true,
        .input_focused = true,
        .now_milliseconds = 100,
        .region = {.origin_x = 0, .width = 640, .height = 480},
    };
    static_cast<void>(wawvr::mod::update_menu_navigation(frame, &state));
    frame.ui_active = false;
    frame.now_milliseconds = 116;
    const auto update = wawvr::mod::update_menu_navigation(frame, &state);
    expect(!update.cursor_position_valid && !state.ui_active &&
               state.region.width == 0,
           "leaving UI clears cursor and edge latches");
}

void test_pointer_cursor_and_trigger_edges() {
    wawvr::mod::MenuNavigationState state{};
    wawvr::mod::MenuNavigationFrame frame{
        .ui_active = true,
        .input_focused = true,
        .stick_valid = true,
        .stick_x = 1.0F,
        .stick_y = -1.0F,
        .pointer_valid = true,
        .pointer_u = 0.25F,
        .pointer_v = 0.75F,
        .now_milliseconds = 100,
        .region = {.origin_x = 1000, .width = 1000, .height = 500},
    };
    auto update = wawvr::mod::update_menu_navigation(frame, &state);
    expect(update.pointer_position_used && update.cursor_x == 1250 &&
               update.cursor_y == 374 && !update.confirm_tap,
           "controller ray takes precedence over stick and positions the offset native cursor");

    frame.pointer_confirm_held = true;
    frame.now_milliseconds = 116;
    update = wawvr::mod::update_menu_navigation(frame, &state);
    expect(update.confirm_tap && update.pointer_confirm_tap,
           "fresh trigger edge clicks the pointed menu item once");

    frame.now_milliseconds = 132;
    update = wawvr::mod::update_menu_navigation(frame, &state);
    expect(!update.confirm_tap,
           "held trigger does not repeat native Mouse1 every frame");

    frame.pointer_valid = false;
    frame.now_milliseconds = 148;
    static_cast<void>(wawvr::mod::update_menu_navigation(frame, &state));
    frame.pointer_confirm_held = false;
    frame.now_milliseconds = 164;
    static_cast<void>(wawvr::mod::update_menu_navigation(frame, &state));
    frame.pointer_confirm_held = true;
    frame.now_milliseconds = 180;
    update = wawvr::mod::update_menu_navigation(frame, &state);
    expect(!update.confirm_tap,
           "trigger press outside the panel cannot click a stale cursor");

    frame.pointer_valid = true;
    frame.now_milliseconds = 196;
    update = wawvr::mod::update_menu_navigation(frame, &state);
    expect(update.pointer_position_used && !update.confirm_tap,
           "entering the panel while trigger is held moves but does not click");
}

void test_native_mouse_event_gate() {
    using wawvr::mod::should_forward_native_menu_mouse_event;

    expect(!should_forward_native_menu_mouse_event(0, 0),
           "stationary desktop mouse polling does not overwrite VR cursor");
    expect(should_forward_native_menu_mouse_event(1, 0) &&
               should_forward_native_menu_mouse_event(-1, 0) &&
               should_forward_native_menu_mouse_event(0, 1) &&
               should_forward_native_menu_mouse_event(0, -1) &&
               should_forward_native_menu_mouse_event(-17, 23),
           "any signed physical mouse delta preserves native mouse control");
    expect(wawvr::mod::kNativeMenuMouseDxBridgeStackOffset == 0x14 &&
               wawvr::mod::kNativeMenuMouseDyBridgeStackOffset == 0x18,
           "CL_MouseEvent bridge uses the verified x86 dx/dy stack ABI");
}

void test_trigger_held_on_ui_entry_requires_release() {
    wawvr::mod::MenuNavigationState state{};
    wawvr::mod::MenuNavigationFrame frame{
        .ui_active = true,
        .input_focused = true,
        .pointer_valid = true,
        .pointer_u = 0.5F,
        .pointer_v = 0.5F,
        .pointer_confirm_held = true,
        .now_milliseconds = 100,
        .region = {.origin_x = 0, .width = 640, .height = 480},
    };
    auto update = wawvr::mod::update_menu_navigation(frame, &state);
    expect(update.pointer_position_used && !update.confirm_tap,
           "trigger already held when UI appears is latched without clicking");
    frame.now_milliseconds = 116;
    update = wawvr::mod::update_menu_navigation(frame, &state);
    expect(!update.confirm_tap,
           "entry-held trigger remains non-repeating");
    frame.pointer_confirm_held = false;
    frame.now_milliseconds = 132;
    static_cast<void>(wawvr::mod::update_menu_navigation(frame, &state));
    frame.pointer_confirm_held = true;
    frame.now_milliseconds = 148;
    update = wawvr::mod::update_menu_navigation(frame, &state);
    expect(update.pointer_confirm_tap,
           "release plus a new pointed trigger edge clicks normally");
}

void test_analog_trigger_hysteresis() {
    bool held = wawvr::mod::update_menu_analog_trigger(true, 0.56F, false);
    expect(held, "analog trigger crosses the 0.55 press threshold");
    held = wawvr::mod::update_menu_analog_trigger(true, 0.54F, held);
    expect(held, "minor movement below press threshold does not release");
    held = wawvr::mod::update_menu_analog_trigger(true, 0.46F, held);
    expect(held, "held trigger stays latched above release threshold");
    held = wawvr::mod::update_menu_analog_trigger(true, 0.45F, held);
    expect(!held, "analog trigger releases at the 0.45 threshold");
    expect(!wawvr::mod::update_menu_analog_trigger(true, 0.54F, held),
           "released trigger must cross 0.55 again before another click");
    expect(!wawvr::mod::update_menu_analog_trigger(false, 1.0F, true),
           "inactive analog action fails released");
}

void test_pointer_trigger_reconnect_requires_release() {
    wawvr::mod::MenuPointerTriggerState state{};
    auto held = wawvr::mod::update_menu_pointer_trigger(
        true, true, false, true, 0.0F, &state);
    expect(!held && state.armed,
           "active boolean trigger arms only after an observed release");
    held = wawvr::mod::update_menu_pointer_trigger(
        true, true, true, true, 1.0F, &state);
    expect(held, "armed boolean trigger reports a normal press");
    held = wawvr::mod::update_menu_pointer_trigger(
        true, false, false, false, 0.0F, &state);
    expect(!held && !state.armed,
           "action inactivity disarms instead of pretending to be a release");
    held = wawvr::mod::update_menu_pointer_trigger(
        true, true, true, true, 1.0F, &state);
    expect(!held && !state.armed,
           "boolean action reactivation while physically held cannot click");
    static_cast<void>(wawvr::mod::update_menu_pointer_trigger(
        true, true, false, true, 0.0F, &state));
    held = wawvr::mod::update_menu_pointer_trigger(
        true, true, true, true, 1.0F, &state);
    expect(held, "release after boolean reconnect rearms the next press");

    // Switching from a boolean binding to an analog fallback is another
    // activation boundary and must obey the same release-to-rearm rule.
    held = wawvr::mod::update_menu_pointer_trigger(
        true, false, false, true, 1.0F, &state);
    expect(!held && !state.armed,
           "source transition to a high analog sample is disarmed");
    static_cast<void>(wawvr::mod::update_menu_pointer_trigger(
        true, false, false, true, 0.40F, &state));
    held = wawvr::mod::update_menu_pointer_trigger(
        true, false, false, true, 0.60F, &state);
    expect(held, "analog fallback clicks only after release then press");

    held = wawvr::mod::update_menu_pointer_trigger(
        false, false, false, true, 1.0F, &state);
    expect(!held && !state.armed,
           "focus loss disarms a held trigger");
}

}  // namespace

int main() {
    test_native_mouse_event_gate();
    test_frontend_cursor_entry_and_motion();
    test_native_confirm_key_routing();
    test_active_right_eye_offset_and_clamp();
    test_deadzone_buttons_and_focus_edges();
    test_leaving_ui_resets_navigation();
    test_pointer_cursor_and_trigger_edges();
    test_trigger_held_on_ui_entry_requires_release();
    test_analog_trigger_hysteresis();
    test_pointer_trigger_reconnect_requires_release();
    if (failures != 0) {
        std::cerr << failures << " menu-input logic test(s) failed\n";
        return 1;
    }
    std::cout << "menu-input logic tests passed\n";
    return 0;
}
