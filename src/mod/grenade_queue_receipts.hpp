// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "grenade_native_observation.hpp"

#include <array>
#include <cstdint>

namespace wawvr::mod {

// A completed bounded writer observation is not transaction ownership. A caller
// must independently identify the real native call and both complete snapshots.
// These raw receipts never grant gameplay permission or authorize queue drain.
struct GrenadeNativeEnqueueObservation final {
    bool completedNativeWrite{};
    std::uint32_t ordinalRaw{};
    std::uint32_t nextProducerRaw{};
    std::uint32_t expectedNextProducer{};
    std::uint32_t eventRaw{};
    std::uint32_t parameterRaw{};
    std::uint32_t eventNormalized{};
    std::uint32_t parameterNormalized{};
    std::uint32_t slot{};
    bool eventOwnershipEstablished{false};
    bool transactionOwnershipEstablished{false};
    bool queueDrainEstablished{false};
};

[[nodiscard]] inline GrenadeNativeEnqueueObservation
validate_completed_grenade_native_enqueue(
    const GrenadeNativeQueueSnapshot& before,
    const GrenadeNativeQueueSnapshot& after,
    const std::uint32_t eventRaw,
    const std::uint32_t paramRaw) noexcept {
    GrenadeNativeEnqueueObservation result{};
    result.ordinalRaw = before.producer;
    result.nextProducerRaw = after.producer;
    result.expectedNextProducer = (before.producer + 1U) & 0xFFU;
    result.eventRaw = eventRaw;
    result.parameterRaw = paramRaw;
    result.eventNormalized = eventRaw & 0xFFU;
    result.parameterNormalized = paramRaw & 0xFFFFU;
    result.slot = before.producer & 3U;
    if (!before.readable || !after.readable || before.producer > 255U ||
        after.producer > 255U ||
        after.producer != result.expectedNextProducer) {
        return result;
    }
    for (std::uint32_t slot = 0; slot < 4U; ++slot) {
        const auto expectedEvent = slot == result.slot
            ? result.eventNormalized : before.events[slot];
        const auto expectedParameter = slot == result.slot
            ? result.parameterNormalized : before.parameters[slot];
        if (after.events[slot] != expectedEvent ||
            after.parameters[slot] != expectedParameter) {
            return result;
        }
    }
    result.completedNativeWrite = true;
    return result;
}

// Copied at common-writer ENTRY from the existing event-handler TLS frame and
// its enclosing dispatcher frame. These are bounded observer facts, not an
// input intent, source acquisition, accepted-command, or native capability.
struct GrenadeNativeEnclosingEventContext final {
    std::uint64_t event_receipt{}, produced_receipt{}, dispatcher_receipt{};
    std::uint64_t active_dispatcher_receipt{};
    std::uintptr_t entity{}, dispatcher_entity{}, player_state{};
    std::uint32_t event_raw{}, ordinal_raw{}, dispatcher_role{};
    bool active_dispatcher{};
    bool operator==(const GrenadeNativeEnclosingEventContext&) const = default;
};

enum GrenadeNativeLineageRejection : std::uint32_t {
    grenade_lineage_no_parent = 1U,
    grenade_lineage_parent_not40 = 2U,
    grenade_lineage_writer_not40 = 4U,
    grenade_lineage_incomplete_writer = 8U,
    grenade_lineage_entity_mismatch = 16U,
    grenade_lineage_role_mismatch = 32U,
    grenade_lineage_state_mismatch = 64U,
    grenade_lineage_dispatcher_mismatch = 128U,
    grenade_lineage_parent_producer_missing = 256U,
};

struct GrenadeNativeEnqueueLineageObservation final {
    GrenadeNativeEnclosingEventContext parent{};
    std::uint32_t rejection_mask{};
    bool context_captured{}, nested_event40_write_observed{};
    bool parent_producer_receipt_present{};
    bool eventOwnershipEstablished{false};
    bool transactionOwnershipEstablished{false};
    bool predictionDrainEstablished{false};
    bool queueDrainEstablished{false};
};

// A nested40 write is a candidate follow-up topology only. No producer caller
// PC, source, weapon transition, command/world identity, or legitimacy is proved.
// Keep raw mismatching context so absence of a link is not silently promoted.
[[nodiscard]] constexpr GrenadeNativeEnqueueLineageObservation
observe_grenade_native_enqueue_lineage(
    const GrenadeNativeEnclosingEventContext& parent,
    const std::uintptr_t enqueue_state,
    const std::uint32_t enqueue_role,
    const std::uintptr_t local_entity,
    const GrenadeNativeEnqueueObservation& writer) noexcept {
    GrenadeNativeEnqueueLineageObservation result{};
    result.parent = parent;
    result.context_captured = parent.event_receipt != 0;
    result.parent_producer_receipt_present = parent.produced_receipt != 0;
    if (!result.context_captured) result.rejection_mask |= grenade_lineage_no_parent;
    if (parent.event_raw != 40U) result.rejection_mask |= grenade_lineage_parent_not40;
    if (writer.eventNormalized != 40U) result.rejection_mask |= grenade_lineage_writer_not40;
    if (!writer.completedNativeWrite) result.rejection_mask |= grenade_lineage_incomplete_writer;
    if (local_entity == 0 || parent.entity != local_entity ||
        parent.dispatcher_entity != parent.entity)
        result.rejection_mask |= grenade_lineage_entity_mismatch;
    // Retail event40 dispatch is authoritative. A predicted writer nested
    // beneath an authority TLS frame must not inherit that frame's lineage.
    if (enqueue_role != 1U || parent.dispatcher_role != enqueue_role)
        result.rejection_mask |= grenade_lineage_role_mismatch;
    if (enqueue_state == 0 || parent.player_state != enqueue_state)
        result.rejection_mask |= grenade_lineage_state_mismatch;
    if (!parent.active_dispatcher || parent.dispatcher_receipt == 0 ||
        parent.dispatcher_receipt != parent.active_dispatcher_receipt)
        result.rejection_mask |= grenade_lineage_dispatcher_mismatch;
    if (!result.parent_producer_receipt_present)
        result.rejection_mask |= grenade_lineage_parent_producer_missing;
    constexpr std::uint32_t missing_producer = grenade_lineage_parent_producer_missing;
    result.nested_event40_write_observed =
        (result.rejection_mask & ~missing_producer) == 0;
    return result;
}

// The runtime derives this expected batch from independently verified PRE-call
// branches. Never infer its count or inputs from the observed post-call ring.
struct GrenadeNativeExpectedWrite final {
    std::uint32_t eventRaw{};
    std::uint32_t parameterRaw{};
};

struct GrenadeNativeExpectedBatch final {
    std::array<GrenadeNativeExpectedWrite, 2> writes{};
    std::uint32_t count{};
};

struct GrenadeNativeBatchObservation final {
    bool completedNativeWrite{};
    std::uint32_t ordinalRaw{};
    std::uint32_t nextProducerRaw{};
    std::uint32_t expectedNextProducer{};
    std::uint32_t expectedCount{};
    std::array<std::uint32_t, 2> eventNormalized{};
    std::array<std::uint32_t, 2> parameterNormalized{};
    bool eventOwnershipEstablished{false};
    bool transactionOwnershipEstablished{false};
    bool queueDrainEstablished{false};
};

[[nodiscard]] inline GrenadeNativeBatchObservation
validate_completed_grenade_native_batch(
    const GrenadeNativeQueueSnapshot& before,
    const GrenadeNativeQueueSnapshot& after,
    const GrenadeNativeExpectedBatch& expected) noexcept {
    GrenadeNativeBatchObservation result{};
    result.ordinalRaw = before.producer;
    result.nextProducerRaw = after.producer;
    result.expectedCount = expected.count;
    for (std::uint32_t i = 0; i < 2U; ++i) {
        result.eventNormalized[i] = expected.writes[i].eventRaw & 0xFFU;
        result.parameterNormalized[i] = expected.writes[i].parameterRaw & 0xFFFFU;
    }
    if (!before.readable || !after.readable || before.producer > 255U ||
        after.producer > 255U || expected.count > 2U) return result;
    auto predicted = before;
    for (std::uint32_t i = 0; i < expected.count; ++i) {
        const auto slot = predicted.producer & 3U;
        predicted.events[slot] = result.eventNormalized[i];
        predicted.parameters[slot] = result.parameterNormalized[i];
        predicted.producer = (predicted.producer + 1U) & 0xFFU;
    }
    result.expectedNextProducer = predicted.producer;
    // Includes every untouched DWORD. Count zero requires complete identity and
    // remains no-write observational evidence, not an event or drain permission.
    result.completedNativeWrite = after.producer == predicted.producer &&
        after.events == predicted.events && after.parameters == predicted.parameters;
    return result;
}

struct GrenadeNativeDispatchObservation final {
    bool observedDrainInterval{};
    std::uint32_t producerBeforeRaw{};
    std::uint32_t producerAfterRaw{};
    std::uint32_t startOrdinalRaw{};
    std::uint32_t completedOrdinalRaw{};
    std::uint32_t observedEventCount{};
    bool nativeCounterWrapResolved{false};
    bool eventOwnershipEstablished{false};
    bool transactionOwnershipEstablished{false};
    bool predictionDrainEstablished{false};
    bool queueDrainEstablished{false};
};

// Conservative, non-wrapping subset only. Native signed negative ordinals around
// wrap can be legitimate; rejecting their raw bit patterns here means UNSUPPORTED
// OBSERVATION, not invalid native execution. Do not cast wide raw values to signed
// integers or infer their wrap epoch. completedOrdinalRaw must be captured from
// the actual dispatcher-local ordinal at completion, NOT an assumed EAX result.
// A stable zero-length interval also remains merely descriptive, not empty-queue
// proof, prediction acknowledgement, or transaction-drain authorization.
[[nodiscard]] inline GrenadeNativeDispatchObservation
validate_completed_grenade_native_dispatch(
    const GrenadeNativeQueueSnapshot& before,
    const GrenadeNativeQueueSnapshot& after,
    const std::uint32_t startRaw,
    const std::uint32_t completedOrdinalRaw) noexcept {
    GrenadeNativeDispatchObservation result{};
    result.producerBeforeRaw = before.producer;
    result.producerAfterRaw = after.producer;
    result.startOrdinalRaw = startRaw;
    result.completedOrdinalRaw = completedOrdinalRaw;
    if (!before.readable || !after.readable || before.producer > 255U ||
        after.producer > 255U || before.producer != after.producer ||
        startRaw > 255U || completedOrdinalRaw > 255U ||
        completedOrdinalRaw != after.producer || startRaw > before.producer) {
        return result;
    }
    // Checked order avoids unsigned producer-4 underflow near zero.
    const auto distance = before.producer - startRaw;
    if (distance > 4U || before.events != after.events ||
        before.parameters != after.parameters) {
        return result;
    }
    result.observedEventCount = distance;
    result.observedDrainInterval = true;
    return result;
}

}  // namespace wawvr::mod
