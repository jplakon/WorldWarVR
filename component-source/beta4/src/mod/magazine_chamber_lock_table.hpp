// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "detachable_magazine_weapon_profile.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace wawvr::mod {

// T4 reserves 128 weapon-definition slots. Slot zero remains the engine's
// invalid/no-weapon sentinel, but retaining it in the table keeps a direct,
// bounds-checked mapping from a map-local weapon index to its chamber lock.
inline constexpr std::size_t kMagazineChamberLockSlotCount = 128;

enum class MagazineChamberLockPublishResult : std::uint8_t {
    Published,
    AlreadyPresent,
    Invalid,
    Conflict,
};

enum class MagazineReloadBeginFallbackDecision : std::uint8_t {
    Native,
    Suppress,
};

// The render/viewmodel publication may disappear while a seated empty reload
// still needs a physical charging action. At every otherwise-native begin
// fallback, the exact persistent gameplay lock wins until charging or session
// retirement clears it.
[[nodiscard]] constexpr MagazineReloadBeginFallbackDecision
classify_magazine_reload_begin_fallback(
    const bool exact_persistent_chamber_lock_matches_gameplay_weapon) noexcept {
    return exact_persistent_chamber_lock_matches_gameplay_weapon
        ? MagazineReloadBeginFallbackDecision::Suppress
        : MagazineReloadBeginFallbackDecision::Native;
}

struct MagazineChamberLockSnapshot final {
    DetachableMagazineWeaponBinding binding{};
    std::uint32_t definition_address{};
};

// A chamber lock is published as one atomic record. The weapon index is
// implied by the table slot, leaving enough room for the exact x86 WeaponDef
// address and immutable profile identifier without a torn two-atomic read.
class MagazineChamberLockTable final {
public:
    MagazineChamberLockTable() noexcept;

    [[nodiscard]] MagazineChamberLockPublishResult publish(
        DetachableMagazineWeaponBinding binding,
        std::uint32_t definition_address) noexcept;

    [[nodiscard]] bool clear_matching(
        DetachableMagazineWeaponBinding binding,
        std::uint32_t definition_address) noexcept;

    [[nodiscard]] bool snapshot(
        std::int32_t weapon_index,
        MagazineChamberLockSnapshot* output) const noexcept;

    [[nodiscard]] bool matches(
        DetachableMagazineWeaponBinding binding,
        std::uint32_t definition_address) const noexcept;

    // Callers must stop publishers before a session-wide reset. Individual
    // publish/read/clear operations remain atomic and require no mutex.
    void clear_all() noexcept;

private:
    std::array<std::atomic<std::uint64_t>,
               kMagazineChamberLockSlotCount>
        slots_{};
};

}  // namespace wawvr::mod
