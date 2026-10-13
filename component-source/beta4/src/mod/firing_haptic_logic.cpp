// SPDX-License-Identifier: GPL-3.0-only
#include "firing_haptic_logic.hpp"

namespace wawvr::mod {
namespace {

constexpr std::uint32_t kMaximumPendingShots = 32;

bool valid_armed_clock(const FiringHapticMailbox& mailbox,
                       const std::uint64_t now_milliseconds) noexcept {
    return mailbox.accepting && now_milliseconds != 0 &&
        mailbox.acceptance_started_milliseconds != 0 &&
        mailbox.last_service_milliseconds >=
            mailbox.acceptance_started_milliseconds &&
        now_milliseconds >= mailbox.last_service_milliseconds &&
        (mailbox.pending_shots == 0 ||
         (mailbox.newest_shot_milliseconds >=
              mailbox.acceptance_started_milliseconds &&
          now_milliseconds >= mailbox.newest_shot_milliseconds));
}

void clear_pending(FiringHapticMailbox* const mailbox) noexcept {
    mailbox->newest_shot_milliseconds = 0;
    mailbox->pending_shots = 0;
}

}  // namespace

void reset_firing_haptic_mailbox(FiringHapticMailbox* const mailbox) noexcept {
    if (mailbox != nullptr) {
        *mailbox = {};
    }
}

void set_firing_haptic_acceptance(
    FiringHapticMailbox* const mailbox, const bool allowed,
    const std::uint64_t now_milliseconds) noexcept {
    if (mailbox == nullptr) {
        return;
    }
    if (!allowed || now_milliseconds == 0 ||
        (mailbox->accepting &&
         !valid_armed_clock(*mailbox, now_milliseconds))) {
        reset_firing_haptic_mailbox(mailbox);
        return;
    }

    if (!mailbox->accepting ||
        now_milliseconds - mailbox->last_service_milliseconds >
            kFiringHapticMaxAgeMilliseconds) {
        reset_firing_haptic_mailbox(mailbox);
        mailbox->accepting = true;
        mailbox->acceptance_started_milliseconds = now_milliseconds;
    } else if (mailbox->pending_shots != 0 &&
               now_milliseconds - mailbox->newest_shot_milliseconds >
                   kFiringHapticMaxAgeMilliseconds) {
        clear_pending(mailbox);
    }
    mailbox->last_service_milliseconds = now_milliseconds;
}

bool queue_firing_haptic_event(
    FiringHapticMailbox* const mailbox,
    const std::uint64_t now_milliseconds) noexcept {
    if (mailbox == nullptr) {
        return false;
    }
    if (!valid_armed_clock(*mailbox, now_milliseconds) ||
        now_milliseconds - mailbox->last_service_milliseconds >
            kFiringHapticMaxAgeMilliseconds) {
        reset_firing_haptic_mailbox(mailbox);
        return false;
    }

    if (mailbox->pending_shots != 0 &&
        now_milliseconds - mailbox->newest_shot_milliseconds >
            kFiringHapticMaxAgeMilliseconds) {
        clear_pending(mailbox);
    }
    if (mailbox->pending_shots < kMaximumPendingShots) {
        ++mailbox->pending_shots;
    }
    mailbox->newest_shot_milliseconds = now_milliseconds;
    return true;
}

std::uint32_t consume_firing_haptic_events(
    FiringHapticMailbox* const mailbox,
    const std::uint64_t now_milliseconds) noexcept {
    if (mailbox == nullptr) {
        return 0;
    }
    if (!valid_armed_clock(*mailbox, now_milliseconds) ||
        now_milliseconds - mailbox->last_service_milliseconds >
            kFiringHapticMaxAgeMilliseconds) {
        reset_firing_haptic_mailbox(mailbox);
        return 0;
    }

    const auto pending = mailbox->pending_shots;
    const bool fresh = pending != 0 &&
        now_milliseconds - mailbox->newest_shot_milliseconds <=
            kFiringHapticMaxAgeMilliseconds;
    clear_pending(mailbox);
    return fresh ? (pending < kMaximumPendingShots ? pending
                                                   : kMaximumPendingShots)
                 : 0;
}

}  // namespace wawvr::mod
