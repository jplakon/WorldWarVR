// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "xr_types.h"

#include <cstdint>

namespace wawvr::xr
{

constexpr std::uint64_t kFrameTimingReportIntervalNanoseconds =
    60'000'000'000ULL;

// The SteamVR 2.17.9 x86 LOCAL origin was observed starting rolled by 180
// degrees, then changing when its seated zero pose initialized. Use its
// floor-aligned STAGE reference for all poses AND composition layers instead
// of compensating image UVs or individual controllers. Other runtimes retain
// their configured space; explicit STAGE requests remain respected.
[[nodiscard]] constexpr bool prefer_stage_reference_space(
    const char* runtime_name, const bool configured_stage) noexcept
{
    if (configured_stage) return true;
    if (runtime_name == nullptr) return false;
    constexpr char prefix[] = "steamvr";
    for (unsigned i = 0; i < 7; ++i)
    {
        char value = runtime_name[i];
        if (value >= 'A' && value <= 'Z') value += 'a' - 'A';
        if (value != prefix[i]) return false;
    }
    return runtime_name[7] == '\0' || runtime_name[7] == '/' ||
           runtime_name[7] == ' ';
}

// OpenXR requires xrEndFrame even when shouldRender is false, but it expects
// zero composition layers for that frame.  Treating the missing layer as bad
// metadata creates a render-thread log storm while the session is obscured or
// losing focus.
[[nodiscard]] constexpr CompositionLayerKind layer_kind_for_runtime_frame(
    const bool should_render,
    const CompositionLayerKind requested) noexcept
{
    return should_render ? requested : CompositionLayerKind::none;
}

// Report a persistent validation failure once immediately and then sparsely.
// The caller still counts every occurrence; only disk/debugger output is
// throttled.
[[nodiscard]] constexpr bool should_log_repeated_runtime_issue(
    const std::uint64_t occurrence,
    const std::uint64_t interval = 600) noexcept
{
    return occurrence == 1 ||
           (interval != 0 && occurrence % interval == 0);
}

[[nodiscard]] constexpr double duration_milliseconds(
    const std::int64_t duration_nanoseconds) noexcept
{
    return duration_nanoseconds > 0
        ? static_cast<double>(duration_nanoseconds) / 1'000'000.0
        : 0.0;
}

[[nodiscard]] constexpr double predicted_refresh_hz(
    const std::int64_t predicted_display_period) noexcept
{
    const double period_ms = duration_milliseconds(
        predicted_display_period);
    return period_ms > 0.0 ? 1000.0 / period_ms : 0.0;
}


// Infer skipped display intervals from adjacent predicted display times. The
// runtime remains authoritative; this is diagnostic math, rounded to the
// nearest whole refresh interval to tolerate small timestamp jitter.
[[nodiscard]] constexpr std::uint64_t inferred_missed_display_intervals(
    const std::int64_t predicted_display_step,
    const std::int64_t predicted_display_period) noexcept
{
    if (predicted_display_step <= 0 || predicted_display_period <= 0)
    {
        return 0;
    }
    const auto step = static_cast<std::uint64_t>(predicted_display_step);
    const auto period = static_cast<std::uint64_t>(predicted_display_period);
    std::uint64_t intervals = step / period;
    const std::uint64_t rounding_threshold = period / 2 + period % 2;
    if (step % period >= rounding_threshold)
    {
        intervals += 1;
    }
    return intervals > 1 ? intervals - 1 : 0;
}

[[nodiscard]] constexpr bool should_report_frame_timing(
    const std::uint64_t accumulated_predicted_period_nanoseconds,
    const std::uint64_t sample_count) noexcept
{
    return sample_count != 0 &&
           accumulated_predicted_period_nanoseconds >=
               kFrameTimingReportIntervalNanoseconds;
}

} // namespace wawvr::xr
