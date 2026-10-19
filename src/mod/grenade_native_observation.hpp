// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <limits>

namespace wawvr::mod {

struct GrenadeNativeAllocationSnapshot final {
    bool readable{};
    std::uintptr_t address{};
    std::uint32_t type{}, incarnation{};
    std::uint8_t allocated11D{}, handler{};
};

// Exact SP G_InitGentity sets +0x11D. G_FreeEntity zeroes the full object and
// restores old +0x90 plus one. This observes that bounded native transition;
// it does not authorize a transaction or identify arbitrary slot reincarnation.
[[nodiscard]] inline bool observe_grenade_native_allocation_release(
    const GrenadeNativeAllocationSnapshot& before,
    const GrenadeNativeAllocationSnapshot& after) noexcept {
    return before.readable && after.readable && before.address != 0 &&
        before.address == after.address && before.type == 4 &&
        before.handler == 14 && before.allocated11D == 1 &&
        after.allocated11D == 0 &&
        before.incarnation != (std::numeric_limits<std::uint32_t>::max)() &&
        after.incarnation == before.incarnation + 1U;
}

// Read-only diagnostic input copied by a separate bounded native adapter. The
// producer and ordinal are deliberately retained at their original width; an
// out-of-range value must not become valid merely by masking it to eight bits.
struct GrenadeNativeQueueSnapshot final {
    bool readable{};
    std::uint32_t producer{};
    std::array<std::uint32_t, 4> events{};
    std::array<std::uint32_t, 4> parameters{};
};

// A diagnostic receipt attached by a caller is only that observer's per-call
// monotonic diagnostic ID. It is NOT validated native transaction ownership.
// A modulo distance or matching slot cannot establish event39/event40 ownership,
// distinguish a wrapped/replayed event, or prove that either queue has drained.
struct GrenadeNativeQueueObservation final {
    bool boundedSlotMatches{};
    std::uint32_t ordinalRaw{};
    std::uint32_t producerRaw{};
    std::uint32_t moduloDistance{};
    std::uint32_t eventAtSlot{};
    std::uint32_t parameterAtSlot{};
    bool eventOwnershipEstablished{false};
    bool queueDrainEstablished{false};
};

[[nodiscard]] inline GrenadeNativeQueueObservation observe_grenade_native_queue(
    const GrenadeNativeQueueSnapshot& snapshot,
    const std::uint32_t ordinalRaw,
    const std::uint32_t expectedEvent39or40) noexcept {
    GrenadeNativeQueueObservation result{};
    result.ordinalRaw = ordinalRaw;
    result.producerRaw = snapshot.producer;
    // These masked values are descriptive even for unreadable/invalid inputs.
    // Only boundedSlotMatches below may qualify the bounded slot observation.
    result.moduloDistance = (snapshot.producer - ordinalRaw) & 0xFFU;
    const auto slot = ordinalRaw & 3U;
    result.eventAtSlot = snapshot.events[slot];
    result.parameterAtSlot = snapshot.parameters[slot];
    result.boundedSlotMatches = snapshot.readable &&
        snapshot.producer <= 255U && ordinalRaw <= 255U &&
        (expectedEvent39or40 == 39U || expectedEvent39or40 == 40U) &&
        result.moduloDistance >= 1U && result.moduloDistance <= 4U &&
        result.eventAtSlot == expectedEvent39or40;
    // Never promote this raw observation into transaction or queue-drain proof.
    return result;
}

}  // namespace wawvr::mod
