#include "usercmd.hpp"

#include <bit>
#include <cmath>

namespace wawvr::t4 {

std::optional<std::int16_t> encode_short_angle_degrees(const float degrees) noexcept {
    if (!std::isfinite(degrees)) {
        return std::nullopt;
    }

    constexpr float kShortUnitsPerDegree = 65536.0F / 360.0F;
    const float wrapped_degrees = std::fmod(degrees, 360.0F);
    const auto scaled =
        static_cast<std::int32_t>(wrapped_degrees * kShortUnitsPerDegree);
    const auto bits = static_cast<std::uint16_t>(scaled);
    return std::bit_cast<std::int16_t>(bits);
}

template <typename Command>
bool apply_vr_weapon_aim_impl(Command& command, const float pitch_degrees,
                              const float yaw_degrees,
                              const bool trigger_held) noexcept {
    const auto pitch = encode_short_angle_degrees(pitch_degrees);
    const auto yaw = encode_short_angle_degrees(yaw_degrees);
    if (!pitch || !yaw) {
        return false;
    }

    command.gun_pitch_short = *pitch;
    command.gun_yaw_short = *yaw;
    if (trigger_held) {
        add_button(command, UsercmdButton::attack);
    }
    return true;
}

bool apply_vr_weapon_aim(UsercmdSp& command, const float pitch_degrees,
                         const float yaw_degrees,
                         const bool trigger_held) noexcept {
    return apply_vr_weapon_aim_impl(
        command, pitch_degrees, yaw_degrees, trigger_held);
}

bool apply_vr_weapon_aim(UsercmdMp& command, const float pitch_degrees,
                         const float yaw_degrees,
                         const bool trigger_held) noexcept {
    return apply_vr_weapon_aim_impl(
        command, pitch_degrees, yaw_degrees, trigger_held);
}

}  // namespace wawvr::t4
