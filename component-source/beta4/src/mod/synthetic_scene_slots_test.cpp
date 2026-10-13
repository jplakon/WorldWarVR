// SPDX-License-Identifier: GPL-3.0-only
#include "synthetic_scene_slots.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

using wawvr::mod::SyntheticSceneIndexPointers;

[[nodiscard]] SyntheticSceneIndexPointers addresses_for(
    std::uint16_t** const xmodel,
    std::uint16_t** const dobj) noexcept {
    return {
        reinterpret_cast<std::uintptr_t>(xmodel),
        reinterpret_cast<std::uintptr_t>(dobj)};
}

}  // namespace

int main() {
    using namespace wawvr::mod;

    constexpr std::size_t kSceneTableTestSpan = 0x800;
    static_assert(kSyntheticSceneLastLeaseCandidate < kSceneTableTestSpan);
    std::array<std::uint16_t, kSceneTableTestSpan> xmodel{};
    std::array<std::uint16_t, kSceneTableTestSpan> dobj{};
    xmodel.fill(kEmptySyntheticSceneIndex);
    dobj.fill(kEmptySyntheticSceneIndex);
    auto* xmodel_pointer = xmodel.data();
    auto* dobj_pointer = dobj.data();
    const auto pointers = addresses_for(&xmodel_pointer, &dobj_pointer);

    std::uint32_t leased_entity = kNoSyntheticSceneLease;
    if (!find_available_synthetic_scene_slot(pointers, &leased_entity) ||
        leased_entity != kSyntheticSceneLastLeaseCandidate) {
        std::cerr << "an empty scene must lease the highest candidate first\n";
        return 1;
    }

    // Der Riese was observed populating this entire low client-only range.
    // Those entries must not prevent a free high renderer entry from being
    // leased.
    constexpr std::uint32_t kDerRieseLastOccupiedEntity = 0x425;
    std::fill(
        xmodel.begin() + kSyntheticSceneFirstLeaseCandidate,
        xmodel.begin() + kDerRieseLastOccupiedEntity + 1,
        std::uint16_t{0});
    leased_entity = kNoSyntheticSceneLease;
    if (synthetic_scene_slot_available(
            pointers, kSyntheticSceneFirstLeaseCandidate) ||
        synthetic_scene_slot_available(
            pointers, kDerRieseLastOccupiedEntity) ||
        !synthetic_scene_slot_available(
            pointers, kDerRieseLastOccupiedEntity + 1) ||
        !find_available_synthetic_scene_slot(pointers, &leased_entity) ||
        leased_entity != kSyntheticSceneLastLeaseCandidate) {
        std::cerr << "occupied Der Riese entries must be skipped in favor of a high free slot\n";
        return 1;
    }
    std::fill(
        xmodel.begin() + kSyntheticSceneFirstLeaseCandidate,
        xmodel.begin() + kDerRieseLastOccupiedEntity + 1,
        kEmptySyntheticSceneIndex);

    xmodel[kSyntheticSceneLastLeaseCandidate] = 0;
    leased_entity = kNoSyntheticSceneLease;
    if (synthetic_scene_slot_available(
            pointers, kSyntheticSceneLastLeaseCandidate) ||
        !find_available_synthetic_scene_slot(pointers, &leased_entity) ||
        leased_entity != kSyntheticSceneLastLeaseCandidate - 1) {
        std::cerr << "an occupied scene-XModel slot must be skipped high-to-low\n";
        return 1;
    }
    dobj[kSyntheticSceneLastLeaseCandidate - 1] = 0;
    leased_entity = kNoSyntheticSceneLease;
    if (synthetic_scene_slot_available(
            pointers, kSyntheticSceneLastLeaseCandidate - 1) ||
        !find_available_synthetic_scene_slot(pointers, &leased_entity) ||
        leased_entity != kSyntheticSceneLastLeaseCandidate - 2) {
        std::cerr << "an occupied scene-DObj slot must be skipped high-to-low\n";
        return 1;
    }
    xmodel[kSyntheticSceneLastLeaseCandidate] = kEmptySyntheticSceneIndex;
    dobj[kSyntheticSceneLastLeaseCandidate - 1] =
        kEmptySyntheticSceneIndex;

    if (!synthetic_scene_slot_available(
            pointers, kSyntheticSceneFirstLeaseCandidate) ||
        !synthetic_scene_slot_available(
            pointers, kSyntheticSceneLastLeaseCandidate) ||
        synthetic_scene_slot_available(
            pointers, kSyntheticSceneFirstLeaseCandidate - 1) ||
        synthetic_scene_slot_available(
            pointers, kSyntheticSceneLastLeaseCandidate + 1) ||
        synthetic_scene_slot_available(
            pointers, kSyntheticSceneLastLeaseCandidate + 2) ||
        synthetic_scene_slot_available(
            pointers, kSceneTableTestSpan) ||
        synthetic_scene_slot_available(pointers, 0x883)) {
        std::cerr << "lease boundaries and renderer sentinels must fail closed\n";
        return 1;
    }

    for (std::uint32_t entity = kSyntheticSceneFirstLeaseCandidate;
         entity <= kSyntheticSceneLastLeaseCandidate; ++entity) {
        if (((entity - kSyntheticSceneFirstLeaseCandidate) & 1U) == 0) {
            xmodel[entity] = 0;
        } else {
            dobj[entity] = 0;
        }
    }
    leased_entity = kSyntheticSceneFirstLeaseCandidate;
    if (find_available_synthetic_scene_slot(pointers, &leased_entity) ||
        leased_entity != kNoSyntheticSceneLease) {
        std::cerr << "lease exhaustion must fail and return no lease\n";
        return 1;
    }
    xmodel.fill(kEmptySyntheticSceneIndex);
    dobj.fill(kEmptySyntheticSceneIndex);

    if (find_available_synthetic_scene_slot(pointers, nullptr)) {
        std::cerr << "a null lease output must fail closed\n";
        return 1;
    }

    std::uint16_t* null_table = nullptr;
    if (synthetic_scene_slot_available(
            addresses_for(&null_table, &dobj_pointer),
            kSyntheticSceneLastLeaseCandidate) ||
        synthetic_scene_slot_available(
            addresses_for(&xmodel_pointer, &null_table),
            kSyntheticSceneLastLeaseCandidate) ||
        synthetic_scene_slot_available(
            {}, kSyntheticSceneLastLeaseCandidate)) {
        std::cerr << "missing globals or tables must fail closed\n";
        return 1;
    }
    leased_entity = kSyntheticSceneFirstLeaseCandidate;
    if (find_available_synthetic_scene_slot({}, &leased_entity) ||
        leased_entity != kNoSyntheticSceneLease) {
        std::cerr << "an invalid lease source must return no lease\n";
        return 1;
    }
    auto* const overflow_table = reinterpret_cast<std::uint16_t*>(
        (std::numeric_limits<std::uintptr_t>::max)() - 1);
    auto* overflow_pointer = overflow_table;
    if (synthetic_scene_slot_available(
            addresses_for(&overflow_pointer, &dobj_pointer),
            kSyntheticSceneLastLeaseCandidate) ||
        synthetic_scene_slot_available(
            addresses_for(&xmodel_pointer, &overflow_pointer),
            kSyntheticSceneLastLeaseCandidate)) {
        std::cerr << "overflowing renderer table pointers must fail closed\n";
        return 1;
    }

    constexpr std::size_t kTableBytes =
        kSceneTableTestSpan * sizeof(std::uint16_t);
    auto* const protected_xmodel = static_cast<std::uint16_t*>(VirtualAlloc(
        nullptr, kTableBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    auto* const protected_dobj = static_cast<std::uint16_t*>(VirtualAlloc(
        nullptr, kTableBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (protected_xmodel == nullptr || protected_dobj == nullptr) {
        if (protected_xmodel != nullptr) {
            VirtualFree(protected_xmodel, 0, MEM_RELEASE);
        }
        if (protected_dobj != nullptr) {
            VirtualFree(protected_dobj, 0, MEM_RELEASE);
        }
        std::cerr << "could not allocate protected-table regression pages\n";
        return 1;
    }
    std::fill_n(
        protected_xmodel, kSceneTableTestSpan,
        kEmptySyntheticSceneIndex);
    std::fill_n(
        protected_dobj, kSceneTableTestSpan,
        kEmptySyntheticSceneIndex);
    auto* protected_xmodel_pointer = protected_xmodel;
    auto* protected_dobj_pointer = protected_dobj;
    const auto protected_pointers = addresses_for(
        &protected_xmodel_pointer, &protected_dobj_pointer);
    DWORD old_protection = 0;
    bool protected_pages_rejected =
        VirtualProtect(
            protected_xmodel, kTableBytes, PAGE_READONLY,
            &old_protection) != FALSE &&
        !synthetic_scene_slot_available(
            protected_pointers, kSyntheticSceneLastLeaseCandidate);
    DWORD ignored_protection = 0;
    protected_pages_rejected =
        VirtualProtect(
            protected_xmodel, kTableBytes, PAGE_READWRITE,
            &ignored_protection) != FALSE &&
        VirtualProtect(
            protected_dobj, kTableBytes, PAGE_READONLY,
            &old_protection) != FALSE &&
        !synthetic_scene_slot_available(
            protected_pointers, kSyntheticSceneLastLeaseCandidate) &&
        protected_pages_rejected;
    protected_pages_rejected =
        VirtualProtect(
            protected_dobj, kTableBytes, PAGE_NOACCESS,
            &ignored_protection) != FALSE &&
        !synthetic_scene_slot_available(
            protected_pointers, kSyntheticSceneLastLeaseCandidate) &&
        protected_pages_rejected;
    VirtualFree(protected_xmodel, 0, MEM_RELEASE);
    VirtualFree(protected_dobj, 0, MEM_RELEASE);
    if (!protected_pages_rejected) {
        std::cerr << "read-only and inaccessible renderer tables must fail closed\n";
        return 1;
    }

    std::cout << "synthetic scene slot tests passed\n";
    return 0;
}
