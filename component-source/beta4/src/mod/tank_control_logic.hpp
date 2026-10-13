// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>

namespace wawvr::mod {

inline constexpr float kTankControlDeadzone = 0.20F;
inline constexpr float kTankControlYawDegreesPerSecond = 65.0F;
inline constexpr float kTankControlPitchDegreesPerSecond = 35.0F;
inline constexpr std::uint64_t kTankControlMaximumElapsedMilliseconds = 50;

struct TankControlInput final {
    // The caller owns validated tank context, active gameplay, XR freshness,
    // and focus. Losing any of those conditions clears only sample timing.
    bool input_owned{};
    bool stick_active{};
    std::uint64_t action_sequence{};
    std::uint64_t now_milliseconds{};
    float stick_x{};
    float stick_y{};
};

struct TankControlState final {
    bool input_was_owned{};
    std::uint64_t last_action_sequence{};
    std::uint64_t previous_update_milliseconds{};
};

struct TankControlDelta final {
    float pitch_degrees{};
    float yaw_degrees{};
};

// Returns angular displacement for one fresh input sample. Positive stick X
// rotates right (negative engine yaw); positive Y raises the actual cannon
// and its world-space target (negative engine pitch). The native target and
// rendered aim-marker direction verify this sign, not a head-fixed crosshair.
// A radial deadzone preserves diagonal aiming. A linear/cubic blend provides
// fine aim near center without reducing full-stick speed.
//
// This function never owns or recenters the turret target: the native runtime
// integrates these deltas, retaining its exact angle whenever the result is
// zero, and applies the current vehicle's native mechanical limits.
[[nodiscard]] TankControlDelta update_tank_control(
    const TankControlInput& input, TankControlState* state) noexcept;

// Scale each requested axis to the native turret's maximum traverse speed.
// Unknown, nonpositive, or implausible native rates fail closed to zero.
[[nodiscard]] TankControlDelta limit_tank_control_delta(
    TankControlDelta requested, float native_degrees_per_second) noexcept;

// CL angles exclude native ps.delta_angles. Preserve that representation by
// adding the same displacement to the live CL angle and the completed native
// command rather than reconstructing either from the other. A zero axis is
// left bit-for-bit unchanged, including any native angle representation.
// Returns false without mutation for missing or invalid input. The caller
// validates writable storage and the native tank context before calling.
[[nodiscard]] bool apply_tank_control_delta(
    TankControlDelta delta, float* client_pitch, float* client_yaw,
    std::array<std::int32_t, 3>* command_view_angles) noexcept;

// Call on vehicle changes or input ownership loss. The next accepted sample
// establishes its timestamp and contributes no displacement.
void reset_tank_control(TankControlState* state) noexcept;

}  // namespace wawvr::mod
