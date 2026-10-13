// SPDX-License-Identifier: GPL-3.0-only
#include "synthetic_scene_slots.hpp"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace wawvr::mod {
namespace {

[[nodiscard]] bool readable_protection(const DWORD protection) noexcept {
    const DWORD access = protection & 0xffU;
    return access == PAGE_READONLY || access == PAGE_READWRITE ||
           access == PAGE_WRITECOPY || access == PAGE_EXECUTE_READ ||
           access == PAGE_EXECUTE_READWRITE ||
           access == PAGE_EXECUTE_WRITECOPY;
}

[[nodiscard]] bool writable_protection(const DWORD protection) noexcept {
    const DWORD access = protection & 0xffU;
    return access == PAGE_READWRITE || access == PAGE_WRITECOPY ||
           access == PAGE_EXECUTE_READWRITE ||
           access == PAGE_EXECUTE_WRITECOPY;
}

[[nodiscard]] bool accessible_range(
    const std::uintptr_t address,
    const std::size_t size,
    const bool writable) noexcept {
    if (address == 0 || size == 0 ||
        address > (std::numeric_limits<std::uintptr_t>::max)() - size) {
        return false;
    }
    const auto* const pointer = reinterpret_cast<const void*>(address);
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(pointer, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
        (writable ? !writable_protection(memory.Protect)
                  : !readable_protection(memory.Protect))) {
        return false;
    }
    const auto region_begin =
        reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    if (memory.RegionSize >
        (std::numeric_limits<std::uintptr_t>::max)() - region_begin) {
        return false;
    }
    const auto region_end = region_begin + memory.RegionSize;
    return address >= region_begin && address <= region_end &&
           size <= region_end - address;
}

[[nodiscard]] bool read_scene_index(
    const std::uintptr_t table_pointer_address,
    const std::uint32_t entity_number,
    std::uint16_t* const index) noexcept {
    if (index == nullptr ||
        !accessible_range(
            table_pointer_address, sizeof(std::uintptr_t), false)) {
        return false;
    }
    std::uintptr_t table = 0;
    std::memcpy(
        &table, reinterpret_cast<const void*>(table_pointer_address),
        sizeof(table));
    const auto offset =
        static_cast<std::uintptr_t>(entity_number) * sizeof(std::uint16_t);
    if (table == 0 ||
        table > (std::numeric_limits<std::uintptr_t>::max)() - offset) {
        return false;
    }
    const auto slot = table + offset;
    // R_AddDObjToScene writes its scene index immediately after this
    // preflight. A readable-but-not-writable stale target must fail closed.
    if (!accessible_range(slot, sizeof(*index), true)) {
        return false;
    }
    std::memcpy(index, reinterpret_cast<const void*>(slot), sizeof(*index));
    return true;
}

}  // namespace

bool synthetic_scene_slot_available(
    const SyntheticSceneIndexPointers& pointers,
    const std::uint32_t entity_number) noexcept {
    if (entity_number < kSyntheticSceneFirstLeaseCandidate ||
        entity_number > kSyntheticSceneLastLeaseCandidate) {
        return false;
    }
    std::uint16_t xmodel_index = 0;
    std::uint16_t dobj_index = 0;
    return read_scene_index(
               pointers.xmodel_index_pointer, entity_number,
               &xmodel_index) &&
           read_scene_index(
               pointers.dobj_index_pointer, entity_number,
               &dobj_index) &&
           xmodel_index == kEmptySyntheticSceneIndex &&
           dobj_index == kEmptySyntheticSceneIndex;
}

bool find_available_synthetic_scene_slot(
    const SyntheticSceneIndexPointers& pointers,
    std::uint32_t* const entity_number) noexcept {
    if (entity_number == nullptr) {
        return false;
    }
    *entity_number = kNoSyntheticSceneLease;
    for (std::uint32_t candidate = kSyntheticSceneLastLeaseCandidate;;
         --candidate) {
        if (synthetic_scene_slot_available(pointers, candidate)) {
            *entity_number = candidate;
            return true;
        }
        if (candidate == kSyntheticSceneFirstLeaseCandidate) {
            return false;
        }
    }
}

}  // namespace wawvr::mod
