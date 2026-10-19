// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <atomic>
#include <cstdint>

namespace wawvr::mod {

// Observational data only. Request values are sampled AFTER the original
// installer returns, not substituted for or claimed as its gate evaluation.
struct ServerImpactInstallRequestObservation final {
    std::uint32_t length_result{};
    bool exact_one{};
};
struct ServerImpactInstallDiagnosticSnapshot final {
    bool status_known{};
    std::uint32_t status{};
    bool installed_at_record{};
    bool enabled_at_record{};
    ServerImpactInstallRequestObservation post_gameplay{};
    ServerImpactInstallRequestObservation post_server_impact{};
    ServerImpactInstallRequestObservation post_disable_xr{};
};

// Process-lifetime one-shot: the immutable original install result is never
// replaced by a retry, later environment change, map change, or later status.
// An unready logger or null output MUST NOT spend the emission opportunity.
class DeferredServerImpactInstallDiagnostic final {
public:
    [[nodiscard]] bool record(
        const ServerImpactInstallDiagnosticSnapshot& snapshot) noexcept {
        unsigned expected = 0;
        if (!phase_.compare_exchange_strong(
                expected, 1, std::memory_order_acq_rel,
                std::memory_order_relaxed)) return false;
        snapshot_ = snapshot;
        phase_.store(2, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool claim(
        const bool logger_ready,
        ServerImpactInstallDiagnosticSnapshot* const output) noexcept {
        if (!logger_ready || output == nullptr) return false;
        unsigned expected = 2;
        if (!phase_.compare_exchange_strong(
                expected, 3, std::memory_order_acq_rel,
                std::memory_order_relaxed)) return false;
        *output = snapshot_;
        return true;
    }

private:
    std::atomic<unsigned> phase_{0}; // empty, writing, recorded, emitted
    ServerImpactInstallDiagnosticSnapshot snapshot_{};
};

[[nodiscard]] constexpr const char* server_impact_install_status_name(
    const std::uint32_t status) noexcept {
    switch (status) {
    case 0: return "installed";
    case 1: return "already-installed";
    case 2: return "disabled";
    case 3: return "unsupported";
    case 4: return "fingerprint-mismatch";
    case 5: return "thread-suspend-failed";
    case 6: return "patch-failed";
    default: return "unknown-status";
    }
}

} // namespace wawvr::mod
