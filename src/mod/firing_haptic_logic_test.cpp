// SPDX-License-Identifier: GPL-3.0-only
#include "firing_haptic_logic.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <type_traits>

namespace {

using namespace wawvr::mod;
int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool empty(const FiringHapticMailbox& mailbox) {
    return !mailbox.accepting &&
        mailbox.acceptance_started_milliseconds == 0 &&
        mailbox.last_service_milliseconds == 0 &&
        mailbox.newest_shot_milliseconds == 0 && mailbox.pending_shots == 0;
}

void test_no_event_and_once_only_drain() {
    FiringHapticMailbox mailbox{};
    expect(!queue_firing_haptic_event(&mailbox, 1'000),
           "unarmed mailbox rejects a shot");
    expect(consume_firing_haptic_events(&mailbox, 1'000) == 0,
           "unarmed mailbox creates no pulse");
    set_firing_haptic_acceptance(&mailbox, true, 1'000);
    expect(consume_firing_haptic_events(&mailbox, 1'000) == 0,
           "active gameplay without a confirmed shot creates no pulse");
    expect(queue_firing_haptic_event(&mailbox, 1'000),
           "confirmed shot in first service frame is accepted");
    expect(consume_firing_haptic_events(&mailbox, 1'001) == 1,
           "one shot produces one drainable event");
    expect(consume_firing_haptic_events(&mailbox, 1'002) == 0,
           "consumed event cannot replay next frame");
    expect(mailbox.newest_shot_milliseconds == 0 &&
               mailbox.pending_shots == 0,
           "draining removes all pending state");
}

void test_autofire_and_bounded_coalescing() {
    FiringHapticMailbox mailbox{};
    set_firing_haptic_acceptance(&mailbox, true, 2'000);
    for (std::uint64_t sample = 0; sample < 12; ++sample) {
        const auto now = 2'000 + sample * 50;
        set_firing_haptic_acceptance(&mailbox, true, now);
        expect(queue_firing_haptic_event(&mailbox, now),
               "each later confirmed autofire shot is accepted");
        expect(consume_firing_haptic_events(&mailbox, now) == 1,
               "each autofire sample can produce its own pulse");
        expect(consume_firing_haptic_events(&mailbox, now) == 0,
               "autofire cannot duplicate a drained shot");
    }
    set_firing_haptic_acceptance(&mailbox, true, 2'600);
    for (int shot = 0; shot < 100; ++shot) {
        expect(queue_firing_haptic_event(&mailbox, 2'600),
               "same-frame events coalesce without rejecting valid fire");
    }
    expect(mailbox.pending_shots == 32,
           "same-frame pending count is capped at 32");
    expect(consume_firing_haptic_events(&mailbox, 2'601) == 32,
           "bounded same-frame batch drains once");
    expect(consume_firing_haptic_events(&mailbox, 2'601) == 0,
           "excess same-frame events never spill into another pulse");
}

void test_freshness_boundaries() {
    FiringHapticMailbox mailbox{};
    set_firing_haptic_acceptance(&mailbox, true, 3'000);
    expect(queue_firing_haptic_event(&mailbox, 3'100),
           "event exactly 100 ms after service remains fresh");
    expect(consume_firing_haptic_events(&mailbox, 3'100) == 1,
           "boundary event drains while heartbeat remains fresh");
    expect(!queue_firing_haptic_event(&mailbox, 3'101) && empty(mailbox),
           "stale heartbeat rejects firing and disarms");

    set_firing_haptic_acceptance(&mailbox, true, 4'000);
    expect(queue_firing_haptic_event(&mailbox, 4'000),
           "fresh shot queues before delayed consumption");
    expect(consume_firing_haptic_events(&mailbox, 4'101) == 0 && empty(mailbox),
           "late consumption discards the stale event and disarms");
    set_firing_haptic_acceptance(&mailbox, true, 4'102);
    expect(consume_firing_haptic_events(&mailbox, 4'102) == 0,
           "rearming after stale consumption cannot replay");

    set_firing_haptic_acceptance(&mailbox, true, 5'000);
    expect(queue_firing_haptic_event(&mailbox, 5'000),
           "event queues before an unserviced gap");
    set_firing_haptic_acceptance(&mailbox, true, 5'101);
    expect(mailbox.accepting &&
               mailbox.acceptance_started_milliseconds == 5'101 &&
               consume_firing_haptic_events(&mailbox, 5'101) == 0,
           "service after a long gap starts a clean epoch");
    expect(!queue_firing_haptic_event(&mailbox, 5'100),
           "delayed callback from before the new epoch is rejected");

    set_firing_haptic_acceptance(&mailbox, true, 6'000);
    expect(queue_firing_haptic_event(&mailbox, 6'000),
           "event queues before repeated heartbeat refreshes");
    set_firing_haptic_acceptance(&mailbox, true, 6'050);
    set_firing_haptic_acceptance(&mailbox, true, 6'100);
    set_firing_haptic_acceptance(&mailbox, true, 6'101);
    expect(consume_firing_haptic_events(&mailbox, 6'101) == 0,
           "fresh heartbeat cannot revive an expired shot");
}

void test_disarm_and_session_boundaries() {
    FiringHapticMailbox mailbox{};
    for (std::uint64_t reason = 0; reason < 4; ++reason) {
        const auto now = 7'000 + reason * 200;
        set_firing_haptic_acceptance(&mailbox, true, now);
        expect(queue_firing_haptic_event(&mailbox, now),
               "active gameplay queues an event before permission loss");
        set_firing_haptic_acceptance(&mailbox, false, now + 1);
        expect(empty(mailbox),
               "menu, focus, session, or tracking loss clears the mailbox");
        expect(!queue_firing_haptic_event(&mailbox, now + 2),
               "events cannot queue while gameplay permission is absent");
        set_firing_haptic_acceptance(&mailbox, true, now + 3);
        expect(consume_firing_haptic_events(&mailbox, now + 3) == 0,
               "regaining gameplay permission cannot replay old shots");
    }
    expect(queue_firing_haptic_event(&mailbox, 7'604),
           "mailbox accepts a new shot after a fresh session epoch");
    reset_firing_haptic_mailbox(&mailbox);
    expect(empty(mailbox), "explicit shutdown reset removes every field");
}

void test_invalid_and_regressing_clocks() {
    FiringHapticMailbox mailbox{};
    set_firing_haptic_acceptance(&mailbox, true, 0);
    expect(empty(mailbox), "zero service time cannot arm the mailbox");
    set_firing_haptic_acceptance(&mailbox, true, 8'000);
    expect(!queue_firing_haptic_event(&mailbox, 0) && empty(mailbox),
           "zero event time disarms without a pulse");
    set_firing_haptic_acceptance(&mailbox, true, 8'000);
    expect(queue_firing_haptic_event(&mailbox, 8'010),
           "event after heartbeat is valid");
    set_firing_haptic_acceptance(&mailbox, true, 8'009);
    expect(empty(mailbox),
           "service older than pending shot rejects future-clock state");

    set_firing_haptic_acceptance(&mailbox, true, 8'100);
    expect(queue_firing_haptic_event(&mailbox, 8'110),
           "fresh shot queues for consume regression test");
    expect(consume_firing_haptic_events(&mailbox, 8'109) == 0 && empty(mailbox),
           "consumption cannot replay a future-dated shot");

    set_firing_haptic_acceptance(&mailbox, true, 8'200);
    set_firing_haptic_acceptance(&mailbox, true, 8'210);
    expect(!queue_firing_haptic_event(&mailbox, 8'209) && empty(mailbox),
           "event older than last heartbeat is rejected");
    set_firing_haptic_acceptance(&mailbox, true, 8'300);
    set_firing_haptic_acceptance(&mailbox, true, 8'299);
    expect(empty(mailbox), "regressing service time disarms");

    constexpr auto last = std::numeric_limits<std::uint64_t>::max();
    set_firing_haptic_acceptance(&mailbox, true, last - 100);
    expect(queue_firing_haptic_event(&mailbox, last),
           "freshness arithmetic is safe near maximum timestamp");
    expect(consume_firing_haptic_events(&mailbox, last) == 1,
           "maximum timestamp drains without overflow");
    set_firing_haptic_acceptance(&mailbox, true, 1);
    expect(empty(mailbox), "clock wrap is rejected as regression");
}

void test_null_and_policy_constants() {
    set_firing_haptic_acceptance(nullptr, true, 9'000);
    reset_firing_haptic_mailbox(nullptr);
    expect(!queue_firing_haptic_event(nullptr, 9'000),
           "null mailbox cannot queue an event");
    expect(consume_firing_haptic_events(nullptr, 9'000) == 0,
           "null mailbox cannot produce a pulse");
    expect(kFiringHapticAmplitude == 0.78F &&
               kFiringHapticDurationSeconds == 0.050F &&
               kFiringHapticMaxAgeMilliseconds == 100,
           "firing pulse has the requested bounded strength and duration");
}

static_assert(std::is_trivially_copyable_v<FiringHapticMailbox>);
static_assert(std::is_standard_layout_v<FiringHapticMailbox>);

}  // namespace

int main() {
    test_no_event_and_once_only_drain();
    test_autofire_and_bounded_coalescing();
    test_freshness_boundaries();
    test_disarm_and_session_boundaries();
    test_invalid_and_regressing_clocks();
    test_null_and_policy_constants();
    if (failures != 0) {
        std::cerr << failures << " firing haptic logic checks failed\n";
        return 1;
    }
    std::cout << "Firing haptic logic checks passed\n";
    return 0;
}
