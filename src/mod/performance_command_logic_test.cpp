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
        "toggleconsole", "console_close", "test_load_nacht", "test_load_mak",
        "test_load_pel1", "test_load_ber1", "test_load_sniper", "test_load_ber3", "test_colt", "test_garand",
        "test_carbine", "test_thompson", "test_type100", "test_bar",
        "test_springfield", "test_mosin", "test_svt", "test_ppsh",
        "test_kar98k", "test_ammo", "test_observe_safely",
        "test_load_factory", "test_load_asylum", "test_load_pel2",
        "test_load_oki2", "test_load_mak_event2", "test_load_oki2_event2",
        "test_load_sniper_event4", "test_load_ber3_reich"};
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
    require(performance_command_native_text(PerformanceCommandId::test_load_sniper, false) ==
        "devmap sniper\n", "Vendetta is a fixed simulator fixture, not arbitrary command text");
    require(performance_command_native_text(PerformanceCommandId::test_load_ber3, false) ==
        "devmap ber3\n", "Accepted SVT mission is a fixed simulator fixture");
    require(performance_command_native_text(PerformanceCommandId::test_load_mak_event2, false) ==
        "set start event2\ndevmap mak\n", "Makin checkpoint exactly reuses the existing Pacific magazine fixture");
    require(performance_command_native_text(PerformanceCommandId::test_load_oki2_event2, false) ==
        "set start event2\nspdevmap oki2\n", "Okinawa checkpoint exactly reuses the existing wet inventory fixture");
    require(performance_command_native_text(PerformanceCommandId::test_load_sniper_event4, false) ==
        "set start event4\nspdevmap sniper\n", "Vendetta checkpoint exactly reuses the existing scoped Mosin fixture");
    require(performance_command_native_text(PerformanceCommandId::test_load_ber3_reich, false) ==
        "set start reich\nspdevmap ber3\n", "Ber3 checkpoint exactly reuses the existing Russian magazine fixture");
    for (const auto* const name : {"test_load_mak_event2", "test_load_oki2_event2",
                                  "test_load_sniper_event4", "test_load_ber3_reich"}) {
        const auto request = parse_performance_command_request(std::string("732 ") + name);
        require(request.valid() && performance_command_id_name(request) == name &&
            performance_command_native_text(request, true) ==
                performance_command_native_text(request.id, false) &&
            performance_command_nonce_is_fresh(request, 731) &&
            !performance_command_nonce_is_fresh(request, 732),
            "exact checkpoint requests preserve fixed text, full request identity and replay protection");
    }
    require(performance_command_native_text(PerformanceCommandId::test_mosin, false) ==
        "take all\ngive mosin_rifle\n", "Mosin fixture uses the observed registered unscoped weapon name");
    require(parse_performance_command_request("9 fxdraw0\n").valid() &&
        parse_performance_command_request("9 fxdraw0\r\n").valid(), "LF and CRLF accepted");
}

void malformed_and_injection_rejected() {
    constexpr std::array invalid{
        "", "0 fxdraw0", "01 fxdraw0", "-1 fxdraw0", "+1 fxdraw0",
        "1", "1 ", " 1 fxdraw0", "1\tfxdraw0", "1  fxdraw0", "1 fxdraw0 ",
        "1 FXDRAW0", "1 fxdraw2", "1 quit", "1 loadgame oki3-perf.svg",
        "1 fxdraw0;quit", "1 fxdraw0\nquit", "1 fxdraw0\n2 fxdraw1\n",
        "1 fxdraw0\n\n", "18446744073709551616 fxdraw0", "1e2 fxdraw0",
        "1 test_load_mak_event3", "1 test_load_oki2_event4",
        "1 test_load_sniper_event4;quit", "1 test_load_ber3_reich\nquit",
        "1 test_load_mak_event2 event4", "1 set start event2"};
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

void finite_all_weapon_aim_fixtures() {
    require(kAllWeaponAimTestDefinitions.size() == 129,
        "finite installed SP and existing source-fixture inventory is explicit");
    std::size_t index = 0;
    for (const auto& fixture : kAllWeaponAimTestDefinitions) {
        const auto text = std::string("731 ") + std::string(fixture.request_name);
        const auto request = parse_performance_command_request(text);
        require(request.valid() && request.nonce == 731 &&
            request.id == PerformanceCommandId::test_aim_weapon &&
            request.aim_weapon_index == index,
            "every finite exact weapon request parses with its own index");
        require(performance_command_id_name(request) == fixture.request_name,
            "full request preserves exact weapon name in native ACK");
        const auto command = performance_command_native_text(request, false);
        require(command == fixture.native_text && command.back() == '\n' &&
            command.data()[command.size()] == '\0',
            "every fixture returns static newline and NUL terminated text");
        const auto expected = std::string("take weapons\nwait 30\ngive ") +
            std::string(fixture.weapon) + "\ngive ammo\n";
        require(command == expected,
            "native inventory fixture is exact literal and contains no caller text");
        require(command.find("god") == std::string_view::npos &&
            command.find("notarget") == std::string_view::npos,
            "cycling a fixture never toggles map protection off");
        require(performance_command_nonce_is_fresh(request, 730) &&
            !performance_command_nonce_is_fresh(request, 731),
            "weapon selection obeys the existing no-replay nonce gate");
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            require(kAllWeaponAimTestDefinitions[earlier].request_name !=
                fixture.request_name, "fixture request names are unique");
        }
        ++index;
    }
    constexpr std::array rejected{
        "1 test_aim_", "1 test_aim_COLT", "1 test_aim_nonexistent",
        "1 test_aim_defaultweapon", "1 test_aim_pby_20mm",
        "1 test_aim_zombie_melee", "1 test_aim_colt;quit",
        "1 test_aim_colt\nquit", "1 test_aim_colt ",
        "1 test_aim_colt\n2 test_aim_bar", "1 test_aim_colt;give ammo"};
    for (const auto* text : rejected) {
        require(!parse_performance_command_request(text).valid(),
            "unknown, excluded, case-changed and injected aim fixtures reject");
    }
    PerformanceCommandRequest invalid{9,
        PerformanceCommandId::test_aim_weapon,
        static_cast<std::uint16_t>(kAllWeaponAimTestDefinitions.size())};
    require(!invalid.valid() &&
        performance_command_native_text(invalid, true).empty() &&
        performance_command_id_name(invalid) == "invalid",
        "out-of-bounds fixture index cannot reach static text");
    invalid.aim_weapon_index = kInvalidAllWeaponAimTestIndex;
    require(!invalid.valid() &&
        performance_command_native_text(invalid, false).empty(),
        "default invalid index never aliases the first weapon");
    require(performance_command_native_text(
        PerformanceCommandId::test_aim_weapon, false).empty(),
        "legacy id-only API cannot silently choose a weapon");
}
}  // namespace

int main() {
    exact_allowlist();
    malformed_and_injection_rejected();
    nonce_and_console_rules();
    finite_all_weapon_aim_fixtures();
    std::cout << "Performance command allowlist, parser and replay checks passed\n";
    return 0;
}
