// SPDX-License-Identifier: GPL-3.0-only
// Additive pure production-helper regressions. No game, renderer, process or
// physical motion-to-photon acceptance is represented by this executable.
#include "weapon_placement.hpp"
#include "weapon_grip_logic.hpp"
#include "xr_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
namespace mod = wawvr::mod;
namespace xr = wawvr::xr;
constexpr float kRadiansPerDegree = 0.017453292519943295F;
std::uint64_t checks{};

void check(const bool passed, const std::string_view message) {
    ++checks;
    if (!passed) {
        std::fprintf(stderr, "FAIL: %.*s\n", static_cast<int>(message.size()),
                     message.data());
        throw std::runtime_error("handling continuity regression");
    }
}
bool same(const xr::Vec3f& a, const xr::Vec3f& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
bool same(const xr::Quaternionf& a, const xr::Quaternionf& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}
bool same(const mod::ControllerWeaponPoseFilterState& a,
          const mod::ControllerWeaponPoseFilterState& b) {
    return a.valid == b.valid && a.generation == b.generation &&
        a.publication_milliseconds == b.publication_milliseconds &&
        same(a.filtered_grip_position, b.filtered_grip_position) &&
        same(a.filtered_aim_orientation, b.filtered_aim_orientation);
}
float length(const xr::Vec3f& a) {
    return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
}
xr::Vec3f delta(const xr::Vec3f& a, const xr::Vec3f& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
xr::Quaternionf axis_rotation(const int axis, const float degrees) {
    xr::Quaternionf result{};
    const float sine = std::sin(degrees * kRadiansPerDegree * .5F);
    result.w = std::cos(degrees * kRadiansPerDegree * .5F);
    if (axis == 0) result.x = sine;
    else if (axis == 1) result.y = sine;
    else result.z = sine;
    return result;
}
float angle(const xr::Quaternionf& a, const xr::Quaternionf& b) {
    const auto difference = xr::Multiply(xr::Conjugate(a), b);
    return 2.0F * std::atan2(std::sqrt(difference.x * difference.x +
            difference.y * difference.y + difference.z * difference.z),
        std::abs(difference.w)) / kRadiansPerDegree;
}
xr::HandActionState tracked_hand() {
    xr::HandActionState hand{};
    hand.grip.active = hand.grip.position_valid = hand.grip.orientation_valid = true;
    hand.grip.position_tracked = hand.grip.orientation_tracked = true;
    hand.aim.active = hand.aim.position_valid = hand.aim.orientation_valid = true;
    hand.aim.position_tracked = hand.aim.orientation_tracked = true;
    hand.grip.pose.position = {.20F, -.28F, -.44F};
    return hand;
}

void test_three_axis_position_and_orientation_response() {
    // This tests actual adaptive production helpers, not a duplicated filter.
    // Both a deliberate step and a small quiet-region step have a separately
    // labeled bound; quiet damping is intentionally not given a fast-step SLA.
    std::size_t trials{};
    for (const auto amplitude : {.001F, .010F, .100F}) {
        for (const int axis : {0, 1, 2}) {
            for (const int interval : {8, 11, 14, 16}) {
                for (const bool coarse_clock : {false, true}) {
                    auto hand = tracked_hand();
                    mod::ControllerWeaponPoseFilterState state{};
                    check(mod::update_publication_controller_weapon_pose_filter(
                        hand, 1, 1000, &state), "position trial seeds without lag");
                    auto target = hand.grip.pose.position;
                    if (axis == 0) target.x += amplitude;
                    else if (axis == 1) target.y += amplitude;
                    else target.z += amplitude;
                    hand.grip.pose.position = target;
                    std::uint64_t generation = 1, time = 1000;
                    float previous_error = amplitude;
                    int t90 = -1;
                    for (int sample = 1; sample <= 40; ++sample) {
                        if (coarse_clock) {
                            const auto before = state;
                            check(mod::update_publication_controller_weapon_pose_filter(
                                hand, ++generation, time, &state),
                                "fresh generation may share a coarse clock tick");
                            check(same(state.filtered_grip_position,
                                       before.filtered_grip_position) &&
                                  same(state.filtered_aim_orientation,
                                       before.filtered_aim_orientation),
                                  "zero elapsed time never double-counts response");
                        }
                        time += static_cast<std::uint64_t>(interval);
                        check(mod::update_publication_controller_weapon_pose_filter(
                            hand, ++generation, time, &state),
                            "fresh bounded position publication accepted");
                        const float error = length(delta(target, state.filtered_grip_position));
                        check(error <= previous_error + .000001F && error >= 0,
                              "position response converges without overshoot on all axes");
                        if (t90 < 0 && error <= amplitude * .1001F) t90 = sample * interval;
                        previous_error = error;
                        const auto once = state;
                        for (int eye_or_consumer = 0; eye_or_consumer < 4; ++eye_or_consumer) {
                            check(mod::update_publication_controller_weapon_pose_filter(
                                hand, generation, time, &state) && same(once, state),
                                "repeated eye/consumer reads do not advance history");
                        }
                    }
                    const int limit = amplitude >= .100F ? 64 : 160;
                    check(t90 > 0 && t90 <= limit,
                          "measured position t90 remains inside labeled pure-filter bound");
                    ++trials;
                }
            }
        }
    }
    for (const float degrees : {.10F, 1.0F, 20.0F}) {
        for (const int axis : {0, 1, 2}) {
            for (const int interval : {8, 11, 14, 16}) {
                for (const bool negative_hemisphere : {false, true}) {
                    auto hand = tracked_hand();
                    mod::ControllerWeaponPoseFilterState state{};
                    check(mod::update_publication_controller_weapon_pose_filter(
                        hand, 1, 2000, &state), "orientation trial seeds without lag");
                    auto target = axis_rotation(axis, degrees);
                    if (negative_hemisphere) {
                        target.x = -target.x; target.y = -target.y;
                        target.z = -target.z; target.w = -target.w;
                    }
                    hand.aim.pose.orientation = target;
                    float previous_error = degrees;
                    int t90 = -1;
                    for (std::uint64_t sample = 1; sample <= 40; ++sample) {
                        check(mod::update_publication_controller_weapon_pose_filter(
                            hand, sample + 1, 2000 + sample * interval, &state),
                            "fresh bounded orientation publication accepted");
                        const float error = angle(state.filtered_aim_orientation, target);
                        check(error <= previous_error + .0001F,
                              "orientation converges on each axis without wrong-hemisphere jump");
                        if (t90 < 0 && error <= degrees * .1001F)
                            t90 = static_cast<int>(sample) * interval;
                        previous_error = error;
                    }
                    check(t90 > 0 && t90 <= (degrees >= 20.0F ? 64 : 160),
                          "measured angular t90 remains inside labeled pure-filter bound");
                    ++trials;
                }
            }
        }
    }
    std::printf("PASS: %zu three-axis position/angular quiet/deliberate trials; "
                "pure filter only, not motion-to-photon\n", trials);
}

void test_invalid_observations_preserve_history_atomically() {
    const auto original = tracked_hand();
    mod::ControllerWeaponPoseFilterState state{};
    check(mod::update_publication_controller_weapon_pose_filter(original, 10, 3000,
        &state), "negative trial has an accepted baseline");
    const auto accepted = state;
    for (int mutation = 0; mutation < 11; ++mutation) {
        auto hand = original;
        switch (mutation) {
        case 0: hand.grip.active = false; break;
        case 1: hand.grip.position_valid = false; break;
        case 2: hand.aim.active = false; break;
        case 3: hand.aim.orientation_valid = false; break;
        case 4: hand.grip.pose.position.x = std::numeric_limits<float>::quiet_NaN(); break;
        case 5: hand.aim.pose.orientation.w = std::numeric_limits<float>::infinity(); break;
        case 6: hand.aim.pose.orientation = {0, 0, 0, 0}; break;
        case 7: hand.grip.pose.position.x += .501F; break;
        case 8: hand.aim.pose.orientation = axis_rotation(0, 121); break;
        case 9: hand.aim.pose.orientation = axis_rotation(1, 121); break;
        case 10: hand.aim.pose.orientation = axis_rotation(2, 121); break;
        }
        for (std::uint64_t sample = 0; sample < 100; ++sample) {
            check(!mod::update_publication_controller_weapon_pose_filter(hand,
                sample + 11, sample * 16 + 3016, &state) && same(state, accepted),
                "invalid tracking/relocalized pose cannot poison accepted history");
        }
    }
    check(!mod::update_publication_controller_weapon_pose_filter(original, 9, 3016,
        &state) && same(state, accepted), "generation regression is transactional");
    check(!mod::update_publication_controller_weapon_pose_filter(original, 11, 2999,
        &state) && same(state, accepted), "clock regression is transactional");
    check(!mod::update_publication_controller_weapon_pose_filter(original, 10, 3016,
        &state) && same(state, accepted), "one generation cannot acquire another timestamp");
    auto relocalized = original;
    relocalized.grip.pose.position.x += 2.0F;
    mod::reset_controller_weapon_pose_filter(&state);
    check(mod::update_publication_controller_weapon_pose_filter(relocalized, 500,
        5000, &state) && same(state.filtered_grip_position,
                             relocalized.grip.pose.position),
        "only explicit owner retirement permits a new relocalized baseline");
}

void test_lost_tracking_and_readable_release_rearm() {
    for (const auto initial_mode : {mod::WeaponGripMode::RightHand,
                                    mod::WeaponGripMode::TwoHand}) {
        for (const bool readable_release : {false, true}) {
            mod::WeaponGripState state{};
            mod::WeaponGripInput input{};
            input.enabled = input.focused = true;
            input.weapon_identity = 0x123456;
            input.now_milliseconds = 1000;
            input.right_squeeze_active = input.left_squeeze_active = true;
            input.right_squeeze = .9F;
            input.left_squeeze = initial_mode == mod::WeaponGripMode::TwoHand ? .9F : 0;
            input.right_pose_usable = input.left_pose_usable = true;
            check(mod::update_weapon_grip(input, &state).mode == initial_mode,
                  "fixture acquires the real production grip mode");
            input.right_pose_usable = input.left_pose_usable = false;
            if (readable_release) input.right_squeeze = input.left_squeeze = 0;
            input.now_milliseconds = 1020;
            const auto lost = mod::update_weapon_grip(input, &state);
            if (readable_release) {
                check(lost.mode == mod::WeaponGripMode::Chest &&
                    !state.right_rearm_required && !state.left_rearm_required,
                    "readable physical release wins immediately despite missing pose");
            } else {
                check(lost.mode == initial_mode, "brief tracking loss retains ownership");
                input.now_milliseconds = 1250;
                check(mod::update_weapon_grip(input, &state).mode == initial_mode,
                      "exact grace boundary remains retained");
                input.now_milliseconds = 1251;
                check(mod::update_weapon_grip(input, &state).mode == mod::WeaponGripMode::Chest &&
                    state.right_rearm_required &&
                    (initial_mode != mod::WeaponGripMode::TwoHand || state.left_rearm_required),
                    "expired loss retires ownership and requires real release");
                input.right_pose_usable = input.left_pose_usable = true;
                input.now_milliseconds = 1267;
                check(mod::update_weapon_grip(input, &state).mode == mod::WeaponGripMode::Chest,
                      "held relocalized controller cannot snap-acquire the weapon");
                input.right_squeeze = input.left_squeeze = 0;
                input.now_milliseconds = 1283;
                check(mod::update_weapon_grip(input, &state).mode == mod::WeaponGripMode::Chest &&
                    !state.right_rearm_required && !state.left_rearm_required,
                    "readable release clears retirement interlock without pickup");
            }
            input.right_pose_usable = input.left_pose_usable = true;
            input.right_squeeze = .9F;
            input.left_squeeze = initial_mode == mod::WeaponGripMode::TwoHand ? .9F : 0;
            input.now_milliseconds = 1300;
            check(mod::update_weapon_grip(input, &state).mode == initial_mode,
                  "fresh squeeze reacquires only after the required release");
        }
    }
    std::uint64_t failure_since{};
    check(mod::retain_weapon_pose_during_tracking_failure(2000, &failure_since),
          "renderer retention begins from the last complete pose");
    check(mod::retain_weapon_pose_during_tracking_failure(2250, &failure_since),
          "renderer retention includes its exact 250ms boundary");
    check(!mod::retain_weapon_pose_during_tracking_failure(2251, &failure_since) &&
          !mod::retain_weapon_pose_during_tracking_failure(1999, &failure_since),
          "expired or regressed renderer time cannot extend a frozen pose");
    mod::reset_weapon_pose_tracking_failure(&failure_since);
    check(failure_since == 0, "successful fresh placement explicitly retires failure timer");
}
}  // namespace

int main() {
    test_three_axis_position_and_orientation_response();
    test_invalid_observations_preserve_history_atomically();
    test_lost_tracking_and_readable_release_rearm();
    std::printf("PASS: %llu handling continuity checks; accepted filters/offsets "
        "unchanged; no native-render or headset acceptance\n",
        static_cast<unsigned long long>(checks));
}
