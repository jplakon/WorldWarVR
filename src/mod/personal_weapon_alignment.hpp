// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "xr_math.h"

#include <charconv>
#include <cmath>
#include <string_view>

namespace wawvr::mod {

inline constexpr float kMaximumPersonalWeaponAngleDegrees = 35.0F;
inline constexpr char kWeaponPitchEnvironment[] = "WAWVR_WEAPON_PITCH_DEGREES";
inline constexpr char kWeaponYawEnvironment[] = "WAWVR_WEAPON_YAW_DEGREES";
inline constexpr char kWeaponRollEnvironment[] = "WAWVR_WEAPON_ROLL_DEGREES";

// Personal controller fit, not a sight/ballistics correction or a headset
// preset. Applied once before publication filters. The grip and physical
// left-hand/pouch positions are never changed. Positive pitch raises the
// muzzle, positive yaw moves it left; roll is around the forward aim ray.
struct PersonalWeaponAlignment final {
    float pitch_degrees{};
    float yaw_degrees{};
    float roll_degrees{};
};

[[nodiscard]] inline bool valid_personal_weapon_alignment(
    const PersonalWeaponAlignment& value) noexcept {
    const auto valid = [](const float angle) noexcept {
        return std::isfinite(angle) &&
            std::abs(angle) <= kMaximumPersonalWeaponAngleDegrees;
    };
    return valid(value.pitch_degrees) && valid(value.yaw_degrees) &&
        valid(value.roll_degrees);
}

// Parse the entire invariant-culture value. Malformed, nonfinite, and
// out-of-range values fail without changing the output; never clamp silently
// into a surprising live weapon angle.
[[nodiscard]] inline bool parse_personal_weapon_angle(
    std::string_view text, float* const angle) noexcept {
    if (angle == nullptr) return false;
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1);
    if (!text.empty() && text.front() == '+') {
        text.remove_prefix(1);
        if (!text.empty() && (text.front() == '+' || text.front() == '-'))
            return false;
    }
    if (text.empty()) return false;
    float parsed{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(),
        parsed, std::chars_format::general);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        !std::isfinite(parsed) ||
        std::abs(parsed) > kMaximumPersonalWeaponAngleDegrees) return false;
    *angle = parsed;
    return true;
}

// Raw and filtered weapon consumers receive this same calibrated aim sample.
// Never apply another offset at rendering or native firing: their existing
// exact per-weapon sight calibration must remain shared and unchanged.
[[nodiscard]] inline bool apply_personal_weapon_alignment(
    const PersonalWeaponAlignment& alignment,
    wawvr::xr::FrameState* const frame) noexcept {
    if (frame == nullptr || !valid_personal_weapon_alignment(alignment))
        return false;
    // Preserve every byte of the accepted default, even a non-unit runtime
    // quaternion. Existing consumers retain ownership of normal validation.
    if (alignment.pitch_degrees == 0.0F && alignment.yaw_degrees == 0.0F &&
        alignment.roll_degrees == 0.0F) return true;
    auto& aim = frame->actions.hands[
        static_cast<unsigned>(wawvr::xr::Hand::Right)].aim;
    const auto& original = aim.pose.orientation;
    if (!aim.active || !aim.orientation_valid ||
        !std::isfinite(original.x) || !std::isfinite(original.y) ||
        !std::isfinite(original.z) || !std::isfinite(original.w)) return false;
    const float length_squared = original.x * original.x +
        original.y * original.y + original.z * original.z +
        original.w * original.w;
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-8F)
        return false;
    constexpr float kHalfRadiansPerDegree =
        3.14159265358979323846F / 360.0F;
    const float pitch = alignment.pitch_degrees * kHalfRadiansPerDegree;
    const float yaw = alignment.yaw_degrees * kHalfRadiansPerDegree;
    const float roll = alignment.roll_degrees * kHalfRadiansPerDegree;
    const wawvr::xr::Quaternionf pitch_rotation{
        std::sin(pitch), 0.0F, 0.0F, std::cos(pitch)};
    const wawvr::xr::Quaternionf yaw_rotation{
        0.0F, std::sin(yaw), 0.0F, std::cos(yaw)};
    const wawvr::xr::Quaternionf roll_rotation{
        0.0F, 0.0F, -std::sin(roll), std::cos(roll)};
    const auto local = wawvr::xr::Multiply(yaw_rotation,
        wawvr::xr::Multiply(pitch_rotation, roll_rotation));
    const auto calibrated = wawvr::xr::Normalize(wawvr::xr::Multiply(
        wawvr::xr::Normalize(original), local));
    if (!std::isfinite(calibrated.x) || !std::isfinite(calibrated.y) ||
        !std::isfinite(calibrated.z) || !std::isfinite(calibrated.w))
        return false;
    aim.pose.orientation = calibrated;
    return true;
}

} // namespace wawvr::mod
