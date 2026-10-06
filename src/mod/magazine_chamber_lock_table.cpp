// SPDX-License-Identifier: GPL-3.0-only
#include "magazine_chamber_lock_table.hpp"

#include <cstdint>

namespace wawvr::mod {
namespace {

constexpr std::uint64_t kDefinitionAddressMask = 0x00000000FFFFFFFFULL;
constexpr std::uint64_t kProfileIdMask = 0x000000FF00000000ULL;
constexpr std::uint64_t kRecordMask =
    kDefinitionAddressMask | kProfileIdMask;

[[nodiscard]] constexpr bool valid_weapon_slot(
    const std::int32_t weapon_index) noexcept {
    return weapon_index > 0 &&
        static_cast<std::size_t>(weapon_index) <
            kMagazineChamberLockSlotCount;
}

[[nodiscard]] constexpr std::uint64_t encode_record(
    const DetachableMagazineWeaponBinding binding,
    const std::uint32_t definition_address) noexcept {
    if (!valid_weapon_slot(binding.weapon_index) ||
        definition_address == 0 ||
        pack_detachable_magazine_weapon_binding(binding) == 0) {
        return 0;
    }

    return static_cast<std::uint64_t>(definition_address) |
        (static_cast<std::uint64_t>(binding.profile_id) << 32U);
}

[[nodiscard]] constexpr bool decode_record(
    const std::uint64_t record,
    const std::int32_t weapon_index,
    MagazineChamberLockSnapshot* const output) noexcept {
    if (output == nullptr || !valid_weapon_slot(weapon_index) ||
        record == 0 || (record & ~kRecordMask) != 0) {
        return false;
    }

    const DetachableMagazineWeaponBinding binding{
        .profile_id = static_cast<DetachableMagazineWeaponProfileId>(
            (record >> 32U) & 0xFFU),
        .weapon_index = weapon_index,
    };
    const std::uint32_t definition_address =
        static_cast<std::uint32_t>(record & kDefinitionAddressMask);
    if (definition_address == 0 ||
        pack_detachable_magazine_weapon_binding(binding) == 0) {
        return false;
    }

    *output = {
        .binding = binding,
        .definition_address = definition_address,
    };
    return true;
}

}  // namespace

MagazineChamberLockTable::MagazineChamberLockTable() noexcept {
    clear_all();
}

MagazineChamberLockPublishResult MagazineChamberLockTable::publish(
    const DetachableMagazineWeaponBinding binding,
    const std::uint32_t definition_address) noexcept {
    const std::uint64_t record = encode_record(binding, definition_address);
    if (record == 0) {
        return MagazineChamberLockPublishResult::Invalid;
    }

    std::uint64_t expected = 0;
    if (slots_[static_cast<std::size_t>(binding.weapon_index)]
            .compare_exchange_strong(expected, record,
                                     std::memory_order_acq_rel,
                                     std::memory_order_acquire)) {
        return MagazineChamberLockPublishResult::Published;
    }
    return expected == record
        ? MagazineChamberLockPublishResult::AlreadyPresent
        : MagazineChamberLockPublishResult::Conflict;
}

bool MagazineChamberLockTable::clear_matching(
    const DetachableMagazineWeaponBinding binding,
    const std::uint32_t definition_address) noexcept {
    const std::uint64_t record = encode_record(binding, definition_address);
    if (record == 0) {
        return false;
    }

    std::uint64_t expected = record;
    return slots_[static_cast<std::size_t>(binding.weapon_index)]
        .compare_exchange_strong(expected, 0, std::memory_order_acq_rel,
                                 std::memory_order_acquire);
}

bool MagazineChamberLockTable::snapshot(
    const std::int32_t weapon_index,
    MagazineChamberLockSnapshot* const output) const noexcept {
    if (output == nullptr) {
        return false;
    }
    *output = {};
    if (!valid_weapon_slot(weapon_index)) {
        return false;
    }

    const std::uint64_t record =
        slots_[static_cast<std::size_t>(weapon_index)].load(
            std::memory_order_acquire);
    return decode_record(record, weapon_index, output);
}

bool MagazineChamberLockTable::matches(
    const DetachableMagazineWeaponBinding binding,
    const std::uint32_t definition_address) const noexcept {
    MagazineChamberLockSnapshot observed{};
    return definition_address != 0 &&
        snapshot(binding.weapon_index, &observed) &&
        observed.binding.profile_id == binding.profile_id &&
        observed.binding.weapon_index == binding.weapon_index &&
        observed.definition_address == definition_address;
}

void MagazineChamberLockTable::clear_all() noexcept {
    for (auto& slot : slots_) {
        slot.store(0, std::memory_order_release);
    }
}

}  // namespace wawvr::mod
