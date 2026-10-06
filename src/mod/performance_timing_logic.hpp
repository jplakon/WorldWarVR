#pragma once

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>

namespace wawvr::mod {

// Captured on the outer Com_Frame thread only. Nested phase times are
// inclusive; they must not be added together as independent frame costs.
template <std::size_t PhaseCount>
struct PerformanceFrameSample final {
    std::uint64_t sequence{};
    std::uint64_t xr_frame_id{};
    std::uint32_t scene_views{};
    std::array<std::uint64_t, PhaseCount> nanoseconds{};
};

template <std::size_t PhaseCount>
constexpr void retain_slowest_performance_frame(
    PerformanceFrameSample<PhaseCount>* destination,
    const PerformanceFrameSample<PhaseCount>& sample,
    std::size_t phase) noexcept {
    if (destination != nullptr && phase < PhaseCount && sample.sequence != 0 &&
        (destination->sequence == 0 ||
         sample.nanoseconds[phase] > destination->nanoseconds[phase])) {
        *destination = sample;
    }
}

inline constexpr std::uint64_t kPerformanceSpikeThresholdNanoseconds =
    13'889'000u;

struct PerformanceTimingAggregate final {
    std::uint64_t count{};
    std::uint64_t total_nanoseconds{};
    std::uint64_t maximum_nanoseconds{};
    std::uint64_t spike_count{};
};

constexpr void add_performance_timing_sample(
    PerformanceTimingAggregate* const aggregate,
    const std::uint64_t duration_nanoseconds,
    const std::uint64_t spike_threshold_nanoseconds =
        kPerformanceSpikeThresholdNanoseconds) noexcept {
    if (aggregate == nullptr) {
        return;
    }
    ++aggregate->count;
    aggregate->total_nanoseconds += duration_nanoseconds;
    if (duration_nanoseconds > aggregate->maximum_nanoseconds) {
        aggregate->maximum_nanoseconds = duration_nanoseconds;
    }
    if (duration_nanoseconds > spike_threshold_nanoseconds) {
        ++aggregate->spike_count;
    }
}

// Diagnostics can be recorded from the front-end, back-end, Present, and
// post-Com_Frame paths concurrently. Keep each report window coherent: the
// reporter must never reset count/total/max/spikes independently while a
// producer is midway through one sample.
class PerformanceTimingWindow final {
public:
    void add_sample(
        const std::uint64_t duration_nanoseconds,
        const std::uint64_t spike_threshold_nanoseconds =
            kPerformanceSpikeThresholdNanoseconds) noexcept {
        lock();
        add_performance_timing_sample(
            &aggregate_, duration_nanoseconds, spike_threshold_nanoseconds);
        unlock();
    }

    [[nodiscard]] PerformanceTimingAggregate take() noexcept {
        lock();
        const PerformanceTimingAggregate result = aggregate_;
        aggregate_ = {};
        unlock();
        return result;
    }

    PerformanceTimingWindow() noexcept = default;
    PerformanceTimingWindow(const PerformanceTimingWindow&) = delete;
    PerformanceTimingWindow& operator=(const PerformanceTimingWindow&) =
        delete;

private:
    void lock() noexcept {
        while (guard_.test_and_set(std::memory_order_acquire)) {
        }
    }

    void unlock() noexcept { guard_.clear(std::memory_order_release); }

    std::atomic_flag guard_ = ATOMIC_FLAG_INIT;
    PerformanceTimingAggregate aggregate_{};
};

[[nodiscard]] constexpr double performance_timing_average_milliseconds(
    const PerformanceTimingAggregate& aggregate) noexcept {
    return aggregate.count != 0
        ? static_cast<double>(aggregate.total_nanoseconds) /
              static_cast<double>(aggregate.count) / 1'000'000.0
        : 0.0;
}

[[nodiscard]] constexpr double performance_timing_maximum_milliseconds(
    const PerformanceTimingAggregate& aggregate) noexcept {
    return static_cast<double>(aggregate.maximum_nanoseconds) / 1'000'000.0;
}

} // namespace wawvr::mod
