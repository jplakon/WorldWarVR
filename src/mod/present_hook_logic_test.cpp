#include "present_hook_logic.hpp"

#include "xr_math.h"

#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

int failures = 0;

void expect(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void* fake_pointer(const std::uintptr_t value) {
    return reinterpret_cast<void*>(value);
}

void test_present_target_monitor_plan() {
    using wawvr::mod::PresentTargetIdentity;
    using wawvr::mod::PresentTargetMonitorAction;

    constexpr PresentTargetIdentity first{0x1000U, 0x2000U};
    constexpr PresentTargetIdentity replacement{0x3000U, 0x4000U};

    expect(wawvr::mod::plan_present_target_monitor(
               false, {}, false, {}) ==
               PresentTargetMonitorAction::wait_for_target,
           "an invalid candidate before installation waits for a target");
    expect(wawvr::mod::plan_present_target_monitor(
               false, {}, true, first) ==
               PresentTargetMonitorAction::install_target,
           "a validated initial target is installed");
    expect(wawvr::mod::plan_present_target_monitor(
               true, first, true, first) ==
               PresentTargetMonitorAction::keep_target,
           "the same validated target keeps the existing hooks");
    expect(wawvr::mod::plan_present_target_monitor(
               true, first, true, replacement) ==
               PresentTargetMonitorAction::rebind_target,
           "a validated replacement target requests a recoverable rebind");
    expect(wawvr::mod::plan_present_target_monitor(
               true, first, false, {}) ==
               PresentTargetMonitorAction::keep_target,
           "a temporarily missing candidate never abandons an installed target");

    expect(wawvr::mod::plan_present_target_monitor(
               false, {}, true, {0, 0x2000U}) ==
               PresentTargetMonitorAction::wait_for_target,
           "a nominally validated candidate with no device fails closed");
    expect(wawvr::mod::plan_present_target_monitor(
               true, first, true, {0x3000U, 0}) ==
               PresentTargetMonitorAction::keep_target,
           "an incomplete replacement cannot trigger a rebind");

    expect(wawvr::mod::present_target_still_current(
               replacement, true, replacement),
           "the same fully validated post-teardown target remains installable");
    expect(!wawvr::mod::present_target_still_current(
               replacement, false, replacement),
           "an unreadable post-teardown target fails closed");
    expect(!wawvr::mod::present_target_still_current(
               replacement, true, first),
           "a target that changes during teardown must be retried");
    expect(!wawvr::mod::present_target_still_current(
               {}, true, {}),
           "null target identities are never current install candidates");
}

void test_immutable_thunk_routes() {
    using wawvr::mod::ImmutableThunkRouteRegistration;
    wawvr::mod::ImmutableThunkRouteTable<
        std::uintptr_t, std::uintptr_t, 2> routes;

    expect(routes.Register(0, 0x1000U) ==
               ImmutableThunkRouteRegistration::invalid,
           "a null thunk object cannot be routed");
    expect(routes.Register(0x100U, 0) ==
               ImmutableThunkRouteRegistration::invalid,
           "a null native callback cannot be routed");
    expect(routes.Register(0x100U, 0x1000U) ==
               ImmutableThunkRouteRegistration::inserted,
           "the first object-to-native route is inserted");
    expect(routes.Lookup(0x100U) == 0x1000U,
           "the inserted object resolves to its native callback");
    expect(routes.Register(0x100U, 0x1000U) ==
               ImmutableThunkRouteRegistration::already_registered,
           "re-registering an identical immutable route is harmless");
    expect(routes.Register(0x100U, 0x2000U) ==
               ImmutableThunkRouteRegistration::conflicting_callback,
           "an old object's native callback can never be replaced");
    expect(routes.Lookup(0x100U) == 0x1000U,
           "a conflicting registration preserves the original route");
    expect(routes.Register(0x200U, 0x2000U) ==
               ImmutableThunkRouteRegistration::inserted,
           "a replacement object receives its own native route");
    expect(routes.Register(0x300U, 0x3000U) ==
               ImmutableThunkRouteRegistration::capacity_exhausted,
           "route exhaustion fails closed");
    expect(routes.Lookup(0x300U) == 0,
           "an unregistered object never inherits another target's callback");

    wawvr::mod::ImmutableThunkRouteTable<
        std::uintptr_t, std::uintptr_t, 2> object_routes;
    wawvr::mod::ImmutableThunkRouteTable<
        std::uintptr_t, std::uintptr_t, 2> shared_vtable_routes;
    expect(shared_vtable_routes.Register(0xA00U, 0xA000U) ==
               ImmutableThunkRouteRegistration::inserted,
           "a shared vtable route is registered before its slot is patched");
    expect(wawvr::mod::lookup_immutable_thunk_route(
               object_routes, 0xB00U,
               shared_vtable_routes, 0xA00U) == 0xA000U,
           "a recreated COM object sharing the patched vtable reaches the native callback");
    expect(object_routes.Register(0xB00U, 0xA000U) ==
               ImmutableThunkRouteRegistration::inserted,
           "the renderer monitor can later register the replacement object");
    expect(wawvr::mod::lookup_immutable_thunk_route(
               object_routes, 0xB00U,
               shared_vtable_routes, 0xA00U) == 0xA000U,
           "matching exact-object and shared-vtable routes preserve the native callback");
    expect(object_routes.Register(0xC00U, 0xC000U) ==
               ImmutableThunkRouteRegistration::inserted,
           "a distinct retired object can retain its immutable callback");
    expect(wawvr::mod::lookup_immutable_thunk_route(
               object_routes, 0xC00U,
               shared_vtable_routes, 0xA00U) == 0xA000U,
           "a current shared vtable overrides a recycled object's stale exact route");
    expect(wawvr::mod::lookup_immutable_thunk_route(
               object_routes, 0xC00U,
               shared_vtable_routes, std::uintptr_t{0}) == 0xC000U,
           "an exact retired-object route remains usable when its vtable is unreadable");
    expect(wawvr::mod::lookup_immutable_thunk_route(
               object_routes, 0xD00U,
               shared_vtable_routes, 0xD00U) == 0,
           "an object and vtable with no immutable route still fail closed");

    wawvr::mod::ImmutableThunkRouteTable<
        std::uintptr_t, std::uintptr_t, 1> exhausted_object_routes;
    wawvr::mod::ImmutableThunkRouteTable<
        std::uintptr_t, std::uintptr_t, 1> reusable_vtable_routes;
    expect(exhausted_object_routes.Register(0xE00U, 0xE000U) ==
               ImmutableThunkRouteRegistration::inserted,
           "the exact-object route fixture reaches capacity");
    expect(exhausted_object_routes.Register(0xF00U, 0xE000U) ==
               ImmutableThunkRouteRegistration::capacity_exhausted,
           "a later replacement object cannot consume another exact slot");
    expect(reusable_vtable_routes.Register(0xF10U, 0xE000U) ==
               ImmutableThunkRouteRegistration::inserted,
           "the process-lifetime shared vtable remains registered");
    expect(wawvr::mod::lookup_immutable_thunk_route(
               exhausted_object_routes, 0xF00U,
               reusable_vtable_routes, 0xF10U) == 0xE000U,
           "exact-object capacity exhaustion cannot break a known shared vtable");
}

void test_hook_slot_plan() {
    const auto original = fake_pointer(0x1000u);
    const auto replacement = fake_pointer(0x70001000u);
    wawvr::mod::HookSlotPlan plan{};
    expect(wawvr::mod::build_hook_slot_plan(original, replacement, &plan),
           "valid hook slot should be planned");
    expect(plan.original == original, "original slot value should be preserved");
    expect(plan.replacement == replacement,
           "replacement slot value should be preserved");
    expect(!wawvr::mod::build_hook_slot_plan(nullptr, replacement, &plan),
           "a null original must fail closed");
    expect(!wawvr::mod::build_hook_slot_plan(original, nullptr, &plan),
           "a null replacement must fail closed");
    expect(!wawvr::mod::build_hook_slot_plan(original, original, &plan),
           "an already-installed replacement must fail closed");
}

void test_ownership_classification() {
    const auto original = fake_pointer(0x1000u);
    const auto installed = fake_pointer(0x2000u);
    const auto foreign = fake_pointer(0x3000u);
    expect(wawvr::mod::classify_hook_slot_ownership(
               installed, original, installed) ==
               wawvr::mod::VtableOwnership::installed_by_us,
           "installed table should be owned by this mod");
    expect(wawvr::mod::classify_hook_slot_ownership(
               original, original, installed) ==
               wawvr::mod::VtableOwnership::original,
           "original table should be recognized");
    expect(wawvr::mod::classify_hook_slot_ownership(
               foreign, original, installed) ==
               wawvr::mod::VtableOwnership::foreign,
           "foreign replacement must never be overwritten during restore");
}

void test_duplicate_mono_layout() {
    const auto layout = wawvr::mod::duplicate_mono_layout();
    for (const auto& eye : layout.eyes) {
        expect(eye.x == 0.0f && eye.y == 0.0f && eye.width == 1.0f &&
                   eye.height == 1.0f,
               "each duplicate-mono eye should sample the full backbuffer");
    }
    expect(layout.preserve_source_aspect,
           "duplicate-mono presentation preserves desktop aspect ratio");
}

void test_eye_local_bloom_requires_explicit_stereo_opt_in() {
    using wawvr::mod::PresentationMode;

    expect(!wawvr::mod::should_enable_eye_local_bloom(
               PresentationMode::stereo, false),
           "default stereo uses the COD4/F.E.A.R. exact color-copy path");
    expect(wawvr::mod::should_enable_eye_local_bloom(
               PresentationMode::stereo, true),
           "explicit stereo opt-in preserves the experimental WaW bloom");
    expect(!wawvr::mod::should_enable_eye_local_bloom(
               PresentationMode::full_frame_mono, true) &&
               !wawvr::mod::should_enable_eye_local_bloom(
                   PresentationMode::active_ui_mono, true) &&
               !wawvr::mod::should_enable_eye_local_bloom(
                   PresentationMode::active_console_mono, true),
           "bloom never changes frontend, cinematic, or menu panels");
}

void test_presentation_mode_classifier_and_layouts() {
    using wawvr::mod::ActiveUiMonoSource;
    using wawvr::mod::PresentationMode;

    expect(wawvr::mod::should_pack_stereo_scene(
               false, false, 0, 10, 0x10),
           "SP keeps its established scene path regardless of presentation state");
    expect(wawvr::mod::should_pack_stereo_scene(
               true, true, 10, 10, 0),
           "active catcher-free MP gameplay packs two VR eyes");
    expect(!wawvr::mod::should_pack_stereo_scene(
               true, true, 10, 10, 0x10),
           "MP team/class/pause UI renders a stock mono scene");
    expect(!wawvr::mod::should_pack_stereo_scene(
               true, true, 10, 10, 0x01),
           "MP console presentation renders a stock mono scene");
    expect(!wawvr::mod::should_pack_stereo_scene(
               true, true, 10, 10, 0x18),
           "mixed or unknown MP catchers also fail safe to stock mono");
    expect(!wawvr::mod::should_pack_stereo_scene(
               true, true, 9, 10, 0),
           "non-active MP loading states render a stock mono scene");
    expect(!wawvr::mod::should_pack_stereo_scene(
               true, false, 10, 10, 0),
           "unreadable MP presentation state fails safe to stock mono");

    for (std::int32_t state = 0; state < 10; ++state) {
        expect(wawvr::mod::classify_presentation_mode(
                   true, state, 0, false) ==
                   PresentationMode::full_frame_mono,
               "every non-active T4 connection state uses full-frame mono");
    }
    expect(wawvr::mod::classify_presentation_mode(
               true, 10, 0, true) == PresentationMode::stereo,
           "active gameplay without a catcher stays stereo");
    expect(wawvr::mod::classify_presentation_mode(
               true, 10, 0x10, true) ==
               PresentationMode::active_ui_mono,
           "active UI catcher switches gameplay to mono");
    expect(wawvr::mod::classify_presentation_mode(
               true, 10, 0x01, true) ==
               PresentationMode::active_console_mono,
           "active console catcher switches gameplay to mono");
    expect(wawvr::mod::classify_presentation_mode(
               true, 10, 0x08, true) ==
               PresentationMode::full_frame_mono,
           "active message catcher fails safe to full-frame mono");
    expect(wawvr::mod::classify_presentation_mode(
               true, 10, 0x20, true) ==
               PresentationMode::full_frame_mono,
           "any unclassified active catcher fails safe to full-frame mono");
    expect(wawvr::mod::classify_presentation_mode(
               false, 0, 0, false) ==
               PresentationMode::full_frame_mono,
           "unbound no-scene frame fails safe to full mono");
    expect(wawvr::mod::classify_presentation_mode(
               false, 0, 0, true) == PresentationMode::stereo,
           "confirmed stereo remains usable if state read is unavailable");

    const auto right = wawvr::mod::presentation_layout(
        PresentationMode::active_ui_mono,
        ActiveUiMonoSource::right_eye);
    for (const auto& eye : right.eyes) {
        expect(eye.x == 0.5F && eye.width == 0.5F &&
                   eye.y == 0.0F && eye.height == 1.0F,
               "active pause menu can duplicate the final right-eye crop");
    }
    for (const float multiplier : right.source_aspect_multipliers) {
        expect(multiplier == 2.0F,
               "packed pause-menu half restores its logical full width");
    }
    wawvr::xr::StereoSourceLayout packed{};
    packed.eyes[0] = {0.0F, 0.0F, 2496.0F / 6016.0F, 1.0F};
    packed.eyes[1] = {
        2496.0F / 6016.0F, 0.0F, 2496.0F / 6016.0F, 1.0F};
    packed.physical_scope.active = true;
    packed.physical_scope.source = {
        4992.0F / 6016.0F, 0.0F, 1024.0F / 6016.0F,
        1024.0F / 2688.0F};
    const auto packed_right = wawvr::mod::presentation_layout(
        PresentationMode::active_ui_mono,
        ActiveUiMonoSource::right_eye, &packed);
    for (const auto& eye : packed_right.eyes) {
        expect(std::fabs(eye.x - packed.eyes[1].x) < 0.00001F &&
                   std::fabs(eye.width - packed.eyes[1].width) < 0.00001F &&
                   eye.y == 0.0F && eye.height == 1.0F,
               "packed scope frames crop the staged right eye, not the scope panel");
    }
    expect(!packed_right.physical_scope.active,
           "mono pause presentation does not composite the physical scope");
    const auto full = wawvr::mod::presentation_layout(
        PresentationMode::full_frame_mono,
        ActiveUiMonoSource::right_eye);
    for (const auto& eye : full.eyes) {
        expect(eye.x == 0.0F && eye.width == 1.0F,
               "frontend/cinematic mono always samples the complete frame");
    }
}

void test_monoscopic_comfort_views() {
    wawvr::xr::Posef center{};
    center.position = {1.0F, 2.0F, 3.0F};
    center.orientation = {0.0F, 0.25F, 0.0F, 0.9682458F};
    const auto views = wawvr::mod::monoscopic_comfort_views(center);
    for (const auto& view : views) {
        expect(view.pose.position.x == center.position.x &&
                   view.pose.position.y == center.position.y &&
                   view.pose.position.z == center.position.z &&
                   view.pose.orientation.y == center.orientation.y,
               "comfort eyes share the frozen center pose");
        expect(view.fov.angle_left == -0.62F &&
                   view.fov.angle_right == 0.62F &&
                   view.fov.angle_up == 0.38F &&
                   view.fov.angle_down == -0.38F,
               "comfort eyes share the compact centered FOV");
    }
}

void test_projection_recovery_plan() {
    using wawvr::mod::ProjectionSubmission;

    auto plan = wawvr::mod::plan_projection_recovery(true, true, true);
    expect(plan.submission == ProjectionSubmission::current,
           "a newly rendered projection must take precedence");
    expect(!plan.abandon_xr,
           "a newly rendered projection keeps XR active");

    plan = wawvr::mod::plan_projection_recovery(false, true, true);
    expect(plan.submission == ProjectionSubmission::previous,
           "device loss must reuse the last released projection");
    expect(!plan.abandon_xr,
           "a reusable projection keeps XR out of the loading state");

    plan = wawvr::mod::plan_projection_recovery(false, false, true);
    expect(plan.submission == ProjectionSubmission::none,
           "device loss before first projection has nothing safe to submit");
    expect(plan.abandon_xr,
           "device loss before first projection must leave XR cleanly");

    plan = wawvr::mod::plan_projection_recovery(false, false, false);
    expect(plan.submission == ProjectionSubmission::none,
           "an ordinary skipped render submits no projection");
    expect(!plan.abandon_xr,
           "an ordinary skipped render is not a fatal XR condition");

    expect(wawvr::mod::reusable_layer_intact_after_render_attempt(
               false, 0),
           "failed render before any release preserves the prior layer");
    expect(!wawvr::mod::reusable_layer_intact_after_render_attempt(
                false, 0x1U) &&
               !wawvr::mod::reusable_layer_intact_after_render_attempt(
                   false, 0x2U),
           "any partially released eye invalidates prior layer metadata");
    expect(wawvr::mod::reusable_layer_intact_after_render_attempt(
               true, 0x3U),
           "complete current render replaces rather than corrupts recovery state");
    expect(!wawvr::mod::reusable_layer_intact_after_frame_end(true, false),
           "failed EndFrame invalidates metadata after complete image overwrite");
    expect(wawvr::mod::reusable_layer_intact_after_frame_end(false, false) &&
               wawvr::mod::reusable_layer_intact_after_frame_end(true, true),
           "no overwrite or successful frame end preserves valid recovery state");
}

void test_mono_composition_selection() {
    using wawvr::mod::ProjectionSubmission;
    using wawvr::xr::CompositionLayerKind;

    expect(wawvr::mod::select_composition_layer_kind(
               ProjectionSubmission::none, true, true, true) ==
               CompositionLayerKind::none,
           "no reusable image always submits no composition layer");
    expect(wawvr::mod::select_composition_layer_kind(
               ProjectionSubmission::current, true, false, true) ==
               CompositionLayerKind::quad,
           "a current finite mono panel selects a quad");
    expect(wawvr::mod::select_composition_layer_kind(
               ProjectionSubmission::current, false, false, true) ==
               CompositionLayerKind::none,
           "mono without a current quad fails closed instead of projecting");
    expect(wawvr::mod::select_composition_layer_kind(
               ProjectionSubmission::previous, false, false, true) ==
               CompositionLayerKind::none,
           "a cached stereo projection cannot cross into a mono interval");
    expect(wawvr::mod::select_composition_layer_kind(
               ProjectionSubmission::previous, false, true, true) ==
               CompositionLayerKind::quad,
           "a previously accepted mono quad remains reusable");
    expect(wawvr::mod::select_composition_layer_kind(
               ProjectionSubmission::current, false, false, false) ==
               CompositionLayerKind::projection,
           "stereo gameplay still selects projection");
    expect(wawvr::mod::select_composition_layer_kind(
               ProjectionSubmission::current, true, false, true, true) ==
               CompositionLayerKind::projection,
           "an explicitly selected current mono cadence fallback uses projection");
    expect(wawvr::mod::select_composition_layer_kind(
               ProjectionSubmission::previous, false, true, true, true) ==
               CompositionLayerKind::quad,
           "the cadence fallback never reinterprets a cached quad as projection");
    expect(wawvr::mod::select_composition_layer_kind(
               ProjectionSubmission::previous, false, false, true, true) ==
               CompositionLayerKind::projection,
           "an accepted cadence-fallback projection remains reusable when capture is late");
    expect(wawvr::mod::select_composition_layer_kind(
               ProjectionSubmission::current, false, false, true, true) ==
               CompositionLayerKind::none,
           "the cadence fallback still requires a validated current panel");
}

void test_virtual_desktop_ui_projection_fallback_gate() {
    using wawvr::mod::PresentationMode;
    using wawvr::mod::should_use_virtual_desktop_ui_projection_fallback;

    expect(should_use_virtual_desktop_ui_projection_fallback(
               PresentationMode::active_ui_mono,
               "VirtualDesktopXR", false),
           "VirtualDesktop active pause UI selects the cadence fallback");
    expect(!should_use_virtual_desktop_ui_projection_fallback(
               PresentationMode::active_ui_mono,
               "VirtualDesktopXR", true),
           "the environment kill switch disables the cadence fallback");
    expect(!should_use_virtual_desktop_ui_projection_fallback(
               PresentationMode::active_ui_mono,
               "SteamVR/OpenXR", false) &&
               !should_use_virtual_desktop_ui_projection_fallback(
                   PresentationMode::full_frame_mono,
                   "VirtualDesktopXR", false) &&
               !should_use_virtual_desktop_ui_projection_fallback(
                   PresentationMode::active_console_mono,
                   "VirtualDesktopXR", false) &&
               !should_use_virtual_desktop_ui_projection_fallback(
                   PresentationMode::stereo,
                   "VirtualDesktopXR", false),
           "other runtimes and non-pause presentation modes keep their existing layer kind");
}

void test_presentation_transition_resets_panel_interval() {
    using wawvr::mod::PresentationMode;
    expect(wawvr::mod::presentation_transition_resets_comfort_anchor(
               false, PresentationMode::stereo,
               PresentationMode::full_frame_mono),
           "first observed presentation establishes a fresh interval");
    expect(wawvr::mod::presentation_transition_resets_comfort_anchor(
               true, PresentationMode::stereo,
               PresentationMode::active_ui_mono) &&
               wawvr::mod::presentation_transition_resets_comfort_anchor(
                   true, PresentationMode::active_ui_mono,
                   PresentationMode::stereo),
           "both stereo-to-mono and mono-to-stereo reset the panel anchor");
    expect(!wawvr::mod::presentation_transition_resets_comfort_anchor(
                true, PresentationMode::full_frame_mono,
                PresentationMode::active_ui_mono) &&
               !wawvr::mod::presentation_transition_resets_comfort_anchor(
                   true, PresentationMode::stereo,
                   PresentationMode::stereo),
           "mode changes within one mono interval preserve its room anchor");
}

void test_deferred_capture_presentation_classes() {
    using wawvr::mod::PresentationMode;
    using wawvr::mod::presentation_modes_share_capture_class;

    expect(presentation_modes_share_capture_class(
               PresentationMode::stereo, PresentationMode::stereo),
           "deferred stereo remains compatible with stereo submission");
    expect(!presentation_modes_share_capture_class(
                PresentationMode::stereo,
                PresentationMode::active_ui_mono) &&
               !presentation_modes_share_capture_class(
                   PresentationMode::full_frame_mono,
                   PresentationMode::stereo),
           "a queued projection never crosses a stereo/mono transition");
    expect(presentation_modes_share_capture_class(
               PresentationMode::full_frame_mono,
               PresentationMode::active_ui_mono) &&
               presentation_modes_share_capture_class(
                   PresentationMode::active_ui_mono,
                   PresentationMode::active_console_mono),
           "capture-owned layouts preserve transitions among mono panels");
}

void test_current_frame_gates_deferred_capture_rendering() {
    using wawvr::mod::current_frame_accepts_source_render;

    expect(current_frame_accepts_source_render(true, true, true),
           "a live renderable frame may consume a deferred source");
    expect(!current_frame_accepts_source_render(true, false, true),
           "current shouldRender false withholds deferred swapchain writes");
    expect(!current_frame_accepts_source_render(true, true, false),
           "current invalid views withhold deferred swapchain writes");
    expect(!current_frame_accepts_source_render(false, true, true),
           "post-Com_Frame policy can independently withhold fresh rendering");
}

void test_live_held_weapon_prefers_exact_current_capture() {
    using wawvr::mod::PresentationMode;
    using wawvr::mod::should_prefer_exact_current_shared_capture;

    expect(should_prefer_exact_current_shared_capture(
               true, true, PresentationMode::stereo, true, true, true),
           "every live controller-held stereo weapon prefers its exact current capture");
    expect(!should_prefer_exact_current_shared_capture(
               true, true, PresentationMode::stereo, true, false, true) &&
               !should_prefer_exact_current_shared_capture(
                   true, true, PresentationMode::stereo, true, true, false),
           "missing and retained weapon receipts preserve the nonblocking capture path");
    expect(!should_prefer_exact_current_shared_capture(
               false, true, PresentationMode::stereo, true, true, true) &&
               !should_prefer_exact_current_shared_capture(
                   true, false, PresentationMode::stereo, true, true, true) &&
               !should_prefer_exact_current_shared_capture(
                   true, true, PresentationMode::active_ui_mono, true, true,
                   true) &&
               !should_prefer_exact_current_shared_capture(
                   true, true, PresentationMode::stereo, false, true, true),
           "unavailable, nonrenderable, mono, and non-stereo scenes never block for a weapon capture");
}

void test_capture_submission_phase_observation() {
    using wawvr::mod::observe_capture_submission_phase;

    const auto same = observe_capture_submission_phase(
        120, 1'200'000'000, 120, 1'200'000'000,
        true, 120, 9120, 9120);
    expect(same.valid && same.source_is_live_frame &&
               same.live_minus_source_frame_id == 0 &&
               same.live_minus_source_predicted_time == 0 &&
               same.weapon_receipt_matches_source,
           "same-frame capture and matching weapon receipt classify exactly");

    const auto deferred = observe_capture_submission_phase(
        123, 1'233'333'333, 121, 1'211'111'111,
        true, 121, 9121, 9121);
    expect(deferred.valid && !deferred.source_is_live_frame &&
               deferred.live_minus_source_frame_id == 2 &&
               deferred.live_minus_source_predicted_time == 22'222'222 &&
               deferred.weapon_receipt_matches_source,
           "deferred source preserves positive live-minus-source phase");

    const auto mismatched_receipt = observe_capture_submission_phase(
        124, 1'244'444'444, 122, 1'222'222'222,
        true, 122, 9123, 9122);
    expect(mismatched_receipt.valid &&
               !mismatched_receipt.weapon_receipt_matches_source,
           "action-sequence mismatch rejects a nominal frame-id receipt");

    const auto invalid = observe_capture_submission_phase(
        0, 0, 122, 1'222'222'222, false, 0, 0, 0);
    expect(!invalid.valid,
           "missing live frame metadata fails the diagnostic closed");
}

void test_post_com_frame_service_plan() {
    auto plan = wawvr::mod::plan_post_com_frame_service(
        true, true, true, false, false);
    expect(plan.allow_fresh_projection,
           "a successful cooperative Present permits fresh projection");
    expect(plan.prime_next_frame,
           "a successful cooperative engine frame primes XR after Com_Frame");

    plan = wawvr::mod::plan_post_com_frame_service(
        false, false, true, false, false);
    expect(!plan.allow_fresh_projection,
           "a no-Present engine frame cannot claim a fresh projection");
    expect(plan.prime_next_frame,
           "a no-Present engine frame still re-primes XR after Com_Frame");

    plan = wawvr::mod::plan_post_com_frame_service(
        true, false, true, false, false);
    expect(!plan.allow_fresh_projection,
           "a failed Present cannot claim a fresh projection");
    expect(plan.prime_next_frame,
           "a cooperative frame can re-prime after a non-device-loss Present failure");

    plan = wawvr::mod::plan_post_com_frame_service(
        true, true, false, false, false);
    expect(!plan.allow_fresh_projection && !plan.prime_next_frame,
           "device loss before any reusable projection pauses XR priming");

    plan = wawvr::mod::plan_post_com_frame_service(
        true, false, false, true, false);
    expect(!plan.allow_fresh_projection && plan.prime_next_frame,
           "device loss with a reusable projection preserves XR frame cadence");

    plan = wawvr::mod::plan_post_com_frame_service(
        true, true, true, true, true);
    expect(!plan.allow_fresh_projection && !plan.prime_next_frame,
           "an in-progress Reset owns the boundary and pauses XR service");
}

void test_d3d9ex_reset_preflight_classification() {
    using wawvr::mod::d3d9ex_reset_requires_lost_preflight;

    expect(!d3d9ex_reset_requires_lost_preflight(
               false, true, true, true, true),
           "a legacy D3D9 reset never selects the Ex lost-device preflight");
    expect(!d3d9ex_reset_requires_lost_preflight(
               true, false, false, false, false),
           "a healthy Ex vid_restart uses complete bridge invalidation");
    expect(d3d9ex_reset_requires_lost_preflight(
               true, true, false, false, false),
           "a prior ResetEx failure remains on the D3D11-only retry route");
    expect(d3d9ex_reset_requires_lost_preflight(
               true, false, true, false, false),
           "an observed device-loss boundary selects lost-device preflight");
    expect(d3d9ex_reset_requires_lost_preflight(
               true, false, false, true, false),
           "a loss or mode-change Present result selects lost-device preflight");
    expect(d3d9ex_reset_requires_lost_preflight(
               true, false, false, false, true),
           "persistent Ex recovery evidence survives boundary observation clearing");
}

void test_tracking_anchor_keeps_yaw_but_levels_horizon() {
    constexpr float kHalfSqrtTwo = 0.70710678118F;
    const wawvr::xr::Quaternionf yaw_left{
        0.0F, kHalfSqrtTwo, 0.0F, kHalfSqrtTwo};
    const wawvr::xr::Quaternionf pitch_up{
        0.25881904510F, 0.0F, 0.0F, 0.96592582628F};
    wawvr::xr::Posef head{};
    head.position = {1.0F, 2.0F, 3.0F};
    head.orientation = wawvr::xr::Multiply(yaw_left, pitch_up);

    const auto anchor = wawvr::mod::leveled_tracking_anchor(head);
    expect(anchor.position.x == 1.0F && anchor.position.y == 2.0F &&
               anchor.position.z == 3.0F,
           "leveled anchor preserves tracked position");
    expect(std::abs(anchor.orientation.x) < 0.0001F &&
               std::abs(anchor.orientation.z) < 0.0001F,
           "leveled anchor removes pitch and roll");
    const auto forward = wawvr::xr::Rotate(
        anchor.orientation, {0.0F, 0.0F, -1.0F});
    expect(std::abs(forward.x + 1.0F) < 0.0001F &&
               std::abs(forward.y) < 0.0001F &&
               std::abs(forward.z) < 0.0001F,
           "leveled anchor retains OpenXR yaw");
}

void test_vertical_recenter_preserves_prior_heading() {
    constexpr float kHalfSqrtTwo = 0.70710678118F;
    wawvr::xr::Posef looking_straight_up{};
    looking_straight_up.orientation = {
        kHalfSqrtTwo, 0.0F, 0.0F, kHalfSqrtTwo};
    const wawvr::xr::Quaternionf prior_yaw{
        0.0F, kHalfSqrtTwo, 0.0F, kHalfSqrtTwo};
    const auto anchor = wawvr::mod::leveled_tracking_anchor(
        looking_straight_up, &prior_yaw);
    const auto forward = wawvr::xr::Rotate(
        anchor.orientation, {0.0F, 0.0F, -1.0F});
    expect(std::abs(forward.x + 1.0F) < 0.0001F &&
               std::abs(forward.y) < 0.0001F &&
               std::abs(forward.z) < 0.0001F,
           "near-vertical recenter preserves prior stable yaw instead of global -Z");
}

void test_manual_recenter_is_always_gravity_level() {
    const wawvr::xr::Quaternionf yaw{
        0.0F, 0.34202014333F, 0.0F, 0.93969262079F};
    const wawvr::xr::Quaternionf pitch{
        0.25881904510F, 0.0F, 0.0F, 0.96592582628F};
    const wawvr::xr::Quaternionf roll{
        0.0F, 0.0F, 0.17364817767F, 0.98480775301F};
    wawvr::xr::Posef head{};
    head.position = {1.25F, -0.5F, 2.75F};
    const auto orientation = wawvr::xr::Multiply(
        yaw, wawvr::xr::Multiply(pitch, roll));
    head.orientation = {
        orientation.x * 2.0F,
        orientation.y * 2.0F,
        orientation.z * 2.0F,
        orientation.w * 2.0F,
    };

    const auto anchor = wawvr::mod::leveled_tracking_anchor(head);
    const float orientation_length = std::sqrt(
        anchor.orientation.x * anchor.orientation.x +
        anchor.orientation.y * anchor.orientation.y +
        anchor.orientation.z * anchor.orientation.z +
        anchor.orientation.w * anchor.orientation.w);
    expect(std::abs(orientation_length - 1.0F) < 0.0001F,
           "manual recenter produces a normalized orientation");
    expect(anchor.position.x == head.position.x &&
               anchor.position.y == head.position.y &&
               anchor.position.z == head.position.z,
           "manual recenter preserves the HMD center position");

    const auto relative = wawvr::xr::OpenXrPoseToIwRelative(
        head, anchor, 1.0F);
    expect(std::abs(relative.position.x) < 0.0001F &&
               std::abs(relative.position.y) < 0.0001F &&
               std::abs(relative.position.z) < 0.0001F,
           "manual recenter zeros current HMD translation");
    expect(std::abs(anchor.orientation.x) < 0.0001F &&
               std::abs(anchor.orientation.z) < 0.0001F,
           "manual recenter cannot preserve headset pitch or roll in the horizon anchor");
    const auto anchor_forward = wawvr::xr::Rotate(
        anchor.orientation, {0.0F, 0.0F, -1.0F});
    const auto source_forward = wawvr::xr::Rotate(
        wawvr::xr::Normalize(head.orientation), {0.0F, 0.0F, -1.0F});
    const float source_horizontal = std::sqrt(
        source_forward.x * source_forward.x +
        source_forward.z * source_forward.z);
    expect(std::abs(anchor_forward.y) < 0.0001F &&
               std::abs(anchor_forward.x -
                        source_forward.x / source_horizontal) < 0.0001F &&
               std::abs(anchor_forward.z -
                        source_forward.z / source_horizontal) < 0.0001F,
           "manual recenter preserves heading while restoring a level horizon");
    expect(std::abs(relative.axis.forward.y) < 0.0001F,
           "manual recenter zeros current relative yaw");
    expect(std::abs(relative.axis.forward.z) > 0.1F &&
               std::abs(relative.axis.left.z) > 0.1F,
           "manual recenter does not erase physical HMD pitch or roll");
}

} // namespace

int main() {
    test_present_target_monitor_plan();
    test_immutable_thunk_routes();
    test_hook_slot_plan();
    test_ownership_classification();
    test_duplicate_mono_layout();
    test_eye_local_bloom_requires_explicit_stereo_opt_in();
    test_presentation_mode_classifier_and_layouts();
    test_monoscopic_comfort_views();
    test_projection_recovery_plan();
    test_mono_composition_selection();
    test_virtual_desktop_ui_projection_fallback_gate();
    test_presentation_transition_resets_panel_interval();
    test_deferred_capture_presentation_classes();
    test_current_frame_gates_deferred_capture_rendering();
    test_live_held_weapon_prefers_exact_current_capture();
    test_capture_submission_phase_observation();
    test_post_com_frame_service_plan();
    test_d3d9ex_reset_preflight_classification();
    test_tracking_anchor_keeps_yaw_but_levels_horizon();
    test_vertical_recenter_preserves_prior_heading();
    test_manual_recenter_is_always_gravity_level();
    if (failures != 0) {
        std::cerr << failures << " present-hook logic test(s) failed\n";
        return 1;
    }
    std::cout << "present-hook logic tests passed\n";
    return 0;
}
