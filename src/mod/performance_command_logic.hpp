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
