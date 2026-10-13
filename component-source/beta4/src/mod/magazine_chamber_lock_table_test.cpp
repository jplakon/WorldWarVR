// SPDX-License-Identifier: GPL-3.0-only
#include "magazine_chamber_lock_table.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

using wawvr::mod::DetachableMagazineWeaponBinding;
using wawvr::mod::DetachableMagazineWeaponProfileId;
using wawvr::mod::MagazineChamberLockPublishResult;
using wawvr::mod::MagazineChamberLockSnapshot;
using wawvr::mod::MagazineChamberLockTable;
using wawvr::mod::MagazineReloadBeginFallbackDecision;
using wawvr::mod::classify_magazine_reload_begin_fallback;
using wawvr::mod::kMagazineChamberLockSlotCount;

constexpr DetachableMagazineWeaponBinding kM1Binding{
    .profile_id = DetachableMagazineWeaponProfileId::M1Carbine,
    .weapon_index = 16,
};
constexpr DetachableMagazineWeaponBinding kColtBinding{
    .profile_id = DetachableMagazineWeaponProfileId::ZombieColt,
    .weapon_index = 7,
};
constexpr std::uint32_t kM1DefinitionAddress = 0x11223344U;
constexpr std::uint32_t kColtDefinitionAddress = 0x55667788U;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void test_empty_table_has_no_snapshot() {
    MagazineChamberLockTable table;
    MagazineChamberLockSnapshot snapshot{
        .binding = kM1Binding,
        .definition_address = kM1DefinitionAddress,
    };
    expect(!table.snapshot(kM1Binding.weapon_index, &snapshot),
           "a new table has no chamber lock");
    expect(snapshot.binding.profile_id ==
                   DetachableMagazineWeaponProfileId::None &&
               snapshot.binding.weapon_index == 0 &&
               snapshot.definition_address == 0,
           "an absent slot returns a cleared snapshot");
}

void test_m1_and_colt_locks_are_independent() {
    MagazineChamberLockTable table;
    expect(table.publish(kM1Binding, kM1DefinitionAddress) ==
               MagazineChamberLockPublishResult::Published,
           "the accepted M1 chamber lock publishes");
    expect(table.publish(kColtBinding, kColtDefinitionAddress) ==
               MagazineChamberLockPublishResult::Published,
           "a second weapon publishes without overwriting the M1");
    expect(table.matches(kM1Binding, kM1DefinitionAddress) &&
               table.matches(kColtBinding, kColtDefinitionAddress),
           "each map-local weapon slot retains its own lock");
    expect(table.clear_matching(kM1Binding, kM1DefinitionAddress),
           "the exact M1 identity clears");
    expect(!table.matches(kM1Binding, kM1DefinitionAddress) &&
               table.matches(kColtBinding, kColtDefinitionAddress),
           "clearing M1 cannot retire another weapon's chamber lock");
}

void test_publish_is_idempotent_and_conflicts_fail_closed() {
    MagazineChamberLockTable table;
    expect(table.publish(kM1Binding, kM1DefinitionAddress) ==
               MagazineChamberLockPublishResult::Published &&
               table.publish(kM1Binding, kM1DefinitionAddress) ==
                   MagazineChamberLockPublishResult::AlreadyPresent,
           "republishing the exact accepted M1 identity is idempotent");

    expect(table.publish(kM1Binding, 0xAABBCCDDU) ==
               MagazineChamberLockPublishResult::Conflict,
           "a different WeaponDef cannot replace a live slot");
    const DetachableMagazineWeaponBinding wrong_profile{
        .profile_id = DetachableMagazineWeaponProfileId::ZombieColt,
        .weapon_index = kM1Binding.weapon_index,
    };
    expect(table.publish(wrong_profile, kM1DefinitionAddress) ==
               MagazineChamberLockPublishResult::Conflict,
           "a different profile cannot replace a live slot");
    expect(table.matches(kM1Binding, kM1DefinitionAddress),
           "conflicting publications preserve the original M1 lock");
}

void test_clear_requires_exact_identity() {
    MagazineChamberLockTable table;
    static_cast<void>(table.publish(kM1Binding, kM1DefinitionAddress));
    expect(!table.clear_matching(kM1Binding, 0xAABBCCDDU),
           "a mismatched WeaponDef cannot clear a lock");
    const DetachableMagazineWeaponBinding wrong_profile{
        .profile_id = DetachableMagazineWeaponProfileId::ZombieColt,
        .weapon_index = kM1Binding.weapon_index,
    };
    expect(!table.clear_matching(wrong_profile, kM1DefinitionAddress),
           "a mismatched profile cannot clear a lock");
    const DetachableMagazineWeaponBinding wrong_slot{
        .profile_id = DetachableMagazineWeaponProfileId::M1Carbine,
        .weapon_index = kM1Binding.weapon_index + 1,
    };
    expect(!table.clear_matching(wrong_slot, kM1DefinitionAddress) &&
               table.matches(kM1Binding, kM1DefinitionAddress),
           "a mismatched weapon index cannot clear the original slot");
}

void test_invalid_identity_and_slot_are_rejected() {
    MagazineChamberLockTable table;
    const DetachableMagazineWeaponBinding no_profile{
        .profile_id = DetachableMagazineWeaponProfileId::None,
        .weapon_index = 1,
    };
    const DetachableMagazineWeaponBinding unknown_profile{
        .profile_id =
            static_cast<DetachableMagazineWeaponProfileId>(0xFFU),
        .weapon_index = 1,
    };
    const DetachableMagazineWeaponBinding slot_zero{
        .profile_id = DetachableMagazineWeaponProfileId::M1Carbine,
        .weapon_index = 0,
    };
    const DetachableMagazineWeaponBinding slot_past_end{
        .profile_id = DetachableMagazineWeaponProfileId::M1Carbine,
        .weapon_index =
            static_cast<std::int32_t>(kMagazineChamberLockSlotCount),
    };
    expect(table.publish(no_profile, kM1DefinitionAddress) ==
                   MagazineChamberLockPublishResult::Invalid &&
               table.publish(unknown_profile, kM1DefinitionAddress) ==
                   MagazineChamberLockPublishResult::Invalid &&
               table.publish(slot_zero, kM1DefinitionAddress) ==
                   MagazineChamberLockPublishResult::Invalid &&
               table.publish(slot_past_end, kM1DefinitionAddress) ==
                   MagazineChamberLockPublishResult::Invalid &&
               table.publish(kM1Binding, 0) ==
                   MagazineChamberLockPublishResult::Invalid,
           "unknown identities, sentinel slots, bounds, and null addresses fail closed");

    MagazineChamberLockSnapshot snapshot{};
    expect(!table.snapshot(-1, &snapshot) &&
               !table.snapshot(0, &snapshot) &&
               !table.snapshot(
                   static_cast<std::int32_t>(
                       kMagazineChamberLockSlotCount),
                   &snapshot) &&
               !table.snapshot(kM1Binding.weapon_index, nullptr),
           "invalid read requests fail without touching the table");
}

void test_boundary_slot_and_session_clear() {
    MagazineChamberLockTable table;
    const DetachableMagazineWeaponBinding final_slot{
        .profile_id = DetachableMagazineWeaponProfileId::M1Carbine,
        .weapon_index = static_cast<std::int32_t>(
            kMagazineChamberLockSlotCount - 1),
    };
    expect(table.publish(final_slot, kM1DefinitionAddress) ==
                   MagazineChamberLockPublishResult::Published &&
               table.publish(kColtBinding, kColtDefinitionAddress) ==
                   MagazineChamberLockPublishResult::Published,
           "the final T4 weapon slot and another slot both publish");
    table.clear_all();
    expect(!table.matches(final_slot, kM1DefinitionAddress) &&
               !table.matches(kColtBinding, kColtDefinitionAddress),
           "a session clear retires every weapon slot");
    expect(table.publish(kM1Binding, kM1DefinitionAddress) ==
               MagazineChamberLockPublishResult::Published &&
               table.clear_matching(kM1Binding, kM1DefinitionAddress) &&
               table.publish(kM1Binding, kM1DefinitionAddress) ==
                   MagazineChamberLockPublishResult::Published,
           "an exactly cleared slot can be reused");
}

void test_empty_reload_lock_denies_native_begin_while_viewmodel_is_unavailable() {
    MagazineChamberLockTable table;
    const auto classify_begin_fallback = [&]() noexcept {
        return classify_magazine_reload_begin_fallback(
            table.matches(kM1Binding, kM1DefinitionAddress));
    };

    expect(classify_begin_fallback() ==
               MagazineReloadBeginFallbackDecision::Native,
           "an ordinary supported weapon starts with its native fallback available");
    expect(table.publish(kM1Binding, kM1DefinitionAddress) ==
               MagazineChamberLockPublishResult::Published,
           "an empty reload publishes its exact persistent chamber lock");

    // Chest stow/viewmodel loss changes no gameplay identity and deliberately
    // does not clear the independent chamber-lock table.
    const bool viewmodel_supported = false;
    expect(!viewmodel_supported &&
               classify_begin_fallback() ==
                   MagazineReloadBeginFallbackDecision::Suppress,
           "chest stow cannot reopen native reload while the exact empty-chamber lock persists");
    expect(table.clear_matching(kM1Binding, kM1DefinitionAddress) &&
               classify_begin_fallback() ==
                   MagazineReloadBeginFallbackDecision::Native,
           "physical charging restores native fallback only after clearing the exact lock");
}

void test_unrelated_or_unsupported_weapon_retains_native_begin() {
    MagazineChamberLockTable table;
    static_cast<void>(table.publish(kM1Binding, kM1DefinitionAddress));

    expect(classify_magazine_reload_begin_fallback(
               table.matches(kColtBinding, kColtDefinitionAddress)) ==
               MagazineReloadBeginFallbackDecision::Native,
           "another gameplay weapon is not suppressed by the locked rifle");
    const DetachableMagazineWeaponBinding unsupported{
        .profile_id = DetachableMagazineWeaponProfileId::None,
        .weapon_index = 63,
    };
    expect(classify_magazine_reload_begin_fallback(
               table.matches(unsupported, 0xCAFEBABEU)) ==
               MagazineReloadBeginFallbackDecision::Native,
           "an unsupported weapon without an exact persistent lock keeps native reload");
}

}  // namespace

int main() {
    test_empty_table_has_no_snapshot();
    test_m1_and_colt_locks_are_independent();
    test_publish_is_idempotent_and_conflicts_fail_closed();
    test_clear_requires_exact_identity();
    test_invalid_identity_and_slot_are_rejected();
    test_boundary_slot_and_session_clear();
    test_empty_reload_lock_denies_native_begin_while_viewmodel_is_unavailable();
    test_unrelated_or_unsupported_weapon_retains_native_begin();
    std::cout << "magazine chamber-lock table tests passed\n";
    return EXIT_SUCCESS;
}
