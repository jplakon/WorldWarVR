// SPDX-License-Identifier: GPL-3.0-only
#include "pistol_support_pose.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string_view>

namespace {

using wawvr::xr::Basis3f;
using wawvr::xr::EnginePose;
using wawvr::xr::Vec3f;
using wawvr::mod::calculate_pistol_support_hand_pose;

int failures = 0;

void check(const bool condition, const char* const message) {
    if (!condition) {
        std::fprintf(stderr, "pistol support pose test failed: %s\n", message);
        ++failures;
    }
}

bool near(const float a, const float b) {
    return std::fabs(a - b) < 0.0001F;
}

bool near(const Vec3f& a, const Vec3f& b) {
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

bool near(const Basis3f& a, const Basis3f& b) {
    return near(a.forward, b.forward) && near(a.left, b.left) &&
           near(a.up, b.up);
}

bool near(const EnginePose& a, const EnginePose& b) {
    return near(a.position, b.position) && near(a.axis, b.axis);
}

Vec3f compose(const Basis3f& basis, const Vec3f& local) {
    return {
        basis.forward.x * local.x + basis.left.x * local.y + basis.up.x * local.z,
        basis.forward.y * local.x + basis.left.y * local.y + basis.up.y * local.z,
        basis.forward.z * local.x + basis.left.z * local.y + basis.up.z * local.z,
    };
}

Basis3f compose_axes(const Basis3f& outer, const Basis3f& inner) {
    return {compose(outer, inner.forward), compose(outer, inner.left),
            compose(outer, inner.up)};
}

Basis3f rotation(const float pitch, const float yaw, const float roll) {
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    const float cr = std::cos(roll), sr = std::sin(roll);
    const Basis3f x{{1, 0, 0}, {0, cr, sr}, {0, -sr, cr}};
    const Basis3f y{{cp, 0, -sp}, {0, 1, 0}, {sp, 0, cp}};
    const Basis3f z{{cy, sy, 0}, {-sy, cy, 0}, {0, 0, 1}};
    return compose_axes(z, compose_axes(y, x));
}

void test_exact_pistol_identities() {
    using wawvr::mod::pistol_support_pose_for_weapon_name;
    using namespace std::string_view_literals;
    constexpr std::array accepted{
        "colt"sv, "colt_wet"sv, "walther"sv, "tokarev"sv, "nambu"sv,
        "zombie_colt"sv, "zombie_colt_upgraded"sv, "sw_357"sv,
        "zombie_sw_357"sv, "zombie_sw_357_upgraded"sv, "colt_dirty_harry"sv,
    };
    for (const auto name : accepted) {
        check(pistol_support_pose_for_weapon_name(name),
              "each verified pistol identity uses the close support pose");
    }
    constexpr std::array rejected{
        ""sv, "COLT"sv, "colt_mp"sv, "colt_other"sv, "colt "sv,
        " colt"sv, "colt\0extra"sv, "colt\0"sv, "zombie_colt_upgraded_extra"sv,
        "sw_357_mp"sv, "357"sv, "magnum"sv, "m1carbine"sv, "kar98k"sv,
        "ptrs41"sv, "rocket_barrage"sv, "air_support"sv, "fraggrenade"sv,
    };
    for (const auto name : rejected) {
        check(!pistol_support_pose_for_weapon_name(name),
              "unknown, rifle, MP, prefix and malformed identities fail closed");
    }
}

void test_palm_spacing_and_mirrored_attachments() {
    const Basis3f grip = rotation(0.13F, -0.41F, 0.27F);
    const Basis3f right_attachment = rotation(-0.23F, 0.62F, 0.37F);
    const Basis3f left_attachment = rotation(0.44F, -1.28F, -0.53F);
    const Vec3f right_position{-0.6F, 0.7F, 1.1F};
    const Vec3f left_position{0.8F, -0.5F, 1.3F};
    const Vec3f right_palm{10.0F, 20.0F, 30.0F};
    const Vec3f wrist_delta = compose(grip, right_position);
    const EnginePose right_wrist{
        {right_palm.x + wrist_delta.x, right_palm.y + wrist_delta.y,
         right_palm.z + wrist_delta.z},
        compose_axes(grip, right_attachment),
    };
    const auto original_right = right_wrist;
    EnginePose left{};
    check(calculate_pistol_support_hand_pose(
              right_wrist, right_attachment, right_position,
              left_attachment, left_position, Basis3f{}, &left),
          "different left and right anatomical attachments produce a pose");
    const Vec3f left_delta = compose(grip, left_position);
    const Vec3f left_palm{
        left.position.x - left_delta.x,
        left.position.y - left_delta.y,
        left.position.z - left_delta.z,
    };
    const Vec3f palm_separation{
        left_palm.x - right_palm.x, left_palm.y - right_palm.y,
        left_palm.z - right_palm.z,
    };
    check(near(palm_separation,
               wawvr::mod::kPistolSupportPalmOffsetWeaponLocal),
          "support palm sits just beside the right palm in weapon space");
    const float distance = std::sqrt(
        palm_separation.x * palm_separation.x +
        palm_separation.y * palm_separation.y +
        palm_separation.z * palm_separation.z);
    check(distance > 0.5F && distance < 2.0F,
          "the palm spacing is a pistol grip, not a rifle fore-end");
    check(near(left.axis, compose_axes(grip, left_attachment)),
          "left mesh uses its own anatomical attachment, not right wrist Euler angles");
    check(near(right_wrist, original_right),
          "calculating support presentation does not modify the firing hand");
}

void test_bounded_native_name_buffer() {
    using wawvr::mod::pistol_support_pose_for_weapon_name_buffer;
    using namespace std::string_view_literals;
    constexpr std::array accepted{
        "colt"sv, "colt_wet"sv, "walther"sv, "tokarev"sv, "nambu"sv,
        "zombie_colt"sv, "zombie_colt_upgraded"sv, "sw_357"sv,
        "zombie_sw_357"sv, "zombie_sw_357_upgraded"sv, "colt_dirty_harry"sv,
    };
    for (const auto name : accepted) {
        std::array<char, 64> snapshot{};
        snapshot.fill('X');
        std::copy(name.begin(), name.end(), snapshot.begin());
        snapshot[name.size()] = '\0';
        check(snapshot.back() != '\0' &&
                  pistol_support_pose_for_weapon_name_buffer(
                      {snapshot.data(), snapshot.size()}),
              "native chunk copy classifies first-NUL name despite a nonzero tail");
        check(!pistol_support_pose_for_weapon_name_buffer(
                  {snapshot.data(), name.size()}),
              "buffer classifier never reads beyond its supplied bound for a terminator");
        check(pistol_support_pose_for_weapon_name_buffer(
                  {snapshot.data(), name.size() + 1}),
              "terminator at final bounded byte is accepted");
    }
    constexpr std::array rejected{
        ""sv, "\0colt"sv, "colt"sv, "colt_extra\0colt"sv,
        "COLT\0"sv, "colt_mp\0"sv, "kar98k\0colt"sv,
        "zombie_colt_upgraded_extra\0"sv,
    };
    for (const auto name : rejected) {
        check(!pistol_support_pose_for_weapon_name_buffer(name),
              "empty, missing-NUL and unsupported bounded identities fail closed");
    }
}

void test_rigid_world_transform_invariance() {
    const EnginePose right{{3, -7, 11}, rotation(0.2F, 0.7F, -0.4F)};
    const Basis3f right_attachment = rotation(0.3F, -0.2F, 0.6F);
    const Basis3f left_attachment = rotation(-0.7F, 0.8F, 0.1F);
    const Vec3f right_position{0.3F, -0.9F, 1.0F};
    const Vec3f left_position{-0.4F, 0.7F, 1.2F};
    const Basis3f weapon = rotation(-0.1F, 1.0F, 0.9F);
    EnginePose baseline{};
    check(calculate_pistol_support_hand_pose(
              right, right_attachment, right_position, left_attachment,
              left_position, weapon, &baseline),
          "baseline visual support pose builds");
    for (int index = 0; index < 12; ++index) {
        const float step = static_cast<float>(index);
        const Basis3f world = rotation(0.17F * step, -0.29F * step, 0.41F * step);
        const Vec3f translation{13 + step, -21 + 2 * step, 34 - step};
        Vec3f moved_right = compose(world, right.position);
        moved_right.x += translation.x;
        moved_right.y += translation.y;
        moved_right.z += translation.z;
        const EnginePose transformed_right{
            moved_right, compose_axes(world, right.axis)};
        EnginePose actual{};
        check(calculate_pistol_support_hand_pose(
                  transformed_right, right_attachment, right_position,
                  left_attachment, left_position,
                  compose_axes(world, weapon), &actual),
              "translated and rotated pistol pose builds");
        Vec3f expected_position = compose(world, baseline.position);
        expected_position.x += translation.x;
        expected_position.y += translation.y;
        expected_position.z += translation.z;
        check(near(actual, EnginePose{
                       expected_position, compose_axes(world, baseline.axis)}),
              "head/world motion rotates and translates both hands rigidly together");
    }
}

void test_invalid_input_preserves_output() {
    const EnginePose preserved{{19, 23, 31}, rotation(0.2F, 0.3F, 0.4F)};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (int index = 0; index < 10; ++index) {
        EnginePose right{};
        Basis3f right_axis{}, left_axis{}, weapon_axis{};
        Vec3f right_position{}, left_position{};
        switch (index) {
        case 0: right.position.x = nan; break;
        case 1: right.axis.forward.x = infinity; break;
        case 2: right_axis.forward = {}; break;
        case 3: right_position.y = nan; break;
        case 4: left_axis.left = left_axis.forward; break;
        case 5: left_position.z = infinity; break;
        case 6: weapon_axis.up = {0, 0, -1}; break;
        case 7: weapon_axis.forward = {2, 0, 0}; break;
        case 8: left_axis.up.z = nan; break;
        case 9: right.axis.left = right.axis.forward; break;
        }
        EnginePose output = preserved;
        check(!calculate_pistol_support_hand_pose(
                  right, right_axis, right_position, left_axis,
                  left_position, weapon_axis, &output),
              "nonfinite, scaled, reflected and degenerate transforms fail closed");
        check(near(output, preserved), "failure never partially changes the output");
    }
    check(!calculate_pistol_support_hand_pose(
              {}, {}, {}, {}, {}, {}, nullptr),
          "missing output is rejected");
    EnginePose output = preserved;
    const float maximum = std::numeric_limits<float>::max();
    check(!calculate_pistol_support_hand_pose(
              {{maximum, 0, 0}, {}}, {}, {-maximum, 0, 0}, {}, {}, {}, &output) &&
              near(output, preserved),
          "finite inputs whose result overflows cannot publish a broken hand");
}

}  // namespace

int main() {
    test_exact_pistol_identities();
    test_bounded_native_name_buffer();
    test_palm_spacing_and_mirrored_attachments();
    test_rigid_world_transform_invariance();
    test_invalid_input_preserves_output();
    return failures == 0 ? 0 : 1;
}
