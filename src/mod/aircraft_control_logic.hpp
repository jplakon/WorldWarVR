// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "xr_types.h"

#include <array>
#include <cstdint>

namespace wawvr::mod {

// The runtime reads this data from the verified SP layout. Passenger slots
// contain raw entity numbers, not EntHandles: zero identifies the local player.
struct AircraftContextData final {
    bool active_connection{};
    std::int32_t local_client_number{-1};
    std::uint32_t player_flags{};
    std::int32_t vehicle_entity{-1};
    std::int32_t entity_type{-1};
    std::int32_t vehicle_type{-1};
    std::int32_t seat{-1};
    std::int32_t passenger_entity{-1};
};

[[nodiscard]] bool aircraft_controller_context_valid(
    const AircraftContextData& context) noexcept;
// Native authored-rest/transition frames still own vehicle controls. Suppress
// handheld/snap input there, but do not replace the authored gun/camera pose.
[[nodiscard]] bool aircraft_passenger_context_present(
    const AircraftContextData& context) noexcept;

// IW angles and axes: pitch/yaw/roll in degrees, forward/left/up basis.
// Full composition retains banking so native gun-space conversion remains
// correct. It does not read or feed back the controller-owned native view.
[[nodiscard]] bool aircraft_basis_from_angles(
    const wawvr::xr::Vec3f& angles,
    wawvr::xr::Basis3f* basis) noexcept;

[[nodiscard]] bool compose_aircraft_seat_basis(
    const wawvr::xr::Basis3f& hull,
    const wawvr::xr::Vec2f& rest_pitch_yaw,
    wawvr::xr::Basis3f* basis) noexcept;

// The existing stereo comfort path removes pitch and roll from its base.
[[nodiscard]] bool level_aircraft_seat_basis(
    const wawvr::xr::Basis3f& seat,
    wawvr::xr::Basis3f* leveled) noexcept;

// Aim and capture-only stereo must use this SAME stable comfort basis. Level
// the hull first, then apply authored seat yaw only. In particular the rear
// gun's 81-degree rest pitch must not flip the view by 180 degrees when the
// aircraft pitches across nine degrees. Full hull/rest angles still belong
// to native gun-space conversion, never to the comfort camera horizon.
[[nodiscard]] bool build_aircraft_comfort_seat_basis(
    const wawvr::xr::Basis3f& hull,
    const wawvr::xr::Vec2f& rest_pitch_yaw,
    wawvr::xr::Basis3f* basis) noexcept;

// Capture the stock seat camera origin only after native seat setup has run,
// then retain this hull-local offset for that seat. Reconstructing it from
// subsequent hull transforms follows the aircraft without importing the
// native camera's controller-aim-dependent orbit. The runtime owns the
// seat/entity/map lifecycle and must discard the offset when those change.
[[nodiscard]] bool aircraft_world_origin_to_hull_local(
    const wawvr::xr::Vec3f& world_origin,
    const wawvr::xr::Vec3f& hull_origin,
    const wawvr::xr::Basis3f& hull,
    wawvr::xr::Vec3f* hull_local_origin) noexcept;

[[nodiscard]] bool aircraft_hull_local_origin_to_world(
    const wawvr::xr::Vec3f& hull_local_origin,
    const wawvr::xr::Vec3f& hull_origin,
    const wawvr::xr::Basis3f& hull,
    wawvr::xr::Vec3f* world_origin) noexcept;

[[nodiscard]] bool aircraft_controller_world_direction(
    const wawvr::xr::Basis3f& stable_seat_basis,
    const wawvr::xr::Vec3f& controller_forward_in_tracking_anchor,
    wawvr::xr::Vec3f* world_direction) noexcept;

// Convert the world ray back into the aircraft's full banked hull frame and
// subtract the authored gunner rest angles. The verified passenger consumer
// adds those rest angles back to ps.viewangles when publishing gun angles.
// Native movement/gun code remains responsible for each seat's actual limits.
[[nodiscard]] bool aircraft_world_direction_to_seat_view(
    const wawvr::xr::Vec3f& world_direction,
    const wawvr::xr::Basis3f& hull,
    const wawvr::xr::Vec2f& rest_pitch_yaw,
    wawvr::xr::Vec2f* native_view_pitch_yaw) noexcept;

// Absolute VR targets must stay inside the live native PS arc before command
// serialization. Repeatedly asking for an unreachable angle makes native
// range limiting keep rewriting delta_angles. A two-command-quantum interior
// margin prevents angle-short rounding from pushing a boundary back outside.
// Native limits remain authoritative; this never widens a station's arc.
[[nodiscard]] bool clamp_aircraft_native_view(
    const wawvr::xr::Vec2f& desired_pitch_yaw,
    const wawvr::xr::Vec2f& native_clamp_base,
    const wawvr::xr::Vec2f& native_clamp_range,
    wawvr::xr::Vec2f* clamped_pitch_yaw) noexcept;

struct AircraftNativeViewCommand final {
    wawvr::xr::Vec2f client_pitch_yaw{};
    std::array<std::int32_t, 2> command_pitch_yaw{};
};

// CL and usercmd angles exclude ps.delta_angles. Only pitch/yaw are returned;
// callers preserve the native roll and every unrelated command field.
[[nodiscard]] bool encode_aircraft_native_view_command(
    const wawvr::xr::Vec2f& desired_native_view_pitch_yaw,
    const wawvr::xr::Vec2f& delta_pitch_yaw,
    AircraftNativeViewCommand* output) noexcept;

}  // namespace wawvr::mod
