// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "xr_types.h"

#include <cmath>
#include <cstdint>

namespace wawvr::mod {

// Upright head-yaw coordinates, in IW inches: +X forward, +Y left, +Z up.
// Keep the standing belt and add a front-lap reach instead of moving the belt
// to the face or requiring a controller to pass through a chair/armrest.
struct ReloadPouchPoint final { float x{}, y{}, z{}; };
inline constexpr ReloadPouchPoint kReloadPouchLapCenter{18.0F, 16.0F, -19.0F};
inline constexpr ReloadPouchPoint kReloadPouchLapHalfExtent{12.0F, 14.0F, 9.0F};
// Visual discovery anchor only. The compact pouch sits beside the near hip,
// just forward enough to see while looking down and inside both the standing
// belt and the raised seated-reach box. Never use it to narrow pickup reach.
inline constexpr ReloadPouchPoint kReloadPouchHipMarkerCenter{7.0F, 10.0F, -23.0F};
inline constexpr float kReloadPouchUnitsPerMeter = 39.37007874015748F;

[[nodiscard]] inline bool reload_pouch_contains(
    const ReloadPouchPoint& point, const bool left,
    const float margin = 0.0F) noexcept {
    if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
        !std::isfinite(point.z) || !std::isfinite(margin) ||
        margin < 0.0F || margin > 2.0F) return false;
    const float side = left ? point.y : -point.y;
    const bool standing = point.x >= -18.0F-margin && point.x <= 24.0F+margin &&
        side >= 2.0F-margin && side <= 24.0F+margin &&
        point.z >= -42.0F-margin && point.z <= -14.0F+margin;
    const bool seated_lap = point.x >= 6.0F-margin && point.x <= 30.0F+margin &&
        side >= 2.0F-margin && side <= 30.0F+margin &&
        point.z >= -28.0F-margin && point.z <= -10.0F+margin;
    return standing || seated_lap;
}

struct ReloadPouchUprightFrame final {
    wawvr::xr::Vec3f origin{};
    wawvr::xr::Vec3f forward{};
    wawvr::xr::Vec3f left{};
};

// Derive heading from a validated HMD pose, but never pitch/roll the pouch
// with a player looking down to find it. At the vertical forward-axis
// singularity retain the last upright heading, or use the nonsingular Y
// twist on the first sample. A straight-down search must not remove ammo.
[[nodiscard]] inline bool make_reload_pouch_upright_frame(
    const wawvr::xr::Posef& head, ReloadPouchUprightFrame* const output,
    const ReloadPouchUprightFrame* const last_heading = nullptr) noexcept {
    if (output == nullptr) return false;
    *output = {};
    const auto& p = head.position;
    const auto& q = head.orientation;
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
        !std::isfinite(q.x) || !std::isfinite(q.y) ||
        !std::isfinite(q.z) || !std::isfinite(q.w)) return false;
    const float norm = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    if (!std::isfinite(norm) || norm < 0.25F || norm > 4.0F) return false;
    const float twist_norm = q.y*q.y + q.w*q.w;
    if (twist_norm < 0.000001F*norm) return false; // Ambiguous upside-down twist.
    const float inverse = 1.0F / norm;
    float forward_x = -2.0F*(q.x*q.z + q.w*q.y)*inverse;
    float forward_z = -(1.0F - 2.0F*(q.x*q.x + q.y*q.y)*inverse);
    float horizontal = std::sqrt(forward_x*forward_x + forward_z*forward_z);
    if (!std::isfinite(horizontal)) return false;
    if (horizontal < 0.05F) {
        // Pitch and roll are indistinguishable at a vertical gaze. Preserve
        // the already observed torso heading rather than letting tiny pose
        // noise (or roll) rotate the pouch while the player searches for it.
        const float cached_length = last_heading == nullptr ? 0.0F : std::hypot(
            last_heading->forward.x,last_heading->forward.z);
        if (std::isfinite(cached_length) && cached_length > 0.99F && cached_length < 1.01F) {
            forward_x = last_heading->forward.x;
            forward_z = last_heading->forward.z;
        } else {
            forward_x = -2.0F*q.w*q.y/twist_norm;
            forward_z = -(1.0F-2.0F*q.y*q.y/twist_norm);
        }
        horizontal = std::hypot(forward_x,forward_z);
        if (!std::isfinite(horizontal) || horizontal < 0.05F) return false;
    }
    output->origin = p;
    output->forward = {forward_x/horizontal, 0.0F, forward_z/horizontal};
    output->left = {output->forward.z, 0.0F, -output->forward.x};
    return true;
}

[[nodiscard]] inline bool reload_pouch_point_from_tracking(
    const ReloadPouchUprightFrame& frame, const wawvr::xr::Vec3f& position,
    ReloadPouchPoint* const output) noexcept {
    if (output == nullptr) return false;
    *output = {};
    if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(position.z)) return false;
    const float dx = position.x-frame.origin.x;
    const float dz = position.z-frame.origin.z;
    *output = {(dx*frame.forward.x + dz*frame.forward.z)*kReloadPouchUnitsPerMeter,
        (dx*frame.left.x + dz*frame.left.z)*kReloadPouchUnitsPerMeter,
        (position.y-frame.origin.y)*kReloadPouchUnitsPerMeter};
    return std::isfinite(output->x) && std::isfinite(output->y) && std::isfinite(output->z);
}

[[nodiscard]] inline wawvr::xr::Vec3f reload_pouch_lap_center_tracking(
    const ReloadPouchUprightFrame& frame, const bool left) noexcept {
    const float x = kReloadPouchLapCenter.x/kReloadPouchUnitsPerMeter;
    const float y = (left ? kReloadPouchLapCenter.y : -kReloadPouchLapCenter.y)/kReloadPouchUnitsPerMeter;
    return {frame.origin.x + x*frame.forward.x + y*frame.left.x,
        frame.origin.y + kReloadPouchLapCenter.z/kReloadPouchUnitsPerMeter,
        frame.origin.z + x*frame.forward.z + y*frame.left.z};
}

[[nodiscard]] inline wawvr::xr::Vec3f reload_pouch_hip_marker_tracking(
    const ReloadPouchUprightFrame& frame, const bool left) noexcept {
    const float x = kReloadPouchHipMarkerCenter.x/kReloadPouchUnitsPerMeter;
    const float y = (left ? kReloadPouchHipMarkerCenter.y : -kReloadPouchHipMarkerCenter.y)/kReloadPouchUnitsPerMeter;
    return {frame.origin.x + x*frame.forward.x + y*frame.left.x,
        frame.origin.y + kReloadPouchHipMarkerCenter.z/kReloadPouchUnitsPerMeter,
        frame.origin.z + x*frame.forward.z + y*frame.left.z};
}

struct ReloadPouchCueInput final {
    bool eligible{};
    bool pose_valid{};
    bool hand_busy{};
    bool left_hand{true};
    ReloadPouchPoint point{};
    std::uint64_t action_sequence{};
    std::uint64_t milliseconds{};
};
struct ReloadPouchCueState final {
    bool baselined{};
    bool inside{};
    bool left_hand{true};
    std::uint64_t last_action_sequence{};
    std::uint64_t last_sample_milliseconds{};
    std::uint64_t last_cue_milliseconds{};
};
struct ReloadPouchCueOutput final { bool visible{}, hovered{}, pulse{}; };

// This is discovery feedback only: it never requests a grab, reload or ammo
// transfer. Fresh squeeze edges and authoritative ammo accounting stay in the
// existing reload transaction. Recovery baselines the hand before any cue.
[[nodiscard]] inline ReloadPouchCueOutput update_reload_pouch_cue(
    const ReloadPouchCueInput& input, ReloadPouchCueState* const state) noexcept {
    if (state == nullptr) return {};
    ReloadPouchCueOutput output{};
    const bool valid = input.eligible && input.pose_valid && !input.hand_busy &&
        input.action_sequence != 0 && input.milliseconds != 0;
    if (!valid) {
        state->baselined = false;
        state->inside = false;
        state->last_action_sequence = input.action_sequence;
        state->last_sample_milliseconds = input.milliseconds;
        return output;
    }
    output.visible = true;
    output.hovered = reload_pouch_contains(input.point, input.left_hand);
    const bool clock_valid = state->last_sample_milliseconds != 0 &&
        input.milliseconds >= state->last_sample_milliseconds &&
        input.milliseconds-state->last_sample_milliseconds <= 100;
    if (!state->baselined || !clock_valid || state->left_hand != input.left_hand ||
        input.action_sequence < state->last_action_sequence) {
        state->baselined = true;
        state->inside = output.hovered;
        state->left_hand = input.left_hand;
        state->last_action_sequence = input.action_sequence;
        state->last_sample_milliseconds = input.milliseconds;
        return output;
    }
    if (input.action_sequence == state->last_action_sequence) return output;
    state->last_action_sequence = input.action_sequence;
    state->last_sample_milliseconds = input.milliseconds;
    if (!state->inside && output.hovered) {
        output.pulse = state->last_cue_milliseconds == 0 ||
            (input.milliseconds >= state->last_cue_milliseconds &&
             input.milliseconds-state->last_cue_milliseconds >= 500);
        if (output.pulse) state->last_cue_milliseconds = input.milliseconds;
        state->inside = true;
    } else if (state->inside && !reload_pouch_contains(input.point, input.left_hand, 2.0F)) {
        state->inside = false;
    }
    return output;
}

} // namespace wawvr::mod
