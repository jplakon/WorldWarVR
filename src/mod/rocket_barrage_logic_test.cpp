// SPDX-License-Identifier: GPL-3.0-only
#include "rocket_barrage_logic.hpp"

#include <array>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace wawvr::mod;

void check(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

[[nodiscard]] RocketBarrageAngleGate accepted_gate(
    const std::string_view weapon_name) noexcept {
    return {
        true,
        true,
        0x0176C6F0,
        0x0176C6F0,
        true,
        17,
        9,
        64,
        true,
        weapon_name,
        true,
        true,
        -12.5F,
        87.25F,
    };
}

void check_rejected(
    const RocketBarrageAngleGate& gate,
    const std::string_view message) {
    const auto selection = select_rocket_barrage_angles(gate);
    check(!selection.substitute, message);
    check(selection.effective_weapon_id == 0,
          "rejection cannot publish an effective weapon ID");
    check(selection.angles == std::array<float, 3>{},
          "rejection cannot publish replacement angles");
}

void test_exact_success_and_native_value_preservation(
    const std::string_view weapon_name) {
    const std::array<float, 3> native_angles{31.0F, 42.0F, 53.0F};
    const auto native_before = native_angles;
    const auto gate = accepted_gate(weapon_name);
    const auto selection = select_rocket_barrage_angles(gate);

    check(selection.substitute,
          "exact local SP campaign targeting path is accepted");
    check(selection.effective_weapon_id == gate.primary_weapon_id,
          "nonzero primary weapon ID is selected");
    check(selection.angles[0] == gate.controller_pitch_degrees,
          "replacement pitch follows the controller");
    check(selection.angles[1] == gate.controller_yaw_degrees,
          "replacement yaw follows the controller");
    check(selection.angles[2] == 0.0F,
          "replacement roll is always level");
    check(native_angles == native_before,
          "pure selection never mutates native game angles");
}

void test_runtime_and_entity_gates(const std::string_view weapon_name) {
    const auto accepted = accepted_gate(weapon_name);

    auto rejected = accepted;
    rejected.hook_enabled = false;
    check_rejected(rejected, "disabled hook is rejected");
    rejected = accepted;
    rejected.single_player = false;
    check_rejected(rejected, "multiplayer is rejected");
    rejected = accepted;
    rejected.entity = 0;
    check_rejected(rejected, "null script entity is rejected");
    rejected = accepted;
    rejected.local_player_entity = 0;
    check_rejected(rejected, "null local entity is rejected");
    rejected = accepted;
    rejected.entity += 0x378;
    check_rejected(rejected, "nonlocal script entity is rejected");
    rejected = accepted;
    rejected.client_readable = false;
    check_rejected(rejected, "unreadable client state is rejected");
}

void test_effective_weapon_id_selection_and_bounds(
    const std::string_view weapon_name) {
    const auto accepted = accepted_gate(weapon_name);

    auto fallback = accepted;
    fallback.primary_weapon_id = 0;
    fallback.fallback_weapon_id = 23;
    const auto fallback_selection = select_rocket_barrage_angles(fallback);
    check(fallback_selection.substitute,
          "fallback weapon ID is accepted when primary is zero");
    check(fallback_selection.effective_weapon_id == 23,
          "fallback weapon ID is reported exactly");

    auto primary = accepted;
    primary.primary_weapon_id = 31;
    primary.fallback_weapon_id = 23;
    const auto primary_selection = select_rocket_barrage_angles(primary);
    check(primary_selection.substitute,
          "valid nonzero primary weapon ID is accepted");
    check(primary_selection.effective_weapon_id == 31,
          "primary weapon ID takes precedence over fallback");

    auto rejected = accepted;
    rejected.primary_weapon_id = 0;
    rejected.fallback_weapon_id = 0;
    check_rejected(rejected, "zero effective weapon ID is rejected");
    rejected = accepted;
    rejected.primary_weapon_id = kMaximumSerializedT4WeaponId + 1;
    rejected.fallback_weapon_id = 23;
    rejected.weapon_count = 512;
    check_rejected(
        rejected,
        "invalid nonzero primary ID cannot fall through to fallback");
    rejected = accepted;
    rejected.weapon_count = 0;
    check_rejected(rejected, "zero registered weapon count is rejected");
    rejected = accepted;
    rejected.weapon_count = kMaximumSerializedT4WeaponId + 1;
    check_rejected(
        rejected, "weapon count above the serialized table bound is rejected");
    rejected = accepted;
    rejected.primary_weapon_id = 65;
    rejected.weapon_count = 64;
    check_rejected(rejected, "weapon ID above weapon count is rejected");

    auto maximum = accepted;
    maximum.primary_weapon_id = kMaximumSerializedT4WeaponId;
    maximum.weapon_count = kMaximumSerializedT4WeaponId;
    check(select_rocket_barrage_angles(maximum).substitute,
          "serialized weapon ID 255 is accepted at an exact count bound");
}

void test_exact_weapon_identity(const std::string_view weapon_name) {
    const auto accepted = accepted_gate(weapon_name);

    auto rejected = accepted;
    rejected.weapon_definition_readable = false;
    check_rejected(rejected, "unreadable weapon definition is rejected");
    rejected = accepted;
    rejected.weapon_name = {};
    check_rejected(rejected, "empty weapon name is rejected");
    rejected = accepted;
    rejected.weapon_name = "rocket_barrag";
    check_rejected(rejected, "short near-name is rejected");
    rejected = accepted;
    rejected.weapon_name = "rocket_barrage_mp";
    check_rejected(rejected, "suffixed near-name is rejected");
    rejected = accepted;
    rejected.weapon_name = "Rocket_Barrage";
    check_rejected(rejected, "case-changed near-name is rejected");
    rejected = accepted;
    rejected.weapon_name = "air_suppor";
    check_rejected(rejected, "short air-support near-name is rejected");
    rejected = accepted;
    rejected.weapon_name = "air_support_mp";
    check_rejected(rejected, "suffixed air-support near-name is rejected");
    rejected = accepted;
    rejected.weapon_name = "Air_Support";
    check_rejected(rejected, "case-changed air-support near-name is rejected");
    rejected = accepted;
    rejected.weapon_name = "m1garand";
    check_rejected(rejected, "ordinary weapons preserve native script angles");
}

void test_controller_state_and_finite_angles(
    const std::string_view weapon_name) {
    const auto accepted = accepted_gate(weapon_name);

    auto rejected = accepted;
    rejected.controller_focused = false;
    check_rejected(rejected, "unfocused controller is rejected");
    rejected = accepted;
    rejected.controller_frame_current = false;
    check_rejected(rejected, "stale controller frame is rejected");
    rejected = accepted;
    rejected.controller_pitch_degrees =
        std::numeric_limits<float>::quiet_NaN();
    check_rejected(rejected, "NaN controller pitch is rejected");
    rejected = accepted;
    rejected.controller_pitch_degrees =
        std::numeric_limits<float>::infinity();
    check_rejected(rejected, "infinite controller pitch is rejected");
    rejected = accepted;
    rejected.controller_yaw_degrees =
        std::numeric_limits<float>::quiet_NaN();
    check_rejected(rejected, "NaN controller yaw is rejected");
    rejected = accepted;
    rejected.controller_yaw_degrees =
        -std::numeric_limits<float>::infinity();
    check_rejected(rejected, "infinite controller yaw is rejected");

    auto zero = accepted;
    zero.controller_pitch_degrees = 0.0F;
    zero.controller_yaw_degrees = 0.0F;
    check(select_rocket_barrage_angles(zero).substitute,
          "finite zero controller angles are accepted");
}

void test_bounded_terminated_weapon_name_resolution() {
    using namespace std::string_view_literals;
    static_assert(kCampaignTargetingWeaponNameBytes == 15);

    check(resolve_campaign_targeting_weapon_name("rocket_barrage\0"sv) ==
              kRocketBarrageWeaponName,
          "rocket-barrage terminator at the final allowed byte is accepted");
    check(resolve_campaign_targeting_weapon_name("air_support\0"sv) ==
              kAirSupportWeaponName,
          "shorter air-support terminated span is accepted");

    constexpr auto air_support_storage = "air_support\0XYZ"sv;
    static_assert(air_support_storage.size() ==
                  kCampaignTargetingWeaponNameBytes);
    const auto resolved =
        resolve_campaign_targeting_weapon_name(air_support_storage);
    check(resolved == kAirSupportWeaponName && resolved.size() == 11 &&
              resolved.data() == air_support_storage.data(),
          "air support uses its first terminator and ignores following storage");
    auto gate = accepted_gate(kAirSupportWeaponName);
    gate.weapon_name = resolved;
    check(select_rocket_barrage_angles(gate).substitute,
          "shorter runtime name passes the same campaign substitution gate");

    for (const auto rejected : {
             ""sv, "\0"sv, "m1garand\0"sv,
             "rocket_barrag\0"sv, "air_suppor\0"sv,
             "rocket_barrage_mp\0"sv, "air_support_mp\0"sv,
             "Rocket_Barrage\0"sv, "Air_Support\0"sv,
             "rocket_barrage"sv, "air_support"sv,
             "rocket_barrag"sv, "air_suppor"sv,
             "rocket_barrageX\0"sv, "air_supportXYZ\0"sv,
             "\0air_support\0"sv}) {
        check(resolve_campaign_targeting_weapon_name(rejected).empty(),
              "unsupported, malformed, or unterminated name is rejected");
    }
}

}  // namespace

int main() {
    test_bounded_terminated_weapon_name_resolution();
    for (const auto weapon_name :
         {kRocketBarrageWeaponName, kAirSupportWeaponName}) {
        test_exact_success_and_native_value_preservation(weapon_name);
        test_runtime_and_entity_gates(weapon_name);
        test_effective_weapon_id_selection_and_bounds(weapon_name);
        test_exact_weapon_identity(weapon_name);
        test_controller_state_and_finite_angles(weapon_name);
    }
    return 0;
}
