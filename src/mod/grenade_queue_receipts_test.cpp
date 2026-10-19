// SPDX-License-Identifier: GPL-3.0-only
#include "grenade_queue_receipts.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

using namespace wawvr::mod;

int failures = 0;
std::uint64_t writerCases = 0;
std::uint64_t dispatcherCases = 0;
std::uint64_t batchCases = 0;
std::uint64_t lineageCases = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

GrenadeNativeQueueSnapshot snapshot(const std::uint32_t producer) {
    return {true, producer, {0x12345678U, 0x23456789U, 0x3456789AU, 0x456789ABU},
        {0x87654321U, 0x98765432U, 0xA9876543U, 0xBA987654U}};
}

GrenadeNativeQueueSnapshot simulated_write(const GrenadeNativeQueueSnapshot& before,
    const std::uint32_t event, const std::uint32_t parameter) {
    auto after = before;
    after.events[before.producer & 3U] = event & 0xFFU;
    after.parameters[before.producer & 3U] = parameter & 0xFFFFU;
    after.producer = (before.producer + 1U) & 0xFFU;
    return after;
}

void no_authorization(const GrenadeNativeEnqueueObservation& result) {
    expect(!result.eventOwnershipEstablished &&
        !result.transactionOwnershipEstablished && !result.queueDrainEstablished,
        "completed writer observation never establishes transaction or drain ownership");
}

void no_authorization(const GrenadeNativeDispatchObservation& result) {
    expect(!result.nativeCounterWrapResolved && !result.eventOwnershipEstablished &&
        !result.transactionOwnershipEstablished && !result.predictionDrainEstablished &&
        !result.queueDrainEstablished,
        "completed dispatcher interval never establishes wrap, prediction or drain authorization");
}

void no_authorization(const GrenadeNativeBatchObservation& result) {
    expect(!result.eventOwnershipEstablished &&
        !result.transactionOwnershipEstablished && !result.queueDrainEstablished,
        "expected inline batches never establish transaction or drain authorization");
}

void no_authorization(const GrenadeNativeEnqueueLineageObservation& result) {
    expect(!result.eventOwnershipEstablished &&
        !result.transactionOwnershipEstablished && !result.predictionDrainEstablished &&
        !result.queueDrainEstablished,
        "enclosing event40 context is diagnostic topology, never native authorization");
}

void test_every_writer_counter_and_normalized_inputs() {
    constexpr std::array<std::uint32_t, 6> events{
        0U, 39U, 40U, 0xFFFFFF27U, 0x12340028U, 0xFFFFFFFFU};
    constexpr std::array<std::uint32_t, 6> parameters{
        0U, 1U, 0xFFFFU, 0x10000U, 0xABCD1234U, 0xFFFFFFFFU};
    for (std::uint32_t counter = 0; counter < 256U; ++counter) {
        for (const auto event : events) {
            for (const auto parameter : parameters) {
                const auto before = snapshot(counter);
                const auto after = simulated_write(before, event, parameter);
                const auto result = validate_completed_grenade_native_enqueue(
                    before, after, event, parameter);
                ++writerCases;
                expect(result.completedNativeWrite && result.ordinalRaw == counter &&
                    result.nextProducerRaw == ((counter + 1U) & 255U) &&
                    result.expectedNextProducer == result.nextProducerRaw &&
                    result.slot == (counter & 3U),
                    "all 256 producer counters have exactly one bounded normalized write");
                expect(result.eventRaw == event && result.parameterRaw == parameter &&
                    result.eventNormalized == (event & 255U) &&
                    result.parameterNormalized == (parameter & 65535U),
                    "raw CL/stack-WORD inputs are retained separately from normalization");
                no_authorization(result);
            }
        }
    }
}

void test_writer_requires_every_ring_slot_and_complete_snapshots() {
    const auto before = snapshot(255U);
    const auto after = simulated_write(before, 0x10027U, 0xABCDEFFFU);
    auto observed = validate_completed_grenade_native_enqueue(
        before, after, 0x10027U, 0xABCDEFFFU);
    expect(observed.completedNativeWrite && observed.ordinalRaw == 255U &&
        observed.nextProducerRaw == 0U && observed.slot == 3U,
        "verified writer normalization includes the exact 255-to-zero wrap");
    no_authorization(observed);
    for (std::size_t slot = 0; slot < 4U; ++slot) {
        auto changed = after;
        changed.events[slot] ^= 1U;
        observed = validate_completed_grenade_native_enqueue(
            before, changed, 0x10027U, 0xABCDEFFFU);
        expect(!observed.completedNativeWrite,
            "modified target or untouched event slot cannot qualify a writer receipt");
        no_authorization(observed);
        changed = after;
        changed.parameters[slot] ^= 1U;
        observed = validate_completed_grenade_native_enqueue(
            before, changed, 0x10027U, 0xABCDEFFFU);
        expect(!observed.completedNativeWrite,
            "modified target or untouched parameter slot cannot qualify a writer receipt");
        no_authorization(observed);
    }
    auto missing = before;
    missing.readable = false;
    expect(!validate_completed_grenade_native_enqueue(missing, after,
        0x10027U, 0xABCDEFFFU).completedNativeWrite,
        "missing before snapshot cannot qualify completed native writing");
    missing = after;
    missing.readable = false;
    expect(!validate_completed_grenade_native_enqueue(before, missing,
        0x10027U, 0xABCDEFFFU).completedNativeWrite,
        "missing after snapshot cannot qualify completed native writing");
    auto changed = after;
    changed.producer = 1U;
    expect(!validate_completed_grenade_native_enqueue(before, changed,
        0x10027U, 0xABCDEFFFU).completedNativeWrite,
        "wrong increment cannot substitute a completed native write");
    for (const auto wide : std::array<std::uint32_t, 3>{256U, 257U, 0xFFFFFFFFU}) {
        auto invalid = before;
        invalid.producer = wide;
        observed = validate_completed_grenade_native_enqueue(invalid, after,
            0x10027U, 0xABCDEFFFU);
        expect(!observed.completedNativeWrite && observed.ordinalRaw == wide,
            "wide writer producer is preserved but never masked into validity");
        no_authorization(observed);
        invalid = after;
        invalid.producer = wide;
        observed = validate_completed_grenade_native_enqueue(before, invalid,
            0x10027U, 0xABCDEFFFU);
        expect(!observed.completedNativeWrite && observed.nextProducerRaw == wide,
            "wide completed producer remains unsupported raw evidence");
        no_authorization(observed);
    }
}

void test_all_nonnegative_dispatcher_intervals() {
    for (std::uint32_t producer = 0; producer < 256U; ++producer) {
        const auto before = snapshot(producer);
        for (std::uint32_t start = 0; start < 256U; ++start) {
            const auto result = validate_completed_grenade_native_dispatch(
                before, before, start, producer);
            ++dispatcherCases;
            const bool expected = start <= producer && producer - start <= 4U;
            expect(result.observedDrainInterval == expected &&
                result.producerBeforeRaw == producer &&
                result.producerAfterRaw == producer &&
                result.startOrdinalRaw == start &&
                result.completedOrdinalRaw == producer,
                "all nonnegative dispatcher intervals use a checked four-entry bound");
            expect(result.observedEventCount == (expected ? producer - start : 0U),
                "event count is descriptive only for a completely observed interval");
            no_authorization(result);
        }
    }
}

void test_dispatcher_missing_mutated_wrap_and_signed_input() {
    const auto before = snapshot(7U);
    auto observed = validate_completed_grenade_native_dispatch(before, before, 3U, 7U);
    expect(observed.observedDrainInterval && observed.observedEventCount == 4U,
        "complete unchanged four-entry dispatcher interval is observable");
    no_authorization(observed);
    for (std::size_t slot = 0; slot < 4U; ++slot) {
        auto after = before;
        after.events[slot] ^= 1U;
        expect(!validate_completed_grenade_native_dispatch(before, after,
            3U, 7U).observedDrainInterval,
            "any event-ring mutation denies a stable dispatcher interval");
        after = before;
        after.parameters[slot] ^= 1U;
        expect(!validate_completed_grenade_native_dispatch(before, after,
            3U, 7U).observedDrainInterval,
            "any parameter-ring mutation denies a stable dispatcher interval");
    }
    auto after = before;
    after.producer = 8U;
    expect(!validate_completed_grenade_native_dispatch(before, after,
        3U, 8U).observedDrainInterval,
        "producer movement during dispatch is not an observed stable drain");
    expect(!validate_completed_grenade_native_dispatch(snapshot(255U), snapshot(0U),
        252U, 0U).observedDrainInterval,
        "dispatcher producer wrap is unsupported rather than guessed as completed drain");
    auto missing = before;
    missing.readable = false;
    expect(!validate_completed_grenade_native_dispatch(missing, before,
        3U, 7U).observedDrainInterval &&
        !validate_completed_grenade_native_dispatch(before, missing,
            3U, 7U).observedDrainInterval,
        "both complete dispatcher snapshots are required");
    expect(!validate_completed_grenade_native_dispatch(before, before,
        3U, 6U).observedDrainInterval,
        "epilogue-local completion must equal the stable producer");
    for (const auto raw : std::array<std::uint32_t, 5>{
             256U, 0xFFFFFFFFU, 0xFFFFFFFEU, 0xFFFFFFFCU, 0x80000000U}) {
        // Negative native signed starts can be legitimate around wrap. This
        // intentionally conservative subset must not interpret their epoch.
        observed = validate_completed_grenade_native_dispatch(
            snapshot(0U), snapshot(0U), raw, 0U);
        expect(!observed.observedDrainInterval && observed.startOrdinalRaw == raw,
            "negative-bit-pattern/wide start stays unsupported without signed casts");
        no_authorization(observed);
        observed = validate_completed_grenade_native_dispatch(before, before, 3U, raw);
        expect(!observed.observedDrainInterval && observed.completedOrdinalRaw == raw,
            "wide or negative completed ordinal cannot match a low producer by masking");
        no_authorization(observed);
    }
    auto wideProducer = before;
    wideProducer.producer = 0xFFFFFFFFU;
    observed = validate_completed_grenade_native_dispatch(wideProducer, wideProducer,
        0U, 0xFFFFFFFFU);
    expect(!observed.observedDrainInterval &&
        observed.producerBeforeRaw == 0xFFFFFFFFU,
        "wide producer remains unsupported even when raw completion matches it");
    no_authorization(observed);
    observed = validate_completed_grenade_native_dispatch(
        snapshot(0U), snapshot(0U), 0U, 0U);
    expect(observed.observedDrainInterval && observed.observedEventCount == 0U,
        "zero-length unchanged interval is descriptive, not empty-queue proof");
    no_authorization(observed);
}

GrenadeNativeQueueSnapshot simulated_batch(
    GrenadeNativeQueueSnapshot before, const GrenadeNativeExpectedBatch& expected) {
    for (std::uint32_t i = 0; i < expected.count; ++i)
        before = simulated_write(before, expected.writes[i].eventRaw,
            expected.writes[i].parameterRaw);
    return before;
}

GrenadeNativeExpectedBatch expected_batch(const std::uint32_t event0,
    const std::uint32_t parameter0, const std::uint32_t event1,
    const std::uint32_t parameter1, const std::uint32_t count) {
    GrenadeNativeExpectedBatch result{};
    result.writes[0] = {event0, parameter0};
    result.writes[1] = {event1, parameter1};
    result.count = count;
    return result;
}

void test_all_inline_batch_counters_counts_and_inputs() {
    const std::array<GrenadeNativeExpectedBatch, 4> inputs{
        expected_batch(0U, 0U, 0U, 0U, 0U),
        expected_batch(38U, 0xFFFFFFFFU, 39U, 0xABCD0001U, 0U),
        expected_batch(0xFFFFFF26U, 0xABCDEFFFU, 0x10027U, 0xFFFFFFFFU, 0U),
        expected_batch(0U, 0x10000U, 0xFFFFFFFFU, 0xABCDEF01U, 0U)};
    for (std::uint32_t counter = 0; counter < 256U; ++counter) {
        for (const auto& input : inputs) {
            for (std::uint32_t count = 0; count <= 2U; ++count) {
                auto expected = input;
                expected.count = count;
                const auto before = snapshot(counter);
                const auto after = simulated_batch(before, expected);
                const auto result = validate_completed_grenade_native_batch(
                    before, after, expected);
                ++batchCases;
                expect(result.completedNativeWrite && result.ordinalRaw == counter &&
                    result.nextProducerRaw == ((counter + count) & 255U) &&
                    result.expectedNextProducer == result.nextProducerRaw &&
                    result.expectedCount == count,
                    "all 256 counters and zero/one/two expected writes validate exact batch results");
                for (std::size_t i = 0; i < 2U; ++i) {
                    expect(result.eventNormalized[i] ==
                        (expected.writes[i].eventRaw & 255U) &&
                        result.parameterNormalized[i] ==
                        (expected.writes[i].parameterRaw & 65535U),
                        "inline batch raw inputs normalize event8 and parameter16 only");
                }
                no_authorization(result);
            }
        }
    }
}

void test_inline_batch_wrap_untouched_slots_and_expected_count() {
    const auto expected = expected_batch(38U, 1U, 39U, 2U, 2U);
    const auto before = snapshot(255U);
    const auto after = simulated_batch(before, expected);
    auto result = validate_completed_grenade_native_batch(before, after, expected);
    expect(result.completedNativeWrite && result.nextProducerRaw == 1U &&
        after.events[3] == 38U && after.events[0] == 39U,
        "two expected inline writes cross 255-to-zero in their exact order");
    no_authorization(result);
    for (std::size_t slot = 0; slot < 4U; ++slot) {
        auto mutated = after;
        mutated.events[slot] ^= 1U;
        expect(!validate_completed_grenade_native_batch(before, mutated,
            expected).completedNativeWrite,
            "batch target and untouched event slots must all be exact");
        mutated = after;
        mutated.parameters[slot] ^= 1U;
        expect(!validate_completed_grenade_native_batch(before, mutated,
            expected).completedNativeWrite,
            "batch target and untouched parameter slots must all be exact");
    }
    auto changedCount = expected;
    changedCount.count = 1U;
    expect(!validate_completed_grenade_native_batch(before, after,
        changedCount).completedNativeWrite,
        "post-call two-write result cannot override a pre-derived expected count of one");
    changedCount.count = 0U;
    expect(!validate_completed_grenade_native_batch(before, after,
        changedCount).completedNativeWrite &&
        validate_completed_grenade_native_batch(before, before,
            changedCount).completedNativeWrite,
        "pre-derived count zero requires identity rather than inferred actual writes");
    for (const auto invalid : {3U, 255U, 0xFFFFFFFFU}) {
        changedCount.count = invalid;
        result = validate_completed_grenade_native_batch(before, after, changedCount);
        expect(!result.completedNativeWrite && result.expectedCount == invalid,
            "count above two is preserved raw but never indexed or inferred");
        no_authorization(result);
    }
    auto missing = before;
    missing.readable = false;
    expect(!validate_completed_grenade_native_batch(missing, after,
        expected).completedNativeWrite,
        "missing batch before snapshot remains unknown");
    missing = after;
    missing.readable = false;
    expect(!validate_completed_grenade_native_batch(before, missing,
        expected).completedNativeWrite,
        "missing batch after snapshot remains unknown");
    auto wide = before;
    wide.producer = 256U;
    expect(!validate_completed_grenade_native_batch(wide, after,
        expected).completedNativeWrite,
        "wide pre-batch producer is not normalized into validity");
    wide = after;
    wide.producer = 257U;
    expect(!validate_completed_grenade_native_batch(before, wide,
        expected).completedNativeWrite,
        "wide post-batch producer is not normalized into validity");
}

void test_initializer16_preserves_observed_pickup40_slot() {
    for (std::uint32_t counter = 0; counter < 256U; ++counter) {
        auto before = snapshot(counter);
        const auto pickup_slot = (counter + 255U) & 3U;
        before.events[pickup_slot] = 40U;
        before.parameters[pickup_slot] = 4U;
        const auto expected = expected_batch(16U, 0U, 0U, 0U, 1U);
        const auto after = simulated_batch(before, expected);
        auto observed = validate_completed_grenade_native_batch(before, after, expected);
        ++batchCases;
        expect(observed.completedNativeWrite &&
            after.events[pickup_slot] == 40U && after.parameters[pickup_slot] == 4U,
            "initializer16 completion preserves the preceding native pickup40 slot");
        no_authorization(observed);
        auto lost_pickup = after;
        lost_pickup.events[pickup_slot] = 0U;
        expect(!validate_completed_grenade_native_batch(before, lost_pickup,
            expected).completedNativeWrite,
            "missing preceding pickup40 cannot be hidden by a correct initializer16 slot");
        const auto mode_no_write = expected_batch(16U, 0U, 0U, 0U, 0U);
        observed = validate_completed_grenade_native_batch(before, before, mode_no_write);
        ++batchCases;
        expect(observed.completedNativeWrite && observed.expectedCount == 0,
            "no-write initializer branch requires the complete queue unchanged");
        no_authorization(observed);
    }
}

GrenadeNativeEnclosingEventContext enclosing_pickup() {
    return {10, 11, 12, 12, 0x176C6F0U, 0x176C6F0U, 0x1800000U,
        40, 7, 1, true};
}

void test_lineage_preserves_raw_context_without_authorization() {
    constexpr std::array<std::uint32_t, 4> ordinals{
        0U, 255U, 0xFFFFFFFEU, 0xFFFFFFFFU};
    for (std::uint32_t counter = 0; counter < 256U; ++counter) {
        const auto before = snapshot(counter);
        // Preserve the common native writer's CL normalization. The parent
        // event field is a distinct exact-handler literal, not a masked slot.
        const auto writer = validate_completed_grenade_native_enqueue(before,
            simulated_write(before, 0xABCD0028U, 5U), 0xABCD0028U, 5U);
        for (const auto ordinal : ordinals) {
            for (const auto linked : {false, true}) {
                auto parent = enclosing_pickup();
                parent.ordinal_raw = ordinal;
                if (!linked) parent.produced_receipt = 0;
                const auto observed = observe_grenade_native_enqueue_lineage(
                    parent, parent.player_state, 1U, parent.entity, writer);
                ++lineageCases;
                expect(observed.parent == parent && observed.context_captured &&
                    observed.nested_event40_write_observed,
                    "all raw parent ordinals remain descriptive without wrap inference");
                expect(observed.parent_producer_receipt_present == linked &&
                    observed.rejection_mask == (linked ? 0U :
                        static_cast<std::uint32_t>(grenade_lineage_parent_producer_missing)),
                    "missing parent producer receipt is explicit, not fabricated from current writer");
                no_authorization(observed);
            }
        }
    }
}

void test_lineage_rejects_mismatched_or_shadowed_context() {
    const auto before = snapshot(7U);
    const auto writer = validate_completed_grenade_native_enqueue(before,
        simulated_write(before, 40U, 5U), 40U, 5U);
    const auto original = enclosing_pickup();
    const auto rejects = [&](const GrenadeNativeEnclosingEventContext& parent,
        const std::uintptr_t state, const std::uint32_t role,
        const std::uintptr_t local, const GrenadeNativeEnqueueObservation& candidate,
        const std::uint32_t reason) {
        const auto observed = observe_grenade_native_enqueue_lineage(
            parent, state, role, local, candidate);
        ++lineageCases;
        expect(!observed.nested_event40_write_observed &&
            (observed.rejection_mask & reason) != 0 && observed.parent == parent,
            "mismatching context is retained raw but cannot be described as nested authority40");
        no_authorization(observed);
    };
    auto parent = original; parent.event_receipt = 0;
    rejects(parent, original.player_state, 1U, original.entity, writer, grenade_lineage_no_parent);
    parent = original; parent.event_raw = 39;
    rejects(parent, original.player_state, 1U, original.entity, writer, grenade_lineage_parent_not40);
    parent = original; parent.event_raw = 0x10028U;
    rejects(parent, original.player_state, 1U, original.entity, writer, grenade_lineage_parent_not40);
    auto incomplete = writer; incomplete.completedNativeWrite = false;
    rejects(original, original.player_state, 1U, original.entity, incomplete,
        grenade_lineage_incomplete_writer);
    const auto wrong_write = validate_completed_grenade_native_enqueue(before,
        simulated_write(before, 39U, 5U), 39U, 5U);
    rejects(original, original.player_state, 1U, original.entity, wrong_write,
        grenade_lineage_writer_not40);
    parent = original; ++parent.entity;
    rejects(parent, original.player_state, 1U, original.entity, writer, grenade_lineage_entity_mismatch);
    parent = original; ++parent.dispatcher_entity;
    rejects(parent, original.player_state, 1U, original.entity, writer, grenade_lineage_entity_mismatch);
    rejects(original, original.player_state, 1U, 0, writer, grenade_lineage_entity_mismatch);
    for (const auto role : {0U, 2U, 99U, 0xFFFFFFFFU}) {
        rejects(original, original.player_state, role, original.entity, writer,
            grenade_lineage_role_mismatch);
        parent = original; parent.dispatcher_role = role;
        rejects(parent, original.player_state, role, original.entity, writer,
            grenade_lineage_role_mismatch);
    }
    parent = original; parent.dispatcher_role = 0;
    rejects(parent, original.player_state, 1U, original.entity, writer, grenade_lineage_role_mismatch);
    parent = original; ++parent.player_state;
    rejects(parent, original.player_state, 1U, original.entity, writer, grenade_lineage_state_mismatch);
    rejects(original, 0, 1U, original.entity, writer, grenade_lineage_state_mismatch);
    parent = original; parent.active_dispatcher = false;
    rejects(parent, original.player_state, 1U, original.entity, writer,
        grenade_lineage_dispatcher_mismatch);
    parent = original; parent.dispatcher_receipt = 0;
    rejects(parent, original.player_state, 1U, original.entity, writer,
        grenade_lineage_dispatcher_mismatch);
    parent = original; ++parent.active_dispatcher_receipt;
    rejects(parent, original.player_state, 1U, original.entity, writer,
        grenade_lineage_dispatcher_mismatch);
    // Existing native observer deliberately shadows outer TLS on unsampled or
    // nonlocal nesting. An empty snapshot cannot recover an outer receipt.
    rejects({}, original.player_state, 1U, original.entity, writer, grenade_lineage_no_parent);

    const auto entry_copy = original;
    parent = original; parent.event_receipt = 999; parent.event_raw = 39;
    const auto preserved = observe_grenade_native_enqueue_lineage(
        entry_copy, original.player_state, 1U, original.entity, writer);
    ++lineageCases;
    expect(preserved.parent == entry_copy && preserved.nested_event40_write_observed &&
        preserved.parent.event_receipt != parent.event_receipt,
        "entry copy is independent of a later TLS frame change/reset");
    no_authorization(preserved);
}

}  // namespace

int main() {
    test_every_writer_counter_and_normalized_inputs();
    test_writer_requires_every_ring_slot_and_complete_snapshots();
    test_all_nonnegative_dispatcher_intervals();
    test_dispatcher_missing_mutated_wrap_and_signed_input();
    expect(writerCases == 9216U && dispatcherCases == 65536U,
        "original writer and dispatcher exhaustive test counts are retained");
    test_all_inline_batch_counters_counts_and_inputs();
    test_inline_batch_wrap_untouched_slots_and_expected_count();
    expect(batchCases == 3072U, "all prior expected-batch cases retained");
    test_initializer16_preserves_observed_pickup40_slot();
    test_lineage_preserves_raw_context_without_authorization();
    test_lineage_rejects_mismatched_or_shadowed_context();
    expect(lineageCases == 2072U, "all added parent-lineage cases retained");
    if (failures == 0) {
        std::cout << "PASS: " << writerCases << " completed-writer normalization cases and "
                  << dispatcherCases << " nonnegative dispatcher intervals and "
                  << batchCases << " expected inline batch cases; "
                  << lineageCases << " enclosing-event lineage cases; "
                     "mutation/missing/wide/signed-wrap observations fail conservatively; "
                     "transaction/prediction/drain authorization always0\n";
    }
    return failures == 0 ? 0 : 1;
}
