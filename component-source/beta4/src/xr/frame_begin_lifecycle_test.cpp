// SPDX-License-Identifier: GPL-3.0-only
#include "frame_begin_lifecycle.hpp"

#include <cstdio>
#include <initializer_list>

using namespace wawvr::xr;

int main()
{
    int failures = 0;
    const auto check = [&failures](const bool value, const char* label)
    {
        if (!value) { std::printf("FAIL: %s\n", label); ++failures; }
    };
    check(!defer_graphics_begin_for_runtime(false, true, "VirtualDesktopXR"),
          "default disabled even for VDXR");
    check(defer_graphics_begin_for_runtime(true, false, "VirtualDesktopXR"),
          "explicit VDXR opt-in");
    check(!defer_graphics_begin_for_runtime(true, false, "OpenXR Simulator Runtime"),
          "simulator also requires explicit development flag");
    check(defer_graphics_begin_for_runtime(true, true, "OpenXR Simulator Runtime"),
          "explicit simulator lifecycle screening");
    for (const auto runtime : {"SteamVR/OpenXR", "Oculus", "VirtualDesktopXR-other", ""})
        check(!defer_graphics_begin_for_runtime(true, true, runtime),
              "all other runtimes rejected");

    FrameBeginLifecycle frame;
    check(frame.can_wait() && !frame.active() && !frame.begun(), "initial idle");
    check(!frame.begin_completed(true) && !frame.end_completed(true),
          "cannot invent Begin/End without Wait");
    check(frame.waited() && frame.active() && frame.needs_begin(), "pending wait");
    check(!frame.can_wait() && !frame.waited(), "reject second Wait while deferred");
    check(!frame.end_completed(true), "cannot End an unbegun frame");
    check(frame.begin_completed(true) && frame.begun(), "one deferred Begin");
    check(!frame.begin_completed(true) && !frame.waited(), "no duplicate Begin or Wait");
    check(frame.end_completed(true) && frame.can_wait(), "completed cycle rearms Wait");
    // The same fallback sequence is required for fresh/reused/zero-layer frames,
    // shouldRender=false, and healthy shutdown before the compositor was called.
    for (int path = 0; path < 5; ++path)
    {
        check(frame.waited() && frame.needs_begin(), "fallback owns pending wait");
        check(frame.begin_completed(true) && frame.end_completed(true),
              "fallback pairs Wait Begin End");
    }
    check(frame.waited() && !frame.begin_completed(false), "native Begin failure");
    check(!frame.can_wait() && !frame.active() && !frame.needs_begin() &&
          !frame.end_completed(true), "Begin failure requires teardown, no retry or End");
    frame = {};
    check(frame.waited() && frame.begin_completed(true) && !frame.end_completed(false),
          "native End failure");
    check(!frame.can_wait() && !frame.begun(), "End failure requires teardown");
    std::printf("Frame begin lifecycle: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
