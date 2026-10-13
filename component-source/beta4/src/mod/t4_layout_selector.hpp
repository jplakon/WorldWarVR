#pragma once

#include "t4/profile.hpp"

#include <cstdint>

namespace wawvr::mod {

// Mod-private layout family selected only after t4::ValidatedBindings has
// authenticated an exact executable identity and its mapped sentinels. This
// indirection lets independently profiled distributions share verified engine
// addresses without teaching every hook about file hashes.
enum class T4LayoutFamily : std::uint8_t {
    unsupported,
    single_player_1_7_1263,
    multiplayer_1_7_1263,
};

[[nodiscard]] constexpr T4LayoutFamily t4_layout_family_from_id(
    const wawvr::t4::ExecutableLayoutId layout) noexcept {
    switch (layout) {
    case wawvr::t4::ExecutableLayoutId::t4_sp_1_7_1263:
        return T4LayoutFamily::single_player_1_7_1263;
    case wawvr::t4::ExecutableLayoutId::t4_mp_1_7_1263:
        return T4LayoutFamily::multiplayer_1_7_1263;
    }
    return T4LayoutFamily::unsupported;
}

// A malformed profile must not be able to combine one layout's addresses with
// the other executable variant, even after its exact identity was selected.
[[nodiscard]] constexpr T4LayoutFamily select_t4_layout_family(
    const wawvr::t4::ExecutableLayoutId layout,
    const wawvr::t4::ExecutableVariant variant) noexcept {
    const auto family = t4_layout_family_from_id(layout);
    if (family == T4LayoutFamily::single_player_1_7_1263 &&
        variant == wawvr::t4::ExecutableVariant::single_player) {
        return family;
    }
    if (family == T4LayoutFamily::multiplayer_1_7_1263 &&
        variant == wawvr::t4::ExecutableVariant::multiplayer) {
        return family;
    }
    return T4LayoutFamily::unsupported;
}

[[nodiscard]] constexpr T4LayoutFamily select_t4_layout_family(
    const wawvr::t4::ExecutableProfile& profile) noexcept {
    return select_t4_layout_family(profile.layout, profile.variant);
}

} // namespace wawvr::mod
