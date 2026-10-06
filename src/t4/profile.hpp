#pragma once

#include "address.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace wawvr::t4 {

struct Sha256Digest final {
    std::array<std::uint8_t, 32> bytes{};

    friend constexpr bool operator==(const Sha256Digest&, const Sha256Digest&) = default;
};

enum class HookSiteId : std::uint8_t {
    gameplay_render_scene_call,
    render_backend_begin,
    lod_tan_half_fov_y_load,
    diagnostic_print_sentinel,
    main_loop_com_frame_call,
    main_loop_com_frame_context_sentinel,
    com_frame_entry_sentinel,
    scr_place_setup_float_viewport_entry_sentinel,
    scr_place_view_zero_init_context_sentinel,
    cg_draw_2d_call,
    cg_draw_2d_call_context_sentinel,
    post_build_usercmd,
    auto_melee_enabled_branch,
    auto_melee_enabled_context_sentinel,
    cl_key_event_entry_sentinel,
    cl_key_event_dispatch_context_sentinel,
    ui_mouse_event_entry_sentinel,
    ui_mouse_event_native_call,
    ui_mouse_event_call_context_sentinel,
    cbuf_add_text_entry_sentinel,
    winmain_cbuf_add_text_call_context_sentinel,
    weapnext_command_identity_sentinel,
    weapnext_command_registration_sentinel,
    weapnext_handler_entry_sentinel,
    viewmodel_weapon_call,
    viewmodel_pose_update_call,
    viewmodel_camera_tag_matrix_call,
    viewmodel_camera_tag_matrix_context_sentinel,
    cg_add_player_weapon_entry_sentinel,
    cg_update_viewmodel_pose_entry_sentinel,
    cg_dobj_get_world_tag_pos_context_sentinel,
    viewmodel_pose_context_sentinel,
    viewmodel_composition_context_sentinel,
    dobj_bone_layout_sentinel,
    renderer_model_layout_sentinel,
    renderer_hide_part_bits_sentinel,
    renderer_hidden_surface_cull_sentinel,
    authoritative_gun_angles_sentinel,
    mounted_turret_clientaim_call,
    mounted_turret_clientaim_call_context_sentinel,
    mounted_turret_clientaim_entry_sentinel,
    mounted_turret_fill_weapon_parms_call,
    mounted_turret_fill_weapon_parms_call_context_sentinel,
    mounted_mg42_controllers_call,
    mounted_mg42_controllers_call_context_sentinel,
    mounted_mg42_controllers_entry_sentinel,
    mounted_mg42_control_tags_context_sentinel,
    mounted_mg42_precontrollers_entry_sentinel,
    calc_muzzle_points_sentinel,
    fire_weapon_calc_muzzle_call,
    fire_weapon_calc_muzzle_context_sentinel,
    weapon_rocket_launcher_fire_rocket_call,
    weapon_rocket_launcher_fire_rocket_context_sentinel,
    fire_rocket_entry_sentinel,
    fire_rocket_stable_missile_flags_and_return_sentinel,
    cg_missile_bazooka_trail_call_sentinel,
    cg_play_bolted_effect_spawn_call,
    cg_play_bolted_effect_spawn_context_sentinel,
    fx_spawn_bolted_effect_entry_sentinel,
    fx_spawn_bolted_effect_return_token_sentinel,
    fire_weapon_bullet_fire_call,
    fire_weapon_bullet_fire_context_sentinel,
    bullet_fire_entry_sentinel,
    bullet_fire_spread_argument_sentinel,
    draw_bullet_impacts_get_spread_call,
    draw_bullet_impacts_get_spread_context_sentinel,
    get_spread_for_weapon_entry_sentinel,
    draw_bullet_impacts_view_origin_call,
    draw_bullet_impacts_view_origin_context_sentinel,
    client_bullet_view_origin_entry_sentinel,
    draw_bullet_impacts_fire_bullet_penetrate_call_sentinel,
    draw_bullet_impacts_fire_bullet_penetrate_context_sentinel,
    fire_bullet_penetrate_entry_sentinel,
    fire_bullet_penetrate_initial_bullet_trace_call_sentinel,
    fire_bullet_penetrate_initial_bullet_trace_result_seam,
    fire_bullet_penetrate_initial_bullet_trace_context_sentinel,
    client_bullet_trace_entry_sentinel,
    client_impact_selector_call,
    client_impact_selector_context_sentinel,
    client_impact_selector_entry_sentinel,
    client_impact_fx_spawn_call,
    client_impact_fx_spawn_context_sentinel,
    client_impact_fx_spawn_entry_sentinel,
    get_player_angles_registration_sentinel,
    get_player_angles_entry_sentinel,
    get_player_angles_return_context_sentinel,
    get_player_angles_scr_add_vector_call,
    scr_add_vector_entry_sentinel,
    get_current_weapon_identity_sentinel,
    get_current_weapon_fallback_identity_sentinel,
    weapon_definition_registration_sentinel,
};

enum class HookRole : std::uint8_t {
    render,
    frame_boundary,
    identity_sentinel,
    input,
    weapon_path,
};

enum class SiteUse : std::uint8_t {
    detour_candidate,
    validation_only,
};

struct HookSite final {
    static constexpr std::size_t kMaximumExpectedBytes = 100;

    HookSiteId id{};
    std::string_view name{};
    HookRole role{};
    SiteUse use{};
    Rva rva{};
    std::uint8_t minimum_patch_bytes{};
    std::uint8_t expected_size{};
    std::array<std::uint8_t, kMaximumExpectedBytes> expected{};

    [[nodiscard]] constexpr std::span<const std::uint8_t> expected_bytes() const noexcept {
        return {expected.data(), expected_size};
    }
};

enum class DataSymbolId : std::uint8_t {
    command_ring,
    command_number,
    cgame_gun_pitch_degrees,
    cgame_gun_yaw_degrees,
    client_view_yaw_degrees,
    key_catchers,
    connection_state,
    gameplay_refdef_viewport,
    scr_place_view_zero,
    local_player_entity,
    predicted_player_state,
    weapon_definition_pointer_table,
    // Legacy identifier: retail SP stores the inclusive
    // bg_lastParsedWeaponIndex here, not an exclusive count.
    weapon_definition_count,
    fx_marks_no_marks,
    fx_marks_allocated_count,
    fx_marks_freed_count,
    fx_system,
};

// Exact-profile data locations exposed only after the executable and mapped
// image have passed validation. `size` is the minimum known usable extent; it
// is not a claim that the surrounding proprietary engine structure is known.
struct DataSymbol final {
    DataSymbolId id{};
    std::string_view name{};
    Rva rva{};
    std::uint32_t size{};
};

enum class ExecutableVariant : std::uint8_t {
    single_player,
    multiplayer,
};

// Engine/code layout selected only after the complete executable identity has
// matched. Multiple on-disk distributions may share one layout, but only when
// every profiled RVA, ABI sentinel, and data symbol has been independently
// verified for that exact file identity.
enum class ExecutableLayoutId : std::uint8_t {
    t4_sp_1_7_1263,
    t4_mp_1_7_1263,
};

// SteamStub-wrapped executables retain an exact whole-file identity and PE
// metadata, but their original game code is restored only after process
// startup. On-disk hook bytes are therefore meaningful only for plain images;
// mapped hook bytes remain mandatory for every state.
enum class OnDiskCodeState : std::uint8_t {
    plain,
    steam_drm_wrapped,
};

struct ExecutableProfile final {
    std::string_view id{};
    ExecutableVariant variant{};
    ExecutableLayoutId layout{};
    OnDiskCodeState on_disk_code{};
    std::string_view observed_filename{};
    std::string_view product_version{};
    std::string_view engine_build{};
    std::uint64_t file_size{};
    Sha256Digest sha256{};
    std::uint16_t machine{};
    std::uint16_t section_count{};
    std::uint16_t coff_characteristics{};
    std::uint16_t dll_characteristics{};
    std::uint32_t coff_timestamp{};
    std::uint32_t preferred_image_base{};
    Rva entry_point_rva{};
    std::uint32_t size_of_image{};
    std::span<const HookSite> sites{};
    std::span<const DataSymbol> data_symbols{};
};

// Exact profile for the user's locally observed Campaign/Co-op executable.
// The hash is authoritative; the filename is descriptive only.
[[nodiscard]] const ExecutableProfile& t4_sp_1_7_1263_profile() noexcept;

// Exact profile for the user's locally observed Multiplayer executable.
// As with the SP profile, the hash is authoritative and the filename is only
// descriptive.
[[nodiscard]] const ExecutableProfile& t4_mp_1_7_1263_profile() noexcept;

// Exact untouched Steam Build 252004 SP executable. Its on-disk code is
// SteamStub-wrapped; all mapped instruction sentinels still have to match the
// independently verified T4 SP layout before binding succeeds.
[[nodiscard]] const ExecutableProfile&
t4_steam_sp_1_7_1263_profile() noexcept;

// Exact untouched Steam Build 252004 MP executable. It shares the verified T4
// MP layout only after the complete wrapped-file identity has selected it and
// the restored mapped image has passed every sentinel.
[[nodiscard]] const ExecutableProfile&
t4_steam_mp_1_7_1263_profile() noexcept;

// Enumerates every exact executable identity understood by this build. The
// returned profile objects have static lifetime.
[[nodiscard]] std::span<const ExecutableProfile* const>
supported_profiles() noexcept;

// Selects a profile only when both immutable file identity fields match.
[[nodiscard]] const ExecutableProfile* find_supported_profile(
    std::uint64_t file_size, const Sha256Digest& sha256) noexcept;

[[nodiscard]] const HookSite* find_hook_site(const ExecutableProfile& profile,
                                             HookSiteId id) noexcept;

[[nodiscard]] const DataSymbol* find_data_symbol(const ExecutableProfile& profile,
                                                 DataSymbolId id) noexcept;

}  // namespace wawvr::t4
