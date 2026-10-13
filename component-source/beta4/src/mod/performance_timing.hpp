#pragma once

#include <chrono>
#include <cstdint>

namespace wawvr::mod {

enum class PerformanceTimingPhase : std::uint8_t {
    stereo_frontend_view,
    stereo_frontend_batch,
    stereo_backend_view,
    stereo_backend_batch,
    target_swapchain_present_path,
    post_com_frame_xr_service,
    com_frame_interval,
    com_frame_outside_bridge_gap,
    com_frame_original,
    com_frame_bridge_total,
    weapon_bridge_chest,
    weapon_bridge_right,
    weapon_bridge_left,
    weapon_bridge_two_hand,
    weapon_native_original,
    weapon_post_update_custom,
    weapon_post_held_prepare_grip,
    weapon_post_held_pose_commit,
    weapon_post_held_tracked_models,
    weapon_post_held_muzzle_diagnostics,
    weapon_post_held_scope,
    weapon_post_held_manual_reload,
};

// Reads WAWVR_FRAME_TIMING_DIAGNOSTICS once. The disabled path performs no
// clock sampling and emits no timing logs.
[[nodiscard]] bool performance_timing_diagnostics_enabled() noexcept;

// Correlates phases from the same outer main-thread frame, without retaining
// native pointers or adding game-state reads. All calls are disabled by default.
void begin_performance_frame() noexcept;
void mark_performance_frame_scene(std::uint64_t xr_frame_id,
                                  std::uint32_t views) noexcept;
void finish_performance_frame() noexcept;

void record_performance_timing(
    PerformanceTimingPhase phase,
    std::chrono::steady_clock::duration duration) noexcept;

// The validated outer Com_Frame bridge calls this after every enabled timing
// phase for that frame has been recorded, keeping each aggregate window
// coherent.
void report_performance_timing_if_ready() noexcept;

class ScopedPerformanceTiming final {
public:
    explicit ScopedPerformanceTiming(
        PerformanceTimingPhase phase,
        bool active = true) noexcept;
    ~ScopedPerformanceTiming() noexcept;

    ScopedPerformanceTiming(const ScopedPerformanceTiming&) = delete;
    ScopedPerformanceTiming& operator=(const ScopedPerformanceTiming&) =
        delete;

private:
    PerformanceTimingPhase phase_{};
    bool enabled_{};
    std::chrono::steady_clock::time_point started_{};
};

} // namespace wawvr::mod
