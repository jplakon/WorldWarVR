// SPDX-License-Identifier: GPL-3.0-only
#include "firing_haptics.hpp"
#include "firing_haptic_logic.hpp"
#include "controller_state.hpp"
#include "stereo_diagnostics.hpp"
#include "t4_presentation_state.hpp"
#include "openxr_runtime.h"
#include <windows.h>
#include <atomic>

namespace wawvr::mod {
namespace {
SRWLOCK g_mailbox_lock = SRWLOCK_INIT;
FiringHapticMailbox g_mailbox{};
// XR-owner-thread state only; producers never read or write these.
bool g_pulse_may_be_active{};
std::uint32_t g_diagnostic_pulses{};

bool gameplay_active() noexcept {
    const auto state = read_t4_presentation_state();
    return state.valid && state.connection_state == state.active_connection_state &&
           state.key_catchers == 0;
}

bool feedback_frame_usable(const wawvr::xr::FrameState& frame) noexcept {
    const auto& hand = frame.actions.hands[
        static_cast<std::uint32_t>(wawvr::xr::Hand::Right)];
    return frame.frame_id != 0 && frame.actions.sequence != 0 &&
        frame.should_render && frame.views_valid && frame.actions.focused &&
        (hand.aim.active || hand.grip.active);
}

bool diagnostic_logging_enabled() noexcept {
    static const bool enabled = [] {
        wchar_t value[8]{};
        return GetEnvironmentVariableW(L"WAWVR_HAPTIC_DIAGNOSTICS", value, 8) == 1 &&
               value[0] == L'1';
    }();
    return enabled;
}
}

void queue_firing_haptic(const std::uint64_t now_milliseconds) noexcept {
    ControllerFrameSnapshot snapshot{};
    if (!read_controller_frame(&snapshot)) return;
    const auto current = GetTickCount64();
    if (now_milliseconds == 0 || current < now_milliseconds ||
        current - now_milliseconds > kFiringHapticMaxAgeMilliseconds ||
        snapshot.publication_milliseconds == 0 ||
        current < snapshot.publication_milliseconds ||
        current - snapshot.publication_milliseconds >
            kFiringHapticMaxAgeMilliseconds ||
        !feedback_frame_usable(snapshot.frame) || !gameplay_active()) return;

    AcquireSRWLockExclusive(&g_mailbox_lock);
    // Timestamp while holding the shared mailbox lock. A service heartbeat
    // from the other thread cannot overtake a timestamp sampled before it.
    const auto queued_now = GetTickCount64();
    const bool queued = queued_now >= now_milliseconds &&
        queued_now - now_milliseconds <= kFiringHapticMaxAgeMilliseconds &&
        now_milliseconds >= g_mailbox.acceptance_started_milliseconds &&
        queue_firing_haptic_event(&g_mailbox, queued_now);
    ReleaseSRWLockExclusive(&g_mailbox_lock);
    if (queued) {
        WAWVR_STEREO_DIAG_ONCE(
            "HapticDiag confirmed local shot queued; COD4-style right-controller firing pulse");
    }
}

void service_firing_haptics(
    wawvr::xr::OpenXrRuntime& runtime,
    const wawvr::xr::FrameState* const frame) noexcept {
    const bool allowed = frame != nullptr && feedback_frame_usable(*frame) &&
        runtime.session_running() && runtime.session_focused() && gameplay_active();
    AcquireSRWLockExclusive(&g_mailbox_lock);
    const auto now = GetTickCount64();
    set_firing_haptic_acceptance(&g_mailbox, allowed, now);
    const std::uint32_t shots = allowed
        ? consume_firing_haptic_events(&g_mailbox, now) : 0;
    ReleaseSRWLockExclusive(&g_mailbox_lock);

    if (!allowed) {
        if (g_pulse_may_be_active) {
            static_cast<void>(runtime.StopHaptic(wawvr::xr::Hand::Right));
            g_pulse_may_be_active = false;
        }
        return;
    }
    if (shots == 0) return;

    // COD4 uses 0.78 / 50ms, runtime-selected frequency, in the firearm hand
    // only. WaW currently fires with the right trigger/right weapon grip.
    // Multiple events inside one XR interval coalesce, never become a delayed
    // queue of pulses after firing stops or after a hitch/focus recovery.
    const bool applied = runtime.ApplyHaptic(
        wawvr::xr::Hand::Right, kFiringHapticAmplitude,
        kFiringHapticDurationSeconds, 0.0F);
    g_pulse_may_be_active = applied || g_pulse_may_be_active;
    if (diagnostic_logging_enabled() && g_diagnostic_pulses++ < 64) {
        stereo_diagnostic_log(
            "HapticDiag firing pulse result=%d hand=right amplitude=%.2f durationMs=50 shots=%u",
            applied ? 1 : 0, kFiringHapticAmplitude, shots);
    }
    if (applied) {
        WAWVR_STEREO_DIAG_ONCE(
            "HapticDiag first firing pulse accepted by OpenXR: right amplitude=0.78 durationMs=50");
    } else {
        WAWVR_STEREO_DIAG_ONCE(
            "HapticDiag firing feedback unavailable; gameplay and stereo continue unchanged");
    }
}
}
