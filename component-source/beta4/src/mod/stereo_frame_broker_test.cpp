#include "stereo_frame_broker.hpp"

#include <atomic>
#include <cstdarg>
#include <iostream>
#include <string_view>

namespace wawvr::mod {

// The production diagnostics live with the Present owner. Broker unit tests
// intentionally use no-op sinks so they can exercise publication semantics in
// isolation without constructing D3D/OpenXR runtime state.
void stereo_diagnostic_log(const char*, ...) noexcept {}
void stereo_diagnostic_log_once(
    std::atomic_flag&, const char*, ...) noexcept {}

}  // namespace wawvr::mod

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

}  // namespace

int main() {
    using namespace wawvr::mod;

    clear_pending_stereo_frame();
    expect(!pending_stereo_frame_ready_after(0),
           "cleared broker is not packing-ready");

    wawvr::xr::FrameState frame{};
    frame.frame_id = 41;
    frame.views_valid = true;
    wawvr::xr::Posef anchor{};
    publish_pending_stereo_frame(frame, anchor);
    expect(!pending_stereo_frame_ready_after(0),
           "non-rendering publication is rejected");

    frame.should_render = true;
    publish_pending_stereo_frame(frame, anchor);
    expect(pending_stereo_frame_ready_after(0),
           "valid boundary publication is packing-ready");

    PendingStereoFrame acquired{};
    expect(try_acquire_pending_stereo_frame(0, &acquired) &&
               acquired.frame.frame_id == 41,
           "scene acquisition sees the same valid frame");
    wawvr::xr::Quaternionf rebased_orientation{
        0.0F, 0.70710678F, 0.0F, 0.70710678F};
    expect(try_rebase_pending_stereo_tracking_anchor(
               41, anchor.orientation, rebased_orientation),
           "unstaged exact frame accepts a body-yaw tracking rebase");
    PendingStereoFrame rebased{};
    expect(try_acquire_pending_stereo_frame(0, &rebased) &&
               rebased.tracking_anchor.orientation.y ==
                   rebased_orientation.y,
           "scene acquisition observes the rebased anchor");
    expect(!try_rebase_pending_stereo_tracking_anchor(
               40, anchor.orientation, rebased_orientation),
           "stale frame id cannot rebase a newer stereo publication");
    expect(!pending_stereo_frame_ready_after(41),
           "consumed publication cannot keep a later HUD frame packed");
    expect(pending_stereo_frame_ready_after(40),
           "readiness query remains non-consuming for a newer publication");

    clear_pending_stereo_frame();
    expect(!pending_stereo_frame_ready_after(41),
           "reset invalidation rejects a stale consumed publication");

    frame.frame_id = 42;
    anchor.orientation = {};
    publish_pending_stereo_frame(frame, anchor);
    expect(pending_stereo_frame_ready_after(41),
           "a newly published frame becomes packing-ready after reset");

    if (failures != 0) {
        std::cerr << failures << " stereo broker test(s) failed\n";
        return 1;
    }
    std::cout << "stereo-frame broker tests passed\n";
    return 0;
}
