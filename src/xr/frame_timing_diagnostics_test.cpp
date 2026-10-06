// SPDX-License-Identifier: GPL-3.0-only
#include "frame_timing_diagnostics.hpp"
#include <cmath>
#include <cstdio>

namespace {
int failures = 0;
void check(bool condition, const char* label) {
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label); }
}
}

int main() {
    using namespace wawvr::xr;
    check(FrameTimingHistogram::bucket(0) == 0, "zero bucket");
    check(FrameTimingHistogram::bucket(250'000) == 1, "quarter ms inclusive");
    check(FrameTimingHistogram::bucket(250'001) == 2, "quarter ms next");
    check(FrameTimingHistogram::bucket(16'000'000) == 64, "16ms edge");
    check(FrameTimingHistogram::bucket(16'000'001) == 65, "16ms next");
    check(FrameTimingHistogram::bucket(80'000'000) == 128, "80ms edge");
    check(FrameTimingHistogram::bucket(80'000'001) == 129, "80ms next");
    check(FrameTimingHistogram::bucket(336'000'000) == 192, "336ms edge");
    check(FrameTimingHistogram::bucket(336'000'001) == 193, "overflow");
    FrameTimingHistogram histogram{};
    check(histogram.percentile_upper_ms(95) == 0, "empty histogram");
    for (unsigned i = 0; i < 95; ++i) histogram.add(1'000'000);
    for (unsigned i = 0; i < 4; ++i) histogram.add(20'000'000);
    histogram.add(95'000'000);
    check(histogram.count == 100 && histogram.max_ns == 95'000'000, "count and exact maximum");
    check(histogram.percentile_upper_ms(95) == 1.0, "p95 nearest rank");
    check(histogram.percentile_upper_ms(99) == 20.0, "p99 nearest rank");
    check(histogram.percentile_upper_ms(100) == 96.0, "p100 upper bucket edge");
    FrameTimingHistogram overflow{};
    overflow.add(400'000'000);
    check(std::isinf(overflow.percentile_upper_ms(95)), "overflow does not understate quantile");

    FrameTimingSample sample{};
    check(!sample.add(0, FrameTimingStage::application, 1), "inactive frame rejected");
    sample.frame_id = 123;
    sample.source_frame_id = 122;
    sample.source_serial = 500;
    sample.end_succeeded = true;
    check(!sample.add(122, FrameTimingStage::application, 10), "wrong-frame stage rejected");
    check(!sample.add(123, FrameTimingStage::count, 10), "invalid stage rejected");
    check(sample.add(123, FrameTimingStage::application, 10'000'000), "exact-frame stage accepted");
    sample.add(123, FrameTimingStage::application, 2'000'000);
    sample.add(123, FrameTimingStage::end_frame, 90'000'000);
    check(sample.value(FrameTimingStage::application) == 12'000'000, "same stage sums within frame");
    FrameLayerTimingWindow window{};
    window.add(sample);
    FrameTimingSample native_spike = sample;
    native_spike.frame_id = 124;
    native_spike.stage_ns[static_cast<std::size_t>(FrameTimingStage::application)] = 100'000'000;
    native_spike.stage_ns[static_cast<std::size_t>(FrameTimingStage::end_frame)] = 1'000'000;
    window.add(native_spike);
    check(window.unknown_presentation_frames == 2, "unknown presentation counted");
    FrameTimingSample paused = sample;
    paused.frame_id = 125;
    paused.kind = CompositionLayerKind::projection;
    paused.presentation_valid = true;
    paused.observed_stereo = false;
    paused.reused_layer = true;
    window.add(paused);
    check(window.observed_mono_frames == 1 && window.reused_layers == 1,
          "projection fallback is not automatically classified as gameplay");
    check(window.worst[1][0].frame_id == 124, "app rank selects native-spike frame");
    check(window.worst[2][0].frame_id == 123, "end rank selects runtime-spike frame");
    check(window.worst[2][0].value(FrameTimingStage::application) == 12'000'000,
          "worst event retains SAME-frame application timing");
    check(window.worst[2][0].source_frame_id == 122 && window.worst[2][0].source_serial == 500,
          "worst event preserves capture provenance");
    check(window.stages[static_cast<std::size_t>(FrameTimingStage::wait_left)].count == 0,
          "absent stage distinguished from measured zero");
    check(frame_layer_timing_index(CompositionLayerKind::projection) == 0 &&
          frame_layer_timing_index(CompositionLayerKind::quad) == 1 &&
          frame_layer_timing_index(CompositionLayerKind::none) == 2, "layer classification");
    window = {};
    check(window.frames == 0 && window.worst[2][0].frame_id == 0, "window reset drops old events");
    std::printf("XR timing diagnostic tests: %s\n", failures == 0 ? "passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
