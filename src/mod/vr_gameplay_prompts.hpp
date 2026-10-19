// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "../xr/xr_types.h"

namespace wawvr::mod {

enum class VrReloadPromptStage : std::uint8_t {
    none, begin, ammunition, insert, chamber, ready,
};

enum class VrReloadPromptFamily : std::uint8_t {
    native, stripper_clip, detachable_magazine, en_bloc,
};

enum class VrGameplayPromptKind : std::uint8_t {
    none, introduction, controls, reload, support_selector, support_aim,
    enemy_grenade,
};

// Semantic action labels deliberately name the bound OpenXR action rather
// than assuming that every controller has Touch's A/X/Y lettering. These
// labels describe the real primary/secondary actions on Touch, Index and WMR.
inline constexpr std::size_t kVrPromptLineBytes = 96;
struct VrGameplayPromptInput final {
    bool active_stereo_gameplay{};
    bool input_focused{};
    bool introduction_visible{};
    bool help_held{};
    bool automatic_reload{};
    bool button_grenades{};
    bool right_stick_melee{};
    bool multiplayer{};
    bool support_weapon_selected{};
    bool enemy_grenade_available{};
    VrReloadPromptFamily reload_family{VrReloadPromptFamily::native};
    VrReloadPromptStage reload_stage{VrReloadPromptStage::none};
    wawvr::xr::ControllerBindingProfile left_binding_profile{};
    wawvr::xr::ControllerBindingProfile right_binding_profile{};
};

struct VrGameplayPrompt final {
    VrGameplayPromptKind kind{VrGameplayPromptKind::none};
    std::array<char, kVrPromptLineBytes> first{};
    std::array<char, kVrPromptLineBytes> second{};
    friend bool operator==(const VrGameplayPrompt&, const VrGameplayPrompt&) = default;
};

// Pure, bounded action-level guidance. Never accepts/replaces localized text,
// subtitles, objective names or arbitrary strings from native memory.
[[nodiscard]] VrGameplayPrompt build_vr_gameplay_prompt(
    const VrGameplayPromptInput& input) noexcept;

struct VrNativeBindingPromptSettings final {
    bool automatic_reload{};
    bool button_grenades{};
    bool right_stick_melee{};
    bool support_weapon_selected{};
    wawvr::xr::ControllerBindingProfile left_binding_profile{};
    wawvr::xr::ControllerBindingProfile right_binding_profile{};
};

// Only an exact native action identifier can resolve. This does not accept a
// localized sentence, subtitle or arbitrary key name, and never changes the
// native script getkeybinding result or the actual keyboard bindings.
[[nodiscard]] std::string_view resolve_vr_native_binding_prompt(
    std::string_view exact_command,
    const VrNativeBindingPromptSettings& settings) noexcept;

// Diagnostic classification only. These are authored localization KEYS, not
// action commands or translated sentences. A nonzero one-hot bit permits one
// bounded observation; it never authorizes replacement of native text.
[[nodiscard]] std::uint32_t observed_vr_localized_hint_bit(
    std::string_view exact_key) noexcept;

// Separate diagnostic namespace for the 33 exact keys referenced by the
// stock maps/didyouknow_coop.csv table. These bits never extend the use-hint
// substitution allowlist, translate a loading tip, or imply it was displayed.
[[nodiscard]] std::uint64_t observed_vr_coop_loading_hint_bit(
    std::string_view exact_key) noexcept;

struct VrEncodedPromptSegment final {
    std::uint16_t offset{},bytes{};
    std::uint32_t hash{},known_key_bit{};
    bool localized_key{},conversion_marker16{};
};
struct VrEncodedPromptObservation final {
    bool valid{},exact_single_target_key{};
    std::uint8_t segment_count{},localized_segments{},literal_arguments{};
    std::uint32_t known_key_mask{};
    std::array<VrEncodedPromptSegment,8> segments{};
};
// Diagnostic grammar from the captured native encoded-message formatter:
// default/14h localized segment,15h literal argument,16h conversion marker.
// Does NOT localize, substitute arguments, produce display text, or grant any
// replacement permission. No arbitrary payload is copied into this summary.
[[nodiscard]] VrEncodedPromptObservation observe_vr_encoded_prompt(
    std::string_view encoded) noexcept;

// Exact use/plant/door template only, canonical one-key + one literal F
// argument. Returns an immutable, process-lifetime PRIVATE encoded copy with
// only that binding argument changed. Unknown grammar/profile stays native.
// Caller must separately prove active focused SP stereo display ownership.
[[nodiscard]] std::string_view resolve_vr_encoded_use_hint_copy(
    std::string_view encoded,const VrNativeBindingPromptSettings& settings) noexcept;

// Only the game-thread owner publishes exact validated identity. No engine
// pointers cross to the renderer; old publications expire after 500 ms.
void publish_vr_gameplay_prompt_weapon(
    std::string_view exact_internal_name, bool identity_valid,
    bool right_gripped, bool left_gripped) noexcept;
void publish_vr_gameplay_reload_hint(VrReloadPromptStage stage) noexcept;
[[nodiscard]] bool vr_gameplay_prompt_support_weapon_selected() noexcept;

} // namespace wawvr::mod
