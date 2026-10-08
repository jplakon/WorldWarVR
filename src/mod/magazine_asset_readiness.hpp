// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace wawvr::mod {

enum class MagazineAssetReadiness {
    unavailable,
    ready,
    recovered,
};

// A native DObj creation attempt may own engine allocations even when its
// immediate postcondition is unreadable. Keep that node and all its geometry
// alive. Recovery only validates the existing object; it never creates another
// DObj, overwrites the node, or releases native ownership.
template <class VerifyExistingObject>
[[nodiscard]] MagazineAssetReadiness refresh_magazine_asset_readiness(
    const bool creation_attempted,
    bool& ready,
    bool& failed,
    VerifyExistingObject&& verify_existing_object) noexcept {
    if (ready) {
        return MagazineAssetReadiness::ready;
    }
    if (!creation_attempted || !failed || !verify_existing_object()) {
        return MagazineAssetReadiness::unavailable;
    }
    ready = true;
    failed = false;
    return MagazineAssetReadiness::recovered;
}

}  // namespace wawvr::mod
