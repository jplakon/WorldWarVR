// SPDX-License-Identifier: GPL-3.0-only
#include "aircraft_control_logic.hpp"

#include <algorithm>
#include <cmath>

namespace wawvr::mod {
namespace {

constexpr float kRadiansPerDegree = 0.01745329251994329577F;
constexpr float kDegreesPerRadian = 57.295779513082320876F;

bool finite(const wawvr::xr::Vec3f& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

bool finite(const wawvr::xr::Vec2f& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

float dot(const wawvr::xr::Vec3f& a, const wawvr::xr::Vec3f& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

wawvr::xr::Vec3f cross(
    const wawvr::xr::Vec3f& a, const wawvr::xr::Vec3f& b) noexcept {
    return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z,
            a.x*b.y - a.y*b.x};
}

bool valid_basis(const wawvr::xr::Basis3f& basis) noexcept {
    if (!finite(basis.forward) || !finite(basis.left) || !finite(basis.up))
        return false;
    constexpr float tolerance = 0.002F;
    return std::abs(dot(basis.forward, basis.forward) - 1.0F) <= tolerance &&
        std::abs(dot(basis.left, basis.left) - 1.0F) <= tolerance &&
        std::abs(dot(basis.up, basis.up) - 1.0F) <= tolerance &&
        std::abs(dot(basis.forward, basis.left)) <= tolerance &&
        std::abs(dot(basis.forward, basis.up)) <= tolerance &&
        std::abs(dot(basis.left, basis.up)) <= tolerance &&
        dot(cross(basis.forward, basis.left), basis.up) >= 1.0F - tolerance;
}

bool normalize(const wawvr::xr::Vec3f& value,
               wawvr::xr::Vec3f* output) noexcept {
    if (output == nullptr || !finite(value)) return false;
    const float length_squared = dot(value, value);
    if (!std::isfinite(length_squared) || length_squared < 1.0e-8F)
        return false;
    const float inverse_length = 1.0F / std::sqrt(length_squared);
    *output = {value.x*inverse_length, value.y*inverse_length,
               value.z*inverse_length};
    return true;
}

wawvr::xr::Vec3f compose(const wawvr::xr::Basis3f& basis,
                        const wawvr::xr::Vec3f& local) noexcept {
    return {basis.forward.x*local.x + basis.left.x*local.y + basis.up.x*local.z,
            basis.forward.y*local.x + basis.left.y*local.y + basis.up.y*local.z,
            basis.forward.z*local.x + basis.left.z*local.y + basis.up.z*local.z};
}

}  // namespace

bool aircraft_controller_context_valid(
    const AircraftContextData& context) noexcept {
    return aircraft_passenger_context_present(context) &&
        (context.player_flags & 0x10000U) == 0;
}

bool aircraft_passenger_context_present(
    const AircraftContextData& context) noexcept {
    return context.active_connection && context.local_client_number == 0 &&
        (context.player_flags & 0x4000U) != 0 &&
        context.vehicle_entity > 0 && context.vehicle_entity < 1023 &&
        context.entity_type == 13 && context.vehicle_type == 2 &&
        context.seat >= 1 && context.seat <= 4 &&
        context.passenger_entity == 0;
}

bool aircraft_basis_from_angles(const wawvr::xr::Vec3f& angles,
                               wawvr::xr::Basis3f* const basis) noexcept {
    if (basis == nullptr || !finite(angles)) return false;
    // Bound even finite input before trigonometry, avoiding overflow in the
    // degrees-to-radians product and keeping periodic angle representations.
    const float p = std::remainder(angles.x, 360.0F) * kRadiansPerDegree;
    const float y = std::remainder(angles.y, 360.0F) * kRadiansPerDegree;
    const float r = std::remainder(angles.z, 360.0F) * kRadiansPerDegree;
    const float sp = std::sin(p), cp = std::cos(p);
    const float sy = std::sin(y), cy = std::cos(y);
    const float sr = std::sin(r), cr = std::cos(r);
    const wawvr::xr::Basis3f candidate{
        {cp*cy, cp*sy, -sp},
        {sr*sp*cy - cr*sy, sr*sp*sy + cr*cy, sr*cp},
        {cr*sp*cy + sr*sy, cr*sp*sy - sr*cy, cr*cp}};
    if (!valid_basis(candidate)) return false;
    *basis = candidate;
    return true;
}

bool compose_aircraft_seat_basis(const wawvr::xr::Basis3f& hull,
                                const wawvr::xr::Vec2f& rest_pitch_yaw,
                                wawvr::xr::Basis3f* const basis) noexcept {
    if (basis == nullptr || !valid_basis(hull) || !finite(rest_pitch_yaw))
        return false;
    wawvr::xr::Basis3f rest{};
    if (!aircraft_basis_from_angles(
            {rest_pitch_yaw.x, rest_pitch_yaw.y, 0.0F}, &rest)) return false;
    const wawvr::xr::Basis3f candidate{
        compose(hull, rest.forward), compose(hull, rest.left),
        compose(hull, rest.up)};
    if (!valid_basis(candidate)) return false;
    *basis = candidate;
    return true;
}

bool level_aircraft_seat_basis(const wawvr::xr::Basis3f& seat,
                              wawvr::xr::Basis3f* const leveled) noexcept {
    if (leveled == nullptr || !valid_basis(seat)) return false;
    wawvr::xr::Vec3f forward{};
    if (!normalize({seat.forward.x, seat.forward.y, 0.0F}, &forward))
        return false;
    *leveled = {forward, {-forward.y, forward.x, 0.0F}, {0.0F, 0.0F, 1.0F}};
    return true;
}

bool aircraft_controller_world_direction(
    const wawvr::xr::Basis3f& stable_seat_basis,
    const wawvr::xr::Vec3f& controller_forward_in_tracking_anchor,
    wawvr::xr::Vec3f* const world_direction) noexcept {
    if (world_direction == nullptr || !valid_basis(stable_seat_basis))
        return false;
    wawvr::xr::Vec3f local{};
    if (!normalize(controller_forward_in_tracking_anchor, &local)) return false;
    return normalize(compose(stable_seat_basis, local), world_direction);
}

bool build_aircraft_comfort_seat_basis(
    const wawvr::xr::Basis3f& hull,
    const wawvr::xr::Vec2f& rest_pitch_yaw,
    wawvr::xr::Basis3f* const basis) noexcept {
    if (basis == nullptr || !finite(rest_pitch_yaw)) return false;
    wawvr::xr::Basis3f leveled_hull{};
    return level_aircraft_seat_basis(hull,&leveled_hull) &&
        compose_aircraft_seat_basis(leveled_hull,{0,rest_pitch_yaw.y},basis);
}

bool aircraft_world_origin_to_hull_local(
    const wawvr::xr::Vec3f& world_origin,
    const wawvr::xr::Vec3f& hull_origin,
    const wawvr::xr::Basis3f& hull,
    wawvr::xr::Vec3f* const hull_local_origin) noexcept {
    if (hull_local_origin == nullptr || !finite(world_origin) ||
        !finite(hull_origin) || !valid_basis(hull)) return false;
    const wawvr::xr::Vec3f relative{world_origin.x-hull_origin.x,
        world_origin.y-hull_origin.y, world_origin.z-hull_origin.z};
    const wawvr::xr::Vec3f candidate{dot(relative,hull.forward),
        dot(relative,hull.left),dot(relative,hull.up)};
    if (!finite(candidate)) return false;
    *hull_local_origin = candidate;
    return true;
}

bool aircraft_hull_local_origin_to_world(
    const wawvr::xr::Vec3f& hull_local_origin,
    const wawvr::xr::Vec3f& hull_origin,
    const wawvr::xr::Basis3f& hull,
    wawvr::xr::Vec3f* const world_origin) noexcept {
    if (world_origin == nullptr || !finite(hull_local_origin) ||
        !finite(hull_origin) || !valid_basis(hull)) return false;
    const auto relative = compose(hull,hull_local_origin);
    const wawvr::xr::Vec3f candidate{hull_origin.x+relative.x,
        hull_origin.y+relative.y,hull_origin.z+relative.z};
    if (!finite(candidate)) return false;
    *world_origin = candidate;
    return true;
}

bool aircraft_world_direction_to_seat_view(
    const wawvr::xr::Vec3f& world_direction,
    const wawvr::xr::Basis3f& hull,
    const wawvr::xr::Vec2f& rest_pitch_yaw,
    wawvr::xr::Vec2f* const native_view_pitch_yaw) noexcept {
    if (native_view_pitch_yaw == nullptr || !valid_basis(hull) ||
        !finite(rest_pitch_yaw)) return false;
    wawvr::xr::Vec3f world{};
    if (!normalize(world_direction, &world)) return false;
    const wawvr::xr::Vec3f local{
        dot(world, hull.forward), dot(world, hull.left), dot(world, hull.up)};
    const float horizontal = std::hypot(local.x, local.y);
    // A precisely vertical ray has no unique yaw. Preserve the seat's own
    // heading instead of manufacturing a world-axis yaw discontinuity.
    const float yaw = horizontal > 1.0e-5F
        ? std::atan2(local.y, local.x)*kDegreesPerRadian
        : std::remainder(rest_pitch_yaw.y, 360.0F);
    const float pitch = -std::atan2(local.z, horizontal)*kDegreesPerRadian;
    const wawvr::xr::Vec2f candidate{
        std::remainder(pitch - std::remainder(rest_pitch_yaw.x, 360.0F), 360.0F),
        std::remainder(yaw - std::remainder(rest_pitch_yaw.y, 360.0F), 360.0F)};
    if (!finite(candidate)) return false;
    *native_view_pitch_yaw = candidate;
    return true;
}

bool clamp_aircraft_native_view(
    const wawvr::xr::Vec2f& desired_pitch_yaw,
    const wawvr::xr::Vec2f& native_clamp_base,
    const wawvr::xr::Vec2f& native_clamp_range,
    wawvr::xr::Vec2f* const clamped_pitch_yaw) noexcept {
    if (clamped_pitch_yaw == nullptr || !finite(desired_pitch_yaw) ||
        !finite(native_clamp_base) || !finite(native_clamp_range) ||
        native_clamp_range.x < 0.0F || native_clamp_range.y < 0.0F) return false;
    constexpr float interior_margin = 2.0F * (360.0F / 65536.0F);
    const auto clamp_axis = [](const float desired, const float base,
                               const float range) noexcept {
        const float wrapped = std::remainder(desired,360.0F);
        if (range >= 180.0F) return wrapped;
        const float center = std::remainder(base,360.0F);
        const float relative = std::remainder(wrapped-center,360.0F);
        const float allowed = std::max(0.0F,range-interior_margin);
        return std::remainder(center+std::clamp(relative,-allowed,allowed),360.0F);
    };
    const wawvr::xr::Vec2f candidate{
        clamp_axis(desired_pitch_yaw.x,native_clamp_base.x,native_clamp_range.x),
        clamp_axis(desired_pitch_yaw.y,native_clamp_base.y,native_clamp_range.y)};
    if (!finite(candidate)) return false;
    *clamped_pitch_yaw = candidate;
    return true;
}

bool encode_aircraft_native_view_command(
    const wawvr::xr::Vec2f& desired_native_view_pitch_yaw,
    const wawvr::xr::Vec2f& delta_pitch_yaw,
    AircraftNativeViewCommand* const output) noexcept {
    if (output == nullptr || !finite(desired_native_view_pitch_yaw) ||
        !finite(delta_pitch_yaw)) return false;
    AircraftNativeViewCommand candidate{};
    candidate.client_pitch_yaw = {
        std::remainder(std::remainder(desired_native_view_pitch_yaw.x, 360.0F) -
                           std::remainder(delta_pitch_yaw.x, 360.0F), 360.0F),
        std::remainder(std::remainder(desired_native_view_pitch_yaw.y, 360.0F) -
                           std::remainder(delta_pitch_yaw.y, 360.0F), 360.0F)};
    const float angles[]{candidate.client_pitch_yaw.x,
                         candidate.client_pitch_yaw.y};
    for (std::size_t axis = 0; axis < 2; ++axis) {
        const auto encoded = static_cast<std::int32_t>(
            std::lround(angles[axis] * (65536.0F / 360.0F)));
        candidate.command_pitch_yaw[axis] = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(encoded) & 0xFFFFU);
    }
    *output = candidate;
    return true;
}

}  // namespace wawvr::mod
