// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "xr_math.h"
#include "xr_types.h"

#include <cstdint>

namespace wawvr::mod {

// These defaults intentionally match the established weapon-pose filter.
// Unlike that legacy path, this module filters only after both the grip and
// aim have been expressed relative to the exact head sample from their frame.
inline constexpr float kCurrentHeadLocalPositionResponse = 0.45F;
inline constexpr float kCurrentHeadLocalOrientationResponse = 0.55F;
inline constexpr float
    kCurrentHeadLocalMaximumPositionDiscontinuityMeters = 0.50F;
inline constexpr float
    kCurrentHeadLocalMaximumOrientationDiscontinuityDegrees = 120.0F;
inline constexpr std::uint64_t
    kCurrentHeadLocalMaximumFrameAgeMilliseconds = 250;

struct CurrentHeadLocalControllerPoseFilterConfig final {
    float position_response{kCurrentHeadLocalPositionResponse};
    float orientation_response{kCurrentHeadLocalOrientationResponse};
    float maximum_position_discontinuity_meters{
        kCurrentHeadLocalMaximumPositionDiscontinuityMeters};
    float maximum_orientation_discontinuity_degrees{
        kCurrentHeadLocalMaximumOrientationDiscontinuityDegrees};
    std::uint64_t maximum_frame_age_milliseconds{
        kCurrentHeadLocalMaximumFrameAgeMilliseconds};
};

// The filter owns one immutable result for each accepted XR generation. Its
// position and orientation history are already current-head-local; neither a
// tracking anchor nor a body-camera pose is part of this state.
struct CurrentHeadLocalControllerPoseFilterState final {
    bool valid{};
    std::uint64_t generation{};
    std::uint64_t publication_milliseconds{};
    float engine_units_per_meter{};
    CurrentHeadLocalControllerPoseFilterConfig config{};
    wawvr::xr::Vec3f filtered_grip_position{};
    wawvr::xr::Quaternionf filtered_aim_orientation{};
    wawvr::xr::EnginePose cached_pose{};
};

// Builds H_current^-1 * C_current from one exact frame. Grip translation comes
// from HandActionState::grip; weapon orientation comes from
// HandActionState::aim. The output uses IW forward/left/up rows and engine
// units. Invalid input fails without changing the caller's output.
[[nodiscard]] bool current_head_local_controller_pose(
    const wawvr::xr::Posef& frame_head,
    const wawvr::xr::HandActionState& hand,
    wawvr::xr::EnginePose* pose,
    float engine_units_per_meter = wawvr::xr::kIwUnitsPerMeter) noexcept;

// Applies a generation-keyed EMA to the already head-local pose. A generation
// advances history at most once; exact repeated consumers receive cached_pose.
// Invalid, stale, regressed, discontinuous, or configuration-inconsistent
// samples fail transactionally, preserving both state and caller output.
[[nodiscard]] bool filtered_current_head_local_controller_pose(
    const wawvr::xr::Posef& frame_head,
    const wawvr::xr::HandActionState& hand,
    std::uint64_t generation,
    std::uint64_t publication_milliseconds,
    std::uint64_t now_milliseconds,
    CurrentHeadLocalControllerPoseFilterState* state,
    wawvr::xr::EnginePose* pose,
    const CurrentHeadLocalControllerPoseFilterConfig& config = {},
    float engine_units_per_meter = wawvr::xr::kIwUnitsPerMeter) noexcept;

void reset_current_head_local_controller_pose_filter(
    CurrentHeadLocalControllerPoseFilterState* state) noexcept;

}  // namespace wawvr::mod
