// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <string_view>

namespace wawvr::mod {

struct VrLoadingTipText final {
    std::string_view key;
    std::string_view original_english;
    std::string_view vr_text;
};

// Exact authored keys AND exact shipped English values. These strings have
// process lifetime; no mutable TLS, localization storage or caller asset is
// modified. A native display/provenance adapter must separately admit the call.
inline constexpr std::array<VrLoadingTipText,4> kVrLoadingTipText{{
    {"PLATFORM_DYK_MSG33",
     "Press the V key (default) to perform a knife melee attack.",
     "Use deliberate physical hand attacks for melee."},
    {"PLATFORM_DYK_MSG34",
     "Pressing the shift key (default) allows you to sprint, however you cannot use your weapons while sprinting.",
     "Move forward and click the left stick to sprint when stamina and stance allow."},
    {"PLATFORM_DYK_MSG35",
     "When zoomed in with a scoped weapon, holding down the shift key (default) allows you to hold your breath, and steady your aim.",
     "Scoped hold-breath has no assigned VR control."},
    {"PLATFORM_DYK_MSG36",
     "If a frag grenade lands near you look for the throwback indicator. This will allow you to send the enemy grenade back using the G key or middle mouse wheel.",
     "The contextual VR throwback shortcut is disabled. Avoid live enemy grenades."},
}};

// An empty view means retain the original native result pointer exactly. This
// is not general translation and deliberately preserves German/custom values.
[[nodiscard]] constexpr std::string_view resolve_vr_loading_tip_text(
    const std::string_view exact_key,const std::string_view native_text,
    const bool right_stick_melee=false) noexcept {
    for (std::size_t index=0;index<kVrLoadingTipText.size();++index) {
        const auto& tip=kVrLoadingTipText[index];
        if (exact_key==tip.key && native_text==tip.original_english) {
            if (index==0 && right_stick_melee)
                return "Click the right stick or use a deliberate physical hand attack for melee.";
            return tip.vr_text;
        }
    }
    return {};
}

} // namespace wawvr::mod
