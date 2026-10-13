// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>

namespace wawvr::xr { class OpenXrRuntime; struct FrameState; }
namespace wawvr::mod {
// Native confirmed-local-shot producers only. Never call from a trigger-held,
// animation, tracer, or per-eye draw path. No OpenXR calls on the game thread.
void queue_firing_haptic(std::uint64_t now_milliseconds) noexcept;

// Called only by the XR owner, after action sync. nullptr disarms the mailbox
// and cancels any short pulse during menu/focus/session/device loss.
void service_firing_haptics(
    wawvr::xr::OpenXrRuntime& runtime,
    const wawvr::xr::FrameState* frame) noexcept;
}
