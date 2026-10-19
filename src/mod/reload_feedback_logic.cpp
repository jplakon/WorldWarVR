// SPDX-License-Identifier: GPL-3.0-only
#include "reload_feedback_logic.hpp"

#include <algorithm>
#include <cmath>

namespace wawvr::mod {
namespace {
bool valid_clock(const ReloadFeedbackMailbox& box, std::uint64_t now) noexcept {
    return box.accepting && now != 0 && box.last_service_milliseconds != 0 &&
        now >= box.last_service_milliseconds &&
        now - box.last_service_milliseconds <= kReloadFeedbackMaxAgeMilliseconds;
}
void disarm(ReloadFeedbackMailbox* box) noexcept {
    const auto serial = box->next_serial;
    *box = {};
    box->next_serial = serial;
}
}

ReloadFeedbackStep reload_feedback_step(
    const gameplay::ReloadEvent event,
    const gameplay::ReloadProfileKind profile) noexcept {
    if (profile != gameplay::ReloadProfileKind::DetachableMagazine &&
        profile != gameplay::ReloadProfileKind::InternalStripperClip) return {};
    switch (event) {
    case gameplay::ReloadEvent::ReloadArmed:
        return profile == gameplay::ReloadProfileKind::DetachableMagazine
            ? ReloadFeedbackStep::Ejected : ReloadFeedbackStep::None;
    case gameplay::ReloadEvent::FeedDeviceGrabbed: return ReloadFeedbackStep::Grabbed;
    case gameplay::ReloadEvent::CommitCompleted: return ReloadFeedbackStep::Inserted;
    default: return {};
    }
}

const char* reload_feedback_step_name(const ReloadFeedbackStep step) noexcept {
    switch (step) {
    case ReloadFeedbackStep::Ejected: return "eject";
    case ReloadFeedbackStep::Grabbed: return "grab";
    case ReloadFeedbackStep::Inserted: return "insert";
    case ReloadFeedbackStep::ActionOpened: return "action-open";
    case ReloadFeedbackStep::ActionClosed: return "action-close";
    case ReloadFeedbackStep::ChargeReady: return "charge-ready";
    default: return "none";
    }
}

float reload_feedback_amplitude(const ReloadFeedbackStep step) noexcept {
    switch (step) {
    case ReloadFeedbackStep::Ejected: return 0.20F;
    case ReloadFeedbackStep::Grabbed: return 0.12F;
    case ReloadFeedbackStep::Inserted: return 0.30F;
    case ReloadFeedbackStep::ActionOpened: return 0.22F;
    case ReloadFeedbackStep::ActionClosed: return 0.32F;
    case ReloadFeedbackStep::ChargeReady: return 0.30F;
    default: return 0.0F;
    }
}

float reload_feedback_duration_seconds(const ReloadFeedbackStep step) noexcept {
    switch (step) {
    case ReloadFeedbackStep::Grabbed: return 0.018F;
    case ReloadFeedbackStep::Ejected: return 0.025F;
    case ReloadFeedbackStep::Inserted: return 0.040F;
    case ReloadFeedbackStep::ActionOpened: return 0.025F;
    case ReloadFeedbackStep::ActionClosed: return 0.045F;
    case ReloadFeedbackStep::ChargeReady: return 0.035F;
    default: return 0.0F;
    }
}

void clear_reload_feedback_pending(ReloadFeedbackMailbox* const box) noexcept {
    if (box == nullptr) return;
    box->pending = {};
    box->last_action_sequence = 0;
    box->seen_steps = 0;
}

void set_reload_feedback_acceptance(
    ReloadFeedbackMailbox* const box, const bool allowed, const std::uint64_t now) noexcept {
    if (box == nullptr) return;
    if (!allowed || now == 0) { disarm(box); return; }
    if (!valid_clock(*box, now)) disarm(box);
    box->accepting = true;
    box->last_service_milliseconds = now;
}

bool queue_reload_feedback_event(
    ReloadFeedbackMailbox* const box, const ReloadFeedbackStep step,
    const ReloadFeedbackHand hand, const std::uint64_t sequence,
    const std::uint64_t now) noexcept {
    if (box == nullptr) return false;
    if (!valid_clock(*box, now)) { disarm(box); return false; }
    const auto step_index = static_cast<unsigned>(step);
    const auto hand_index = static_cast<unsigned>(hand);
    if (step_index == 0 || step_index > 6 || hand_index > 1 || sequence == 0 ||
        sequence < box->last_action_sequence) return false;
    if (sequence != box->last_action_sequence) {
        box->last_action_sequence = sequence;
        box->seen_steps = 0;
    }
    const std::uint32_t bit = 1U << (step_index * 2U + hand_index);
    if ((box->seen_steps & bit) != 0) return false;
    // Mark even a capacity drop: it must not become a delayed retry.
    box->seen_steps |= bit;
    if (box->pending.count == box->pending.cues.size()) return false;
    box->pending.cues[box->pending.count++] = {step, hand, sequence, now, ++box->next_serial};
    return true;
}

ReloadFeedbackBatch consume_reload_feedback_events(
    ReloadFeedbackMailbox* const box, const std::uint64_t now) noexcept {
    ReloadFeedbackBatch result{};
    if (box == nullptr) return result;
    if (!valid_clock(*box, now)) { disarm(box); return result; }
    for (std::size_t index = 0; index < box->pending.count; ++index) {
        const auto& cue = box->pending.cues[index];
        if (now >= cue.milliseconds &&
            now - cue.milliseconds <= kReloadFeedbackMaxAgeMilliseconds) {
            result.cues[result.count++] = cue;
        }
    }
    box->pending = {};
    return result;
}

ReloadFeedbackSamples make_reload_feedback_samples(const ReloadFeedbackStep step) noexcept {
    ReloadFeedbackSamples samples{};
    if (step == ReloadFeedbackStep::None || reload_feedback_amplitude(step) == 0.0F) return samples;
    const auto kind = static_cast<std::uint32_t>(step);
    std::uint32_t noise = 0x49C17U + kind;
    const float frequency = 820.0F + static_cast<float>(kind) * 137.0F;
    const float second_click = step == ReloadFeedbackStep::Grabbed ? 0.0F : 0.55F;
    const float loudness = step == ReloadFeedbackStep::Grabbed ? 2100.0F : 3600.0F;
    for (std::size_t index = 1; index + 1 < samples.size(); ++index) {
        const float seconds = static_cast<float>(index) / static_cast<float>(kReloadFeedbackSampleRate);
        const float envelope = std::exp(-seconds * 100.0F) +
            (seconds >= 0.019F ? second_click * std::exp(-(seconds - 0.019F) * 130.0F) : 0.0F);
        const float attack = std::min(seconds * 2000.0F, 1.0F);
        const float tail = std::min((0.080F - seconds) * 400.0F, 1.0F);
        noise = noise * 1664525U + 1013904223U;
        const float noise_value = static_cast<float>((noise >> 16) & 0xffffU) / 32768.0F - 1.0F;
        const float ring = std::sin(seconds * frequency * 6.283185307F);
        samples[index] = static_cast<std::int16_t>(
            loudness * attack * tail * envelope * (0.65F * noise_value + 0.35F * ring));
    }
    return samples;
}

}  // namespace wawvr::mod
