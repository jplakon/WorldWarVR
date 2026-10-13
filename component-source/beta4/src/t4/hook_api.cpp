#include "hook_api.hpp"

#include <algorithm>

namespace wawvr::t4 {

HookPreparationResult prepare_inline_hook(const ValidatedBindings& bindings,
                                          const HookSiteId site_id,
                                          const std::uintptr_t detour) {
    if (detour == 0) {
        return {
            .hook = std::nullopt,
            .error = HookPrepareError::null_detour,
            .message = "detour address is null",
        };
    }

    const auto* site = bindings.site(site_id);
    if (site == nullptr) {
        return {
            .hook = std::nullopt,
            .error = HookPrepareError::site_not_in_profile,
            .message = "hook site has not been independently located and verified for T4",
        };
    }
    if (site->use != SiteUse::detour_candidate) {
        return {
            .hook = std::nullopt,
            .error = HookPrepareError::site_is_validation_only,
            .message = "the selected site is validation-only, not an approved patch site",
        };
    }

    const auto target = bindings.site_address(site_id);
    if (!target) {
        return {
            .hook = std::nullopt,
            .error = HookPrepareError::target_unmapped,
            .message = "hook target is outside the mapped executable image",
        };
    }
    if (!bindings.site_bytes_still_match(site_id)) {
        return {
            .hook = std::nullopt,
            .error = HookPrepareError::target_bytes_changed,
            .message = "hook target bytes changed after executable validation",
        };
    }

    PreparedInlineHook prepared{
        .id = site_id,
        .target = *target,
        .detour = detour,
        .minimum_patch_bytes = site->minimum_patch_bytes,
        .expected_size = site->expected_size,
    };
    std::ranges::copy(site->expected_bytes(), prepared.expected.begin());
    return {
        .hook = prepared,
        .error = HookPrepareError::none,
        .message = {},
    };
}

}  // namespace wawvr::t4
