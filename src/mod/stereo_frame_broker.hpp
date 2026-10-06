#pragma once

#include "xr_types.h"

#include <cstdint>

namespace wawvr::mod {

struct PendingStereoFrame final {
    wawvr::xr::FrameState frame{};
    wawvr::xr::Posef tracking_anchor{};
};

// Present owns xrWaitFrame/xrBeginFrame and publishes the resulting predicted
// view here. The gameplay call-site hook copies the publication before it
// calls any engine code, so no lock is held while T4 renders.
void publish_pending_stereo_frame(
    const wawvr::xr::FrameState& frame,
    const wawvr::xr::Posef& tracking_anchor);
void clear_pending_stereo_frame() noexcept;

// Non-consuming boundary-scoped readiness query. PrimeNextFrame publishes a
// valid frame after one Com_Frame and keeps it valid through the following
// Com_Frame's scene/HUD/Present work. The caller supplies the last frame the
// scene hook consumed so a stale publication retained across Reset/recovery
// cannot make HUD placement claim stereo is available. No frame data is
// copied or invalidated.
[[nodiscard]] bool pending_stereo_frame_ready_after(
    std::uint64_t consumed_frame_id) noexcept;

[[nodiscard]] bool try_acquire_pending_stereo_frame(
    std::uint64_t after_frame_id,
    PendingStereoFrame* output);

// Rebases a not-yet-staged eye pair by the same yaw transferred into T4's
// player body. Exact frame/orientation ownership prevents an input sample from
// mutating a newer prediction or an eye pair already handed to the backend.
[[nodiscard]] bool try_rebase_pending_stereo_tracking_anchor(
    std::uint64_t expected_frame_id,
    const wawvr::xr::Quaternionf& expected_orientation,
    const wawvr::xr::Quaternionf& desired_orientation) noexcept;

// The scene hook stages a frame only after both frontend R_RenderScene calls
// returned. The backend hook consumes that staged publication and marks it
// rendered only after both GfxViewInfo records were actually drawn. Present
// therefore cannot mistake two enqueued views for a completed SBS frame.
void stage_stereo_frame_for_backend(
    std::uint64_t frame_id,
    const wawvr::xr::StereoSourceLayout& layout) noexcept;
[[nodiscard]] bool try_get_staged_stereo_frame(
    std::uint64_t* frame_id,
    wawvr::xr::StereoSourceLayout* layout) noexcept;

void mark_stereo_frame_rendered(
    std::uint64_t frame_id,
    const wawvr::xr::StereoSourceLayout& layout) noexcept;
[[nodiscard]] bool try_get_rendered_stereo_layout(
    std::uint64_t frame_id,
    wawvr::xr::StereoSourceLayout* layout) noexcept;

} // namespace wawvr::mod
