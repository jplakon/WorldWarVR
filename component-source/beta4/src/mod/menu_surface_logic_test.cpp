// SPDX-License-Identifier: GPL-3.0-only
#include "menu_surface_logic.hpp"

#include "menu_panel_logic.hpp"

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

bool near(const float left, const float right) {
    return std::abs(left - right) < 0.0001F;
}

void test_full_and_packed_surface_descriptors() {
    wawvr::xr::QuadLayer panel{};
    expect(wawvr::mod::build_world_menu_panel({}, &panel),
           "test panel builds");

    wawvr::mod::MenuPointerSurface full{};
    expect(wawvr::mod::build_menu_pointer_surface(
               panel,
               wawvr::mod::presentation_layout(
                   wawvr::mod::PresentationMode::full_frame_mono,
                   wawvr::mod::ActiveUiMonoSource::full_frame),
               2560, 1440, 4, wawvr::mod::kT4UiKeyCatcher, &full),
           "frontend full-frame UI surface builds");
    expect(full.cursor_region.origin_x == 0 &&
               full.cursor_region.width == 2560 &&
               full.cursor_region.height == 1440,
           "full surface stores exact native backbuffer region");

    wawvr::mod::MenuPointerSurface right{};
    expect(wawvr::mod::build_menu_pointer_surface(
               panel,
               wawvr::mod::presentation_layout(
                   wawvr::mod::PresentationMode::active_ui_mono,
                   wawvr::mod::ActiveUiMonoSource::right_eye),
               2560, 1440, 10, wawvr::mod::kT4UiKeyCatcher, &right),
           "packed right-eye UI surface builds");
    expect(right.cursor_region.origin_x == 1280 &&
               right.cursor_region.width == 1280 &&
               right.cursor_region.height == 1440,
           "packed descriptor retains exact T4 source origin and extent");
    expect(near(right.content_viewport.x, full.content_viewport.x) &&
               near(right.content_viewport.width, full.content_viewport.width),
           "logical packed-half multiplier preserves the same visible aspect");
}

void test_state_matching_blocks_stale_ui() {
    wawvr::xr::QuadLayer panel{};
    static_cast<void>(wawvr::mod::build_world_menu_panel({}, &panel));
    wawvr::mod::MenuPointerSurface surface{};
    expect(wawvr::mod::build_menu_pointer_surface(
               panel,
               wawvr::mod::presentation_layout(
                   wawvr::mod::PresentationMode::full_frame_mono,
                   wawvr::mod::ActiveUiMonoSource::full_frame),
               1024, 768, 4, wawvr::mod::kT4UiKeyCatcher, &surface),
           "4:3 recovered UI descriptor builds");
    expect(wawvr::mod::menu_pointer_surface_matches_state(
               surface, 4, wawvr::mod::kT4UiKeyCatcher),
           "unchanged static UI remains pointable during safe recovery");
    expect(!wawvr::mod::menu_pointer_surface_matches_state(
                surface, 10, wawvr::mod::kT4UiKeyCatcher) &&
               !wawvr::mod::menu_pointer_surface_matches_state(
                   surface, 4, 0),
           "connection/catcher transition disables stale visible UI targeting");
    expect(!wawvr::mod::build_menu_pointer_surface(
                panel, {}, 1024, 768, 4, 0, &surface),
           "non-UI mono quad never becomes an interactive surface");
}

void test_only_submitted_current_pages_are_interactive() {
    wawvr::xr::QuadLayer panel{};
    static_cast<void>(wawvr::mod::build_world_menu_panel({}, &panel));
    wawvr::mod::MenuPointerSurface surface{};
    expect(wawvr::mod::build_menu_pointer_surface(
               panel,
               wawvr::mod::presentation_layout(
                   wawvr::mod::PresentationMode::full_frame_mono,
                   wawvr::mod::ActiveUiMonoSource::full_frame),
               1024, 768, 4, wawvr::mod::kT4UiKeyCatcher, &surface),
           "submitted UI descriptor builds for interaction policy");
    expect(wawvr::mod::menu_pointer_surface_allows_interaction(
               &surface, 4, wawvr::mod::kT4UiKeyCatcher, true, false),
           "matching focused submitted page allows interaction");
    expect(!wawvr::mod::menu_pointer_surface_allows_interaction(
                nullptr, 4, wawvr::mod::kT4UiKeyCatcher, true, false) &&
               !wawvr::mod::menu_pointer_surface_allows_interaction(
                   &surface, 4, wawvr::mod::kT4UiKeyCatcher, false, false) &&
               !wawvr::mod::menu_pointer_surface_allows_interaction(
                   &surface, 4, wawvr::mod::kT4UiKeyCatcher, true, true),
           "missing surface, lost focus, and same-call Escape suppress every menu action");
    expect(wawvr::mod::menu_ui_action_invalidates_pointer_surface(
               true, false, false) &&
               wawvr::mod::menu_ui_action_invalidates_pointer_surface(
                   false, true, false) &&
               wawvr::mod::menu_ui_action_invalidates_pointer_surface(
                   false, false, true) &&
               !wawvr::mod::menu_ui_action_invalidates_pointer_surface(
                   false, false, false),
           "every page-mutating input retires the descriptor until fresh pixels submit");
}

}  // namespace

int main() {
    test_full_and_packed_surface_descriptors();
    test_state_matching_blocks_stale_ui();
    test_only_submitted_current_pages_are_interactive();
    if (failures != 0) {
        std::cerr << failures << " menu-surface logic test(s) failed\n";
        return 1;
    }
    std::cout << "menu-surface logic tests passed\n";
    return 0;
}
