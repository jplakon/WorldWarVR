// SPDX-License-Identifier: GPL-3.0-only
#include "xr_math.h"
#include "runtime_policy.hpp"

#include <cmath>
#include <cstdio>

namespace
{

bool Near(const float left, const float right, const float tolerance = 1.0e-4f)
{
    return std::fabs(left - right) <= tolerance;
}

bool CheckVector(
    const wawvr::xr::Vec3f& value,
    const wawvr::xr::Vec3f& expected,
    const char* label)
{
    if (Near(value.x, expected.x) &&
        Near(value.y, expected.y) &&
        Near(value.z, expected.z))
    {
        return true;
    }
    std::fprintf(
        stderr,
        "%s: got {%f, %f, %f}, expected {%f, %f, %f}\n",
        label,
        value.x,
        value.y,
        value.z,
        expected.x,
        expected.y,
        expected.z);
    return false;
}

} // namespace

int main()
{
    using namespace wawvr::xr;
    bool passed = true;

    if (!prefer_stage_reference_space("SteamVR/OpenXR", false) ||
        !prefer_stage_reference_space("SteamVR", false) ||
        !prefer_stage_reference_space("steamvr/openxr", false) ||
        prefer_stage_reference_space("VirtualDesktopXR", false) ||
        prefer_stage_reference_space("Oculus", false) ||
        prefer_stage_reference_space("Pimax OpenXR", false) ||
        prefer_stage_reference_space("OpenXR Simulator", false) ||
        prefer_stage_reference_space("NotSteamVR", false) ||
        prefer_stage_reference_space("SteamVRImposter", false) ||
        prefer_stage_reference_space("Steam", false) ||
        prefer_stage_reference_space("", false) ||
        prefer_stage_reference_space(nullptr, false) ||
        !prefer_stage_reference_space("VirtualDesktopXR", true) ||
        !prefer_stage_reference_space(nullptr, true))
    {
        std::fprintf(stderr, "OpenXR reference-space selection failed\n");
        passed = false;
    }

    // A floor-level reference changes the initial absolute height, not hand
    // movement relative to the player's recentered origin.
    Posef floor_reference{};
    floor_reference.position.y = 1.65F;
    Posef hand_low = floor_reference;
    hand_low.position = {0.25F, 1.25F, -0.4F};
    Posef hand_high = hand_low;
    hand_high.position.y += 0.30F;
    const auto low = OpenXrPoseToIwRelative(hand_low, floor_reference, 1.0F);
    const auto high = OpenXrPoseToIwRelative(hand_high, floor_reference, 1.0F);
    passed &= CheckVector(low.position, {0.4F, -0.25F, -0.4F},
                          "stage recenter preserves hand-relative placement");
    passed &= CheckVector(
        {high.position.x - low.position.x, high.position.y - low.position.y,
         high.position.z - low.position.z}, {0.0F, 0.0F, 0.30F},
        "physical hand-up remains engine-up with floor reference");

    passed &= CheckVector(
        OpenXrVectorToIw({0.0f, 0.0f, -1.0f}),
        {1.0f, 0.0f, 0.0f},
        "OpenXR forward -> IW forward");
    passed &= CheckVector(
        OpenXrVectorToIw({-1.0f, 0.0f, 0.0f}),
        {0.0f, 1.0f, 0.0f},
        "OpenXR left -> IW left");
    passed &= CheckVector(
        OpenXrVectorToIw({0.0f, 1.0f, 0.0f}),
        {0.0f, 0.0f, 1.0f},
        "OpenXR up -> IW up");

    Posef reference = {};
    Posef one_meter_forward = {};
    one_meter_forward.position.z = -1.0f;
    const EnginePose converted = OpenXrPoseToIwRelative(
        one_meter_forward, reference, kIwUnitsPerMeter);
    passed &= CheckVector(
        converted.position,
        {kIwUnitsPerMeter, 0.0f, 0.0f},
        "meter scale");
    passed &= CheckVector(converted.axis.forward, {1.0f, 0.0f, 0.0f},
                          "identity forward basis");
    passed &= CheckVector(converted.axis.left, {0.0f, 1.0f, 0.0f},
                          "identity left basis");
    passed &= CheckVector(converted.axis.up, {0.0f, 0.0f, 1.0f},
                          "identity up basis");

    constexpr float half_sqrt_two = 0.7071067811865475f;
    Posef yaw_right = {};
    yaw_right.orientation = {0.0f, half_sqrt_two, 0.0f, half_sqrt_two};
    const EnginePose yaw_converted = OpenXrPoseToIwRelative(
        yaw_right, reference, 1.0f);
    passed &= CheckVector(
        yaw_converted.axis.forward,
        {0.0f, 1.0f, 0.0f},
        "+90-degree OpenXR yaw maps forward onto IW left");

    const Quaternionf normalized = Normalize({0.0f, 0.0f, 0.0f, 2.0f});
    if (!Near(normalized.w, 1.0f))
    {
        std::fprintf(stderr, "quaternion normalization failed\n");
        passed = false;
    }

    const ActionSnapshot default_actions = {};
    if (default_actions.focused || default_actions.sequence != 0 ||
        default_actions.hands[0].trigger.active ||
        default_actions.hands[1].aim.position_valid ||
        default_actions.hands[1].aim.orientation_tracked ||
        default_actions.hands[1].aim.position_tracked)
    {
        std::fprintf(stderr, "action-state defaults are not inactive\n");
        passed = false;
    }

    if (layer_kind_for_runtime_frame(
            false, CompositionLayerKind::quad) !=
            CompositionLayerKind::none ||
        layer_kind_for_runtime_frame(
            false, CompositionLayerKind::projection) !=
            CompositionLayerKind::none ||
        layer_kind_for_runtime_frame(
            true, CompositionLayerKind::quad) !=
            CompositionLayerKind::quad)
    {
        std::fprintf(stderr,
                     "OpenXR shouldRender layer policy is incorrect\n");
        passed = false;
    }

    if (!should_log_repeated_runtime_issue(1) ||
        should_log_repeated_runtime_issue(2) ||
        should_log_repeated_runtime_issue(599) ||
        !should_log_repeated_runtime_issue(600) ||
        should_log_repeated_runtime_issue(601) ||
        !should_log_repeated_runtime_issue(1200) ||
        !should_log_repeated_runtime_issue(1, 0) ||
        should_log_repeated_runtime_issue(2, 0))
    {
        std::fprintf(stderr,
                     "OpenXR repeated-issue log policy is incorrect\n");
        passed = false;
    }

    constexpr std::int64_t period_72_hz = 13'888'889;
    if (std::fabs(duration_milliseconds(period_72_hz) - 13.888889) >
            1.0e-6 ||
        std::fabs(predicted_refresh_hz(period_72_hz) - 72.0) > 0.01 ||
        predicted_refresh_hz(0) != 0.0 ||
        inferred_missed_display_intervals(period_72_hz, period_72_hz) != 0 ||
        inferred_missed_display_intervals(
            period_72_hz * 2, period_72_hz) != 1 ||
        inferred_missed_display_intervals(-1, period_72_hz) != 0 ||
        should_report_frame_timing(
            kFrameTimingReportIntervalNanoseconds - 1, 4'319) ||
        !should_report_frame_timing(
            kFrameTimingReportIntervalNanoseconds, 4'320) ||
        should_report_frame_timing(
            kFrameTimingReportIntervalNanoseconds, 0))
    {
        std::fprintf(stderr,
                     "OpenXR predicted-display timing math is incorrect\n");
        passed = false;
    }

    return passed ? 0 : 1;
}
