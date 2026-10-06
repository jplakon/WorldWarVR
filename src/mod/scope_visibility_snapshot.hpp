#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <span>

namespace wawvr::mod {

inline constexpr std::size_t kScopeVisibilityBankCount = 7;
inline constexpr std::size_t kScopeVisibilityMaxEntities = 4096;

// Retail T4 reuses its camera/shadow entity visibility banks for successive
// scene views. In particular, a narrow scope frustum can mark an entity culled
// (2), and a later eye's portal walk will only reconsider unknown entries (0).
// Keep the exact pre-scope state so that the optional view cannot decide what
// the normal eyes see. The platform adapter owns address/lifetime validation;
// every supplied span must remain writable until restore or destruction.
class ScopeVisibilitySnapshot final {
public:
    using Banks =
        std::array<std::span<std::byte>, kScopeVisibilityBankCount>;

    ScopeVisibilitySnapshot() noexcept = default;
    ~ScopeVisibilitySnapshot() { restore(); }
    ScopeVisibilitySnapshot(const ScopeVisibilitySnapshot&) = delete;
    ScopeVisibilitySnapshot& operator=(const ScopeVisibilitySnapshot&) = delete;
    ScopeVisibilitySnapshot(ScopeVisibilitySnapshot&&) = delete;
    ScopeVisibilitySnapshot& operator=(ScopeVisibilitySnapshot&&) = delete;

    [[nodiscard]] bool capture(const Banks& banks) noexcept {
        // Reject before touching any bank, including a second capture that
        // would otherwise discard the still-active transaction's baseline.
        if (active_ || banks[0].empty() ||
            banks[0].size() > kScopeVisibilityMaxEntities) {
            return false;
        }
        const std::size_t entity_count = banks[0].size();
        for (const auto bank : banks) {
            if (bank.data() == nullptr || bank.size() != entity_count) {
                return false;
            }
        }
        for (std::size_t bank = 0; bank < banks.size(); ++bank) {
            std::memcpy(baseline_[bank].data(), banks[bank].data(), entity_count);
        }
        banks_ = banks;
        active_ = true;
        return true;
    }

    void restore() noexcept {
        if (!active_) {
            return;
        }
        for (std::size_t bank = 0; bank < banks_.size(); ++bank) {
            std::memcpy(
                banks_[bank].data(), baseline_[bank].data(), banks_[bank].size());
        }
        active_ = false;
        banks_ = {};
    }

    [[nodiscard]] bool active() const noexcept { return active_; }

    [[nodiscard]] std::size_t changed_bytes() const noexcept {
        if (!active_) {
            return 0;
        }
        std::size_t changed = 0;
        for (std::size_t bank = 0; bank < banks_.size(); ++bank) {
            for (std::size_t entity = 0; entity < banks_[bank].size(); ++entity) {
                if (banks_[bank][entity] != baseline_[bank][entity]) {
                    ++changed;
                }
            }
        }
        return changed;
    }

private:
    // Do not zero this hot-path scratch storage. capture fills exactly the
    // bytes restore will read, and inactive instances never read it.
    std::array<std::array<std::byte, kScopeVisibilityMaxEntities>,
               kScopeVisibilityBankCount> baseline_;
    Banks banks_{};
    bool active_{};
};

}  // namespace wawvr::mod
