// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "reload_feedback_logic.hpp"

namespace wawvr::xr { class OpenXrRuntime; struct FrameState; }
namespace wawvr::mod {
// Producers supply accepted physical transitions only. No audio or OpenXR
// API is called from these game-thread entry points.
void queue_manual_reload_feedback(ReloadFeedbackStep step, ReloadFeedbackHand hand,
                                  std::uint64_t action_sequence) noexcept;
void reset_manual_reload_feedback() noexcept;

// XR owner only, before firing feedback so a simultaneous shot retains its
// normal pulse. nullptr discards pending work and closes owned audio channels.
void service_manual_reload_feedback(wawvr::xr::OpenXrRuntime& runtime,
                                    const wawvr::xr::FrameState* frame) noexcept;
// XR owner only: a successful shot replaced any reload pulse in this hand.
void notify_firing_haptic_overrode_reload() noexcept;
}
