#include "performance_timing_logic.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {

void expect(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    using namespace wawvr::mod;

    PerformanceFrameSample<4> worst{};
    PerformanceFrameSample<4> first{};
    first.sequence = 1;
    first.xr_frame_id = 100;
    first.scene_views = 2;
    first.nanoseconds = {10, 40, 20, 70};
    retain_slowest_performance_frame(&worst, first, 0);
    PerformanceFrameSample<4> second{};
    second.sequence = 2;
    second.xr_frame_id = 101;
    second.scene_views = 3;
    second.nanoseconds = {50, 5, 2, 57};
    retain_slowest_performance_frame(&worst, second, 0);
    expect(worst.sequence == 2 && worst.xr_frame_id == 101 &&
               worst.scene_views == 3 && worst.nanoseconds[1] == 5 &&
               worst.nanoseconds[3] == 57,
           "worst phase retains whole same-frame record, not independent maxima");
    retain_slowest_performance_frame(&worst, first, 0);
    retain_slowest_performance_frame(&worst, first, 4);
    retain_slowest_performance_frame<4>(nullptr, first, 0);
    expect(worst.sequence == 2,
           "smaller phase and invalid index cannot replace correlated record");
    worst = {};
    retain_slowest_performance_frame(&worst, first, 3);
    retain_slowest_performance_frame(&worst, second, 3);
    expect(worst.sequence == 1 && worst.nanoseconds[0] == 10,
           "selecting full frame does not reuse the independently slowest native phase");

    PerformanceTimingAggregate aggregate{};
    add_performance_timing_sample(
        &aggregate, kPerformanceSpikeThresholdNanoseconds - 1u);
    add_performance_timing_sample(
        &aggregate, kPerformanceSpikeThresholdNanoseconds);
    add_performance_timing_sample(
        &aggregate, kPerformanceSpikeThresholdNanoseconds + 1u);

    expect(aggregate.count == 3u, "all timing samples are counted");
    expect(
        aggregate.maximum_nanoseconds ==
            kPerformanceSpikeThresholdNanoseconds + 1u,
        "maximum timing sample is retained");
    expect(
        aggregate.spike_count == 1u,
        "only samples strictly above 13.889 ms are spikes");
    expect(
        std::abs(
            performance_timing_average_milliseconds(aggregate) - 13.889) <
            0.000001,
        "average timing converts nanoseconds to milliseconds");
    expect(
        std::abs(
            performance_timing_maximum_milliseconds(aggregate) -
            13.889001) < 0.000001,
        "maximum timing converts nanoseconds to milliseconds");

    PerformanceTimingAggregate empty{};
    add_performance_timing_sample(nullptr, 100u);
    expect(
        performance_timing_average_milliseconds(empty) == 0.0,
        "empty aggregate has a zero average");

    PerformanceTimingAggregate per_view{};
    add_performance_timing_sample(&per_view, 8'000'000u);
    add_performance_timing_sample(&per_view, 8'000'000u);
    PerformanceTimingAggregate completed_batch{};
    add_performance_timing_sample(&completed_batch, 16'000'000u);
    expect(
        per_view.spike_count == 0u && completed_batch.spike_count == 1u,
        "completed batch timing exposes two sub-budget views that miss the frame budget in aggregate");

    PerformanceTimingWindow concurrent_window;
    constexpr std::uint64_t kWriterSamples = 20'000u;
    constexpr std::uint64_t kShortSampleNanoseconds = 1'000'000u;
    constexpr std::uint64_t kLongSampleNanoseconds = 20'000'000u;
    std::atomic<std::uint32_t> completed_writers{0};
    const auto writer = [&](const std::uint64_t duration_nanoseconds) {
        for (std::uint64_t index = 0; index < kWriterSamples; ++index) {
            concurrent_window.add_sample(duration_nanoseconds);
        }
        completed_writers.fetch_add(1, std::memory_order_release);
    };
    std::thread short_writer(writer, kShortSampleNanoseconds);
    std::thread long_writer(writer, kLongSampleNanoseconds);

    PerformanceTimingAggregate combined{};
    const auto merge_window = [&](const PerformanceTimingAggregate& window) {
        combined.count += window.count;
        combined.total_nanoseconds += window.total_nanoseconds;
        combined.maximum_nanoseconds = std::max(
            combined.maximum_nanoseconds, window.maximum_nanoseconds);
        combined.spike_count += window.spike_count;
    };
    while (completed_writers.load(std::memory_order_acquire) != 2u) {
        merge_window(concurrent_window.take());
        std::this_thread::yield();
    }
    short_writer.join();
    long_writer.join();
    merge_window(concurrent_window.take());

    expect(
        combined.count == kWriterSamples * 2u,
        "concurrent timing windows retain every sample across snapshots");
    expect(
        combined.total_nanoseconds ==
            kWriterSamples *
                (kShortSampleNanoseconds + kLongSampleNanoseconds),
        "concurrent timing windows keep count and total in the same window");
    expect(
        combined.maximum_nanoseconds == kLongSampleNanoseconds,
        "concurrent timing windows retain the maximum across snapshots");
    expect(
        combined.spike_count == kWriterSamples,
        "concurrent timing windows keep spike count matched to samples");
    expect(
        concurrent_window.take().count == 0u,
        "taking a timing window resets it atomically");

    std::cout << "performance timing logic tests passed\n";
    return EXIT_SUCCESS;
}
