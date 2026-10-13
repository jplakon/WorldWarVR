// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>

namespace wawvr::mod {

inline constexpr float kFiringHapticAmplitude = 0.78F;
inline constexpr float kFiringHapticDurationSeconds = 0.050F;
inline constexpr std::uint64_t kFiringHapticMaxAgeMilliseconds = 100;

// This policy owns no locks or runtime handles. The caller serializes every
// operation and supplies monotonic milliseconds from the same clock. Only
// confirmed local firing events belong here, never a held trigger alone.
struct FiringHapticMailbox final {
    bool accepting{};
    std::uint64_t acceptance_started_milliseconds{};
    std::uint64_t last_service_milliseconds{};
    std::uint64_t newest_shot_milliseconds{};
    std::uint32_t pending_shots{};
};

// Service from an active, focused gameplay frame. False permission, invalid
// time, and clock regression discard all pending events and disarm. A service
// gap exceeding the freshness bound starts a new epoch without replay.
void set_firing_haptic_acceptance(
    FiringHapticMailbox* mailbox, bool allowed,
    std::uint64_t now_milliseconds) noexcept;

// Accept only while a recent service heartbeat has armed the mailbox. Events
// coalesce into one bounded batch (at most 32), including same-frame events.
// A true result means accepted, not that a pulse has already been submitted.
[[nodiscard]] bool queue_firing_haptic_event(
    FiringHapticMailbox* mailbox, std::uint64_t now_milliseconds) noexcept;

// Drain the fresh batch once; zero shots never creates a pulse. This does not
// renew the permission heartbeat: the caller must service acceptance first.
[[nodiscard]] std::uint32_t consume_firing_haptic_events(
    FiringHapticMailbox* mailbox, std::uint64_t now_milliseconds) noexcept;

void reset_firing_haptic_mailbox(FiringHapticMailbox* mailbox) noexcept;

}  // namespace wawvr::mod
