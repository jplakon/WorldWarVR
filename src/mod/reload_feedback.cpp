// SPDX-License-Identifier: GPL-3.0-only
#include "reload_feedback.hpp"
#include "controller_state.hpp"
#include "stereo_diagnostics.hpp"
#include "t4_presentation_state.hpp"
#include "openxr_runtime.h"

#include <windows.h>
#include <mmsystem.h>
#include <array>
#include <new>

namespace wawvr::mod {
namespace {
SRWLOCK g_lock = SRWLOCK_INIT;
ReloadFeedbackMailbox g_mailbox{};
std::uint64_t g_reset_generation{};
// Everything below is touched only by the XR owner thread.
std::uint64_t g_serviced_reset_generation{};
std::array<std::uint64_t, 2> g_pulse_expires_at{};

bool diagnostics_enabled() noexcept {
    static const bool enabled = [] {
        wchar_t value[8]{};
        return GetEnvironmentVariableW(L"WAWVR_RELOAD_FEEDBACK_DIAGNOSTICS", value, 8) == 1 && value[0] == L'1';
    }();
    return enabled;
}
bool audio_enabled() noexcept {
    static const bool enabled = [] {
        wchar_t value[8]{};
        return !(GetEnvironmentVariableW(L"WAWVR_RELOAD_AUDIO", value, 8) == 1 && value[0] == L'0');
    }();
    return enabled;
}
bool gameplay_active() noexcept {
    const auto state = read_t4_presentation_state();
    return state.valid && state.connection_state == state.active_connection_state && state.key_catchers == 0;
}
bool frame_usable(const wawvr::xr::FrameState& frame) noexcept {
    return frame.frame_id != 0 && frame.actions.sequence != 0 && frame.should_render &&
        frame.views_valid && frame.actions.focused;
}
bool hand_usable(const wawvr::xr::FrameState& frame, const ReloadFeedbackHand hand) noexcept {
    const auto index = static_cast<std::size_t>(hand);
    if (index >= 2) return false;
    const auto& state = frame.actions.hands[index];
    return (state.grip.active && state.grip.position_valid && state.grip.orientation_valid) ||
        (state.aim.active && state.aim.position_valid && state.aim.orientation_valid);
}

struct AudioSlot final {
    HWAVEOUT device{};
    WAVEHDR header{};
    ReloadFeedbackSamples samples{};
    std::uint64_t serial{};
    bool submitted{};
    bool cancelled{};
};
struct AudioSlots final {
    std::array<AudioSlot, 4> slots{};
    std::array<ReloadFeedbackSamples, 7> samples{};
    bool initialization_attempted{};
};

// Process-lifetime backing storage is intentional. No callback points into
// this DLL, and the driver never borrows DLL storage during asynchronous
// playback, even if the host unloads without an orderly XR teardown. A small
// fixed allocation is retained; normal focus/session teardown closes handles.
AudioSlots* g_audio_slots{};
bool g_audio_allocation_attempted{};

void retire_audio(const bool cancel, const bool close) noexcept {
    if (g_audio_slots == nullptr) return;
    for (auto& slot : g_audio_slots->slots) {
        if (slot.device == nullptr) continue;
        if (cancel && slot.submitted && !slot.cancelled) {
            slot.cancelled = true;
            const MMRESULT result = waveOutReset(slot.device);
            if (diagnostics_enabled()) stereo_diagnostic_log(
                "ReloadFeedbackDiag audio-cancel serial=%llu result=%u",
                static_cast<unsigned long long>(slot.serial), static_cast<unsigned>(result));
        }
        if (slot.submitted && (slot.header.dwFlags & WHDR_DONE) == 0) continue;
        if (slot.submitted && diagnostics_enabled()) stereo_diagnostic_log(
            "ReloadFeedbackDiag audio-finished serial=%llu completed=%d backend=waveOut",
            static_cast<unsigned long long>(slot.serial), slot.cancelled ? 0 : 1);
        slot.submitted = false;
        if (close) {
            if ((slot.header.dwFlags & WHDR_PREPARED) != 0 &&
                waveOutUnprepareHeader(slot.device, &slot.header, sizeof(slot.header)) != MMSYSERR_NOERROR) continue;
            if (waveOutClose(slot.device) != MMSYSERR_NOERROR) continue;
            slot = {};
        }
    }
    if (close) g_audio_slots->initialization_attempted = false;
}

// Open/prepare at the start of an active gameplay epoch, before any physical
// action is accepted. A gesture never opens a device or prepares a buffer.
// Failed initialization is not retried per-frame or per-gesture.
void initialize_audio() noexcept {
    if (!audio_enabled()) return;
    if (!g_audio_allocation_attempted) {
        g_audio_allocation_attempted = true;
        g_audio_slots = new (std::nothrow) AudioSlots{};
        if (g_audio_slots != nullptr) {
            for (std::size_t index = 1; index < g_audio_slots->samples.size(); ++index) {
                g_audio_slots->samples[index] = make_reload_feedback_samples(static_cast<ReloadFeedbackStep>(index));
            }
        }
    }
    if (g_audio_slots == nullptr || g_audio_slots->initialization_attempted) return;
    g_audio_slots->initialization_attempted = true;
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = kReloadFeedbackSampleRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 2;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    unsigned ready = 0;
    for (auto& slot : g_audio_slots->slots) {
        if (slot.device == nullptr) {
            MMRESULT result = waveOutOpen(&slot.device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
            if (result != MMSYSERR_NOERROR) { slot.device = nullptr; continue; }
            slot.header.lpData = reinterpret_cast<LPSTR>(slot.samples.data());
            slot.header.dwBufferLength = static_cast<DWORD>(sizeof(slot.samples));
            result = waveOutPrepareHeader(slot.device, &slot.header, sizeof(slot.header));
            if (result != MMSYSERR_NOERROR) {
                if (waveOutClose(slot.device) == MMSYSERR_NOERROR) slot = {};
                continue;
            }
        }
        if ((slot.header.dwFlags & WHDR_PREPARED) != 0) ++ready;
    }
    if (diagnostics_enabled()) stereo_diagnostic_log(
        "ReloadFeedbackDiag audio-ready slots=%u endpoint=Windows-default sampleRate=24000 durationMs=80 backend=waveOut",
        ready);
}

// Separate, bounded channels start immediately; never append a stale sound
// queue or interrupt an existing cue/native game sound. Device failure and
// saturation drop this cue without changing gameplay or retrying it.
const char* submit_audio(const ReloadFeedbackCue& cue, MMRESULT* result) noexcept {
    *result = MMSYSERR_NOERROR;
    if (!audio_enabled()) return "disabled";
    if (g_audio_slots == nullptr) { *result = MMSYSERR_NOMEM; return "unavailable"; }
    AudioSlot* available = nullptr;
    for (auto& slot : g_audio_slots->slots) {
        if (slot.device != nullptr && !slot.submitted &&
            (slot.header.dwFlags & WHDR_PREPARED) != 0) { available = &slot; break; }
    }
    if (available == nullptr) return "unavailable-or-busy-dropped";
    auto& slot = *available;
    slot.samples = g_audio_slots->samples[static_cast<std::size_t>(cue.step)];
    slot.serial = cue.serial;
    slot.cancelled = false;
    *result = waveOutWrite(slot.device, &slot.header, sizeof(slot.header));
    if (*result != MMSYSERR_NOERROR) return "write-failed";
    slot.submitted = true;
    return "submitted";
}
}

void notify_firing_haptic_overrode_reload() noexcept {
    g_pulse_expires_at[static_cast<std::size_t>(ReloadFeedbackHand::Right)] = 0;
}

void reset_manual_reload_feedback() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    clear_reload_feedback_pending(&g_mailbox);
    ++g_reset_generation;
    ReleaseSRWLockExclusive(&g_lock);
}

void queue_manual_reload_feedback(const ReloadFeedbackStep step,
    const ReloadFeedbackHand hand, const std::uint64_t action_sequence) noexcept {
    if (step == ReloadFeedbackStep::None) return;
    ControllerFrameSnapshot snapshot{};
    if (!read_controller_frame(&snapshot)) return;
    const auto now = GetTickCount64();
    if (!frame_usable(snapshot.frame) || !hand_usable(snapshot.frame, hand) ||
        action_sequence != snapshot.frame.actions.sequence ||
        snapshot.publication_milliseconds == 0 || now < snapshot.publication_milliseconds ||
        now - snapshot.publication_milliseconds > kReloadFeedbackMaxAgeMilliseconds || !gameplay_active()) return;
    AcquireSRWLockExclusive(&g_lock);
    const bool queued = queue_reload_feedback_event(&g_mailbox, step, hand, action_sequence, GetTickCount64());
    const auto serial = g_mailbox.next_serial;
    ReleaseSRWLockExclusive(&g_lock);
    if (queued && diagnostics_enabled()) stereo_diagnostic_log(
        "ReloadFeedbackDiag queued serial=%llu step=%s hand=%s actionSeq=%llu",
        static_cast<unsigned long long>(serial), reload_feedback_step_name(step),
        hand == ReloadFeedbackHand::Left ? "left" : "right",
        static_cast<unsigned long long>(action_sequence));
}

void service_manual_reload_feedback(wawvr::xr::OpenXrRuntime& runtime,
    const wawvr::xr::FrameState* const frame) noexcept {
    const bool allowed = frame != nullptr && frame_usable(*frame) &&
        runtime.session_running() && runtime.session_focused() && gameplay_active();
    AcquireSRWLockExclusive(&g_lock);
    const auto now = GetTickCount64();
    set_reload_feedback_acceptance(&g_mailbox, allowed, now);
    const auto batch = consume_reload_feedback_events(&g_mailbox, now);
    const auto reset_generation = g_reset_generation;
    ReleaseSRWLockExclusive(&g_lock);
    const bool reset = reset_generation != g_serviced_reset_generation;
    retire_audio(!allowed || reset, !allowed);
    g_serviced_reset_generation = reset_generation;
    for (std::size_t hand = 0; hand < g_pulse_expires_at.size(); ++hand) {
        if (g_pulse_expires_at[hand] <= now) g_pulse_expires_at[hand] = 0;
        if (g_pulse_expires_at[hand] != 0 &&
            (!allowed || reset || !hand_usable(*frame, static_cast<ReloadFeedbackHand>(hand)))) {
            static_cast<void>(runtime.StopHaptic(static_cast<wawvr::xr::Hand>(hand)));
            g_pulse_expires_at[hand] = 0;
        }
    }
    if (!allowed) return;
    initialize_audio();
    for (std::size_t index = 0; index < batch.count; ++index) {
        const auto& cue = batch.cues[index];
        const auto dispatch_now = GetTickCount64();
        if (dispatch_now < cue.milliseconds ||
            dispatch_now - cue.milliseconds > kReloadFeedbackMaxAgeMilliseconds) continue;
        AcquireSRWLockExclusive(&g_lock);
        const bool still_current = reset_generation == g_reset_generation && g_mailbox.accepting;
        ReleaseSRWLockExclusive(&g_lock);
        if (!still_current) break;
        if (!hand_usable(*frame, cue.hand)) continue;
        const auto hand = static_cast<std::size_t>(cue.hand);
        const bool haptic_accepted = runtime.ApplyHaptic(static_cast<wawvr::xr::Hand>(hand),
            reload_feedback_amplitude(cue.step), reload_feedback_duration_seconds(cue.step), 0.0F);
        if (haptic_accepted) g_pulse_expires_at[hand] = dispatch_now + static_cast<std::uint64_t>(
            reload_feedback_duration_seconds(cue.step) * 1000.0F + 1.0F);
        MMRESULT audio_result{};
        const char* const audio = submit_audio(cue, &audio_result);
        if (diagnostics_enabled()) stereo_diagnostic_log(
            "ReloadFeedbackDiag submit serial=%llu step=%s hand=%s actionSeq=%llu hapticAccepted=%d audio=%s audioResult=%u backend=waveOut amplitude=%.2f durationMs=%.0f",
            static_cast<unsigned long long>(cue.serial), reload_feedback_step_name(cue.step),
            cue.hand == ReloadFeedbackHand::Left ? "left" : "right",
            static_cast<unsigned long long>(cue.action_sequence), haptic_accepted ? 1 : 0,
            audio, static_cast<unsigned>(audio_result), reload_feedback_amplitude(cue.step),
            reload_feedback_duration_seconds(cue.step) * 1000.0F);
    }
}
}  // namespace wawvr::mod
