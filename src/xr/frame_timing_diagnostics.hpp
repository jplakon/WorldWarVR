// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "xr_types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace wawvr::xr {

// CPU wall durations only: submission/Flush timing is NOT GPU execution time.
enum class FrameTimingStage : std::uint8_t {
    wait_frame, begin_frame, application, end_frame,
    d3d9_capture, d3d9_present, shared_acquire, shared_release,
    compositor_prepare, compositor_submit,
    acquire_left, acquire_right, wait_left, wait_right,
    release_left, release_right, flush_left, flush_right,
    count
};
inline constexpr std::size_t kFrameTimingStageCount =
    static_cast<std::size_t>(FrameTimingStage::count);
inline constexpr std::array<const char*, kFrameTimingStageCount> kFrameTimingStageNames{
    "wait-frame", "begin-frame", "app", "end-frame", "d3d9-capture",
    "d3d9-present", "shared-acquire", "shared-release", "source-prepare",
    "compositor-submit", "acquire-L", "acquire-R", "wait-L", "wait-R",
    "release-L", "release-R", "flush-L", "flush-R"};

struct FrameTimingHistogram final {
    // 0, 0.25ms bins through16ms, 1ms through80ms, 4ms through336ms,
    // then overflow. Percentiles are bin upper bounds, not exact quantiles.
    std::array<std::uint32_t, 194> bins{};
    std::uint64_t count{};
    std::uint64_t total_ns{};
    std::uint64_t max_ns{};

    static constexpr std::size_t bucket(std::uint64_t ns) noexcept {
        if (ns == 0) return 0;
        if (ns <= 16'000'000)
            return static_cast<std::size_t>(1 + (ns - 1) / 250'000);
        if (ns <= 80'000'000)
            return static_cast<std::size_t>(65 + (ns - 16'000'001) / 1'000'000);
        if (ns <= 336'000'000)
            return static_cast<std::size_t>(129 + (ns - 80'000'001) / 4'000'000);
        return 193;
    }
    void add(std::uint64_t ns) noexcept {
        ++bins[bucket(ns)];
        ++count;
        total_ns += ns;
        if (ns > max_ns) max_ns = ns;
    }
    [[nodiscard]] double percentile_upper_ms(unsigned percent) const noexcept {
        if (count == 0) return 0.0;
        if (percent > 100) percent = 100;
        const std::uint64_t rank = (count * percent + 99) / 100;
        std::uint64_t seen = 0;
        for (std::size_t i = 0; i < bins.size(); ++i) {
            seen += bins[i];
            if (seen < (rank != 0 ? rank : 1)) continue;
            if (i <= 64) return static_cast<double>(i) * 0.25;
            if (i <= 128) return 16.0 + static_cast<double>(i - 64);
            if (i <= 192) return 80.0 + static_cast<double>(i - 128) * 4.0;
            return std::numeric_limits<double>::infinity();
        }
        return 0.0;
    }
};

struct FrameTimingSample final {
    std::uint64_t frame_id{};
    std::int64_t display_time{};
    std::int64_t display_period{};
    std::uint64_t source_frame_id{};
    std::uint64_t source_serial{};
    CompositionLayerKind kind{CompositionLayerKind::none};
    bool should_render{};
    bool end_succeeded{};
    bool presentation_valid{};
    bool observed_stereo{};
    bool reused_layer{};
    std::array<std::uint64_t, kFrameTimingStageCount> stage_ns{};
    std::uint32_t stage_mask{};

    [[nodiscard]] std::uint64_t value(FrameTimingStage stage) const noexcept {
        const auto i = static_cast<std::size_t>(stage);
        return i < stage_ns.size() ? stage_ns[i] : 0;
    }
    bool add(std::uint64_t expected_frame, FrameTimingStage stage,
             std::uint64_t ns) noexcept {
        const auto i = static_cast<std::size_t>(stage);
        if (frame_id == 0 || expected_frame != frame_id || i >= stage_ns.size())
            return false;
        stage_ns[i] += ns;
        stage_mask |= 1u << i;
        return true;
    }
};

struct FrameLayerTimingWindow final {
    static constexpr std::size_t kWorstCount = 2;
    static constexpr std::array<FrameTimingStage, 3> kRankStages{
        FrameTimingStage::wait_frame, FrameTimingStage::application,
        FrameTimingStage::end_frame};
    std::array<FrameTimingHistogram, kFrameTimingStageCount> stages{};
    std::array<std::array<FrameTimingSample, kWorstCount>, 3> worst{};
    std::uint64_t frames{};
    std::uint64_t failed_ends{};
    std::uint64_t observed_stereo_frames{};
    std::uint64_t observed_mono_frames{};
    std::uint64_t unknown_presentation_frames{};
    std::uint64_t reused_layers{};

    void add(const FrameTimingSample& sample) noexcept {
        ++frames;
        failed_ends += sample.end_succeeded ? 0u : 1u;
        if (!sample.presentation_valid) ++unknown_presentation_frames;
        else if (sample.observed_stereo) ++observed_stereo_frames;
        else ++observed_mono_frames;
        reused_layers += sample.reused_layer ? 1u : 0u;
        for (std::size_t i = 0; i < stages.size(); ++i)
            if ((sample.stage_mask & (1u << i)) != 0) stages[i].add(sample.stage_ns[i]);
        for (std::size_t metric = 0; metric < worst.size(); ++metric) {
            for (std::size_t rank = 0; rank < kWorstCount; ++rank) {
                if (worst[metric][rank].frame_id != 0 &&
                    sample.value(kRankStages[metric]) <=
                        worst[metric][rank].value(kRankStages[metric])) continue;
                for (std::size_t j = kWorstCount - 1; j > rank; --j)
                    worst[metric][j] = worst[metric][j - 1];
                worst[metric][rank] = sample;
                break;
            }
        }
    }
};

inline constexpr std::size_t frame_layer_timing_index(CompositionLayerKind kind) noexcept {
    return kind == CompositionLayerKind::projection ? 0u :
        (kind == CompositionLayerKind::quad ? 1u : 2u);
}
inline constexpr std::array<const char*, 3> kFrameLayerTimingNames{
    "projection", "quad", "none"};

} // namespace wawvr::xr
