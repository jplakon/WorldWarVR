// SPDX-License-Identifier: GPL-3.0-only
#include "support_animation_policy.hpp"

#include <cstdio>
#include <string_view>

namespace {

int failures = 0;

void check(const bool condition, const char* const message) {
    if (!condition) {
        std::fprintf(
            stderr, "support animation policy test failed: %s\n", message);
        ++failures;
    }
}

}  // namespace

int main() {
    using wawvr::mod::native_support_animation_allowed_for_weapon_name;

    check(!native_support_animation_allowed_for_weapon_name("m1carbine") &&
              !native_support_animation_allowed_for_weapon_name(
                  "m1carbine_bayonet") &&
              !native_support_animation_allowed_for_weapon_name(
                  "zombie_m1carbine") &&
              !native_support_animation_allowed_for_weapon_name(
                  "zombie_m1carbine_upgraded") &&
              !native_support_animation_allowed_for_weapon_name("kar98k") &&
              !native_support_animation_allowed_for_weapon_name(
                  "kar98k_scoped_zombie") &&
              !native_support_animation_allowed_for_weapon_name(
                  "mosin_rifle") &&
              !native_support_animation_allowed_for_weapon_name(
                  "springfield") &&
              !native_support_animation_allowed_for_weapon_name(
                  "m1garand") &&
              !native_support_animation_allowed_for_weapon_name(
                  "zombie_m1garand") &&
              !native_support_animation_allowed_for_weapon_name("ppsh") &&
              !native_support_animation_allowed_for_weapon_name("colt") &&
              !native_support_animation_allowed_for_weapon_name("PTRS41"),
          "every well-formed SP weapon identity suppresses native support animation");

    check(native_support_animation_allowed_for_weapon_name("") &&
              native_support_animation_allowed_for_weapon_name(
                  " zombie_m1carbine") &&
              native_support_animation_allowed_for_weapon_name(
                  "kar98k-scoped") &&
              native_support_animation_allowed_for_weapon_name(
                  "mosin rifle"),
          "missing or malformed identities preserve native support animation");

    constexpr char kEmbeddedNullIdentity[] = "m1carbine\0suffix";
    check(native_support_animation_allowed_for_weapon_name(std::string_view{
              kEmbeddedNullIdentity, sizeof(kEmbeddedNullIdentity) - 1}),
          "an embedded terminator is rejected as a malformed bounded identity");

    return failures == 0 ? 0 : 1;
}
