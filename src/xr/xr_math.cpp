// SPDX-License-Identifier: GPL-3.0-only
#include "xr_math.h"

#include <cmath>

namespace wawvr::xr
{

namespace
{

Vec3f Subtract(const Vec3f& left, const Vec3f& right)
{
    return {
        left.x - right.x,
        left.y - right.y,
        left.z - right.z,
    };
}

Vec3f Scale(const Vec3f& value, const float scalar)
{
    return {
        value.x * scalar,
        value.y * scalar,
        value.z * scalar,
    };
}

Vec3f Cross(const Vec3f& left, const Vec3f& right)
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

} // namespace

Quaternionf Normalize(const Quaternionf& value)
{
    const float length_squared =
        value.x * value.x + value.y * value.y + value.z * value.z +
        value.w * value.w;

    if (length_squared <= 1.0e-12f)
    {
        return {};
    }

    const float inverse_length = 1.0f / std::sqrt(length_squared);
    return {
        value.x * inverse_length,
        value.y * inverse_length,
        value.z * inverse_length,
        value.w * inverse_length,
    };
}

Quaternionf Conjugate(const Quaternionf& value)
{
    return {-value.x, -value.y, -value.z, value.w};
}

Quaternionf Multiply(const Quaternionf& left, const Quaternionf& right)
{
    return {
        left.w * right.x + left.x * right.w + left.y * right.z -
            left.z * right.y,
        left.w * right.y - left.x * right.z + left.y * right.w +
            left.z * right.x,
        left.w * right.z + left.x * right.y - left.y * right.x +
            left.z * right.w,
        left.w * right.w - left.x * right.x - left.y * right.y -
            left.z * right.z,
    };
}

Vec3f Rotate(const Quaternionf& orientation, const Vec3f& vector)
{
    const Quaternionf normalized = Normalize(orientation);
    const Vec3f quaternion_vector = {
        normalized.x,
        normalized.y,
        normalized.z,
    };
    const Vec3f twice_cross = Scale(Cross(quaternion_vector, vector), 2.0f);
    const Vec3f second_cross = Cross(quaternion_vector, twice_cross);

    return {
        vector.x + normalized.w * twice_cross.x + second_cross.x,
        vector.y + normalized.w * twice_cross.y + second_cross.y,
        vector.z + normalized.w * twice_cross.z + second_cross.z,
    };
}

Vec3f OpenXrVectorToIw(const Vec3f& vector)
{
    return {-vector.z, -vector.x, vector.y};
}

EnginePose OpenXrPoseToIwRelative(
    const Posef& pose,
    const Posef& reference,
    const float engine_units_per_meter)
{
    const Quaternionf inverse_reference =
        Conjugate(Normalize(reference.orientation));
    const Quaternionf relative_orientation = Normalize(
        Multiply(inverse_reference, Normalize(pose.orientation)));
    const Vec3f relative_position = Rotate(
        inverse_reference,
        Subtract(pose.position, reference.position));

    EnginePose result = {};
    result.position = Scale(
        OpenXrVectorToIw(relative_position),
        engine_units_per_meter);
    result.axis.forward = OpenXrVectorToIw(
        Rotate(relative_orientation, {0.0f, 0.0f, -1.0f}));
    result.axis.left = OpenXrVectorToIw(
        Rotate(relative_orientation, {-1.0f, 0.0f, 0.0f}));
    result.axis.up = OpenXrVectorToIw(
        Rotate(relative_orientation, {0.0f, 1.0f, 0.0f}));
    return result;
}

} // namespace wawvr::xr
