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
    CommandDefinition{PerformanceCommandId::test_load_nacht, "test_load_nacht", "devmap nazi_zombie_prototype\n"},
    CommandDefinition{PerformanceCommandId::test_load_mak, "test_load_mak", "devmap mak\n"},
    CommandDefinition{PerformanceCommandId::test_load_pel1, "test_load_pel1", "devmap pel1\n"},
    CommandDefinition{PerformanceCommandId::test_load_ber1, "test_load_ber1", "devmap ber1\n"},
    CommandDefinition{PerformanceCommandId::test_load_sniper, "test_load_sniper", "devmap sniper\n"},
    CommandDefinition{PerformanceCommandId::test_load_ber3, "test_load_ber3", "devmap ber3\n"},
    CommandDefinition{PerformanceCommandId::test_colt, "test_colt", "take all\ngive colt\n"},
    CommandDefinition{PerformanceCommandId::test_garand, "test_garand", "take all\ngive m1garand\n"},
    CommandDefinition{PerformanceCommandId::test_carbine, "test_carbine", "take all\ngive m1carbine\n"},
    CommandDefinition{PerformanceCommandId::test_thompson, "test_thompson", "take all\ngive thompson\n"},
    CommandDefinition{PerformanceCommandId::test_type100, "test_type100", "take all\ngive type100_smg\n"},
    CommandDefinition{PerformanceCommandId::test_bar, "test_bar", "take all\ngive bar\n"},
    CommandDefinition{PerformanceCommandId::test_springfield, "test_springfield", "take all\ngive springfield\n"},
    CommandDefinition{PerformanceCommandId::test_mosin, "test_mosin", "take all\ngive mosin_rifle\n"},
    CommandDefinition{PerformanceCommandId::test_svt, "test_svt", "take all\ngive svt40\n"},
    CommandDefinition{PerformanceCommandId::test_ppsh, "test_ppsh", "take all\ngive ppsh\n"},
    CommandDefinition{PerformanceCommandId::test_kar98k, "test_kar98k", "take all\ngive kar98k\n"},
    CommandDefinition{PerformanceCommandId::test_ammo, "test_ammo", "give ammo\n"},
    CommandDefinition{PerformanceCommandId::test_observe_safely, "test_observe_safely", "god\nnotarget\n"},
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
