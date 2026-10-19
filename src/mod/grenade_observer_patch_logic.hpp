// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>

namespace wawvr::mod {

struct GrenadeObserverPatchResult final {
    bool installed{};
    bool rollbackVerified{};
    // Immutable original targets remain needed by any possible pass-through
    // adapter. Even a verified rollback does not authorize dropping them here.
    bool retainPassThroughDependencies{true};
};

// This only orchestrates caller-supplied operations. A synthetic Operations
// implementation is a unit model, never native patch/ABI/protection evidence.
// Operations must verify saved original page protections, not substitute a
// permissive protection. Every cleanup is attempted independently on failure.
template<std::size_t Count, class Operations>
[[nodiscard]] GrenadeObserverPatchResult install_grenade_observer_spans(
    Operations& operations) noexcept {
    static_assert(Count != 0, "An observer needs at least one exact native seam");
    GrenadeObserverPatchResult result{};
    const auto attempt = [](auto&& operation) noexcept {
        try {
            return static_cast<bool>(operation());
        } catch (...) {
            return false;
        }
    };

    bool patched = true;
    for (std::size_t index = 0; index < Count && patched; ++index) {
        patched = attempt([&]() { return operations.patch(index); });
    }
    if (patched) {
        const bool protectionsMatch = attempt(
            [&]() { return operations.protections_match(); });
        if (protectionsMatch) {
            result.installed = true;
            return result;
        }
    }

    // Do not combine these calls with &&: failure/exception in one cleanup
    // must not skip the other CALL, protection restoration, or final readback.
    bool restored = true;
    for (std::size_t index = 0; index < Count; ++index) {
        const bool current = attempt([&]() { return operations.restore(index); });
        restored = current && restored;
    }
    const bool protectionsRestored = attempt(
        [&]() { return operations.restore_protections(); });
    const bool originalsMatch = attempt(
        [&]() { return operations.originals_match(); });
    result.rollbackVerified = restored &&
        protectionsRestored && originalsMatch;
    return result;
}

template<class Operations>
[[nodiscard]] GrenadeObserverPatchResult install_grenade_observer_calls(
    Operations& operations) noexcept {
    return install_grenade_observer_spans<2>(operations);
}

}  // namespace wawvr::mod
