// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "gameplay/manual_reload_logic.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace wawvr::mod {

enum class ReloadFeedbackStep : std::uint8_t {
    None, Ejected, Grabbed, Inserted, ActionOpened, ActionClosed, ChargeReady,
    PouchNearby,
};
enum class ReloadFeedbackHand : std::uint8_t { Left, Right };

struct ReloadFeedbackCue final {
    ReloadFeedbackStep step{};
    ReloadFeedbackHand hand{};
    std::uint64_t action_sequence{};
    std::uint64_t milliseconds{};
    std::uint64_t serial{};
};

inline constexpr std::uint64_t kReloadFeedbackMaxAgeMilliseconds = 100;
inline constexpr std::size_t kReloadFeedbackBatchCapacity = 8;
struct ReloadFeedbackBatch final {
    std::array<ReloadFeedbackCue, kReloadFeedbackBatchCapacity> cues{};
    std::size_t count{};
};
struct ReloadFeedbackMailbox final {
    bool accepting{};
    std::uint64_t last_service_milliseconds{};
    std::uint64_t last_action_sequence{};
    std::uint32_t seen_steps{};
    std::uint64_t next_serial{};
    ReloadFeedbackBatch pending{};
};

// The state machines emit transitions once. Insertion feedback belongs only
// to acknowledged native completion, never a request or a retry heartbeat.
[[nodiscard]] ReloadFeedbackStep reload_feedback_step(
    gameplay::ReloadEvent event, gameplay::ReloadProfileKind profile) noexcept;
[[nodiscard]] const char* reload_feedback_step_name(ReloadFeedbackStep step) noexcept;
[[nodiscard]] float reload_feedback_amplitude(ReloadFeedbackStep step) noexcept;
[[nodiscard]] float reload_feedback_duration_seconds(ReloadFeedbackStep step) noexcept;

// Caller serializes these operations. Consumed cues remain deduplicated for
// their action sequence, including repeated eye/render callbacks. Pending
// work expires rather than becoming feedback after a hitch or focus recovery.
void set_reload_feedback_acceptance(ReloadFeedbackMailbox* mailbox, bool allowed,
                                   std::uint64_t now) noexcept;
void clear_reload_feedback_pending(ReloadFeedbackMailbox* mailbox) noexcept;
[[nodiscard]] bool queue_reload_feedback_event(
    ReloadFeedbackMailbox* mailbox, ReloadFeedbackStep step,
    ReloadFeedbackHand hand, std::uint64_t sequence, std::uint64_t now) noexcept;
[[nodiscard]] ReloadFeedbackBatch consume_reload_feedback_events(
    ReloadFeedbackMailbox* mailbox, std::uint64_t now) noexcept;
// Discovery is dispensable. Accepted mechanical transitions and an existing
// mechanical/firing pulse always win, independent of batch ordering.
[[nodiscard]] bool reload_feedback_priority_allows(
    const ReloadFeedbackCue& cue, const ReloadFeedbackBatch& batch,
    bool owned_pulse_active, bool firing_pulse_active) noexcept;

// Original procedural mechanical clicks; no game or third-party recording.
// Fixed 80 ms mono PCM with bounded volume and silence at both boundaries.
inline constexpr std::uint32_t kReloadFeedbackSampleRate = 24000;
inline constexpr std::size_t kReloadFeedbackSampleCount = 1920;
using ReloadFeedbackSamples = std::array<std::int16_t, kReloadFeedbackSampleCount>;
[[nodiscard]] ReloadFeedbackSamples make_reload_feedback_samples(
    ReloadFeedbackStep step) noexcept;

}  // namespace wawvr::mod
