// SPDX-License-Identifier: GPL-3.0-only
#include "grenade_native_observation.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

using namespace wawvr::mod;

int failures = 0;
std::uint64_t pair_observations = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void expect_no_ownership_or_drain(
    const GrenadeNativeQueueObservation& observation) {
    expect(!observation.eventOwnershipEstablished &&
        !observation.queueDrainEstablished,
        "raw slot observations never establish ownership or queue drain");
}

void test_every_eight_bit_producer_ordinal_pair() {
    for (const auto expected : std::array<std::uint32_t, 2>{39U, 40U}) {
        for (std::uint32_t producer = 0; producer < 256U; ++producer) {
            for (std::uint32_t ordinal = 0; ordinal < 256U; ++ordinal) {
                GrenadeNativeQueueSnapshot snapshot{
                    true, producer, {expected, expected, expected, expected},
                    {0x11223344U, 0x22334455U, 0x33445566U, 0x44556677U}};
                const auto before = snapshot;
                const auto observed = observe_grenade_native_queue(
                    snapshot, ordinal, expected);
                ++pair_observations;
                const auto distance = (producer + 256U - ordinal) & 255U;
                expect(observed.ordinalRaw == ordinal &&
                    observed.producerRaw == producer &&
                    observed.moduloDistance == distance,
                    "all eight-bit pairs retain raw values and modulo distance");
                expect(observed.boundedSlotMatches ==
                    (distance >= 1U && distance <= 4U),
                    "only one through four queued slots qualify descriptively");
                expect(observed.eventAtSlot == expected &&
                    observed.parameterAtSlot == snapshot.parameters[ordinal & 3U],
                    "the exact ordinal slot retains its event and raw parameter");
                expect(snapshot.readable == before.readable &&
                    snapshot.producer == before.producer &&
                    snapshot.events == before.events &&
                    snapshot.parameters == before.parameters,
                    "observation leaves the supplied snapshot unchanged");
                expect_no_ownership_or_drain(observed);
            }
        }
    }
}

void test_wrap_overflow_and_exact_slot() {
    GrenadeNativeQueueSnapshot snapshot{
        true, 0U, {40U, 40U, 40U, 39U}, {10U, 20U, 30U, 0xFFFFFFFFU}};
    auto observed = observe_grenade_native_queue(snapshot, 255U, 39U);
    expect(observed.boundedSlotMatches && observed.moduloDistance == 1U &&
        observed.ordinalRaw == 255U && observed.producerRaw == 0U &&
        observed.eventAtSlot == 39U && observed.parameterAtSlot == 0xFFFFFFFFU,
        "255-to-zero wrap keeps its raw values without inventing ownership");
    expect_no_ownership_or_drain(observed);
    snapshot.producer = 3U;
    observed = observe_grenade_native_queue(snapshot, 255U, 39U);
    expect(observed.boundedSlotMatches && observed.moduloDistance == 4U,
        "four remaining ring entries are the inclusive descriptive bound");
    expect_no_ownership_or_drain(observed);
    snapshot.producer = 4U;
    observed = observe_grenade_native_queue(snapshot, 255U, 39U);
    expect(!observed.boundedSlotMatches && observed.moduloDistance == 5U,
        "four-entry overflow remains unknown rather than accepting the slot");
    expect_no_ownership_or_drain(observed);
    snapshot.producer = 255U;
    observed = observe_grenade_native_queue(snapshot, 255U, 39U);
    expect(!observed.boundedSlotMatches && observed.moduloDistance == 0U,
        "equal wrapped ordinals cannot prove either emptiness or ownership");
    expect_no_ownership_or_drain(observed);
    snapshot.producer = 0U;
    observed = observe_grenade_native_queue(snapshot, 255U, 40U);
    expect(!observed.boundedSlotMatches && observed.eventAtSlot == 39U,
        "an expected event elsewhere in the ring cannot replace the exact slot");
    expect_no_ownership_or_drain(observed);
}

void test_unreadable_invalid_event_and_wide_raw_ordinals() {
    GrenadeNativeQueueSnapshot snapshot{
        false, 1U, {39U, 40U, 39U, 40U}, {1U, 2U, 3U, 4U}};
    auto observed = observe_grenade_native_queue(snapshot, 0U, 39U);
    expect(!observed.boundedSlotMatches && observed.producerRaw == 1U &&
        observed.ordinalRaw == 0U && observed.eventAtSlot == 39U &&
        observed.parameterAtSlot == 1U,
        "unreadable snapshot retains descriptive raw fields but cannot qualify");
    expect_no_ownership_or_drain(observed);
    snapshot.readable = true;
    for (const auto expected : std::array<std::uint32_t, 6>{
             0U, 1U, 38U, 41U, 256U,
             (std::numeric_limits<std::uint32_t>::max)()}) {
        snapshot.events[0] = expected;
        observed = observe_grenade_native_queue(snapshot, 0U, expected);
        expect(!observed.boundedSlotMatches && observed.eventAtSlot == expected,
            "only exact event39 or event40 IDs can qualify a bounded slot");
        expect_no_ownership_or_drain(observed);
    }
    snapshot.events.fill(39U);
    for (const auto producer : std::array<std::uint32_t, 3>{
             256U, 257U, (std::numeric_limits<std::uint32_t>::max)()}) {
        snapshot.producer = producer;
        observed = observe_grenade_native_queue(snapshot, 0U, 39U);
        expect(!observed.boundedSlotMatches && observed.producerRaw == producer,
            "wide producer cannot become valid by masking its raw high bits");
        expect_no_ownership_or_drain(observed);
    }
    snapshot.producer = 1U;
    for (const auto ordinal : std::array<std::uint32_t, 3>{
             256U, 257U, (std::numeric_limits<std::uint32_t>::max)()}) {
        observed = observe_grenade_native_queue(snapshot, ordinal, 39U);
        expect(!observed.boundedSlotMatches && observed.ordinalRaw == ordinal &&
            observed.parameterAtSlot == snapshot.parameters[ordinal & 3U],
            "wide ordinal remains raw and unknown despite a safe masked index");
        expect_no_ownership_or_drain(observed);
    }
    observed = observe_grenade_native_queue({}, 0U, 39U);
    expect(!observed.boundedSlotMatches && observed.ordinalRaw == 0U &&
        observed.producerRaw == 0U && observed.eventAtSlot == 0U &&
        observed.parameterAtSlot == 0U,
        "missing snapshot defaults to unknown, not an empty-queue witness");
    expect_no_ownership_or_drain(observed);
}

void test_exact_allocation_release_and_stale_slot_refusal() {
    const GrenadeNativeAllocationSnapshot before{true, 0x12340000, 4, 37, 1, 14};
    const GrenadeNativeAllocationSnapshot after{true, 0x12340000, 0, 38, 0, 0};
    expect(observe_grenade_native_allocation_release(before, after),
        "exact allocation1-to-zero with one native free-counter advance observed");
    for (std::uint32_t change = 0; change < 10; ++change) {
        auto a = before; auto b = after;
        switch (change) {
        case 0: a.readable = false; break;
        case 1: b.readable = false; break;
        case 2: a.address = b.address = 0; break;
        case 3: ++b.address; break;
        case 4: a.type = 1; break;
        case 5: a.handler = 13; break;
        case 6: a.allocated11D = 0; break;
        case 7: b.allocated11D = 1; break;
        case 8: b.incarnation = a.incarnation; break;
        case 9: b.incarnation = a.incarnation + 2; break;
        }
        expect(!observe_grenade_native_allocation_release(a, b),
            "missing/foreign/not-allocated/unchanged/reused slot never qualifies");
    }
    auto edge = before; auto wrapped = after;
    edge.incarnation = (std::numeric_limits<std::uint32_t>::max)();
    wrapped.incarnation = 0;
    expect(!observe_grenade_native_allocation_release(edge, wrapped),
        "counter wrap does not invent an unwrapped source identity");
}

}  // namespace

int main() {
    test_every_eight_bit_producer_ordinal_pair();
    test_wrap_overflow_and_exact_slot();
    test_unreadable_invalid_event_and_wide_raw_ordinals();
    test_exact_allocation_release_and_stale_slot_refusal();
    if (failures == 0) {
        std::cout << "PASS: " << pair_observations
                  << " exhaustive event39/event40 modulo-pair observations; "
                     "wrap/overflow/missing/invalid inputs remain diagnostic only; "
                     "ownership=0 queueDrain=0\n";
    }
    return failures == 0 ? 0 : 1;
}
