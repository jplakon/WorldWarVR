// SPDX-License-Identifier: GPL-3.0-only
#include "two_hand_sight_alignment_policy.hpp"

#include <cstdio>
#include <string_view>

namespace {

int failures = 0;

void check(const bool condition, const char* const message) {
    if (!condition) {
        std::fprintf(
            stderr, "two-hand sight alignment policy test failed: %s\n",
            message);
        ++failures;
    }
}

}  // namespace

int main() {
    using wawvr::mod::
        controller_owned_two_hand_sight_axis_for_weapon_name;

    check(controller_owned_two_hand_sight_axis_for_weapon_name("m1carbine") &&
              controller_owned_two_hand_sight_axis_for_weapon_name(
                  "m1carbine_bayonet") &&
              controller_owned_two_hand_sight_axis_for_weapon_name(
                  "zombie_m1carbine") &&
              controller_owned_two_hand_sight_axis_for_weapon_name(
                  "zombie_m1carbine_upgraded") &&
              controller_owned_two_hand_sight_axis_for_weapon_name(
                  "kar98k"),
          "the exact controller-owned M1 Carbine and Kar98 identities use controller-owned sight alignment");

    check(!controller_owned_two_hand_sight_axis_for_weapon_name("") &&
              !controller_owned_two_hand_sight_axis_for_weapon_name(
                  "M1CARBINE") &&
              !controller_owned_two_hand_sight_axis_for_weapon_name(
                  "KAR98K") &&
              !controller_owned_two_hand_sight_axis_for_weapon_name(
                  "kar98k_scoped") &&
              !controller_owned_two_hand_sight_axis_for_weapon_name(
                  "kar98k_extra") &&
              !controller_owned_two_hand_sight_axis_for_weapon_name(
                  "m1carbine_scoped") &&
              !controller_owned_two_hand_sight_axis_for_weapon_name(
                  "m1carbine_scoped_extra") &&
              !controller_owned_two_hand_sight_axis_for_weapon_name(
                  "zombie_m1carbine_upgraded_extra") &&
              !controller_owned_two_hand_sight_axis_for_weapon_name(
                  "m1garand") &&
              !controller_owned_two_hand_sight_axis_for_weapon_name(
                  "springfield"),
          "nearby and unrelated identities keep their existing placement");

    constexpr char kEmbeddedNullIdentity[] = "m1carbine\0suffix";
    check(!controller_owned_two_hand_sight_axis_for_weapon_name(
              std::string_view{kEmbeddedNullIdentity,
                               sizeof(kEmbeddedNullIdentity) - 1}),
          "bounded comparison rejects an exact prefix followed by hidden data");

    return failures == 0 ? 0 : 1;
}
