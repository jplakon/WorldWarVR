// SPDX-License-Identifier: GPL-3.0-only
#include "pistol_hand_skinning.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {

using wawvr::mod::HandSkinBoneTransform;
using wawvr::mod::HandSkinInfluence;
using wawvr::mod::HandSkinnedVertex;
using wawvr::mod::decode_pistol_hand_skin_influences;
using wawvr::mod::skin_pistol_hand_vertex_to_root;
using wawvr::xr::Basis3f;
using wawvr::xr::EnginePose;
using wawvr::xr::Vec3f;

int failures = 0;

void check(const bool condition, const char* const message) {
    if (!condition) {
        std::fprintf(stderr, "pistol hand skinning test failed: %s\n", message);
        ++failures;
    }
}

bool near(const float a, const float b) {
    return std::fabs(a - b) < 0.0001F;
}

bool near(const Vec3f& a, const Vec3f& b) {
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

bool near(const HandSkinnedVertex& a, const HandSkinnedVertex& b) {
    return near(a.position, b.position) && near(a.normal, b.normal) &&
           near(a.tangent, b.tangent);
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

EnginePose transformed(const EnginePose& pose, const EnginePose& outer) {
    const Vec3f rotated = compose(outer.axis, pose.position);
    return {{rotated.x + outer.position.x, rotated.y + outer.position.y,
             rotated.z + outer.position.z}, compose_axes(outer.axis, pose.axis)};
}

bool same_influences(const std::array<HandSkinInfluence, 4>& a,
                     const std::array<HandSkinInfluence, 4>& b) {
    for (std::size_t index = 0; index < a.size(); ++index) {
        if (a[index].bone != b[index].bone || a[index].weight != b[index].weight) {
            return false;
        }
    }
    return true;
}

void test_exact_blend_decoding() {
    std::array<HandSkinInfluence, 4> output{};
    const std::array<std::uint16_t, 7> words{64, 128, 12345, 192, 23456, 256, 9999};
    for (std::size_t count = 1; count <= 4; ++count) {
        output.fill({99, 123});
        check(decode_pistol_hand_skin_influences(words, count, 5, &output),
              "one through four packed influences decode");
        std::uint32_t sum = 0;
        for (std::size_t index = 0; index < count; ++index) {
            sum += output[index].weight;
            check(output[index].bone == index + 1,
                  "64-byte matrix offsets become exact bone indexes");
            if (index != 0) {
                check(output[index].weight == words[index * 2],
                      "secondary fixed-point weights are unchanged");
            }
        }
        check(sum == 65535U, "primary remainder restores exact fixed-point unity");
        for (std::size_t index = count; index < output.size(); ++index) {
            check(output[index].bone == 0 && output[index].weight == 0,
                  "unused influences are cleared");
        }
    }
    const std::array<std::uint16_t, 3> zero_primary{0, 8128, 65535};
    check(decode_pistol_hand_skin_influences(zero_primary, 2, 128, &output) &&
              output[0].weight == 0 && output[1].bone == 127 &&
              output[1].weight == 65535,
          "zero primary remainder and highest supported bone are valid");
}

void test_malformed_blend_preserves_output() {
    const std::array<HandSkinInfluence, 4> sentinel{{{11, 12}, {13, 14}, {15, 16}, {17, 18}}};
    for (int index = 0; index < 11; ++index) {
        std::array<std::uint16_t, 7> words{0, 64, 15000, 128, 16000, 192, 17000};
        std::size_t count = 4, bones = 4, length = words.size();
        switch (index) {
        case 0: count = 0; break;
        case 1: count = 5; break;
        case 2: bones = 0; break;
        case 3: bones = 129; break;
        case 4: length = 6; break;
        case 5: words[0] = 1; break;
        case 6: words[3] = 129; break;
        case 7: words[0] = 256; break;
        case 8: words[5] = 256; break;
        case 9: words[2] = 65535; break;
        case 10: length = 0; count = 1; break;
        }
        auto output = sentinel;
        check(!decode_pistol_hand_skin_influences(
                  {words.data(), length}, count, bones, &output),
              "malformed blend record fails closed");
        check(same_influences(output, sentinel),
              "rejected record cannot partially overwrite influences");
    }
    const std::array<std::uint16_t, 1> words{0};
    check(!decode_pistol_hand_skin_influences(words, 1, 1, nullptr),
          "decoder rejects null output");
}

void test_identity_and_bind_inverse() {
    const std::array<HandSkinInfluence, 1> influences{{{0, 65535}}};
    std::array<HandSkinBoneTransform, 1> bones{{{true, {}, {}}}};
    HandSkinnedVertex output{};
    const Vec3f position{2, -3, 4};
    check(skin_pistol_hand_vertex_to_root(
              position, {0, 0, 2}, {3, 0, 0}, influences, bones, {}, &output),
          "identity bone preserves a vertex and normalizes its directions");
    check(near(output, {position, {0, 0, 1}, {1, 0, 0}}),
          "identity skinning is exact in root space");
    const EnginePose bind{{7, -9, 3}, rotation(0.3F, -0.6F, 0.9F)};
    const EnginePose world{{19, 23, -11}, rotation(-0.2F, 0.8F, 0.5F)};
    bones[0].bind_pose = bind;
    bones[0].animated_world_pose = transformed(bind, world);
    check(skin_pistol_hand_vertex_to_root(
              position, {0, 0, 1}, {1, 0, 0}, influences, bones, world, &output) &&
              near(output, {position, {0, 0, 1}, {1, 0, 0}}),
          "nontrivial inverse bind and inverse root cancel their rigid transforms");
}

void test_weighted_finger_bend() {
    const Basis3f quarter_turn{{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}};
    const std::array<HandSkinInfluence, 2> influences{{{0, 21845}, {1, 43690}}};
    const std::array<HandSkinBoneTransform, 2> bones{{
        {true, {}, {}},
        {true, {{1, 0, 0}, {}}, {{1, 0, 0}, quarter_turn}},
    }};
    HandSkinnedVertex output{};
    check(skin_pistol_hand_vertex_to_root(
              {2, 0, 0}, {0, 0, 1}, {1, 0, 0}, influences, bones, {}, &output),
          "two-bone fingertip bends through its authored knuckle pivot");
    check(near(output.position, {4.0F / 3.0F, 2.0F / 3.0F, 0}),
          "one-third straight plus two-thirds bent computes expected fingertip");
    const float inverse_sqrt5 = 1.0F / std::sqrt(5.0F);
    check(near(output.normal, {0, 0, 1}) &&
              near(output.tangent, {inverse_sqrt5, 2 * inverse_sqrt5, 0}),
          "normal and tangent rotate without translation and normalize after blending");
}

void test_unused_zero_weight_entries() {
    const std::array<HandSkinInfluence, 4> influences{{{1, 65535}, {0, 0}, {0, 0}, {0, 0}}};
    std::array<HandSkinBoneTransform, 128> bones{};
    bones[1].valid = true;
    HandSkinnedVertex output{};
    check(skin_pistol_hand_vertex_to_root(
              {2, 3, 4}, {0, 0, 1}, {1, 0, 0}, influences, bones, {}, &output) &&
              near(output, {{2, 3, 4}, {0, 0, 1}, {1, 0, 0}}),
          "unused zero-weight entries do not require a cached bone-zero pose");
}

void test_rigid_world_invariance() {
    const std::array<HandSkinInfluence, 3> influences{{{0, 20000}, {1, 30000}, {2, 15535}}};
    const std::array<HandSkinBoneTransform, 3> bones{{
        {true, {{1, 2, 3}, rotation(0.1F, 0.2F, 0.3F)},
         {{4, 5, 6}, rotation(0.2F, -0.3F, 0.4F)}},
        {true, {{3, 2, 1}, rotation(-0.3F, 0.6F, 0.2F)},
         {{7, -3, 8}, rotation(-0.5F, 0.1F, -0.2F)}},
        {true, {{2, 4, 6}, rotation(0.5F, 0.3F, -0.4F)},
         {{5, 9, -2}, rotation(0.4F, 0.7F, 0.1F)}},
    }};
    const EnginePose root{{2, -3, 4}, rotation(-0.2F, 0.4F, 0.8F)};
    HandSkinnedVertex baseline{};
    check(skin_pistol_hand_vertex_to_root(
              {4, 3, 2}, {0, 0, 1}, {1, 0, 0}, influences, bones, root, &baseline),
          "nontrivial three-bone skinning baseline succeeds");
    for (int index = 0; index < 12; ++index) {
        const float step = static_cast<float>(index);
        const EnginePose world{{11 + step, -20 + step, 31 - step},
            rotation(0.19F * step, -0.31F * step, 0.41F * step)};
        auto moved_bones = bones;
        for (auto& bone : moved_bones) {
            bone.animated_world_pose = transformed(bone.animated_world_pose, world);
        }
        HandSkinnedVertex output{};
        check(skin_pistol_hand_vertex_to_root(
                  {4, 3, 2}, {0, 0, 1}, {1, 0, 0}, influences, moved_bones,
                  transformed(root, world), &output) && near(output, baseline),
              "arbitrary rigid world motion cannot change the root-local baked mesh");
    }
}

void test_invalid_skin_preserves_output() {
    const HandSkinnedVertex sentinel{{19, 23, 31}, {0, 1, 0}, {0, 0, 1}};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (int index = 0; index < 19; ++index) {
        std::array<HandSkinInfluence, 5> influences{{{0, 65535}}};
        std::array<HandSkinBoneTransform, 129> bones{};
        bones[0].valid = true;
        std::size_t influence_count = 1, bone_count = 1;
        Vec3f position{1, 2, 3}, normal{0, 0, 1}, tangent{1, 0, 0};
        EnginePose root{};
        switch (index) {
        case 0: influence_count = 0; break;
        case 1: influence_count = 5; break;
        case 2: bone_count = 0; break;
        case 3: bone_count = 129; break;
        case 4: influences[0].bone = 1; break;
        case 5: influences[0].weight = 65534; break;
        case 6: bones[0].valid = false; break;
        case 7: bones[0].bind_pose.axis.up = {0, 0, -1}; break;
        case 8: bones[0].animated_world_pose.axis.forward = {2, 0, 0}; break;
        case 9: bones[0].animated_world_pose.position.x = infinity; break;
        case 10: root.position.z = nan; break;
        case 11: root.axis.left = root.axis.forward; break;
        case 12: position.y = infinity; break;
        case 13: normal = {}; break;
        case 14: tangent = {}; break;
        case 15: tangent.x = nan; break;
        case 16: bones[0].bind_pose.position.y = nan; break;
        case 17: normal.z = infinity; break;
        case 18: influences[1] = {0, 1}; influence_count = 2; break;
        }
        HandSkinnedVertex output = sentinel;
        check(!skin_pistol_hand_vertex_to_root(
                  position, normal, tangent, {influences.data(), influence_count},
                  {bones.data(), bone_count}, root, &output),
              "invalid skin input is rejected");
        check(near(output, sentinel), "skin failure leaves the entire output unchanged");
    }
    const std::array<HandSkinInfluence, 1> influences{{{0, 65535}}};
    std::array<HandSkinBoneTransform, 1> bones{{{true, {}, {}}}};
    check(!skin_pistol_hand_vertex_to_root(
              {}, {0, 0, 1}, {1, 0, 0}, influences, bones, {}, nullptr),
          "skinner rejects null output");
    const float maximum = std::numeric_limits<float>::max();
    bones[0].bind_pose.position.x = -maximum;
    HandSkinnedVertex output = sentinel;
    check(!skin_pistol_hand_vertex_to_root(
              {maximum, 0, 0}, {0, 0, 1}, {1, 0, 0}, influences, bones, {}, &output) &&
              near(output, sentinel),
          "finite coordinates that overflow during inverse bind fail closed");

    // Equal opposing directions cancel despite valid individual transforms.
    // Three exact fixed-point weights permit exact cancellation in x and y.
    const float c = -0.5F, s = std::sqrt(3.0F) * 0.5F;
    const std::array<HandSkinBoneTransform, 3> cancellation_bones{{
        {true, {}, {}},
        {true, {}, {{}, {{c, s, 0}, {-s, c, 0}, {0, 0, 1}}}},
        {true, {}, {{}, {{c, -s, 0}, {s, c, 0}, {0, 0, 1}}}},
    }};
    const std::array<HandSkinInfluence, 3> cancellation_influences{{
        {0, 21845}, {1, 21845}, {2, 21845}}};
    check(!skin_pistol_hand_vertex_to_root(
              {}, {1, 0, 0}, {0, 0, 1}, cancellation_influences,
              cancellation_bones, {}, &output) && near(output, sentinel),
          "blended zero-length normal cannot publish a malformed vertex");
}

}  // namespace

int main() {
    test_exact_blend_decoding();
    test_malformed_blend_preserves_output();
    test_identity_and_bind_inverse();
    test_weighted_finger_bend();
    test_unused_zero_weight_entries();
    test_rigid_world_invariance();
    test_invalid_skin_preserves_output();
    return failures == 0 ? 0 : 1;
}
