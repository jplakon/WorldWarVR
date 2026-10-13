// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <mutex>

namespace wawvr::mod {

// Serializes the two frame publications with the native body-yaw transfer.
// It is recursive because the high-level transaction deliberately calls the
// individually guarded controller/stereo broker operations while holding the
// same process-local lock.
[[nodiscard]] inline std::recursive_mutex& tracking_anchor_sync_mutex()
    noexcept {
    static std::recursive_mutex mutex;
    return mutex;
}

using TrackingAnchorSyncLock = std::lock_guard<std::recursive_mutex>;

}  // namespace wawvr::mod
