// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace wawvr::mod {

inline constexpr std::size_t kPerformanceCommandMaximumBytes = 128;

enum class PerformanceCommandId : std::uint8_t {
    none,
    baseline,
    fxdraw0,
    fxdraw1,
    distortion0,
    distortion1,
    dlight1,
    dlight4,
    zfeather0,
    zfeather1,
    load_oki3,
    toggleconsole,
    console_close,
    // Fixed simulator-only fixtures. The service additionally requires the
    // actual private Tester module and an explicit child command-file path.
    test_load_nacht,
    test_load_mak,
    test_load_pel1,
    test_load_ber1,
    test_load_sniper,
    test_load_ber3,
    test_colt,
    test_garand,
    test_carbine,
    test_thompson,
    test_type100,
    test_bar,
    test_springfield,
    test_mosin,
    test_svt,
    test_ppsh,
    test_kar98k,
    test_ammo,
    test_observe_safely,
};

struct PerformanceCommandRequest final {
    std::uint64_t nonce{};
    PerformanceCommandId id{PerformanceCommandId::none};
    [[nodiscard]] bool valid() const noexcept {
        return nonce != 0 && id != PerformanceCommandId::none;
    }
};

// A single "positive-decimal-nonce allowlisted-id" line; never engine text.
[[nodiscard]] PerformanceCommandRequest parse_performance_command_request(
    std::string_view text) noexcept;
[[nodiscard]] std::string_view performance_command_id_name(
    PerformanceCommandId id) noexcept;
// All nonempty results have static storage and a terminating newline and NUL.
// Closing an already closed console intentionally yields an empty command.
[[nodiscard]] std::string_view performance_command_native_text(
    PerformanceCommandId id, bool console_open) noexcept;
[[nodiscard]] bool performance_command_nonce_is_fresh(
    const PerformanceCommandRequest& request,
    std::uint64_t last_consumed_nonce) noexcept;

}  // namespace wawvr::mod
