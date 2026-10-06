// SPDX-License-Identifier: GPL-3.0-only
#include "pistol_hand_skinning.hpp"

#include <cmath>

namespace wawvr::mod {
namespace {

using wawvr::xr::Basis3f;
using wawvr::xr::EnginePose;
using wawvr::xr::Vec3f;

[[nodiscard]] bool finite_vector(const Vec3f& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] float dot(const Vec3f& a, const Vec3f& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] Vec3f cross(const Vec3f& a, const Vec3f& b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

[[nodiscard]] bool valid_basis(const Basis3f& value) noexcept {
    constexpr float tolerance = 0.02F;
    return finite_vector(value.forward) && finite_vector(value.left) &&
           finite_vector(value.up) &&
           std::fabs(dot(value.forward, value.forward) - 1.0F) <= tolerance &&
           std::fabs(dot(value.left, value.left) - 1.0F) <= tolerance &&
           std::fabs(dot(value.up, value.up) - 1.0F) <= tolerance &&
           std::fabs(dot(value.forward, value.left)) <= tolerance &&
           std::fabs(dot(value.forward, value.up)) <= tolerance &&
           std::fabs(dot(value.left, value.up)) <= tolerance &&
           dot(cross(value.forward, value.left), value.up) > 0.98F;
}

[[nodiscard]] bool valid_pose(const EnginePose& value) noexcept {
    return finite_vector(value.position) && valid_basis(value.axis);
}

[[nodiscard]] Vec3f compose(const Basis3f& basis, const Vec3f& local) noexcept {
    return {
        basis.forward.x * local.x + basis.left.x * local.y + basis.up.x * local.z,
        basis.forward.y * local.x + basis.left.y * local.y + basis.up.y * local.z,
        basis.forward.z * local.x + basis.left.z * local.y + basis.up.z * local.z,
    };
}

[[nodiscard]] Vec3f inverse_rotate(
    const Basis3f& basis, const Vec3f& world) noexcept {
    return {dot(basis.forward, world), dot(basis.left, world),
            dot(basis.up, world)};
}

[[nodiscard]] Vec3f subtract(const Vec3f& a, const Vec3f& b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] Vec3f add(const Vec3f& a, const Vec3f& b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] bool normalize(Vec3f* const value) noexcept {
    const float squared_length = dot(*value, *value);
    if (!finite_vector(*value) || !std::isfinite(squared_length) ||
        squared_length <= 1.0e-12F) {
        return false;
    }
    const float reciprocal = 1.0F / std::sqrt(squared_length);
    value->x *= reciprocal;
    value->y *= reciprocal;
    value->z *= reciprocal;
    return finite_vector(*value);
}

[[nodiscard]] bool accumulate(
    Vec3f* const accumulated, const Vec3f& value, const float weight) noexcept {
    if (!finite_vector(value)) {
        return false;
    }
    accumulated->x += value.x * weight;
    accumulated->y += value.y * weight;
    accumulated->z += value.z * weight;
    return finite_vector(*accumulated);
}

}  // namespace

bool decode_pistol_hand_skin_influences(
    const std::span<const std::uint16_t> blend_words,
    const std::size_t influence_count,
    const std::size_t bone_count,
    std::array<HandSkinInfluence, 4>* const output) noexcept {
    if (output == nullptr || influence_count == 0 || influence_count > 4 ||
        bone_count == 0 || bone_count > 128 ||
        blend_words.size() < 1 + 2 * (influence_count - 1)) {
        return false;
    }
    std::array<HandSkinInfluence, 4> candidate{};
    std::uint32_t secondary_weight = 0;
    for (std::size_t index = 0; index < influence_count; ++index) {
        const std::size_t offset_word = index == 0 ? 0 : 2 * index - 1;
        const std::uint16_t matrix_offset = blend_words[offset_word];
        const std::uint16_t bone = matrix_offset >> 6;
        if ((matrix_offset & 63U) != 0 || bone >= bone_count) {
            return false;
        }
        candidate[index].bone = bone;
        if (index != 0) {
            candidate[index].weight = blend_words[offset_word + 1];
            secondary_weight += candidate[index].weight;
            if (secondary_weight > 65535U) {
                return false;
            }
        }
    }
    candidate[0].weight = static_cast<std::uint16_t>(65535U - secondary_weight);
    *output = candidate;
    return true;
}

bool skin_pistol_hand_vertex_to_root(
    const Vec3f bind_position,
    Vec3f normal,
    Vec3f tangent,
    const std::span<const HandSkinInfluence> influences,
    const std::span<const HandSkinBoneTransform> bones,
    const EnginePose root_world,
    HandSkinnedVertex* const output) noexcept {
    if (output == nullptr || influences.empty() || influences.size() > 4 ||
        bones.empty() || bones.size() > 128 || !finite_vector(bind_position) ||
        !valid_pose(root_world) || !normalize(&normal) || !normalize(&tangent)) {
        return false;
    }

    std::uint32_t weight_sum = 0;
    HandSkinnedVertex world{};
    for (const HandSkinInfluence influence : influences) {
        weight_sum += influence.weight;
        if (weight_sum > 65535U || influence.bone >= bones.size()) {
            return false;
        }
        // Decoded records clear unused array entries to {0, 0}; bone zero
        // need not belong to this extracted hand or have an animated pose.
        if (influence.weight == 0) {
            continue;
        }
        const HandSkinBoneTransform& bone = bones[influence.bone];
        if (!bone.valid || !valid_pose(bone.bind_pose) ||
            !valid_pose(bone.animated_world_pose)) {
            return false;
        }
        const Vec3f local_position = inverse_rotate(
            bone.bind_pose.axis, subtract(bind_position, bone.bind_pose.position));
        const Vec3f animated_position = add(
            compose(bone.animated_world_pose.axis, local_position),
            bone.animated_world_pose.position);
        const Vec3f animated_normal = compose(bone.animated_world_pose.axis,
            inverse_rotate(bone.bind_pose.axis, normal));
        const Vec3f animated_tangent = compose(bone.animated_world_pose.axis,
            inverse_rotate(bone.bind_pose.axis, tangent));
        const float weight = static_cast<float>(influence.weight) / 65535.0F;
        if (!finite_vector(local_position) ||
            !accumulate(&world.position, animated_position, weight) ||
            !accumulate(&world.normal, animated_normal, weight) ||
            !accumulate(&world.tangent, animated_tangent, weight)) {
            return false;
        }
    }
    if (weight_sum != 65535U) {
        return false;
    }
    HandSkinnedVertex candidate{
        inverse_rotate(root_world.axis, subtract(world.position, root_world.position)),
        inverse_rotate(root_world.axis, world.normal),
        inverse_rotate(root_world.axis, world.tangent),
    };
    if (!finite_vector(candidate.position) || !normalize(&candidate.normal) ||
        !normalize(&candidate.tangent)) {
        return false;
    }
    *output = candidate;
    return true;
}

}  // namespace wawvr::mod
