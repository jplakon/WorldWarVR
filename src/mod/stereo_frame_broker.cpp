#include "stereo_frame_broker.hpp"

#include "stereo_diagnostics.hpp"
#include "tracking_anchor_sync.hpp"

#include <atomic>
#include <cstring>
#include <mutex>

namespace wawvr::mod {
namespace {

std::mutex g_publication_mutex;
PendingStereoFrame g_publication{};
bool g_publication_valid = false;
wawvr::xr::StereoSourceLayout g_rendered_layout{};
std::uint64_t g_staged_frame_id = 0;
wawvr::xr::StereoSourceLayout g_staged_layout{};
std::atomic<std::uint64_t> g_rendered_frame_id{0};

} // namespace

void publish_pending_stereo_frame(
    const wawvr::xr::FrameState& frame,
    const wawvr::xr::Posef& tracking_anchor) {
    TrackingAnchorSyncLock sync(tracking_anchor_sync_mutex());
    std::lock_guard<std::mutex> lock(g_publication_mutex);
    g_publication.frame = frame;
    g_publication.tracking_anchor = tracking_anchor;
    g_publication_valid = frame.frame_id != 0 && frame.should_render &&
                          frame.views_valid;
    if (!g_publication_valid) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag broker.publish rejected: frame=%llu shouldRender=%u viewsValid=%u",
            static_cast<unsigned long long>(frame.frame_id),
            frame.should_render ? 1u : 0u, frame.views_valid ? 1u : 0u);
    }
}

void clear_pending_stereo_frame() noexcept {
    try {
        TrackingAnchorSyncLock sync(tracking_anchor_sync_mutex());
        std::lock_guard<std::mutex> lock(g_publication_mutex);
        g_publication = {};
        g_publication_valid = false;
        g_staged_frame_id = 0;
        g_staged_layout = {};
        g_rendered_layout = {};
    } catch (...) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag broker.clear rejected: publication mutex/assignment exception");
        // Teardown must not make the host process fail. A stale publication is
        // still rejected after its frame id has been consumed.
    }
    g_rendered_frame_id.store(0, std::memory_order_release);
}

bool pending_stereo_frame_ready_after(
    const std::uint64_t consumed_frame_id) noexcept {
    try {
        TrackingAnchorSyncLock sync(tracking_anchor_sync_mutex());
        std::lock_guard<std::mutex> lock(g_publication_mutex);
        return g_publication_valid && g_publication.frame.frame_id != 0 &&
               g_publication.frame.frame_id != consumed_frame_id &&
               g_publication.frame.should_render &&
               g_publication.frame.views_valid;
    } catch (...) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag broker.ready-after rejected: publication mutex exception consumedFrame=%llu",
            static_cast<unsigned long long>(consumed_frame_id));
        return false;
    }
}

bool try_acquire_pending_stereo_frame(
    const std::uint64_t after_frame_id,
    PendingStereoFrame* const output) {
    if (output == nullptr) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag broker.acquire rejected: output=null afterFrame=%llu",
            static_cast<unsigned long long>(after_frame_id));
        return false;
    }
    TrackingAnchorSyncLock sync(tracking_anchor_sync_mutex());
    std::lock_guard<std::mutex> lock(g_publication_mutex);
    if (!g_publication_valid) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag broker.acquire rejected: publicationValid=0 publishedFrame=%llu afterFrame=%llu shouldRender=%u viewsValid=%u",
            static_cast<unsigned long long>(g_publication.frame.frame_id),
            static_cast<unsigned long long>(after_frame_id),
            g_publication.frame.should_render ? 1u : 0u,
            g_publication.frame.views_valid ? 1u : 0u);
        return false;
    }
    if (g_publication.frame.frame_id == after_frame_id) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag broker.acquire rejected: already-consumed frame=%llu",
            static_cast<unsigned long long>(after_frame_id));
        return false;
    }
    *output = g_publication;
    return true;
}

bool try_rebase_pending_stereo_tracking_anchor(
    const std::uint64_t expected_frame_id,
    const wawvr::xr::Quaternionf& expected_orientation,
    const wawvr::xr::Quaternionf& desired_orientation) noexcept {
    if (expected_frame_id == 0) {
        return false;
    }
    try {
        TrackingAnchorSyncLock sync(tracking_anchor_sync_mutex());
        std::lock_guard<std::mutex> lock(g_publication_mutex);
        if (!g_publication_valid ||
            g_publication.frame.frame_id != expected_frame_id ||
            g_staged_frame_id == expected_frame_id ||
            std::memcmp(
                &g_publication.tracking_anchor.orientation,
                &expected_orientation, sizeof(expected_orientation)) != 0) {
            return false;
        }
        g_publication.tracking_anchor.orientation = desired_orientation;
        return true;
    } catch (...) {
        return false;
    }
}

void stage_stereo_frame_for_backend(
    const std::uint64_t frame_id,
    const wawvr::xr::StereoSourceLayout& layout) noexcept {
    if (frame_id == 0) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag broker.stage rejected: frame=0");
        return;
    }
    try {
        TrackingAnchorSyncLock sync(tracking_anchor_sync_mutex());
        std::lock_guard<std::mutex> lock(g_publication_mutex);
        g_staged_frame_id = frame_id;
        g_staged_layout = layout;
        // A newly generated frontend frame is not compositor-ready until the
        // exact backend wrapper has returned from both eye draws.
        g_rendered_frame_id.store(0, std::memory_order_release);
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag broker.stage accepted: frame=%llu",
            static_cast<unsigned long long>(frame_id));
    } catch (...) {
        g_rendered_frame_id.store(0, std::memory_order_release);
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag broker.stage rejected: frame=%llu mutex/assignment exception",
            static_cast<unsigned long long>(frame_id));
    }
}

bool try_get_staged_stereo_frame(
    std::uint64_t* const frame_id,
    wawvr::xr::StereoSourceLayout* const layout) noexcept {
    if (frame_id == nullptr || layout == nullptr) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag broker.staged-get rejected: frameOut=%p layoutOut=%p",
            frame_id, layout);
        return false;
    }
    try {
        TrackingAnchorSyncLock sync(tracking_anchor_sync_mutex());
        std::lock_guard<std::mutex> lock(g_publication_mutex);
        if (g_staged_frame_id == 0) {
            WAWVR_STEREO_DIAG_ONCE(
                "StereoDiag broker.staged-get rejected: stagedFrame=0 renderedFrame=%llu",
                static_cast<unsigned long long>(
                    g_rendered_frame_id.load(std::memory_order_acquire)));
            return false;
        }
        *frame_id = g_staged_frame_id;
        *layout = g_staged_layout;
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag broker.staged-get accepted: stagedFrame=%llu",
            static_cast<unsigned long long>(g_staged_frame_id));
        return true;
    } catch (...) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag broker.staged-get rejected: mutex/copy exception");
        return false;
    }
}

void mark_stereo_frame_rendered(
    const std::uint64_t frame_id,
    const wawvr::xr::StereoSourceLayout& layout) noexcept {
    if (frame_id == 0) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag broker.mark rejected: drawnFrame=0");
        return;
    }
    try {
        TrackingAnchorSyncLock sync(tracking_anchor_sync_mutex());
        std::lock_guard<std::mutex> lock(g_publication_mutex);
        if (g_staged_frame_id != frame_id) {
            g_rendered_frame_id.store(0, std::memory_order_release);
            WAWVR_STEREO_DIAG_ONCE(
                "StereoDiag broker.mark rejected: stagedFrame=%llu drawnFrame=%llu",
                static_cast<unsigned long long>(g_staged_frame_id),
                static_cast<unsigned long long>(frame_id));
            return;
        }
        g_rendered_layout = layout;
        g_rendered_frame_id.store(frame_id, std::memory_order_release);
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag broker.mark accepted: stagedFrame=%llu renderedFrame=%llu",
            static_cast<unsigned long long>(g_staged_frame_id),
            static_cast<unsigned long long>(frame_id));
    } catch (...) {
        g_rendered_frame_id.store(0, std::memory_order_release);
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag broker.mark rejected: drawnFrame=%llu mutex/copy exception",
            static_cast<unsigned long long>(frame_id));
    }
}

bool try_get_rendered_stereo_layout(
    const std::uint64_t frame_id,
    wawvr::xr::StereoSourceLayout* const layout) noexcept {
    if (layout == nullptr) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag broker.rendered-get rejected: layoutOut=null requestedFrame=%llu",
            static_cast<unsigned long long>(frame_id));
        return false;
    }
    if (frame_id == 0) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag broker.rendered-get rejected: requestedFrame=0");
        return false;
    }
    const std::uint64_t published =
        g_rendered_frame_id.load(std::memory_order_acquire);
    if (published != frame_id) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag broker.rendered-get rejected: requestedFrame=%llu renderedFrame=%llu",
            static_cast<unsigned long long>(frame_id),
            static_cast<unsigned long long>(published));
        return false;
    }
    try {
        TrackingAnchorSyncLock sync(tracking_anchor_sync_mutex());
        std::lock_guard<std::mutex> lock(g_publication_mutex);
        if (g_rendered_frame_id.load(std::memory_order_relaxed) != frame_id) {
            WAWVR_STEREO_DIAG_ONCE(
                "StereoDiag broker.rendered-get rejected after lock: requestedFrame=%llu renderedFrame=%llu",
                static_cast<unsigned long long>(frame_id),
                static_cast<unsigned long long>(
                    g_rendered_frame_id.load(std::memory_order_relaxed)));
            return false;
        }
        *layout = g_rendered_layout;
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag broker.rendered-get accepted: frame=%llu",
            static_cast<unsigned long long>(frame_id));
        return true;
    } catch (...) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag broker.rendered-get rejected: requestedFrame=%llu mutex/copy exception",
            static_cast<unsigned long long>(frame_id));
        return false;
    }
}

} // namespace wawvr::mod
