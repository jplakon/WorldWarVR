// SPDX-License-Identifier: GPL-3.0-only
#include "personal_weapon_alignment.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
using namespace wawvr;
int failures{};

void check(const bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "personal weapon alignment: %s\n", message);
        ++failures;
    }
}

bool near(float left, float right) { return std::abs(left - right) < 0.00002F; }
bool near(const xr::Vec3f& left, const xr::Vec3f& right) {
    return near(left.x, right.x) && near(left.y, right.y) && near(left.z, right.z);
}

xr::FrameState frame_fixture() {
    xr::FrameState frame{};
    frame.frame_id = 25;
    frame.views_valid = true;
    frame.actions.sequence = 29;
    frame.actions.focused = true;
    for (unsigned index = 0; index < xr::kHandCount; ++index) {
        auto& hand = frame.actions.hands[index];
        hand.aim.active = hand.aim.orientation_valid = true;
        hand.aim.orientation_tracked = true;
        hand.grip.active = hand.grip.position_valid = true;
        hand.grip.pose.position = {0.2F * index, 1.1F, -0.45F};
        hand.trigger.active = hand.squeeze.active = true;
        hand.trigger.current = 0.72F;
        hand.squeeze.current = 1.0F;
    }
    return frame;
}

void test_strict_parser() {
    float output = 7.0F;
    check(mod::parse_personal_weapon_angle(" -12.5\t", &output) && output == -12.5F,
        "bounded signed value with surrounding whitespace parses");
    check(mod::parse_personal_weapon_angle("+35", &output) && output == 35.0F,
        "positive endpoint parses");
    check(mod::parse_personal_weapon_angle("-35", &output) && output == -35.0F,
        "negative endpoint parses");
    for (const auto invalid : {"", "+", "nan", "inf", "-infinity", "35.001",
             "-35.001", "10deg", "1,5", "--3", "+-3", "++3", "1 2", "1e1000"}) {
        const float before = output;
        check(!mod::parse_personal_weapon_angle(invalid, &output) && output == before,
            "invalid values fail without clamping or changing the output");
    }
    check(!mod::parse_personal_weapon_angle("1", nullptr), "null parser output rejected");
}

void test_zero_exact_and_invalid_transaction() {
    auto frame = frame_fixture();
    frame.actions.hands[1].aim.pose.orientation = {0.0F, 0.0F, 0.0F, 1.1F};
    const auto original = frame;
    check(mod::apply_personal_weapon_alignment({}, &frame) &&
        std::memcmp(&frame, &original, sizeof(frame)) == 0,
        "default zero is an exact byte-preserving no-op");
    for (const auto invalid : {
             mod::PersonalWeaponAlignment{36.0F, 0.0F, 0.0F},
             mod::PersonalWeaponAlignment{0.0F, -36.0F, 0.0F},
             mod::PersonalWeaponAlignment{0.0F, 0.0F, 36.0F},
             mod::PersonalWeaponAlignment{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}}) {
        check(!mod::apply_personal_weapon_alignment(invalid, &frame) &&
            std::memcmp(&frame, &original, sizeof(frame)) == 0,
            "invalid calibration leaves the complete frame unchanged");
    }
    frame.actions.hands[1].aim.orientation_valid = false;
    const auto unavailable = frame;
    check(!mod::apply_personal_weapon_alignment({10.0F, 0.0F, 0.0F}, &frame) &&
        std::memcmp(&frame, &unavailable, sizeof(frame)) == 0,
        "unavailable tracking remains unavailable and untouched");
    check(!mod::apply_personal_weapon_alignment({}, nullptr), "null frame rejected");
}

void test_axis_signs_and_local_composition() {
    constexpr float radians = 3.14159265358979323846F / 180.0F;
    const float sine = std::sin(20.0F * radians);
    const float cosine = std::cos(20.0F * radians);
    auto pitch = frame_fixture();
    check(mod::apply_personal_weapon_alignment({20.0F, 0.0F, 0.0F}, &pitch),
        "pitch applies");
    check(near(xr::Rotate(pitch.actions.hands[1].aim.pose.orientation, {0, 0, -1}),
        {0, sine, -cosine}), "positive pitch raises the muzzle");
    auto yaw = frame_fixture();
    check(mod::apply_personal_weapon_alignment({0.0F, 20.0F, 0.0F}, &yaw),
        "yaw applies");
    check(near(xr::Rotate(yaw.actions.hands[1].aim.pose.orientation, {0, 0, -1}),
        {-sine, 0, -cosine}), "positive yaw moves muzzle left");
    auto roll = frame_fixture();
    check(mod::apply_personal_weapon_alignment({0.0F, 0.0F, 20.0F}, &roll),
        "roll applies");
    check(near(xr::Rotate(roll.actions.hands[1].aim.pose.orientation, {0, 0, -1}),
        {0, 0, -1}), "roll preserves the forward aim ray");

    const mod::PersonalWeaponAlignment fit{12.0F, -7.0F, 9.0F};
    auto neutral = frame_fixture();
    check(mod::apply_personal_weapon_alignment(fit, &neutral), "combined fit applies");
    const auto local = neutral.actions.hands[1].aim.pose.orientation;
    const std::array<xr::Quaternionf, 3> physical_rotations{{
        {0.0F, std::sin(0.5F), 0.0F, std::cos(0.5F)},
        {0.0F, 0.0F, std::sin(0.4F), std::cos(0.4F)},
        xr::Normalize({0.2F, -0.4F, 0.1F, 0.8F}),
    }};
    for (const auto& physical : physical_rotations) {
        auto rotated = frame_fixture();
        rotated.actions.hands[1].aim.pose.orientation = physical;
        const auto before = rotated;
        check(mod::apply_personal_weapon_alignment(fit, &rotated),
            "fit applies to yawed/rolled/oblique controller");
        for (const auto axis : {xr::Vec3f{0, 0, -1}, xr::Vec3f{0, 1, 0}, xr::Vec3f{1, 0, 0}}) {
            check(near(xr::Rotate(rotated.actions.hands[1].aim.pose.orientation, axis),
                xr::Rotate(physical, xr::Rotate(local, axis))),
                "controller-local fit rotates with the controller, not headset/world");
        }
        const auto adjusted = rotated.actions.hands[1].aim.pose.orientation;
        rotated.actions.hands[1].aim.pose.orientation = before.actions.hands[1].aim.pose.orientation;
        check(std::memcmp(&rotated, &before, sizeof(before)) == 0,
            "only right aim orientation changes; grips, left pouch, head, controls stay exact");
        rotated.actions.hands[1].aim.pose.orientation = adjusted;
    }
}
} // namespace

int main() {
    test_strict_parser();
    test_zero_exact_and_invalid_transaction();
    test_axis_signs_and_local_composition();
    if (failures != 0) return 1;
    std::puts("personal weapon alignment checks passed");
    return 0;
}
