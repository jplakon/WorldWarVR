// SPDX-License-Identifier: GPL-3.0-only
#include "performance_command_logic.hpp"

#include <array>
#include <charconv>
#include <system_error>

namespace wawvr::mod {
namespace {
struct CommandDefinition final {
    PerformanceCommandId id;
    std::string_view name;
    std::string_view native_text;
};
constexpr std::array kCommands{
    CommandDefinition{PerformanceCommandId::baseline, "baseline",
        "fx_cull_elem_draw 0\nr_distortion 1\nr_dlightLimit 4\nr_zfeather 1\n"},
    CommandDefinition{PerformanceCommandId::fxdraw0, "fxdraw0", "fx_cull_elem_draw 0\n"},
    CommandDefinition{PerformanceCommandId::fxdraw1, "fxdraw1", "fx_cull_elem_draw 1\n"},
    CommandDefinition{PerformanceCommandId::distortion0, "distortion0", "r_distortion 0\n"},
    CommandDefinition{PerformanceCommandId::distortion1, "distortion1", "r_distortion 1\n"},
    CommandDefinition{PerformanceCommandId::dlight1, "dlight1", "r_dlightLimit 1\n"},
    CommandDefinition{PerformanceCommandId::dlight4, "dlight4", "r_dlightLimit 4\n"},
    CommandDefinition{PerformanceCommandId::zfeather0, "zfeather0", "r_zfeather 0\n"},
    CommandDefinition{PerformanceCommandId::zfeather1, "zfeather1", "r_zfeather 1\n"},
    CommandDefinition{PerformanceCommandId::load_oki3, "load_oki3", "loadgame oki3-perf.svg\n"},
    CommandDefinition{PerformanceCommandId::toggleconsole, "toggleconsole", "toggleconsole\n"},
    CommandDefinition{PerformanceCommandId::console_close, "console_close", "toggleconsole\n"},
};
}  // namespace

PerformanceCommandRequest parse_performance_command_request(
    std::string_view text) noexcept {
    if (text.empty() || text.size() > kPerformanceCommandMaximumBytes) {
        return {};
    }
    if (text.back() == '\n') {
        text.remove_suffix(1);
        if (!text.empty() && text.back() == '\r') {
            text.remove_suffix(1);
        }
    }
    const auto separator = text.find(' ');
    if (separator == std::string_view::npos || separator == 0 ||
        separator + 1 >= text.size() || text.front() == '0') {
        return {};
    }
    const auto nonce_text = text.substr(0, separator);
    for (const char value : nonce_text) {
        if (value < '0' || value > '9') {
            return {};
        }
    }
    std::uint64_t nonce = 0;
    const auto converted = std::from_chars(
        nonce_text.data(), nonce_text.data() + nonce_text.size(), nonce);
    if (converted.ec != std::errc{} ||
        converted.ptr != nonce_text.data() + nonce_text.size() || nonce == 0) {
        return {};
    }
    const auto name = text.substr(separator + 1);
    for (const auto& command : kCommands) {
        if (name == command.name) {
            return {nonce, command.id};
        }
    }
    return {};
}

std::string_view performance_command_id_name(
    const PerformanceCommandId id) noexcept {
    for (const auto& command : kCommands) {
        if (id == command.id) {
            return command.name;
        }
    }
    return "invalid";
}

std::string_view performance_command_native_text(
    const PerformanceCommandId id, const bool console_open) noexcept {
    if (id == PerformanceCommandId::console_close && !console_open) {
        return {};
    }
    for (const auto& command : kCommands) {
        if (id == command.id) {
            return command.native_text;
        }
    }
    return {};
}

bool performance_command_nonce_is_fresh(
    const PerformanceCommandRequest& request,
    const std::uint64_t last_consumed_nonce) noexcept {
    return request.valid() && request.nonce > last_consumed_nonce;
}

}  // namespace wawvr::mod
