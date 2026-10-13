// SPDX-License-Identifier: GPL-3.0-only
#include "performance_command_logic.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {
using namespace wawvr::mod;

void require(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void exact_allowlist() {
    constexpr std::array names{
        "baseline", "fxdraw0", "fxdraw1", "distortion0", "distortion1",
        "dlight1", "dlight4", "zfeather0", "zfeather1", "load_oki3",
        "toggleconsole", "console_close"};
    for (const auto* const name : names) {
        const auto request = parse_performance_command_request(std::string("42 ") + name);
        require(request.valid() && request.nonce == 42, "every exact allowlisted id parses");
        require(performance_command_id_name(request.id) == name, "id roundtrip is exact");
        const auto command = performance_command_native_text(request.id, true);
        require(!command.empty() && command.back() == '\n', "every native command terminates with newline");
        require(command.data()[command.size()] == '\0', "every returned native command has static NUL termination");
    }
    require(performance_command_native_text(PerformanceCommandId::baseline, false) ==
        "fx_cull_elem_draw 0\nr_distortion 1\nr_dlightLimit 4\nr_zfeather 1\n",
        "baseline restores only the four agreed rendering variables");
    require(performance_command_native_text(PerformanceCommandId::load_oki3, false) ==
        "loadgame oki3-perf.svg\n", "checkpoint path is fixed, not caller controlled");
    require(parse_performance_command_request("9 fxdraw0\n").valid() &&
        parse_performance_command_request("9 fxdraw0\r\n").valid(), "LF and CRLF accepted");
}

void malformed_and_injection_rejected() {
    constexpr std::array invalid{
        "", "0 fxdraw0", "01 fxdraw0", "-1 fxdraw0", "+1 fxdraw0",
        "1", "1 ", " 1 fxdraw0", "1\tfxdraw0", "1  fxdraw0", "1 fxdraw0 ",
        "1 FXDRAW0", "1 fxdraw2", "1 quit", "1 loadgame oki3-perf.svg",
        "1 fxdraw0;quit", "1 fxdraw0\nquit", "1 fxdraw0\n2 fxdraw1\n",
        "1 fxdraw0\n\n", "18446744073709551616 fxdraw0", "1e2 fxdraw0"};
    for (const auto* const text : invalid) {
        require(!parse_performance_command_request(text).valid(), "malformed and injected requests rejected");
    }
    const std::string embedded_nul("1 fxdraw0\0quit", 14);
    require(!parse_performance_command_request(embedded_nul).valid(), "embedded NUL rejected");
    require(!parse_performance_command_request(std::string(129, '1')).valid(), "oversized input rejected");
    require(!parse_performance_command_request("\xEF\xBB\xBF" "1 fxdraw0").valid(), "BOM rejected, protocol is ASCII");
    require(performance_command_native_text(PerformanceCommandId::none, true).empty(), "unknown id has no engine text");
}

void nonce_and_console_rules() {
    auto request = parse_performance_command_request("100 fxdraw1");
    require(performance_command_nonce_is_fresh(request, 99), "new nonce is fresh");
    require(!performance_command_nonce_is_fresh(request, 100), "same nonce never repeats");
    require(!performance_command_nonce_is_fresh(request, 101), "older nonce never replays");
    require(!performance_command_nonce_is_fresh({}, 0), "invalid request cannot advance state");
    request = parse_performance_command_request("18446744073709551615 baseline");
    require(request.valid() && request.nonce == std::numeric_limits<std::uint64_t>::max(), "maximum unsigned nonce accepted without overflow");
    require(performance_command_native_text(PerformanceCommandId::console_close, false).empty(), "closing closed console is an acknowledged no-op");
    require(performance_command_native_text(PerformanceCommandId::console_close, true) == "toggleconsole\n", "open console closes through fixed native command");
    require(performance_command_native_text(PerformanceCommandId::toggleconsole, false) == "toggleconsole\n", "explicit toggle remains available");
}
}  // namespace

int main() {
    exact_allowlist();
    malformed_and_injection_rejected();
    nonce_and_console_rules();
    std::cout << "Performance command allowlist, parser and replay checks passed\n";
    return 0;
}
