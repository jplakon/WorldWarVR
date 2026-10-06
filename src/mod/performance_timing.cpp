#include "performance_timing.hpp"

#include "performance_timing_logic.hpp"
#include "present_hook.hpp"
#include "virtual_query_timing.hpp"

#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace wawvr::mod {
namespace {

constexpr ULONGLONG kReportIntervalMilliseconds = 10'000u;
constexpr std::size_t kPhaseCount = 22;

using FrameSample = PerformanceFrameSample<kPhaseCount>;
thread_local bool g_frame_collecting{};
thread_local std::uint64_t g_frame_sequence{};
thread_local FrameSample g_current_frame{};
thread_local FrameSample g_slowest_native_frame{};
thread_local FrameSample g_slowest_complete_frame{};

std::array<PerformanceTimingWindow, kPhaseCount> g_phase_aggregates{};
std::atomic<ULONGLONG> g_next_report_milliseconds{0};
std::atomic_flag g_enabled_log_gate = ATOMIC_FLAG_INIT;

struct QueryTimingAggregate final {
    PerformanceTimingAggregate timing{};
    std::uintptr_t slowest_address{};
    SIZE_T slowest_region_size{};
    DWORD slowest_protection{};
    DWORD slowest_state{};
    SIZE_T slowest_result{};
};

class QueryTimingWindow final {
public:
    void add(const std::uint64_t nanoseconds, LPCVOID address,
             const MEMORY_BASIC_INFORMATION* memory, const SIZE_T result,
             const SIZE_T supplied_size) noexcept {
        while (guard_.test_and_set(std::memory_order_acquire)) {}
        if (aggregate_.timing.count == 0 ||
            nanoseconds > aggregate_.timing.maximum_nanoseconds) {
            aggregate_.slowest_address = reinterpret_cast<std::uintptr_t>(address);
            aggregate_.slowest_result = result;
            const bool complete = memory != nullptr && result >= sizeof(*memory) &&
                                  supplied_size >= sizeof(*memory);
            aggregate_.slowest_region_size = complete ? memory->RegionSize : 0;
            aggregate_.slowest_protection = complete ? memory->Protect : 0;
            aggregate_.slowest_state = complete ? memory->State : 0;
        }
        add_performance_timing_sample(&aggregate_.timing, nanoseconds);
        guard_.clear(std::memory_order_release);
    }

    [[nodiscard]] QueryTimingAggregate take() noexcept {
        while (guard_.test_and_set(std::memory_order_acquire)) {}
        const auto result = aggregate_;
        aggregate_ = {};
        guard_.clear(std::memory_order_release);
        return result;
    }

private:
    std::atomic_flag guard_ = ATOMIC_FLAG_INIT;
    QueryTimingAggregate aggregate_{};
};

constexpr std::size_t kQueryGroupCount =
    static_cast<std::size_t>(VirtualQueryTimingGroup::count);
std::array<QueryTimingWindow, kQueryGroupCount> g_query_aggregates{};
constexpr std::array<const char*, kQueryGroupCount> kQueryGroupNames{
    "weapon-identity", "vehicle-input", "manual-reload", "weapon-hands",
    "presentation", "hud", "stereo-backend"};

[[nodiscard]] constexpr std::size_t phase_index(
    const PerformanceTimingPhase phase) noexcept {
    switch (phase) {
    case PerformanceTimingPhase::stereo_frontend_view:
        return 0;
    case PerformanceTimingPhase::stereo_frontend_batch:
        return 1;
    case PerformanceTimingPhase::stereo_backend_view:
        return 2;
    case PerformanceTimingPhase::stereo_backend_batch:
        return 3;
    case PerformanceTimingPhase::target_swapchain_present_path:
        return 4;
    case PerformanceTimingPhase::post_com_frame_xr_service:
        return 5;
    case PerformanceTimingPhase::com_frame_interval:
        return 6;
    case PerformanceTimingPhase::com_frame_outside_bridge_gap:
        return 7;
    case PerformanceTimingPhase::com_frame_original:
        return 8;
    case PerformanceTimingPhase::com_frame_bridge_total:
        return 9;
    case PerformanceTimingPhase::weapon_bridge_chest:
        return 10;
    case PerformanceTimingPhase::weapon_bridge_right:
        return 11;
    case PerformanceTimingPhase::weapon_bridge_left:
        return 12;
    case PerformanceTimingPhase::weapon_bridge_two_hand:
        return 13;
    case PerformanceTimingPhase::weapon_native_original:
        return 14;
    case PerformanceTimingPhase::weapon_post_update_custom:
        return 15;
    case PerformanceTimingPhase::weapon_post_held_prepare_grip:
        return 16;
    case PerformanceTimingPhase::weapon_post_held_pose_commit:
        return 17;
    case PerformanceTimingPhase::weapon_post_held_tracked_models:
        return 18;
    case PerformanceTimingPhase::weapon_post_held_muzzle_diagnostics:
        return 19;
    case PerformanceTimingPhase::weapon_post_held_scope:
        return 20;
    case PerformanceTimingPhase::weapon_post_held_manual_reload:
        return 21;
    }
    return kPhaseCount;
}

void report_if_ready() noexcept {
    const ULONGLONG now = GetTickCount64();
    ULONGLONG next =
        g_next_report_milliseconds.load(std::memory_order_relaxed);
    if (next == 0) {
        const ULONGLONG first_report = now + kReportIntervalMilliseconds;
        if (g_next_report_milliseconds.compare_exchange_strong(
                next, first_report, std::memory_order_relaxed,
                std::memory_order_relaxed) &&
            !g_enabled_log_gate.test_and_set(std::memory_order_relaxed)) {
            input_diagnostic_log(
                "Performance phase timing enabled: approximately 10-second aggregates; spike threshold >13.889 ms; no per-frame logging");
        }
        return;
    }
    if (now < next ||
        !g_next_report_milliseconds.compare_exchange_strong(
            next, now + kReportIntervalMilliseconds,
            std::memory_order_relaxed, std::memory_order_relaxed)) {
        return;
    }

    std::array<PerformanceTimingAggregate, kPhaseCount> samples{};
    for (std::size_t index = 0; index < samples.size(); ++index) {
        samples[index] = g_phase_aggregates[index].take();
    }
    const auto& frontend_view = samples[0];
    const auto& frontend_batch = samples[1];
    const auto& backend_view = samples[2];
    const auto& backend_batch = samples[3];
    const auto& present_path = samples[4];
    const auto& service = samples[5];
    const auto& frame_interval = samples[6];
    const auto& outside_bridge_gap = samples[7];
    const auto& original_frame = samples[8];
    const auto& bridge_total = samples[9];
    const auto& weapon_chest = samples[10];
    const auto& weapon_right = samples[11];
    const auto& weapon_left = samples[12];
    const auto& weapon_two_hand = samples[13];
    const auto& weapon_native = samples[14];
    const auto& weapon_post_custom = samples[15];
    const auto& weapon_prepare_grip = samples[16];
    const auto& weapon_pose_commit = samples[17];
    const auto& weapon_tracked_models = samples[18];
    const auto& weapon_muzzle_diagnostics = samples[19];
    const auto& weapon_scope = samples[20];
    const auto& weapon_manual_reload = samples[21];
    const auto report_correlated = [](const char* selected_by,
                                      const FrameSample& frame) noexcept {
        if (frame.sequence == 0) {
            return;
        }
        const auto ms = [&frame](std::size_t index) noexcept {
            return static_cast<double>(frame.nanoseconds[index]) / 1'000'000.0;
        };
        input_diagnostic_log(
            "Performance correlated frame (~10s, main-thread inclusive phases only): selected-by=%s sequence=%llu xrFrame=%llu sceneViews=%u native=%.3f ms frontend=%.3f ms backend=%.3f ms present=%.3f ms XR-service=%.3f ms full=%.3f ms weapon=%.3f ms; sceneViews=0 means no accepted stereo scene, not verified gameplay",
            selected_by, static_cast<unsigned long long>(frame.sequence),
            static_cast<unsigned long long>(frame.xr_frame_id), frame.scene_views,
            ms(8), ms(1), ms(3), ms(4), ms(5), ms(9),
            ms(10) + ms(11) + ms(12) + ms(13));
    };
    report_correlated("native", g_slowest_native_frame);
    report_correlated("full", g_slowest_complete_frame);
    g_slowest_native_frame = {};
    g_slowest_complete_frame = {};
    input_diagnostic_log(
        "Performance phase timing (~10s, spike >13.889 ms): frontend-view count=%llu avg/max=%.3f/%.3f ms spikes=%llu; frontend-completed-batch count=%llu avg/max=%.3f/%.3f ms spikes=%llu; backend-view count=%llu avg/max=%.3f/%.3f ms spikes=%llu; backend-completed-batch count=%llu avg/max=%.3f/%.3f ms spikes=%llu; target-swapchain-Present-path count=%llu avg/max=%.3f/%.3f ms spikes=%llu; post-Com_Frame-XR count=%llu avg/max=%.3f/%.3f ms spikes=%llu",
        static_cast<unsigned long long>(frontend_view.count),
        performance_timing_average_milliseconds(frontend_view),
        performance_timing_maximum_milliseconds(frontend_view),
        static_cast<unsigned long long>(frontend_view.spike_count),
        static_cast<unsigned long long>(frontend_batch.count),
        performance_timing_average_milliseconds(frontend_batch),
        performance_timing_maximum_milliseconds(frontend_batch),
        static_cast<unsigned long long>(frontend_batch.spike_count),
        static_cast<unsigned long long>(backend_view.count),
        performance_timing_average_milliseconds(backend_view),
        performance_timing_maximum_milliseconds(backend_view),
        static_cast<unsigned long long>(backend_view.spike_count),
        static_cast<unsigned long long>(backend_batch.count),
        performance_timing_average_milliseconds(backend_batch),
        performance_timing_maximum_milliseconds(backend_batch),
        static_cast<unsigned long long>(backend_batch.spike_count),
        static_cast<unsigned long long>(present_path.count),
        performance_timing_average_milliseconds(present_path),
        performance_timing_maximum_milliseconds(present_path),
        static_cast<unsigned long long>(present_path.spike_count),
        static_cast<unsigned long long>(service.count),
        performance_timing_average_milliseconds(service),
        performance_timing_maximum_milliseconds(service),
        static_cast<unsigned long long>(service.spike_count));
    input_diagnostic_log(
        "Performance cadence probe (~10s): Com_Frame interval count=%llu avg/max=%.3f/%.3f ms spikes=%llu; outside-bridge gap count=%llu avg/max=%.3f/%.3f ms spikes=%llu; native original count=%llu avg/max=%.3f/%.3f ms spikes=%llu; full bridge count=%llu avg/max=%.3f/%.3f ms spikes=%llu",
        static_cast<unsigned long long>(frame_interval.count),
        performance_timing_average_milliseconds(frame_interval),
        performance_timing_maximum_milliseconds(frame_interval),
        static_cast<unsigned long long>(frame_interval.spike_count),
        static_cast<unsigned long long>(outside_bridge_gap.count),
        performance_timing_average_milliseconds(outside_bridge_gap),
        performance_timing_maximum_milliseconds(outside_bridge_gap),
        static_cast<unsigned long long>(outside_bridge_gap.spike_count),
        static_cast<unsigned long long>(original_frame.count),
        performance_timing_average_milliseconds(original_frame),
        performance_timing_maximum_milliseconds(original_frame),
        static_cast<unsigned long long>(original_frame.spike_count),
        static_cast<unsigned long long>(bridge_total.count),
        performance_timing_average_milliseconds(bridge_total),
        performance_timing_maximum_milliseconds(bridge_total),
        static_cast<unsigned long long>(bridge_total.spike_count));
    input_diagnostic_log(
        "Performance weapon probe (~10s): bridge chest count=%llu avg/max=%.3f/%.3f ms spikes=%llu; right count=%llu avg/max=%.3f/%.3f ms spikes=%llu; left count=%llu avg/max=%.3f/%.3f ms spikes=%llu; two-hand count=%llu avg/max=%.3f/%.3f ms spikes=%llu; native-call count=%llu avg/max=%.3f/%.3f ms spikes=%llu; custom-post count=%llu avg/max=%.3f/%.3f ms spikes=%llu",
        static_cast<unsigned long long>(weapon_chest.count),
        performance_timing_average_milliseconds(weapon_chest),
        performance_timing_maximum_milliseconds(weapon_chest),
        static_cast<unsigned long long>(weapon_chest.spike_count),
        static_cast<unsigned long long>(weapon_right.count),
        performance_timing_average_milliseconds(weapon_right),
        performance_timing_maximum_milliseconds(weapon_right),
        static_cast<unsigned long long>(weapon_right.spike_count),
        static_cast<unsigned long long>(weapon_left.count),
        performance_timing_average_milliseconds(weapon_left),
        performance_timing_maximum_milliseconds(weapon_left),
        static_cast<unsigned long long>(weapon_left.spike_count),
        static_cast<unsigned long long>(weapon_two_hand.count),
        performance_timing_average_milliseconds(weapon_two_hand),
        performance_timing_maximum_milliseconds(weapon_two_hand),
        static_cast<unsigned long long>(weapon_two_hand.spike_count),
        static_cast<unsigned long long>(weapon_native.count),
        performance_timing_average_milliseconds(weapon_native),
        performance_timing_maximum_milliseconds(weapon_native),
        static_cast<unsigned long long>(weapon_native.spike_count),
        static_cast<unsigned long long>(weapon_post_custom.count),
        performance_timing_average_milliseconds(weapon_post_custom),
        performance_timing_maximum_milliseconds(weapon_post_custom),
        static_cast<unsigned long long>(weapon_post_custom.spike_count));
    input_diagnostic_log(
        "Performance held-post breakdown (~10s, complete held calls): prepare/grip count=%llu avg/max=%.3f/%.3f ms spikes=%llu; pose/commit count=%llu avg/max=%.3f/%.3f ms spikes=%llu; hands+grenade count=%llu avg/max=%.3f/%.3f ms spikes=%llu; muzzle/aim-diag count=%llu avg/max=%.3f/%.3f ms spikes=%llu; scope count=%llu avg/max=%.3f/%.3f ms spikes=%llu; manual-reload count=%llu avg/max=%.3f/%.3f ms spikes=%llu",
        static_cast<unsigned long long>(weapon_prepare_grip.count),
        performance_timing_average_milliseconds(weapon_prepare_grip),
        performance_timing_maximum_milliseconds(weapon_prepare_grip),
        static_cast<unsigned long long>(weapon_prepare_grip.spike_count),
        static_cast<unsigned long long>(weapon_pose_commit.count),
        performance_timing_average_milliseconds(weapon_pose_commit),
        performance_timing_maximum_milliseconds(weapon_pose_commit),
        static_cast<unsigned long long>(weapon_pose_commit.spike_count),
        static_cast<unsigned long long>(weapon_tracked_models.count),
        performance_timing_average_milliseconds(weapon_tracked_models),
        performance_timing_maximum_milliseconds(weapon_tracked_models),
        static_cast<unsigned long long>(weapon_tracked_models.spike_count),
        static_cast<unsigned long long>(weapon_muzzle_diagnostics.count),
        performance_timing_average_milliseconds(weapon_muzzle_diagnostics),
        performance_timing_maximum_milliseconds(weapon_muzzle_diagnostics),
        static_cast<unsigned long long>(weapon_muzzle_diagnostics.spike_count),
        static_cast<unsigned long long>(weapon_scope.count),
        performance_timing_average_milliseconds(weapon_scope),
        performance_timing_maximum_milliseconds(weapon_scope),
        static_cast<unsigned long long>(weapon_scope.spike_count),
        static_cast<unsigned long long>(weapon_manual_reload.count),
        performance_timing_average_milliseconds(weapon_manual_reload),
        performance_timing_maximum_milliseconds(weapon_manual_reload),
        static_cast<unsigned long long>(weapon_manual_reload.spike_count));
    for (std::size_t index = 0; index < g_query_aggregates.size(); ++index) {
        const auto query = g_query_aggregates[index].take();
        const auto& timing = query.timing;
        if (timing.count == 0) continue;
        const double frames = static_cast<double>(bridge_total.count);
        input_diagnostic_log(
            "Performance VirtualQuery (~10s, instrumented sites only): group=%s count=%llu frames=%llu calls/frame=%.2f total=%.3f ms avg/max=%.3f/%.3f us cost/frame=%.3f ms slowest-address=%p region=%llu protect=0x%08lx state=0x%08lx result=%llu",
            kQueryGroupNames[index], static_cast<unsigned long long>(timing.count),
            static_cast<unsigned long long>(bridge_total.count),
            frames != 0.0 ? static_cast<double>(timing.count) / frames : 0.0,
            static_cast<double>(timing.total_nanoseconds) / 1'000'000.0,
            performance_timing_average_milliseconds(timing) * 1000.0,
            performance_timing_maximum_milliseconds(timing) * 1000.0,
            frames != 0.0 ? static_cast<double>(timing.total_nanoseconds) /
                               frames / 1'000'000.0 : 0.0,
            reinterpret_cast<const void*>(query.slowest_address),
            static_cast<unsigned long long>(query.slowest_region_size),
            static_cast<unsigned long>(query.slowest_protection),
            static_cast<unsigned long>(query.slowest_state),
            static_cast<unsigned long long>(query.slowest_result));
    }
}

} // namespace

SIZE_T timed_virtual_query(const VirtualQueryTimingGroup group, LPCVOID address,
                          PMEMORY_BASIC_INFORMATION memory, SIZE_T size) noexcept {
    static const bool enabled = []() noexcept {
        const DWORD saved_error = GetLastError();
        const bool value = performance_timing_diagnostics_enabled();
        SetLastError(saved_error);
        return value;
    }();
    const auto index = static_cast<std::size_t>(group);
    if (!enabled || index >= g_query_aggregates.size()) {
        return VirtualQuery(address, memory, size);
    }
    const DWORD entry_error = GetLastError();
    const auto started = std::chrono::steady_clock::now();
    SetLastError(entry_error);
    const SIZE_T result = VirtualQuery(address, memory, size);
    const DWORD result_error = GetLastError();
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count();
    if (elapsed >= 0) {
        g_query_aggregates[index].add(static_cast<std::uint64_t>(elapsed),
                                     address, memory, result, size);
    }
    SetLastError(result_error);
    return result;
}

bool performance_timing_diagnostics_enabled() noexcept {
    static const bool enabled = []() noexcept {
        wchar_t value[2]{};
        return GetEnvironmentVariableW(
                   L"WAWVR_FRAME_TIMING_DIAGNOSTICS", value, 2) == 1 &&
               value[0] == L'1';
    }();
    return enabled;
}

void begin_performance_frame() noexcept {
    if (!performance_timing_diagnostics_enabled()) {
        return;
    }
    g_current_frame = {};
    g_current_frame.sequence = ++g_frame_sequence;
    g_frame_collecting = true;
}

void mark_performance_frame_scene(const std::uint64_t xr_frame_id,
                                  const std::uint32_t views) noexcept {
    if (performance_timing_diagnostics_enabled() && g_frame_collecting) {
        g_current_frame.xr_frame_id = xr_frame_id;
        g_current_frame.scene_views = views;
    }
}

void finish_performance_frame() noexcept {
    if (!performance_timing_diagnostics_enabled() || !g_frame_collecting) {
        return;
    }
    g_frame_collecting = false;
    retain_slowest_performance_frame(&g_slowest_native_frame, g_current_frame, 8);
    retain_slowest_performance_frame(&g_slowest_complete_frame, g_current_frame, 9);
}

void record_performance_timing(
    const PerformanceTimingPhase phase,
    const std::chrono::steady_clock::duration duration) noexcept {
    const std::size_t index = phase_index(phase);
    if (index >= g_phase_aggregates.size()) {
        return;
    }
    const auto signed_nanoseconds =
        std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    if (signed_nanoseconds < 0) {
        return;
    }
    const std::uint64_t nanoseconds = static_cast<std::uint64_t>(
        signed_nanoseconds);
    g_phase_aggregates[index].add_sample(nanoseconds);
    if (g_frame_collecting) {
        g_current_frame.nanoseconds[index] += nanoseconds;
    }
}

void report_performance_timing_if_ready() noexcept {
    if (performance_timing_diagnostics_enabled()) {
        report_if_ready();
    }
}

ScopedPerformanceTiming::ScopedPerformanceTiming(
    const PerformanceTimingPhase phase,
    const bool active) noexcept
    : phase_(phase),
      enabled_(active && performance_timing_diagnostics_enabled()) {
    if (enabled_) {
        started_ = std::chrono::steady_clock::now();
    }
}

ScopedPerformanceTiming::~ScopedPerformanceTiming() noexcept {
    if (enabled_) {
        record_performance_timing(
            phase_, std::chrono::steady_clock::now() - started_);
    }
}

} // namespace wawvr::mod
