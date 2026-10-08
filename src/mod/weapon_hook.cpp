// SPDX-License-Identifier: GPL-3.0-only
// WorldAtWarVR's independently structured T4/WaW viewmodel integration.
// Implemented against validated T4 weapon and command boundaries.
#include "weapon_hook.hpp"
#include "virtual_query_timing.hpp"
#include "resident_page_access.hpp"

#include "bazooka_trail_hook.hpp"
#include "bolt_action_weapon_profile.hpp"
#include "camera_comfort_logic.hpp"
#include "chest_weapon_pose.hpp"

#include "controller_state.hpp"
#include "firing_haptics.hpp"
#include "head_relative_pose_freeze_logic.hpp"
#include "input_mapping.hpp"
#include "manual_grenade_runtime.hpp"
#include "manual_reload_runtime.hpp"
#include "performance_timing.hpp"
#include "tracked_hands_runtime.hpp"
#include "peer_thread_quiescence.hpp"
#include "physical_scope_logic.hpp"
#include "pistol_support_pose.hpp"
#include "post_t4_aim_phase_logic.hpp"
#include "rocket_barrage_logic.hpp"
#include "stereo_diagnostics.hpp"
#include "t4_layout_selector.hpp"
#include "two_hand_sight_alignment_policy.hpp"
#include "viewmodel_filter.hpp"
#include "weapon_ballistics_logic.hpp"
#include "weapon_grip_logic.hpp"
#include "weapon_identity_snapshot_reader.hpp"
#include "weapon_placement.hpp"
#include "weapon_pose_pipeline_trace_logic.hpp"

#include "t4/hook_api.hpp"
#include "t4/profile.hpp"
#include "xr_math.h"
#include "xr_types.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

namespace wawvr::mod {

extern "C" void __cdecl wawvr_apply_physical_muzzle_from_bridge(
    void* weapon_parms,
    void* firing_entity) noexcept;
extern "C" void __cdecl wawvr_apply_fixed_ads_spread_from_bridge(
    void* weapon_parms,
    void* attacker,
    float* spread_degrees) noexcept;
extern "C" const float* __cdecl
wawvr_select_rocket_barrage_angles_from_bridge(
    void* entity,
    const float* native_angles) noexcept;
extern "C" int __cdecl wawvr_call_dobj_get_world_tag_pos(
    void* dobj,
    std::uint32_t tag,
    const void* pose,
    wawvr::xr::Vec3f* world_position) noexcept;
extern "C" int __cdecl wawvr_call_dobj_get_world_tag_matrix(
    void* dobj,
    std::uint32_t tag,
    const void* pose,
    float* tag_matrix,
    wawvr::xr::Vec3f* world_position) noexcept;
extern "C" void* __cdecl wawvr_player_rocket_bridge(
    void* parent,
    std::uint32_t weapon_index,
    float* start,
    float* direction,
    const float* gun_velocity,
    void* target,
    const float* target_offset) noexcept;

namespace {

constexpr std::size_t kCallInstructionSize = 5;
constexpr std::size_t kViewmodelPoseOriginOffset = 0x24;
constexpr std::size_t kDObjSize = 0x68;
constexpr std::size_t kDObjNumBonesOffset = 0x0A;
constexpr std::size_t kDObjSkeletonLockOffset = 0x10;
constexpr std::size_t kDObjSkelPartBitsOffset = 0x34;
constexpr std::size_t kDObjSkelTimestampOffset = 0x44;
constexpr std::size_t kDObjSkelMatOffset = 0x48;
constexpr std::size_t kSpClientCurrentWeaponOffset = 0x104;
constexpr std::size_t kSpClientFallbackWeaponOffset = 0x20F8;
// Recovered CG_AddPlayerWeapon reads viewmodel/DOF fields through the same
// exact 0x920-byte playerState prefix already required by manual reload. Keep
// the temporary shadow bounded to that proven prefix while ensuring the native
// callee cannot read beyond it.
constexpr std::size_t kSpPlayerStateViewmodelPrefixSpan = 0x920;
constexpr std::size_t kSpPlayerStateWeapFlagsOffset = 0x10;
constexpr std::uint32_t kSpPlayerStateOffhandViewmodelFlag = 0x02;
constexpr std::size_t kSpGEntityFlagsOffset = 0x1B4;
constexpr std::size_t kSpGEntityTrajectoryDeltaOffset = 0x24;
constexpr std::size_t kSpGEntityCurrentAnglesOffset = 0x16C;
constexpr std::size_t kWeaponDefinitionNameOffset = 0x00;
constexpr std::uint32_t kMaximumRuntimeSpWeaponIndex = 127;
constexpr std::size_t kWeaponDefinitionPointerTableExtent =
    (kMaximumRuntimeSpWeaponIndex + 1U) * sizeof(std::uint32_t);

struct ViewmodelAxisPlacement final {
    wawvr::xr::Basis3f axis{};
    wawvr::xr::Vec3f origin{};
};

static_assert(
    offsetof(ViewmodelAxisPlacement, origin) ==
    sizeof(wawvr::xr::Basis3f));
static_assert(
    sizeof(ViewmodelAxisPlacement) ==
    sizeof(wawvr::xr::Basis3f) + sizeof(wawvr::xr::Vec3f));
constexpr std::array<std::uint8_t, 15> kSpCgAddPlayerWeaponSentinel{
    0xA1, 0xE0, 0x32, 0x47, 0x03,
    0x8B, 0x50, 0x20,
    0x83, 0xEC, 0x18,
    0xF6, 0xC2, 0x06,
    0x53,
};
constexpr std::array<std::uint8_t, 31> kSpCgUpdateViewModelPoseSentinel{
    0x51, 0x85, 0xC0, 0x74, 0x10, 0x6A, 0x30, 0x83,
    0xC0, 0x14, 0x6A, 0x00, 0x50, 0xE8, 0x2E, 0x84,
    0x34, 0x00, 0x83, 0xC4, 0x0C, 0xB8, 0xE4, 0xCF,
    0x5C, 0x03, 0xB9, 0xA8, 0xB6, 0x52, 0x03,
};
constexpr std::array<std::uint8_t, 25> kSpCgDObjGetWorldTagPosSentinel{
    0x51, 0x53, 0x8B, 0x5C, 0x24, 0x10, 0x56, 0x8D,
    0x44, 0x24, 0x0B, 0x50, 0x51, 0x8B, 0xCF, 0xC6,
    0x44, 0x24, 0x13, 0xFE, 0xE8, 0xD7, 0x93, 0x1C, 0x00,
};
constexpr std::array<std::uint8_t, 29> kSpCgDObjGetWorldTagMatrixSentinel{
    0x51, 0x53, 0x8B, 0x5C, 0x24, 0x14, 0x56, 0x57,
    0x8B, 0xF8, 0x8D, 0x44, 0x24, 0x0F, 0x50, 0x51,
    0x8B, 0xCF, 0xC6, 0x44, 0x24, 0x17, 0xFE, 0xE8,
    0x74, 0x94, 0x1C, 0x00, 0x83,
};
constexpr std::array<std::uint8_t, 36> kSpViewmodelPoseContextSentinel{
    0x83, 0xBD, 0x48, 0x01, 0x00, 0x00, 0x09, 0x75,
    0x09, 0x8B, 0x44, 0x24, 0x38, 0xE8, 0xE6, 0x14,
    0x1C, 0x00, 0x8B, 0x07, 0xE8, 0x0F, 0xE2, 0xFF,
    0xFF, 0x83, 0x7C, 0x24, 0x3C, 0x00, 0x0F, 0x84,
    0x00, 0x01, 0x00, 0x00,
};
constexpr std::array<std::uint8_t, 100> kSpViewmodelCompositionSentinel{
    0x8B, 0x43, 0x0C, 0x83, 0xC4, 0x0C, 0x85, 0xC0,
    0x0F, 0x84, 0xD2, 0x01, 0x00, 0x00, 0x8B, 0x4B,
    0x4C, 0x85, 0xC9, 0x0F, 0x84, 0xC7, 0x01, 0x00,
    0x00, 0x66, 0x8B, 0x15, 0x5A, 0x3C, 0xF3, 0x01,
    0x53, 0x66, 0xC7, 0x44, 0x24, 0x28, 0x00, 0x00,
    0x66, 0x89, 0x54, 0x24, 0x30, 0xC6, 0x44, 0x24,
    0x2A, 0x00, 0xC6, 0x44, 0x24, 0x32, 0x00, 0x89,
    0x4C, 0x24, 0x24, 0x89, 0x44, 0x24, 0x2C, 0xE8,
    0xAC, 0xFD, 0xFF, 0xFF, 0x8B, 0xC8, 0x8B, 0x44,
    0x24, 0x3C, 0x83, 0xC4, 0x04, 0x50, 0x51, 0x89,
    0x4D, 0x30, 0x8D, 0x4C, 0x24, 0x28, 0x6A, 0x02,
    0x8D, 0x86, 0x00, 0x08, 0x00, 0x00, 0x51, 0xE8,
    0x2C, 0x9C, 0x13, 0x00,
};
constexpr std::array<std::uint8_t, 18> kMpCgAddPlayerWeaponSentinel{
    0x83, 0xEC, 0x1C, 0x53, 0x8B, 0x5C, 0x24, 0x2C,
    0x55, 0x56, 0x57, 0x8B, 0xF8, 0xA1, 0x04, 0xFD,
    0x98, 0x00,
};
constexpr std::array<std::uint8_t, 31> kMpCgUpdateViewModelPoseSentinel{
    0x51, 0x85, 0xC0, 0x74, 0x10, 0x6A, 0x30, 0x83,
    0xC0, 0x14, 0x6A, 0x00, 0x50, 0xE8, 0x9E, 0xB6,
    0x32, 0x00, 0x83, 0xC4, 0x0C, 0xB8, 0x9C, 0xC3,
    0xA8, 0x00, 0xB9, 0x04, 0xEC, 0x9E, 0x00,
};
constexpr std::array<std::uint8_t, 25> kMpCgDObjGetWorldTagPosSentinel{
    0x51, 0x53, 0x8B, 0x5C, 0x24, 0x10, 0x56, 0x8D,
    0x44, 0x24, 0x0B, 0x50, 0x51, 0x8B, 0xCF, 0xC6,
    0x44, 0x24, 0x13, 0xFE, 0xE8, 0x67, 0x5C, 0x19, 0x00,
};
constexpr std::array<std::uint8_t, 36> kMpViewmodelPoseContextSentinel{
    0x83, 0xBD, 0x48, 0x01, 0x00, 0x00, 0x09, 0x75,
    0x09, 0x8B, 0x44, 0x24, 0x38, 0xE8, 0xB1, 0x88,
    0x1B, 0x00, 0x8B, 0x03, 0xE8, 0x9A, 0xE2, 0xFF,
    0xFF, 0x83, 0x7C, 0x24, 0x3C, 0x00, 0x0F, 0x84,
    0x30, 0x01, 0x00, 0x00,
};
constexpr std::array<std::uint8_t, 100> kMpViewmodelCompositionSentinel{
    0x8B, 0x43, 0x0C, 0x83, 0xC4, 0x0C, 0x85, 0xC0,
    0x0F, 0x84, 0xD2, 0x01, 0x00, 0x00, 0x8B, 0x4B,
    0x4C, 0x85, 0xC9, 0x0F, 0x84, 0xC7, 0x01, 0x00,
    0x00, 0x66, 0x8B, 0x15, 0xDA, 0xA9, 0x21, 0x02,
    0x53, 0x66, 0xC7, 0x44, 0x24, 0x28, 0x00, 0x00,
    0x66, 0x89, 0x54, 0x24, 0x30, 0xC6, 0x44, 0x24,
    0x2A, 0x00, 0xC6, 0x44, 0x24, 0x32, 0x00, 0x89,
    0x4C, 0x24, 0x24, 0x89, 0x44, 0x24, 0x2C, 0xE8,
    0xFD, 0xFD, 0xFF, 0xFF, 0x8B, 0xC8, 0x8B, 0x44,
    0x24, 0x3C, 0x83, 0xC4, 0x04, 0x50, 0x51, 0x89,
    0x4D, 0x2C, 0x8D, 0x4C, 0x24, 0x28, 0x6A, 0x02,
    0x8D, 0x86, 0x00, 0x06, 0x00, 0x00, 0x51, 0xE8,
    0x1D, 0xB3, 0x0E, 0x00,
};
constexpr std::array<std::uint8_t, 50> kDObjBoneLayoutSentinel{
    0x55, 0x8B, 0x69, 0x64, 0x56, 0x57, 0x0F, 0xB6,
    0x79, 0x09, 0x0F, 0xB6, 0x49, 0x0A, 0x3B, 0xC1,
    0x89, 0x6C, 0x24, 0x0C, 0x73, 0x1C, 0x33, 0xC9,
    0x85, 0xFF, 0x7E, 0x16, 0x90, 0x8B, 0x74, 0x8D,
    0x00, 0x0F, 0xB6, 0x56, 0x04, 0x3B, 0xC2, 0x72,
    0x52, 0x83, 0xC1, 0x01, 0x2B, 0xC2, 0x3B, 0xCF,
    0x7C, 0xEB,
};
constexpr std::array<std::uint8_t, 34> kRendererModelLayoutSentinel{
    0x8B, 0x42, 0x64, 0x8B, 0x04, 0xB0, 0x0F, 0xB6,
    0x48, 0x04, 0x89, 0x4C, 0x24, 0x10, 0x8B, 0x4D,
    0x08, 0x0F, 0xBE, 0x4C, 0x31, 0x48, 0x85, 0xC9,
    0x0F, 0x8C, 0xF8, 0x01, 0x00, 0x00, 0x8B, 0x50,
    0x1C, 0x89,
};
constexpr std::array<std::uint8_t, 48> kRendererHidePartBitsSentinel{
    0xF3, 0x0F, 0x7E, 0x42, 0x50, 0xC1, 0xE8, 0x05,
    0x85, 0xF6, 0x66, 0x0F, 0xD6, 0x84, 0x24, 0x90,
    0x00, 0x00, 0x00, 0xF3, 0x0F, 0x7E, 0x42, 0x58,
    0x66, 0xC7, 0x44, 0x24, 0x44, 0x00, 0x00, 0x66,
    0x0F, 0xD6, 0x84, 0x24, 0x98, 0x00, 0x00, 0x00,
    0x0F, 0x86, 0x6F, 0x01, 0x00, 0x00, 0xF7, 0xD8,
};
constexpr std::array<std::uint8_t, 57> kRendererHiddenSurfaceCullSentinel{
    0x8B, 0x8C, 0x24, 0x9C, 0x00, 0x00, 0x00, 0x8B,
    0xBC, 0x24, 0x98, 0x00, 0x00, 0x00, 0x23, 0xFA,
    0x23, 0xCE, 0x0B, 0xCF, 0x8B, 0xBC, 0x24, 0x94,
    0x00, 0x00, 0x00, 0x23, 0xF8, 0x0B, 0xCF, 0x8B,
    0xBC, 0x24, 0x90, 0x00, 0x00, 0x00, 0x23, 0xBC,
    0x24, 0xA0, 0x00, 0x00, 0x00, 0x0B, 0xCF, 0x8B,
    0x7C, 0x24, 0x24, 0x74, 0x0B, 0xC7, 0x07, 0xFD,
    0xFF,
};

struct WeaponExecutableLayout final {
    T4LayoutFamily family{T4LayoutFamily::unsupported};
    bool add_player_weapon_uses_eax_centity{};
    bool overwrite_authoritative_weapon_basis{};
    wawvr::t4::Rva cg_add_player_weapon_rva{};
    std::span<const std::uint8_t> cg_add_player_weapon_sentinel{};
    wawvr::t4::Rva cg_update_viewmodel_pose_rva{};
    std::span<const std::uint8_t> cg_update_viewmodel_pose_sentinel{};
    wawvr::t4::Rva cg_dobj_get_world_tag_pos_rva{};
    std::span<const std::uint8_t> cg_dobj_get_world_tag_pos_sentinel{};
    wawvr::t4::Rva cg_dobj_get_world_tag_matrix_rva{};
    std::span<const std::uint8_t> cg_dobj_get_world_tag_matrix_sentinel{};
    wawvr::t4::Rva viewmodel_pose_context_rva{};
    std::span<const std::uint8_t> viewmodel_pose_context_sentinel{};
    wawvr::t4::Rva viewmodel_composition_rva{};
    std::span<const std::uint8_t> viewmodel_composition_sentinel{};
    wawvr::t4::Rva dobj_bone_layout_rva{};
    wawvr::t4::Rva renderer_model_layout_rva{};
    wawvr::t4::Rva renderer_hide_part_bits_rva{};
    wawvr::t4::Rva renderer_hidden_surface_cull_rva{};
    wawvr::t4::Rva gameplay_refdef_origin_rva{};
    wawvr::t4::Rva gameplay_refdef_axis_rva{};
    wawvr::t4::Rva viewmodel_axis_rva{};
    wawvr::t4::Rva viewmodel_axis_origin_rva{};
    wawvr::t4::Rva viewmodel_pose_rva{};
    std::size_t viewmodel_pose_extent{};
    wawvr::t4::Rva tag_brass_word_rva{};
    wawvr::t4::Rva tag_flash_word_rva{};
    wawvr::t4::Rva tag_inhand_word_rva{};
    wawvr::t4::Rva tag_origin_word_rva{};
    wawvr::t4::Rva tag_weapon_word_rva{};
    wawvr::t4::Rva tag_weapon_right_word_rva{};
    std::size_t weapon_parms_size{};
    std::size_t weapon_parms_forward_offset{};
    std::size_t weapon_parms_right_offset{};
    std::size_t weapon_parms_up_offset{};
    std::size_t weapon_parms_muzzle_trace_offset{};
    std::size_t weapon_parms_weapon_definition_offset{};
    std::size_t gentity_size{};
    std::size_t gentity_number_offset{};
    std::size_t gentity_client_offset{};
    std::size_t weapon_definition_type_offset{};
    std::size_t weapon_definition_ads_spread_offset{};
    std::size_t weapon_definition_projectile_speed_offset{};
    std::size_t missile_launch_time_offset{};
};

const WeaponExecutableLayout kSpWeaponLayout{
    .family = T4LayoutFamily::single_player_1_7_1263,
    .add_player_weapon_uses_eax_centity = false,
    .overwrite_authoritative_weapon_basis = false,
    .cg_add_player_weapon_rva = 0x000697A0,
    .cg_add_player_weapon_sentinel = kSpCgAddPlayerWeaponSentinel,
    .cg_update_viewmodel_pose_rva = 0x00067B00,
    .cg_update_viewmodel_pose_sentinel = kSpCgUpdateViewModelPoseSentinel,
    .cg_dobj_get_world_tag_pos_rva = 0x00043030,
    .cg_dobj_get_world_tag_pos_sentinel = kSpCgDObjGetWorldTagPosSentinel,
    .cg_dobj_get_world_tag_matrix_rva = 0x00042F90,
    .cg_dobj_get_world_tag_matrix_sentinel =
        kSpCgDObjGetWorldTagMatrixSentinel,
    .viewmodel_pose_context_rva = 0x000698D8,
    .viewmodel_pose_context_sentinel = kSpViewmodelPoseContextSentinel,
    .viewmodel_composition_rva = 0x00064C60,
    .viewmodel_composition_sentinel = kSpViewmodelCompositionSentinel,
    .dobj_bone_layout_rva = 0x0020C433,
    .renderer_model_layout_rva = 0x0031E0B0,
    .renderer_hide_part_bits_rva = 0x0031E121,
    .renderer_hidden_surface_cull_rva = 0x0031E21D,
    .gameplay_refdef_origin_rva = 0x03120354,
    .gameplay_refdef_axis_rva = 0x03120364,
    .viewmodel_axis_rva = 0x0312B6A8,
    .viewmodel_axis_origin_rva = 0x0312B6CC,
    .viewmodel_pose_rva = 0x031CCFB4,
    .viewmodel_pose_extent = 0x40,
    .tag_brass_word_rva = 0x01B33C3C,
    .tag_flash_word_rva = 0x01B33C42,
    .tag_inhand_word_rva = 0x01B33C4E,
    .tag_origin_word_rva = 0x01B33C58,
    .tag_weapon_word_rva = 0x01B33C5A,
    .tag_weapon_right_word_rva = 0x01B33C60,
    .weapon_parms_size = 0x40,
    .weapon_parms_forward_offset = 0x00,
    .weapon_parms_right_offset = 0x0C,
    .weapon_parms_up_offset = 0x18,
    .weapon_parms_muzzle_trace_offset = 0x24,
    .weapon_parms_weapon_definition_offset = 0x3C,
    .gentity_size = 0x378,
    .gentity_number_offset = 0x00,
    .gentity_client_offset = 0x180,
    .weapon_definition_type_offset = 0x144,
    .weapon_definition_ads_spread_offset = 0x830,
    .weapon_definition_projectile_speed_offset = 0x664,
    .missile_launch_time_offset = 0x54,
};

const WeaponExecutableLayout kMpWeaponLayout{
    .family = T4LayoutFamily::multiplayer_1_7_1263,
    .add_player_weapon_uses_eax_centity = true,
    .overwrite_authoritative_weapon_basis = true,
    .cg_add_player_weapon_rva = 0x0007F410,
    .cg_add_player_weapon_sentinel = kMpCgAddPlayerWeaponSentinel,
    .cg_update_viewmodel_pose_rva = 0x0007D7D0,
    .cg_update_viewmodel_pose_sentinel = kMpCgUpdateViewModelPoseSentinel,
    .cg_dobj_get_world_tag_pos_rva = 0x000464E0,
    .cg_dobj_get_world_tag_pos_sentinel = kMpCgDObjGetWorldTagPosSentinel,
    .viewmodel_pose_context_rva = 0x0007F51D,
    .viewmodel_pose_context_sentinel = kMpViewmodelPoseContextSentinel,
    .viewmodel_composition_rva = 0x0007C18F,
    .viewmodel_composition_sentinel = kMpViewmodelCompositionSentinel,
    .dobj_bone_layout_rva = 0x001DC173,
    .renderer_model_layout_rva = 0x002F5E80,
    .renderer_hide_part_bits_rva = 0x002F5EF1,
    .renderer_hidden_surface_cull_rva = 0x002F5FED,
    .gameplay_refdef_origin_rva = 0x005E6788,
    .gameplay_refdef_axis_rva = 0x005E6798,
    .viewmodel_axis_rva = 0x005EEC04,
    .viewmodel_axis_origin_rva = 0x005EEC28,
    .viewmodel_pose_rva = 0x0068C368,
    .viewmodel_pose_extent = 0x40,
    .tag_brass_word_rva = 0x01E1A9BC,
    .tag_flash_word_rva = 0x01E1A9C2,
    .tag_inhand_word_rva = 0x01E1A9CE,
    .tag_origin_word_rva = 0x01E1A9D8,
    .tag_weapon_word_rva = 0x01E1A9DA,
    .tag_weapon_right_word_rva = 0x01E1A9E0,
    .weapon_parms_size = 0x40,
    .weapon_parms_forward_offset = 0x00,
    .weapon_parms_right_offset = 0x0C,
    .weapon_parms_up_offset = 0x18,
    .weapon_parms_muzzle_trace_offset = 0x24,
    .weapon_parms_weapon_definition_offset = 0x3C,
    .gentity_size = 0x330,
    .gentity_number_offset = 0x00,
    .gentity_client_offset = 0x184,
    .weapon_definition_type_offset = 0x144,
    .weapon_definition_ads_spread_offset = 0x830,
    .weapon_definition_projectile_speed_offset = 0x664,
    .missile_launch_time_offset = 0x54,
};

[[nodiscard]] const WeaponExecutableLayout* weapon_layout_for_profile(
    const wawvr::t4::ExecutableProfile& profile) noexcept {
    switch (select_t4_layout_family(profile)) {
    case T4LayoutFamily::single_player_1_7_1263:
        return &kSpWeaponLayout;
    case T4LayoutFamily::multiplayer_1_7_1263:
        return &kMpWeaponLayout;
    case T4LayoutFamily::unsupported:
        return nullptr;
    }
    return nullptr;
}

struct GfxScaledPlacement final {
    wawvr::xr::Quaternionf quaternion{};
    wawvr::xr::Vec3f origin{};
    float scale{};
};

static_assert(sizeof(GfxScaledPlacement) == 0x20);
static_assert(offsetof(GfxScaledPlacement, quaternion) == 0x00);
static_assert(offsetof(GfxScaledPlacement, origin) == 0x10);
static_assert(offsetof(GfxScaledPlacement, scale) == 0x1C);

using CgAddPlayerWeaponFunction = void(__cdecl*)(
    std::int32_t,
    const GfxScaledPlacement*,
    const void*,
    void*,
    std::int32_t);

using GFireRocketFunction = void*(__cdecl*)(
    void*,
    std::uint32_t,
    float*,
    float*,
    const float*,
    void*,
    const float*);

struct FinalVisibleAim final {
    bool valid{};
    std::uint64_t controller_generation{};
    std::uint64_t controller_frame_id{};
    std::uint64_t controller_action_sequence{};
    std::uint64_t publication_milliseconds{};
    bool live_controller_pose{};
    float pitch_degrees{};
    float yaw_degrees{};
    wawvr::xr::Basis3f axis{};
};

struct ActiveWeaponPoseContext final {
    bool valid{};
    WeaponGripMode grip_mode{WeaponGripMode::Chest};
    std::uint64_t weapon_identity{};
    std::uint64_t controller_generation{};
    std::uint64_t controller_publication_milliseconds{};
    wawvr::xr::Vec3f tracked_grip_world{};
    wawvr::xr::Vec3f weapon_origin{};
    wawvr::xr::Basis3f weapon_axis{};
    ControllerWeaponPose controller_pose{};
    bool weapon_attachment_axis_valid{};
    wawvr::xr::Basis3f weapon_attachment_axis{};
    wawvr::xr::Vec3f camera_origin{};
    wawvr::xr::Basis3f camera_axis{};
    // Controller placement may use the exact current head as its parent while
    // stereo receipts, hands, reloads, grenades, and holsters continue to use
    // the gravity-level body camera above.
    wawvr::xr::Vec3f controller_camera_origin{};
    wawvr::xr::Basis3f controller_camera_axis{};
    bool scene_base_lock_eligible{};
    bool right_gripping{};
    bool left_gripping{};
    bool support_pose_latched{};
    bool preserve_right_handoff_root{};
    // A logical latch transition is rendered first and becomes the committed
    // controller frame only after the post-T4 grip tag has been evaluated.
    // Until then, a failed post pass leaves the outgoing mode authoritative so
    // the next generation retries instead of consuming the transition edge.
    bool transition_pending{};
    bool align_grip_tag_before_commit{};
    bool recapture_attachment_after_post{};
    bool commit_right_ray_two_hand_steering_after_post{};
    bool reset_right_ray_two_hand_steering_after_post{};
    RightRayTwoHandSteeringState staged_right_ray_two_hand_steering{};
    // Retained rendering may run against a fresh-but-invalid XR frame. Do
    // not make that old frozen root look fresh merely because T4 evaluated it
    // again; only a genuinely tracked placement advances retention.
    bool refresh_retained_pose{};
    ManualReloadViewmodelContext manual_reload{};
};

struct PendingPlayerBazookaRocket final {
    bool valid{};
    const PhysicalRocketLauncherProfile* profile{};
    std::uintptr_t parent{};
    std::uint32_t weapon_index{};
    std::uint32_t weapon_definition_address{};
    std::uint64_t controller_generation{};
    std::uint64_t publication_milliseconds{};
    wawvr::xr::Vec3f muzzle_origin{};
    wawvr::xr::Basis3f weapon_root_launch_basis{};
    bool tracked_basis_valid{};
    wawvr::xr::Basis3f tracked_basis{};
    bool tag_bore_basis_valid{};
    wawvr::xr::Basis3f tag_bore_basis{};
};
static_assert(sizeof(EvaluatedViewmodelBoneTransform) == 0x20);
static_assert(
    offsetof(EvaluatedViewmodelBoneTransform, translation) == 0x10);

struct RetainedWeaponPose final {
    bool valid{};
    std::uint64_t weapon_identity{};
    std::uint64_t controller_generation{};
    std::uint64_t publication_milliseconds{};
    WeaponGripMode grip_mode{WeaponGripMode::Chest};
    wawvr::xr::Vec3f origin{};
    wawvr::xr::Basis3f axis{};
    bool grip_tag_valid{};
    wawvr::xr::Vec3f grip_tag_world{};
};

struct CommittedWeaponGripState final {
    WeaponGripMode mode{WeaponGripMode::Chest};
    std::uint64_t weapon_identity{};
};

struct ActiveHandsContext final {
    bool valid{};
    ControllerFrameSnapshot controller{};
    wawvr::xr::Vec3f camera_origin{};
    wawvr::xr::Basis3f camera_axis{};
};

struct HeadRelativeWeaponFreezeState final {
    HeadRelativePoseFreeze freeze{};
    std::uint64_t weapon_identity{};
    WeaponGripMode grip_mode{WeaponGripMode::Chest};
    std::uint64_t controller_generation{};
};

struct PostT4VisibleAimMetric final {
    std::uint32_t count{};
    double sum{};
    double sum_squared{};
    float minimum{(std::numeric_limits<float>::max)()};
    float maximum{};
};

struct PostT4VisibleAimDiagnosticState final {
    bool valid{};
    std::uint64_t weapon_identity{};
    std::uint64_t first_generation{};
    std::uint64_t last_generation{};
    std::uint64_t last_publication_milliseconds{};
    wawvr::xr::Vec3f previous_raw_pair_forward{};
    wawvr::xr::Vec3f previous_applied_forward{};
    wawvr::xr::Vec3f previous_raw_pair_up{};
    wawvr::xr::Vec3f previous_applied_up{};
    wawvr::xr::Vec3f previous_visible_forward{};
    // The visible grip-to-flash direction expressed in the applied weapon
    // basis. Its step is independent of whole-rifle controller motion, so it
    // isolates authored/native animation moving the evaluated barrel.
    wawvr::xr::Vec3f previous_visible_in_weapon_basis{};
    // Translation counterparts to the angular probe. These local-space
    // vectors separate a whole weapon/head phase error from attachment drift
    // and evaluated-skeleton motion that leaves every direction unchanged.
    wawvr::xr::Vec3f previous_head_relative_root{};
    wawvr::xr::Vec3f previous_grip_relative_root{};
    wawvr::xr::Vec3f previous_visible_grip_root{};
    wawvr::xr::Vec3f previous_visible_flash_root{};
    PostT4AimPhaseState head_local_phase{};
    std::uint32_t comparison_count{};
    std::uint32_t head_local_phase_comparison_count{};
    std::uint32_t head_local_lag1_better_count{};
    PostT4VisibleAimMetric raw_pair_step_degrees{};
    PostT4VisibleAimMetric applied_step_degrees{};
    PostT4VisibleAimMetric raw_pair_up_step_degrees{};
    PostT4VisibleAimMetric applied_up_step_degrees{};
    PostT4VisibleAimMetric controller_applied_up_error_degrees{};
    PostT4VisibleAimMetric visible_step_degrees{};
    PostT4VisibleAimMetric native_residual_step_degrees{};
    PostT4VisibleAimMetric controller_applied_error_degrees{};
    PostT4VisibleAimMetric visible_applied_error_degrees{};
    PostT4VisibleAimMetric grip_to_flash_length{};
    PostT4VisibleAimMetric head_relative_root_step{};
    PostT4VisibleAimMetric grip_relative_root_step{};
    PostT4VisibleAimMetric visible_grip_root_step{};
    PostT4VisibleAimMetric visible_flash_root_step{};
    PostT4VisibleAimMetric head_local_lag0_error_degrees{};
    PostT4VisibleAimMetric head_local_lag1_error_degrees{};
    PostT4VisibleAimMetric publication_delta_milliseconds{};
};

struct ClientBulletFireParams final {
    std::int32_t weapon_entity_index{};
    std::int32_t ignore_entity_index{};
    float damage_multiplier{};
    std::int32_t method_of_death{};
    wawvr::xr::Vec3f original_start{};
    wawvr::xr::Vec3f start{};
    wawvr::xr::Vec3f end{};
    wawvr::xr::Vec3f direction{};
};

struct ClientTrace final {
    std::array<float, 4> normal{};
    float fraction{};
    std::int32_t surface_flags{};
    std::int32_t contents{};
    std::uint32_t material_address{};
    std::int32_t hit_type{};
    std::uint16_t hit_id{};
    std::uint16_t model_index{};
    std::uint16_t part_name{};
    std::uint16_t bone_index{};
    std::uint16_t part_group{};
    std::uint8_t all_solid{};
    std::uint8_t start_solid{};
    std::uint8_t walkable{};
    std::array<std::uint8_t, 3> padding{};
};

struct ClientBulletTraceResults final {
    ClientTrace trace{};
    std::uint32_t hit_entity_address{};
    wawvr::xr::Vec3f hit_position{};
    std::uint8_t ignore_hit_entity{};
    std::array<std::uint8_t, 3> padding{};
    std::int32_t depth_surface_type{};
};

struct RetailMarkCounters final {
    bool valid{};
    std::uint8_t no_marks{};
    std::uint32_t allocated{};
    std::uint32_t freed{};
};

struct MosinImpactCorrelation final {
    bool armed{};
    std::uint32_t sequence{};
    std::uint32_t local_client_number{};
    std::uint32_t source_entity{};
    std::uint32_t weapon_index{};
    std::uint32_t weapon_definition{};
    std::uint32_t position_address{};
    wawvr::xr::Vec3f position{};
    wawvr::xr::Vec3f normal{};
    std::uint32_t selected_fx{};
    std::uint32_t target_entity{};
    RetailMarkCounters mark_counters{};
};

static_assert(sizeof(ClientBulletFireParams) == 0x40);
static_assert(offsetof(ClientBulletFireParams, start) == 0x1C);
static_assert(offsetof(ClientBulletFireParams, end) == 0x28);
static_assert(offsetof(ClientBulletFireParams, direction) == 0x34);
static_assert(sizeof(ClientTrace) == 0x34);
static_assert(offsetof(ClientTrace, fraction) == 0x10);
static_assert(offsetof(ClientTrace, surface_flags) == 0x14);
static_assert(offsetof(ClientTrace, hit_type) == 0x20);
static_assert(offsetof(ClientTrace, all_solid) == 0x2E);
static_assert(offsetof(ClientTrace, start_solid) == 0x2F);
static_assert(sizeof(ClientBulletTraceResults) == 0x4C);
static_assert(offsetof(ClientBulletTraceResults, hit_position) == 0x38);
static_assert(offsetof(ClientBulletTraceResults, depth_surface_type) == 0x48);

struct GripTagBinding final {
    std::uintptr_t address{};
    const char* name{};
};

struct ScopeTagBinding final {
    const char* name{};
    std::uint16_t tag{};
    bool generic_optic_anchor{};
};

std::atomic<bool> g_weapon_hook_installed{false};
std::atomic<bool> g_weapon_hook_enabled{false};
std::atomic<bool> g_published_support_pose{false};
std::atomic<bool> g_campaign_targeting_hook_installed{false};
std::atomic<bool> g_campaign_targeting_hook_enabled{false};
const WeaponExecutableLayout* g_weapon_layout = nullptr;
std::uintptr_t g_original_add_player_weapon = 0;
std::uintptr_t g_original_update_viewmodel_pose = 0;
std::uintptr_t g_original_calc_muzzle_points = 0;
std::uintptr_t g_original_bullet_fire = 0;
std::uintptr_t g_original_client_bullet_view_origin = 0;
std::uintptr_t g_original_get_spread_for_weapon = 0;
std::uintptr_t g_client_bullet_trace_resume = 0;
std::uintptr_t g_original_client_impact_selector = 0;
std::uintptr_t g_client_impact_selector_resume = 0;
std::uintptr_t g_original_client_impact_fx_spawn = 0;
std::uintptr_t g_client_impact_fx_spawn_resume = 0;
std::uintptr_t g_original_scr_add_vector = 0;
std::uintptr_t g_cg_dobj_get_world_tag_pos = 0;
std::uintptr_t g_cg_dobj_get_world_tag_matrix = 0;
std::uintptr_t g_original_fire_rocket = 0;
std::uintptr_t g_camera_origin_address = 0;
std::uintptr_t g_camera_axis_address = 0;
std::uintptr_t g_cgame_gun_pitch_address = 0;
std::uintptr_t g_cgame_gun_yaw_address = 0;
std::uintptr_t g_viewmodel_axis_address = 0;
std::uintptr_t g_viewmodel_axis_origin_address = 0;
std::uintptr_t g_viewmodel_pose_address = 0;
std::array<GripTagBinding, 4> g_grip_tags{};
std::array<ScopeTagBinding, 4> g_scope_tags{{
    {"tag_scope_rear", 0, false},
    {"tag_scope_rear_lid_animate", 0, false},
    {"tag_scope", 0, true},
    {"tag_scope_animate", 0, true},
}};
std::uintptr_t g_tag_brass_address = 0;
std::uintptr_t g_tag_flash_address = 0;
std::uintptr_t g_local_player_entity_address = 0;
std::uintptr_t g_weapon_definition_pointer_table_address = 0;
std::uintptr_t g_weapon_definition_count_address = 0;
std::uintptr_t g_fx_marks_no_marks_address = 0;
std::uintptr_t g_fx_marks_allocated_count_address = 0;
std::uintptr_t g_fx_marks_freed_count_address = 0;
std::array<WeaponAttachmentState, wawvr::xr::kHandCount>
    g_weapon_attachments{};
RightRayTwoHandSteeringState g_right_ray_two_hand_steering{};
WeaponAttachmentState g_two_hand_attachment{};
RetainedWeaponPose g_retained_weapon_pose{};
wawvr::xr::Posef g_weapon_attachment_anchor{};
bool g_weapon_attachment_anchor_valid = false;
thread_local ActiveWeaponPoseContext g_active_weapon_pose{};
thread_local ActiveHandsContext g_active_hands{};
thread_local HeadRelativeWeaponFreezeState
    g_head_relative_weapon_freeze{};
thread_local ChestWeaponPoseState g_chest_weapon_pose_state{};
thread_local WeaponGripState g_weapon_grip_state{};
thread_local CommittedWeaponGripState g_committed_weapon_grip{};
thread_local std::uint64_t g_held_pose_failure_since_milliseconds = 0;
thread_local MosinImpactCorrelation g_mosin_impact_correlation{};
thread_local PostT4VisibleAimDiagnosticState
    g_post_t4_visible_aim_diagnostic{};
thread_local std::uint32_t g_post_t4_visible_aim_report_count = 0;
thread_local WeaponPosePipelineTraceState
    g_weapon_pose_pipeline_trace{};
thread_local PendingPlayerBazookaRocket g_pending_player_bazooka_rocket{};
thread_local std::array<std::uint32_t, 4>
    g_weapon_pose_pipeline_report_counts{};
SRWLOCK g_final_visible_aim_lock = SRWLOCK_INIT;
FinalVisibleAim g_final_visible_aim{};
SRWLOCK g_weapon_frame_base_receipt_lock = SRWLOCK_INIT;
WeaponFrameBaseReceipt g_weapon_frame_base_receipt{};
SRWLOCK g_published_muzzle_lock = SRWLOCK_INIT;
PublishedWeaponMuzzleSnapshot g_published_muzzle{};
thread_local std::array<float, 3> g_campaign_rocket_angles{};

[[nodiscard]] WeaponAttachmentState* weapon_attachment_for_mode(
    const WeaponGripMode mode) noexcept {
    switch (mode) {
    case WeaponGripMode::RightHand:
        return &g_weapon_attachments[static_cast<std::size_t>(
            wawvr::xr::Hand::Right)];
    case WeaponGripMode::LeftHand:
        return &g_weapon_attachments[static_cast<std::size_t>(
            wawvr::xr::Hand::Left)];
    case WeaponGripMode::TwoHand:
        return &g_two_hand_attachment;
    case WeaponGripMode::Chest:
    default:
        return nullptr;
    }
}

[[nodiscard]] bool retained_weapon_pose_matches_committed(
    const std::uint64_t weapon_identity) noexcept {
    return g_retained_weapon_pose.valid && weapon_identity != 0 &&
        g_retained_weapon_pose.weapon_identity == weapon_identity &&
        g_committed_weapon_grip.weapon_identity == weapon_identity &&
        g_retained_weapon_pose.grip_mode ==
            g_committed_weapon_grip.mode &&
        g_retained_weapon_pose.controller_generation != 0;
}

[[nodiscard]] bool hand_weapon_pose_usable(
    const wawvr::xr::HandActionState& hand) noexcept {
    return hand.grip.active && hand.grip.position_valid &&
        hand.aim.active && hand.aim.orientation_valid;
}

[[nodiscard]] const float* preserve_native_campaign_angles(
    const float* const native_angles,
    const char* const reason) noexcept {
    static std::atomic_flag rejected_once = ATOMIC_FLAG_INIT;
    stereo_diagnostic_log_once(
        rejected_once,
        "CampaignDiag rocket-angle substitution rejected: %s; native angles preserved",
        reason);
    return native_angles;
}

void log_campaign_angle_acceptance_once(
    const std::uint32_t weapon_id,
    const std::string_view weapon_name,
    const float pitch_degrees,
    const float yaw_degrees) noexcept {
    static std::atomic_flag accepted_once = ATOMIC_FLAG_INIT;
    stereo_diagnostic_log_once(
        accepted_once,
        "CampaignDiag rocket-angle substitution accepted: exactSpProfile=1 localPlayer=0 weaponId=%u asset=%.*s pitch=%.3f yaw=%.3f",
        weapon_id, static_cast<int>(weapon_name.size()), weapon_name.data(),
        pitch_degrees, yaw_degrees);
}

[[nodiscard]] bool weapon_hook_disabled_by_environment() noexcept {
    std::array<wchar_t, 8> value{};
    const DWORD length = GetEnvironmentVariableW(
        L"WAWVR_DISABLE_WEAPON", value.data(),
        static_cast<DWORD>(value.size()));
    return length == 1 && value[0] == L'1';
}

[[nodiscard]] bool accessible_range(
    const void* const address,
    const std::size_t size,
    const bool writable) noexcept {
    return resident_page_access(address, size, writable,
        &grouped_virtual_query<VirtualQueryTimingGroup::weapon_hands>);
}

[[nodiscard]] bool bazooka_visual_alignment_disabled_by_environment() noexcept {
    static const bool disabled = []() noexcept {
        std::array<wchar_t, 2> value{};
        return GetEnvironmentVariableW(
                   L"WAWVR_DISABLE_BAZOOKA_VISUAL_ALIGNMENT", value.data(),
                   static_cast<DWORD>(value.size())) == 1 &&
            value[0] == L'1';
    }();
    return disabled;
}

[[nodiscard]] bool head_relative_weapon_freeze_enabled() noexcept {
    static const bool enabled = []() noexcept {
        std::array<wchar_t, 2> value{};
        return GetEnvironmentVariableW(
                   L"WAWVR_HEAD_RELATIVE_WEAPON_FREEZE", value.data(),
                   static_cast<DWORD>(value.size())) == 1 &&
            value[0] == L'1';
    }();
    return enabled;
}

[[nodiscard]] bool current_head_local_weapon_enabled() noexcept {
    static const bool enabled = []() noexcept {
        std::array<wchar_t, 2> value{};
        return GetEnvironmentVariableW(
                   L"WAWVR_CURRENT_HEAD_LOCAL_WEAPON", value.data(),
                   static_cast<DWORD>(value.size())) == 1 &&
            value[0] == L'1';
    }();
    return enabled;
}

[[nodiscard]] bool weapon_pose_pipeline_trace_enabled() noexcept {
    static const bool enabled = []() noexcept {
        std::array<wchar_t, 2> value{};
        return GetEnvironmentVariableW(
                   L"WAWVR_WEAPON_PIPELINE_TRACE", value.data(),
                   static_cast<DWORD>(value.size())) == 1 &&
            value[0] == L'1';
    }();
    return enabled;
}

[[nodiscard]] bool post_t4_visible_aim_diagnostics_enabled() noexcept {
    static const bool enabled = []() noexcept {
        const DWORD previous_error = GetLastError();
        std::array<wchar_t, 2> value{};
        const bool requested = GetEnvironmentVariableW(
            L"WAWVR_POST_T4_VISIBLE_AIM_DIAGNOSTICS", value.data(),
            static_cast<DWORD>(value.size())) == 1 && value[0] == L'1';
        SetLastError(previous_error);
        return requested;
    }();
    return enabled;
}

[[nodiscard]] bool mosin_bullet_diagnostics_enabled() noexcept {
    // These trace/impact callbacks only collect development evidence. They
    // used to resolve every campaign bullet's registered weapon and name,
    // even after the log caps were exhausted. Keep them entirely out of the
    // normal firing path; origin, spread, recoil and reload hooks are separate.
    static const bool enabled = []() noexcept {
        std::array<wchar_t, 2> value{};
        return GetEnvironmentVariableW(
                   L"WAWVR_MOSIN_BULLET_DIAGNOSTICS", value.data(),
                   static_cast<DWORD>(value.size())) == 1 &&
            value[0] == L'1';
    }();
    return enabled;
}

[[nodiscard]] bool compose_snapshot_head_world_pose(
    const ControllerFrameSnapshot& snapshot,
    const wawvr::xr::Vec3f& camera_origin,
    const wawvr::xr::Basis3f& camera_axis,
    wawvr::xr::EnginePose* const head_world_pose) noexcept {
    return compose_head_world_pose(
        {camera_origin, camera_axis}, snapshot.frame.head_center,
        snapshot.tracking_anchor, wawvr::xr::kIwUnitsPerMeter,
        head_world_pose);
}

class ScopedDObjSkeletonLock final {
public:
    ScopedDObjSkeletonLock() = default;
    ScopedDObjSkeletonLock(const ScopedDObjSkeletonLock&) = delete;
    ScopedDObjSkeletonLock& operator=(const ScopedDObjSkeletonLock&) = delete;

    ~ScopedDObjSkeletonLock() { Release(); }

    [[nodiscard]] bool TryAcquire(void* const dobj) noexcept {
        if (lock_ != nullptr || dobj == nullptr) {
            return false;
        }
        auto* const candidate = reinterpret_cast<volatile LONG*>(
            static_cast<std::uint8_t*>(dobj) +
            kDObjSkeletonLockOffset);
        if (InterlockedCompareExchange(candidate, 1, 0) != 0) {
            return false;
        }
        lock_ = candidate;
        return true;
    }

    void Release() noexcept {
        if (lock_ != nullptr) {
            InterlockedExchange(lock_, 0);
            lock_ = nullptr;
        }
    }

private:
    volatile LONG* lock_{};
};

template <std::size_t Capacity>
[[nodiscard]] bool copy_readable_bounded_c_string(
    const std::uintptr_t address,
    std::array<char, Capacity>* const output) noexcept {
    WeaponIdentitySnapshotReader reader;
    return reader.copy_c_string(address, output);
}

[[nodiscard]] bool bytes_match(
    const void* const address,
    const std::span<const std::uint8_t> expected) noexcept {
    return accessible_range(address, expected.size(), false) &&
           std::memcmp(address, expected.data(), expected.size()) == 0;
}

[[nodiscard]] bool make_relative_call(
    const std::uintptr_t source,
    const std::uintptr_t destination,
    std::array<std::uint8_t, kCallInstructionSize>* const output) noexcept {
    if (output == nullptr) {
        return false;
    }
    const std::int64_t displacement =
        static_cast<std::int64_t>(destination) -
        static_cast<std::int64_t>(source + kCallInstructionSize);
    if (displacement < std::numeric_limits<std::int32_t>::min() ||
        displacement > std::numeric_limits<std::int32_t>::max()) {
        return false;
    }
    (*output)[0] = 0xE8;
    const auto encoded = static_cast<std::int32_t>(displacement);
    std::memcpy(output->data() + 1, &encoded, sizeof(encoded));
    return true;
}

[[nodiscard]] std::uintptr_t decode_relative_call_target(
    const std::uintptr_t source,
    const std::array<std::uint8_t, kCallInstructionSize>& bytes) noexcept {
    std::int32_t displacement = 0;
    std::memcpy(&displacement, bytes.data() + 1, sizeof(displacement));
    return static_cast<std::uintptr_t>(
        static_cast<std::int64_t>(source + kCallInstructionSize) +
        displacement);
}

[[nodiscard]] float normalize_degrees(float value) noexcept {
    value = std::fmod(value, 360.0F);
    if (value < 0.0F) {
        value += 360.0F;
    }
    return value;
}

[[nodiscard]] bool same_pose(
    const wawvr::xr::Posef& left,
    const wawvr::xr::Posef& right) noexcept {
    return left.orientation.x == right.orientation.x &&
           left.orientation.y == right.orientation.y &&
           left.orientation.z == right.orientation.z &&
           left.orientation.w == right.orientation.w &&
           left.position.x == right.position.x &&
           left.position.y == right.position.y &&
           left.position.z == right.position.z;
}

[[nodiscard]] bool aim_degrees_from_forward(
    wawvr::xr::Vec3f forward,
    float* const pitch,
    float* const yaw) noexcept {
    if (pitch == nullptr || yaw == nullptr || !std::isfinite(forward.x) ||
        !std::isfinite(forward.y) || !std::isfinite(forward.z)) {
        return false;
    }
    const float length_squared =
        forward.x * forward.x + forward.y * forward.y +
        forward.z * forward.z;
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-8F) {
        return false;
    }
    const float inverse_length = 1.0F / std::sqrt(length_squared);
    forward.x *= inverse_length;
    forward.y *= inverse_length;
    forward.z *= inverse_length;
    constexpr float kRadiansToDegrees =
        180.0F / 3.14159265358979323846F;
    const float horizontal =
        std::sqrt(forward.x * forward.x + forward.y * forward.y);
    *pitch = normalize_degrees(
        std::atan2(-forward.z, horizontal) * kRadiansToDegrees);
    *yaw = normalize_degrees(
        std::atan2(forward.y, forward.x) * kRadiansToDegrees);
    return std::isfinite(*pitch) && std::isfinite(*yaw);
}

[[nodiscard]] bool finite_basis(
    const wawvr::xr::Basis3f& axis) noexcept {
    return std::isfinite(axis.forward.x) &&
           std::isfinite(axis.forward.y) &&
           std::isfinite(axis.forward.z) &&
           std::isfinite(axis.left.x) && std::isfinite(axis.left.y) &&
           std::isfinite(axis.left.z) && std::isfinite(axis.up.x) &&
           std::isfinite(axis.up.y) && std::isfinite(axis.up.z);
}

[[nodiscard]] bool finite_post_t4_diagnostic_vector(
    const wawvr::xr::Vec3f& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] wawvr::xr::Vec3f compose_post_t4_diagnostic_vector(
    const wawvr::xr::Basis3f& basis,
    const wawvr::xr::Vec3f& local) noexcept {
    return {
        local.x * basis.forward.x + local.y * basis.left.x +
            local.z * basis.up.x,
        local.x * basis.forward.y + local.y * basis.left.y +
            local.z * basis.up.y,
        local.x * basis.forward.z + local.y * basis.left.z +
            local.z * basis.up.z,
    };
}

[[nodiscard]] wawvr::xr::Vec3f project_post_t4_diagnostic_vector(
    const wawvr::xr::Basis3f& basis,
    const wawvr::xr::Vec3f& world) noexcept {
    return {
        world.x * basis.forward.x + world.y * basis.forward.y +
            world.z * basis.forward.z,
        world.x * basis.left.x + world.y * basis.left.y +
            world.z * basis.left.z,
        world.x * basis.up.x + world.y * basis.up.y +
            world.z * basis.up.z,
    };
}

[[nodiscard]] float post_t4_diagnostic_vector_step(
    const wawvr::xr::Vec3f& previous,
    const wawvr::xr::Vec3f& current) noexcept {
    const float x = current.x - previous.x;
    const float y = current.y - previous.y;
    const float z = current.z - previous.z;
    return std::sqrt(x * x + y * y + z * z);
}

[[nodiscard]] bool normalize_post_t4_diagnostic_direction(
    wawvr::xr::Vec3f direction,
    wawvr::xr::Vec3f* const normalized,
    float* const original_length = nullptr) noexcept {
    if (normalized == nullptr || !std::isfinite(direction.x) ||
        !std::isfinite(direction.y) || !std::isfinite(direction.z)) {
        return false;
    }
    const float length_squared = direction.x * direction.x +
        direction.y * direction.y + direction.z * direction.z;
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-8F) {
        return false;
    }
    const float length = std::sqrt(length_squared);
    const float inverse_length = 1.0F / length;
    direction.x *= inverse_length;
    direction.y *= inverse_length;
    direction.z *= inverse_length;
    if (!std::isfinite(direction.x) || !std::isfinite(direction.y) ||
        !std::isfinite(direction.z)) {
        return false;
    }
    *normalized = direction;
    if (original_length != nullptr) {
        *original_length = length;
    }
    return true;
}

[[nodiscard]] float post_t4_diagnostic_angle_degrees(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    const float dot = std::clamp(
        left.x * right.x + left.y * right.y + left.z * right.z,
        -1.0F, 1.0F);
    constexpr float kRadiansToDegrees =
        180.0F / 3.14159265358979323846F;
    return std::acos(dot) * kRadiansToDegrees;
}

void add_post_t4_visible_aim_metric(
    PostT4VisibleAimMetric* const metric,
    const float value) noexcept {
    if (metric == nullptr || !std::isfinite(value) || value < 0.0F) {
        return;
    }
    ++metric->count;
    metric->sum += static_cast<double>(value);
    metric->sum_squared +=
        static_cast<double>(value) * static_cast<double>(value);
    metric->minimum = (std::min)(metric->minimum, value);
    metric->maximum = (std::max)(metric->maximum, value);
}

[[nodiscard]] float post_t4_visible_aim_metric_mean(
    const PostT4VisibleAimMetric& metric) noexcept {
    return metric.count == 0
        ? 0.0F
        : static_cast<float>(metric.sum / metric.count);
}

[[nodiscard]] float post_t4_visible_aim_metric_rms(
    const PostT4VisibleAimMetric& metric) noexcept {
    return metric.count == 0
        ? 0.0F
        : static_cast<float>(
              std::sqrt(metric.sum_squared / metric.count));
}

[[nodiscard]] bool post_t4_production_controller_axis_world(
    const ActiveWeaponPoseContext& context,
    wawvr::xr::Basis3f* const controller_axis_world) noexcept {
    if (controller_axis_world == nullptr ||
        !finite_basis(context.controller_camera_axis) ||
        !finite_basis(context.controller_pose.aim_axis)) {
        return false;
    }

    const auto compose_camera_local =
        [&context](const wawvr::xr::Vec3f& local) noexcept {
            return wawvr::xr::Vec3f{
                local.x * context.controller_camera_axis.forward.x +
                    local.y * context.controller_camera_axis.left.x +
                    local.z * context.controller_camera_axis.up.x,
                local.x * context.controller_camera_axis.forward.y +
                    local.y * context.controller_camera_axis.left.y +
                    local.z * context.controller_camera_axis.up.y,
                local.x * context.controller_camera_axis.forward.z +
                    local.y * context.controller_camera_axis.left.z +
                    local.z * context.controller_camera_axis.up.z,
            };
        };
    const wawvr::xr::Basis3f calculated{
        compose_camera_local(context.controller_pose.aim_axis.forward),
        compose_camera_local(context.controller_pose.aim_axis.left),
        compose_camera_local(context.controller_pose.aim_axis.up),
    };
    wawvr::xr::Quaternionf ignored{};
    if (!finite_basis(calculated) ||
        !iw_axis_to_unit_quaternion(calculated, &ignored)) {
        return false;
    }
    *controller_axis_world = calculated;
    return true;
}

[[nodiscard]] const char* weapon_pose_pipeline_grip_mode_name(
    const WeaponGripMode mode) noexcept {
    switch (mode) {
    case WeaponGripMode::RightHand:
        return "right";
    case WeaponGripMode::LeftHand:
        return "left";
    case WeaponGripMode::TwoHand:
        return "two-hand";
    case WeaponGripMode::Chest:
    default:
        return "chest";
    }
}

[[nodiscard]] PerformanceTimingPhase weapon_bridge_timing_phase(
    const WeaponGripMode mode) noexcept {
    switch (mode) {
    case WeaponGripMode::RightHand:
        return PerformanceTimingPhase::weapon_bridge_right;
    case WeaponGripMode::LeftHand:
        return PerformanceTimingPhase::weapon_bridge_left;
    case WeaponGripMode::TwoHand:
        return PerformanceTimingPhase::weapon_bridge_two_hand;
    case WeaponGripMode::Chest:
    default:
        return PerformanceTimingPhase::weapon_bridge_chest;
    }
}

[[nodiscard]] WeaponPosePipelineTracePose weapon_pose_pipeline_pose(
    const ControllerWeaponPose& pose,
    const bool valid) noexcept {
    return {
        .valid = valid,
        .position = pose.grip_position,
        .axis = pose.aim_axis,
    };
}

void observe_weapon_pose_pipeline_diagnostic(
    const ActiveWeaponPoseContext& context,
    const ActiveHandsContext& hands,
    const wawvr::xr::Vec3f& evaluated_grip_world,
    const wawvr::xr::Vec3f& evaluated_flash_world,
    const bool evaluated_flash_valid) noexcept {
    if (!weapon_pose_pipeline_trace_enabled()) {
        return;
    }
    if (!context.valid || !hands.valid ||
        context.controller_generation == 0 ||
        context.controller_generation != hands.controller.generation) {
        reset_weapon_pose_pipeline_trace(&g_weapon_pose_pipeline_trace);
        return;
    }

    const std::size_t mode_index = static_cast<std::size_t>(
        context.grip_mode);
    constexpr std::uint32_t kMaximumReportsPerMode = 4;
    if (mode_index >= g_weapon_pose_pipeline_report_counts.size() ||
        g_weapon_pose_pipeline_report_counts[mode_index] >=
            kMaximumReportsPerMode) {
        return;
    }

    const std::uint64_t now_milliseconds = GetTickCount64();
    ControllerWeaponPose raw_right{};
    ControllerWeaponPose filtered_right{};
    ControllerWeaponPose raw_left{};
    ControllerWeaponPose filtered_left{};
    const bool raw_right_ready = context.right_gripping &&
        controller_weapon_pose(
            hands.controller, wawvr::xr::Hand::Right,
            now_milliseconds, &raw_right);
    const bool filtered_right_ready = context.right_gripping &&
        published_controller_weapon_pose(
            hands.controller, wawvr::xr::Hand::Right,
            now_milliseconds, &filtered_right);
    const bool raw_left_ready = context.left_gripping &&
        controller_weapon_pose(
            hands.controller, wawvr::xr::Hand::Left,
            now_milliseconds, &raw_left);
    const bool filtered_left_ready = context.left_gripping &&
        published_controller_weapon_pose(
            hands.controller, wawvr::xr::Hand::Left,
            now_milliseconds, &filtered_left);

    WeaponPosePipelineTraceDirection visible_direction{};
    if (evaluated_flash_valid) {
        visible_direction.valid = normalize_post_t4_diagnostic_direction(
            {
                evaluated_flash_world.x - evaluated_grip_world.x,
                evaluated_flash_world.y - evaluated_grip_world.y,
                evaluated_flash_world.z - evaluated_grip_world.z,
            },
            &visible_direction.direction);
    }

    WeaponPosePipelineTraceDirection pre_final_direction{};
    WeaponPosePipelineTraceDirection post_final_direction{};
    if (context.grip_mode == WeaponGripMode::TwoHand &&
        filtered_right_ready && raw_right_ready && raw_left_ready) {
        ControllerWeaponPose absolute_pair{};
        if (two_hand_controller_weapon_pose_from_raw_grip_delta(
                filtered_right, raw_right, raw_left, &absolute_pair)) {
            pre_final_direction = {
                .valid = true,
                .direction = absolute_pair.aim_axis.forward,
            };
        }
    }
    if (context.grip_mode == WeaponGripMode::TwoHand &&
        g_right_ray_two_hand_steering.valid &&
        g_right_ray_two_hand_steering.generation ==
            context.controller_generation) {
        post_final_direction = {
            .valid = true,
            .direction =
                g_right_ray_two_hand_steering.cached_pose.aim_axis.forward,
        };
    }

    const WeaponPosePipelineTraceInput input{
        .grip_mode = context.grip_mode,
        .weapon_identity = context.weapon_identity,
        .transition_pending = context.transition_pending,
        .generation = context.controller_generation,
        .frame_id = hands.controller.frame.frame_id,
        .publication_milliseconds =
            context.controller_publication_milliseconds,
        .render_milliseconds = now_milliseconds,
        .predicted_display_time_nanoseconds =
            hands.controller.frame.predicted_display_time,
        .predicted_display_period_nanoseconds =
            hands.controller.frame.predicted_display_period,
        .raw_right = weapon_pose_pipeline_pose(
            raw_right, raw_right_ready),
        .filtered_right = weapon_pose_pipeline_pose(
            filtered_right, filtered_right_ready),
        .raw_left = weapon_pose_pipeline_pose(raw_left, raw_left_ready),
        .filtered_left = weapon_pose_pipeline_pose(
            filtered_left, filtered_left_ready),
        .pre_final_two_hand_direction = pre_final_direction,
        .post_final_two_hand_direction = post_final_direction,
        .applied_weapon = {
            .valid = true,
            .position = context.weapon_origin,
            .axis = context.weapon_axis,
        },
        .visible_weapon_direction = visible_direction,
    };
    WeaponPosePipelineTraceReport report{};
    if (!observe_weapon_pose_pipeline_trace(
            input, &g_weapon_pose_pipeline_trace, &report) ||
        !report.valid) {
        return;
    }

    ++g_weapon_pose_pipeline_report_counts[mode_index];
    const auto& window = report.window;
    const auto minimum = [](const WeaponPosePipelineTraceMetric& metric) {
        return metric.count == 0 ? 0.0F : metric.minimum;
    };
    stereo_diagnostic_log(
        "WeaponDiag pipeline cadence mode=%s window=%u weapon=%llu generations=%llu-%llu publications=%u renderCalls=%u duplicateRenderCalls=%u renderDtMeanRange=%.3f/%.3f-%.3fms publicationDtMeanRange=%.3f/%.3f-%.3fms predictedStepMeanRange=%.3f/%.3f-%.3fms predictedPeriodMeanRange=%.3f/%.3f-%.3fms publicationAgeMeanRange=%.3f/%.3f-%.3fms",
        weapon_pose_pipeline_grip_mode_name(window.grip_mode),
        g_weapon_pose_pipeline_report_counts[mode_index] - 1,
        static_cast<unsigned long long>(window.weapon_identity),
        static_cast<unsigned long long>(window.first_generation),
        static_cast<unsigned long long>(window.last_generation),
        window.unique_publications, window.render_calls,
        window.duplicate_render_calls,
        weapon_pose_pipeline_trace_metric_mean(
            window.render_delta_milliseconds),
        minimum(window.render_delta_milliseconds),
        window.render_delta_milliseconds.maximum,
        weapon_pose_pipeline_trace_metric_mean(
            window.publication_delta_milliseconds),
        minimum(window.publication_delta_milliseconds),
        window.publication_delta_milliseconds.maximum,
        weapon_pose_pipeline_trace_metric_mean(
            window.predicted_display_step_milliseconds),
        minimum(window.predicted_display_step_milliseconds),
        window.predicted_display_step_milliseconds.maximum,
        weapon_pose_pipeline_trace_metric_mean(
            window.predicted_display_period_milliseconds),
        minimum(window.predicted_display_period_milliseconds),
        window.predicted_display_period_milliseconds.maximum,
        weapon_pose_pipeline_trace_metric_mean(
            window.publication_to_render_age_milliseconds),
        minimum(window.publication_to_render_age_milliseconds),
        window.publication_to_render_age_milliseconds.maximum);
    stereo_diagnostic_log(
        "WeaponDiag pipeline hands mode=%s window=%u rawRPos/AxisStepRmsMax=%.3f/%.3fmm,%.4f/%.4fdeg filteredRPos/AxisStepRmsMax=%.3f/%.3fmm,%.4f/%.4fdeg rawFilteredRErrorMeanMax=%.3f/%.3fmm,%.4f/%.4fdeg rawLPos/AxisStepRmsMax=%.3f/%.3fmm,%.4f/%.4fdeg filteredLPos/AxisStepRmsMax=%.3f/%.3fmm,%.4f/%.4fdeg rawFilteredLErrorMeanMax=%.3f/%.3fmm,%.4f/%.4fdeg",
        weapon_pose_pipeline_grip_mode_name(window.grip_mode),
        g_weapon_pose_pipeline_report_counts[mode_index] - 1,
        weapon_pose_pipeline_trace_metric_rms(
            window.raw_right.position_step_millimeters),
        window.raw_right.position_step_millimeters.maximum,
        weapon_pose_pipeline_trace_metric_rms(
            window.raw_right.orientation_step_degrees),
        window.raw_right.orientation_step_degrees.maximum,
        weapon_pose_pipeline_trace_metric_rms(
            window.filtered_right.position_step_millimeters),
        window.filtered_right.position_step_millimeters.maximum,
        weapon_pose_pipeline_trace_metric_rms(
            window.filtered_right.orientation_step_degrees),
        window.filtered_right.orientation_step_degrees.maximum,
        weapon_pose_pipeline_trace_metric_mean(
            window.right_raw_to_filtered.position_error_millimeters),
        window.right_raw_to_filtered.position_error_millimeters.maximum,
        weapon_pose_pipeline_trace_metric_mean(
            window.right_raw_to_filtered.orientation_error_degrees),
        window.right_raw_to_filtered.orientation_error_degrees.maximum,
        weapon_pose_pipeline_trace_metric_rms(
            window.raw_left.position_step_millimeters),
        window.raw_left.position_step_millimeters.maximum,
        weapon_pose_pipeline_trace_metric_rms(
            window.raw_left.orientation_step_degrees),
        window.raw_left.orientation_step_degrees.maximum,
        weapon_pose_pipeline_trace_metric_rms(
            window.filtered_left.position_step_millimeters),
        window.filtered_left.position_step_millimeters.maximum,
        weapon_pose_pipeline_trace_metric_rms(
            window.filtered_left.orientation_step_degrees),
        window.filtered_left.orientation_step_degrees.maximum,
        weapon_pose_pipeline_trace_metric_mean(
            window.left_raw_to_filtered.position_error_millimeters),
        window.left_raw_to_filtered.position_error_millimeters.maximum,
        weapon_pose_pipeline_trace_metric_mean(
            window.left_raw_to_filtered.orientation_error_degrees),
        window.left_raw_to_filtered.orientation_error_degrees.maximum);
    stereo_diagnostic_log(
        "WeaponDiag pipeline solve mode=%s window=%u pairPre/PostStepRmsMax=%.4f/%.4f,%.4f/%.4fdeg prePostErrorMeanMax=%.4f/%.4fdeg appliedPos/AxisStepRmsMax=%.3f/%.3fmm,%.4f/%.4fdeg visibleStepRmsMax=%.4f/%.4fdeg samePublicationVisibleStepRmsMax=%.4f/%.4fdeg",
        weapon_pose_pipeline_grip_mode_name(window.grip_mode),
        g_weapon_pose_pipeline_report_counts[mode_index] - 1,
        weapon_pose_pipeline_trace_metric_rms(
            window.pre_final_two_hand_direction_step_degrees),
        window.pre_final_two_hand_direction_step_degrees.maximum,
        weapon_pose_pipeline_trace_metric_rms(
            window.post_final_two_hand_direction_step_degrees),
        window.post_final_two_hand_direction_step_degrees.maximum,
        weapon_pose_pipeline_trace_metric_mean(
            window.pre_to_post_two_hand_error_degrees),
        window.pre_to_post_two_hand_error_degrees.maximum,
        weapon_pose_pipeline_trace_metric_rms(
            window.applied_weapon.position_step_millimeters),
        window.applied_weapon.position_step_millimeters.maximum,
        weapon_pose_pipeline_trace_metric_rms(
            window.applied_weapon.orientation_step_degrees),
        window.applied_weapon.orientation_step_degrees.maximum,
        weapon_pose_pipeline_trace_metric_rms(
            window.visible_weapon_direction_step_degrees),
        window.visible_weapon_direction_step_degrees.maximum,
        weapon_pose_pipeline_trace_metric_rms(
            window.same_publication_visible_direction_step_degrees),
        window.same_publication_visible_direction_step_degrees.maximum);
}

void reset_post_t4_visible_aim_diagnostic() noexcept {
    g_post_t4_visible_aim_diagnostic = {};
}

void observe_post_t4_visible_aim_diagnostic(
    const ActiveWeaponPoseContext& context,
    const ActiveHandsContext& hands,
    const wawvr::xr::Vec3f& evaluated_grip_world,
    const wawvr::xr::Vec3f& evaluated_flash_world,
    const bool evaluated_flash_valid) noexcept {
    // This retired aiming investigation is separate from muzzle publication
    // and pose ownership. Keep its calculations and synchronous aggregate
    // logging out of normal play; only these diagnostic counters/state depend
    // on it. The flag is cached, so disabled frames do no environment reads.
    if (!post_t4_visible_aim_diagnostics_enabled()) {
        return;
    }
    // This probe is intentionally measurement-only and bounded. It observes
    // only steady, fully tracked two-hand generations, emits twenty-four aggregate
    // windows at most, and never feeds any measured value back into placement.
    constexpr std::uint32_t kComparisonsPerReport = 120;
    constexpr std::uint32_t kMaximumReports = 24;
    constexpr std::uint64_t kMaximumGenerationGap = 16;
    constexpr std::uint64_t kMaximumPublicationGapMilliseconds = 250;
    if (g_post_t4_visible_aim_report_count >= kMaximumReports) {
        return;
    }
    if (!context.valid || !hands.valid ||
        context.grip_mode != WeaponGripMode::TwoHand ||
        !context.right_gripping || !context.left_gripping ||
        context.transition_pending || context.controller_generation == 0 ||
        context.controller_publication_milliseconds == 0 ||
        !evaluated_flash_valid || !finite_basis(context.weapon_axis) ||
        !context.weapon_attachment_axis_valid ||
        !finite_basis(context.weapon_attachment_axis)) {
        reset_post_t4_visible_aim_diagnostic();
        return;
    }

    auto& state = g_post_t4_visible_aim_diagnostic;
    if (state.valid &&
        state.weapon_identity == context.weapon_identity &&
        state.last_generation == context.controller_generation) {
        return;
    }

    wawvr::xr::Basis3f production_controller_world_axis{};
    wawvr::xr::Vec3f raw_pair_forward{};
    wawvr::xr::Vec3f applied_forward{};
    wawvr::xr::Vec3f raw_pair_up{};
    wawvr::xr::Vec3f applied_up{};
    wawvr::xr::Vec3f visible_forward{};
    float grip_to_flash_length = 0.0F;
    if (!post_t4_production_controller_axis_world(
            context, &production_controller_world_axis) ||
        !normalize_post_t4_diagnostic_direction(
            production_controller_world_axis.forward, &raw_pair_forward) ||
        !normalize_post_t4_diagnostic_direction(
            context.weapon_axis.forward, &applied_forward) ||
        !normalize_post_t4_diagnostic_direction(
            production_controller_world_axis.up, &raw_pair_up) ||
        !normalize_post_t4_diagnostic_direction(
            context.weapon_axis.up, &applied_up) ||
        !normalize_post_t4_diagnostic_direction(
            {
                evaluated_flash_world.x - evaluated_grip_world.x,
                evaluated_flash_world.y - evaluated_grip_world.y,
                evaluated_flash_world.z - evaluated_grip_world.z,
            },
            &visible_forward, &grip_to_flash_length)) {
        reset_post_t4_visible_aim_diagnostic();
        return;
    }

    // Expressing the evaluated barrel in the applied weapon basis removes all
    // controller/root rotation. Any remaining frame-to-frame step is movement
    // inside the evaluated T4 model (animation, tag motion, or deformation).
    wawvr::xr::Vec3f visible_in_weapon_basis{};
    if (!normalize_post_t4_diagnostic_direction(
            {
                visible_forward.x * context.weapon_axis.forward.x +
                    visible_forward.y * context.weapon_axis.forward.y +
                    visible_forward.z * context.weapon_axis.forward.z,
                visible_forward.x * context.weapon_axis.left.x +
                    visible_forward.y * context.weapon_axis.left.y +
                    visible_forward.z * context.weapon_axis.left.z,
                visible_forward.x * context.weapon_axis.up.x +
                    visible_forward.y * context.weapon_axis.up.y +
                    visible_forward.z * context.weapon_axis.up.z,
            },
            &visible_in_weapon_basis)) {
        reset_post_t4_visible_aim_diagnostic();
        return;
    }

    const wawvr::xr::EnginePose head_relative =
        wawvr::xr::OpenXrPoseToIwRelative(
            hands.controller.frame.head_center,
            hands.controller.tracking_anchor,
            wawvr::xr::kIwUnitsPerMeter);
    const wawvr::xr::Vec3f head_offset_world =
        compose_post_t4_diagnostic_vector(
            context.camera_axis, head_relative.position);
    const wawvr::xr::Vec3f head_world_origin{
        context.camera_origin.x + head_offset_world.x,
        context.camera_origin.y + head_offset_world.y,
        context.camera_origin.z + head_offset_world.z,
    };
    const wawvr::xr::Basis3f head_world_axis{
        compose_post_t4_diagnostic_vector(
            context.camera_axis, head_relative.axis.forward),
        compose_post_t4_diagnostic_vector(
            context.camera_axis, head_relative.axis.left),
        compose_post_t4_diagnostic_vector(
            context.camera_axis, head_relative.axis.up),
    };
    const wawvr::xr::Vec3f root_from_head{
        context.weapon_origin.x - head_world_origin.x,
        context.weapon_origin.y - head_world_origin.y,
        context.weapon_origin.z - head_world_origin.z,
    };
    const wawvr::xr::Vec3f root_from_grip{
        context.weapon_origin.x - context.tracked_grip_world.x,
        context.weapon_origin.y - context.tracked_grip_world.y,
        context.weapon_origin.z - context.tracked_grip_world.z,
    };
    const wawvr::xr::Vec3f visible_grip_from_root{
        evaluated_grip_world.x - context.weapon_origin.x,
        evaluated_grip_world.y - context.weapon_origin.y,
        evaluated_grip_world.z - context.weapon_origin.z,
    };
    const wawvr::xr::Vec3f visible_flash_from_root{
        evaluated_flash_world.x - context.weapon_origin.x,
        evaluated_flash_world.y - context.weapon_origin.y,
        evaluated_flash_world.z - context.weapon_origin.z,
    };
    const wawvr::xr::Vec3f head_relative_root =
        project_post_t4_diagnostic_vector(head_world_axis, root_from_head);
    const wawvr::xr::Vec3f grip_relative_root =
        project_post_t4_diagnostic_vector(
            production_controller_world_axis, root_from_grip);
    const wawvr::xr::Vec3f visible_grip_root =
        project_post_t4_diagnostic_vector(
            context.weapon_axis, visible_grip_from_root);
    const wawvr::xr::Vec3f visible_flash_root =
        project_post_t4_diagnostic_vector(
            context.weapon_axis, visible_flash_from_root);
    if (!finite_basis(head_world_axis) ||
        !finite_post_t4_diagnostic_vector(head_relative_root) ||
        !finite_post_t4_diagnostic_vector(grip_relative_root) ||
        !finite_post_t4_diagnostic_vector(visible_grip_root) ||
        !finite_post_t4_diagnostic_vector(visible_flash_root)) {
        reset_post_t4_visible_aim_diagnostic();
        return;
    }

    const PostT4AimPhaseInput head_local_phase_input{
        .generation = context.controller_generation,
        .frame_id = hands.controller.frame.frame_id,
        .publication_milliseconds =
            context.controller_publication_milliseconds,
        .head_pose = hands.controller.frame.head_center,
        .tracking_anchor = hands.controller.tracking_anchor,
        .body_axis = context.camera_axis,
        .production_controller_world_axis =
            production_controller_world_axis,
        .weapon_attachment_axis = context.weapon_attachment_axis,
        .visible_world_direction = visible_forward,
    };

    const bool discontinuity = state.valid &&
        (state.weapon_identity != context.weapon_identity ||
         context.controller_generation < state.last_generation ||
         context.controller_generation - state.last_generation >
             kMaximumGenerationGap ||
         context.controller_publication_milliseconds <
             state.last_publication_milliseconds ||
         context.controller_publication_milliseconds -
                 state.last_publication_milliseconds >
             kMaximumPublicationGapMilliseconds);
    if (!state.valid || discontinuity) {
        state = {};
        state.valid = true;
        state.weapon_identity = context.weapon_identity;
        state.first_generation = context.controller_generation;
        state.last_generation = context.controller_generation;
        state.last_publication_milliseconds =
            context.controller_publication_milliseconds;
        state.previous_raw_pair_forward = raw_pair_forward;
        state.previous_applied_forward = applied_forward;
        state.previous_raw_pair_up = raw_pair_up;
        state.previous_applied_up = applied_up;
        state.previous_visible_forward = visible_forward;
        state.previous_visible_in_weapon_basis = visible_in_weapon_basis;
        state.previous_head_relative_root = head_relative_root;
        state.previous_grip_relative_root = grip_relative_root;
        state.previous_visible_grip_root = visible_grip_root;
        state.previous_visible_flash_root = visible_flash_root;
        PostT4AimPhaseObservation ignored{};
        if (!update_post_t4_aim_phase(
                head_local_phase_input, &state.head_local_phase, &ignored)) {
            reset_post_t4_visible_aim_diagnostic();
        }
        return;
    }

    PostT4AimPhaseObservation head_local_phase_observation{};
    if (!update_post_t4_aim_phase(
            head_local_phase_input, &state.head_local_phase,
            &head_local_phase_observation)) {
        reset_post_t4_visible_aim_diagnostic();
        return;
    }

    add_post_t4_visible_aim_metric(
        &state.raw_pair_step_degrees,
        post_t4_diagnostic_angle_degrees(
            state.previous_raw_pair_forward, raw_pair_forward));
    add_post_t4_visible_aim_metric(
        &state.applied_step_degrees,
        post_t4_diagnostic_angle_degrees(
            state.previous_applied_forward, applied_forward));
    add_post_t4_visible_aim_metric(
        &state.raw_pair_up_step_degrees,
        post_t4_diagnostic_angle_degrees(
            state.previous_raw_pair_up, raw_pair_up));
    add_post_t4_visible_aim_metric(
        &state.applied_up_step_degrees,
        post_t4_diagnostic_angle_degrees(
            state.previous_applied_up, applied_up));
    add_post_t4_visible_aim_metric(
        &state.controller_applied_up_error_degrees,
        post_t4_diagnostic_angle_degrees(raw_pair_up, applied_up));
    add_post_t4_visible_aim_metric(
        &state.visible_step_degrees,
        post_t4_diagnostic_angle_degrees(
            state.previous_visible_forward, visible_forward));
    add_post_t4_visible_aim_metric(
        &state.native_residual_step_degrees,
        post_t4_diagnostic_angle_degrees(
            state.previous_visible_in_weapon_basis,
            visible_in_weapon_basis));
    add_post_t4_visible_aim_metric(
        &state.controller_applied_error_degrees,
        post_t4_diagnostic_angle_degrees(
            raw_pair_forward, applied_forward));
    add_post_t4_visible_aim_metric(
        &state.visible_applied_error_degrees,
        post_t4_diagnostic_angle_degrees(
            applied_forward, visible_forward));
    add_post_t4_visible_aim_metric(
        &state.grip_to_flash_length, grip_to_flash_length);
    add_post_t4_visible_aim_metric(
        &state.head_relative_root_step,
        post_t4_diagnostic_vector_step(
            state.previous_head_relative_root, head_relative_root));
    add_post_t4_visible_aim_metric(
        &state.grip_relative_root_step,
        post_t4_diagnostic_vector_step(
            state.previous_grip_relative_root, grip_relative_root));
    add_post_t4_visible_aim_metric(
        &state.visible_grip_root_step,
        post_t4_diagnostic_vector_step(
            state.previous_visible_grip_root, visible_grip_root));
    add_post_t4_visible_aim_metric(
        &state.visible_flash_root_step,
        post_t4_diagnostic_vector_step(
            state.previous_visible_flash_root, visible_flash_root));
    if (head_local_phase_observation.compared) {
        add_post_t4_visible_aim_metric(
            &state.head_local_lag0_error_degrees,
            head_local_phase_observation.lag0_error_degrees);
        add_post_t4_visible_aim_metric(
            &state.head_local_lag1_error_degrees,
            head_local_phase_observation.lag1_error_degrees);
        add_post_t4_visible_aim_metric(
            &state.publication_delta_milliseconds,
            head_local_phase_observation.publication_delta_milliseconds);
        ++state.head_local_phase_comparison_count;
        if (head_local_phase_observation.lag1_better) {
            ++state.head_local_lag1_better_count;
        }
    }
    ++state.comparison_count;
    state.last_generation = context.controller_generation;
    state.last_publication_milliseconds =
        context.controller_publication_milliseconds;
    state.previous_raw_pair_forward = raw_pair_forward;
    state.previous_applied_forward = applied_forward;
    state.previous_raw_pair_up = raw_pair_up;
    state.previous_applied_up = applied_up;
    state.previous_visible_forward = visible_forward;
    state.previous_visible_in_weapon_basis = visible_in_weapon_basis;
    state.previous_head_relative_root = head_relative_root;
    state.previous_grip_relative_root = grip_relative_root;
    state.previous_visible_grip_root = visible_grip_root;
    state.previous_visible_flash_root = visible_flash_root;

    if (state.comparison_count < kComparisonsPerReport) {
        return;
    }

    const float visible_error_minimum =
        state.visible_applied_error_degrees.count == 0
        ? 0.0F
        : state.visible_applied_error_degrees.minimum;
    const float barrel_length_minimum =
        state.grip_to_flash_length.count == 0
        ? 0.0F
        : state.grip_to_flash_length.minimum;
    const float publication_delta_minimum =
        state.publication_delta_milliseconds.count == 0
        ? 0.0F
        : state.publication_delta_milliseconds.minimum;
    stereo_diagnostic_log(
        "WeaponDiag post-T4 visible-axis window[%u] weapon=%llu generations=%llu-%llu n=%u productionControllerStepRmsMax=%.4f/%.4fdeg appliedStepRmsMax=%.4f/%.4fdeg productionUpStepRmsMax=%.4f/%.4fdeg appliedUpStepRmsMax=%.4f/%.4fdeg controllerAppliedUpErrorMeanMax=%.4f/%.4fdeg visibleStepRmsMax=%.4f/%.4fdeg nativeResidualStepRmsMax=%.4f/%.4fdeg controllerAppliedErrorMeanMax=%.4f/%.4fdeg visibleAppliedErrorMeanRange=%.4f/%.4f-%.4fdeg gripFlashLengthMeanRange=%.3f/%.3f-%.3f headRelativeRootStepRmsMax=%.4f/%.4fIW gripRelativeRootStepRmsMax=%.4f/%.4fIW visibleGripRootStepRmsMax=%.4f/%.4fIW visibleFlashRootStepRmsMax=%.4f/%.4fIW headLocalLag0RmsMax=%.4f/%.4fdeg headLocalLag1RmsMax=%.4f/%.4fdeg publicationDtMeanRange=%.2f/%.2f-%.2fms lag1Better=%u/%u",
        g_post_t4_visible_aim_report_count,
        static_cast<unsigned long long>(state.weapon_identity),
        static_cast<unsigned long long>(state.first_generation),
        static_cast<unsigned long long>(state.last_generation),
        state.comparison_count,
        post_t4_visible_aim_metric_rms(state.raw_pair_step_degrees),
        state.raw_pair_step_degrees.maximum,
        post_t4_visible_aim_metric_rms(state.applied_step_degrees),
        state.applied_step_degrees.maximum,
        post_t4_visible_aim_metric_rms(state.raw_pair_up_step_degrees),
        state.raw_pair_up_step_degrees.maximum,
        post_t4_visible_aim_metric_rms(state.applied_up_step_degrees),
        state.applied_up_step_degrees.maximum,
        post_t4_visible_aim_metric_mean(
            state.controller_applied_up_error_degrees),
        state.controller_applied_up_error_degrees.maximum,
        post_t4_visible_aim_metric_rms(state.visible_step_degrees),
        state.visible_step_degrees.maximum,
        post_t4_visible_aim_metric_rms(
            state.native_residual_step_degrees),
        state.native_residual_step_degrees.maximum,
        post_t4_visible_aim_metric_mean(
            state.controller_applied_error_degrees),
        state.controller_applied_error_degrees.maximum,
        post_t4_visible_aim_metric_mean(
            state.visible_applied_error_degrees),
        visible_error_minimum,
        state.visible_applied_error_degrees.maximum,
        post_t4_visible_aim_metric_mean(state.grip_to_flash_length),
        barrel_length_minimum,
        state.grip_to_flash_length.maximum,
        post_t4_visible_aim_metric_rms(state.head_relative_root_step),
        state.head_relative_root_step.maximum,
        post_t4_visible_aim_metric_rms(state.grip_relative_root_step),
        state.grip_relative_root_step.maximum,
        post_t4_visible_aim_metric_rms(state.visible_grip_root_step),
        state.visible_grip_root_step.maximum,
        post_t4_visible_aim_metric_rms(state.visible_flash_root_step),
        state.visible_flash_root_step.maximum,
        post_t4_visible_aim_metric_rms(
            state.head_local_lag0_error_degrees),
        state.head_local_lag0_error_degrees.maximum,
        post_t4_visible_aim_metric_rms(
            state.head_local_lag1_error_degrees),
        state.head_local_lag1_error_degrees.maximum,
        post_t4_visible_aim_metric_mean(
            state.publication_delta_milliseconds),
        publication_delta_minimum,
        state.publication_delta_milliseconds.maximum,
        state.head_local_lag1_better_count,
        state.head_local_phase_comparison_count);
    ++g_post_t4_visible_aim_report_count;

    // Keep the latest sample as the boundary for the next aggregate while
    // clearing only the reported metrics. This avoids a blind generation at
    // each window boundary and still guarantees exact once-per-generation
    // consumption.
    const std::uint64_t weapon_identity = state.weapon_identity;
    const std::uint64_t last_generation = state.last_generation;
    const std::uint64_t last_publication_milliseconds =
        state.last_publication_milliseconds;
    const wawvr::xr::Vec3f previous_raw_pair_forward =
        state.previous_raw_pair_forward;
    const wawvr::xr::Vec3f previous_applied_forward =
        state.previous_applied_forward;
    const wawvr::xr::Vec3f previous_raw_pair_up =
        state.previous_raw_pair_up;
    const wawvr::xr::Vec3f previous_applied_up =
        state.previous_applied_up;
    const wawvr::xr::Vec3f previous_visible_forward =
        state.previous_visible_forward;
    const wawvr::xr::Vec3f previous_visible_in_weapon_basis =
        state.previous_visible_in_weapon_basis;
    const wawvr::xr::Vec3f previous_head_relative_root =
        state.previous_head_relative_root;
    const wawvr::xr::Vec3f previous_grip_relative_root =
        state.previous_grip_relative_root;
    const wawvr::xr::Vec3f previous_visible_grip_root =
        state.previous_visible_grip_root;
    const wawvr::xr::Vec3f previous_visible_flash_root =
        state.previous_visible_flash_root;
    const PostT4AimPhaseState head_local_phase = state.head_local_phase;
    state = {};
    state.valid = true;
    state.weapon_identity = weapon_identity;
    state.first_generation = last_generation;
    state.last_generation = last_generation;
    state.last_publication_milliseconds = last_publication_milliseconds;
    state.previous_raw_pair_forward = previous_raw_pair_forward;
    state.previous_applied_forward = previous_applied_forward;
    state.previous_raw_pair_up = previous_raw_pair_up;
    state.previous_applied_up = previous_applied_up;
    state.previous_visible_forward = previous_visible_forward;
    state.previous_visible_in_weapon_basis =
        previous_visible_in_weapon_basis;
    state.previous_head_relative_root = previous_head_relative_root;
    state.previous_grip_relative_root = previous_grip_relative_root;
    state.previous_visible_grip_root = previous_visible_grip_root;
    state.previous_visible_flash_root = previous_visible_flash_root;
    state.head_local_phase = head_local_phase;
}

void publish_final_visible_aim(
    const std::uint64_t generation,
    const std::uint64_t frame_id,
    const std::uint64_t action_sequence,
    const bool live_controller_pose,
    const float pitch,
    const float yaw,
    const wawvr::xr::Basis3f& axis) noexcept {
    if (generation == 0 || frame_id == 0 || !std::isfinite(pitch) ||
        !std::isfinite(yaw) || !finite_basis(axis)) {
        return;
    }
    AcquireSRWLockExclusive(&g_final_visible_aim_lock);
    g_final_visible_aim = {
        true, generation, frame_id, action_sequence, GetTickCount64(),
        live_controller_pose, pitch, yaw, axis};
    ReleaseSRWLockExclusive(&g_final_visible_aim_lock);
}

void publish_weapon_frame_base_receipt(
    const ActiveWeaponPoseContext& context,
    const ActiveHandsContext& hands) noexcept {
    const auto& controller = hands.controller;
    if (!context.valid || !hands.valid || context.controller_generation == 0 ||
        context.controller_generation != controller.generation ||
        context.controller_publication_milliseconds == 0 ||
        context.controller_publication_milliseconds !=
            controller.publication_milliseconds ||
        controller.frame.frame_id == 0 ||
        controller.frame.actions.sequence == 0 ||
        controller.frame.predicted_display_time == 0 ||
        !std::isfinite(context.camera_origin.x) ||
        !std::isfinite(context.camera_origin.y) ||
        !std::isfinite(context.camera_origin.z) ||
        !finite_basis(context.camera_axis) ||
        !std::isfinite(context.weapon_origin.x) ||
        !std::isfinite(context.weapon_origin.y) ||
        !std::isfinite(context.weapon_origin.z) ||
        !finite_basis(context.weapon_axis)) {
        return;
    }

    WeaponFrameBaseReceipt receipt{
        .valid = true,
        .controller_generation = controller.generation,
        .frame_id = controller.frame.frame_id,
        .action_sequence = controller.frame.actions.sequence,
        .predicted_display_time = controller.frame.predicted_display_time,
        .controller_publication_milliseconds =
            controller.publication_milliseconds,
        .weapon_sample_milliseconds = GetTickCount64(),
        .same_frame_sample_count = 1,
        .scene_base_lock_eligible = context.scene_base_lock_eligible,
        .tracking_anchor = controller.tracking_anchor,
        .head_center = controller.frame.head_center,
        .camera_origin = context.camera_origin,
        .body_axis = context.camera_axis,
        .weapon_origin = context.weapon_origin,
        .weapon_axis = context.weapon_axis,
    };
    if (receipt.weapon_sample_milliseconds == 0) {
        return;
    }

    AcquireSRWLockExclusive(&g_weapon_frame_base_receipt_lock);
    const auto& previous = g_weapon_frame_base_receipt;
    if (previous.valid &&
        previous.controller_generation == receipt.controller_generation &&
        previous.frame_id == receipt.frame_id &&
        previous.action_sequence == receipt.action_sequence &&
        previous.predicted_display_time == receipt.predicted_display_time &&
        previous.same_frame_sample_count <
            (std::numeric_limits<std::uint32_t>::max)()) {
        receipt.same_frame_sample_count = previous.same_frame_sample_count + 1;
    }
    g_weapon_frame_base_receipt = receipt;
    ReleaseSRWLockExclusive(&g_weapon_frame_base_receipt_lock);
}

[[nodiscard]] bool read_fresh_final_visible_aim(
    const std::uint64_t controller_generation,
    const std::uint64_t now_milliseconds,
    FinalVisibleAim* const output) noexcept {
    if (output == nullptr || controller_generation == 0 ||
        now_milliseconds == 0) {
        return false;
    }
    AcquireSRWLockShared(&g_final_visible_aim_lock);
    const FinalVisibleAim snapshot = g_final_visible_aim;
    ReleaseSRWLockShared(&g_final_visible_aim_lock);
    constexpr std::uint64_t kMaximumRenderedAimAgeMilliseconds = 150;
    constexpr std::uint64_t kMaximumGenerationLag = 4;
    if (!snapshot.valid || snapshot.controller_generation == 0 ||
        snapshot.controller_generation > controller_generation ||
        controller_generation - snapshot.controller_generation >
            kMaximumGenerationLag ||
        snapshot.publication_milliseconds == 0 ||
        now_milliseconds < snapshot.publication_milliseconds ||
        now_milliseconds - snapshot.publication_milliseconds >
            kMaximumRenderedAimAgeMilliseconds ||
        !std::isfinite(snapshot.pitch_degrees) ||
        !std::isfinite(snapshot.yaw_degrees) || !finite_basis(snapshot.axis)) {
        return false;
    }
    *output = snapshot;
    return true;
}

[[nodiscard]] bool read_final_visible_weapon_basis(
    const std::uint64_t controller_generation,
    const std::uint64_t now_milliseconds,
    wawvr::xr::Basis3f* const axis) noexcept {
    FinalVisibleAim snapshot{};
    if (axis == nullptr ||
        !read_fresh_final_visible_aim(
            controller_generation, now_milliseconds, &snapshot)) {
        return false;
    }
    *axis = snapshot.axis;
    return true;
}

[[nodiscard]] bool read_evaluated_viewmodel_weapon_root_basis(
    void* const viewmodel_dobj,
    wawvr::xr::Basis3f* const weapon_root_basis,
    std::uint8_t* const weapon_root_bone_index,
    const PhysicalRocketLauncherProfile* const launcher_profile) noexcept {
    if (viewmodel_dobj == nullptr || weapon_root_basis == nullptr ||
        weapon_root_bone_index == nullptr ||
        g_cg_dobj_get_world_tag_matrix == 0 ||
        g_viewmodel_pose_address == 0 ||
        !accessible_range(viewmodel_dobj, kDObjSize, false)) {
        return false;
    }

    const auto* const dobj_bytes =
        static_cast<const std::uint8_t*>(viewmodel_dobj);
    std::uint8_t model_count = 0;
    std::uint8_t total_bone_count = 0;
    void* model_table = nullptr;
    std::memcpy(
        &model_count, dobj_bytes + kT4DObjNumModelsOffset,
        sizeof(model_count));
    std::memcpy(
        &total_bone_count, dobj_bytes + kDObjNumBonesOffset,
        sizeof(total_bone_count));
    std::memcpy(
        &model_table, dobj_bytes + kT4DObjModelsOffset,
        sizeof(model_table));
    if (model_count != 2 || model_table == nullptr ||
        total_bone_count == 0 ||
        total_bone_count > kMaximumViewmodelBoneCount ||
        !accessible_range(model_table, 2 * sizeof(void*), false)) {
        return false;
    }

    std::array<void*, 2> models{};
    std::memcpy(models.data(), model_table, sizeof(models));
    constexpr std::size_t kXModelBoneNamesOffset = 0x08;
    if (models[0] == nullptr || models[1] == nullptr ||
        !accessible_range(
            models[0], kXModelBoneNamesOffset + sizeof(void*),
            false) ||
        !accessible_range(
            models[1], kXModelBoneNamesOffset + sizeof(void*),
            false)) {
        return false;
    }

    if (launcher_profile == nullptr) {
        return false;
    }
    if (!launcher_profile->required_viewmodel_name.empty()) {
        std::uint32_t model_name_address = 0;
        std::memcpy(&model_name_address, models[1], sizeof(model_name_address));
        std::array<char, 64> model_name{};
        if (!copy_readable_bounded_c_string(model_name_address, &model_name) ||
            !physical_launcher_model_matches(
                launcher_profile, model_name.data())) {
            return false;
        }
    }

    std::uint8_t hands_bone_count = 0;
    std::uint8_t weapon_bone_count = 0;
    std::memcpy(
        &hands_bone_count,
        static_cast<const std::uint8_t*>(models[0]) +
            kT4XModelNumBonesOffset,
        sizeof(hands_bone_count));
    std::memcpy(
        &weapon_bone_count,
        static_cast<const std::uint8_t*>(models[1]) +
            kT4XModelNumBonesOffset,
        sizeof(weapon_bone_count));
    const std::size_t weapon_root_index = hands_bone_count;
    if (hands_bone_count == 0 || weapon_bone_count == 0 ||
        weapon_root_index >= total_bone_count ||
        weapon_root_index + weapon_bone_count > total_bone_count) {
        return false;
    }

    std::uint16_t* hands_bone_names = nullptr;
    std::uint16_t* weapon_bone_names = nullptr;
    std::memcpy(
        &hands_bone_names,
        static_cast<const std::uint8_t*>(models[0]) +
            kXModelBoneNamesOffset,
        sizeof(hands_bone_names));
    std::memcpy(
        &weapon_bone_names,
        static_cast<const std::uint8_t*>(models[1]) +
            kXModelBoneNamesOffset,
        sizeof(weapon_bone_names));
    if (hands_bone_names == nullptr || weapon_bone_names == nullptr ||
        !accessible_range(
            hands_bone_names,
            static_cast<std::size_t>(hands_bone_count) *
                sizeof(std::uint16_t),
            false) ||
        !accessible_range(
            weapon_bone_names,
            static_cast<std::size_t>(weapon_bone_count) *
                sizeof(std::uint16_t),
            false)) {
        return false;
    }

    std::uint16_t weapon_root_tag = 0;
    std::memcpy(
        &weapon_root_tag, weapon_bone_names, sizeof(weapon_root_tag));
    if (weapon_root_tag == 0) {
        return false;
    }
    std::size_t matching_tag_count = 0;
    for (std::size_t bone = 0; bone < hands_bone_count; ++bone) {
        matching_tag_count += hands_bone_names[bone] == weapon_root_tag ? 1 : 0;
    }
    for (std::size_t bone = 0; bone < weapon_bone_count; ++bone) {
        matching_tag_count +=
            weapon_bone_names[bone] == weapon_root_tag ? 1 : 0;
    }
    std::uint16_t expected_j_gun_tag = 0;
    const bool j_gun_resolved =
        resolve_t4_script_string("j_gun", &expected_j_gun_tag);
    if (matching_tag_count != 1 ||
        (j_gun_resolved && expected_j_gun_tag != weapon_root_tag)) {
        return false;
    }

    std::array<float, 9> root_matrix{};
    wawvr::xr::Vec3f root_origin{};
    if (wawvr_call_dobj_get_world_tag_matrix(
            viewmodel_dobj, weapon_root_tag,
            reinterpret_cast<const void*>(g_viewmodel_pose_address),
            root_matrix.data(), &root_origin) == 0) {
        return false;
    }
    const wawvr::xr::Basis3f raw_basis{
        .forward = {
            root_matrix[0], root_matrix[1], root_matrix[2]},
        .left = {
            root_matrix[3], root_matrix[4], root_matrix[5]},
        .up = {
            root_matrix[6], root_matrix[7], root_matrix[8]},
    };
    wawvr::xr::Basis3f calculated_basis{};
    if (!build_evaluated_projectile_basis(
            raw_basis.forward, raw_basis, &calculated_basis)) {
        return false;
    }
    *weapon_root_basis = calculated_basis;
    *weapon_root_bone_index = static_cast<std::uint8_t>(weapon_root_index);
    return true;
}

void publish_muzzle(
    const std::uint64_t generation,
    const wawvr::xr::Vec3f& origin,
    const wawvr::xr::Basis3f* const projectile_basis,
    const wawvr::xr::Basis3f* const weapon_root_basis = nullptr) noexcept {
    if (generation == 0 || !std::isfinite(origin.x) ||
        !std::isfinite(origin.y) || !std::isfinite(origin.z)) {
        return;
    }
    const bool projectile_basis_valid =
        projectile_basis != nullptr && finite_basis(*projectile_basis);
    const bool weapon_root_basis_valid =
        weapon_root_basis != nullptr && finite_basis(*weapon_root_basis);
    AcquireSRWLockExclusive(&g_published_muzzle_lock);
    g_published_muzzle = {
        true,
        generation,
        GetTickCount64(),
        origin,
        projectile_basis_valid,
        projectile_basis_valid ? *projectile_basis : wawvr::xr::Basis3f{},
        weapon_root_basis_valid,
        weapon_root_basis_valid
            ? *weapon_root_basis
            : wawvr::xr::Basis3f{},
    };
    ReleaseSRWLockExclusive(&g_published_muzzle_lock);
}

void invalidate_published_muzzle() noexcept {
    AcquireSRWLockExclusive(&g_published_muzzle_lock);
    g_published_muzzle = {};
    ReleaseSRWLockExclusive(&g_published_muzzle_lock);
}

[[nodiscard]] bool read_fresh_published_muzzle_snapshot(
    const std::uint64_t controller_generation,
    const std::uint64_t now_milliseconds,
    PublishedWeaponMuzzleSnapshot* const output) noexcept {
    if (output == nullptr) {
        return false;
    }
    AcquireSRWLockShared(&g_published_muzzle_lock);
    const PublishedWeaponMuzzleSnapshot snapshot = g_published_muzzle;
    ReleaseSRWLockShared(&g_published_muzzle_lock);
    if (!published_weapon_muzzle_is_fresh(
            snapshot, controller_generation, now_milliseconds)) {
        return false;
    }
    *output = snapshot;
    return true;
}

[[nodiscard]] bool read_tag_word(
    const std::uintptr_t address,
    std::uint16_t* const tag) noexcept {
    if (tag == nullptr ||
        !accessible_range(
            reinterpret_cast<const void*>(address), sizeof(*tag), false)) {
        return false;
    }
    std::uint16_t value = 0;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    if (value == 0) {
        return false;
    }
    *tag = value;
    return true;
}

[[nodiscard]] bool read_scope_lens_tag(
    void* const viewmodel_dobj,
    const void* const viewmodel_pose,
    wawvr::xr::Vec3f* const lens_world,
    const char** const selected_name,
    bool* const selected_generic_optic_anchor) noexcept {
    if (viewmodel_dobj == nullptr || viewmodel_pose == nullptr ||
        lens_world == nullptr || selected_name == nullptr ||
        selected_generic_optic_anchor == nullptr ||
        g_cg_dobj_get_world_tag_pos == 0) {
        return false;
    }
    *lens_world = {};
    *selected_name = nullptr;
    *selected_generic_optic_anchor = false;
    for (auto& candidate : g_scope_tags) {
        if (candidate.tag == 0) {
            static_cast<void>(
                resolve_t4_script_string(candidate.name, &candidate.tag));
        }
        wawvr::xr::Vec3f position{};
        if (candidate.tag != 0 &&
            wawvr_call_dobj_get_world_tag_pos(
                viewmodel_dobj, candidate.tag, viewmodel_pose, &position) != 0 &&
            std::isfinite(position.x) && std::isfinite(position.y) &&
            std::isfinite(position.z)) {
            *lens_world = position;
            *selected_name = candidate.name;
            *selected_generic_optic_anchor =
                candidate.generic_optic_anchor;
            return true;
        }
    }
    return false;
}

[[nodiscard]] wawvr::xr::Vec3f compose_local_vector(
    const wawvr::xr::Basis3f& basis,
    const wawvr::xr::Vec3f& local) noexcept {
    return {
        local.x * basis.forward.x + local.y * basis.left.x +
            local.z * basis.up.x,
        local.x * basis.forward.y + local.y * basis.left.y +
            local.z * basis.up.y,
        local.x * basis.forward.z + local.y * basis.left.z +
            local.z * basis.up.z,
    };
}

[[nodiscard]] wawvr::xr::Vec3f add_local_vector(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] bool apply_chest_weapon_placement(
    const ControllerFrameSnapshot& snapshot,
    const wawvr::xr::Vec3f& camera_origin,
    const wawvr::xr::Basis3f& camera_axis,
    wawvr::xr::Vec3f* const weapon_origin,
    wawvr::xr::Basis3f* const weapon_axis) noexcept {
    if (weapon_origin == nullptr || weapon_axis == nullptr ||
        !snapshot.frame.views_valid ||
        !controller_frame_is_current(snapshot, GetTickCount64())) {
        return false;
    }
    wawvr::xr::EnginePose chest{};
    if (!build_chest_weapon_pose(
            {camera_origin, camera_axis}, snapshot.frame.head_center,
            snapshot.tracking_anchor, &chest, &g_chest_weapon_pose_state)) {
        return false;
    }
    wawvr::xr::Quaternionf ignored{};
    if (!iw_axis_to_unit_quaternion(chest.axis, &ignored)) {
        return false;
    }
    *weapon_origin = chest.position;
    *weapon_axis = chest.axis;
    // This is a display-only chest context, never a held/controller pose.
    // After native animation, anchor the evaluated visible grip to this
    // sternum target instead of leaving the model's authored root offset.
    g_active_weapon_pose = {
        .valid = true,
        .grip_mode = WeaponGripMode::Chest,
        .weapon_identity = g_weapon_grip_state.weapon_identity,
        .controller_generation = snapshot.generation,
        .controller_publication_milliseconds = snapshot.publication_milliseconds,
        .tracked_grip_world = chest.position,
        .weapon_origin = chest.position,
        .weapon_axis = chest.axis,
        .camera_origin = camera_origin,
        .camera_axis = camera_axis,
    };
    return true;
}

[[nodiscard]] bool read_sp_player_weapon_index(
    const void* const player_state,
    std::int32_t* const weapon_index) noexcept {
    if (weapon_index == nullptr || player_state == nullptr ||
        !accessible_range(player_state, 0x108, false)) {
        return false;
    }
    const auto* const bytes =
        static_cast<const std::uint8_t*>(player_state);
    const std::size_t offset = (bytes[0x10] & 0x02U) != 0
        ? 0xFCU
        : 0x104U;
    std::int32_t value = 0;
    std::memcpy(&value, bytes + offset, sizeof(value));
    if (value <= 0 || value > 4095) {
        return false;
    }
    *weapon_index = value;
    return true;
}

[[nodiscard]] bool read_runtime_sp_weapon_count(
    std::uint32_t* const count,
    WeaponIdentitySnapshotReader& memory) noexcept {
    if (count == nullptr ||
        !g_weapon_hook_enabled.load(std::memory_order_acquire) ||
        g_weapon_layout != &kSpWeaponLayout ||
        g_weapon_definition_pointer_table_address == 0 ||
        g_weapon_definition_count_address == 0) {
        return false;
    }
    std::uint32_t value = 0;
    if (!memory.read(g_weapon_definition_count_address, &value) ||
        value == 0 || value > kMaximumRuntimeSpWeaponIndex) {
        return false;
    }
    const std::size_t table_size =
        (static_cast<std::size_t>(value) + 1U) * sizeof(std::uint32_t);
    if (table_size > kWeaponDefinitionPointerTableExtent ||
        !memory.readable(g_weapon_definition_pointer_table_address, table_size)) {
        return false;
    }
    *count = value;
    return true;
}

// The caller validated this exact table extent in the same synchronous call;
// no native engine service may be entered before this helper returns.
[[nodiscard]] bool read_runtime_sp_weapon_definition_from_checked_table(
    const std::int32_t weapon_index,
    const std::uint32_t count,
    WeaponIdentitySnapshotReader& memory,
    RuntimeWeaponDefinitionIdentity* const identity) noexcept {
    if (identity == nullptr) {
        return false;
    }
    *identity = {};
    if (weapon_index <= 0 ||
        static_cast<std::uint32_t>(weapon_index) > count) {
        return false;
    }

    const std::uintptr_t slot =
        g_weapon_definition_pointer_table_address +
        static_cast<std::size_t>(weapon_index) * sizeof(std::uint32_t);
    std::uint32_t definition = 0;
    std::memcpy(
        &definition, reinterpret_cast<const void*>(slot),
        sizeof(definition));
    std::uint32_t name_address = 0;
    if (definition == 0 ||
        !memory.read(static_cast<std::uintptr_t>(definition) +
                         kWeaponDefinitionNameOffset, &name_address) ||
        name_address == 0) {
        return false;
    }

    RuntimeWeaponDefinitionIdentity snapshot{};
    snapshot.weapon_index = weapon_index;
    snapshot.registered_count = count;
    snapshot.definition_address = definition;
    if (!memory.copy_c_string(
            static_cast<std::uintptr_t>(name_address), &snapshot.name)) {
        return false;
    }

    std::uint32_t stable_definition = 0;
    std::uint32_t stable_name_address = 0;
    std::uint32_t stable_count = 0;
    std::memcpy(
        &stable_definition, reinterpret_cast<const void*>(slot),
        sizeof(stable_definition));
    std::memcpy(
        &stable_name_address,
        reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(definition) +
            kWeaponDefinitionNameOffset),
        sizeof(stable_name_address));
    // The count and registration table were checked at this synchronous
    // snapshot's entry. No engine callback runs before this value recheck.
    // Read the live value again, not its page protections; a changed count
    // still rejects the snapshot before a different table extent is used.
    std::memcpy(
        &stable_count,
        reinterpret_cast<const void*>(g_weapon_definition_count_address),
        sizeof(stable_count));
    if (stable_count != count || stable_definition != definition ||
        stable_name_address != name_address ||
        g_weapon_layout != &kSpWeaponLayout ||
        !g_weapon_hook_enabled.load(std::memory_order_acquire)) {
        return false;
    }
    *identity = snapshot;
    return true;
}

[[nodiscard]] bool read_runtime_sp_weapon_definition_snapshot(
    const std::int32_t weapon_index,
    RuntimeWeaponDefinitionIdentity* const identity) noexcept {
    if (identity == nullptr) return false;
    *identity = {};
    WeaponIdentitySnapshotReader memory;
    std::uint32_t count = 0;
    return read_runtime_sp_weapon_count(&count, memory) &&
        read_runtime_sp_weapon_definition_from_checked_table(
            weapon_index, count, memory, identity);
}

[[nodiscard]] const BoltActionWeaponProfile*
profile_from_runtime_identity(
    const RuntimeWeaponDefinitionIdentity& identity) noexcept {
    return find_bolt_action_weapon_profile_by_internal_name(
        std::string_view{identity.name.data()});
}

[[nodiscard]] const BoltActionWeaponProfile*
profile_from_weapon_definition(
    const std::uint32_t weapon_definition_address,
    RuntimeWeaponDefinitionIdentity* const identity) noexcept {
    if (identity == nullptr || weapon_definition_address == 0 ||
        !find_runtime_sp_weapon_definition(
            reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(weapon_definition_address)),
            identity)) {
        return nullptr;
    }
    return profile_from_runtime_identity(*identity);
}

[[nodiscard]] RetailMarkCounters read_retail_mark_counters() noexcept {
    RetailMarkCounters snapshot{};
    if (g_fx_marks_no_marks_address == 0 ||
        g_fx_marks_allocated_count_address == 0 ||
        g_fx_marks_freed_count_address == 0 ||
        !accessible_range(
            reinterpret_cast<const void*>(g_fx_marks_no_marks_address),
            sizeof(snapshot.no_marks), false) ||
        !accessible_range(
            reinterpret_cast<const void*>(
                g_fx_marks_allocated_count_address),
            sizeof(snapshot.allocated), false) ||
        !accessible_range(
            reinterpret_cast<const void*>(g_fx_marks_freed_count_address),
            sizeof(snapshot.freed), false)) {
        return snapshot;
    }
    std::memcpy(
        &snapshot.no_marks,
        reinterpret_cast<const void*>(g_fx_marks_no_marks_address),
        sizeof(snapshot.no_marks));
    std::memcpy(
        &snapshot.allocated,
        reinterpret_cast<const void*>(g_fx_marks_allocated_count_address),
        sizeof(snapshot.allocated));
    std::memcpy(
        &snapshot.freed,
        reinterpret_cast<const void*>(g_fx_marks_freed_count_address),
        sizeof(snapshot.freed));
    snapshot.valid = true;
    return snapshot;
}

}  // namespace

extern "C" void __cdecl wawvr_post_update_viewmodel_pose(
    void* viewmodel_dobj) noexcept;
extern "C" void __cdecl wawvr_override_local_bullet_origin(
    std::int32_t local_client_number,
    const void* player_state,
    void* output_origin) noexcept;
extern "C" void __cdecl wawvr_override_local_bullet_spread(
    const void* weapon_definition,
    float* minimum_spread_degrees,
    float* maximum_spread_degrees) noexcept;
extern "C" void __cdecl wawvr_log_mosin_client_bullet_trace(
    std::int32_t local_client_number,
    const void* bullet_fire_params,
    const void* weapon_definition,
    const void* trace_results,
    std::uint32_t trace_hit) noexcept;
extern "C" void __cdecl wawvr_log_mosin_client_impact_selector(
    std::uint32_t local_client_number,
    std::uint32_t source_entity,
    std::uint32_t weapon_index,
    const void* position,
    const void* normal,
    std::uint32_t surface_type,
    std::uint32_t impact_flags,
    const void* selected_fx_slot,
    const void* selected_sound_slot,
    std::uint32_t target_entity,
    std::uint32_t effect_gate_contents) noexcept;
extern "C" void __cdecl wawvr_log_mosin_client_impact_fx_spawn(
    std::uint32_t local_client_number,
    std::uint32_t source_entity,
    std::uint32_t weapon_index,
    const void* position,
    const void* fx_definition,
    std::uint32_t game_time,
    std::uint32_t target_entity,
    std::uint32_t returned_effect_handle) noexcept;
extern "C" void __cdecl wawvr_add_player_weapon_bridge(
    std::int32_t local_client_number,
    GfxScaledPlacement* placement,
    const void* player_state,
    void* centity,
    std::int32_t draw_gun) noexcept;

#if defined(_MSC_VER) && defined(_M_IX86)
extern "C" __declspec(naked) void
wawvr_add_player_weapon_mp_entry_bridge() noexcept {
    __asm {
        // Native MP contract: EAX=centity and four caller-cleaned stack args.
        // Snapshot the untouched native stack before marshalling the fifth,
        // register-only argument into the ordinary C++ bridge signature.
        mov edx, esp
        push dword ptr [edx + 0x10]
        push eax
        push dword ptr [edx + 0x0C]
        push dword ptr [edx + 0x08]
        push dword ptr [edx + 0x04]
        call wawvr_add_player_weapon_bridge
        add esp, 0x14
        ret
    }
}

extern "C" __declspec(naked) void __cdecl
wawvr_call_add_player_weapon_mp_original(
    std::int32_t,
    const GfxScaledPlacement*,
    const void*,
    void*,
    std::int32_t) noexcept {
    __asm {
        push ebp
        mov ebp, esp
        // Recreate the stock MP contract: centity in EAX, with only the four
        // native arguments on the stock function's caller-cleaned stack.
        push dword ptr [ebp + 0x18]
        push dword ptr [ebp + 0x10]
        push dword ptr [ebp + 0x0C]
        push dword ptr [ebp + 0x08]
        mov eax, dword ptr [ebp + 0x14]
        call dword ptr [g_original_add_player_weapon]
        add esp, 0x10
        mov esp, ebp
        pop ebp
        ret
    }
}

extern "C" __declspec(naked) int __cdecl
wawvr_call_dobj_get_world_tag_pos(
    void*, std::uint32_t, const void*, wawvr::xr::Vec3f*) noexcept {
    __asm {
        push ebp
        mov ebp, esp
        push edi
        mov edi, dword ptr [ebp + 8]
        mov ecx, dword ptr [ebp + 0x0C]
        push dword ptr [ebp + 0x14]
        push dword ptr [ebp + 0x10]
        call dword ptr [g_cg_dobj_get_world_tag_pos]
        add esp, 8
        pop edi
        mov esp, ebp
        pop ebp
        ret
    }
}

extern "C" __declspec(naked) void __cdecl
wawvr_call_update_viewmodel_pose(void*) noexcept {
    __asm {
        mov eax, dword ptr [esp + 4]
        call dword ptr [g_original_update_viewmodel_pose]
        ret
    }
}

extern "C" __declspec(naked) void
wawvr_update_viewmodel_pose_bridge() noexcept {
    __asm {
        push eax
        call dword ptr [g_original_update_viewmodel_pose]
        pop eax
        push eax
        call wawvr_post_update_viewmodel_pose
        add esp, 4
        ret
    }
}

extern "C" __declspec(naked) void
wawvr_calc_muzzle_points_bridge() noexcept {
    __asm {
        // Entry contract at FireWeapon+0x53:
        // EAX = weaponParms*, [ESP+4] = firing gentity*. Preserve the
        // original call's custom ABI by duplicating its stack argument.
        push eax
        push dword ptr [esp + 8]
        call dword ptr [g_original_calc_muzzle_points]
        lea esp, [esp + 4]

        // Preserve every caller-observable register, flag and FP/SIMD value
        // while the ordinary C++ helper performs only the guarded +0x24 write.
        pushfd
        pushad
        mov ebx, esp
        sub esp, 527
        and esp, 0FFFFFFF0h
        fxsave [esp]
        push dword ptr [ebx + 44]
        push dword ptr [ebx + 36]
        call wawvr_apply_physical_muzzle_from_bridge
        add esp, 8
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        lea esp, [esp + 4]
        ret
    }
}

extern "C" __declspec(naked) void
wawvr_bullet_fire_bridge() noexcept {
    __asm {
        // Entry contract at FireWeapon's ordinary-bullet call:
        // ESI=weaponParms*, [ESP+4]=attacker, [ESP+8]=final spread.
        // Preserve every caller-observable value while the guarded helper
        // considers replacing only that one stack float.
        pushfd
        pushad
        mov ebx, esp
        sub esp, 527
        and esp, 0FFFFFFF0h
        fxsave [esp]
        lea eax, [ebx + 44]
        push eax
        push dword ptr [ebx + 40]
        push dword ptr [ebx + 4]
        call wawvr_apply_fixed_ads_spread_from_bridge
        add esp, 12
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd

        // Tail transfer preserves Bullet_Fire's original cdecl stack and
        // return address. Disabled/rejected decisions are stock behavior.
        jmp dword ptr [g_original_bullet_fire]
    }
}

extern "C" __declspec(naked) void
wawvr_client_bullet_spread_bridge() noexcept {
    __asm {
        // Exact local DrawBulletImpacts call contract:
        // ESI=playerState*, EDX=minSpread*, ECX=maxSpread*,
        // [ESP+4]=WeaponDef*. Save the output pointers privately, duplicate
        // the caller-cleaned WeaponDef argument, then run native first.
        push ecx
        push edx
        push dword ptr [esp + 0x0C]
        call dword ptr [g_original_get_spread_for_weapon]
        add esp, 4

        // Preserve the native return state while the guarded helper collapses
        // both local visual bounds to the authored ADS spread.
        pushfd
        pushad
        mov ebx, esp
        sub esp, 527
        and esp, 0FFFFFFF0h
        fxsave [esp]
        push dword ptr [ebx + 40]
        push dword ptr [ebx + 36]
        push dword ptr [ebx + 48]
        call wawvr_override_local_bullet_spread
        add esp, 12
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        lea esp, [esp + 8]
        ret
    }
}

extern "C" __declspec(naked) void
wawvr_client_bullet_view_origin_bridge() noexcept {
    __asm {
        // Exact DrawBulletImpacts contract:
        // EAX=localClientNum, [ESP+4]=playerState*, [ESP+8]=outOrigin.
        // Keep a private copy of the register-only client number and recreate
        // the original helper's two stack arguments.
        push eax
        mov edx, esp
        push dword ptr [edx + 0x0C]
        push dword ptr [edx + 0x08]
        call dword ptr [g_original_client_bullet_view_origin]
        add esp, 8
        test al, al
        jz stock_return

        // The stock AL result remains authoritative. On success only, preserve
        // every caller-observable register and FP/SIMD value while replacing
        // the written eye origin with the fresh tracked tag_flash.
        pushfd
        pushad
        mov ebx, esp
        sub esp, 527
        and esp, 0FFFFFFF0h
        fxsave [esp]
        push dword ptr [ebx + 48]
        push dword ptr [ebx + 44]
        push dword ptr [ebx + 36]
        call wawvr_override_local_bullet_origin
        add esp, 12
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd

    stock_return:
        lea esp, [esp + 4]
        ret
    }
}

extern "C" __declspec(naked) int __cdecl
wawvr_call_dobj_get_world_tag_matrix(
    void*, std::uint32_t, const void*, float*,
    wawvr::xr::Vec3f*) noexcept {
    __asm {
        push ebp
        mov ebp, esp
        mov eax, dword ptr [ebp + 8]
        mov ecx, dword ptr [ebp + 0x0C]
        push dword ptr [ebp + 0x18]
        push dword ptr [ebp + 0x14]
        push dword ptr [ebp + 0x10]
        call dword ptr [g_cg_dobj_get_world_tag_matrix]
        add esp, 0x0C
        mov esp, ebp
        pop ebp
        ret
    }
}

extern "C" __declspec(naked) void
wawvr_client_bullet_trace_bridge() noexcept {
    __asm {
        // This bridge replaces only the exact five-byte post-call sequence
        // `add esp,0x0C; test al,al`. BulletTrace has already returned with
        // AL=result, ESI=BulletFireParams*, EDI=WeaponDef*, EBP=attacker,
        // EBX=localClientNum, and its three arguments still on the native
        // stack. The replacement CALL adds one synthetic return address.
        pushfd
        pushad
        mov ebx, esp
        sub esp, 527
        and esp, 0FFFFFFF0h
        fxsave [esp]
        lea eax, [ebx + 0x74]
        push dword ptr [ebx + 28]
        push eax
        push dword ptr [ebx]
        push dword ptr [ebx + 4]
        push dword ptr [ebx + 16]
        call wawvr_log_mosin_client_bullet_trace
        add esp, 20
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd

        // Discard the replacement CALL's synthetic return, then replay the
        // displaced stock instructions exactly. The following native JNE at
        // the resume address consumes these TEST flags.
        add esp, 4
        add esp, 0x0C
        test al, al
        jmp dword ptr [g_client_bullet_trace_resume]
    }
}

extern "C" __declspec(naked) void
wawvr_client_impact_selector_bridge() noexcept {
    __asm {
        // The exact replacement is a CALL over the native CALL. Discard only
        // that synthetic replacement return so the selector sees its original
        // three arguments and custom EAX/EDI register contract unchanged.
        add esp, 4
        call dword ptr [g_original_client_impact_selector]

        // Snapshot the selector's complete post-native state. At anchor A,
        // A+0/+4 retain EDI/ESI, A+24/+28/+2C are the three native arguments,
        // and the outer CG_BulletHitEvent context begins at A+74. In
        // particular, A+7C is the raw map-local weapon index, not WeaponDef*.
        pushfd
        pushad
        mov ebx, esp
        sub esp, 527
        and esp, 0FFFFFFF0h
        fxsave [esp]
        push dword ptr [ebx + 098h]
        push dword ptr [ebx + 004h]
        push dword ptr [ebx + 000h]
        push dword ptr [ebx + 02Ch]
        push dword ptr [ebx + 028h]
        push dword ptr [ebx + 024h]
        push dword ptr [ebx + 088h]
        push dword ptr [ebx + 084h]
        push dword ptr [ebx + 07Ch]
        push dword ptr [ebx + 078h]
        push dword ptr [ebx + 074h]
        call wawvr_log_mosin_client_impact_selector
        add esp, 44
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd

        // Stock caller cleanup and output consumption remain at the resume.
        jmp dword ptr [g_client_impact_selector_resume]
    }
}

extern "C" __declspec(naked) void
wawvr_client_impact_fx_spawn_bridge() noexcept {
    __asm {
        // Preserve the original ECX=markEntity, EDX=axis and three stack
        // arguments. The decoded native FX spawn is called exactly once.
        add esp, 4
        call dword ptr [g_original_client_impact_fx_spawn]

        // Snapshot only after native returns. Saved EAX at A+1C is the effect
        // handle stock code must push into its following DelRef call; A+7C is
        // the same raw map-local weapon index retained by the outer frame.
        pushfd
        pushad
        mov ebx, esp
        sub esp, 527
        and esp, 0FFFFFFF0h
        fxsave [esp]
        push dword ptr [ebx + 01Ch]
        push dword ptr [ebx + 004h]
        push dword ptr [ebx + 028h]
        push dword ptr [ebx + 024h]
        push dword ptr [ebx + 02Ch]
        push dword ptr [ebx + 07Ch]
        push dword ptr [ebx + 078h]
        push dword ptr [ebx + 074h]
        call wawvr_log_mosin_client_impact_fx_spawn
        add esp, 32
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd

        // POPAD restores the exact native EAX handle for stock cleanup/DelRef.
        jmp dword ptr [g_client_impact_fx_spawn_resume]
    }
}

extern "C" __declspec(naked) void
wawvr_get_player_angles_scr_add_vector_bridge() noexcept {
    __asm {
        // Exact SP callsite contract inside GScr_GetPlayerAngles:
        // [ESP] is the native return address, [ESP+4] is float angles[3],
        // and ESI is the resolved gentity. Replace only the vector pointer
        // handed to Scr_AddVector; no native view/player state is written.
        mov eax, dword ptr [esp + 4]
        push eax
        push esi
        call wawvr_select_rocket_barrage_angles_from_bridge
        add esp, 8
        mov dword ptr [esp + 4], eax
        xor eax, eax
        jmp dword ptr [g_original_scr_add_vector]
    }
}
#else
extern "C" int __cdecl wawvr_call_dobj_get_world_tag_pos(
    void*, std::uint32_t, const void*, wawvr::xr::Vec3f*) noexcept {
    return 0;
}
extern "C" int __cdecl wawvr_call_dobj_get_world_tag_matrix(
    void*, std::uint32_t, const void*, float*,
    wawvr::xr::Vec3f*) noexcept {
    return 0;
}

extern "C" void __cdecl wawvr_call_update_viewmodel_pose(void*) noexcept {}
extern "C" void wawvr_add_player_weapon_mp_entry_bridge() noexcept {}
extern "C" void __cdecl wawvr_call_add_player_weapon_mp_original(
    std::int32_t,
    const GfxScaledPlacement*,
    const void*,
    void*,
    std::int32_t) noexcept {}
extern "C" void wawvr_update_viewmodel_pose_bridge() noexcept {}
extern "C" void wawvr_calc_muzzle_points_bridge() noexcept {}
extern "C" void wawvr_bullet_fire_bridge() noexcept {}
extern "C" void wawvr_client_bullet_spread_bridge() noexcept {}
extern "C" void wawvr_client_bullet_view_origin_bridge() noexcept {}
extern "C" void wawvr_client_bullet_trace_bridge() noexcept {}
extern "C" void wawvr_client_impact_selector_bridge() noexcept {}
extern "C" void wawvr_client_impact_fx_spawn_bridge() noexcept {}
extern "C" void
wawvr_get_player_angles_scr_add_vector_bridge() noexcept {}
#endif

extern "C" const float* __cdecl
wawvr_select_rocket_barrage_angles_from_bridge(
    void* const entity,
    const float* const native_angles) noexcept {
    const bool campaign_enabled =
        g_campaign_targeting_hook_enabled.load(std::memory_order_acquire);
    if (!campaign_enabled) {
        return native_angles;
    }
    const WeaponExecutableLayout* const layout = g_weapon_layout;
    if (!g_weapon_hook_enabled.load(std::memory_order_acquire) ||
        layout == nullptr ||
        layout->family != T4LayoutFamily::single_player_1_7_1263 ||
        native_angles == nullptr || entity == nullptr ||
        reinterpret_cast<std::uintptr_t>(entity) !=
            g_local_player_entity_address ||
        g_weapon_definition_pointer_table_address == 0 ||
        g_weapon_definition_count_address == 0 ||
        g_camera_axis_address == 0) {
        return preserve_native_campaign_angles(
            native_angles, "validated SP hook prerequisites unavailable");
    }

    const auto entity_address = reinterpret_cast<std::uintptr_t>(entity);
    const auto client_pointer_address =
        entity_address + layout->gentity_client_offset;
    if (!accessible_range(
            reinterpret_cast<const void*>(client_pointer_address),
            sizeof(std::uint32_t), false)) {
        return preserve_native_campaign_angles(
            native_angles, "local-player client pointer unreadable");
    }

    std::uint32_t client_address32 = 0;
    std::memcpy(
        &client_address32,
        reinterpret_cast<const void*>(client_pointer_address),
        sizeof(client_address32));
    const std::uintptr_t client_address = client_address32;
    if (client_address == 0 ||
        client_address > std::numeric_limits<std::uintptr_t>::max() -
            kSpClientFallbackWeaponOffset - 1U ||
        !accessible_range(
            reinterpret_cast<const void*>(
                client_address + kSpClientCurrentWeaponOffset),
            sizeof(std::uint32_t), false) ||
        !accessible_range(
            reinterpret_cast<const void*>(
                client_address + kSpClientFallbackWeaponOffset),
            sizeof(std::uint8_t), false)) {
        return preserve_native_campaign_angles(
            native_angles, "local-player weapon fields unreadable");
    }

    std::uint32_t primary_weapon_id = 0;
    std::uint8_t fallback_weapon_id_byte = 0;
    std::uint32_t weapon_count = 0;
    std::memcpy(
        &primary_weapon_id,
        reinterpret_cast<const void*>(
            client_address + kSpClientCurrentWeaponOffset),
        sizeof(primary_weapon_id));
    std::memcpy(
        &fallback_weapon_id_byte,
        reinterpret_cast<const void*>(
            client_address + kSpClientFallbackWeaponOffset),
        sizeof(fallback_weapon_id_byte));
    if (!accessible_range(
            reinterpret_cast<const void*>(
                g_weapon_definition_count_address),
            sizeof(weapon_count), false)) {
        return preserve_native_campaign_angles(
            native_angles, "weapon-definition count unreadable");
    }
    std::memcpy(
        &weapon_count,
        reinterpret_cast<const void*>(g_weapon_definition_count_address),
        sizeof(weapon_count));

    const std::uint32_t fallback_weapon_id = fallback_weapon_id_byte;
    const std::uint32_t effective_weapon_id =
        primary_weapon_id != 0 ? primary_weapon_id : fallback_weapon_id;
    if (weapon_count == 0 ||
        weapon_count > kMaximumSerializedT4WeaponId ||
        effective_weapon_id == 0 || effective_weapon_id > weapon_count) {
        return preserve_native_campaign_angles(
            native_angles, "effective weapon id outside validated bounds");
    }

    const std::uintptr_t table_slot =
        g_weapon_definition_pointer_table_address +
        effective_weapon_id * sizeof(std::uint32_t);
    if (!accessible_range(
            reinterpret_cast<const void*>(table_slot),
            sizeof(std::uint32_t), false)) {
        return preserve_native_campaign_angles(
            native_angles, "weapon-definition table slot unreadable");
    }
    std::uint32_t weapon_definition_address32 = 0;
    std::memcpy(
        &weapon_definition_address32,
        reinterpret_cast<const void*>(table_slot),
        sizeof(weapon_definition_address32));
    const std::uintptr_t weapon_definition_address =
        weapon_definition_address32;
    if (weapon_definition_address == 0 ||
        !accessible_range(
            reinterpret_cast<const void*>(
                weapon_definition_address + kWeaponDefinitionNameOffset),
            sizeof(std::uint32_t), false)) {
        return preserve_native_campaign_angles(
            native_angles, "weapon-definition record unreadable");
    }

    std::uint32_t weapon_name_address32 = 0;
    std::memcpy(
        &weapon_name_address32,
        reinterpret_cast<const void*>(
            weapon_definition_address + kWeaponDefinitionNameOffset),
        sizeof(weapon_name_address32));
    const std::uintptr_t weapon_name_address = weapon_name_address32;
    if (weapon_name_address == 0 ||
        !accessible_range(
            reinterpret_cast<const void*>(weapon_name_address),
            kCampaignTargetingWeaponNameBytes, false)) {
        return preserve_native_campaign_angles(
            native_angles, "weapon-name span unreadable");
    }
    std::array<char, kCampaignTargetingWeaponNameBytes> weapon_name{};
    std::memcpy(
        weapon_name.data(),
        reinterpret_cast<const void*>(weapon_name_address),
        weapon_name.size());
    // air_support is shorter than rocket_barrage. Its terminator is at its
    // own length; bytes after that NUL belong to adjacent native storage.
    const std::string_view targeting_weapon_name =
        resolve_campaign_targeting_weapon_name(
            std::string_view(weapon_name.data(), weapon_name.size()));
    if (targeting_weapon_name.empty()) {
        return preserve_native_campaign_angles(
            native_angles, "weapon is not an exact supported campaign designator");
    }

    ControllerFrameSnapshot controller{};
    wawvr::xr::Basis3f camera_axis{};
    const std::uint64_t now_milliseconds = GetTickCount64();
    const bool frame_available = read_controller_frame(&controller);
    const bool frame_current = frame_available &&
        controller_frame_is_current(controller, now_milliseconds);
    if (!frame_current ||
        !accessible_range(
            reinterpret_cast<const void*>(g_camera_axis_address),
            sizeof(camera_axis), false)) {
        return preserve_native_campaign_angles(
            native_angles, "controller frame stale or camera axis unreadable");
    }
    std::memcpy(
        &camera_axis,
        reinterpret_cast<const void*>(g_camera_axis_address),
        sizeof(camera_axis));
    float pitch_degrees = 0.0F;
    float yaw_degrees = 0.0F;
    if (!controller_aim_degrees(
            controller, camera_axis, &pitch_degrees, &yaw_degrees)) {
        return preserve_native_campaign_angles(
            native_angles, "controller aim conversion rejected");
    }

    const RocketBarrageAngleGate gate{
        campaign_enabled &&
            g_weapon_hook_enabled.load(std::memory_order_acquire),
        true,
        reinterpret_cast<std::uintptr_t>(entity),
        g_local_player_entity_address,
        true,
        primary_weapon_id,
        fallback_weapon_id,
        weapon_count,
        weapon_definition_address != 0,
        targeting_weapon_name,
        controller.frame.actions.focused,
        frame_current,
        pitch_degrees,
        yaw_degrees,
    };
    const RocketBarrageAngleSelection selection =
        select_rocket_barrage_angles(gate);
    if (!selection.substitute ||
        !g_campaign_targeting_hook_enabled.load(
            std::memory_order_acquire)) {
        return preserve_native_campaign_angles(
            native_angles, "exact local campaign-designator gate rejected");
    }
    log_campaign_angle_acceptance_once(
        selection.effective_weapon_id,
        targeting_weapon_name,
        selection.angles[0], selection.angles[1]);
    g_campaign_rocket_angles = selection.angles;
    return g_campaign_rocket_angles.data();
}

extern "C" void __cdecl wawvr_log_mosin_client_bullet_trace(
    const std::int32_t local_client_number,
    const void* const bullet_fire_params,
    const void* const weapon_definition,
    const void* const trace_results,
    const std::uint32_t trace_hit) noexcept {
    if (local_client_number != 0 ||
        !g_weapon_hook_enabled.load(std::memory_order_acquire) ||
        g_weapon_layout != &kSpWeaponLayout ||
        bullet_fire_params == nullptr || weapon_definition == nullptr ||
        trace_results == nullptr ||
        !accessible_range(
            bullet_fire_params, sizeof(ClientBulletFireParams), false) ||
        !accessible_range(
            trace_results, sizeof(ClientBulletTraceResults), false)) {
        return;
    }

    const auto definition_address =
        reinterpret_cast<std::uintptr_t>(weapon_definition);
    if (definition_address >
        (std::numeric_limits<std::uint32_t>::max)()) {
        return;
    }
    RuntimeWeaponDefinitionIdentity identity{};
    const BoltActionWeaponProfile* const profile =
        profile_from_weapon_definition(
            static_cast<std::uint32_t>(definition_address), &identity);
    if (profile == nullptr ||
        profile->id != BoltActionWeaponProfileId::Mosin ||
        !profile->direct_ballistic_basis) {
        return;
    }

    ClientBulletFireParams params{};
    ClientBulletTraceResults results{};
    std::memcpy(&params, bullet_fire_params, sizeof(params));
    std::memcpy(&results, trace_results, sizeof(results));

    static std::atomic<std::uint32_t> mosin_trace_logs{0};
    const std::uint32_t shot_index =
        mosin_trace_logs.fetch_add(1, std::memory_order_relaxed);
    if (shot_index >= 24) {
        return;
    }
    const bool hit = (trace_hit & 0xFFU) != 0;
    stereo_diagnostic_log(
        "WeaponTraceDiag Mosin shot[%u] firePenetrate=1 initialTrace=1 hit=%d start=%.3f %.3f %.3f end=%.3f %.3f %.3f dir=%.6f %.6f %.6f fraction=%.6f startSolid=%u allSolid=%u surfaceFlags=0x%08X contents=0x%08X hitType=%d hitId=%u hitPos=%.3f %.3f %.3f ignoreHit=%u depthSurface=%d weapon=%d",
        shot_index, hit ? 1 : 0,
        params.start.x, params.start.y, params.start.z,
        params.end.x, params.end.y, params.end.z,
        params.direction.x, params.direction.y, params.direction.z,
        results.trace.fraction,
        static_cast<unsigned>(results.trace.start_solid),
        static_cast<unsigned>(results.trace.all_solid),
        static_cast<unsigned>(results.trace.surface_flags),
        static_cast<unsigned>(results.trace.contents),
        results.trace.hit_type,
        static_cast<unsigned>(results.trace.hit_id),
        results.hit_position.x, results.hit_position.y,
        results.hit_position.z,
        static_cast<unsigned>(results.ignore_hit_entity),
        results.depth_surface_type, identity.weapon_index);
}

extern "C" void __cdecl wawvr_log_mosin_client_impact_selector(
    const std::uint32_t local_client_number,
    const std::uint32_t source_entity,
    const std::uint32_t weapon_index,
    const void* const position,
    const void* const normal,
    const std::uint32_t surface_type,
    const std::uint32_t impact_flags,
    const void* const selected_fx_slot,
    const void* const selected_sound_slot,
    const std::uint32_t target_entity,
    const std::uint32_t effect_gate_contents) noexcept {
    // Every selector arrival invalidates any unconsumed correlation from an
    // earlier impact on this same game thread, including non-Mosin impacts.
    g_mosin_impact_correlation = {};

    // Retail passes the map-local weapon index in the selector's custom EAX
    // contract. The post-call outer frame retains that uint32 at A+0x7C; it is
    // not a WeaponDef pointer. Resolve the definition through the pinned SP
    // weapon table before applying the exact-Mosin filter.
    const bool runtime_ready = local_client_number == 0 &&
        g_weapon_hook_enabled.load(std::memory_order_acquire) &&
        g_weapon_layout == &kSpWeaponLayout;
    RuntimeWeaponDefinitionIdentity identity{};
    const bool identity_resolved = runtime_ready &&
        weapon_index <=
            static_cast<std::uint32_t>(
                (std::numeric_limits<std::int32_t>::max)()) &&
        read_runtime_sp_weapon_definition_snapshot(
            static_cast<std::int32_t>(weapon_index), &identity);
    const BoltActionWeaponProfile* const profile = identity_resolved
        ? profile_from_runtime_identity(identity)
        : nullptr;
    const bool profile_is_mosin = profile != nullptr &&
        profile->id == BoltActionWeaponProfileId::Mosin;

    // Keep raw ABI/identity failures observable instead of silently losing the
    // same diagnostic again. This cap is independent of the exact-Mosin pair
    // cap below, so unrelated impacts cannot suppress detailed Mosin records.
    static std::atomic<std::uint32_t> selector_arrivals{0};
    const std::uint32_t arrival_sequence =
        selector_arrivals.fetch_add(1, std::memory_order_relaxed);
    if (arrival_sequence < 24) {
        stereo_diagnostic_log(
            "WeaponImpactDiag selector-arrival[%u] rawWeaponIndex=%u identityResolved=%d profileMosin=%d resolvedWeapon=%d weaponDef=0x%08X runtimeReady=%d",
            arrival_sequence, weapon_index, identity_resolved ? 1 : 0,
            profile_is_mosin ? 1 : 0,
            identity_resolved ? identity.weapon_index : 0,
            identity_resolved ? identity.definition_address : 0,
            runtime_ready ? 1 : 0);
    }
    if (!profile_is_mosin) {
        return;
    }

    wawvr::xr::Vec3f stable_position{};
    wawvr::xr::Vec3f stable_normal{};
    std::uint32_t selected_fx = 0;
    std::uint32_t selected_sound = 0;
    const bool position_readable = position != nullptr &&
        accessible_range(position, sizeof(stable_position), false);
    const bool normal_readable = normal != nullptr &&
        accessible_range(normal, sizeof(stable_normal), false);
    if (position_readable) {
        std::memcpy(&stable_position, position, sizeof(stable_position));
    }
    if (normal_readable) {
        std::memcpy(&stable_normal, normal, sizeof(stable_normal));
    }
    if (selected_fx_slot != nullptr &&
        accessible_range(
            selected_fx_slot, sizeof(selected_fx), false)) {
        std::memcpy(
            &selected_fx, selected_fx_slot, sizeof(selected_fx));
    }
    if (selected_sound_slot != nullptr &&
        accessible_range(
            selected_sound_slot, sizeof(selected_sound), false)) {
        std::memcpy(
            &selected_sound, selected_sound_slot,
            sizeof(selected_sound));
    }

    const bool normal_finite = normal_readable &&
        std::isfinite(stable_normal.x) &&
        std::isfinite(stable_normal.y) &&
        std::isfinite(stable_normal.z);
    const float normal_length_squared = normal_finite
        ? stable_normal.x * stable_normal.x +
              stable_normal.y * stable_normal.y +
              stable_normal.z * stable_normal.z
        : 0.0F;
    const bool normal_valid = normal_finite &&
        std::isfinite(normal_length_squared) &&
        normal_length_squared > 1.0e-8F;
    const RetailMarkCounters counters = read_retail_mark_counters();

    static std::atomic<std::uint32_t> mosin_impact_selectors{0};
    const std::uint32_t sequence =
        mosin_impact_selectors.fetch_add(1, std::memory_order_relaxed);
    if (sequence >= 24) {
        return;
    }

    const bool armed = selected_fx != 0 && position_readable && normal_valid;
    stereo_diagnostic_log(
        "WeaponImpactDiag Mosin selector[%u] arrived=1 local=%u source=0x%08X rawWeaponIndex=%u weapon=%d weaponDef=0x%08X target=0x%08X surf=%u impactFlags=0x%08X contents=0x%08X pos=%.3f %.3f %.3f normal=%.6f %.6f %.6f normalReadable=%d normalFinite=%d normalLenSq=%.6f selectedFx=0x%08X selectedSound=0x%08X armed=%d marksValid=%d noMarks=%u allocated=%u freed=%u",
        sequence, local_client_number, source_entity,
        weapon_index, identity.weapon_index, identity.definition_address,
        target_entity, surface_type, impact_flags, effect_gate_contents,
        stable_position.x, stable_position.y, stable_position.z,
        stable_normal.x, stable_normal.y, stable_normal.z,
        normal_readable ? 1 : 0, normal_finite ? 1 : 0,
        normal_length_squared, selected_fx, selected_sound,
        armed ? 1 : 0, counters.valid ? 1 : 0,
        static_cast<unsigned>(counters.no_marks), counters.allocated,
        counters.freed);
    if (!armed) {
        return;
    }

    g_mosin_impact_correlation = {
        .armed = true,
        .sequence = sequence,
        .local_client_number = local_client_number,
        .source_entity = source_entity,
        .weapon_index = weapon_index,
        .weapon_definition = identity.definition_address,
        .position_address = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(position)),
        .position = stable_position,
        .normal = stable_normal,
        .selected_fx = selected_fx,
        .target_entity = target_entity,
        .mark_counters = counters,
    };
}

extern "C" void __cdecl wawvr_log_mosin_client_impact_fx_spawn(
    const std::uint32_t local_client_number,
    const std::uint32_t source_entity,
    const std::uint32_t weapon_index,
    const void* const position,
    const void* const fx_definition,
    const std::uint32_t game_time,
    const std::uint32_t target_entity,
    const std::uint32_t returned_effect_handle) noexcept {
    const auto position_address = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(position));
    if (!g_mosin_impact_correlation.armed) {
        return;
    }
    if (g_mosin_impact_correlation.weapon_index != weapon_index ||
        g_mosin_impact_correlation.position_address != position_address) {
        const MosinImpactCorrelation mismatch =
            g_mosin_impact_correlation;
        g_mosin_impact_correlation = {};
        stereo_diagnostic_log(
            "WeaponImpactDiag Mosin spawn[%u] correlated=0 mismatch=1 expectedWeaponIndex=%u actualWeaponIndex=%u expectedWeaponDef=0x%08X expectedPosition=0x%08X actualPosition=0x%08X returnedHandle=0x%08X",
            mismatch.sequence, mismatch.weapon_index, weapon_index,
            mismatch.weapon_definition,
            mismatch.position_address, position_address,
            returned_effect_handle);
        return;
    }

    const MosinImpactCorrelation correlation =
        g_mosin_impact_correlation;
    g_mosin_impact_correlation = {};

    wawvr::xr::Vec3f stable_position{};
    const bool position_readable = position != nullptr &&
        accessible_range(position, sizeof(stable_position), false);
    if (position_readable) {
        std::memcpy(&stable_position, position, sizeof(stable_position));
    }
    const bool position_unchanged = position_readable &&
        std::memcmp(
            &stable_position, &correlation.position,
            sizeof(stable_position)) == 0;
    const RetailMarkCounters after = read_retail_mark_counters();
    const bool counter_pair_valid =
        correlation.mark_counters.valid && after.valid;
    const std::uint32_t allocated_delta = counter_pair_valid
        ? after.allocated - correlation.mark_counters.allocated
        : 0;
    const std::uint32_t freed_delta = counter_pair_valid
        ? after.freed - correlation.mark_counters.freed
        : 0;

    stereo_diagnostic_log(
        "WeaponImpactDiag Mosin spawn[%u] correlated=1 local=%u/%u source=0x%08X/0x%08X weaponIndex=%u weaponDef=0x%08X position=0x%08X positionUnchanged=%d selectedFx=0x%08X spawnFx=0x%08X selectorTarget=0x%08X spawnTarget=0x%08X gameTime=%u returnedHandle=0x%08X marksValid=%d noMarks=%u/%u allocated=%u/%u delta=%u freed=%u/%u delta=%u",
        correlation.sequence, correlation.local_client_number,
        local_client_number, correlation.source_entity, source_entity,
        correlation.weapon_index, correlation.weapon_definition,
        correlation.position_address,
        position_unchanged ? 1 : 0, correlation.selected_fx,
        static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(fx_definition)),
        correlation.target_entity, target_entity, game_time,
        returned_effect_handle, counter_pair_valid ? 1 : 0,
        static_cast<unsigned>(correlation.mark_counters.no_marks),
        static_cast<unsigned>(after.no_marks),
        correlation.mark_counters.allocated, after.allocated,
        allocated_delta, correlation.mark_counters.freed, after.freed,
        freed_delta);
}

extern "C" void __cdecl wawvr_override_local_bullet_origin(
    const std::int32_t local_client_number,
    const void* const player_state,
    void* const output_origin) noexcept {
    if (local_client_number != 0 ||
        !g_weapon_hook_enabled.load(std::memory_order_acquire) ||
        !accessible_range(
            output_origin, sizeof(wawvr::xr::Vec3f), true)) {
        return;
    }

    ControllerFrameSnapshot controller{};
    const std::uint64_t now_milliseconds = GetTickCount64();
    RightControllerWeaponPose ignored_pose{};
    if (!read_controller_frame(&controller) ||
        !right_controller_weapon_pose(
            controller, now_milliseconds, &ignored_pose)) {
        return;
    }
    PublishedWeaponMuzzleSnapshot published_muzzle{};
    if (!read_fresh_published_muzzle_snapshot(
            controller.generation, now_milliseconds, &published_muzzle) ||
        !g_weapon_hook_enabled.load(std::memory_order_acquire)) {
        return;
    }
    const wawvr::xr::Vec3f physical_muzzle = published_muzzle.origin;

    std::int32_t weapon_index = 0;
    std::int32_t weapon_type = -1;
    RuntimeWeaponDefinitionIdentity identity{};
    const BoltActionWeaponProfile* profile = nullptr;
    if (read_sp_player_weapon_index(player_state, &weapon_index) &&
        read_runtime_sp_weapon_definition_snapshot(
            weapon_index, &identity)) {
        profile = profile_from_runtime_identity(identity);
        const WeaponExecutableLayout* const layout = g_weapon_layout;
        const std::uintptr_t type_address =
            static_cast<std::uintptr_t>(identity.definition_address) +
            (layout != nullptr
                 ? layout->weapon_definition_type_offset
                 : 0);
        if (layout != nullptr && identity.definition_address != 0 &&
            accessible_range(
                reinterpret_cast<const void*>(type_address),
                sizeof(weapon_type), false)) {
            std::memcpy(
                &weapon_type,
                reinterpret_cast<const void*>(type_address),
                sizeof(weapon_type));
        }
    }
    const bool direct_basis_profile =
        profile != nullptr && profile->direct_ballistic_basis;
    const bool exact_flash_basis = weapon_type == 0 &&
        hitscan_uses_tag_flash_forward(
            std::string_view{identity.name.data()});
    const bool tracked_hitscan_basis =
        weapon_type == 0 && authoritative_weapon_basis_is_requested(
            false, direct_basis_profile, weapon_type);
    const bool evaluated_barrel_fresh = tracked_hitscan_basis &&
        published_muzzle.projectile_basis_valid &&
        finite_basis(published_muzzle.projectile_basis);
    float evaluated_barrel_pitch = 0.0F;
    float evaluated_barrel_yaw = 0.0F;
    const bool evaluated_barrel_angles_valid = evaluated_barrel_fresh &&
        aim_degrees_from_forward(
            published_muzzle.projectile_basis.forward,
            &evaluated_barrel_pitch, &evaluated_barrel_yaw);
    if (tracked_hitscan_basis && !evaluated_barrel_angles_valid) {
        // The authoritative path also preserves the complete native
        // WeaponParms in this case. Keep the client helper's native eye
        // origin and angles so a stale publication cannot split origin from
        // direction between predicted and authoritative shots.
        static std::atomic<std::uint32_t> rejected_visual_basis_logs{0};
        const std::uint32_t log_index =
            rejected_visual_basis_logs.fetch_add(
                1, std::memory_order_relaxed);
        if (log_index < 24) {
            stereo_diagnostic_log(
                "WeaponDiag hitscan visual basis rejected[%u] profile=%s weapon=%d generation=%llu; native impact ray preserved atomically",
                log_index,
                profile != nullptr ? profile->diagnostic_name
                                   : exact_flash_basis
                                       ? "exact-tag-flash"
                                       : "shared-type0",
                weapon_index,
                static_cast<unsigned long long>(controller.generation));
        }
        return;
    }
    if (evaluated_barrel_angles_valid &&
        accessible_range(
            reinterpret_cast<void*>(g_cgame_gun_pitch_address),
            sizeof(float), true) &&
        accessible_range(
            reinterpret_cast<void*>(g_cgame_gun_yaw_address),
            sizeof(float), true)) {
        // CG_DrawBulletImpacts loads these globals immediately after this
        // native view-origin call. Refresh them from the same visible pose as
        // the physical muzzle so the predicted mark and authoritative ray
        // share one generation during right-hand bolt operation.
        std::memcpy(
            reinterpret_cast<void*>(g_cgame_gun_pitch_address),
            &evaluated_barrel_pitch,
            sizeof(evaluated_barrel_pitch));
        std::memcpy(
            reinterpret_cast<void*>(g_cgame_gun_yaw_address),
            &evaluated_barrel_yaw,
            sizeof(evaluated_barrel_yaw));
        static std::atomic<std::uint32_t> visual_basis_logs{0};
        const std::uint32_t log_index =
            visual_basis_logs.fetch_add(1, std::memory_order_relaxed);
        if (log_index < 24) {
            stereo_diagnostic_log(
                "WeaponDiag evaluated-barrel visual basis[%u] profile=%s weapon=%d generation=%llu lag=%llu ageMs=%llu pitch=%.3f yaw=%.3f forward=%.5f %.5f %.5f",
                log_index,
                profile != nullptr ? profile->diagnostic_name
                                   : exact_flash_basis
                                       ? "exact-tag-flash"
                                       : "shared-type0",
                weapon_index,
                static_cast<unsigned long long>(controller.generation),
                static_cast<unsigned long long>(
                    controller.generation -
                    published_muzzle.controller_generation),
                static_cast<unsigned long long>(
                    now_milliseconds -
                    published_muzzle.publication_milliseconds),
                evaluated_barrel_pitch,
                evaluated_barrel_yaw,
                published_muzzle.projectile_basis.forward.x,
                published_muzzle.projectile_basis.forward.y,
                published_muzzle.projectile_basis.forward.z);
        }
    }

    wawvr::xr::Vec3f native_origin{};
    std::memcpy(&native_origin, output_origin, sizeof(native_origin));
    std::memcpy(output_origin, &physical_muzzle, sizeof(physical_muzzle));
    WAWVR_STEREO_DIAG_ONCE(
        "WeaponDiag aligned local client tracer/impact origin from eye %.2f %.2f %.2f to tracked tag_flash %.2f %.2f %.2f",
        native_origin.x, native_origin.y, native_origin.z,
        physical_muzzle.x, physical_muzzle.y, physical_muzzle.z);
}

extern "C" void __cdecl wawvr_override_local_bullet_spread(
    const void* const weapon_definition,
    float* const minimum_spread_degrees,
    float* const maximum_spread_degrees) noexcept {
    const bool hook_enabled =
        g_weapon_hook_enabled.load(std::memory_order_acquire);
    const WeaponExecutableLayout* const layout = g_weapon_layout;
    if (!hook_enabled || layout == nullptr || weapon_definition == nullptr ||
        !accessible_range(
            minimum_spread_degrees, sizeof(float), true) ||
        !accessible_range(
            maximum_spread_degrees, sizeof(float), true)) {
        return;
    }

    const auto weapon_definition_address =
        reinterpret_cast<std::uintptr_t>(weapon_definition);
    if (!accessible_range(
            reinterpret_cast<const void*>(
                weapon_definition_address +
                layout->weapon_definition_type_offset),
            sizeof(std::int32_t), false) ||
        !accessible_range(
            reinterpret_cast<const void*>(
                weapon_definition_address +
                layout->weapon_definition_ads_spread_offset),
            sizeof(float), false)) {
        return;
    }

    std::int32_t weapon_type = -1;
    float ads_spread_degrees = 0.0F;
    float native_minimum_spread_degrees = 0.0F;
    float native_maximum_spread_degrees = 0.0F;
    std::memcpy(
        &weapon_type,
        reinterpret_cast<const void*>(
            weapon_definition_address +
            layout->weapon_definition_type_offset),
        sizeof(weapon_type));
    std::memcpy(
        &ads_spread_degrees,
        reinterpret_cast<const void*>(
            weapon_definition_address +
            layout->weapon_definition_ads_spread_offset),
        sizeof(ads_spread_degrees));
    std::memcpy(
        &native_minimum_spread_degrees, minimum_spread_degrees,
        sizeof(native_minimum_spread_degrees));
    std::memcpy(
        &native_maximum_spread_degrees, maximum_spread_degrees,
        sizeof(native_maximum_spread_degrees));

    float replacement_minimum_spread_degrees =
        native_minimum_spread_degrees;
    float replacement_maximum_spread_degrees =
        native_maximum_spread_degrees;
    const FixedAdsVisualSpreadGate gate{
        hook_enabled,
        weapon_type,
        ads_spread_degrees,
    };
    if (!apply_fixed_ads_visual_spread_override(
            gate, &replacement_minimum_spread_degrees,
            &replacement_maximum_spread_degrees) ||
        !g_weapon_hook_enabled.load(std::memory_order_acquire)) {
        return;
    }

    std::memcpy(
        minimum_spread_degrees, &replacement_minimum_spread_degrees,
        sizeof(replacement_minimum_spread_degrees));
    std::memcpy(
        maximum_spread_degrees, &replacement_maximum_spread_degrees,
        sizeof(replacement_maximum_spread_degrees));
    WAWVR_STEREO_DIAG_ONCE(
        "WeaponDiag fixed local tracer/impact spread: native=%.3f..%.3f applied-ads=%.3f",
        native_minimum_spread_degrees, native_maximum_spread_degrees,
        replacement_maximum_spread_degrees);
}

extern "C" void __cdecl wawvr_apply_physical_muzzle_from_bridge(
    void* const weapon_parms,
    void* const firing_entity) noexcept {
    // This token is one-shot and belongs only to the synchronous
    // FireWeapon -> CalcMuzzlePoints -> Weapon_RocketLauncher_Fire path.
    // Clearing it before every attempt prevents an unrelated later rocket
    // from consuming stale local-player state.
    g_pending_player_bazooka_rocket = {};
    const bool hook_enabled =
        g_weapon_hook_enabled.load(std::memory_order_acquire);
    const WeaponExecutableLayout* const layout = g_weapon_layout;
    if (!hook_enabled || layout == nullptr ||
        g_local_player_entity_address == 0 ||
        !accessible_range(weapon_parms, layout->weapon_parms_size, true) ||
        !accessible_range(firing_entity, layout->gentity_size, false)) {
        return;
    }

    const auto weapon_bytes = reinterpret_cast<std::uint8_t*>(weapon_parms);
    const auto entity_bytes =
        reinterpret_cast<const std::uint8_t*>(firing_entity);
    std::int32_t entity_number = -1;
    std::uint32_t client_address = 0;
    std::uint32_t weapon_definition_address = 0;
    std::memcpy(
        &entity_number, entity_bytes + layout->gentity_number_offset,
        sizeof(entity_number));
    std::memcpy(
        &client_address, entity_bytes + layout->gentity_client_offset,
        sizeof(client_address));
    std::memcpy(
        &weapon_definition_address,
        weapon_bytes + layout->weapon_parms_weapon_definition_offset,
        sizeof(weapon_definition_address));
    if (weapon_definition_address == 0 ||
        !accessible_range(
            reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(weapon_definition_address) +
                layout->weapon_definition_type_offset),
            sizeof(std::int32_t), false)) {
        return;
    }

    std::int32_t weapon_type = -1;
    std::memcpy(
        &weapon_type,
        reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(weapon_definition_address) +
            layout->weapon_definition_type_offset),
        sizeof(weapon_type));

    ControllerFrameSnapshot controller{};
    const std::uint64_t now_milliseconds = GetTickCount64();
    RightControllerWeaponPose ignored_pose{};
    const bool controller_current = read_controller_frame(&controller) &&
        right_controller_weapon_pose(
            controller, now_milliseconds, &ignored_pose);
    PublishedWeaponMuzzleSnapshot published_muzzle{};
    const bool muzzle_fresh = controller_current &&
        read_fresh_published_muzzle_snapshot(
            controller.generation, now_milliseconds, &published_muzzle);
    const wawvr::xr::Vec3f physical_muzzle = published_muzzle.origin;
    RuntimeWeaponDefinitionIdentity identity{};
    std::int32_t player_weapon_index = 0;
    bool runtime_identity_from_player_state = false;
    bool runtime_identity_valid = false;
    if (layout->family == T4LayoutFamily::single_player_1_7_1263) {
        // WeaponParms carries a fire-time WeaponDef record that is not always
        // the same pointer stored in the map-local registration table. The
        // local gclient begins with playerState, whose selected/offhand weapon
        // index is the authoritative identity for this player-only hook.
        runtime_identity_from_player_state = client_address != 0 &&
            read_sp_player_weapon_index(
                reinterpret_cast<const void*>(
                    static_cast<std::uintptr_t>(client_address)),
                &player_weapon_index) &&
            read_runtime_sp_weapon_definition_snapshot(
                player_weapon_index, &identity);
        runtime_identity_valid = runtime_identity_from_player_state ||
            find_runtime_sp_weapon_definition(
                reinterpret_cast<const void*>(
                    static_cast<std::uintptr_t>(weapon_definition_address)),
                &identity);
    }
    const BoltActionWeaponProfile* const bolt_profile =
        runtime_identity_valid && weapon_type == 0
            ? profile_from_runtime_identity(identity)
            : nullptr;
    const PhysicalRocketLauncherProfile* const launcher_profile =
        runtime_identity_valid && weapon_type == 2
            ? find_physical_rocket_launcher_profile(identity.name.data())
            : nullptr;
    const bool exact_bazooka_projectile = launcher_profile != nullptr;
    const bool bazooka_final_direction_route =
        exact_bazooka_projectile && g_original_fire_rocket != 0;
    const BoltActionWeaponProfile* const basis_profile =
        bolt_profile != nullptr && bolt_profile->direct_ballistic_basis
            ? bolt_profile
            : nullptr;
    const bool exact_flash_basis = runtime_identity_valid &&
        weapon_type == 0 && hitscan_uses_tag_flash_forward(
            std::string_view{identity.name.data()});
    const bool authoritative_basis_requested =
        authoritative_weapon_basis_is_requested(
            layout->overwrite_authoritative_weapon_basis,
            basis_profile != nullptr, weapon_type);
    FinalVisibleAim final_visible{};
    const bool final_visible_fresh = controller_current &&
        read_fresh_final_visible_aim(
            controller.generation, now_milliseconds, &final_visible);
    const bool evaluated_hitscan_basis_fresh =
        weapon_type == 0 && muzzle_fresh &&
        published_muzzle.projectile_basis_valid &&
        finite_basis(published_muzzle.projectile_basis);
    wawvr::xr::Basis3f authoritative_axis{};
    bool authoritative_axis_fresh = !authoritative_basis_requested;
    std::uint64_t authoritative_source_generation =
        controller.generation;
    std::uint64_t authoritative_publication_milliseconds =
        now_milliseconds;
    if (authoritative_basis_requested) {
        // Ordinary bullets use the published per-asset evaluated barrel
        // basis, not the raw controller ray used to place the model root.
        // This is also the basis consumed by predicted visual impacts;
        // audited weapons may use tag_flash rather than an off-axis grip.
        if (evaluated_hitscan_basis_fresh) {
            authoritative_axis_fresh = true;
            authoritative_axis = published_muzzle.projectile_basis;
            authoritative_source_generation =
                published_muzzle.controller_generation;
            authoritative_publication_milliseconds =
                published_muzzle.publication_milliseconds;
        } else if (weapon_type != 0 && final_visible_fresh) {
            authoritative_axis_fresh = true;
            authoritative_axis = final_visible.axis;
            authoritative_source_generation =
                final_visible.controller_generation;
            authoritative_publication_milliseconds =
                final_visible.publication_milliseconds;
        }
    }
    const PhysicalMuzzleGate gate{
        hook_enabled,
        reinterpret_cast<std::uintptr_t>(firing_entity),
        g_local_player_entity_address,
        entity_number,
        client_address != 0,
        weapon_type,
        controller_current,
        muzzle_fresh,
    };
    const bool physical_muzzle_allowed = physical_muzzle_gate_allows(gate);
    if (!physical_muzzle_allowed ||
        !g_weapon_hook_enabled.load(std::memory_order_acquire)) {
        return;
    }

    wawvr::xr::Vec3f native_muzzle{};
    wawvr::xr::Vec3f native_forward{};
    wawvr::xr::Vec3f native_right{};
    wawvr::xr::Vec3f native_up{};
    std::memcpy(
        &native_muzzle,
        weapon_bytes + layout->weapon_parms_muzzle_trace_offset,
        sizeof(native_muzzle));
    std::memcpy(
        &native_forward,
        weapon_bytes + layout->weapon_parms_forward_offset,
        sizeof(native_forward));
    std::memcpy(
        &native_right,
        weapon_bytes + layout->weapon_parms_right_offset,
        sizeof(native_right));
    std::memcpy(
        &native_up,
        weapon_bytes + layout->weapon_parms_up_offset,
        sizeof(native_up));
    wawvr::xr::Vec3f corrected_forward = native_forward;
    wawvr::xr::Vec3f corrected_right = native_right;
    wawvr::xr::Vec3f corrected_up = native_up;
    const bool authoritative_basis_applied =
        apply_authoritative_weapon_basis_override(
            {
                authoritative_basis_requested,
                physical_muzzle_allowed,
                authoritative_axis_fresh,
            },
            authoritative_axis,
            &corrected_forward,
            &corrected_right,
            &corrected_up);
    if (authoritative_basis_requested &&
        !authoritative_basis_applied) {
        if (weapon_type == 0 || weapon_type == 2) {
            static std::atomic<std::uint32_t> rejected_basis_logs{0};
            const std::uint32_t log_index =
                rejected_basis_logs.fetch_add(
                    1, std::memory_order_relaxed);
            if (log_index < 24) {
                stereo_diagnostic_log(
                    "WeaponDiag %s shot basis rejected[%u] profile=%s weapon=%d type=%d generation=%llu evaluatedBarrelFresh=%d trackedFresh=%d; native shot preserved atomically",
                    weapon_type == 0 ? "hitscan" : "projectile",
                    log_index,
                    basis_profile != nullptr
                        ? basis_profile->diagnostic_name
                        : exact_flash_basis
                            ? "exact-tag-flash"
                        : exact_bazooka_projectile
                            ? "bazooka-tracked-line"
                            : "type2-shared",
                    identity.weapon_index, weapon_type,
                    static_cast<unsigned long long>(
                        controller.generation),
                    evaluated_hitscan_basis_fresh ? 1 : 0,
                    authoritative_axis_fresh ? 1 : 0);
            }
        }
        return;
    }
    wawvr::xr::Basis3f weapon_root_bazooka_launch_basis{};
    const bool weapon_root_bazooka_launch_ready =
        select_exact_bazooka_weapon_root_launch_basis(
            {
                bazooka_final_direction_route,
                physical_muzzle_allowed && authoritative_basis_applied,
                muzzle_fresh &&
                    published_muzzle.weapon_root_basis_valid,
            },
            published_muzzle.weapon_root_basis,
            &weapon_root_bazooka_launch_basis);
    if (weapon_root_bazooka_launch_ready) {
        g_pending_player_bazooka_rocket = {
            .valid = true,
            .profile = launcher_profile,
            .parent = reinterpret_cast<std::uintptr_t>(firing_entity),
            .weapon_index = static_cast<std::uint32_t>(
                identity.weapon_index),
            .weapon_definition_address = weapon_definition_address,
            .controller_generation =
                final_visible.controller_generation,
            .publication_milliseconds = now_milliseconds,
            .muzzle_origin = physical_muzzle,
            .weapon_root_launch_basis = weapon_root_bazooka_launch_basis,
            .tracked_basis_valid = final_visible_fresh,
            .tracked_basis = final_visible.axis,
            .tag_bore_basis_valid =
                published_muzzle.projectile_basis_valid &&
                finite_basis(published_muzzle.projectile_basis),
            .tag_bore_basis = published_muzzle.projectile_basis,
        };
    }
    if (authoritative_basis_applied) {
        std::memcpy(
            weapon_bytes + layout->weapon_parms_forward_offset,
            &corrected_forward, sizeof(corrected_forward));
        std::memcpy(
            weapon_bytes + layout->weapon_parms_right_offset,
            &corrected_right, sizeof(corrected_right));
        std::memcpy(
            weapon_bytes + layout->weapon_parms_up_offset,
            &corrected_up, sizeof(corrected_up));
    }
    std::memcpy(
        weapon_bytes + layout->weapon_parms_muzzle_trace_offset,
        &physical_muzzle, sizeof(physical_muzzle));
    if (weapon_type == 0 ||
        (weapon_type == 2 && !bazooka_final_direction_route)) {
        static std::atomic<std::uint32_t> shot_basis_logs{0};
        const std::uint32_t log_index =
            shot_basis_logs.fetch_add(1, std::memory_order_relaxed);
        if (log_index < 24) {
            const float dot =
                native_forward.x * corrected_forward.x +
                native_forward.y * corrected_forward.y +
                native_forward.z * corrected_forward.z;
            stereo_diagnostic_log(
                "WeaponDiag %s shot basis[%u] profile=%s weapon=%d identity=%s type=%d generation=%llu lag=%llu ageMs=%llu applied=%d nativeF=%.4f %.4f %.4f visibleF=%.4f %.4f %.4f dot=%.6f muzzle=%.2f %.2f %.2f",
                weapon_type == 0 ? "hitscan" : "projectile",
                log_index,
                basis_profile != nullptr
                    ? basis_profile->diagnostic_name
                    : exact_flash_basis
                        ? "exact-tag-flash"
                    : weapon_type == 0
                        ? "shared-type0-evaluated-barrel"
                    : exact_bazooka_projectile
                        ? "bazooka-evaluated-barrel"
                        : "type2-shared",
                identity.weapon_index,
                runtime_identity_from_player_state ? "player-state" :
                    runtime_identity_valid ? "weapon-table" : "unresolved",
                weapon_type,
                static_cast<unsigned long long>(controller.generation),
                static_cast<unsigned long long>(
                    controller.generation -
                    authoritative_source_generation),
                static_cast<unsigned long long>(
                    now_milliseconds -
                    authoritative_publication_milliseconds),
                authoritative_basis_applied ? 1 : 0,
                native_forward.x, native_forward.y, native_forward.z,
                corrected_forward.x, corrected_forward.y,
                corrected_forward.z, dot,
                physical_muzzle.x, physical_muzzle.y,
                physical_muzzle.z);
        }
    }
    WAWVR_STEREO_DIAG_ONCE(
        "WeaponDiag routed local bullet origin from native %.2f %.2f %.2f to corrected tag_flash %.2f %.2f %.2f",
        native_muzzle.x, native_muzzle.y, native_muzzle.z,
        physical_muzzle.x, physical_muzzle.y, physical_muzzle.z);
}

extern "C" void* __cdecl wawvr_player_rocket_bridge(
    void* const parent,
    const std::uint32_t weapon_index,
    float* const start,
    float* const direction,
    const float* const gun_velocity,
    void* const target,
    const float* const target_offset) noexcept {
    const std::uintptr_t original_address = g_original_fire_rocket;
    if (original_address == 0) {
        return nullptr;
    }
    const auto original =
        reinterpret_cast<GFireRocketFunction>(original_address);

    // This is the shared native projectile spawn call (including the tank
    // cannon), not a render or
    // trigger event. Keep its feedback independent of the optional physical
    // trajectory correction so default-pass-through launchers rumble too.
    const WeaponExecutableLayout* const layout = g_weapon_layout;
    std::int32_t parent_entity_number = -1;
    std::uint32_t parent_client_address = 0;
    if (g_weapon_hook_enabled.load(std::memory_order_acquire) &&
        layout != nullptr && parent != nullptr &&
        reinterpret_cast<std::uintptr_t>(parent) ==
            g_local_player_entity_address &&
        accessible_range(parent, layout->gentity_size, false)) {
        const auto* const parent_bytes =
            reinterpret_cast<const std::uint8_t*>(parent);
        std::memcpy(
            &parent_entity_number,
            parent_bytes + layout->gentity_number_offset,
            sizeof(parent_entity_number));
        std::memcpy(
            &parent_client_address,
            parent_bytes + layout->gentity_client_offset,
            sizeof(parent_client_address));
    }
    const bool local_player_shot =
        parent_entity_number == 0 && parent_client_address != 0;
    const auto spawn_and_notify = [&](float* const spawn_start,
                                      float* const spawn_direction) noexcept {
        void* const spawned = original(
            parent, weapon_index, spawn_start, spawn_direction,
            gun_velocity, target, target_offset);
        if (spawned != nullptr && local_player_shot &&
            g_weapon_hook_enabled.load(std::memory_order_acquire)) {
            queue_firing_haptic(GetTickCount64());
        }
        return spawned;
    };

    const PendingPlayerBazookaRocket pending =
        g_pending_player_bazooka_rocket;
    g_pending_player_bazooka_rocket = {};
    const std::uint64_t now_milliseconds = GetTickCount64();
    const bool accepted =
        g_weapon_hook_enabled.load(std::memory_order_acquire) &&
        pending.valid &&
        (pending.profile == &kBazookaPhysicalLauncher ||
         pending.profile == &kPanzerschreckPhysicalLauncher) &&
        layout == &kSpWeaponLayout && parent != nullptr &&
        reinterpret_cast<std::uintptr_t>(parent) == pending.parent &&
        pending.parent == g_local_player_entity_address &&
        weapon_index == pending.weapon_index && start != nullptr &&
        direction != nullptr &&
        pending.publication_milliseconds != 0 &&
        pending.publication_milliseconds <= now_milliseconds &&
        now_milliseconds - pending.publication_milliseconds <=
            kMaximumPublishedMuzzleAgeMilliseconds &&
        accessible_range(start, sizeof(wawvr::xr::Vec3f), false) &&
        accessible_range(direction, sizeof(wawvr::xr::Vec3f), false);
    if (!accepted) {
        return spawn_and_notify(start, direction);
    }

    wawvr::xr::Vec3f native_start{};
    wawvr::xr::Vec3f native_direction{};
    std::memcpy(&native_start, start, sizeof(native_start));
    std::memcpy(&native_direction, direction, sizeof(native_direction));
    wawvr::xr::Vec3f corrected_direction{};
    // Weapon_RocketLauncher_Fire has already added the launcher's native
    // hip-spread cone to native_direction. The physical one-shot route must
    // discard that ray and use the final evaluated weapon-XModel root line.
    // This is the rigid bone that owns the visible launch tube. The controller
    // and animated-tag lines remain diagnostics only.
    if (!exact_bazooka_projectile_direction(
            pending.weapon_root_launch_basis, &corrected_direction)) {
        static std::atomic<std::uint32_t> rejected_logs{0};
        const std::uint32_t log_index =
            rejected_logs.fetch_add(1, std::memory_order_relaxed);
        if (log_index < 24) {
            stereo_diagnostic_log(
                "WeaponDiag final bazooka route rejected[%u]: weapon=%u generation=%llu; evaluated weapon-root aim invalid, native start/direction preserved",
                log_index, weapon_index,
                static_cast<unsigned long long>(
                    pending.controller_generation));
        }
        return spawn_and_notify(start, direction);
    }

    wawvr::xr::Vec3f corrected_start = pending.muzzle_origin;
    void* const rocket = spawn_and_notify(
        reinterpret_cast<float*>(&corrected_start),
        reinterpret_cast<float*>(&corrected_direction));

    wawvr::xr::Vec3f tag_bore_direction{};
    const bool tag_bore_direction_valid = pending.tag_bore_basis_valid &&
        exact_bazooka_projectile_direction(
            pending.tag_bore_basis, &tag_bore_direction);
    const float root_tag_dot = tag_bore_direction_valid
        ? corrected_direction.x * tag_bore_direction.x +
              corrected_direction.y * tag_bore_direction.y +
              corrected_direction.z * tag_bore_direction.z
        : 0.0F;
    wawvr::xr::Vec3f tracked_direction{};
    const bool tracked_direction_valid = pending.tracked_basis_valid &&
        exact_bazooka_projectile_direction(
            pending.tracked_basis, &tracked_direction);
    const float tracked_root_dot = tracked_direction_valid
        ? corrected_direction.x * tracked_direction.x +
              corrected_direction.y * tracked_direction.y +
              corrected_direction.z * tracked_direction.z
        : 0.0F;

    wawvr::xr::Vec3f spawned_velocity{};
    wawvr::xr::Vec3f spawned_angles{};
    bool spawned_velocity_read = false;
    bool spawned_angles_read = false;
    const std::uintptr_t spawned_rocket_address =
        reinterpret_cast<std::uintptr_t>(rocket);
    if (spawned_rocket_address != 0 &&
        spawned_rocket_address <=
            (std::numeric_limits<std::uintptr_t>::max)() -
                kSpGEntityTrajectoryDeltaOffset) {
        const void* const velocity_address = reinterpret_cast<const void*>(
            spawned_rocket_address + kSpGEntityTrajectoryDeltaOffset);
        if (accessible_range(
                velocity_address, sizeof(spawned_velocity), false)) {
            std::memcpy(
                &spawned_velocity, velocity_address,
                sizeof(spawned_velocity));
            spawned_velocity_read = std::isfinite(spawned_velocity.x) &&
                std::isfinite(spawned_velocity.y) &&
                std::isfinite(spawned_velocity.z);
        }
    }
    if (spawned_rocket_address != 0 &&
        spawned_rocket_address <=
            (std::numeric_limits<std::uintptr_t>::max)() -
                kSpGEntityCurrentAnglesOffset) {
        const void* const angles_address = reinterpret_cast<const void*>(
            spawned_rocket_address + kSpGEntityCurrentAnglesOffset);
        if (accessible_range(
                angles_address, sizeof(spawned_angles), false)) {
            std::memcpy(
                &spawned_angles, angles_address, sizeof(spawned_angles));
            spawned_angles_read = std::isfinite(spawned_angles.x) &&
                std::isfinite(spawned_angles.y) &&
                std::isfinite(spawned_angles.z);
        }
    }
    wawvr::xr::Vec3f inherited_gun_velocity{};
    const bool inherited_gun_velocity_read = gun_velocity != nullptr &&
        accessible_range(
            gun_velocity, sizeof(inherited_gun_velocity), false);
    if (inherited_gun_velocity_read) {
        std::memcpy(
            &inherited_gun_velocity, gun_velocity,
            sizeof(inherited_gun_velocity));
    }

    bool stable_missile_applied = false;
    const std::uintptr_t rocket_address =
        reinterpret_cast<std::uintptr_t>(rocket);
    const std::uintptr_t parent_address =
        reinterpret_cast<std::uintptr_t>(parent);
    if (rocket_address != 0 && rocket_address != parent_address &&
        rocket_address <=
            (std::numeric_limits<std::uintptr_t>::max)() -
                kSpGEntityFlagsOffset) {
        auto* const flags_address = reinterpret_cast<std::uint32_t*>(
            rocket_address + kSpGEntityFlagsOffset);
        if (accessible_range(flags_address, sizeof(*flags_address), true)) {
            std::uint32_t flags = 0;
            std::memcpy(&flags, flags_address, sizeof(flags));
            if (apply_exact_bazooka_stable_missile_flag(true, &flags)) {
                std::memcpy(flags_address, &flags, sizeof(flags));
                std::uint32_t verified_flags = 0;
                std::memcpy(
                    &verified_flags, flags_address, sizeof(verified_flags));
                stable_missile_applied = verified_flags == flags;
            }
        }
    }
    // Keep T4's client-missile and bolted-trail timing native. In particular,
    // launchTime also participates in missile trajectory evaluation, and the
    // first sequence-zero trail point is intentionally transparent. Direction
    // is corrected above; native CG_Missile remains the visual owner.

    static std::atomic<std::uint32_t> accepted_logs{0};
    const std::uint32_t log_index =
        accepted_logs.fetch_add(1, std::memory_order_relaxed);
    if (log_index < 24) {
        const float forward_dot =
            native_direction.x * corrected_direction.x +
            native_direction.y * corrected_direction.y +
            native_direction.z * corrected_direction.z;
        stereo_diagnostic_log(
            "WeaponDiag final physical launcher weapon-root route[%u]: profile=%s weapon=%u generation=%llu ageMs=%llu nativeStart=%.2f %.2f %.2f muzzleStart=%.2f %.2f %.2f nativeDir=%.4f %.4f %.4f tubeDir=%.4f %.4f %.4f nativeTubeDot=%.6f trackedValid=%d trackedDir=%.4f %.4f %.4f trackedTubeDot=%.6f tagBoreValid=%d tagBoreDir=%.4f %.4f %.4f tubeTagDot=%.6f spawnedVelocityRead=%d spawnedVelocity=%.2f %.2f %.2f gunVelocityRead=%d gunVelocity=%.2f %.2f %.2f spawnedAnglesRead=%d spawnedAngles=%.2f %.2f %.2f stableMissile=%d nativeClientTrail=1 rocket=%p",
            log_index, pending.profile->weapon_name.data(), weapon_index,
            static_cast<unsigned long long>(
                pending.controller_generation),
            static_cast<unsigned long long>(
                now_milliseconds - pending.publication_milliseconds),
            native_start.x, native_start.y, native_start.z,
            corrected_start.x, corrected_start.y, corrected_start.z,
            native_direction.x, native_direction.y,
            native_direction.z, corrected_direction.x,
            corrected_direction.y, corrected_direction.z, forward_dot,
            tracked_direction_valid ? 1 : 0,
            tracked_direction.x, tracked_direction.y,
            tracked_direction.z, tracked_root_dot,
            tag_bore_direction_valid ? 1 : 0,
            tag_bore_direction.x, tag_bore_direction.y,
            tag_bore_direction.z, root_tag_dot,
            spawned_velocity_read ? 1 : 0,
            spawned_velocity.x, spawned_velocity.y, spawned_velocity.z,
            inherited_gun_velocity_read ? 1 : 0,
            inherited_gun_velocity.x, inherited_gun_velocity.y,
            inherited_gun_velocity.z,
            spawned_angles_read ? 1 : 0,
            spawned_angles.x, spawned_angles.y, spawned_angles.z,
            stable_missile_applied ? 1 : 0,
            rocket);
    }
    return rocket;
}

extern "C" void __cdecl wawvr_apply_fixed_ads_spread_from_bridge(
    void* const weapon_parms,
    void* const attacker,
    float* const spread_degrees) noexcept {
    const bool hook_enabled =
        g_weapon_hook_enabled.load(std::memory_order_acquire);
    const WeaponExecutableLayout* const layout = g_weapon_layout;
    if (!hook_enabled || layout == nullptr ||
        g_local_player_entity_address == 0 ||
        !accessible_range(weapon_parms, layout->weapon_parms_size, false) ||
        !accessible_range(attacker, layout->gentity_size, false) ||
        !accessible_range(spread_degrees, sizeof(float), true)) {
        return;
    }

    const auto weapon_bytes =
        reinterpret_cast<const std::uint8_t*>(weapon_parms);
    const auto entity_bytes =
        reinterpret_cast<const std::uint8_t*>(attacker);
    std::int32_t entity_number = -1;
    std::uint32_t client_address = 0;
    std::uint32_t weapon_definition_address = 0;
    std::memcpy(
        &entity_number, entity_bytes + layout->gentity_number_offset,
        sizeof(entity_number));
    std::memcpy(
        &client_address, entity_bytes + layout->gentity_client_offset,
        sizeof(client_address));
    // This exact FireWeapon -> Bullet_Fire call occurs once per discharge,
    // before any native pellet/impact work. Publish independently of ADS
    // spread or manual-reload policy; AI and dry-trigger paths cannot pass.
    if (reinterpret_cast<std::uintptr_t>(attacker) ==
            g_local_player_entity_address &&
        entity_number == 0 && client_address != 0 &&
        g_weapon_hook_enabled.load(std::memory_order_acquire)) {
        queue_firing_haptic(GetTickCount64());
    }
    std::memcpy(
        &weapon_definition_address,
        weapon_bytes + layout->weapon_parms_weapon_definition_offset,
        sizeof(weapon_definition_address));
    if (weapon_definition_address == 0) {
        return;
    }

    const auto weapon_definition =
        static_cast<std::uintptr_t>(weapon_definition_address);
    if (!accessible_range(
            reinterpret_cast<const void*>(
                weapon_definition + layout->weapon_definition_type_offset),
            sizeof(std::int32_t), false) ||
        !accessible_range(
            reinterpret_cast<const void*>(
                weapon_definition +
                layout->weapon_definition_ads_spread_offset),
            sizeof(float), false)) {
        return;
    }

    std::int32_t weapon_type = -1;
    float ads_spread_degrees = 0.0F;
    float native_spread_degrees = 0.0F;
    std::memcpy(
        &weapon_type,
        reinterpret_cast<const void*>(
            weapon_definition + layout->weapon_definition_type_offset),
        sizeof(weapon_type));
    std::memcpy(
        &ads_spread_degrees,
        reinterpret_cast<const void*>(
            weapon_definition +
            layout->weapon_definition_ads_spread_offset),
        sizeof(ads_spread_degrees));
    std::memcpy(
        &native_spread_degrees, spread_degrees,
        sizeof(native_spread_degrees));

    const FixedAdsSpreadGate gate{
        hook_enabled,
        reinterpret_cast<std::uintptr_t>(attacker),
        g_local_player_entity_address,
        entity_number,
        client_address != 0,
        weapon_type,
        ads_spread_degrees,
    };
    float replacement_spread_degrees = native_spread_degrees;
    if (!apply_fixed_ads_spread_override(
            gate, &replacement_spread_degrees) ||
        !g_weapon_hook_enabled.load(std::memory_order_acquire)) {
        return;
    }

    // Reaching Bullet_Fire proves that a local ordinary firearm actually
    // discharged. Identify the weapon from this call's authoritative
    // WeaponDef slot rather than a render-thread model flag, then publish the
    // next-shot lock before another usercmd can be built.
    RuntimeWeaponDefinitionIdentity fired{};
    const bool identified = find_runtime_sp_weapon_definition(
        reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(weapon_definition_address)),
        &fired);
    manual_reload_notify_local_shot(
        identified ? fired.weapon_index : 0,
        identified ? fired.definition_address : 0);

    std::memcpy(
        spread_degrees, &replacement_spread_degrees,
        sizeof(replacement_spread_degrees));
    WAWVR_STEREO_DIAG_ONCE(
        "WeaponDiag fixed local VR firearm spread: native=%.3f applied-ads=%.3f (ADS state and locomotion unchanged)",
        native_spread_degrees, replacement_spread_degrees);
}

extern "C" void __cdecl wawvr_post_update_viewmodel_pose(
    void* const viewmodel_dobj) noexcept {
    ScopedPerformanceTiming post_update_timing(
        PerformanceTimingPhase::weapon_post_update_custom);
    ActiveWeaponPoseContext context = g_active_weapon_pose;
    const ActiveHandsContext hands = g_active_hands;
    const PhysicalRocketLauncherProfile* const launcher_profile =
        context.manual_reload.weapon_definition_valid
            ? find_physical_rocket_launcher_profile(
                  context.manual_reload.weapon_internal_name.data())
            : nullptr;
    const bool exact_bazooka_viewmodel = launcher_profile != nullptr;
    const bool held_breakdown_timing_enabled =
        performance_timing_diagnostics_enabled() && context.valid &&
        context.grip_mode != WeaponGripMode::Chest;
    std::array<std::chrono::steady_clock::duration, 6>
        held_breakdown_durations{};
    std::chrono::steady_clock::time_point held_breakdown_cursor{};
    if (held_breakdown_timing_enabled) {
        held_breakdown_cursor = std::chrono::steady_clock::now();
    }
    const auto held_breakdown_checkpoint =
        [&](const std::size_t phase_index) noexcept {
            if (!held_breakdown_timing_enabled ||
                phase_index >= held_breakdown_durations.size()) {
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            held_breakdown_durations[phase_index] =
                now - held_breakdown_cursor;
            held_breakdown_cursor = now;
        };
    const auto publish_held_breakdown = [&]() noexcept {
        if (!held_breakdown_timing_enabled) {
            return;
        }
        constexpr std::array<PerformanceTimingPhase, 6> kHeldPhases{
            PerformanceTimingPhase::weapon_post_held_prepare_grip,
            PerformanceTimingPhase::weapon_post_held_pose_commit,
            PerformanceTimingPhase::weapon_post_held_tracked_models,
            PerformanceTimingPhase::weapon_post_held_muzzle_diagnostics,
            PerformanceTimingPhase::weapon_post_held_scope,
            PerformanceTimingPhase::weapon_post_held_manual_reload,
        };
        for (std::size_t phase_index = 0;
             phase_index < kHeldPhases.size(); ++phase_index) {
            record_performance_timing(
                kHeldPhases[phase_index],
                held_breakdown_durations[phase_index]);
        }
    };
    const auto submit_hands =
        [&](const TrackedHandsWeaponState& weapon_state) noexcept {
            if (hands.valid && g_viewmodel_pose_address != 0) {
                update_tracked_hands_viewmodel(
                    viewmodel_dobj,
                    reinterpret_cast<const void*>(
                        g_viewmodel_pose_address),
                    hands.controller,
                    hands.camera_origin,
                    hands.camera_axis,
                    weapon_state);
                void* projectile_model = nullptr;
                wawvr::xr::EnginePose grenade_pose{};
                if (read_manual_grenade_render_state(
                        &projectile_model, &grenade_pose)) {
                    static_cast<void>(submit_tracked_first_person_model(
                        projectile_model,
                        reinterpret_cast<const void*>(
                            g_viewmodel_pose_address),
                        grenade_pose, hands.camera_origin));
                }
            }
        };
    if (!g_weapon_hook_enabled.load(std::memory_order_acquire) ||
        viewmodel_dobj == nullptr) {
        if (!g_weapon_hook_enabled.load(std::memory_order_acquire) ||
            g_committed_weapon_grip.mode == WeaponGripMode::Chest) {
            g_retained_weapon_pose = {};
        }
        invalidate_physical_scope_snapshot();
        update_manual_reload_viewmodel(nullptr, nullptr, {});
        return;
    }

    const ViewmodelFilterStatus filter_status =
        hide_t4_viewmodel_hand_surfaces(
            viewmodel_dobj, &accessible_range);
    if (filter_status == ViewmodelFilterStatus::applied ||
        filter_status == ViewmodelFilterStatus::already_hidden) {
        WAWVR_STEREO_DIAG_ONCE(
            "WeaponDiag gun-only viewmodel active; hand/arm surfaces hidden while model-zero skeleton remains animated");
    } else {
        WAWVR_STEREO_DIAG_ONCE(
            "WeaponDiag gun-only viewmodel rejected DObj layout: %s",
            viewmodel_filter_status_name(filter_status));
    }

    if (!context.valid || g_original_update_viewmodel_pose == 0 ||
        g_cg_dobj_get_world_tag_pos == 0 ||
        !accessible_range(
            reinterpret_cast<const void*>(g_viewmodel_axis_origin_address),
            sizeof(wawvr::xr::Vec3f), true) ||
         !accessible_range(
             reinterpret_cast<const void*>(g_viewmodel_pose_address),
             0x40, false)) {
        if (g_committed_weapon_grip.mode == WeaponGripMode::Chest) {
            g_retained_weapon_pose = {};
            g_head_relative_weapon_freeze = {};
        }
        invalidate_physical_scope_snapshot();
        submit_hands({});
        update_manual_reload_viewmodel(nullptr, nullptr, {});
        return;
    }

    wawvr::xr::Vec3f grip_tag_world{};
    const char* selected_tag_name = nullptr;
    std::uint16_t selected_tag = 0;
    for (const auto& candidate : g_grip_tags) {
        std::uint16_t tag = 0;
        if (!read_tag_word(candidate.address, &tag)) {
            continue;
        }
        if (wawvr_call_dobj_get_world_tag_pos(
                viewmodel_dobj, tag,
                reinterpret_cast<const void*>(g_viewmodel_pose_address),
                &grip_tag_world) != 0) {
            selected_tag_name = candidate.name;
            selected_tag = tag;
            break;
        }
    }

    if (selected_tag_name == nullptr) {
        invalidate_physical_scope_snapshot();
        WAWVR_STEREO_DIAG_ONCE(
            "WeaponDiag no usable viewmodel grip tag; tried tag_weapon_right, tag_weapon, tag_inhand, tag_origin");
        submit_hands({});
        update_manual_reload_viewmodel(nullptr, nullptr, {});
        return;
    }

    wawvr::xr::Vec3f original_origin{};
    std::memcpy(
        &original_origin,
        reinterpret_cast<const void*>(g_viewmodel_axis_origin_address),
        sizeof(original_origin));
    wawvr::xr::Vec3f final_weapon_origin = original_origin;
    float correction_length = 0.0F;
    constexpr float kPoseRefreshOriginEpsilonSquared = 1.0e-6F;
    // A second native pose build every held frame matches COD4 literally, but
    // is too expensive in retail T4. Preserve that conservative rebuild for a
    // staged ownership transition, then rigidly translate the already-locked
    // evaluated cache on steady frames. The translation path proves the grip
    // tag moved by the exact same delta and falls back to the native rebuild if
    // that proof ever fails.
    const auto try_translate_evaluated_root =
        [&](const wawvr::xr::Vec3f& corrected_origin,
            wawvr::xr::Vec3f* const translated_tag_world) noexcept {
            if (translated_tag_world == nullptr ||
                g_viewmodel_pose_address >
                    (std::numeric_limits<std::uintptr_t>::max)() -
                        kViewmodelPoseOriginOffset) {
                return false;
            }
            void* const pose_origin_address = reinterpret_cast<void*>(
                g_viewmodel_pose_address + kViewmodelPoseOriginOffset);
            if (!accessible_range(viewmodel_dobj, kDObjSize, true) ||
                !accessible_range(
                    pose_origin_address, sizeof(wawvr::xr::Vec3f), true)) {
                return false;
            }

            ScopedDObjSkeletonLock skeleton_lock;
            if (!skeleton_lock.TryAcquire(viewmodel_dobj)) {
                WAWVR_STEREO_DIAG_ONCE(
                    "WeaponDiag evaluated skeleton was busy; retaining full pose-refresh fallback");
                return false;
            }

            wawvr::xr::Vec3f original_pose_origin{};
            std::memcpy(
                &original_pose_origin, pose_origin_address,
                sizeof(original_pose_origin));
            auto* const dobj_bytes =
                static_cast<std::uint8_t*>(viewmodel_dobj);
            std::uint8_t bone_count = 0;
            std::uint32_t skel_timestamp = 0;
            std::array<std::uint32_t, kViewmodelSkeletonBitWordCount>
                evaluated_bones{};
            EvaluatedViewmodelBoneTransform* live_matrices = nullptr;
            std::memcpy(
                &bone_count, dobj_bytes + kDObjNumBonesOffset,
                sizeof(bone_count));
            std::memcpy(
                evaluated_bones.data(),
                dobj_bytes + kDObjSkelPartBitsOffset,
                sizeof(evaluated_bones));
            std::memcpy(
                &skel_timestamp, dobj_bytes + kDObjSkelTimestampOffset,
                sizeof(skel_timestamp));
            std::memcpy(
                &live_matrices, dobj_bytes + kDObjSkelMatOffset,
                sizeof(live_matrices));
            if (bone_count == 0 ||
                bone_count > kMaximumViewmodelBoneCount ||
                live_matrices == nullptr ||
                !accessible_range(
                    live_matrices,
                    static_cast<std::size_t>(bone_count) *
                        sizeof(EvaluatedViewmodelBoneTransform),
                    true)) {
                return false;
            }

            std::array<EvaluatedViewmodelBoneTransform,
                       kMaximumViewmodelBoneCount>
                original_matrices{};
            std::memcpy(
                original_matrices.data(), live_matrices,
                static_cast<std::size_t>(bone_count) *
                    sizeof(EvaluatedViewmodelBoneTransform));
            auto translated_matrices = original_matrices;
            wawvr::xr::Vec3f translated_pose_origin = original_pose_origin;
            if (!translate_evaluated_viewmodel_skeleton(
                    original_origin, corrected_origin, evaluated_bones,
                    std::span<EvaluatedViewmodelBoneTransform>(
                        translated_matrices.data(), bone_count),
                    &translated_pose_origin)) {
                return false;
            }

            const wawvr::xr::Vec3f translation{
                corrected_origin.x - original_origin.x,
                corrected_origin.y - original_origin.y,
                corrected_origin.z - original_origin.z,
            };
            const wawvr::xr::Vec3f expected_tag_world{
                grip_tag_world.x + translation.x,
                grip_tag_world.y + translation.y,
                grip_tag_world.z + translation.z,
            };
            if (!std::isfinite(expected_tag_world.x) ||
                !std::isfinite(expected_tag_world.y) ||
                !std::isfinite(expected_tag_world.z)) {
                return false;
            }

            const auto bone_is_evaluated =
                [&](const std::size_t bone) noexcept {
                    return (evaluated_bones[bone >> 5U] &
                            (0x80000000U >> (bone & 31U))) != 0;
                };
            // This exact hook runs synchronously on the main thread at
            // 0x004698EC, immediately after the native pose update and before
            // R_AddDObjToScene publishes the DObj/pose at 0x006DA417/434.
            // The launcher also pins r_smp_backend=0 and the non-SMP issue path
            // drains frontend workers before drawing. Native tag readers drop
            // the DObj lock before consuming matrices, so these live writes
            // are safe only at this validated pre-publication phase.
            std::memcpy(
                reinterpret_cast<void*>(g_viewmodel_axis_origin_address),
                &corrected_origin, sizeof(corrected_origin));
            std::memcpy(
                pose_origin_address, &translated_pose_origin,
                sizeof(translated_pose_origin));
            for (std::size_t bone = 0; bone < bone_count; ++bone) {
                if (!bone_is_evaluated(bone)) {
                    continue;
                }
                std::memcpy(
                    &live_matrices[bone].translation,
                    &translated_matrices[bone].translation,
                    sizeof(wawvr::xr::Vec3f));
            }
            skeleton_lock.Release();

            wawvr::xr::Vec3f observed_tag_world{};
            const bool queried = wawvr_call_dobj_get_world_tag_pos(
                viewmodel_dobj, selected_tag,
                reinterpret_cast<const void*>(g_viewmodel_pose_address),
                &observed_tag_world) != 0;
            const bool translated_exactly = queried &&
                translated_viewmodel_tag_matches(
                    expected_tag_world, observed_tag_world);
            if (translated_exactly) {
                *translated_tag_world = observed_tag_world;
                WAWVR_STEREO_DIAG_ONCE(
                    "WeaponDiag translated the locked evaluated viewmodel skeleton without a second pose rebuild");
                return true;
            }

            // A failed post-translation tag proof is not expected, but restore
            // the exact cached state when it is still the state we edited.
            // The caller then takes the established native full-update path.
            bool restored = false;
            ScopedDObjSkeletonLock rollback_lock;
            if (rollback_lock.TryAcquire(viewmodel_dobj)) {
                std::uint8_t rollback_bone_count = 0;
                std::uint32_t rollback_timestamp = 0;
                std::array<std::uint32_t,
                           kViewmodelSkeletonBitWordCount>
                    rollback_bits{};
                EvaluatedViewmodelBoneTransform* rollback_matrices = nullptr;
                std::memcpy(
                    &rollback_bone_count,
                    dobj_bytes + kDObjNumBonesOffset,
                    sizeof(rollback_bone_count));
                std::memcpy(
                    rollback_bits.data(),
                    dobj_bytes + kDObjSkelPartBitsOffset,
                    sizeof(rollback_bits));
                std::memcpy(
                    &rollback_timestamp,
                    dobj_bytes + kDObjSkelTimestampOffset,
                    sizeof(rollback_timestamp));
                std::memcpy(
                    &rollback_matrices,
                    dobj_bytes + kDObjSkelMatOffset,
                    sizeof(rollback_matrices));
                bool rollback_matrices_match =
                    rollback_bone_count == bone_count &&
                    rollback_timestamp == skel_timestamp &&
                    rollback_bits == evaluated_bones &&
                    rollback_matrices == live_matrices &&
                    accessible_range(
                        rollback_matrices,
                        static_cast<std::size_t>(bone_count) *
                            sizeof(EvaluatedViewmodelBoneTransform),
                        true);
                if (rollback_matrices_match) {
                    for (std::size_t bone = 0;
                         bone < bone_count; ++bone) {
                        if (bone_is_evaluated(bone) &&
                            std::memcmp(
                                &rollback_matrices[bone].translation,
                                &translated_matrices[bone].translation,
                                sizeof(wawvr::xr::Vec3f)) != 0) {
                            rollback_matrices_match = false;
                            break;
                        }
                    }
                }
                if (rollback_matrices_match) {
                    std::memcpy(
                        reinterpret_cast<void*>(
                            g_viewmodel_axis_origin_address),
                        &original_origin, sizeof(original_origin));
                    std::memcpy(
                        pose_origin_address, &original_pose_origin,
                        sizeof(original_pose_origin));
                    for (std::size_t bone = 0;
                         bone < bone_count; ++bone) {
                        if (!bone_is_evaluated(bone)) {
                            continue;
                        }
                        std::memcpy(
                            &rollback_matrices[bone].translation,
                            &original_matrices[bone].translation,
                            sizeof(wawvr::xr::Vec3f));
                    }
                    restored = true;
                }
            }
            rollback_lock.Release();
            WAWVR_STEREO_DIAG_ONCE(
                "WeaponDiag evaluated-skeleton translation verification failed (rollback=%d); retaining full pose-refresh fallback",
                restored ? 1 : 0);
            return false;
        };
    if (context.grip_mode == WeaponGripMode::Chest) {
        wawvr::xr::Vec3f corrected_origin = original_origin;
        bool anchored = align_viewmodel_origin_to_grip(
            context.tracked_grip_world, grip_tag_world, &corrected_origin);
        if (anchored) {
            wawvr::xr::Vec3f observed{};
            if (!try_translate_evaluated_root(corrected_origin, &observed)) {
                // Preserve the same validated native fallback as held poses;
                // do not publish a controller attachment or a muzzle here.
                std::memcpy(
                    reinterpret_cast<void*>(g_viewmodel_axis_origin_address),
                    &corrected_origin, sizeof(corrected_origin));
                wawvr_call_update_viewmodel_pose(viewmodel_dobj);
                anchored = wawvr_call_dobj_get_world_tag_pos(
                    viewmodel_dobj, selected_tag,
                    reinterpret_cast<const void*>(g_viewmodel_pose_address),
                    &observed) != 0;
            }
            anchored = anchored && translated_viewmodel_tag_matches(
                context.tracked_grip_world, observed);
            if (anchored) {
                grip_tag_world = observed;
            }
        }
        static const bool diagnostics = []() noexcept {
            std::array<wchar_t, 2> value{};
            return GetEnvironmentVariableW(
                L"WAWVR_CHEST_DIAGNOSTICS", value.data(),
                static_cast<DWORD>(value.size())) == 1 && value[0] == L'1';
        }();
        static std::uint64_t last_receipt = 0;
        const auto receipt_time = GetTickCount64();
        if (diagnostics && receipt_time - last_receipt >= 500) {
            last_receipt = receipt_time;
            wawvr::xr::EnginePose head{};
            const bool head_valid = hands.valid &&
                compose_snapshot_head_world_pose(
                    hands.controller, hands.camera_origin,
                    hands.camera_axis, &head);
            stereo_diagnostic_log(
                "ChestDiag generation=%llu weapon=%llu anchored=%d tag=%s target=(%.5f %.5f %.5f) observed=(%.5f %.5f %.5f) headValid=%d head=(%.5f %.5f %.5f) headF=(%.6f %.6f %.6f) weaponF=(%.6f %.6f %.6f)",
                static_cast<unsigned long long>(context.controller_generation),
                static_cast<unsigned long long>(context.weapon_identity),
                anchored ? 1 : 0, selected_tag_name,
                context.tracked_grip_world.x, context.tracked_grip_world.y,
                context.tracked_grip_world.z,
                grip_tag_world.x, grip_tag_world.y, grip_tag_world.z,
                head_valid ? 1 : 0, head.position.x, head.position.y,
                head.position.z, head.axis.forward.x, head.axis.forward.y,
                head.axis.forward.z, context.weapon_axis.forward.x,
                context.weapon_axis.forward.y, context.weapon_axis.forward.z);
        }
        if (!anchored) {
            WAWVR_STEREO_DIAG_ONCE(
                "ChestDiag evaluated chest grip alignment unavailable; no held attachment committed");
        }
        g_retained_weapon_pose = {};
        g_head_relative_weapon_freeze = {};
        invalidate_physical_scope_snapshot();
        submit_hands({});
        update_manual_reload_viewmodel(nullptr, nullptr, {});
        return;
    }
    held_breakdown_checkpoint(0);
    // Match COD4's fixed controller attachment contract. A new owner aligns
    // the evaluated grip tag and captures that result exactly once. The
    // pair-to-left bolt-work handoff already transferred the exact visible
    // root, so preserve that root rather than dragging the rear grip tag onto
    // the forward support controller. Steady held frames restore the immutable
    // controller root after T4 evaluation, preventing authored tag movement
    // from steering the whole rifle.
    const bool preserve_pair_to_left_root =
        context.transition_pending &&
        context.grip_mode == WeaponGripMode::LeftHand &&
        context.support_pose_latched &&
        !context.align_grip_tag_before_commit;
    const PostT4HeldPosePolicy post_pose_policy =
        post_t4_held_pose_policy(
            context.grip_mode, context.transition_pending,
            context.align_grip_tag_before_commit,
            preserve_pair_to_left_root);
    if (preserve_pair_to_left_root) {
        WAWVR_STEREO_DIAG_ONCE(
            "WeaponDiag preserving the exact transferred pair root during right-release bolt handoff");
    }
    const bool force_native_pose_rebuild =
        post_pose_policy.recapture_attachment;
    const bool align_visible_rear_grip =
        post_pose_policy.align_visible_grip;
    if (align_visible_rear_grip) {
        wawvr::xr::Vec3f corrected_origin = original_origin;
        if (!align_viewmodel_origin_to_grip(
                context.tracked_grip_world, grip_tag_world,
                &corrected_origin)) {
            invalidate_physical_scope_snapshot();
            WAWVR_STEREO_DIAG_ONCE(
                "WeaponDiag rejected non-finite or implausible grip-tag correction");
            submit_hands({});
            update_manual_reload_viewmodel(nullptr, nullptr, {});
            return;
        }

        const float correction_x = corrected_origin.x - original_origin.x;
        const float correction_y = corrected_origin.y - original_origin.y;
        const float correction_z = corrected_origin.z - original_origin.z;
        const float correction_squared =
            correction_x * correction_x + correction_y * correction_y +
            correction_z * correction_z;
        correction_length = std::sqrt(correction_squared);
        const bool refresh_pose = context.transition_pending ||
            context.align_grip_tag_before_commit ||
            correction_squared > kPoseRefreshOriginEpsilonSquared;
        if (refresh_pose) {
            wawvr::xr::Vec3f translated_tag_world{};
            const bool translated_without_rebuild =
                !force_native_pose_rebuild &&
                try_translate_evaluated_root(
                    corrected_origin, &translated_tag_world);
            if (translated_without_rebuild) {
                grip_tag_world = translated_tag_world;
            } else {
                std::memcpy(
                    reinterpret_cast<void*>(g_viewmodel_axis_origin_address),
                    &corrected_origin, sizeof(corrected_origin));
                wawvr_call_update_viewmodel_pose(viewmodel_dobj);
                wawvr::xr::Vec3f refreshed_grip_tag_world{};
                if (wawvr_call_dobj_get_world_tag_pos(
                        viewmodel_dobj, selected_tag,
                        reinterpret_cast<const void*>(
                            g_viewmodel_pose_address),
                        &refreshed_grip_tag_world) != 0) {
                    grip_tag_world = refreshed_grip_tag_world;
                } else {
                    grip_tag_world = context.tracked_grip_world;
                }
                WAWVR_STEREO_DIAG_ONCE(
                    "WeaponDiag COD4-style full native pose rebuild active after held-root correction");
            }
            final_weapon_origin = corrected_origin;
        }
    } else if (post_pose_policy.reapply_controller_root) {
        // The outer placement hook has already calibrated an exact continuous
        // rifle root for the owning controller. T4's normal viewmodel
        // evaluation can overwrite that origin after the placement call.
        // Reapply it for every steady held mode so authored hand/support
        // animation may move inside the root but cannot steer or translate the
        // rifle itself.
        const float reapply_x = context.weapon_origin.x - original_origin.x;
        const float reapply_y = context.weapon_origin.y - original_origin.y;
        const float reapply_z = context.weapon_origin.z - original_origin.z;
        const float reapply_squared =
            reapply_x * reapply_x + reapply_y * reapply_y +
            reapply_z * reapply_z;
        const bool refresh_pose = context.transition_pending ||
            context.align_grip_tag_before_commit ||
            reapply_squared > kPoseRefreshOriginEpsilonSquared;
        if (refresh_pose) {
            wawvr::xr::Vec3f translated_tag_world{};
            const bool translated_without_rebuild =
                !force_native_pose_rebuild &&
                try_translate_evaluated_root(
                    context.weapon_origin, &translated_tag_world);
            if (translated_without_rebuild) {
                grip_tag_world = translated_tag_world;
            } else {
                std::memcpy(
                    reinterpret_cast<void*>(g_viewmodel_axis_origin_address),
                    &context.weapon_origin, sizeof(context.weapon_origin));
                wawvr_call_update_viewmodel_pose(viewmodel_dobj);
                wawvr::xr::Vec3f refreshed_grip_tag_world{};
                if (wawvr_call_dobj_get_world_tag_pos(
                        viewmodel_dobj, selected_tag,
                        reinterpret_cast<const void*>(g_viewmodel_pose_address),
                        &refreshed_grip_tag_world) != 0) {
                    // Manual bolt and receiver anchors must use the
                    // post-reapply tag, not the stale point sampled before the
                    // calibrated root was restored above.
                    grip_tag_world = refreshed_grip_tag_world;
                }
            }
            final_weapon_origin = context.weapon_origin;
        }
    } else {
        final_weapon_origin = original_origin;
    }

    // Capture a post-T4 attachment only while establishing a transition. A
    // steady held pose keeps its committed calibration, so animation cannot
    // become a second controller or accumulate into attachment drift.
    WeaponAttachmentState post_attachment{};
    WeaponAttachmentState* const committed_attachment =
        weapon_attachment_for_mode(context.grip_mode);
    const bool recapture_attachment_after_post =
        context.recapture_attachment_after_post &&
        post_pose_policy.recapture_attachment;
    if (recapture_attachment_after_post &&
        (committed_attachment == nullptr ||
         !calibrate_controller_weapon_attachment(
              context.controller_camera_origin,
              context.controller_camera_axis,
              context.controller_pose, final_weapon_origin,
             context.weapon_axis, &post_attachment))) {
        invalidate_physical_scope_snapshot();
        submit_hands({});
        update_manual_reload_viewmodel(nullptr, nullptr, {});
        WAWVR_STEREO_DIAG_ONCE(
            "WeaponDiag post-T4 controller attachment recapture failed; pose transition remains pending");
        return;
    }
    const bool transition_still_desired =
        g_weapon_grip_state.weapon_identity == context.weapon_identity &&
        g_weapon_grip_state.mode == context.grip_mode;
    if (context.transition_pending &&
        context.grip_mode == WeaponGripMode::TwoHand &&
        !context.commit_right_ray_two_hand_steering_after_post) {
        invalidate_physical_scope_snapshot();
        submit_hands({});
        update_manual_reload_viewmodel(nullptr, nullptr, {});
        WAWVR_STEREO_DIAG_ONCE(
            "WeaponDiag rejected pending two-hand transition without a coherent right-ray support constraint");
        return;
    }
    if (!context.transition_pending || transition_still_desired) {
        if (recapture_attachment_after_post) {
            *committed_attachment = post_attachment;
        }
        if (context.commit_right_ray_two_hand_steering_after_post) {
            g_right_ray_two_hand_steering =
                context.staged_right_ray_two_hand_steering;
        } else if (context.reset_right_ray_two_hand_steering_after_post) {
            reset_right_ray_two_hand_steering(
                &g_right_ray_two_hand_steering);
        }
        if (context.transition_pending) {
            // Publish attachment and committed mode together only after every
            // post-T4 prerequisite has succeeded. A changed logical latch
            // leaves both outgoing values untouched and retries naturally.
            g_committed_weapon_grip = {
                context.grip_mode, context.weapon_identity};
            WAWVR_STEREO_DIAG_ONCE(
                "WeaponDiag committed controller pose transition after post-T4 grip-tag validation");
        }
        // Input-side ADS, scope, and two-hand melee must observe the same
        // validated ownership frame as the rendered attachment and filters.
        // A rejected transition therefore leaves the last committed support
        // latch untouched.
        g_published_support_pose.store(
            context.support_pose_latched, std::memory_order_release);
    }
    context.weapon_origin = final_weapon_origin;
    const bool head_relative_freeze_capture_settled =
        context.grip_mode != WeaponGripMode::TwoHand ||
        g_right_ray_two_hand_steering.valid;
    if (!current_head_local_weapon_enabled() &&
        head_relative_weapon_freeze_enabled() &&
        !context.transition_pending && context.refresh_retained_pose &&
        context.grip_mode != WeaponGripMode::Chest && hands.valid &&
        hands.controller.generation == context.controller_generation &&
        head_relative_freeze_capture_settled &&
        (!g_head_relative_weapon_freeze.freeze.valid ||
         g_head_relative_weapon_freeze.weapon_identity !=
             context.weapon_identity ||
         g_head_relative_weapon_freeze.grip_mode != context.grip_mode)) {
        wawvr::xr::EnginePose head_world{};
        const wawvr::xr::EnginePose weapon_world{
            final_weapon_origin, context.weapon_axis};
        HeadRelativePoseFreeze captured{};
        if (compose_snapshot_head_world_pose(
                hands.controller, context.camera_origin,
                context.camera_axis, &head_world) &&
            capture_head_relative_pose_freeze(
                head_world, weapon_world, &captured)) {
            g_head_relative_weapon_freeze = {
                captured, context.weapon_identity, context.grip_mode,
                context.controller_generation};
            if (context.grip_mode == WeaponGripMode::TwoHand) {
                WAWVR_STEREO_DIAG_ONCE(
                    "WeaponDiag head-relative freeze A/B captured the settled two-hand post-T4 rifle pose");
            } else {
                WAWVR_STEREO_DIAG_ONCE(
                    "WeaponDiag head-relative freeze A/B captured the final post-T4 one-hand rifle pose");
            }
        } else {
            g_head_relative_weapon_freeze = {};
            WAWVR_STEREO_DIAG_ONCE(
                "WeaponDiag head-relative freeze A/B rejected an incoherent post-T4 head/weapon pair");
        }
    }
    if ((!context.transition_pending || transition_still_desired) &&
        context.refresh_retained_pose) {
        g_retained_weapon_pose = {
            .valid = true,
            .weapon_identity = context.weapon_identity,
            .controller_generation = context.controller_generation,
            .publication_milliseconds =
                context.controller_publication_milliseconds,
            .grip_mode = context.grip_mode,
            .origin = final_weapon_origin,
            .axis = context.weapon_axis,
            .grip_tag_valid = true,
            .grip_tag_world = grip_tag_world,
        };
    }
    if (!context.transition_pending || transition_still_desired) {
        publish_weapon_frame_base_receipt(context, hands);
    }
    held_breakdown_checkpoint(1);

    // The retail Bazooka's authored view animation pitches/yaws the visible
    // tube away from the rigid tracked root by roughly seven degrees. Keep the
    // already-audited projectile/controller basis authoritative, and rotate
    // only the rendered root around the gripping hand so the tube, tag_flash,
    // muzzle effect, and projectile line agree. Rebuilding through the native
    // pose function keeps evaluated and lazily evaluated bones coherent.
    if (exact_bazooka_viewmodel &&
        context.grip_mode != WeaponGripMode::Chest &&
        !bazooka_visual_alignment_disabled_by_environment()) {
        constexpr float kMinimumCorrectedBoreDot = 0.9995F;
        constexpr float kMaximumGripDriftIwUnits = 0.10F;
        const auto vector_distance = [](
            const wawvr::xr::Vec3f& first,
            const wawvr::xr::Vec3f& second) noexcept {
            const float x = first.x - second.x;
            const float y = first.y - second.y;
            const float z = first.z - second.z;
            return std::sqrt(x * x + y * y + z * z);
        };
        const auto read_bazooka_bore = [viewmodel_dobj, launcher_profile](
            wawvr::xr::Vec3f* const brass_world,
            wawvr::xr::Vec3f* const flash_world,
            wawvr::xr::Basis3f* const bore_basis,
            float* const bore_length,
            float* const flash_alignment) noexcept {
            if (brass_world == nullptr || flash_world == nullptr ||
                bore_basis == nullptr || g_cg_dobj_get_world_tag_matrix == 0) {
                return false;
            }
            std::uint16_t brass_tag = 0;
            std::uint16_t flash_tag = 0;
            std::array<float, 9> flash_matrix{};
            if (!read_tag_word(g_tag_brass_address, &brass_tag) ||
                !read_tag_word(g_tag_flash_address, &flash_tag) ||
                wawvr_call_dobj_get_world_tag_pos(
                    viewmodel_dobj, brass_tag,
                    reinterpret_cast<const void*>(g_viewmodel_pose_address),
                    brass_world) == 0 ||
                wawvr_call_dobj_get_world_tag_matrix(
                    viewmodel_dobj, flash_tag,
                    reinterpret_cast<const void*>(g_viewmodel_pose_address),
                    flash_matrix.data(), flash_world) == 0) {
                return false;
            }
            const wawvr::xr::Basis3f flash_basis{
                .forward = {
                    flash_matrix[0], flash_matrix[1], flash_matrix[2]},
                .left = {
                    flash_matrix[3], flash_matrix[4], flash_matrix[5]},
                .up = {
                    flash_matrix[6], flash_matrix[7], flash_matrix[8]},
            };
            wawvr::xr::Basis3f rigid_tube_basis{};
            std::uint8_t root_bone = 0;
            if (launcher_profile->use_rigid_tube_axis &&
                !read_evaluated_viewmodel_weapon_root_basis(
                    viewmodel_dobj, &rigid_tube_basis, &root_bone,
                    launcher_profile)) {
                return false;
            }
            return build_validated_physical_launcher_bore_basis(
                launcher_profile, *brass_world, *flash_world, flash_basis,
                launcher_profile->use_rigid_tube_axis
                    ? &rigid_tube_basis : nullptr,
                bore_basis, bore_length, flash_alignment);
        };

        bool correction_attempted = false;
        bool correction_applied = false;
        bool rollback_proven = false;
        float correction_degrees =
            (std::numeric_limits<float>::quiet_NaN)();
        float before_bore_dot =
            (std::numeric_limits<float>::quiet_NaN)();
        float after_bore_dot =
            (std::numeric_limits<float>::quiet_NaN)();
        float grip_drift =
            (std::numeric_limits<float>::quiet_NaN)();
        ViewmodelAxisPlacement original_placement{};
        wawvr::xr::Basis3f tracked_basis{};
        wawvr::xr::Vec3f before_brass{};
        wawvr::xr::Vec3f before_flash{};
        wawvr::xr::Basis3f before_bore{};
        float before_bore_length = 0.0F;
        float before_flash_alignment = 0.0F;
        const bool prerequisites_valid =
            g_viewmodel_axis_address != 0 &&
            accessible_range(
                reinterpret_cast<const void*>(g_viewmodel_axis_address),
                sizeof(original_placement), true) &&
            build_evaluated_projectile_basis(
                context.weapon_axis.forward, context.weapon_axis,
                &tracked_basis) &&
            read_bazooka_bore(
                &before_brass, &before_flash, &before_bore,
                &before_bore_length, &before_flash_alignment);
        if (prerequisites_valid) {
            std::memcpy(
                &original_placement,
                reinterpret_cast<const void*>(g_viewmodel_axis_address),
                sizeof(original_placement));
            before_bore_dot =
                before_bore.forward.x * tracked_basis.forward.x +
                before_bore.forward.y * tracked_basis.forward.y +
                before_bore.forward.z * tracked_basis.forward.z;
            BazookaViewmodelAlignment alignment{};
            if (calculate_physical_launcher_viewmodel_alignment(
                    launcher_profile, original_placement.origin,
                    original_placement.axis,
                    grip_tag_world, before_bore, tracked_basis, &alignment)) {
                correction_attempted = true;
                correction_degrees = alignment.correction_degrees;
                const ViewmodelAxisPlacement corrected_placement{
                    alignment.corrected_root_axis,
                    alignment.corrected_root_origin,
                };
                std::memcpy(
                    reinterpret_cast<void*>(g_viewmodel_axis_address),
                    &corrected_placement, sizeof(corrected_placement));
                wawvr_call_update_viewmodel_pose(viewmodel_dobj);

                wawvr::xr::Vec3f corrected_grip{};
                wawvr::xr::Vec3f after_brass{};
                wawvr::xr::Vec3f after_flash{};
                wawvr::xr::Basis3f after_bore{};
                float after_bore_length = 0.0F;
                float after_flash_alignment = 0.0F;
                const bool corrected_grip_available =
                    wawvr_call_dobj_get_world_tag_pos(
                        viewmodel_dobj, selected_tag,
                        reinterpret_cast<const void*>(
                            g_viewmodel_pose_address),
                        &corrected_grip) != 0;
                const bool corrected_bore_available = read_bazooka_bore(
                    &after_brass, &after_flash, &after_bore,
                    &after_bore_length, &after_flash_alignment);
                if (corrected_grip_available && corrected_bore_available) {
                    after_bore_dot =
                        after_bore.forward.x * tracked_basis.forward.x +
                        after_bore.forward.y * tracked_basis.forward.y +
                        after_bore.forward.z * tracked_basis.forward.z;
                    grip_drift = vector_distance(
                        grip_tag_world, corrected_grip);
                    correction_applied =
                        std::isfinite(after_bore_dot) &&
                        after_bore_dot >= kMinimumCorrectedBoreDot &&
                        std::isfinite(grip_drift) &&
                        grip_drift <= kMaximumGripDriftIwUnits;
                    if (correction_applied) {
                        grip_tag_world = corrected_grip;
                    }
                }

                if (!correction_applied) {
                    std::memcpy(
                        reinterpret_cast<void*>(g_viewmodel_axis_address),
                        &original_placement, sizeof(original_placement));
                    wawvr_call_update_viewmodel_pose(viewmodel_dobj);
                    wawvr::xr::Vec3f restored_grip{};
                    if (wawvr_call_dobj_get_world_tag_pos(
                            viewmodel_dobj, selected_tag,
                            reinterpret_cast<const void*>(
                                g_viewmodel_pose_address),
                            &restored_grip) != 0) {
                        rollback_proven =
                            vector_distance(grip_tag_world, restored_grip) <=
                            kMaximumGripDriftIwUnits;
                        grip_tag_world = restored_grip;
                    }
                }
            }
        }

        if (launcher_profile == &kPanzerschreckPhysicalLauncher) {
            static std::atomic<std::uint64_t> last_panzer_alignment_log_ms{0};
            static std::atomic<std::uint32_t> panzer_alignment_logs{0};
            const auto sample_ms = GetTickCount64();
            auto previous_ms = last_panzer_alignment_log_ms.load(
                std::memory_order_relaxed);
            if (panzer_alignment_logs.load(std::memory_order_relaxed) < 12 &&
                sample_ms >= previous_ms && sample_ms - previous_ms >= 2000 &&
                last_panzer_alignment_log_ms.compare_exchange_strong(
                    previous_ms, sample_ms, std::memory_order_relaxed)) {
                const auto log_index = panzer_alignment_logs.fetch_add(
                    1, std::memory_order_relaxed);
                const float observed_angle = std::isfinite(before_bore_dot)
                    ? std::acos(std::clamp(before_bore_dot, -1.0F, 1.0F)) *
                          (180.0F / 3.14159265358979323846F)
                    : -1.0F;
                stereo_diagnostic_log(
                    "WeaponDiag Panzerschreck settled visual sample[%u]: generation=%llu gripMode=%u right=%d left=%d prerequisites=%d observedAngle=%.5f attempted=%d applied=%d beforeDot=%.6f afterDot=%.6f gripDrift=%.5f rootF=%.6f %.6f %.6f trackedF=%.6f %.6f %.6f",
                    log_index,
                    static_cast<unsigned long long>(context.controller_generation),
                    static_cast<unsigned>(context.grip_mode),
                    context.right_gripping ? 1 : 0, context.left_gripping ? 1 : 0,
                    prerequisites_valid ? 1 : 0, observed_angle,
                    correction_attempted ? 1 : 0, correction_applied ? 1 : 0,
                    before_bore_dot, after_bore_dot, grip_drift,
                    before_bore.forward.x, before_bore.forward.y,
                    before_bore.forward.z, tracked_basis.forward.x,
                    tracked_basis.forward.y, tracked_basis.forward.z);
            }
        }

        if (correction_applied) {
            WAWVR_STEREO_DIAG_ONCE(
                "WeaponDiag exact Bazooka visual bore alignment active: correction=%.3f beforeDot=%.6f afterDot=%.6f gripDrift=%.5f boreLength=%.3f flashDot=%.6f",
                correction_degrees, before_bore_dot, after_bore_dot,
                grip_drift, before_bore_length, before_flash_alignment);
        } else {
            WAWVR_STEREO_DIAG_ONCE(
                "WeaponDiag exact Bazooka visual bore alignment rejected: prerequisites=%d attempted=%d correction=%.3f beforeDot=%.6f afterDot=%.6f gripDrift=%.5f rollback=%d",
                prerequisites_valid ? 1 : 0,
                correction_attempted ? 1 : 0, correction_degrees,
                before_bore_dot, after_bore_dot, grip_drift,
                rollback_proven ? 1 : 0);
        }
    } else if (exact_bazooka_viewmodel &&
               bazooka_visual_alignment_disabled_by_environment()) {
        WAWVR_STEREO_DIAG_ONCE(
            "WeaponDiag exact Bazooka visual bore alignment disabled by WAWVR_DISABLE_BAZOOKA_VISUAL_ALIGNMENT=1");
    }

    const bool pistol_support =
        context.manual_reload.weapon_definition_valid &&
        pistol_support_pose_for_weapon_name_buffer({
            context.manual_reload.weapon_internal_name.data(),
            context.manual_reload.weapon_internal_name.size()});
    bool pistol_idle = false;
    if (pistol_support && context.manual_reload.player_state != nullptr &&
        accessible_range(context.manual_reload.player_state, 0x10C, false)) {
        std::int32_t native_weapon_state = -1;
        std::memcpy(&native_weapon_state,
            static_cast<const std::uint8_t*>(context.manual_reload.player_state) + 0x108,
            sizeof(native_weapon_state));
        pistol_idle = native_weapon_state == 0;
    }
    submit_hands({
        context.right_gripping,
        context.left_gripping,
        true,
        context.weapon_axis,
        pistol_support,
        (static_cast<std::uint64_t>(context.manual_reload.weapon_definition_address) << 32U) |
            static_cast<std::uint32_t>(context.manual_reload.weapon_index),
        pistol_idle,
    });
    held_breakdown_checkpoint(2);

    std::uint16_t brass_tag = 0;
    wawvr::xr::Vec3f brass_world{};
    bool brass_tag_available = false;
    if (exact_bazooka_viewmodel &&
        read_tag_word(g_tag_brass_address, &brass_tag) &&
        wawvr_call_dobj_get_world_tag_pos(
            viewmodel_dobj, brass_tag,
            reinterpret_cast<const void*>(g_viewmodel_pose_address),
            &brass_world) != 0) {
        brass_tag_available = true;
    }

    std::uint16_t flash_tag = 0;
    wawvr::xr::Vec3f muzzle_world{};
    bool muzzle_tag_available = false;
    std::array<float, 9> flash_matrix{};
    bool flash_matrix_available = false;
    if (read_tag_word(g_tag_flash_address, &flash_tag)) {
        if (g_cg_dobj_get_world_tag_matrix != 0 &&
            wawvr_call_dobj_get_world_tag_matrix(
                viewmodel_dobj, flash_tag,
                reinterpret_cast<const void*>(g_viewmodel_pose_address),
                flash_matrix.data(), &muzzle_world) != 0) {
            muzzle_tag_available = true;
            flash_matrix_available = true;
        } else if (wawvr_call_dobj_get_world_tag_pos(
                       viewmodel_dobj, flash_tag,
                       reinterpret_cast<const void*>(
                           g_viewmodel_pose_address),
                       &muzzle_world) != 0) {
            muzzle_tag_available = true;
        }
    }
    // Retain this pass's exact shot ray for optical alignment below. Do not
    // read back a possibly stale global publication during a weapon change.
    wawvr::xr::Basis3f evaluated_projectile_basis{};
    bool evaluated_projectile_basis_valid = false;
    if (muzzle_tag_available) {
        wawvr::xr::Basis3f weapon_root_basis{};
        std::uint8_t weapon_root_bone_index = 0;
        const bool weapon_root_basis_valid = exact_bazooka_viewmodel &&
            read_evaluated_viewmodel_weapon_root_basis(
                viewmodel_dobj, &weapon_root_basis,
                &weapon_root_bone_index, launcher_profile);
        bool bazooka_geometry_basis_valid = false;
        float bazooka_bore_length = 0.0F;
        float bazooka_flash_alignment = 0.0F;
        if (flash_matrix_available) {
            const wawvr::xr::Basis3f raw_flash_basis{
                .forward = {
                    flash_matrix[0], flash_matrix[1], flash_matrix[2]},
                // T4's axis convention is forward/left/up (AnglesToAxis
                // stores the negated AngleVectors right vector in row one),
                // matching Basis3f directly.
                .left = {
                    flash_matrix[3], flash_matrix[4], flash_matrix[5]},
                .up = {
                    flash_matrix[6], flash_matrix[7], flash_matrix[8]},
            };
            if (exact_bazooka_viewmodel && brass_tag_available) {
                bazooka_geometry_basis_valid =
                    build_validated_physical_launcher_bore_basis(
                        launcher_profile, brass_world, muzzle_world,
                        raw_flash_basis,
                        weapon_root_basis_valid ? &weapon_root_basis : nullptr,
                        &evaluated_projectile_basis,
                        &bazooka_bore_length,
                        &bazooka_flash_alignment);
            }
            if (exact_bazooka_viewmodel) {
                // A malformed or unavailable rear tag must not make the
                // complete physical-launcher path disappear. tag_flash row
                // zero is the audited IW forward axis and remains the atomic
                // roll-preserving fallback for this frame.
                evaluated_projectile_basis_valid =
                    bazooka_geometry_basis_valid ||
                    build_evaluated_projectile_basis(
                        raw_flash_basis.forward, raw_flash_basis,
                        &evaluated_projectile_basis);
            } else if (
                context.manual_reload.weapon_definition_valid &&
                hitscan_uses_tag_flash_forward(std::string_view{
                    context.manual_reload.weapon_internal_name.data()})) {
                // These exact weapons must not infer bore direction from
                // an off-axis grip anchor. Preserve the evaluated muzzle's
                // forward/roll for both authoritative and visual shots.
                evaluated_projectile_basis_valid =
                    build_evaluated_projectile_basis(
                        raw_flash_basis.forward, raw_flash_basis,
                        &evaluated_projectile_basis);
                if (evaluated_projectile_basis_valid) {
                    const wawvr::xr::Vec3f shared_direction{
                        muzzle_world.x - grip_tag_world.x,
                        muzzle_world.y - grip_tag_world.y,
                        muzzle_world.z - grip_tag_world.z,
                    };
                    wawvr::xr::Basis3f shared_basis{};
                    const bool shared_basis_valid =
                        build_evaluated_projectile_basis(
                            shared_direction, raw_flash_basis,
                            &shared_basis);
                    const float shared_flash_dot = shared_basis_valid
                        ? shared_basis.forward.x *
                                  evaluated_projectile_basis.forward.x +
                              shared_basis.forward.y *
                                  evaluated_projectile_basis.forward.y +
                              shared_basis.forward.z *
                                  evaluated_projectile_basis.forward.z
                        : 0.0F;
                    if (std::string_view{
                            context.manual_reload.weapon_internal_name.data()} ==
                        "svt40") {
                        WAWVR_STEREO_DIAG_ONCE(
                            "WeaponDiag exact SVT40 tag_flash barrel axis active; grip-to-muzzle dot=%.6f gripF=%.6f %.6f %.6f flashF=%.6f %.6f %.6f flashLeft=%.6f %.6f %.6f flashUp=%.6f %.6f %.6f",
                            shared_flash_dot,
                            shared_basis.forward.x, shared_basis.forward.y,
                            shared_basis.forward.z,
                            evaluated_projectile_basis.forward.x,
                            evaluated_projectile_basis.forward.y,
                            evaluated_projectile_basis.forward.z,
                            evaluated_projectile_basis.left.x,
                            evaluated_projectile_basis.left.y,
                            evaluated_projectile_basis.left.z,
                            evaluated_projectile_basis.up.x,
                            evaluated_projectile_basis.up.y,
                            evaluated_projectile_basis.up.z);
                    } else {
                        WAWVR_STEREO_DIAG_ONCE(
                            "WeaponDiag exact PPSh tag_flash barrel axis active; rejected elevated grip-to-muzzle basis dot=%.6f",
                            shared_flash_dot);
                    }
                }
            } else {
                // Preserve the previously accepted path for other weapons.
                // This segment is a per-asset approximation, not a universal
                // bore reference; exact audited exceptions are above. Both
                // authoritative shots and client impacts use this same basis.
                const wawvr::xr::Vec3f evaluated_barrel_direction{
                    muzzle_world.x - grip_tag_world.x,
                    muzzle_world.y - grip_tag_world.y,
                    muzzle_world.z - grip_tag_world.z,
                };
                evaluated_projectile_basis_valid =
                    build_evaluated_projectile_basis(
                        evaluated_barrel_direction, raw_flash_basis,
                        &evaluated_projectile_basis);
            }
        }
        const bool validated_launcher_root = weapon_root_basis_valid &&
            (!launcher_profile->use_rigid_tube_axis ||
             bazooka_geometry_basis_valid);
        publish_muzzle(
            context.controller_generation, muzzle_world,
            evaluated_projectile_basis_valid
                ? &evaluated_projectile_basis
                : nullptr,
            validated_launcher_root ? &weapon_root_basis : nullptr);
        context.manual_reload.muzzle_world = muzzle_world;
        context.manual_reload.muzzle_valid = true;
        WAWVR_STEREO_DIAG_ONCE(
            "WeaponDiag published corrected tag_flash muzzle at %.2f %.2f %.2f",
            muzzle_world.x, muzzle_world.y, muzzle_world.z);
        if (evaluated_projectile_basis_valid) {
            WAWVR_STEREO_DIAG_ONCE(
                "WeaponDiag published evaluated projectile basis forward=%.4f %.4f %.4f",
                evaluated_projectile_basis.forward.x,
                evaluated_projectile_basis.forward.y,
                evaluated_projectile_basis.forward.z);
            if (exact_bazooka_viewmodel &&
                bazooka_geometry_basis_valid) {
                WAWVR_STEREO_DIAG_ONCE(
                    "WeaponDiag Bazooka bore validated tag_brass=%.2f %.2f %.2f tag_flash=%.2f %.2f %.2f length=%.3f row0Dot=%.6f",
                    brass_world.x, brass_world.y, brass_world.z,
                    muzzle_world.x, muzzle_world.y, muzzle_world.z,
                    bazooka_bore_length, bazooka_flash_alignment);
            } else if (exact_bazooka_viewmodel) {
                WAWVR_STEREO_DIAG_ONCE(
                    "WeaponDiag Bazooka bore geometry unavailable or rejected; using tag_flash row0 fallback (tagBrassAvailable=%d)",
                    brass_tag_available ? 1 : 0);
            }
        } else {
            WAWVR_STEREO_DIAG_ONCE(
                "WeaponDiag tag_flash position published without a validated orientation matrix; projectile direction remains native");
        }
        if (exact_bazooka_viewmodel && weapon_root_basis_valid) {
            wawvr::xr::Basis3f tracked_basis{};
            const bool tracked_basis_valid =
                build_evaluated_projectile_basis(
                    context.weapon_axis.forward, context.weapon_axis,
                    &tracked_basis);
            const float root_tag_dot = evaluated_projectile_basis_valid
                ? weapon_root_basis.forward.x *
                          evaluated_projectile_basis.forward.x +
                      weapon_root_basis.forward.y *
                          evaluated_projectile_basis.forward.y +
                      weapon_root_basis.forward.z *
                          evaluated_projectile_basis.forward.z
                : 0.0F;
            const float root_tracked_dot = tracked_basis_valid
                ? weapon_root_basis.forward.x * tracked_basis.forward.x +
                      weapon_root_basis.forward.y * tracked_basis.forward.y +
                      weapon_root_basis.forward.z * tracked_basis.forward.z
                : 0.0F;
            WAWVR_STEREO_DIAG_ONCE(
                "WeaponDiag Bazooka rigid tube root published bone=%u forward=%.4f %.4f %.4f tagBoreDot=%.6f trackedDot=%.6f",
                static_cast<unsigned>(weapon_root_bone_index),
                weapon_root_basis.forward.x,
                weapon_root_basis.forward.y,
                weapon_root_basis.forward.z,
                root_tag_dot, root_tracked_dot);
        } else if (exact_bazooka_viewmodel) {
            WAWVR_STEREO_DIAG_ONCE(
                "WeaponDiag Bazooka rigid tube root unavailable; exact final rocket route remains fail-closed");
        }
    }
    observe_weapon_pose_pipeline_diagnostic(
        context, hands, grip_tag_world, muzzle_world,
        muzzle_tag_available);
    observe_post_t4_visible_aim_diagnostic(
        context, hands, grip_tag_world, muzzle_world,
        muzzle_tag_available);

    context.manual_reload.rifle_grip_world = grip_tag_world;
    held_breakdown_checkpoint(3);

    RuntimeWeaponDefinitionIdentity scope_identity{};
    PhysicalScopeSnapshot scope_snapshot{};
    const PhysicalScopeProfile* scope_profile = nullptr;
    if (context.manual_reload.weapon_index > 0 &&
        read_runtime_sp_weapon_definition_snapshot(
            context.manual_reload.weapon_index, &scope_identity)) {
        scope_profile = find_physical_scope_profile(scope_identity.name.data());
    }
    wawvr::xr::Vec3f scope_lens_tag_world{};
    const char* selected_scope_tag_name = nullptr;
    bool selected_scope_tag_is_generic_optic_anchor = false;
    const bool scope_lens_tag_available = scope_profile != nullptr &&
        read_scope_lens_tag(
            viewmodel_dobj,
            reinterpret_cast<const void*>(g_viewmodel_pose_address),
            &scope_lens_tag_world, &selected_scope_tag_name,
            &selected_scope_tag_is_generic_optic_anchor);
    const wawvr::xr::Vec3f exact_rear_lens_tag_offset{};
    const wawvr::xr::Vec3f* const scope_lens_tag_local_offset =
        !scope_lens_tag_available
            ? nullptr
            : selected_scope_tag_is_generic_optic_anchor
                ? &scope_profile->lens_from_scope_tag_weapon_local
                : &exact_rear_lens_tag_offset;
    if (build_physical_scope_snapshot(
            scope_profile, context.manual_reload.weapon_index,
            context.controller_generation, GetTickCount64(),
            context.right_gripping, context.left_gripping,
            context.support_pose_latched,
            scope_lens_tag_available ? &scope_lens_tag_world : nullptr,
            scope_lens_tag_local_offset,
            grip_tag_world, context.weapon_axis,
            context.manual_reload.camera_origin,
            context.manual_reload.camera_axis, &scope_snapshot)) {
        if (std::string_view{scope_identity.name.data()} == "mosin_rifle_scoped") {
            const bool aligned = muzzle_tag_available &&
                evaluated_projectile_basis_valid &&
                align_physical_scope_camera_to_projectile(
                    muzzle_world, evaluated_projectile_basis, &scope_snapshot);
            if (aligned) {
                const auto& shot = evaluated_projectile_basis.forward;
                const auto& old_axis = scope_snapshot.lens_axis_world;
                const auto projection = [&shot](const wawvr::xr::Vec3f& axis) {
                    return shot.x * axis.x + shot.y * axis.y + shot.z * axis.z;
                };
                constexpr float kRadiansToDegrees = 180.0F / 3.14159265358979323846F;
                WAWVR_STEREO_DIAG_ONCE(
                    "ScopeDiag Mosin optical camera aligned to same-pass projectile: generation=%llu oldRayLeft=%.4fdeg oldRayUp=%.4fdeg lens=(%.4f %.4f %.4f) camera=(%.4f %.4f %.4f) muzzle=(%.4f %.4f %.4f) shotF=(%.6f %.6f %.6f); physical lens and ballistics unchanged",
                    static_cast<unsigned long long>(context.controller_generation),
                    std::atan2(projection(old_axis.left), projection(old_axis.forward)) * kRadiansToDegrees,
                    std::atan2(projection(old_axis.up), projection(old_axis.forward)) * kRadiansToDegrees,
                    scope_snapshot.lens_origin_world.x, scope_snapshot.lens_origin_world.y,
                    scope_snapshot.lens_origin_world.z,
                    scope_snapshot.camera_origin_world.x, scope_snapshot.camera_origin_world.y,
                    scope_snapshot.camera_origin_world.z,
                    muzzle_world.x, muzzle_world.y, muzzle_world.z,
                    shot.x, shot.y, shot.z);
            } else {
                WAWVR_STEREO_DIAG_ONCE(
                    "ScopeDiag Mosin optical alignment unavailable; retaining the physical lens camera for this frame");
            }
        }
        publish_physical_scope_snapshot(scope_snapshot);
        if (!context.right_gripping && context.left_gripping &&
            context.support_pose_latched) {
            WAWVR_STEREO_DIAG_ONCE(
                "ScopeDiag retained the three-view physical optic across the left-owner bolt handoff");
        }
        WAWVR_STEREO_DIAG_ONCE(
            "ScopeDiag published COD4-style physical optic for %s (weapon=%d anchor=%s fov=%.1f radius=%.3fm)",
            scope_identity.name.data(), context.manual_reload.weapon_index,
            selected_scope_tag_name != nullptr
                ? selected_scope_tag_name
                : "grip-fallback",
            scope_snapshot.zoom_fov_degrees,
            scope_snapshot.lens_radius_meters);
    } else {
        invalidate_physical_scope_snapshot();
    }

    if (context.grip_mode == WeaponGripMode::RightHand &&
        context.preserve_right_handoff_root) {
        WAWVR_STEREO_DIAG_ONCE(
            "WeaponDiag rejoined right owner preserved the exact handoff root after T4 pose evaluation");
    } else if (context.grip_mode == WeaponGripMode::RightHand) {
        WAWVR_STEREO_DIAG_ONCE(
            "WeaponDiag aligned %s to tracked right grip (correction %.2f IW units)",
            selected_tag_name, correction_length);
    } else if (context.grip_mode == WeaponGripMode::LeftHand) {
        WAWVR_STEREO_DIAG_ONCE(
            "WeaponDiag left owner reapplied the exact calibrated rifle root after T4 pose evaluation");
    } else if (context.grip_mode == WeaponGripMode::TwoHand) {
        WAWVR_STEREO_DIAG_ONCE(
            "WeaponDiag COD4-style two-hand pair anchored the visible rear grip after T4 support-pose evaluation");
    }
    held_breakdown_checkpoint(4);
    update_manual_reload_viewmodel(
        viewmodel_dobj,
        reinterpret_cast<const void*>(g_viewmodel_pose_address),
        context.manual_reload);
    held_breakdown_checkpoint(5);
    publish_held_breakdown();
}

extern "C" void __cdecl wawvr_add_player_weapon_bridge(
    const std::int32_t local_client_number,
    GfxScaledPlacement* const placement,
    const void* const player_state,
    void* const centity,
    const std::int32_t draw_gun) noexcept {
    ScopedPerformanceTiming weapon_bridge_timing(
        weapon_bridge_timing_phase(g_committed_weapon_grip.mode));
    const ActiveWeaponPoseContext previous_context = g_active_weapon_pose;
    const ActiveHandsContext previous_hands = g_active_hands;
    g_active_weapon_pose = {};
    g_active_hands = {};
    manual_grenade_observe_player_state(player_state);
    std::array<std::byte, kSpPlayerStateViewmodelPrefixSpan>
        player_state_viewmodel_shadow{};
    const void* rendered_player_state = player_state;
    std::int32_t effective_draw_gun = draw_gun;
    const WeaponExecutableLayout* const active_layout_for_render =
        g_weapon_layout;
    if (active_layout_for_render != nullptr &&
        active_layout_for_render->family ==
            T4LayoutFamily::single_player_1_7_1263 &&
        manual_grenade_view_override_active() &&
        accessible_range(
            player_state, player_state_viewmodel_shadow.size(), false)) {
        std::memcpy(
            player_state_viewmodel_shadow.data(), player_state,
            player_state_viewmodel_shadow.size());
        std::uint32_t weapon_flags = 0;
        std::memcpy(
            &weapon_flags,
            player_state_viewmodel_shadow.data() +
                kSpPlayerStateWeapFlagsOffset,
            sizeof(weapon_flags));
        weapon_flags &= ~kSpPlayerStateOffhandViewmodelFlag;
        std::memcpy(
            player_state_viewmodel_shadow.data() +
                kSpPlayerStateWeapFlagsOffset,
            &weapon_flags, sizeof(weapon_flags));
        rendered_player_state = player_state_viewmodel_shadow.data();
    }
    const bool hook_enabled =
        g_weapon_hook_enabled.load(std::memory_order_acquire);
    if (hook_enabled) {
        // A weapon switch, hidden model, or failed tag evaluation must not
        // leave the previous model's muzzle eligible for another shot.
        invalidate_published_muzzle();
    }
    if (hook_enabled &&
        accessible_range(placement, sizeof(*placement), true) &&
        accessible_range(
            reinterpret_cast<const void*>(g_camera_origin_address),
            sizeof(wawvr::xr::Vec3f), false) &&
        accessible_range(
            reinterpret_cast<const void*>(g_camera_axis_address),
            sizeof(wawvr::xr::Basis3f), false) &&
        accessible_range(
            reinterpret_cast<const void*>(g_cgame_gun_pitch_address),
            sizeof(float), true) &&
        accessible_range(
            reinterpret_cast<const void*>(g_cgame_gun_yaw_address),
            sizeof(float), true)) {
        ControllerFrameSnapshot snapshot{};
        if (read_controller_frame(&snapshot)) {
            if (!g_weapon_attachment_anchor_valid) {
                g_weapon_attachment_anchor = snapshot.tracking_anchor;
                g_weapon_attachment_anchor_valid = true;
            } else if (!same_pose(
                           snapshot.tracking_anchor,
                           g_weapon_attachment_anchor)) {
                // Controller attachments are expressed in the owning
                // controller's local grip/aim frame, so a tracking-space
                // rebase does not invalidate them. Clearing them here made a
                // left-only rifle correct for one or two frames and then
                // rebuilt an identity attachment from T4's stock pose,
                // producing the visible up/left jump and roll. Only the
                // retained world-space root becomes stale across a rebase.
                g_retained_weapon_pose = {};
                g_weapon_attachment_anchor = snapshot.tracking_anchor;
                WAWVR_STEREO_DIAG_ONCE(
                    "WeaponDiag tracking-anchor rebase preserved controller-local weapon attachments");
            }
            wawvr::xr::Vec3f camera_origin{};
            wawvr::xr::Basis3f camera_axis{};
            std::memcpy(
                &camera_origin,
                reinterpret_cast<const void*>(g_camera_origin_address),
                sizeof(camera_origin));
            std::memcpy(
                &camera_axis,
                reinterpret_cast<const void*>(g_camera_axis_address),
                sizeof(camera_axis));

            wawvr::xr::Basis3f body_axis{};
            const bool body_axis_valid =
                gravity_level_t4_camera_axis(camera_axis, &body_axis);
            if (body_axis_valid) {
                camera_axis = body_axis;
            }
            std::int32_t current_sp_weapon_index = 0;
            const WeaponExecutableLayout* const active_layout =
                g_weapon_layout;
            const bool current_sp_weapon_valid = draw_gun != 0 &&
                active_layout != nullptr &&
                active_layout->family ==
                    T4LayoutFamily::single_player_1_7_1263 &&
                read_sp_player_weapon_index(
                    rendered_player_state, &current_sp_weapon_index);
            std::int32_t current_sp_command_time = -1;
            if (current_sp_weapon_valid) {
                // read_sp_player_weapon_index just validated the containing SP
                // playerState span; commandTime is its first int32.
                std::memcpy(
                    &current_sp_command_time, rendered_player_state,
                    sizeof(current_sp_command_time));
            }
            const bool current_sp_command_time_valid =
                current_sp_weapon_valid && current_sp_command_time >= 0;
            RuntimeWeaponDefinitionIdentity current_weapon_runtime_identity{};
            const bool current_weapon_runtime_identity_valid =
                current_sp_weapon_valid &&
                read_runtime_sp_weapon_definition_snapshot(
                    current_sp_weapon_index,
                    &current_weapon_runtime_identity);
            observe_headset_svt40_weapon_identity(
                current_weapon_runtime_identity.name,
                current_weapon_runtime_identity_valid);
            const bool controller_owned_two_hand_sight_axis =
                current_weapon_runtime_identity_valid &&
                controller_owned_two_hand_sight_axis_for_weapon_name(
                    std::string_view{
                        current_weapon_runtime_identity.name.data()});
            const bool close_palm_pistol_support =
                current_weapon_runtime_identity_valid &&
                pistol_support_pose_for_weapon_name_buffer({
                    current_weapon_runtime_identity.name.data(),
                    current_weapon_runtime_identity.name.size()});
            WeaponGripUpdate grip_update{};
            const std::uint64_t prior_weapon_identity =
                g_weapon_grip_state.weapon_identity;
            const std::uint64_t now_milliseconds = GetTickCount64();
            const std::uint64_t current_weapon_identity =
                current_sp_weapon_valid
                ? static_cast<std::uint64_t>(current_sp_weapon_index)
                : draw_gun != 0
                    ? prior_weapon_identity != 0
                        ? prior_weapon_identity
                        : g_committed_weapon_grip.weapon_identity != 0
                            ? g_committed_weapon_grip.weapon_identity
                            : (std::numeric_limits<std::uint64_t>::max)()
                    : 0;
            if (body_axis_valid &&
                controller_frame_is_current(snapshot, now_milliseconds)) {
                const auto& actions = snapshot.frame.actions;
                const auto& right = actions.hands[
                    static_cast<std::uint32_t>(wawvr::xr::Hand::Right)];
                const auto& left = actions.hands[
                    static_cast<std::uint32_t>(wawvr::xr::Hand::Left)];
                // Both grip reservations belong to this one synchronous
                // decision. Share its fresh identity read, not a receipt
                // retained across the later native pose/render callbacks.
                const auto reload_policy = read_manual_reload_gameplay_policy(
                    current_sp_weapon_valid ? current_sp_weapon_index : 0);
                grip_update = update_weapon_grip(
                    {
                        .enabled = true,
                        .focused = actions.focused,
                        .weapon_identity = current_weapon_identity,
                        .now_milliseconds = now_milliseconds,
                        .right_squeeze_active = right.squeeze.active,
                        .right_squeeze = right.squeeze.current,
                        .right_pose_usable =
                            hand_weapon_pose_usable(right),
                        .left_squeeze_active = left.squeeze.active,
                        .left_squeeze = left.squeeze.current,
                        .left_pose_usable =
                            hand_weapon_pose_usable(left),
                        .right_interaction_reserved =
                            reload_policy.reserves_right_grip,
                        .left_interaction_reserved =
                            reload_policy.reserves_left_grip ||
                            manual_grenade_reserves_left_hand(),
                    },
                    &g_weapon_grip_state);
                g_active_hands = {
                    true, snapshot, camera_origin, camera_axis};
            } else {
                // A stale frame or temporarily unusable camera basis carries
                // no readable release signal. Keep an existing owner only for
                // the bounded tracking grace; update_weapon_grip requires a
                // physical release before a timed-out hand can reacquire.
                grip_update = update_weapon_grip(
                    {
                        .enabled = true,
                        .focused = snapshot.frame.actions.focused,
                        .weapon_identity = current_weapon_identity,
                        .now_milliseconds = now_milliseconds,
                    },
                    &g_weapon_grip_state);
                if (grip_update.mode == WeaponGripMode::Chest) {
                    reset_right_ray_two_hand_steering(
                        &g_right_ray_two_hand_steering);
                }
            }

            const bool weapon_changed =
                prior_weapon_identity != current_weapon_identity;
            if (weapon_changed ||
                g_committed_weapon_grip.weapon_identity !=
                    current_weapon_identity) {
                g_committed_weapon_grip = {
                    WeaponGripMode::Chest, current_weapon_identity};
                g_published_support_pose.store(
                    false, std::memory_order_release);
            }
            const WeaponGripMode outgoing_mode =
                g_committed_weapon_grip.mode;
            const bool outgoing_support_pose =
                g_published_support_pose.load(std::memory_order_acquire);
            const bool pose_mode_changed =
                grip_update.mode != outgoing_mode;
            if (weapon_changed) {
                g_weapon_attachments = {};
                g_two_hand_attachment = {};
                g_retained_weapon_pose = {};
                reset_right_ray_two_hand_steering(
                    &g_right_ray_two_hand_steering);
                g_held_pose_failure_since_milliseconds = 0;
                g_head_relative_weapon_freeze = {};
            }

            if (head_relative_weapon_freeze_enabled() &&
                (grip_update.mode == WeaponGripMode::Chest ||
                 current_weapon_identity == 0 ||
                 (g_head_relative_weapon_freeze.freeze.valid &&
                  (g_head_relative_weapon_freeze.weapon_identity !=
                       current_weapon_identity ||
                   g_head_relative_weapon_freeze.grip_mode !=
                       grip_update.mode)))) {
                g_head_relative_weapon_freeze = {};
            }

            // Start from the canonical camera axis rather than the stock
            // sway/bob orientation, making the final barrel an absolute
            // function of the active controller attachment.
            wawvr::xr::Vec3f weapon_origin = placement->origin;
            wawvr::xr::Basis3f weapon_axis = camera_axis;

            // Keep the last trustworthy heading current even while held.
            // Releasing while looking straight down then retains the latest
            // torso yaw, not the yaw from an earlier holster operation.
            if (body_axis_valid && snapshot.frame.views_valid &&
                controller_frame_is_current(snapshot, GetTickCount64())) {
                wawvr::xr::EnginePose unused_chest_pose{};
                static_cast<void>(build_chest_weapon_pose(
                    {camera_origin, camera_axis}, snapshot.frame.head_center,
                    snapshot.tracking_anchor, &unused_chest_pose,
                    &g_chest_weapon_pose_state));
            }

            if (body_axis_valid &&
                grip_update.mode == WeaponGripMode::Chest &&
                apply_chest_weapon_placement(
                    snapshot, camera_origin, camera_axis,
                    &weapon_origin, &weapon_axis)) {
                // A released weapon starts its next pickup directly at the
                // then-current controller pose instead of easing from the
                // last pose held before it returned to the chest.
                wawvr::xr::Quaternionf weapon_quaternion{};
                if (iw_axis_to_unit_quaternion(
                        weapon_axis, &weapon_quaternion)) {
                    placement->origin = weapon_origin;
                    placement->quaternion = weapon_quaternion;
                    g_weapon_attachments = {};
                    g_retained_weapon_pose = {};
                    g_two_hand_attachment = {};
                    reset_right_ray_two_hand_steering(
                        &g_right_ray_two_hand_steering);
                    g_committed_weapon_grip = {
                        WeaponGripMode::Chest, current_weapon_identity};
                    g_published_support_pose.store(
                        false, std::memory_order_release);
                    g_held_pose_failure_since_milliseconds = 0;
                    WAWVR_STEREO_DIAG_ONCE(
                        "WeaponDiag both grips released; weapon anchored to HMD-yaw upright chest holster");
                }
            } else if (body_axis_valid &&
                (grip_update.mode == WeaponGripMode::RightHand ||
                 grip_update.mode == WeaponGripMode::LeftHand ||
                 grip_update.mode == WeaponGripMode::TwoHand)) {
                wawvr::xr::EnginePose current_head_world{};
                const bool current_head_local_controller_path =
                    current_head_local_weapon_enabled() &&
                    compose_snapshot_head_world_pose(
                        snapshot, camera_origin, camera_axis,
                        &current_head_world);
                const wawvr::xr::Vec3f controller_camera_origin =
                    current_head_local_controller_path
                    ? current_head_world.position : camera_origin;
                const wawvr::xr::Basis3f controller_camera_axis =
                    current_head_local_controller_path
                    ? current_head_world.axis : camera_axis;
                const wawvr::xr::Quaternionf
                    two_hand_reference_orientation =
                        snapshot.tracking_anchor.orientation;
                wawvr::xr::EnginePose current_head_from_tracking_anchor{};
                if (current_head_local_controller_path) {
                    current_head_from_tracking_anchor =
                        wawvr::xr::OpenXrPoseToIwRelative(
                            snapshot.frame.head_center,
                            snapshot.tracking_anchor,
                            wawvr::xr::kIwUnitsPerMeter);
                }
                if (current_head_local_controller_path) {
                    WAWVR_STEREO_DIAG_ONCE(
                        "WeaponDiag adaptive quiet-aim stabilization active: tracking-space right-hand filtering precedes current-head removal; two-hand support is a relative right-ray constraint");
                } else if (current_head_local_weapon_enabled()) {
                    WAWVR_STEREO_DIAG_ONCE(
                        "WeaponDiag current-head-local controller A/B rejected incoherent head-world composition; preserving established placement path");
                }
                const std::size_t right_index = static_cast<std::size_t>(
                    wawvr::xr::Hand::Right);
                ControllerWeaponPose right_controller{};
                ControllerWeaponPose left_controller{};
                bool right_pose_ready =
                    current_head_local_controller_path
                    ? published_cod4_current_head_local_controller_weapon_pose(
                          snapshot, wawvr::xr::Hand::Right,
                          now_milliseconds, &right_controller)
                    : published_controller_weapon_pose(
                          snapshot, wawvr::xr::Hand::Right,
                          now_milliseconds, &right_controller);
                bool left_pose_ready =
                    current_head_local_controller_path
                    ? published_cod4_current_head_local_controller_weapon_pose(
                          snapshot, wawvr::xr::Hand::Left,
                          now_milliseconds, &left_controller)
                    : published_controller_weapon_pose(
                          snapshot, wawvr::xr::Hand::Left,
                          now_milliseconds, &left_controller);

                // One-hand placement remains in its established render frame.
                // The two-hand state machines instead consume a separate pair
                // in stable tracking-anchor space so HMD motion cannot enter
                // their persistent history as false rifle motion.
                ControllerWeaponPose stable_right_controller =
                    right_controller;
                bool stable_right_pose_ready = right_pose_ready;
                if (current_head_local_controller_path) {
                    stable_right_pose_ready =
                        published_controller_weapon_pose(
                            snapshot, wawvr::xr::Hand::Right,
                            now_milliseconds, &stable_right_controller);
                }
                ControllerWeaponPose raw_right_controller{};
                const bool raw_right_pose_ready =
                    controller_weapon_pose(
                        snapshot, wawvr::xr::Hand::Right,
                        now_milliseconds, &raw_right_controller);
                ControllerWeaponPose raw_left_controller{};
                const bool raw_left_pose_ready =
                    controller_weapon_pose(
                        snapshot, wawvr::xr::Hand::Left,
                        now_milliseconds, &raw_left_controller);

                // Preserve the stable right-controller ray as the rigid weapon
                // baseline. The raw same-frame hand delta cancels common
                // translation; the left hand can steer only after moving
                // relative to its latched controller-local support direction.
                auto staged_right_ray_two_hand_steering =
                    g_right_ray_two_hand_steering;
                const auto update_two_hand_pose = close_palm_pistol_support
                    ? &update_pistol_two_hand_support_pose
                    : &update_right_ray_two_hand_steering_pose;
                bool staged_right_ray_two_hand_steering_ready = false;
                ControllerWeaponPose desired_two_hand_controller{};
                bool desired_two_hand_pose_ready = false;
                if (grip_update.mode == WeaponGripMode::TwoHand &&
                    stable_right_pose_ready && left_pose_ready &&
                    raw_right_pose_ready && raw_left_pose_ready) {
                    ControllerWeaponPose constrained_two_hand_controller{};
                    desired_two_hand_pose_ready =
                        update_two_hand_pose(
                            snapshot.generation,
                            snapshot.publication_milliseconds,
                            two_hand_reference_orientation,
                            stable_right_controller, raw_right_controller,
                            raw_left_controller,
                            &staged_right_ray_two_hand_steering,
                            &constrained_two_hand_controller);
                    if (desired_two_hand_pose_ready &&
                        current_head_local_controller_path) {
                        desired_two_hand_pose_ready =
                            rebase_controller_weapon_pose_to_reference(
                                current_head_from_tracking_anchor,
                                constrained_two_hand_controller,
                                &desired_two_hand_controller);
                    } else if (desired_two_hand_pose_ready) {
                        desired_two_hand_controller =
                            constrained_two_hand_controller;
                    }
                    staged_right_ray_two_hand_steering_ready =
                        desired_two_hand_pose_ready;
                    if (desired_two_hand_pose_ready) {
                        if (close_palm_pistol_support) {
                            WAWVR_STEREO_DIAG_ONCE(
                                "WeaponDiag pistol close-palm support active: stable right ray retained without a rifle separation gate or support-direction steering");
                        } else {
                            WAWVR_STEREO_DIAG_ONCE(
                                "WeaponDiag right-ray two-hand constraint active: support grip is latched and deadzoned before relative steering");
                        }
                    }
                }

                // An ownership transition may fail in the post-T4 validation
                // pass. Reconstruct the outgoing pair from a copy of the
                // committed constraint without consuming hidden history.
                ControllerWeaponPose outgoing_two_hand_controller{};
                bool outgoing_two_hand_pose_ready = false;
                if (outgoing_mode == WeaponGripMode::TwoHand &&
                    stable_right_pose_ready && left_pose_ready &&
                    raw_right_pose_ready && raw_left_pose_ready) {
                    auto outgoing_constraint =
                        g_right_ray_two_hand_steering;
                    ControllerWeaponPose constrained_outgoing_controller{};
                    outgoing_two_hand_pose_ready =
                        update_two_hand_pose(
                            snapshot.generation,
                            snapshot.publication_milliseconds,
                            two_hand_reference_orientation,
                            stable_right_controller, raw_right_controller,
                            raw_left_controller, &outgoing_constraint,
                            &constrained_outgoing_controller);
                    if (outgoing_two_hand_pose_ready &&
                        current_head_local_controller_path) {
                        outgoing_two_hand_pose_ready =
                            rebase_controller_weapon_pose_to_reference(
                                current_head_from_tracking_anchor,
                                constrained_outgoing_controller,
                                &outgoing_two_hand_controller);
                    } else if (outgoing_two_hand_pose_ready) {
                        outgoing_two_hand_controller =
                            constrained_outgoing_controller;
                    }
                }

                const auto pose_for_mode =
                    [&](const WeaponGripMode mode,
                        ControllerWeaponPose* const pose) noexcept -> bool {
                        if (pose == nullptr) {
                            return false;
                        }
                        switch (mode) {
                        case WeaponGripMode::RightHand:
                            if (right_pose_ready) {
                                *pose = right_controller;
                                return true;
                            }
                            return false;
                        case WeaponGripMode::LeftHand:
                            if (left_pose_ready) {
                                *pose = left_controller;
                                return true;
                            }
                            return false;
                        case WeaponGripMode::TwoHand:
                            if (grip_update.mode ==
                                    WeaponGripMode::TwoHand &&
                                desired_two_hand_pose_ready) {
                                *pose = desired_two_hand_controller;
                                return true;
                            }
                            if (outgoing_mode ==
                                    WeaponGripMode::TwoHand &&
                                outgoing_two_hand_pose_ready) {
                                *pose = outgoing_two_hand_controller;
                                return true;
                            }
                            return false;
                        case WeaponGripMode::Chest:
                        default:
                            return false;
                        }
                    };
                const bool retained_pose_ready =
                    retained_weapon_pose_matches_committed(
                        current_weapon_identity);

                const auto publish_active_pose =
                    [&](const WeaponGripMode applied_mode,
                        const ControllerWeaponPose& applied_controller,
                        const WeaponAttachmentState* const
                            published_attachment,
                        const bool applied_controller_ready,
                        const bool applied_right_gripping,
                        const bool applied_left_gripping,
                        const bool applied_support_pose,
                        const wawvr::xr::Vec3f& applied_origin,
                        const wawvr::xr::Basis3f& applied_axis,
                         const bool transition_pending,
                         const bool align_grip_tag_before_commit,
                         const bool recapture_attachment_after_post,
                         const bool refresh_retained_pose) noexcept
                        -> bool {
                        wawvr::xr::Quaternionf weapon_quaternion{};
                        float pitch = 0.0F;
                        float yaw = 0.0F;
                        if (!iw_axis_to_unit_quaternion(
                                applied_axis, &weapon_quaternion) ||
                            !aim_degrees_from_forward(
                                applied_axis.forward, &pitch, &yaw)) {
                            return false;
                        }

                        wawvr::xr::Vec3f tracked_grip_world{};
                        bool grip_world_ready = applied_controller_ready &&
                            controller_grip_world(
                                controller_camera_origin,
                                controller_camera_axis,
                                applied_controller, &tracked_grip_world);
                        if (!grip_world_ready && retained_pose_ready &&
                            g_retained_weapon_pose.grip_tag_valid) {
                            tracked_grip_world =
                                g_retained_weapon_pose.grip_tag_world;
                            grip_world_ready = true;
                        }
                        if (!grip_world_ready ||
                            rendered_player_state == nullptr ||
                            draw_gun == 0) {
                            return false;
                        }

                        placement->origin = applied_origin;
                        placement->quaternion = weapon_quaternion;
                        std::memcpy(
                            reinterpret_cast<void*>(
                                g_cgame_gun_pitch_address),
                            &pitch, sizeof(pitch));
                        std::memcpy(
                            reinterpret_cast<void*>(
                                g_cgame_gun_yaw_address),
                            &yaw, sizeof(yaw));
                        publish_final_visible_aim(
                            snapshot.generation, snapshot.frame.frame_id,
                            snapshot.frame.actions.sequence,
                            applied_controller_ready && refresh_retained_pose,
                            pitch, yaw, applied_axis);

                        ManualReloadViewmodelContext manual_reload{};
                        if (current_sp_weapon_valid) {
                            manual_reload = {
                                .controller = snapshot,
                                .weapon_index = current_sp_weapon_index,
                                .weapon_registered_count =
                                    current_weapon_runtime_identity
                                        .registered_count,
                                .weapon_definition_address =
                                    current_weapon_runtime_identity
                                        .definition_address,
                                .weapon_internal_name =
                                    current_weapon_runtime_identity.name,
                                .weapon_definition_valid =
                                    current_weapon_runtime_identity_valid,
                                .camera_origin = camera_origin,
                                .camera_axis = camera_axis,
                                .weapon_axis = applied_axis,
                                .rifle_grip_world = tracked_grip_world,
                                .muzzle_world = {},
                                .muzzle_valid = false,
                                .right_rifle_gripped =
                                    applied_right_gripping,
                                .left_rifle_gripped =
                                    applied_left_gripping,
                                .player_state = rendered_player_state,
                                .player_command_time =
                                    current_sp_command_time,
                                .player_command_time_valid =
                                    current_sp_command_time_valid,
                            };
                        }
                        g_active_weapon_pose = {
                            .valid = true,
                            .grip_mode = applied_mode,
                            .weapon_identity = current_weapon_identity,
                            .controller_generation = snapshot.generation,
                            .controller_publication_milliseconds =
                                snapshot.publication_milliseconds,
                            .tracked_grip_world = tracked_grip_world,
                            .weapon_origin = applied_origin,
                            .weapon_axis = applied_axis,
                            .controller_pose = applied_controller,
                            .weapon_attachment_axis_valid =
                                published_attachment != nullptr &&
                                published_attachment->valid,
                            .weapon_attachment_axis =
                                published_attachment != nullptr &&
                                    published_attachment->valid
                                ? published_attachment->axis
                                : wawvr::xr::Basis3f{},
                            .camera_origin = camera_origin,
                            .camera_axis = camera_axis,
                            .controller_camera_origin =
                                controller_camera_origin,
                            .controller_camera_axis = controller_camera_axis,
                            .scene_base_lock_eligible =
                                (controller_owned_two_hand_sight_axis &&
                                 applied_mode == WeaponGripMode::TwoHand &&
                                 outgoing_mode == WeaponGripMode::TwoHand &&
                                 grip_update.mode == WeaponGripMode::TwoHand &&
                                 !transition_pending &&
                                 applied_controller_ready &&
                                 refresh_retained_pose &&
                                 applied_right_gripping &&
                                 applied_left_gripping),
                            .right_gripping = applied_right_gripping,
                            .left_gripping = applied_left_gripping,
                            .support_pose_latched =
                                applied_support_pose,
                            .preserve_right_handoff_root =
                                grip_update.preserve_right_handoff_root,
                            .transition_pending = transition_pending,
                            .align_grip_tag_before_commit =
                                align_grip_tag_before_commit,
                            .recapture_attachment_after_post =
                                 recapture_attachment_after_post,
                            .commit_right_ray_two_hand_steering_after_post =
                                applied_controller_ready &&
                                refresh_retained_pose &&
                                staged_right_ray_two_hand_steering_ready &&
                                applied_mode == WeaponGripMode::TwoHand &&
                                grip_update.mode == WeaponGripMode::TwoHand,
                            .reset_right_ray_two_hand_steering_after_post =
                                applied_controller_ready &&
                                applied_mode != WeaponGripMode::TwoHand &&
                                grip_update.mode != WeaponGripMode::TwoHand &&
                                g_right_ray_two_hand_steering.valid,
                            .staged_right_ray_two_hand_steering =
                                staged_right_ray_two_hand_steering,
                             .refresh_retained_pose = refresh_retained_pose,
                             .manual_reload = manual_reload,
                        };
                        return true;
                    };

                WeaponGripMode applied_mode = outgoing_mode;
                ControllerWeaponPose applied_controller{};
                bool applied_controller_ready = false;
                WeaponAttachmentState staged_attachment{};
                WeaponAttachmentState* applied_attachment = nullptr;
                WeaponAttachmentState frame_attachment{};
                WeaponAttachmentState* placement_attachment = nullptr;
                bool transition_pending = false;
                bool align_grip_tag_before_commit = false;
                // Key this retryable action to the committed outgoing mode,
                // not only the logical latch edge. If post-T4 validation fails
                // once, the logical state is already RightHand on the retry
                // frame while the committed visible owner is still TwoHand.
                const bool restoring_saved_right_attachment =
                    outgoing_mode == WeaponGripMode::TwoHand &&
                    grip_update.mode == WeaponGripMode::RightHand;
                const bool restoring_saved_two_hand_attachment =
                    outgoing_mode == WeaponGripMode::LeftHand &&
                    grip_update.mode == WeaponGripMode::TwoHand;

                if (pose_mode_changed &&
                    pose_for_mode(
                        grip_update.mode, &applied_controller)) {
                    applied_controller_ready = true;
                    if (outgoing_mode == WeaponGripMode::Chest) {
                        // A body holster is a display pose, not a controller
                        // calibration. The post-T4 tag pass aligns every fresh
                        // pickup, including direct left and simultaneous pair
                        // pickups, before committing this staged attachment.
                        applied_mode = grip_update.mode;
                        applied_attachment = &staged_attachment;
                        transition_pending =
                            prepare_controller_forward_weapon_pickup(
                                controller_camera_axis, applied_attachment,
                                &weapon_axis);
                        align_grip_tag_before_commit = transition_pending;
                    } else if (restoring_saved_right_attachment) {
                        // The right attachment remains untouched while the
                        // left support hand steers the pair. Restore a staged
                        // copy on release so right aim resumes immediately;
                        // transferring the pair pose here would permanently
                        // bake its last angular offset into the right owner.
                        applied_mode = WeaponGripMode::RightHand;
                        staged_attachment =
                            g_weapon_attachments[right_index];
                        transition_pending = staged_attachment.valid;
                        if (!transition_pending) {
                            transition_pending =
                                prepare_controller_forward_weapon_pickup(
                                    controller_camera_axis,
                                    &staged_attachment,
                                    &weapon_axis);
                        }
                        if (transition_pending) {
                            applied_attachment = &staged_attachment;
                            align_grip_tag_before_commit = true;
                        }
                    } else {
                        ControllerWeaponPose outgoing_controller{};
                        WeaponAttachmentState* const outgoing_attachment =
                            weapon_attachment_for_mode(outgoing_mode);
                        // The calibrated pair attachment remains untouched
                        // while the right hand performs a bolt/reload action.
                        // Restore that normal pair balance on regrip instead
                        // of baking the temporary left-only rifle angle into a
                        // replacement pair attachment. This decision is keyed
                        // to the committed outgoing mode, so post-T4 failure
                        // retries the same atomic staged restore next frame.
                        bool transition_ready = false;
                        if (restoring_saved_two_hand_attachment &&
                            g_two_hand_attachment.valid) {
                            staged_attachment = g_two_hand_attachment;
                            transition_ready = true;
                        } else {
                            transition_ready =
                                outgoing_attachment != nullptr &&
                                outgoing_attachment->valid &&
                                pose_for_mode(
                                    outgoing_mode,
                                    &outgoing_controller) &&
                                transfer_controller_weapon_attachment(
                                    controller_camera_origin,
                                    controller_camera_axis,
                                    outgoing_controller,
                                    *outgoing_attachment,
                                    applied_controller, &staged_attachment,
                                    &weapon_origin, &weapon_axis);
                        }
                        if (!transition_ready && retained_pose_ready) {
                            transition_ready =
                                calibrate_controller_weapon_attachment(
                                    controller_camera_origin,
                                    controller_camera_axis,
                                    applied_controller,
                                    g_retained_weapon_pose.origin,
                                    g_retained_weapon_pose.axis,
                                    &staged_attachment);
                            if (transition_ready) {
                                weapon_origin =
                                    g_retained_weapon_pose.origin;
                                weapon_axis = g_retained_weapon_pose.axis;
                            }
                        }
                        if (transition_ready) {
                            applied_mode = grip_update.mode;
                            applied_attachment = &staged_attachment;
                            transition_pending = true;
                        }
                    }
                }

                // A rejected logical transition keeps rendering the committed
                // outgoing controller frame. The desired latch remains live,
                // so the transition is retried on the next XR generation.
                if (!transition_pending &&
                    outgoing_mode != WeaponGripMode::Chest) {
                    applied_mode = outgoing_mode;
                    applied_controller_ready = pose_for_mode(
                        outgoing_mode, &applied_controller);
                    applied_attachment =
                        weapon_attachment_for_mode(outgoing_mode);
                    if (applied_attachment == nullptr ||
                        !applied_attachment->valid) {
                        applied_attachment = nullptr;
                    }
                }

                // Placement always consumes a frame-local copy. The committed
                // controller attachment stays immutable between validated
                // ownership transitions, including steady two-hand M1 use.
                if (applied_attachment != nullptr) {
                    frame_attachment = *applied_attachment;
                    placement_attachment = &frame_attachment;
                }

                // Exact policy-selected weapons use the constrained controller
                // pose as their two-hand sight axis. Ownership transfer preserves the
                // visible root position, but it can also preserve a fixed
                // angular offset from the outgoing one-hand attachment. Keep
                // that position and discard only the transferred basis in the
                // frame-local copy. No committed attachment, one-hand pose, or
                // unrelated weapon is changed by this alignment policy.
                if (placement_attachment != nullptr &&
                    applied_mode == WeaponGripMode::TwoHand &&
                    controller_owned_two_hand_sight_axis &&
                    !align_controller_weapon_attachment_to_controller_forward(
                        controller_camera_origin, controller_camera_axis,
                        applied_controller,
                        weapon_origin, placement_attachment)) {
                    placement_attachment = nullptr;
                }

                bool active_pose_published = false;
                const bool applied_right_gripping =
                    applied_mode == grip_update.mode
                    ? grip_update.right_latched
                    : applied_mode == WeaponGripMode::RightHand ||
                          applied_mode == WeaponGripMode::TwoHand;
                const bool applied_left_gripping =
                    applied_mode == grip_update.mode
                    ? grip_update.left_latched
                    : applied_mode == WeaponGripMode::LeftHand ||
                          applied_mode == WeaponGripMode::TwoHand;
                const bool applied_support_pose =
                    applied_mode == grip_update.mode
                    ? grip_update.support_pose
                    : outgoing_support_pose;
                if (applied_controller_ready &&
                    placement_attachment != nullptr &&
                    apply_controller_weapon_placement(
                        controller_camera_origin, controller_camera_axis,
                        applied_controller,
                        placement_attachment, &weapon_origin, &weapon_axis)) {
                    if (!current_head_local_weapon_enabled() &&
                        head_relative_weapon_freeze_enabled() &&
                        !transition_pending &&
                        g_head_relative_weapon_freeze.freeze.valid &&
                        g_head_relative_weapon_freeze.weapon_identity ==
                            current_weapon_identity &&
                        g_head_relative_weapon_freeze.grip_mode ==
                            applied_mode) {
                        wawvr::xr::EnginePose head_world{};
                        wawvr::xr::EnginePose frozen_world{};
                        if (compose_snapshot_head_world_pose(
                                snapshot, camera_origin, camera_axis,
                                &head_world) &&
                            reconstruct_head_relative_pose_freeze(
                                g_head_relative_weapon_freeze.freeze,
                                head_world, &frozen_world)) {
                            weapon_origin = frozen_world.position;
                            weapon_axis = frozen_world.axis;
                            if (applied_mode == WeaponGripMode::TwoHand) {
                                WAWVR_STEREO_DIAG_ONCE(
                                    "WeaponDiag head-relative freeze A/B active for settled two-hand aim: controller motion is excluded from the visible rifle");
                            } else {
                                WAWVR_STEREO_DIAG_ONCE(
                                    "WeaponDiag head-relative freeze A/B active for one-hand aim: controller motion is excluded from the visible rifle");
                            }
                        } else {
                            g_head_relative_weapon_freeze = {};
                        }
                    }
                    active_pose_published = publish_active_pose(
                          applied_mode, applied_controller,
                          placement_attachment, true,
                          applied_right_gripping,
                          applied_left_gripping,
                          applied_support_pose,
                         weapon_origin, weapon_axis, transition_pending,
                         align_grip_tag_before_commit,
                         transition_pending, true);
                    if (active_pose_published) {
                        reset_weapon_pose_tracking_failure(
                            &g_held_pose_failure_since_milliseconds);
                    }
                    if (transition_pending && active_pose_published) {
                        if (restoring_saved_right_attachment &&
                            applied_mode == WeaponGripMode::RightHand) {
                            WAWVR_STEREO_DIAG_ONCE(
                                "WeaponDiag staged saved native right-hand aim restoration pending post-T4 validation");
                        } else if (restoring_saved_two_hand_attachment &&
                                   applied_mode ==
                                       WeaponGripMode::TwoHand) {
                            WAWVR_STEREO_DIAG_ONCE(
                                "WeaponDiag staged saved two-hand steering restoration after left-only bolt handoff pending post-T4 validation");
                        } else {
                            WAWVR_STEREO_DIAG_ONCE(
                                "WeaponDiag staged an exact current-owner transition into %s pending post-T4 validation",
                                applied_mode == WeaponGripMode::TwoHand
                                    ? "COD4-style two-hand control"
                                    : applied_mode == WeaponGripMode::LeftHand
                                        ? "left-only control"
                                        : "right-only control");
                        }
                    }
                }

                bool retained_pose_within_recovery = retained_pose_ready;
                if (!active_pose_published && retained_pose_ready) {
                    retained_pose_within_recovery =
                        retain_weapon_pose_during_tracking_failure(
                            now_milliseconds,
                            &g_held_pose_failure_since_milliseconds);
                }
                if (!active_pose_published &&
                    retained_pose_within_recovery) {
                    // Retained rendering is an exact complete-pose freeze. Do
                    // not substitute either currently tracked hand here: a
                    // rejected relocalization can leave the opposite hand
                    // usable, and feeding that hand into the post-T4 grip-tag
                    // alignment would visibly drag the frozen weapon across
                    // the view. With no live controller selected,
                    // publish_active_pose uses the retained grip tag together
                    // with the retained root and axis.
                    ControllerWeaponPose retained_controller{};
                    active_pose_published = publish_active_pose(
                        outgoing_mode, retained_controller,
                        nullptr, false,
                        outgoing_mode == WeaponGripMode::RightHand ||
                            outgoing_mode == WeaponGripMode::TwoHand,
                        outgoing_mode == WeaponGripMode::LeftHand ||
                            outgoing_mode == WeaponGripMode::TwoHand,
                        outgoing_support_pose,
                         g_retained_weapon_pose.origin,
                         g_retained_weapon_pose.axis,
                         false, false, false, false);
                    if (active_pose_published) {
                        WAWVR_STEREO_DIAG_ONCE(
                            "WeaponDiag invalid controller pair retained the complete committed weapon context");
                    }
                }

                if (!active_pose_published &&
                    outgoing_mode != WeaponGripMode::Chest) {
                    // A held controller mode must never fall through to T4's
                    // native flat-screen placement. If neither a valid pose
                    // nor the last committed complete pose can be rendered,
                    // retire the rifle to the chest and require any timed-out
                    // owner to release before a fresh pickup.
                    const bool right_was_latched =
                        g_weapon_grip_state.right_latched;
                    const bool left_was_latched =
                        g_weapon_grip_state.left_latched;
                    g_weapon_grip_state.mode = WeaponGripMode::Chest;
                    g_weapon_grip_state.right_latched = false;
                    g_weapon_grip_state.left_latched = false;
                    g_weapon_grip_state.support_pose_latched = false;
                    g_weapon_grip_state.preserve_right_handoff_root = false;
                    g_weapon_grip_state.right_rearm_required =
                        g_weapon_grip_state.right_rearm_required ||
                        right_was_latched;
                    g_weapon_grip_state.left_rearm_required =
                        g_weapon_grip_state.left_rearm_required ||
                        left_was_latched;
                    reset_controller_weapon_publication_filters();
                    reset_right_ray_two_hand_steering(
                        &g_right_ray_two_hand_steering);
                    g_weapon_attachments = {};
                    g_two_hand_attachment = {};
                    g_retained_weapon_pose = {};
                    g_committed_weapon_grip = {
                        WeaponGripMode::Chest, current_weapon_identity};
                    g_held_pose_failure_since_milliseconds = 0;
                    g_head_relative_weapon_freeze = {};
                    g_published_support_pose.store(
                        false, std::memory_order_release);
                    bool chest_written = false;
                    if (apply_chest_weapon_placement(
                            snapshot, camera_origin, camera_axis,
                            &weapon_origin, &weapon_axis)) {
                        wawvr::xr::Quaternionf chest_quaternion{};
                        if (iw_axis_to_unit_quaternion(
                                weapon_axis, &chest_quaternion)) {
                            placement->origin = weapon_origin;
                            placement->quaternion = chest_quaternion;
                            chest_written = true;
                            WAWVR_STEREO_DIAG_ONCE(
                                "WeaponDiag unusable held pose retired to chest instead of exposing native viewmodel placement");
                        }
                    }
                    if (!chest_written) {
                        effective_draw_gun = 0;
                    }
                }

                if (!active_pose_published &&
                    outgoing_mode == WeaponGripMode::Chest &&
                    apply_chest_weapon_placement(
                        snapshot, camera_origin, camera_axis,
                        &weapon_origin, &weapon_axis)) {
                    wawvr::xr::Quaternionf chest_quaternion{};
                    if (iw_axis_to_unit_quaternion(
                            weapon_axis, &chest_quaternion)) {
                        placement->origin = weapon_origin;
                        placement->quaternion = chest_quaternion;
                        g_held_pose_failure_since_milliseconds = 0;
                    }
                }

                if (active_pose_published) {
                    if (applied_mode == WeaponGripMode::TwoHand) {
                        WAWVR_STEREO_DIAG_ONCE(
                            "WeaponDiag rigid shared aim active: right ray anchors, left grip supplies deadzoned relative steering");
                    } else {
                        WAWVR_STEREO_DIAG_ONCE(
                            "WeaponDiag applied rigid single-controller viewmodel placement at CG_AddPlayerWeapon");
                    }
                    WAWVR_STEREO_DIAG_ONCE(
                        "WeaponDiag COD4-parity weapon pose stabilization filters tracking shimmer once per OpenXR generation");
                }
            }
            if ((grip_update.mode == WeaponGripMode::Chest ||
                 g_committed_weapon_grip.mode == WeaponGripMode::Chest) &&
                !g_active_weapon_pose.valid) {
                // No current HMD anchor: never expose a stock floating pose.
                // The desired mode may already be Chest while the last
                // successfully committed pose still belongs to a controller.
                reset_controller_weapon_publication_filters();
                reset_right_ray_two_hand_steering(
                    &g_right_ray_two_hand_steering);
                g_weapon_attachments = {};
                g_two_hand_attachment = {};
                g_retained_weapon_pose = {};
                g_head_relative_weapon_freeze = {};
                g_committed_weapon_grip = {
                    WeaponGripMode::Chest, current_weapon_identity};
                g_published_support_pose.store(
                    false, std::memory_order_release);
                g_held_pose_failure_since_milliseconds = 0;
                effective_draw_gun = 0;
            }
        } else {
            const std::uint64_t now_milliseconds = GetTickCount64();
            const std::uint64_t retained_identity =
                g_weapon_grip_state.weapon_identity != 0
                ? g_weapon_grip_state.weapon_identity
                : g_committed_weapon_grip.weapon_identity;
            WeaponGripUpdate dropout_update{};
            if (draw_gun != 0 && retained_identity != 0) {
                dropout_update = update_weapon_grip(
                    {
                        .enabled = true,
                        // A cleared broker has no focus or release evidence.
                        // Treat it as a recoverable publication gap, bounded
                // by the same timeout as stale/untracked hand poses.
                        .focused = true,
                        .weapon_identity = retained_identity,
                        .now_milliseconds = now_milliseconds,
                    },
                    &g_weapon_grip_state);
            } else {
                reset_weapon_grip(&g_weapon_grip_state);
            }

            const bool freeze_retained_pose =
                dropout_update.mode != WeaponGripMode::Chest &&
                retained_weapon_pose_matches_committed(retained_identity);
            bool retained_pose_written = false;
            if (freeze_retained_pose) {
                wawvr::xr::Quaternionf retained_quaternion{};
                if (iw_axis_to_unit_quaternion(
                        g_retained_weapon_pose.axis,
                        &retained_quaternion)) {
                    placement->origin = g_retained_weapon_pose.origin;
                    placement->quaternion = retained_quaternion;
                    retained_pose_written = true;
                    WAWVR_STEREO_DIAG_ONCE(
                        "WeaponDiag controller broker gap froze the last complete committed weapon pose");
                }
            }
            if (!retained_pose_written) {
                // The recovery window expired (or no held pose ever existed).
                // Retire controller ownership atomically. Without a head
                // snapshot we cannot place a real chest anchor; hide until
                // tracking recovers rather than expose a stock floating pose.
                g_chest_weapon_pose_state = {};
                reset_controller_weapon_publication_filters();
                reset_right_ray_two_hand_steering(
                    &g_right_ray_two_hand_steering);
                g_weapon_attachments = {};
                g_two_hand_attachment = {};
                g_retained_weapon_pose = {};
                g_committed_weapon_grip = {
                    WeaponGripMode::Chest, retained_identity};
                g_held_pose_failure_since_milliseconds = 0;
                g_head_relative_weapon_freeze = {};
                g_published_support_pose.store(
                    false, std::memory_order_release);

                effective_draw_gun = 0;
            }
        }
    }

    const std::uintptr_t original = g_original_add_player_weapon;
    const WeaponExecutableLayout* const layout = g_weapon_layout;
    if (original != 0 && layout != nullptr) {
        ScopedPerformanceTiming native_weapon_timing(
            PerformanceTimingPhase::weapon_native_original);
        if (layout->add_player_weapon_uses_eax_centity) {
            wawvr_call_add_player_weapon_mp_original(
                local_client_number, placement, rendered_player_state, centity,
                effective_draw_gun);
        } else {
            reinterpret_cast<CgAddPlayerWeaponFunction>(original)(
                local_client_number, placement, rendered_player_state, centity,
                effective_draw_gun);
        }
    }
    g_active_weapon_pose = previous_context;
    g_active_hands = previous_hands;
}

WeaponHookInstallResult install_weapon_viewmodel_hook(
    const wawvr::t4::ValidatedBindings& bindings) noexcept {
    WeaponHookInstallResult result{};
    if (g_weapon_hook_installed.load(std::memory_order_acquire)) {
        result.status = WeaponHookStatus::already_installed;
        return result;
    }
    if (weapon_hook_disabled_by_environment()) {
        result.status = WeaponHookStatus::disabled_by_environment;
        return result;
    }

#if !defined(_MSC_VER) || !defined(_M_IX86)
    static_cast<void>(bindings);
    result.status = WeaponHookStatus::rejected_wrong_profile;
    return result;
#else
    const auto& bound = bindings.profile();
    const WeaponExecutableLayout* const layout =
        weapon_layout_for_profile(bound);
    if (layout == nullptr) {
        result.status = WeaponHookStatus::rejected_wrong_profile;
        return result;
    }
    const std::uintptr_t add_player_weapon_bridge_address =
        layout->add_player_weapon_uses_eax_centity
        ? reinterpret_cast<std::uintptr_t>(
              &wawvr_add_player_weapon_mp_entry_bridge)
        : reinterpret_cast<std::uintptr_t>(
              &wawvr_add_player_weapon_bridge);

    const auto prepared_outer = wawvr::t4::prepare_inline_hook(
        bindings, wawvr::t4::HookSiteId::viewmodel_weapon_call,
        add_player_weapon_bridge_address);
    const auto prepared_pose = wawvr::t4::prepare_inline_hook(
        bindings, wawvr::t4::HookSiteId::viewmodel_pose_update_call,
        reinterpret_cast<std::uintptr_t>(
            &wawvr_update_viewmodel_pose_bridge));
    const auto prepared_ballistics = wawvr::t4::prepare_inline_hook(
        bindings, wawvr::t4::HookSiteId::fire_weapon_calc_muzzle_call,
        reinterpret_cast<std::uintptr_t>(
            &wawvr_calc_muzzle_points_bridge));
    const auto prepared_spread = wawvr::t4::prepare_inline_hook(
        bindings, wawvr::t4::HookSiteId::fire_weapon_bullet_fire_call,
        reinterpret_cast<std::uintptr_t>(&wawvr_bullet_fire_bridge));
    const auto prepared_client_effects = wawvr::t4::prepare_inline_hook(
        bindings,
        wawvr::t4::HookSiteId::draw_bullet_impacts_view_origin_call,
        reinterpret_cast<std::uintptr_t>(
            &wawvr_client_bullet_view_origin_bridge));
    const auto prepared_client_spread = wawvr::t4::prepare_inline_hook(
        bindings,
        wawvr::t4::HookSiteId::draw_bullet_impacts_get_spread_call,
        reinterpret_cast<std::uintptr_t>(
            &wawvr_client_bullet_spread_bridge));
    const bool install_sp_client_trace_diagnostic =
        layout == &kSpWeaponLayout && mosin_bullet_diagnostics_enabled();
    const bool install_sp_impact_diagnostic =
        layout == &kSpWeaponLayout && mosin_bullet_diagnostics_enabled();
    const bool install_sp_player_rocket_hook =
        layout == &kSpWeaponLayout;
    wawvr::t4::HookPreparationResult prepared_player_rocket{};
    if (install_sp_player_rocket_hook) {
        prepared_player_rocket = wawvr::t4::prepare_inline_hook(
            bindings,
            wawvr::t4::HookSiteId::
                weapon_rocket_launcher_fire_rocket_call,
            reinterpret_cast<std::uintptr_t>(
                &wawvr_player_rocket_bridge));
    }
    wawvr::t4::HookPreparationResult prepared_client_trace{};
    if (install_sp_client_trace_diagnostic) {
        prepared_client_trace = wawvr::t4::prepare_inline_hook(
            bindings,
            wawvr::t4::HookSiteId::
                fire_bullet_penetrate_initial_bullet_trace_result_seam,
            reinterpret_cast<std::uintptr_t>(
                &wawvr_client_bullet_trace_bridge));
    }
    wawvr::t4::HookPreparationResult prepared_impact_selector{};
    wawvr::t4::HookPreparationResult prepared_impact_spawn{};
    if (install_sp_impact_diagnostic) {
        prepared_impact_selector = wawvr::t4::prepare_inline_hook(
            bindings, wawvr::t4::HookSiteId::client_impact_selector_call,
            reinterpret_cast<std::uintptr_t>(
                &wawvr_client_impact_selector_bridge));
        prepared_impact_spawn = wawvr::t4::prepare_inline_hook(
            bindings, wawvr::t4::HookSiteId::client_impact_fx_spawn_call,
            reinterpret_cast<std::uintptr_t>(
                &wawvr_client_impact_fx_spawn_bridge));
    }
    if (!prepared_outer.ok() || !prepared_pose.ok() ||
        !prepared_ballistics.ok() || !prepared_spread.ok() ||
        !prepared_client_effects.ok() || !prepared_client_spread.ok() ||
        (install_sp_player_rocket_hook &&
         !prepared_player_rocket.ok()) ||
        (install_sp_client_trace_diagnostic &&
         !prepared_client_trace.ok()) ||
        (install_sp_impact_diagnostic &&
         (!prepared_impact_selector.ok() || !prepared_impact_spawn.ok()))) {
        result.status = WeaponHookStatus::preparation_failed;
        return result;
    }
    result.target = prepared_outer.hook->target;
    result.ballistics_target = prepared_ballistics.hook->target;
    result.spread_target = prepared_spread.hook->target;
    result.client_effects_target =
        prepared_client_effects.hook->target;
    result.client_spread_target =
        prepared_client_spread.hook->target;

    const auto original_add_player_weapon = bindings.module().address(
        layout->cg_add_player_weapon_rva,
        layout->cg_add_player_weapon_sentinel.size());
    const auto original_update_pose = bindings.module().address(
        layout->cg_update_viewmodel_pose_rva,
        layout->cg_update_viewmodel_pose_sentinel.size());
    const auto get_world_tag_pos = bindings.module().address(
        layout->cg_dobj_get_world_tag_pos_rva,
        layout->cg_dobj_get_world_tag_pos_sentinel.size());
    std::optional<std::uintptr_t> get_world_tag_matrix{};
    std::optional<std::uintptr_t> fire_rocket{};
    std::optional<std::uintptr_t> fire_rocket_context{};
    if (install_sp_player_rocket_hook) {
        get_world_tag_matrix = bindings.module().address(
            layout->cg_dobj_get_world_tag_matrix_rva,
            layout->cg_dobj_get_world_tag_matrix_sentinel.size());
        fire_rocket = bindings.site_address(
            wawvr::t4::HookSiteId::fire_rocket_entry_sentinel);
        fire_rocket_context = bindings.site_address(
            wawvr::t4::HookSiteId::
                weapon_rocket_launcher_fire_rocket_context_sentinel);
    }
    const auto calc_muzzle_points = bindings.site_address(
        wawvr::t4::HookSiteId::calc_muzzle_points_sentinel);
    const auto fire_weapon_context = bindings.site_address(
        wawvr::t4::HookSiteId::fire_weapon_calc_muzzle_context_sentinel);
    const auto bullet_fire = bindings.site_address(
        wawvr::t4::HookSiteId::bullet_fire_entry_sentinel);
    const auto fire_weapon_bullet_context = bindings.site_address(
        wawvr::t4::HookSiteId::fire_weapon_bullet_fire_context_sentinel);
    const auto bullet_spread_context = bindings.site_address(
        wawvr::t4::HookSiteId::bullet_fire_spread_argument_sentinel);
    const auto client_bullet_view_origin = bindings.site_address(
        wawvr::t4::HookSiteId::
            client_bullet_view_origin_entry_sentinel);
    const auto client_bullet_origin_context = bindings.site_address(
        wawvr::t4::HookSiteId::
            draw_bullet_impacts_view_origin_context_sentinel);
    const auto get_spread_for_weapon = bindings.site_address(
        wawvr::t4::HookSiteId::get_spread_for_weapon_entry_sentinel);
    const auto client_bullet_spread_context = bindings.site_address(
        wawvr::t4::HookSiteId::
            draw_bullet_impacts_get_spread_context_sentinel);
    std::optional<std::uintptr_t> client_bullet_trace{};
    std::optional<std::uintptr_t> client_bullet_trace_call{};
    std::optional<std::uintptr_t> client_impact_selector{};
    std::optional<std::uintptr_t> client_impact_spawn{};
    if (install_sp_client_trace_diagnostic) {
        client_bullet_trace = bindings.site_address(
            wawvr::t4::HookSiteId::client_bullet_trace_entry_sentinel);
        client_bullet_trace_call = bindings.site_address(
            wawvr::t4::HookSiteId::
                fire_bullet_penetrate_initial_bullet_trace_call_sentinel);
    }
    if (install_sp_impact_diagnostic) {
        client_impact_selector = bindings.site_address(
            wawvr::t4::HookSiteId::
                client_impact_selector_entry_sentinel);
        client_impact_spawn = bindings.site_address(
            wawvr::t4::HookSiteId::
                client_impact_fx_spawn_entry_sentinel);
    }
    const auto pose_context = bindings.module().address(
        layout->viewmodel_pose_context_rva,
        layout->viewmodel_pose_context_sentinel.size());
    const auto viewmodel_composition = bindings.module().address(
        layout->viewmodel_composition_rva,
        layout->viewmodel_composition_sentinel.size());
    const auto dobj_bone_layout = bindings.module().address(
        layout->dobj_bone_layout_rva, kDObjBoneLayoutSentinel.size());
    const auto renderer_model_layout = bindings.module().address(
        layout->renderer_model_layout_rva,
        kRendererModelLayoutSentinel.size());
    const auto renderer_hide_part_bits = bindings.module().address(
        layout->renderer_hide_part_bits_rva,
        kRendererHidePartBitsSentinel.size());
    const auto renderer_hidden_surface_cull = bindings.module().address(
        layout->renderer_hidden_surface_cull_rva,
        kRendererHiddenSurfaceCullSentinel.size());
    const auto camera_origin = bindings.module().address(
        layout->gameplay_refdef_origin_rva, sizeof(wawvr::xr::Vec3f));
    const auto camera_axis = bindings.module().address(
        layout->gameplay_refdef_axis_rva, sizeof(wawvr::xr::Basis3f));
    const auto viewmodel_axis = bindings.module().address(
        layout->viewmodel_axis_rva, sizeof(ViewmodelAxisPlacement));
    const auto viewmodel_axis_origin = bindings.module().address(
        layout->viewmodel_axis_origin_rva, sizeof(wawvr::xr::Vec3f));
    const auto viewmodel_pose = bindings.module().address(
        layout->viewmodel_pose_rva, layout->viewmodel_pose_extent);
    const auto tag_weapon_right = bindings.module().address(
        layout->tag_weapon_right_word_rva, sizeof(std::uint16_t));
    const auto tag_weapon = bindings.module().address(
        layout->tag_weapon_word_rva, sizeof(std::uint16_t));
    const auto tag_inhand = bindings.module().address(
        layout->tag_inhand_word_rva, sizeof(std::uint16_t));
    const auto tag_origin = bindings.module().address(
        layout->tag_origin_word_rva, sizeof(std::uint16_t));
    const auto tag_brass = bindings.module().address(
        layout->tag_brass_word_rva, sizeof(std::uint16_t));
    const auto tag_flash = bindings.module().address(
        layout->tag_flash_word_rva, sizeof(std::uint16_t));
    const auto gun_pitch = bindings.data_address(
        wawvr::t4::DataSymbolId::cgame_gun_pitch_degrees, sizeof(float));
    const auto gun_yaw = bindings.data_address(
        wawvr::t4::DataSymbolId::cgame_gun_yaw_degrees, sizeof(float));
    const auto local_player_entity = bindings.data_address(
        wawvr::t4::DataSymbolId::local_player_entity,
        layout->gentity_size);
    std::uintptr_t weapon_definition_pointer_table_address = 0;
    std::uintptr_t weapon_definition_count_address = 0;
    std::uintptr_t fx_marks_no_marks_address = 0;
    std::uintptr_t fx_marks_allocated_count_address = 0;
    std::uintptr_t fx_marks_freed_count_address = 0;
    if (layout == &kSpWeaponLayout) {
        const auto table = bindings.data_address(
            wawvr::t4::DataSymbolId::weapon_definition_pointer_table,
            kWeaponDefinitionPointerTableExtent);
        const auto count = bindings.data_address(
            wawvr::t4::DataSymbolId::weapon_definition_count,
            sizeof(std::uint32_t));
        const auto no_marks = bindings.data_address(
            wawvr::t4::DataSymbolId::fx_marks_no_marks,
            sizeof(std::uint8_t));
        const auto allocated_count = bindings.data_address(
            wawvr::t4::DataSymbolId::fx_marks_allocated_count,
            sizeof(std::uint32_t));
        const auto freed_count = bindings.data_address(
            wawvr::t4::DataSymbolId::fx_marks_freed_count,
            sizeof(std::uint32_t));
        if (table.has_value() && count.has_value() &&
            bindings.site_bytes_still_match(
                wawvr::t4::HookSiteId::
                    weapon_definition_registration_sentinel)) {
            weapon_definition_pointer_table_address = *table;
            weapon_definition_count_address = *count;
        }
        if (no_marks.has_value() && allocated_count.has_value() &&
            freed_count.has_value()) {
            fx_marks_no_marks_address = *no_marks;
            fx_marks_allocated_count_address = *allocated_count;
            fx_marks_freed_count_address = *freed_count;
        }
    }
    if (!original_add_player_weapon.has_value() ||
        !original_update_pose.has_value() ||
        !get_world_tag_pos.has_value() ||
        (install_sp_player_rocket_hook &&
         (!get_world_tag_matrix.has_value() ||
          !fire_rocket.has_value() ||
          !fire_rocket_context.has_value())) ||
        !calc_muzzle_points.has_value() ||
        !fire_weapon_context.has_value() || !bullet_fire.has_value() ||
        !fire_weapon_bullet_context.has_value() ||
        !bullet_spread_context.has_value() ||
        !client_bullet_view_origin.has_value() ||
        !client_bullet_origin_context.has_value() ||
        !get_spread_for_weapon.has_value() ||
        !client_bullet_spread_context.has_value() ||
        (install_sp_client_trace_diagnostic &&
         (!client_bullet_trace.has_value() ||
           !client_bullet_trace_call.has_value())) ||
        (install_sp_impact_diagnostic &&
         (!client_impact_selector.has_value() ||
          !client_impact_spawn.has_value() ||
          fx_marks_no_marks_address == 0 ||
          fx_marks_allocated_count_address == 0 ||
          fx_marks_freed_count_address == 0)) ||
        !pose_context.has_value() ||
        !viewmodel_composition.has_value() ||
        !dobj_bone_layout.has_value() ||
        !renderer_model_layout.has_value() ||
        !renderer_hide_part_bits.has_value() ||
        !renderer_hidden_surface_cull.has_value() ||
        !camera_origin.has_value() || !camera_axis.has_value() ||
        !viewmodel_axis.has_value() ||
        !viewmodel_axis_origin.has_value() ||
        *viewmodel_axis >
            (std::numeric_limits<std::uintptr_t>::max)() -
                offsetof(ViewmodelAxisPlacement, origin) ||
        *viewmodel_axis + offsetof(ViewmodelAxisPlacement, origin) !=
            *viewmodel_axis_origin ||
        !viewmodel_pose.has_value() ||
        !tag_weapon_right.has_value() || !tag_weapon.has_value() ||
        !tag_inhand.has_value() || !tag_origin.has_value() ||
        !tag_brass.has_value() || !tag_flash.has_value() ||
        !gun_pitch.has_value() ||
        !gun_yaw.has_value() || !local_player_entity.has_value()) {
        result.status = WeaponHookStatus::address_out_of_range;
        return result;
    }
    result.original = *original_add_player_weapon;
    result.ballistics_original = *calc_muzzle_points;
    result.spread_original = *bullet_fire;
    result.client_effects_original = *client_bullet_view_origin;
    result.client_spread_original = *get_spread_for_weapon;

    if (prepared_outer.hook->expected_size != kCallInstructionSize ||
        prepared_pose.hook->expected_size != kCallInstructionSize ||
        prepared_ballistics.hook->expected_size != kCallInstructionSize ||
        prepared_spread.hook->expected_size != kCallInstructionSize ||
        prepared_client_effects.hook->expected_size !=
            kCallInstructionSize ||
        prepared_client_spread.hook->expected_size !=
            kCallInstructionSize ||
        (install_sp_player_rocket_hook &&
         prepared_player_rocket.hook->expected_size !=
             kCallInstructionSize) ||
        (install_sp_client_trace_diagnostic &&
         prepared_client_trace.hook->expected_size !=
             kCallInstructionSize) ||
        (install_sp_impact_diagnostic &&
         (prepared_impact_selector.hook->expected_size !=
              kCallInstructionSize ||
          prepared_impact_spawn.hook->expected_size !=
              kCallInstructionSize))) {
        result.status = WeaponHookStatus::original_target_mismatch;
        return result;
    }
    std::array<std::uint8_t, kCallInstructionSize> original_outer_call{};
    std::array<std::uint8_t, kCallInstructionSize> original_pose_call{};
    std::array<std::uint8_t, kCallInstructionSize>
        original_ballistics_call{};
    std::array<std::uint8_t, kCallInstructionSize> original_spread_call{};
    std::array<std::uint8_t, kCallInstructionSize>
        original_client_effects_call{};
    std::array<std::uint8_t, kCallInstructionSize>
        original_client_spread_call{};
    std::array<std::uint8_t, kCallInstructionSize>
        original_player_rocket_call{};
    std::array<std::uint8_t, kCallInstructionSize>
        original_client_trace_result{};
    std::array<std::uint8_t, kCallInstructionSize>
        native_client_trace_call{};
    std::array<std::uint8_t, kCallInstructionSize>
        original_impact_selector_call{};
    std::array<std::uint8_t, kCallInstructionSize>
        original_impact_spawn_call{};
    std::copy_n(
        prepared_outer.hook->expected.begin(), kCallInstructionSize,
        original_outer_call.begin());
    std::copy_n(
        prepared_pose.hook->expected.begin(), kCallInstructionSize,
        original_pose_call.begin());
    std::copy_n(
        prepared_ballistics.hook->expected.begin(), kCallInstructionSize,
        original_ballistics_call.begin());
    std::copy_n(
        prepared_spread.hook->expected.begin(), kCallInstructionSize,
        original_spread_call.begin());
    std::copy_n(
        prepared_client_effects.hook->expected.begin(),
        kCallInstructionSize,
        original_client_effects_call.begin());
    std::copy_n(
        prepared_client_spread.hook->expected.begin(),
        kCallInstructionSize,
        original_client_spread_call.begin());
    if (install_sp_player_rocket_hook) {
        std::copy_n(
            prepared_player_rocket.hook->expected.begin(),
            kCallInstructionSize,
            original_player_rocket_call.begin());
    }
    if (install_sp_client_trace_diagnostic) {
        std::copy_n(
            prepared_client_trace.hook->expected.begin(),
            kCallInstructionSize,
            original_client_trace_result.begin());
        const auto* const native_trace_call_site = bindings.site(
            wawvr::t4::HookSiteId::
                fire_bullet_penetrate_initial_bullet_trace_call_sentinel);
        if (native_trace_call_site == nullptr ||
            native_trace_call_site->expected_size !=
                kCallInstructionSize) {
            result.status = WeaponHookStatus::original_target_mismatch;
            return result;
        }
        std::copy_n(
            native_trace_call_site->expected.begin(),
            kCallInstructionSize,
            native_client_trace_call.begin());
    }
    if (install_sp_impact_diagnostic) {
        std::copy_n(
            prepared_impact_selector.hook->expected.begin(),
            kCallInstructionSize, original_impact_selector_call.begin());
        std::copy_n(
            prepared_impact_spawn.hook->expected.begin(),
            kCallInstructionSize, original_impact_spawn_call.begin());
    }
    if (original_outer_call[0] != 0xE8 ||
        original_pose_call[0] != 0xE8 ||
        original_ballistics_call[0] != 0xE8 ||
        original_spread_call[0] != 0xE8 ||
        original_client_effects_call[0] != 0xE8 ||
        original_client_spread_call[0] != 0xE8 ||
        (install_sp_player_rocket_hook &&
         original_player_rocket_call[0] != 0xE8) ||
        (install_sp_client_trace_diagnostic &&
         native_client_trace_call[0] != 0xE8) ||
        (install_sp_impact_diagnostic &&
         (original_impact_selector_call[0] != 0xE8 ||
          original_impact_spawn_call[0] != 0xE8)) ||
        decode_relative_call_target(
            prepared_outer.hook->target, original_outer_call) !=
            *original_add_player_weapon ||
        decode_relative_call_target(
            prepared_pose.hook->target, original_pose_call) !=
            *original_update_pose ||
        decode_relative_call_target(
            prepared_ballistics.hook->target,
            original_ballistics_call) != *calc_muzzle_points ||
        decode_relative_call_target(
            prepared_spread.hook->target, original_spread_call) !=
            *bullet_fire ||
        decode_relative_call_target(
            prepared_client_effects.hook->target,
            original_client_effects_call) !=
            *client_bullet_view_origin ||
        decode_relative_call_target(
            prepared_client_spread.hook->target,
            original_client_spread_call) !=
            *get_spread_for_weapon ||
        (install_sp_player_rocket_hook &&
         decode_relative_call_target(
             prepared_player_rocket.hook->target,
             original_player_rocket_call) != *fire_rocket) ||
        (install_sp_client_trace_diagnostic &&
         decode_relative_call_target(
             *client_bullet_trace_call,
             native_client_trace_call) != *client_bullet_trace) ||
        (install_sp_impact_diagnostic &&
         (decode_relative_call_target(
              prepared_impact_selector.hook->target,
              original_impact_selector_call) != *client_impact_selector ||
          decode_relative_call_target(
              prepared_impact_spawn.hook->target,
              original_impact_spawn_call) != *client_impact_spawn))) {
        result.status = WeaponHookStatus::original_target_mismatch;
        return result;
    }
    if (!bytes_match(
            reinterpret_cast<const void*>(*original_add_player_weapon),
            layout->cg_add_player_weapon_sentinel) ||
        !bytes_match(
            reinterpret_cast<const void*>(*original_update_pose),
            layout->cg_update_viewmodel_pose_sentinel) ||
        !bytes_match(
            reinterpret_cast<const void*>(*get_world_tag_pos),
            layout->cg_dobj_get_world_tag_pos_sentinel) ||
        (install_sp_player_rocket_hook &&
         (!bytes_match(
              reinterpret_cast<const void*>(*get_world_tag_matrix),
              layout->cg_dobj_get_world_tag_matrix_sentinel) ||
          !bindings.site_bytes_still_match(
              wawvr::t4::HookSiteId::
                  weapon_rocket_launcher_fire_rocket_context_sentinel) ||
          !bindings.site_bytes_still_match(
              wawvr::t4::HookSiteId::fire_rocket_entry_sentinel) ||
          !bindings.site_bytes_still_match(
              wawvr::t4::HookSiteId::
                  fire_rocket_stable_missile_flags_and_return_sentinel))) ||
        !bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::calc_muzzle_points_sentinel) ||
        !bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::fire_weapon_calc_muzzle_context_sentinel) ||
        !bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::bullet_fire_entry_sentinel) ||
        !bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::fire_weapon_bullet_fire_context_sentinel) ||
        !bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::bullet_fire_spread_argument_sentinel) ||
        !bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::
                draw_bullet_impacts_view_origin_context_sentinel) ||
        !bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::
                client_bullet_view_origin_entry_sentinel) ||
        !bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::
                draw_bullet_impacts_get_spread_context_sentinel) ||
        !bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::
                get_spread_for_weapon_entry_sentinel) ||
        (install_sp_client_trace_diagnostic &&
         (!bindings.site_bytes_still_match(
              wawvr::t4::HookSiteId::
                  draw_bullet_impacts_fire_bullet_penetrate_call_sentinel) ||
          !bindings.site_bytes_still_match(
              wawvr::t4::HookSiteId::
                  draw_bullet_impacts_fire_bullet_penetrate_context_sentinel) ||
          !bindings.site_bytes_still_match(
              wawvr::t4::HookSiteId::
                  fire_bullet_penetrate_entry_sentinel) ||
          !bindings.site_bytes_still_match(
              wawvr::t4::HookSiteId::
                  fire_bullet_penetrate_initial_bullet_trace_call_sentinel) ||
          !bindings.site_bytes_still_match(
              wawvr::t4::HookSiteId::
                  fire_bullet_penetrate_initial_bullet_trace_context_sentinel) ||
           !bindings.site_bytes_still_match(
               wawvr::t4::HookSiteId::
                   client_bullet_trace_entry_sentinel))) ||
        (install_sp_impact_diagnostic &&
         (!bindings.site_bytes_still_match(
              wawvr::t4::HookSiteId::
                  client_impact_selector_context_sentinel) ||
          !bindings.site_bytes_still_match(
              wawvr::t4::HookSiteId::
                  client_impact_selector_entry_sentinel) ||
          !bindings.site_bytes_still_match(
              wawvr::t4::HookSiteId::
                  client_impact_fx_spawn_context_sentinel) ||
          !bindings.site_bytes_still_match(
              wawvr::t4::HookSiteId::
                  client_impact_fx_spawn_entry_sentinel))) ||
        !bytes_match(
            reinterpret_cast<const void*>(*pose_context),
            layout->viewmodel_pose_context_sentinel) ||
        !bytes_match(
            reinterpret_cast<const void*>(*viewmodel_composition),
            layout->viewmodel_composition_sentinel) ||
        !bytes_match(
            reinterpret_cast<const void*>(*dobj_bone_layout),
            kDObjBoneLayoutSentinel) ||
        !bytes_match(
            reinterpret_cast<const void*>(*renderer_model_layout),
            kRendererModelLayoutSentinel) ||
        !bytes_match(
            reinterpret_cast<const void*>(*renderer_hide_part_bits),
            kRendererHidePartBitsSentinel) ||
        !bytes_match(
            reinterpret_cast<const void*>(*renderer_hidden_surface_cull),
            kRendererHiddenSurfaceCullSentinel)) {
        result.status = WeaponHookStatus::original_sentinel_mismatch;
        return result;
    }

    std::array<std::uint8_t, kCallInstructionSize> outer_replacement{};
    std::array<std::uint8_t, kCallInstructionSize> pose_replacement{};
    std::array<std::uint8_t, kCallInstructionSize> ballistics_replacement{};
    std::array<std::uint8_t, kCallInstructionSize> spread_replacement{};
    std::array<std::uint8_t, kCallInstructionSize>
        client_effects_replacement{};
    std::array<std::uint8_t, kCallInstructionSize>
        client_spread_replacement{};
    std::array<std::uint8_t, kCallInstructionSize>
        player_rocket_replacement{};
    std::array<std::uint8_t, kCallInstructionSize>
        client_trace_replacement{};
    std::array<std::uint8_t, kCallInstructionSize>
        impact_selector_replacement{};
    std::array<std::uint8_t, kCallInstructionSize>
        impact_spawn_replacement{};
    if (!make_relative_call(
            prepared_outer.hook->target,
            add_player_weapon_bridge_address,
            &outer_replacement) ||
        !make_relative_call(
            prepared_pose.hook->target,
            reinterpret_cast<std::uintptr_t>(
                &wawvr_update_viewmodel_pose_bridge),
            &pose_replacement) ||
        !make_relative_call(
            prepared_ballistics.hook->target,
            reinterpret_cast<std::uintptr_t>(
                &wawvr_calc_muzzle_points_bridge),
            &ballistics_replacement) ||
        !make_relative_call(
            prepared_spread.hook->target,
            reinterpret_cast<std::uintptr_t>(&wawvr_bullet_fire_bridge),
            &spread_replacement) ||
        !make_relative_call(
            prepared_client_effects.hook->target,
            reinterpret_cast<std::uintptr_t>(
                &wawvr_client_bullet_view_origin_bridge),
            &client_effects_replacement) ||
        !make_relative_call(
            prepared_client_spread.hook->target,
            reinterpret_cast<std::uintptr_t>(
                &wawvr_client_bullet_spread_bridge),
            &client_spread_replacement) ||
        (install_sp_player_rocket_hook &&
         !make_relative_call(
             prepared_player_rocket.hook->target,
             reinterpret_cast<std::uintptr_t>(
                 &wawvr_player_rocket_bridge),
             &player_rocket_replacement)) ||
        (install_sp_client_trace_diagnostic &&
         !make_relative_call(
             prepared_client_trace.hook->target,
             reinterpret_cast<std::uintptr_t>(
                  &wawvr_client_bullet_trace_bridge),
             &client_trace_replacement)) ||
        (install_sp_impact_diagnostic &&
         (!make_relative_call(
              prepared_impact_selector.hook->target,
              reinterpret_cast<std::uintptr_t>(
                  &wawvr_client_impact_selector_bridge),
              &impact_selector_replacement) ||
          !make_relative_call(
              prepared_impact_spawn.hook->target,
              reinterpret_cast<std::uintptr_t>(
                  &wawvr_client_impact_fx_spawn_bridge),
              &impact_spawn_replacement)))) {
        result.status = WeaponHookStatus::jump_out_of_range;
        return result;
    }

    std::array<PeerThreadPatchRange, 10> patch_ranges{};
    std::size_t patch_range_count = 0;
    const auto add_patch_range = [&](const std::uintptr_t target) noexcept {
        patch_ranges[patch_range_count++] = {
            target, kCallInstructionSize};
    };
    add_patch_range(prepared_outer.hook->target);
    add_patch_range(prepared_pose.hook->target);
    add_patch_range(prepared_ballistics.hook->target);
    add_patch_range(prepared_spread.hook->target);
    add_patch_range(prepared_client_effects.hook->target);
    add_patch_range(prepared_client_spread.hook->target);
    if (install_sp_player_rocket_hook) {
        add_patch_range(prepared_player_rocket.hook->target);
    }
    if (install_sp_client_trace_diagnostic) {
        add_patch_range(prepared_client_trace.hook->target);
    }
    if (install_sp_impact_diagnostic) {
        add_patch_range(prepared_impact_selector.hook->target);
        add_patch_range(prepared_impact_spawn.hook->target);
    }
    SuspendedPeerThreads suspended;
    PeerThreadQuiesceResult quiesce{};
    if (!suspended.suspend(
            std::span<const PeerThreadPatchRange>{patch_ranges}.first(
                patch_range_count),
            &quiesce)) {
        result.status = WeaponHookStatus::thread_suspend_failed;
        result.system_error = quiesce.system_error;
        return result;
    }

    auto* const outer_target = reinterpret_cast<std::uint8_t*>(
        prepared_outer.hook->target);
    auto* const pose_target = reinterpret_cast<std::uint8_t*>(
        prepared_pose.hook->target);
    auto* const ballistics_target = reinterpret_cast<std::uint8_t*>(
        prepared_ballistics.hook->target);
    auto* const spread_target = reinterpret_cast<std::uint8_t*>(
        prepared_spread.hook->target);
    auto* const client_effects_target =
        reinterpret_cast<std::uint8_t*>(
            prepared_client_effects.hook->target);
    auto* const client_spread_target =
        reinterpret_cast<std::uint8_t*>(
            prepared_client_spread.hook->target);
    auto* const player_rocket_target =
        install_sp_player_rocket_hook
            ? reinterpret_cast<std::uint8_t*>(
                  prepared_player_rocket.hook->target)
            : nullptr;
    auto* const client_trace_target =
        install_sp_client_trace_diagnostic
            ? reinterpret_cast<std::uint8_t*>(
                  prepared_client_trace.hook->target)
            : nullptr;
    auto* const impact_selector_target =
        install_sp_impact_diagnostic
            ? reinterpret_cast<std::uint8_t*>(
                  prepared_impact_selector.hook->target)
            : nullptr;
    auto* const impact_spawn_target =
        install_sp_impact_diagnostic
            ? reinterpret_cast<std::uint8_t*>(
                  prepared_impact_spawn.hook->target)
            : nullptr;
    if (!bytes_match(outer_target, original_outer_call) ||
        !bytes_match(pose_target, original_pose_call) ||
        !bytes_match(ballistics_target, original_ballistics_call) ||
        !bytes_match(spread_target, original_spread_call) ||
        !bytes_match(
            client_effects_target, original_client_effects_call) ||
        !bytes_match(
            client_spread_target, original_client_spread_call) ||
        (install_sp_player_rocket_hook &&
         !bytes_match(
             player_rocket_target, original_player_rocket_call)) ||
        (install_sp_client_trace_diagnostic &&
         !bytes_match(
             client_trace_target, original_client_trace_result)) ||
        (install_sp_impact_diagnostic &&
         (!bytes_match(
              impact_selector_target, original_impact_selector_call) ||
          !bytes_match(
              impact_spawn_target, original_impact_spawn_call)))) {
        result.status = WeaponHookStatus::expected_bytes_changed;
        return result;
    }

    std::uintptr_t viewmodel_protection_begin = std::min({
        prepared_outer.hook->target,
        prepared_pose.hook->target,
        prepared_client_effects.hook->target,
        prepared_client_spread.hook->target,
    });
    std::uintptr_t viewmodel_protection_end = std::max({
        prepared_outer.hook->target + kCallInstructionSize,
        prepared_pose.hook->target + kCallInstructionSize,
        prepared_client_effects.hook->target + kCallInstructionSize,
        prepared_client_spread.hook->target + kCallInstructionSize,
    });
    if (install_sp_client_trace_diagnostic) {
        viewmodel_protection_begin = (std::min)(
            viewmodel_protection_begin,
            prepared_client_trace.hook->target);
        viewmodel_protection_end = (std::max)(
            viewmodel_protection_end,
            prepared_client_trace.hook->target +
                kCallInstructionSize);
    }
    auto* const viewmodel_protection_target =
        reinterpret_cast<void*>(viewmodel_protection_begin);
    const std::size_t viewmodel_protection_size =
        viewmodel_protection_end - viewmodel_protection_begin;
    const std::uintptr_t ballistics_protection_begin =
        install_sp_player_rocket_hook
        ? (std::min)({
              prepared_ballistics.hook->target,
              prepared_spread.hook->target,
              prepared_player_rocket.hook->target,
          })
        : (std::min)(
              prepared_ballistics.hook->target,
              prepared_spread.hook->target);
    const std::uintptr_t ballistics_protection_end =
        install_sp_player_rocket_hook
        ? (std::max)({
              prepared_ballistics.hook->target + kCallInstructionSize,
              prepared_spread.hook->target + kCallInstructionSize,
              prepared_player_rocket.hook->target +
                  kCallInstructionSize,
          })
        : (std::max)(
              prepared_ballistics.hook->target + kCallInstructionSize,
              prepared_spread.hook->target + kCallInstructionSize);
    auto* const ballistics_protection_target =
        reinterpret_cast<void*>(ballistics_protection_begin);
    const std::size_t ballistics_protection_size =
        ballistics_protection_end - ballistics_protection_begin;
    const std::uintptr_t impact_protection_begin =
        install_sp_impact_diagnostic
        ? (std::min)(
              prepared_impact_selector.hook->target,
              prepared_impact_spawn.hook->target)
        : 0;
    const std::uintptr_t impact_protection_end =
        install_sp_impact_diagnostic
        ? (std::max)(
              prepared_impact_selector.hook->target +
                  kCallInstructionSize,
              prepared_impact_spawn.hook->target +
                  kCallInstructionSize)
        : 0;
    auto* const impact_protection_target =
        install_sp_impact_diagnostic
        ? reinterpret_cast<void*>(impact_protection_begin)
        : nullptr;
    const std::size_t impact_protection_size =
        install_sp_impact_diagnostic
        ? impact_protection_end - impact_protection_begin
        : 0;
    DWORD old_viewmodel_protection = 0;
    if (!VirtualProtect(
            viewmodel_protection_target, viewmodel_protection_size,
            PAGE_EXECUTE_READWRITE, &old_viewmodel_protection)) {
        result.status = WeaponHookStatus::target_protection_failed;
        result.system_error = GetLastError();
        return result;
    }
    DWORD old_ballistics_protection = 0;
    if (!VirtualProtect(
            ballistics_protection_target, ballistics_protection_size,
            PAGE_EXECUTE_READWRITE, &old_ballistics_protection)) {
        const DWORD protection_error = GetLastError();
        DWORD ignored = 0;
        if (!VirtualProtect(
                viewmodel_protection_target, viewmodel_protection_size,
                old_viewmodel_protection, &ignored)) {
            result.status = WeaponHookStatus::protection_restore_failed;
            result.system_error = GetLastError();
            return result;
        }
        result.status = WeaponHookStatus::target_protection_failed;
        result.system_error = protection_error;
        return result;
    }

    DWORD old_impact_protection = 0;
    if (install_sp_impact_diagnostic &&
        !VirtualProtect(
            impact_protection_target, impact_protection_size,
            PAGE_EXECUTE_READWRITE, &old_impact_protection)) {
        const DWORD protection_error = GetLastError();
        DWORD first_restore_error = 0;
        DWORD ignored = 0;
        if (!VirtualProtect(
                ballistics_protection_target, ballistics_protection_size,
                old_ballistics_protection, &ignored)) {
            first_restore_error = GetLastError();
        }
        if (!VirtualProtect(
                viewmodel_protection_target, viewmodel_protection_size,
                old_viewmodel_protection, &ignored) &&
            first_restore_error == 0) {
            first_restore_error = GetLastError();
        }
        result.status = first_restore_error == 0
            ? WeaponHookStatus::target_protection_failed
            : WeaponHookStatus::protection_restore_failed;
        result.system_error = first_restore_error == 0
            ? protection_error
            : first_restore_error;
        return result;
    }

    const auto restore_original_viewmodel_calls = [&]() noexcept {
        std::memcpy(
            outer_target, original_outer_call.data(),
            original_outer_call.size());
        std::memcpy(
            pose_target, original_pose_call.data(),
            original_pose_call.size());
        std::memcpy(
            client_effects_target,
            original_client_effects_call.data(),
            original_client_effects_call.size());
        std::memcpy(
            client_spread_target,
            original_client_spread_call.data(),
            original_client_spread_call.size());
        if (install_sp_client_trace_diagnostic) {
            std::memcpy(
                client_trace_target,
                original_client_trace_result.data(),
                original_client_trace_result.size());
        }
        FlushInstructionCache(
            GetCurrentProcess(), viewmodel_protection_target,
            viewmodel_protection_size);
    };
    const auto restore_original_ballistics_call = [&]() noexcept {
        std::memcpy(
            ballistics_target, original_ballistics_call.data(),
            original_ballistics_call.size());
        std::memcpy(
            spread_target, original_spread_call.data(),
            original_spread_call.size());
        if (install_sp_player_rocket_hook) {
            std::memcpy(
                player_rocket_target,
                original_player_rocket_call.data(),
                original_player_rocket_call.size());
        }
        FlushInstructionCache(
            GetCurrentProcess(), ballistics_protection_target,
            ballistics_protection_size);
    };
    const auto restore_original_impact_calls = [&]() noexcept {
        if (!install_sp_impact_diagnostic) {
            return;
        }
        std::memcpy(
            impact_selector_target, original_impact_selector_call.data(),
            original_impact_selector_call.size());
        std::memcpy(
            impact_spawn_target, original_impact_spawn_call.data(),
            original_impact_spawn_call.size());
        FlushInstructionCache(
            GetCurrentProcess(), impact_protection_target,
            impact_protection_size);
    };
    const auto restore_protections = [&]() noexcept -> DWORD {
        DWORD first_error = 0;
        DWORD ignored = 0;
        if (install_sp_impact_diagnostic &&
            !VirtualProtect(
                impact_protection_target, impact_protection_size,
                old_impact_protection, &ignored)) {
            first_error = GetLastError();
        }
        if (!VirtualProtect(
                ballistics_protection_target, ballistics_protection_size,
                old_ballistics_protection, &ignored)) {
            if (first_error == 0) {
                first_error = GetLastError();
            }
        }
        if (!VirtualProtect(
                viewmodel_protection_target, viewmodel_protection_size,
                old_viewmodel_protection, &ignored) &&
            first_error == 0) {
            first_error = GetLastError();
        }
        return first_error;
    };

    if (std::memcmp(
            outer_target, original_outer_call.data(),
            original_outer_call.size()) != 0 ||
        std::memcmp(
            pose_target, original_pose_call.data(),
            original_pose_call.size()) != 0 ||
        std::memcmp(
            ballistics_target, original_ballistics_call.data(),
            original_ballistics_call.size()) != 0 ||
        std::memcmp(
            spread_target, original_spread_call.data(),
            original_spread_call.size()) != 0 ||
        std::memcmp(
            client_effects_target,
            original_client_effects_call.data(),
            original_client_effects_call.size()) != 0 ||
        std::memcmp(
            client_spread_target,
            original_client_spread_call.data(),
            original_client_spread_call.size()) != 0 ||
        (install_sp_player_rocket_hook &&
         std::memcmp(
             player_rocket_target,
             original_player_rocket_call.data(),
             original_player_rocket_call.size()) != 0) ||
        (install_sp_client_trace_diagnostic &&
         std::memcmp(
             client_trace_target,
             original_client_trace_result.data(),
             original_client_trace_result.size()) != 0) ||
        (install_sp_impact_diagnostic &&
         (std::memcmp(
              impact_selector_target,
              original_impact_selector_call.data(),
              original_impact_selector_call.size()) != 0 ||
          std::memcmp(
              impact_spawn_target,
              original_impact_spawn_call.data(),
              original_impact_spawn_call.size()) != 0))) {
        const DWORD restore_error = restore_protections();
        result.status = restore_error == 0
            ? WeaponHookStatus::expected_bytes_changed
            : WeaponHookStatus::protection_restore_failed;
        result.system_error = restore_error;
        return result;
    }

    // Initialize every bridge dependency before any replacement call becomes
    // visible. Peer threads remain suspended until this function returns.
    g_weapon_layout = layout;
    g_original_add_player_weapon = *original_add_player_weapon;
    g_original_update_viewmodel_pose = *original_update_pose;
    g_original_calc_muzzle_points = *calc_muzzle_points;
    g_original_bullet_fire = *bullet_fire;
    g_original_client_bullet_view_origin =
        *client_bullet_view_origin;
    g_original_get_spread_for_weapon = *get_spread_for_weapon;
    g_client_bullet_trace_resume =
        install_sp_client_trace_diagnostic
            ? prepared_client_trace.hook->target +
                  kCallInstructionSize
            : 0;
    g_original_client_impact_selector =
        install_sp_impact_diagnostic ? *client_impact_selector : 0;
    g_client_impact_selector_resume =
        install_sp_impact_diagnostic
        ? prepared_impact_selector.hook->target + kCallInstructionSize
        : 0;
    g_original_client_impact_fx_spawn =
        install_sp_impact_diagnostic ? *client_impact_spawn : 0;
    g_client_impact_fx_spawn_resume =
        install_sp_impact_diagnostic
        ? prepared_impact_spawn.hook->target + kCallInstructionSize
            : 0;
    g_cg_dobj_get_world_tag_pos = *get_world_tag_pos;
    g_cg_dobj_get_world_tag_matrix =
        install_sp_player_rocket_hook ? *get_world_tag_matrix : 0;
    g_original_fire_rocket =
        install_sp_player_rocket_hook ? *fire_rocket : 0;
    g_camera_origin_address = *camera_origin;
    g_camera_axis_address = *camera_axis;
    g_cgame_gun_pitch_address = *gun_pitch;
    g_cgame_gun_yaw_address = *gun_yaw;
    g_viewmodel_axis_address = *viewmodel_axis;
    g_viewmodel_axis_origin_address = *viewmodel_axis_origin;
    g_viewmodel_pose_address = *viewmodel_pose;
    g_grip_tags = {{
        {*tag_weapon_right, "tag_weapon_right"},
        {*tag_weapon, "tag_weapon"},
        {*tag_inhand, "tag_inhand"},
        {*tag_origin, "tag_origin"},
    }};
    g_tag_brass_address = *tag_brass;
    g_tag_flash_address = *tag_flash;
    g_local_player_entity_address = *local_player_entity;
    // Publish the immutable SP identity table before enabling any weapon
    // bridge. MP intentionally leaves these unavailable.
    g_weapon_definition_pointer_table_address =
        weapon_definition_pointer_table_address;
    g_weapon_definition_count_address = weapon_definition_count_address;
    g_fx_marks_no_marks_address = fx_marks_no_marks_address;
    g_fx_marks_allocated_count_address =
        fx_marks_allocated_count_address;
    g_fx_marks_freed_count_address = fx_marks_freed_count_address;

    std::memcpy(
        ballistics_target, ballistics_replacement.data(),
        ballistics_replacement.size());
    std::memcpy(
        spread_target, spread_replacement.data(),
        spread_replacement.size());
    if (install_sp_player_rocket_hook) {
        std::memcpy(
            player_rocket_target, player_rocket_replacement.data(),
            player_rocket_replacement.size());
    }
    std::memcpy(
        client_effects_target,
        client_effects_replacement.data(),
        client_effects_replacement.size());
    std::memcpy(
        client_spread_target,
        client_spread_replacement.data(),
        client_spread_replacement.size());
    if (install_sp_client_trace_diagnostic) {
        std::memcpy(
            client_trace_target,
            client_trace_replacement.data(),
            client_trace_replacement.size());
    }
    if (install_sp_impact_diagnostic) {
        std::memcpy(
            impact_selector_target, impact_selector_replacement.data(),
            impact_selector_replacement.size());
        std::memcpy(
            impact_spawn_target, impact_spawn_replacement.data(),
            impact_spawn_replacement.size());
    }
    std::memcpy(pose_target, pose_replacement.data(), pose_replacement.size());
    std::memcpy(
        outer_target, outer_replacement.data(), outer_replacement.size());
    if (std::memcmp(
            outer_target, outer_replacement.data(),
            outer_replacement.size()) != 0 ||
        std::memcmp(
            pose_target, pose_replacement.data(),
            pose_replacement.size()) != 0 ||
        std::memcmp(
            ballistics_target, ballistics_replacement.data(),
            ballistics_replacement.size()) != 0 ||
        std::memcmp(
            spread_target, spread_replacement.data(),
            spread_replacement.size()) != 0 ||
        (install_sp_player_rocket_hook &&
         std::memcmp(
             player_rocket_target, player_rocket_replacement.data(),
             player_rocket_replacement.size()) != 0) ||
        std::memcmp(
            client_effects_target,
            client_effects_replacement.data(),
            client_effects_replacement.size()) != 0 ||
        std::memcmp(
            client_spread_target,
            client_spread_replacement.data(),
            client_spread_replacement.size()) != 0 ||
        (install_sp_client_trace_diagnostic &&
         std::memcmp(
             client_trace_target,
             client_trace_replacement.data(),
             client_trace_replacement.size()) != 0) ||
        (install_sp_impact_diagnostic &&
         (std::memcmp(
              impact_selector_target,
              impact_selector_replacement.data(),
              impact_selector_replacement.size()) != 0 ||
          std::memcmp(
              impact_spawn_target,
              impact_spawn_replacement.data(),
              impact_spawn_replacement.size()) != 0))) {
        restore_original_viewmodel_calls();
        restore_original_ballistics_call();
        restore_original_impact_calls();
        const DWORD restore_error = restore_protections();
        result.status = restore_error == 0
            ? WeaponHookStatus::patch_write_failed
            : WeaponHookStatus::protection_restore_failed;
        result.system_error = restore_error;
        return result;
    }
    if (!FlushInstructionCache(
            GetCurrentProcess(), viewmodel_protection_target,
            viewmodel_protection_size) ||
        !FlushInstructionCache(
            GetCurrentProcess(), ballistics_protection_target,
            ballistics_protection_size) ||
        (install_sp_impact_diagnostic &&
         !FlushInstructionCache(
             GetCurrentProcess(), impact_protection_target,
             impact_protection_size))) {
        result.system_error = GetLastError();
        restore_original_viewmodel_calls();
        restore_original_ballistics_call();
        restore_original_impact_calls();
        const DWORD restore_error = restore_protections();
        if (restore_error != 0) {
            result.system_error = restore_error;
            result.status = WeaponHookStatus::protection_restore_failed;
        } else {
            result.status = WeaponHookStatus::patch_cache_flush_failed;
        }
        return result;
    }
    const DWORD restore_error = restore_protections();
    if (restore_error != 0) {
        result.system_error = restore_error;

        // One range may already be RX. Re-open each range independently,
        // restore only the ranges made writable, then restore their exact
        // original protections again. Disabled bridges remain safe even if a
        // hostile third-party hook prevents a complete rollback.
        DWORD ignored = 0;
        const bool viewmodel_writable = VirtualProtect(
            viewmodel_protection_target, viewmodel_protection_size,
            PAGE_EXECUTE_READWRITE, &ignored) != FALSE;
        const bool ballistics_writable = VirtualProtect(
            ballistics_protection_target, ballistics_protection_size,
            PAGE_EXECUTE_READWRITE, &ignored) != FALSE;
        const bool impact_writable = install_sp_impact_diagnostic &&
            VirtualProtect(
                impact_protection_target, impact_protection_size,
                PAGE_EXECUTE_READWRITE, &ignored) != FALSE;
        if (viewmodel_writable) {
            restore_original_viewmodel_calls();
            VirtualProtect(
                viewmodel_protection_target, viewmodel_protection_size,
                old_viewmodel_protection, &ignored);
        }
        if (ballistics_writable) {
            restore_original_ballistics_call();
            VirtualProtect(
                ballistics_protection_target, ballistics_protection_size,
                old_ballistics_protection, &ignored);
        }
        if (impact_writable) {
            restore_original_impact_calls();
            VirtualProtect(
                impact_protection_target, impact_protection_size,
                old_impact_protection, &ignored);
        }
        result.status = WeaponHookStatus::protection_restore_failed;
        return result;
    }
    g_weapon_hook_enabled.store(true, std::memory_order_release);
    g_weapon_hook_installed.store(true, std::memory_order_release);
    result.status = WeaponHookStatus::installed;
    return result;
#endif
}

CampaignTargetingHookInstallResult
install_campaign_rocket_targeting_hook(
    const wawvr::t4::ValidatedBindings& bindings) noexcept {
    CampaignTargetingHookInstallResult result{};
    if (g_campaign_targeting_hook_installed.load(
            std::memory_order_acquire)) {
        result.status = CampaignTargetingHookStatus::already_installed;
        return result;
    }

#if !defined(_MSC_VER) || !defined(_M_IX86)
    static_cast<void>(bindings);
    result.status = CampaignTargetingHookStatus::rejected_wrong_profile;
    return result;
#else
    const auto& bound = bindings.profile();
    const auto layout = select_t4_layout_family(bound);
    if (layout == T4LayoutFamily::multiplayer_1_7_1263) {
        result.status = CampaignTargetingHookStatus::not_applicable;
        return result;
    }
    if (layout != T4LayoutFamily::single_player_1_7_1263) {
        result.status = CampaignTargetingHookStatus::rejected_wrong_profile;
        return result;
    }
    if (!g_weapon_hook_installed.load(std::memory_order_acquire) ||
        !g_weapon_hook_enabled.load(std::memory_order_acquire) ||
        g_weapon_layout != &kSpWeaponLayout) {
        result.status =
            CampaignTargetingHookStatus::dependency_unavailable;
        return result;
    }

    const auto prepared = wawvr::t4::prepare_inline_hook(
        bindings,
        wawvr::t4::HookSiteId::get_player_angles_scr_add_vector_call,
        reinterpret_cast<std::uintptr_t>(
            &wawvr_get_player_angles_scr_add_vector_bridge));
    if (!prepared.ok()) {
        result.status = CampaignTargetingHookStatus::preparation_failed;
        return result;
    }
    result.target = prepared.hook->target;

    const auto scr_add_vector = bindings.site_address(
        wawvr::t4::HookSiteId::scr_add_vector_entry_sentinel);
    const auto local_player_entity = bindings.data_address(
        wawvr::t4::DataSymbolId::local_player_entity,
        kSpWeaponLayout.gentity_size);
    const auto weapon_definition_pointer_table = bindings.data_address(
        wawvr::t4::DataSymbolId::weapon_definition_pointer_table,
        kWeaponDefinitionPointerTableExtent);
    const auto weapon_definition_count = bindings.data_address(
        wawvr::t4::DataSymbolId::weapon_definition_count,
        sizeof(std::uint32_t));
    const auto camera_axis = bindings.module().address(
        kSpWeaponLayout.gameplay_refdef_axis_rva,
        sizeof(wawvr::xr::Basis3f));
    if (!scr_add_vector.has_value() ||
        !local_player_entity.has_value() ||
        !weapon_definition_pointer_table.has_value() ||
        !weapon_definition_count.has_value() || !camera_axis.has_value()) {
        result.status = CampaignTargetingHookStatus::address_out_of_range;
        return result;
    }
    if (g_weapon_definition_pointer_table_address !=
            *weapon_definition_pointer_table ||
        g_weapon_definition_count_address != *weapon_definition_count ||
        g_local_player_entity_address != *local_player_entity ||
        g_camera_axis_address != *camera_axis) {
        result.status =
            CampaignTargetingHookStatus::dependency_unavailable;
        return result;
    }

    if (!bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::
                get_player_angles_registration_sentinel) ||
        !bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::get_player_angles_entry_sentinel) ||
        !bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::
                get_player_angles_return_context_sentinel) ||
        !bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::scr_add_vector_entry_sentinel) ||
        !bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::
                get_current_weapon_identity_sentinel) ||
        !bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::
                get_current_weapon_fallback_identity_sentinel) ||
        !bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::
                weapon_definition_registration_sentinel)) {
        result.status =
            CampaignTargetingHookStatus::original_sentinel_mismatch;
        return result;
    }
    if (prepared.hook->expected_size != kCallInstructionSize) {
        result.status =
            CampaignTargetingHookStatus::original_target_mismatch;
        return result;
    }

    std::array<std::uint8_t, kCallInstructionSize> original_call{};
    std::copy_n(
        prepared.hook->expected.begin(), kCallInstructionSize,
        original_call.begin());
    if (original_call[0] != 0xE8 ||
        decode_relative_call_target(
            prepared.hook->target, original_call) != *scr_add_vector) {
        result.status =
            CampaignTargetingHookStatus::original_target_mismatch;
        return result;
    }
    result.original = *scr_add_vector;

    std::array<std::uint8_t, kCallInstructionSize> replacement{};
    if (!make_relative_call(
            prepared.hook->target,
            reinterpret_cast<std::uintptr_t>(
                &wawvr_get_player_angles_scr_add_vector_bridge),
            &replacement)) {
        result.status = CampaignTargetingHookStatus::jump_out_of_range;
        return result;
    }

    const std::array<PeerThreadPatchRange, 1> patch_ranges{{
        {prepared.hook->target, kCallInstructionSize},
    }};
    SuspendedPeerThreads suspended;
    PeerThreadQuiesceResult quiesce{};
    if (!suspended.suspend(patch_ranges, &quiesce)) {
        result.status = CampaignTargetingHookStatus::thread_suspend_failed;
        result.system_error = quiesce.system_error;
        return result;
    }

    auto* const target =
        reinterpret_cast<std::uint8_t*>(prepared.hook->target);
    if (!bytes_match(target, original_call)) {
        result.status =
            CampaignTargetingHookStatus::expected_bytes_changed;
        return result;
    }

    DWORD old_protection = 0;
    if (!VirtualProtect(
            target, kCallInstructionSize, PAGE_EXECUTE_READWRITE,
            &old_protection)) {
        result.status =
            CampaignTargetingHookStatus::target_protection_failed;
        result.system_error = GetLastError();
        return result;
    }
    const auto restore_protection = [&]() noexcept -> DWORD {
        DWORD ignored = 0;
        if (!VirtualProtect(
                target, kCallInstructionSize, old_protection, &ignored)) {
            return GetLastError();
        }
        return 0;
    };
    const auto restore_original_call = [&]() noexcept {
        std::memcpy(target, original_call.data(), original_call.size());
        FlushInstructionCache(
            GetCurrentProcess(), target, kCallInstructionSize);
    };

    if (std::memcmp(
            target, original_call.data(), original_call.size()) != 0) {
        const DWORD restore_error = restore_protection();
        result.status = restore_error == 0
            ? CampaignTargetingHookStatus::expected_bytes_changed
            : CampaignTargetingHookStatus::protection_restore_failed;
        result.system_error = restore_error;
        return result;
    }

    // Publish every bridge dependency before the replacement call becomes
    // visible. The bridge remains disabled until bytes and RX protection are
    // both verified, and all peer threads remain suspended in this scope.
    g_original_scr_add_vector = *scr_add_vector;

    std::memcpy(target, replacement.data(), replacement.size());
    if (std::memcmp(
            target, replacement.data(), replacement.size()) != 0) {
        restore_original_call();
        const DWORD restore_error = restore_protection();
        result.status = restore_error == 0
            ? CampaignTargetingHookStatus::patch_write_failed
            : CampaignTargetingHookStatus::protection_restore_failed;
        result.system_error = restore_error;
        return result;
    }
    if (!FlushInstructionCache(
            GetCurrentProcess(), target, kCallInstructionSize)) {
        result.system_error = GetLastError();
        restore_original_call();
        const DWORD restore_error = restore_protection();
        if (restore_error != 0) {
            result.status =
                CampaignTargetingHookStatus::protection_restore_failed;
            result.system_error = restore_error;
        } else {
            result.status =
                CampaignTargetingHookStatus::patch_cache_flush_failed;
        }
        return result;
    }
    const DWORD restore_error = restore_protection();
    if (restore_error != 0) {
        result.system_error = restore_error;
        g_campaign_targeting_hook_enabled.store(
            false, std::memory_order_release);

        // Re-open only this proven five-byte call and roll it back. The
        // original target stays published so a hostile concurrent patch can
        // still fail through the disabled pass-through bridge.
        DWORD ignored = 0;
        if (VirtualProtect(
                target, kCallInstructionSize, PAGE_EXECUTE_READWRITE,
                &ignored)) {
            restore_original_call();
            VirtualProtect(
                target, kCallInstructionSize, old_protection, &ignored);
        }
        result.status =
            CampaignTargetingHookStatus::protection_restore_failed;
        return result;
    }

    g_campaign_targeting_hook_enabled.store(
        true, std::memory_order_release);
    g_campaign_targeting_hook_installed.store(
        true, std::memory_order_release);
    result.status = CampaignTargetingHookStatus::installed;
    return result;
#endif
}

const char* weapon_hook_status_name(const WeaponHookStatus status) noexcept {
    switch (status) {
    case WeaponHookStatus::installed: return "installed";
    case WeaponHookStatus::already_installed: return "already-installed";
    case WeaponHookStatus::disabled_by_environment: return "disabled-by-environment";
    case WeaponHookStatus::rejected_wrong_profile: return "rejected-wrong-profile";
    case WeaponHookStatus::input_dependency_unavailable: return "input-dependency-unavailable";
    case WeaponHookStatus::preparation_failed: return "preparation-failed";
    case WeaponHookStatus::address_out_of_range: return "address-out-of-range";
    case WeaponHookStatus::original_target_mismatch: return "original-target-mismatch";
    case WeaponHookStatus::original_sentinel_mismatch: return "original-sentinel-mismatch";
    case WeaponHookStatus::jump_out_of_range: return "jump-out-of-range";
    case WeaponHookStatus::thread_suspend_failed: return "thread-suspend-failed";
    case WeaponHookStatus::target_protection_failed: return "target-protection-failed";
    case WeaponHookStatus::expected_bytes_changed: return "expected-bytes-changed";
    case WeaponHookStatus::patch_write_failed: return "patch-write-failed";
    case WeaponHookStatus::patch_cache_flush_failed: return "patch-cache-flush-failed";
    case WeaponHookStatus::protection_restore_failed: return "protection-restore-failed";
    }
    return "unknown";
}

const char* campaign_targeting_hook_status_name(
    const CampaignTargetingHookStatus status) noexcept {
    switch (status) {
    case CampaignTargetingHookStatus::installed: return "installed";
    case CampaignTargetingHookStatus::already_installed: return "already-installed";
    case CampaignTargetingHookStatus::not_applicable: return "not-applicable";
    case CampaignTargetingHookStatus::dependency_unavailable: return "dependency-unavailable";
    case CampaignTargetingHookStatus::rejected_wrong_profile: return "rejected-wrong-profile";
    case CampaignTargetingHookStatus::preparation_failed: return "preparation-failed";
    case CampaignTargetingHookStatus::address_out_of_range: return "address-out-of-range";
    case CampaignTargetingHookStatus::original_target_mismatch: return "original-target-mismatch";
    case CampaignTargetingHookStatus::original_sentinel_mismatch: return "original-sentinel-mismatch";
    case CampaignTargetingHookStatus::jump_out_of_range: return "jump-out-of-range";
    case CampaignTargetingHookStatus::thread_suspend_failed: return "thread-suspend-failed";
    case CampaignTargetingHookStatus::target_protection_failed: return "target-protection-failed";
    case CampaignTargetingHookStatus::expected_bytes_changed: return "expected-bytes-changed";
    case CampaignTargetingHookStatus::patch_write_failed: return "patch-write-failed";
    case CampaignTargetingHookStatus::patch_cache_flush_failed: return "patch-cache-flush-failed";
    case CampaignTargetingHookStatus::protection_restore_failed: return "protection-restore-failed";
    }
    return "unknown";
}

bool read_runtime_sp_weapon_definition(
    const std::int32_t weapon_index,
    RuntimeWeaponDefinitionIdentity* const identity) noexcept {
    return read_runtime_sp_weapon_definition_snapshot(
        weapon_index, identity);
}

bool find_runtime_sp_weapon_definition(
    const void* const weapon_definition,
    RuntimeWeaponDefinitionIdentity* const identity) noexcept {
    if (identity == nullptr) {
        return false;
    }
    *identity = {};
    const std::uintptr_t address =
        reinterpret_cast<std::uintptr_t>(weapon_definition);
    if (address == 0 ||
        address > (std::numeric_limits<std::uint32_t>::max)()) {
        return false;
    }
    const std::uint32_t requested = static_cast<std::uint32_t>(address);
    WeaponIdentitySnapshotReader memory;
    std::uint32_t count = 0;
    if (!read_runtime_sp_weapon_count(&count, memory)) {
        return false;
    }
    for (std::uint32_t index = 1; index <= count; ++index) {
        const std::uintptr_t slot =
            g_weapon_definition_pointer_table_address +
            static_cast<std::size_t>(index) * sizeof(std::uint32_t);
        std::uint32_t candidate = 0;
        std::memcpy(
            &candidate, reinterpret_cast<const void*>(slot),
            sizeof(candidate));
        if (candidate != requested) {
            continue;
        }
        RuntimeWeaponDefinitionIdentity snapshot{};
        if (!read_runtime_sp_weapon_definition_from_checked_table(
                static_cast<std::int32_t>(index), count, memory, &snapshot)) {
            return false;
        }
        std::uint32_t stable_candidate = 0;
        std::memcpy(
            &stable_candidate, reinterpret_cast<const void*>(slot),
            sizeof(stable_candidate));
        if (stable_candidate != requested) {
            return false;
        }
        *identity = snapshot;
        return true;
    }
    return false;
}

bool read_final_visible_weapon_aim(
    const std::uint64_t controller_generation,
    const std::uint64_t now_milliseconds,
    float* const pitch_degrees,
    float* const yaw_degrees) noexcept {
    FinalVisibleAim snapshot{};
    if (pitch_degrees == nullptr || yaw_degrees == nullptr ||
        !read_fresh_final_visible_aim(
            controller_generation, now_milliseconds, &snapshot)) {
        return false;
    }
    *pitch_degrees = snapshot.pitch_degrees;
    *yaw_degrees = snapshot.yaw_degrees;
    return true;
}

bool read_final_visible_weapon_receipt(
    const std::uint64_t expected_frame_id,
    FinalVisibleWeaponReceipt* const receipt) noexcept {
    if (expected_frame_id == 0 || receipt == nullptr) {
        return false;
    }
    AcquireSRWLockShared(&g_final_visible_aim_lock);
    const FinalVisibleAim snapshot = g_final_visible_aim;
    ReleaseSRWLockShared(&g_final_visible_aim_lock);
    if (!snapshot.valid || snapshot.controller_generation == 0 ||
        snapshot.controller_frame_id != expected_frame_id ||
        snapshot.publication_milliseconds == 0) {
        return false;
    }
    *receipt = {
        snapshot.controller_generation,
        snapshot.controller_frame_id,
        snapshot.controller_action_sequence,
        snapshot.publication_milliseconds,
        snapshot.live_controller_pose,
    };
    return true;
}

bool read_weapon_frame_base_receipt(
    const std::uint64_t expected_frame_id,
    WeaponFrameBaseReceipt* const receipt) noexcept {
    if (expected_frame_id == 0 || receipt == nullptr) {
        return false;
    }
    AcquireSRWLockShared(&g_weapon_frame_base_receipt_lock);
    const WeaponFrameBaseReceipt snapshot = g_weapon_frame_base_receipt;
    ReleaseSRWLockShared(&g_weapon_frame_base_receipt_lock);
    if (!snapshot.valid || snapshot.frame_id != expected_frame_id) {
        return false;
    }
    *receipt = snapshot;
    return true;
}

bool read_published_weapon_muzzle(
    const std::uint64_t controller_generation,
    const std::uint64_t now_milliseconds,
    wawvr::xr::Vec3f* const origin) noexcept {
    if (origin == nullptr) {
        return false;
    }
    PublishedWeaponMuzzleSnapshot snapshot{};
    if (!read_fresh_published_muzzle_snapshot(
            controller_generation, now_milliseconds, &snapshot)) {
        return false;
    }
    *origin = snapshot.origin;
    return true;
}

bool read_published_projectile_pose(
    const std::uint64_t controller_generation,
    const std::uint64_t now_milliseconds,
    wawvr::xr::Vec3f* const origin,
    wawvr::xr::Basis3f* const projectile_basis) noexcept {
    if (origin == nullptr || projectile_basis == nullptr) {
        return false;
    }
    PublishedWeaponMuzzleSnapshot snapshot{};
    if (!read_fresh_published_muzzle_snapshot(
            controller_generation, now_milliseconds, &snapshot) ||
        !snapshot.projectile_basis_valid ||
        !finite_basis(snapshot.projectile_basis)) {
        return false;
    }
    *origin = snapshot.origin;
    *projectile_basis = snapshot.projectile_basis;
    return true;
}

bool read_viewmodel_world_tag_position(
    void* const viewmodel_dobj,
    const std::uint16_t tag,
    const void* const viewmodel_pose,
    wawvr::xr::Vec3f* const world_position) noexcept {
    if (!g_weapon_hook_enabled.load(std::memory_order_acquire) ||
        g_cg_dobj_get_world_tag_pos == 0 || viewmodel_dobj == nullptr ||
        tag == 0 || viewmodel_pose == nullptr || world_position == nullptr) {
        return false;
    }
    wawvr::xr::Vec3f result{};
    if (wawvr_call_dobj_get_world_tag_pos(
            viewmodel_dobj, tag, viewmodel_pose, &result) == 0 ||
        !std::isfinite(result.x) || !std::isfinite(result.y) ||
        !std::isfinite(result.z)) {
        return false;
    }
    *world_position = result;
    return true;
}

bool read_viewmodel_world_tag_pose(
    void* const viewmodel_dobj,
    const std::uint16_t tag,
    const void* const viewmodel_pose,
    wawvr::xr::EnginePose* const world_pose) noexcept {
    if (!g_weapon_hook_enabled.load(std::memory_order_acquire) ||
        g_cg_dobj_get_world_tag_matrix == 0 || viewmodel_dobj == nullptr ||
        tag == 0 || viewmodel_pose == nullptr || world_pose == nullptr) {
        return false;
    }
    std::array<float, 9> matrix{};
    wawvr::xr::Vec3f origin{};
    if (wawvr_call_dobj_get_world_tag_matrix(viewmodel_dobj, tag,
            viewmodel_pose, matrix.data(), &origin) == 0) {
        return false;
    }
    const wawvr::xr::Basis3f raw_axis{
        {matrix[0], matrix[1], matrix[2]},
        {matrix[3], matrix[4], matrix[5]},
        {matrix[6], matrix[7], matrix[8]},
    };
    wawvr::xr::Basis3f axis{};
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) ||
        !std::isfinite(origin.z) ||
        !build_evaluated_projectile_basis(raw_axis.forward, raw_axis, &axis)) {
        return false;
    }
    *world_pose = {.position = origin, .axis = axis};
    return true;
}

bool controller_weapon_uses_support_pose() noexcept {
    return g_weapon_hook_enabled.load(std::memory_order_acquire) &&
           g_published_support_pose.load(std::memory_order_acquire);
}

ControllerPhysicalScopeIdentity classify_controller_weapon_physical_scope(
    const std::int32_t weapon_index) noexcept {
    RuntimeWeaponDefinitionIdentity identity{};
    if (!g_weapon_hook_enabled.load(std::memory_order_acquire) ||
        weapon_index <= 0 ||
        !read_runtime_sp_weapon_definition_snapshot(
            weapon_index, &identity)) {
        return ControllerPhysicalScopeIdentity::unavailable;
    }
    return find_physical_scope_profile(identity.name.data()) != nullptr
        ? ControllerPhysicalScopeIdentity::physical_scope
        : ControllerPhysicalScopeIdentity::unscoped;
}

void request_weapon_hook_shutdown() noexcept {
    // The script bridge depends on the tracked-weapon globals. Disable it
    // first; neither callsite is unpatched during process detach.
    g_campaign_targeting_hook_enabled.store(
        false, std::memory_order_release);
    g_weapon_hook_enabled.store(false, std::memory_order_release);
    g_published_support_pose.store(false, std::memory_order_release);
    g_head_relative_weapon_freeze = {};
    g_chest_weapon_pose_state = {};
    reset_right_ray_two_hand_steering(&g_right_ray_two_hand_steering);
    AcquireSRWLockExclusive(&g_weapon_frame_base_receipt_lock);
    g_weapon_frame_base_receipt = {};
    ReleaseSRWLockExclusive(&g_weapon_frame_base_receipt_lock);
}

bool weapon_viewmodel_hook_installed() noexcept {
    return g_weapon_hook_installed.load(std::memory_order_acquire);
}

bool weapon_viewmodel_hook_enabled() noexcept {
    return g_weapon_hook_enabled.load(std::memory_order_acquire);
}

}  // namespace wawvr::mod
