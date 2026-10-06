// SPDX-License-Identifier: GPL-3.0-only
#include "manual_reload_runtime.hpp"

#include "automatic_reload_setting.hpp"
#include "bounded_bone_matrices.hpp"
#include "detachable_magazine_weapon_profile.hpp"
#include "input_mapping.hpp"
#include "kar98_bolt_action_logic.hpp"
#include "magazine_charging_logic.hpp"
#include "magazine_chamber_lock_table.hpp"
#include "manual_reload_controller_logic.hpp"
#include "peer_thread_quiescence.hpp"
#include "resident_page_access.hpp"
#include "stereo_diagnostics.hpp"
#include "synthetic_scene_slots.hpp"
#include "t4_layout_selector.hpp"
#include "t4_menu_input.hpp"
#include "viewmodel_filter.hpp"
#include "weapon_hook.hpp"
#include "weapon_identity_snapshot_reader.hpp"
#include "weapon_placement.hpp"

#include "xr_math.h"

#include "gameplay/rigid_submesh_extraction.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace wawvr::mod {
namespace {

std::atomic<HeadsetSvt40SelectionState> g_headset_svt40_selection_state{
    HeadsetSvt40SelectionState::unknown};
std::atomic<HeadsetM1GarandGlSelectionState>
    g_headset_m1garand_gl_selection_state{
        HeadsetM1GarandGlSelectionState::unknown};

using DbFindXAssetHeaderFunction = void* (__cdecl*)(
    int asset_type,
    const char* asset_name,
    bool error_if_missing,
    int wait_time);

constexpr wawvr::t4::Rva kReloadRequestCallRva = 0x0001FACD;
constexpr wawvr::t4::Rva kReloadEmptyCallRva = 0x0002092C;
// PM_Weapon_CheckForRechamber selects animation 4 (hip) or 7 (ADS) here,
// immediately before it enters WEAPON_RECHAMBERING. Preserve native state and
// timing, but replace that one automatic visual start while our physical
// bolt-action cycle is authoritative.
constexpr wawvr::t4::Rva kRechamberVisualAnimContextRva = 0x0001E7EA;
constexpr std::size_t kRechamberVisualAnimCallOffset = 27;
// Each supported rifle bolt is a rigid surface. R_PreSkinXSurface synchronously copies
// its final DObj matrix into an independent rigid placement at this callsite.
// Snapshot and seal only that surface at the actual presentation-consumption
// boundary so a later native rechamber evaluation cannot leak one frame.
constexpr wawvr::t4::Rva kPreSkinSurfaceContextRva = 0x0031E27B;
constexpr std::size_t kPreSkinSurfaceCallOffset = 13;
constexpr wawvr::t4::Rva kPreSkinXSurfaceRva = 0x0031DDB0;
constexpr wawvr::t4::Rva kBeginWeaponReloadRva = 0x0001EA30;
constexpr wawvr::t4::Rva kReloadClipRva = 0x0001E350;
constexpr wawvr::t4::Rva kStartWeaponAnimRva = 0x0001D420;
constexpr wawvr::t4::Rva kReloadRequestContextRva = 0x0001FAB1;
constexpr std::size_t kReloadRequestCallOffset = 28;
constexpr wawvr::t4::Rva kReloadEmptyContextRva = 0x0002090C;
constexpr std::size_t kReloadEmptyCallOffset = 32;

constexpr std::array<std::uint8_t, 41> kReloadRequestContext{
    0xF3, 0x0F, 0x10, 0x86, 0x10, 0x01, 0x00, 0x00,
    0x0F, 0x2E, 0x05, 0x4C, 0x6A, 0x82, 0x00, 0x9F,
    0xF6, 0xC4, 0x44, 0x7A, 0x07, 0x83, 0x7C, 0x24,
    0x10, 0x00, 0x74, 0x05, 0xE8, 0x5E, 0xEF, 0xFF,
    0xFF, 0x5F, 0x5E, 0x5D, 0x5B, 0x83, 0xC4, 0x08,
    0xC3,
};
constexpr std::array<std::uint8_t, 50> kReloadEmptyContext{
    0x8B, 0xBF, 0x44, 0x01, 0x00, 0x00, 0x83, 0xFF,
    0x01, 0x74, 0x0C, 0x83, 0xFF, 0x06, 0x74, 0x07,
    0x81, 0x46, 0x40, 0xF4, 0x01, 0x00, 0x00, 0x5F,
    0x33, 0xC0, 0x5E, 0xC3, 0x85, 0xC9, 0x74, 0xA0,
    0xE8, 0xFF, 0xE0, 0xFF, 0xFF, 0x5F, 0x33, 0xC0,
    0x5E, 0xC3, 0x5F, 0xB8, 0x01, 0x00, 0x00, 0x00,
    0x5E, 0xC3,
};
constexpr std::array<std::uint8_t, 64> kRechamberVisualAnimContext{
    0x85, 0xC9, 0x75, 0x67, 0xF3, 0x0F, 0x10, 0x80,
    0x10, 0x01, 0x00, 0x00, 0x0F, 0x2F, 0x05, 0x80,
    0xDA, 0x83, 0x00, 0x76, 0x04, 0x6A, 0x07, 0xEB,
    0x02, 0x6A, 0x04,
    0xE8, 0x16, 0xEC, 0xFF, 0xFF,
    0xC7, 0x80, 0x08, 0x01, 0x00, 0x00, 0x06, 0x00,
    0x00, 0x00, 0x8B, 0x8F, 0x4C, 0x04, 0x00, 0x00,
    0x89, 0x48, 0x40, 0x8B, 0x8F, 0x50, 0x04, 0x00,
    0x00, 0x83, 0xC4, 0x04, 0x85, 0xC9, 0x74, 0x1A,
};
constexpr std::array<std::uint8_t, 28> kPreSkinSurfaceContext{
    0x83, 0xC6, 0xCC, 0x56, 0x52, 0x8D, 0x54, 0x24,
    0x24, 0x8D, 0x4C, 0x24, 0x44,
    0xE8, 0x23, 0xFB, 0xFF, 0xFF,
    0xF3, 0x0F, 0x7E, 0x44, 0x24, 0x44, 0x8B, 0x4C,
    0x24, 0x4C,
};
constexpr std::array<std::uint8_t, 37> kPreSkinXSurfacePrologue{
    0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x8B, 0x45,
    0x0C, 0x81, 0xEC, 0x98, 0x00, 0x00, 0x00, 0x80,
    0x78, 0x01, 0x00, 0x53, 0x56, 0x0F, 0x85, 0x5B,
    0x02, 0x00, 0x00, 0x8B, 0x35, 0xFC, 0x52, 0xF5,
    0x01, 0x80, 0x7E, 0x10, 0x00,
};
constexpr std::array<std::uint8_t, 32> kBeginReloadPrologue{
    0x8B, 0x86, 0x08, 0x01, 0x00, 0x00, 0x85, 0xC0,
    0x8B, 0x8E, 0x04, 0x01, 0x00, 0x00, 0x55, 0x8B,
    0x2C, 0x8D, 0x70, 0x67, 0x8F, 0x00, 0x74, 0x1C,
    0x83, 0xF8, 0x05, 0x74, 0x17, 0x83, 0xF8, 0x06,
};
constexpr std::size_t kBeginReloadEpilogueOffset = 0x16B;
constexpr std::array<std::uint8_t, 2> kBeginReloadEpilogue{0x5D, 0xC3};
constexpr std::array<std::uint8_t, 32> kReloadClipPrologue{
    0x8B, 0x8E, 0x08, 0x01, 0x00, 0x00, 0x8B, 0x86,
    0x04, 0x01, 0x00, 0x00, 0x8B, 0x04, 0x85, 0x70,
    0x67, 0x8F, 0x00, 0x83, 0xEC, 0x14, 0x83, 0xF9,
    0x09, 0x74, 0x05, 0x83, 0xF9, 0x0A, 0x75, 0x0D,
};
constexpr std::size_t kReloadClipEpilogueOffset = 0x11F;
constexpr std::array<std::uint8_t, 7> kReloadClipEpilogue{
    0x5F, 0x5D, 0x5B, 0x83, 0xC4, 0x14, 0xC3,
};
constexpr std::array<std::uint8_t, 31> kStartWeaponAnimSentinel{
    0x83, 0x78, 0x04, 0x08, 0x7D, 0x18, 0x8B, 0x88,
    0x10, 0x09, 0x00, 0x00, 0xF7, 0xD1, 0x81, 0xE1,
    0x00, 0x02, 0x00, 0x00, 0x0B, 0x4C, 0x24, 0x04,
    0x89, 0x88, 0x10, 0x09, 0x00, 0x00, 0xC3,
};

constexpr std::size_t kPlayerStateMinimumSpan = 0x920;
constexpr std::size_t kPlayerStateCommandTimeOffset = 0x00;
constexpr std::size_t kPlayerStateWeaponTimeOffset = 0x40;
constexpr std::size_t kPlayerStateWeaponDelayOffset = 0x44;
constexpr std::size_t kPlayerStateWeaponOffset = 0x104;
constexpr std::size_t kPlayerStateWeaponStateOffset = 0x108;
constexpr std::size_t kPlayerStateShotCountOffset = 0x10C;
constexpr std::size_t kPlayerStateRechamberOffset = 0x81C;
// Exact T4 playerState_s layout. T4 inserts six stats before ammo, plus heat
// and overheat arrays before ammoclip; the older COD4 offsets read unrelated
// state and made the live Colt counts appear permanently empty.
constexpr std::size_t kPlayerStateAmmoOffset = 0x17C;
constexpr std::size_t kPlayerStateClipAmmoOffset = 0x5FC;
constexpr std::size_t kMaximumPlayerAmmoPools = 128;
// T4 expands WeaponDef ahead of the world-model/ammo block relative to the
// older COD4 layout. These are the exact retail 1.7 T4 offsets (the previous
// COD4 offsets landed in sprint/ADS floats and made every detachable-magazine
// weapon permanently fail its reload eligibility gate).
constexpr std::size_t kWeaponDefinitionWorldClipModelOffset = 0x3C4;
constexpr std::size_t kWeaponDefinitionAmmoIndexOffset = 0x3F4;
constexpr std::size_t kWeaponDefinitionClipIndexOffset = 0x3FC;
constexpr std::size_t kWeaponDefinitionClipSizeOffset = 0x408;
constexpr std::size_t kWeaponDefinitionReloadAmmoAddOffset = 0x630;
constexpr std::size_t kWeaponDefinitionReloadStartAddOffset = 0x634;
constexpr std::size_t kWeaponDefinitionBoltActionOffset = 0x5D4;
constexpr std::size_t kWeaponDefinitionSegmentedReloadOffset = 0x628;
constexpr std::size_t kWeaponDefinitionManualReloadSpan =
    kWeaponDefinitionReloadStartAddOffset + sizeof(std::int32_t);
constexpr std::size_t kWeaponDefinitionGunXModel0Offset = 0x0C;
constexpr int kWeaponAssetType = 0x18;
constexpr float kSqueezeEngage = 0.70F;
constexpr float kSqueezeRelease = 0.35F;
// A local single-player reload command is first replayed against prediction
// and then consumed by the authoritative simulation. Keep a detachable
// magazine commit visible across both passes; otherwise prediction refills and
// the next authoritative snapshot immediately restores the old ammo count.
constexpr std::uint64_t kDetachableMagazineCommitFanoutMilliseconds = 250;
constexpr wawvr::t4::Rva kDbFindXAssetHeaderRva = 0x0008DA30;
constexpr wawvr::t4::Rva kDObjCreateRva = 0x00209340;
constexpr std::array<std::uint8_t, 32> kDObjCreatePrologue{
    0x51, 0x53, 0x6A, 0x38, 0x33, 0xDB, 0x8D, 0x46,
    0x14, 0x53, 0x50, 0xE8, 0xF0, 0x6B, 0x1A, 0x00,
    0x8B, 0x54, 0x24, 0x1C, 0x8B, 0x44, 0x24, 0x18,
    0x66, 0x8B, 0x4C, 0x24, 0x20, 0x83, 0xC4, 0x0C,
};
constexpr std::size_t kDObjCreateEpilogueOffset = 0x70;
constexpr std::array<std::uint8_t, 14> kDObjCreateEpilogue{
    0x66, 0x89, 0x5E, 0x62, 0x88, 0x5E, 0x61,
    0xC6, 0x46, 0x60, 0xFF, 0x5B, 0x59, 0xC3,
};
constexpr wawvr::t4::Rva kAddDObjToSceneRva = 0x002DA340;
constexpr std::array<std::uint8_t, 32> kAddDObjToScenePrologue{
    0x0F, 0x57, 0xC0, 0x83, 0xEC, 0x38, 0x53, 0x8B,
    0x5C, 0x24, 0x48, 0x55, 0x56, 0x0F, 0x2E, 0xC8,
    0x8B, 0xF0, 0x9F, 0xF6, 0xC4, 0x44, 0x57, 0x7A,
    0x16, 0x85, 0xC9, 0x75, 0x12, 0x0F, 0x2F, 0xD8,
};
constexpr std::size_t kAddDObjSceneIndexStoreOffset = 0x1D8;
constexpr std::array<std::uint8_t, 10> kAddDObjSceneIndexStore{
    0x8B, 0x15, 0x14, 0x8D, 0xDA,
    0x03, 0x66, 0x89, 0x0C, 0x42,
};
constexpr wawvr::t4::Rva kSetControlTagAnglesRva = 0x0020BF30;
constexpr std::array<std::uint8_t, 24> kSetControlTagAnglesPrologue{
    0x3D, 0xFE, 0x00, 0x00, 0x00, 0x53, 0x8B, 0x5C,
    0x24, 0x08, 0x73, 0x4B, 0x56, 0x8B, 0xC8, 0x83,
    0xE1, 0x1F, 0xBA, 0x00, 0x00, 0x00, 0x80, 0x8B,
};
constexpr wawvr::t4::Rva kSlFindStringRva = 0x0028DD20;
constexpr std::array<std::uint8_t, 36> kSlFindStringSentinel{
    0x8B, 0xC2, 0x56, 0x8D, 0x70, 0x01, 0x8A, 0x08,
    0x83, 0xC0, 0x01, 0x84, 0xC9, 0x75, 0xF7, 0x2B,
    0xC6, 0x83, 0xC0, 0x01, 0x50, 0x8B, 0x44, 0x24,
    0x0C, 0x52, 0xE8, 0x51, 0xFD, 0xFF, 0xFF, 0x83,
    0xC4, 0x08, 0x5E, 0xC3,
};
constexpr wawvr::t4::Rva kWeaponInfoArrayRva = 0x03063C40;
constexpr std::size_t kWeaponInfoStride = 0x48;
constexpr std::size_t kWeaponInfoPartBitsOffset = 0x18;
constexpr std::size_t kDObjSize = 0x68;
constexpr std::size_t kDObjNumModelsOffset = 0x09;
constexpr std::size_t kDObjNumBonesOffset = 0x0A;
constexpr std::size_t kDObjSkelMatOffset = 0x48;
constexpr std::size_t kDObjSkelPartBitsOffset = 0x34;
constexpr std::size_t kDObjModelsOffset = 0x64;
constexpr std::size_t kPoseSize = 0x64;
constexpr std::size_t kPoseOriginOffset = 0x24;
constexpr std::size_t kPoseAnglesOffset = 0x30;
constexpr wawvr::t4::Rva kViewmodelWorldOriginRva = 0x03120388;
constexpr wawvr::t4::Rva kViewmodelPoseRva = 0x031CCFB4;
constexpr std::uint32_t kClipRenderFlags = 0x07;
struct DObjAnimMat final {
    float quaternion[4]{};
    float translation[3]{};
    float translation_weight{};
};

struct alignas(16) PackedVertex final {
    float xyz[3]{};
    float binormal_sign{};
    std::uint32_t color{};
    std::uint32_t texcoord{};
    std::uint32_t normal{};
    std::uint32_t tangent{};
};

struct XSurfaceVertexInfo final {
    std::int16_t grouped_vertex_count[4]{};
    std::uint16_t* blend{};
};

struct RigidVertexList final {
    std::uint16_t bone_offset{};
    std::uint16_t vertex_count{};
    std::uint16_t triangle_offset{};
    std::uint16_t triangle_count{};
    void* collision_tree{};
};

struct XSurface final {
    std::uint8_t tile_mode{};
    bool deformed{};
    std::uint16_t vertex_count{};
    std::uint16_t triangle_count{};
    std::uint8_t zone_handle{};
    std::uint8_t padding{};
    std::uint16_t base_triangle_index{};
    std::uint16_t base_vertex_index{};
    std::uint16_t* triangle_indices{};
    XSurfaceVertexInfo vertex_info{};
    PackedVertex* vertices{};
    void* vertex_buffer{};
    std::uint32_t rigid_vertex_list_count{};
    RigidVertexList* rigid_vertex_list{};
    void* index_buffer{};
    std::int32_t part_bits[4]{};
};

struct XModelLodInfo final {
    float distance{};
    std::uint16_t surface_count{};
    std::uint16_t surface_index{};
    std::int32_t part_bits[4]{};
    std::uint8_t lod{};
    std::uint8_t smc_index_plus_one{};
    std::uint8_t smc_alloc_bits{};
    std::uint8_t unused{};
};

struct XModel final {
    const char* name{};
    std::uint8_t bone_count{};
    std::uint8_t root_bone_count{};
    std::uint8_t surface_count{};
    std::uint8_t lod_ramp_type{};
    std::uint16_t* bone_names{};
    std::uint8_t* parent_list{};
    std::int16_t* quaternions{};
    float* translations{};
    std::uint8_t* part_classification{};
    DObjAnimMat* base_matrices{};
    XSurface* surfaces{};
    void** material_handles{};
    XModelLodInfo lod_info[4]{};
    void* collision_surfaces{};
    std::int32_t collision_surface_count{};
    std::int32_t contents{};
    void* bone_info{};
    float radius{};
    float minimums[3]{};
    float maximums[3]{};
    std::int16_t lod_count{};
    std::int16_t collision_lod{};
    std::uint8_t stream_info_and_padding[4]{};
    std::int32_t memory_usage{};
    std::uint8_t flags{};
    bool bad{};
    std::uint8_t tail_padding[2]{};
    void* physics_preset{};
    void* physics_geometry{};
    void* collision_map{};
    void* physics_constraints{};
};

struct DObjModelDescription final {
    XModel* model{};
    std::uint16_t bone_name{};
    bool ignore_collision{};
    std::uint8_t padding{};
};

static_assert(sizeof(DObjAnimMat) == 0x20);
static_assert(sizeof(PackedVertex) == 0x20);
static_assert(sizeof(XSurfaceVertexInfo) == 0x0C);
static_assert(sizeof(RigidVertexList) == 0x0C);
static_assert(sizeof(XSurface) == 0x40);
static_assert(sizeof(XModelLodInfo) == 0x1C);
static_assert(sizeof(XModel) == 0xE4);
static_assert(sizeof(DObjModelDescription) == 0x08);

inline constexpr std::size_t kMaximumFeedHiddenBones = 16;

struct MovingBoltSource final {
    std::uint16_t tag{};
    std::uint8_t parent_bone{0xFF};
    std::uint8_t bone{0xFF};
    const XSurface* surface{};
    std::uint8_t surface_index{0xFF};
};

struct ClipSource final {
    XModel* model{};
    std::uint8_t clip_surface{0xFF};
    std::uint8_t round_surface{0xFF};
    std::array<std::uint8_t, kMaximumFeedHiddenBones> hide_bones{};
    std::uint8_t hide_bone_count{};
    std::uint8_t root_bone{0xFF};
    std::array<MovingBoltSource, kMaximumMovingBoltTags> moving_bolts{};
    std::uint8_t moving_bolt_count{};
};

[[nodiscard]] bool is_cached_bolt_surface(
    const ClipSource& source,
    std::size_t moving_bolt_index,
    const XSurface* surface) noexcept;

struct BoltRelativePose final {
    float quaternion[4]{};
    float translation[3]{};
    float translation_weight{};
    bool valid{};
};

struct ClipAsset final {
    XModel* source{};
    XModel model{};
    std::array<XSurface, 2> surfaces{};
    std::array<std::vector<PackedVertex>, 2> vertices{};
    std::array<std::vector<std::uint16_t>, 2> blend{};
    std::vector<DObjAnimMat> base_matrices{};
    std::array<void*, 2> materials{};
    std::uint8_t clip_surface{0xFF};
    std::uint8_t round_surface{0xFF};
    alignas(16) std::array<std::uint8_t, kDObjSize> dobj{};
    alignas(16) std::array<std::uint8_t, kPoseSize> pose{};
    bool ready{};
    bool failed{};
};

struct CachedClipAsset final {
    BoltActionWeaponProfileId profile_id{BoltActionWeaponProfileId::None};
    std::uint64_t source_generation{};
    XModel* source_model{};
    XSurface* source_surfaces{};
    void** source_material_handles{};
    DObjAnimMat* source_base_matrices{};
    std::uint8_t clip_surface{0xFF};
    std::uint8_t round_surface{0xFF};
    bool occupied{};
    ClipAsset asset{};
};

struct MagazineSource final {
    XModel* model{};
    std::uint16_t bone_tag{};
    std::uint16_t insertion_anchor_tag{};
    MovingBoltSource feed_door{};
    XSurface* feed_door_surface{};
    std::uint8_t root_bone{0xFF};
    std::uint8_t local_bone{0xFF};
    std::uint8_t global_bone{0xFF};
    std::uint8_t surface{0xFF};
    std::uint8_t rigid_range{0xFF};
    std::uint8_t piece_count{};
    std::array<std::uint8_t, kMaximumDetachableMagazineMeshPieces>
        piece_surfaces{};
    std::array<std::uint8_t, kMaximumDetachableMagazineMeshPieces>
        piece_rigid_ranges{};
    std::uint64_t asset_fingerprint{};
    MovingBoltSource charging_handle{};
    XSurface* charging_surface{};
    std::uint8_t charging_piece_count{};
    std::array<XSurface*,
               kMaximumDetachableMagazineChargingMeshPieces>
        charging_surfaces{};
    std::array<std::uint8_t,
               kMaximumDetachableMagazineChargingMeshPieces>
        charging_rigid_ranges{};
};

struct InspectedMagazineMeshPiece final {
    XSurface* surface{};
    void* material{};
    gameplay::InspectedRigidSubmesh rigid{};
};

struct DetachableMagazineAmmoSnapshot final {
    bool valid{};
    std::int32_t ammo_index{-1};
    std::int32_t clip_index{-1};
    std::int32_t clip_size{};
    std::int32_t reserve{};
    std::int32_t loaded{};
};

struct BoltActionAmmoSnapshot final {
    bool valid{};
    std::uint32_t definition_address{};
    std::int32_t ammo_index{-1};
    std::int32_t clip_index{-1};
    ManualStripperClipCommitInput commit{};
};

struct MagazineAsset final {
    XModel* source{};
    XModel model{};
    std::array<XSurface, kMaximumDetachableMagazineMeshPieces> surfaces{};
    std::array<std::vector<PackedVertex>,
               kMaximumDetachableMagazineMeshPieces>
        vertices{};
    std::array<std::vector<std::uint16_t>,
               kMaximumDetachableMagazineMeshPieces>
        blend{};
    std::vector<DObjAnimMat> base_matrices{};
    std::array<void*, kMaximumDetachableMagazineMeshPieces> materials{};
    std::uint8_t piece_count{};
    alignas(16) std::array<std::uint8_t, kDObjSize> dobj{};
    alignas(16) std::array<std::uint8_t, kPoseSize> pose{};
    bool dobj_create_attempted{};
    bool ready{};
    bool failed{};
};

struct CachedMagazineAsset final {
    DetachableMagazineWeaponProfileId profile_id{
        DetachableMagazineWeaponProfileId::None};
    XModel* source_model{};
    XSurface* source_surfaces{};
    void** source_material_handles{};
    DObjAnimMat* source_base_matrices{};
    std::array<PackedVertex*, kMaximumDetachableMagazineMeshPieces>
        source_vertices{};
    std::array<std::uint16_t*, kMaximumDetachableMagazineMeshPieces>
        source_triangle_indices{};
    std::array<void*, kMaximumDetachableMagazineMeshPieces>
        source_vertex_buffers{};
    std::array<void*, kMaximumDetachableMagazineMeshPieces>
        source_index_buffers{};
    std::array<std::uint8_t, kMaximumDetachableMagazineMeshPieces>
        source_piece_surfaces{};
    std::array<std::uint8_t, kMaximumDetachableMagazineMeshPieces>
        source_rigid_ranges{};
    std::array<std::uint8_t, kMaximumDetachableMagazineMeshPieces>
        source_zone_handles{};
    std::uint8_t piece_count{};
    std::uint64_t asset_fingerprint{};
    bool occupied{};
    MagazineAsset asset{};
};

struct RuntimeMagazineVisualState final {
    bool supported{};
    DetachableMagazineWeaponProfileId profile_id{
        DetachableMagazineWeaponProfileId::None};
    std::int32_t weapon_index{};
    std::uint32_t weapon_registered_count{};
    std::uint32_t weapon_definition_address{};
    void* viewmodel_dobj{};
    MagazineSource source{};
    MagazineAsset* asset{};
    struct DObjWitness final {
        void* dobj{};
        XModel** model_array{};
        std::array<XModel*, 255> ordered_models{};
        std::uint8_t model_count{};
        std::uint8_t total_bones{};
        std::uint8_t selected_model_index{0xFF};
        const char* selected_name{};
        std::uint8_t selected_bone_count{};
        std::uint8_t selected_root_bone_count{};
        std::uint8_t selected_surface_count{};
        std::uint16_t* selected_bone_names{};
        DObjAnimMat* selected_base_matrices{};
        XSurface* selected_surfaces{};
        void** selected_material_handles{};
        bool valid{};
    } dobj_witness{};
    bool authored_hidden{};
    bool persistent_was_hidden{};
    bool live_was_hidden{};
    BoltRelativePose closed_charging_handle_relative{};
    wawvr::xr::Vec3f closed_charging_handle_grip_offset{};
    bool closed_charging_handle_pose_latched{};
    bool closed_charging_handle_grip_offset_valid{};
};

struct RuntimeVisualState final {
    bool supported{};
    BoltActionWeaponProfileId profile_id{BoltActionWeaponProfileId::None};
    std::int32_t weapon_index{};
    std::uint32_t weapon_definition_address{};
    void* viewmodel_dobj{};
    ClipSource source{};
    XSurface* source_surfaces{};
    void** source_material_handles{};
    DObjAnimMat* source_base_matrices{};
    ClipAsset* asset{};
    std::array<BoltRelativePose, kMaximumMovingBoltTags>
        closed_bolt_relatives{};
    wawvr::xr::Vec3f closed_bolt_grip_offset{};
    bool closed_bolt_pose_latched{};
    bool closed_bolt_grip_offset_valid{};
};

enum class BeginDecision : int {
    native = 0,
    suppress = 1,
    commit = 2,
    commit_detachable_magazine = 3,
};

enum class NativeCommitPhase : std::uint8_t {
    idle,
    requested,
    claimed,
    completed,
};

struct PreSkinScratch final {
    alignas(16) std::array<std::uint8_t, kDObjSize> dobj{};
    alignas(16) std::array<DObjAnimMat, 128> matrices{};
};

struct BoltActionCommitWitness final {
    std::uint64_t generation{};
    const void* player_state{};
    std::int32_t weapon_index{};
    std::uint32_t definition_address{};
    std::int32_t ammo_index{-1};
    std::int32_t clip_index{-1};
    std::int32_t clip_size{};
    std::int32_t expected_reserve{};
    std::int32_t expected_loaded{};
};

std::atomic<bool> g_installed{false};
std::atomic<bool> g_enabled{false};
std::atomic<bool> g_automatic_reload{false};
std::atomic<bool> g_supported{false};
std::atomic<bool> g_manual_active{false};
std::atomic<bool> g_cycle_lock{false};
std::atomic<bool> g_reserve_right_grip{false};
std::atomic<bool> g_reserve_left_grip{false};
std::atomic<bool> g_block_new_left_trigger_action{false};
std::atomic<bool> g_shot_pending{false};
std::atomic<std::uint64_t> g_active_weapon_binding{0};
std::atomic<std::uint64_t> g_active_magazine_binding{0};
std::atomic<std::uint32_t> g_active_weapon_definition_address{0};
// A seated magazine which still needs charging is gameplay state, not render
// state. Keep its exact identity independently published so a DObj seqlock
// window or switching away and back cannot create one fireable sample.
MagazineChamberLockTable g_magazine_chamber_locks{};
std::atomic<std::int32_t> g_last_player_command_time{-1};
std::atomic<std::int32_t> g_command_time_regression_candidate{-1};
std::atomic<std::int32_t> g_last_valid_connection_state{
    (std::numeric_limits<std::int32_t>::min)()};
// Presentation/session retirement advances this before clearing persistent
// state. A render update must carry the same epoch from its first identity
// read through the locked publication, so an old map cannot resurrect a lock
// after active -> non-active retirement.
std::atomic<std::uint64_t> g_manual_reload_session_epoch{1};
// Even values identify a complete active-source publication. A writer under
// g_state_mutex makes this odd before changing any gameplay/pre-skin identity
// and even afterward, so readers detect clear/rebind ABA even when every raw
// address and map-local weapon index is reused.
std::atomic<std::uint64_t> g_active_weapon_publication_generation{0};
std::atomic<std::uint64_t> g_local_shot_generation{0};
std::atomic<std::uint64_t> g_preskin_logged_generation{0};
std::atomic<void*> g_pre_skin_viewmodel_dobj{nullptr};
inline constexpr std::size_t kMaximumPreSkinActionSurfaces =
    kMaximumMovingBoltTags >
            kMaximumDetachableMagazineChargingMeshPieces + 1
        ? kMaximumMovingBoltTags
        : kMaximumDetachableMagazineChargingMeshPieces + 1;
std::array<std::atomic<const XSurface*>, kMaximumPreSkinActionSurfaces>
    g_pre_skin_bolt_surfaces{};
std::atomic<NativeCommitPhase> g_native_commit_phase{
    NativeCommitPhase::idle};
std::atomic<std::uint64_t> g_native_commit_publication_generation{0};
std::atomic<std::uint64_t>
    g_detachable_magazine_commit_completed_at_milliseconds{0};
std::uintptr_t g_original_begin_reload = 0;
std::uintptr_t g_original_pre_skin_surface = 0;
std::uintptr_t g_reload_clip = 0;
std::uintptr_t g_start_weapon_anim = 0;
std::uintptr_t g_dobj_create = 0;
std::uintptr_t g_add_dobj_to_scene = 0;
std::uintptr_t g_set_control_tag_angles = 0;
std::uintptr_t g_sl_find_string = 0;
std::uintptr_t g_db_find_xasset_header = 0;
std::uintptr_t g_weapon_info_array = 0;
std::uintptr_t g_viewmodel_world_origin = 0;
std::uintptr_t g_viewmodel_pose = 0;
SyntheticSceneIndexPointers g_scene_index_pointers{};
ManualReloadControllerState g_controller_state{};
Kar98BoltActionState g_bolt_state{};
MagazineChargingState g_magazine_charging_state{};
bool g_empty_reload_armed{};
std::uint64_t g_magazine_charging_last_update_milliseconds{};
RuntimeVisualState g_visual_state{};
RuntimeMagazineVisualState g_magazine_visual_state{};
// R_AddDObjToScene may leave the render backend holding pointers into a
// detached feed asset after this gameplay frame returns. Keep every built
// source recipe at a stable address for the process lifetime; switching maps
// or profiles must never destroy geometry already submitted to the renderer.
// Detached render objects must remain address-stable after submission. A deque
// keeps prior nodes alive without an arbitrary 16-source failure ceiling.
std::deque<CachedClipAsset> g_clip_asset_cache{};
std::deque<CachedMagazineAsset> g_magazine_asset_cache{};
std::uint64_t g_clip_asset_source_generation{1};
std::mutex g_state_mutex;
thread_local PreSkinScratch g_pre_skin_scratch{};
thread_local BoltActionCommitWitness g_bolt_action_commit_witness{};

void begin_active_weapon_publication_locked() noexcept {
    // All ordinary writers hold g_state_mutex, so the generation is even here.
    // Shutdown independently gates readers with g_enabled before clearing.
    g_active_weapon_publication_generation.fetch_add(
        1, std::memory_order_acq_rel);
}

void finish_active_weapon_publication_locked() noexcept {
    g_active_weapon_publication_generation.fetch_add(
        1, std::memory_order_release);
}

void advance_clip_asset_source_generation_locked() noexcept {
    ++g_clip_asset_source_generation;
    if (g_clip_asset_source_generation == 0) {
        ++g_clip_asset_source_generation;
    }
}

[[nodiscard]] bool accessible_range(
    const void* const address,
    const std::size_t size,
    const bool writable) noexcept {
    return resident_page_access(
        address, size, writable,
        &grouped_virtual_query<VirtualQueryTimingGroup::manual_reload>);
}

template <std::size_t N>
[[nodiscard]] bool bytes_equal(
    const void* const address,
    const std::array<std::uint8_t, N>& expected) noexcept {
    return accessible_range(address, expected.size(), false) &&
           std::memcmp(address, expected.data(), expected.size()) == 0;
}

[[nodiscard]] std::uintptr_t decode_rel32(const std::uint8_t* call) noexcept {
    if (call == nullptr || call[0] != 0xE8) {
        return 0;
    }
    std::int32_t displacement = 0;
    std::memcpy(&displacement, call + 1, sizeof(displacement));
    return reinterpret_cast<std::uintptr_t>(call + 5) + displacement;
}

[[nodiscard]] bool build_rel32(
    const std::uintptr_t source,
    const std::uintptr_t destination,
    std::array<std::uint8_t, 5>* const bytes) noexcept {
    if (bytes == nullptr) {
        return false;
    }
    const std::int64_t displacement =
        static_cast<std::int64_t>(destination) -
        static_cast<std::int64_t>(source + 5);
    if (displacement < (std::numeric_limits<std::int32_t>::min)() ||
        displacement > (std::numeric_limits<std::int32_t>::max)()) {
        return false;
    }
    (*bytes)[0] = 0xE8;
    const auto encoded = static_cast<std::int32_t>(displacement);
    std::memcpy(bytes->data() + 1, &encoded, sizeof(encoded));
    return true;
}

[[nodiscard]] bool context_matches(
    const std::uint8_t* const context,
    const std::uint8_t* const callsite,
    const std::span<const std::uint8_t> expected,
    const std::size_t call_offset) noexcept {
    return context != nullptr && callsite != nullptr &&
           call_offset + 5 <= expected.size() &&
           context + call_offset == callsite && callsite[0] == 0xE8 &&
           std::memcmp(context, expected.data(), call_offset) == 0 &&
           std::memcmp(
               callsite + 5, expected.data() + call_offset + 5,
               expected.size() - call_offset - 5) == 0;
}

[[nodiscard]] bool patch_call(
    std::uint8_t* const callsite,
    const std::uintptr_t destination,
    DWORD* const system_error) noexcept {
    std::array<std::uint8_t, 5> patch{};
    if (!build_rel32(
            reinterpret_cast<std::uintptr_t>(callsite), destination,
            &patch)) {
        return false;
    }
    DWORD old_protection = 0;
    if (!VirtualProtect(
            callsite, patch.size(), PAGE_EXECUTE_READWRITE,
            &old_protection)) {
        if (system_error != nullptr) {
            *system_error = GetLastError();
        }
        return false;
    }
    std::memcpy(callsite, patch.data(), patch.size());
    const bool flushed = FlushInstructionCache(
        GetCurrentProcess(), callsite, patch.size()) != FALSE;
    DWORD ignored = 0;
    const bool restored = VirtualProtect(
        callsite, patch.size(), old_protection, &ignored) != FALSE;
    if ((!flushed || !restored) && system_error != nullptr) {
        *system_error = GetLastError();
    }
    return flushed && restored &&
           decode_rel32(callsite) == destination;
}

[[nodiscard]] bool finite_vector(const wawvr::xr::Vec3f& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] float dot(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] bool valid_basis(const wawvr::xr::Basis3f& axis) noexcept {
    if (!finite_vector(axis.forward) || !finite_vector(axis.left) ||
        !finite_vector(axis.up)) {
        return false;
    }
    const float ff = dot(axis.forward, axis.forward);
    const float ll = dot(axis.left, axis.left);
    const float uu = dot(axis.up, axis.up);
    return ff >= 0.80F && ff <= 1.20F && ll >= 0.80F && ll <= 1.20F &&
           uu >= 0.80F && uu <= 1.20F &&
           std::abs(dot(axis.forward, axis.left)) <= 0.20F &&
           std::abs(dot(axis.forward, axis.up)) <= 0.20F &&
           std::abs(dot(axis.left, axis.up)) <= 0.20F;
}

[[nodiscard]] wawvr::xr::Vec3f compose(
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

[[nodiscard]] bool decompose(
    const wawvr::xr::Basis3f& basis,
    const wawvr::xr::Vec3f& world,
    wawvr::xr::Vec3f* const local) noexcept {
    if (local == nullptr || !valid_basis(basis) || !finite_vector(world)) {
        return false;
    }
    *local = {
        dot(world, basis.forward),
        dot(world, basis.left),
        dot(world, basis.up),
    };
    return finite_vector(*local);
}

[[nodiscard]] wawvr::xr::Basis3f compose_axes(
    const wawvr::xr::Basis3f& outer,
    const wawvr::xr::Basis3f& inner) noexcept {
    return {
        compose(outer, inner.forward),
        compose(outer, inner.left),
        compose(outer, inner.up),
    };
}

[[nodiscard]] bool current_hand_pose(
    const ManualReloadViewmodelContext& context,
    const wawvr::xr::Hand hand,
    wawvr::xr::EnginePose* const world,
    wawvr::xr::Vec3f* const head_local) noexcept {
    if (world == nullptr || head_local == nullptr ||
        !controller_frame_is_current(context.controller, GetTickCount64()) ||
        !valid_basis(context.camera_axis) ||
        !finite_vector(context.camera_origin)) {
        return false;
    }
    const auto& tracked = context.controller.frame.actions.hands[
        static_cast<std::uint32_t>(hand)];
    if (!tracked.grip.active || !tracked.grip.position_valid ||
        !tracked.grip.orientation_valid ||
        !finite_vector(tracked.grip.pose.position)) {
        return false;
    }
    const auto relative = wawvr::xr::OpenXrPoseToIwRelative(
        tracked.grip.pose, context.controller.tracking_anchor,
        wawvr::xr::kIwUnitsPerMeter);
    const auto relative_to_head = wawvr::xr::OpenXrPoseToIwRelative(
        tracked.grip.pose, context.controller.frame.head_center,
        wawvr::xr::kIwUnitsPerMeter);
    if (!finite_vector(relative.position) || !valid_basis(relative.axis) ||
        !finite_vector(relative_to_head.position)) {
        return false;
    }
    const wawvr::xr::Vec3f world_offset =
        compose(context.camera_axis, relative.position);
    world->position = {
        context.camera_origin.x + world_offset.x,
        context.camera_origin.y + world_offset.y,
        context.camera_origin.z + world_offset.z,
    };
    world->axis = compose_axes(context.camera_axis, relative.axis);
    *head_local = relative_to_head.position;
    return finite_vector(world->position) && valid_basis(world->axis);
}

[[nodiscard]] float distance(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    const float x = left.x - right.x;
    const float y = left.y - right.y;
    const float z = left.z - right.z;
    return std::sqrt(x * x + y * y + z * z);
}

[[nodiscard]] bool apply_local_pose(
    const wawvr::xr::EnginePose& anchor,
    const DetachableMagazineLocalPose& local,
    wawvr::xr::EnginePose* const output) noexcept {
    if (output == nullptr || !finite_vector(anchor.position) ||
        !valid_basis(anchor.axis) || !finite_vector(local.translation)) {
        return false;
    }
    const wawvr::xr::Quaternionf orientation = wawvr::xr::Normalize(
        local.orientation);
    if (!std::isfinite(orientation.x) || !std::isfinite(orientation.y) ||
        !std::isfinite(orientation.z) || !std::isfinite(orientation.w)) {
        return false;
    }
    const wawvr::xr::Basis3f local_axis{
        wawvr::xr::Rotate(orientation, {1.0F, 0.0F, 0.0F}),
        wawvr::xr::Rotate(orientation, {0.0F, 1.0F, 0.0F}),
        wawvr::xr::Rotate(orientation, {0.0F, 0.0F, 1.0F}),
    };
    const wawvr::xr::Vec3f offset = compose(
        anchor.axis, local.translation);
    output->position = {
        anchor.position.x + offset.x,
        anchor.position.y + offset.y,
        anchor.position.z + offset.z,
    };
    output->axis = compose_axes(anchor.axis, local_axis);
    return finite_vector(output->position) && valid_basis(output->axis);
}

[[nodiscard]] bool c_string_equals(
    const char* value,
    const char* expected,
    WeaponIdentitySnapshotReader* const one_traversal_memory = nullptr) noexcept {
    if (value == nullptr || expected == nullptr) {
        return false;
    }
    const std::size_t length = std::strlen(expected);
    const bool readable = one_traversal_memory != nullptr
        ? one_traversal_memory->readable(
              reinterpret_cast<std::uintptr_t>(value), length + 1)
        : accessible_range(value, length + 1, false);
    return readable &&
           std::memcmp(value, expected, length + 1) == 0;
}

// RuntimeWeaponDefinitionIdentity owns its bounded name copy. Comparing that
// local array cannot require a native-memory syscall; keep c_string_equals
// above for the genuinely native model/material pointers below.
template <std::size_t Capacity>
[[nodiscard]] constexpr bool owned_c_string_equals(
    const std::array<char, Capacity>& value,
    const std::string_view expected) noexcept {
    if (expected.size() >= Capacity || value[expected.size()] != '\0') {
        return false;
    }
    for (std::size_t index = 0; index < expected.size(); ++index) {
        if (value[index] != expected[index]) return false;
    }
    return true;
}

static_assert(owned_c_string_equals(std::array{'k', 'a', 'r', '\0'}, "kar"));
static_assert(!owned_c_string_equals(std::array{'k', 'a', 'r', '\0'}, "ka"));
static_assert(!owned_c_string_equals(std::array{'k', 'a', 'r', '\0'}, "gar"));
static_assert(!owned_c_string_equals(std::array{'k', 'a', 'r'}, "kar"));
static_assert(!owned_c_string_equals(std::array{'k', 'a', 'r', '\0'}, "kar98"));
static_assert(owned_c_string_equals(std::array{'\0'}, ""));
static_assert(!owned_c_string_equals(std::array<char, 0>{}, ""));

// Constructed only by read_manual_reload_gameplay_policy and destroyed before
// returning its flags. No native callback occurs between these policy reads.
// This is not a render/frame cache: gameplay atomics are rechecked separately
// by every original validator, and the next call starts with a fresh identity.
class GameplayWeaponIdentityRead final {
public:
    explicit GameplayWeaponIdentityRead(const std::int32_t weapon_index) noexcept
        : weapon_index_(weapon_index) {}

    [[nodiscard]] bool read(const std::int32_t weapon_index,
                            RuntimeWeaponDefinitionIdentity* output) noexcept {
        if (weapon_index != weapon_index_ || output == nullptr) return false;
        // Match the old independent policy calls if a publisher advances
        // between helpers: discard that receipt and make one fresh read.
        // Never retry inside a read; a racing publication still fails closed.
        if (attempted_ &&
            (session_epoch_ != g_manual_reload_session_epoch.load(std::memory_order_acquire) ||
             publication_generation_ !=
                 g_active_weapon_publication_generation.load(std::memory_order_acquire))) {
            attempted_ = false;
            valid_ = false;
        }
        if (!attempted_) {
            attempted_ = true;
            session_epoch_ = g_manual_reload_session_epoch.load(std::memory_order_acquire);
            publication_generation_ =
                g_active_weapon_publication_generation.load(std::memory_order_acquire);
            valid_ = read_runtime_sp_weapon_definition(weapon_index_, &identity_);
        }
        if (!valid_ ||
            session_epoch_ != g_manual_reload_session_epoch.load(std::memory_order_acquire) ||
            publication_generation_ !=
                g_active_weapon_publication_generation.load(std::memory_order_acquire)) {
            return false;
        }
        *output = identity_;
        return true;
    }

private:
    std::int32_t weapon_index_{};
    bool attempted_{};
    bool valid_{};
    std::uint64_t session_epoch_{};
    std::uint64_t publication_generation_{};
    RuntimeWeaponDefinitionIdentity identity_{};
};

[[nodiscard]] bool read_gameplay_weapon_identity(
    const std::int32_t weapon_index,
    RuntimeWeaponDefinitionIdentity* const output,
    GameplayWeaponIdentityRead* const one_call_identity) noexcept {
    return one_call_identity != nullptr
        ? one_call_identity->read(weapon_index, output)
        : read_runtime_sp_weapon_definition(weapon_index, output);
}

[[nodiscard]] const DetachableMagazineWeaponProfile*
detachable_magazine_profile_for_viewmodel_dobj(
    const char* const internal_weapon_name,
    void* const viewmodel_dobj) noexcept {
    if (internal_weapon_name == nullptr ||
        !accessible_range(viewmodel_dobj, kDObjSize, false)) {
        return nullptr;
    }
    auto* const bytes = static_cast<std::uint8_t*>(viewmodel_dobj);
    std::uint8_t model_count = 0;
    XModel** models = nullptr;
    std::memcpy(
        &model_count, bytes + kDObjNumModelsOffset, sizeof(model_count));
    std::memcpy(&models, bytes + kDObjModelsOffset, sizeof(models));
    if (model_count == 0 || models == nullptr ||
        !accessible_range(
            models, static_cast<std::size_t>(model_count) * sizeof(*models),
            false)) {
        return nullptr;
    }

    const DetachableMagazineWeaponProfile* match = nullptr;
    for (std::size_t index = 0; index < model_count; ++index) {
        XModel* model = nullptr;
        std::memcpy(&model, models + index, sizeof(model));
        if (model == nullptr ||
            !accessible_range(model, sizeof(*model), false) ||
            model->name == nullptr) {
            return nullptr;
        }
        const DetachableMagazineWeaponProfile* const candidate =
            find_detachable_magazine_weapon_profile_by_identity(
                std::string_view{internal_weapon_name},
                std::string_view{model->name});
        if (candidate != nullptr) {
            if (match != nullptr && match != candidate) {
                return nullptr;
            }
            match = candidate;
        }
    }
    return match;
}

[[nodiscard]] std::uint64_t append_asset_fingerprint(
    std::uint64_t hash,
    const void* const bytes,
    const std::size_t size) noexcept {
    if (bytes == nullptr && size != 0) {
        return 0;
    }
    constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
    const auto* const data = static_cast<const std::uint8_t*>(bytes);
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= data[index];
        hash *= kFnvPrime;
    }
    return hash;
}

[[nodiscard]] const BoltActionWeaponProfile* profile_for_binding(
    const std::uint64_t packed,
    BoltActionWeaponBinding* const binding) noexcept {
    if (binding == nullptr || packed == 0) {
        return nullptr;
    }
    *binding = unpack_bolt_action_weapon_binding(packed);
    const BoltActionWeaponProfile* const profile =
        find_bolt_action_weapon_profile(binding->profile_id);
    return profile != nullptr && bolt_action_weapon_binding_matches(
        packed, profile->id, binding->weapon_index)
        ? profile
        : nullptr;
}

[[nodiscard]] const DetachableMagazineWeaponProfile*
profile_for_magazine_binding(
    const std::uint64_t packed,
    DetachableMagazineWeaponBinding* const binding) noexcept {
    if (binding == nullptr || packed == 0) {
        return nullptr;
    }
    *binding = unpack_detachable_magazine_weapon_binding(packed);
    const DetachableMagazineWeaponProfile* const profile =
        find_detachable_magazine_weapon_profile(binding->profile_id);
    return profile != nullptr && detachable_magazine_weapon_binding_matches(
        packed, profile->id, binding->weapon_index)
        ? profile
        : nullptr;
}

void publish_magazine_chamber_lock_locked(
    const DetachableMagazineWeaponProfile& profile,
    const std::int32_t weapon_index,
    const std::uint32_t definition_address) noexcept {
    if (!g_enabled.load(std::memory_order_acquire)) {
        return;
    }
    const DetachableMagazineWeaponBinding binding{
        .profile_id = profile.id,
        .weapon_index = weapon_index,
    };
    const MagazineChamberLockPublishResult result =
        g_magazine_chamber_locks.publish(binding, definition_address);
    if (result == MagazineChamberLockPublishResult::Conflict) {
        WAWVR_STEREO_DIAG_ONCE(
            "ReloadDiag refused to overwrite a conflicting persistent magazine-action lock in weapon slot %d",
            weapon_index);
    }
    // Loader-lock shutdown cannot wait on the render mutex. If it disabled the
    // runtime between the precheck and this atomic publication, retire this
    // exact record immediately so the process-lifetime table remains
    // fail-closed throughout teardown.
    if (!g_enabled.load(std::memory_order_acquire) &&
        (result == MagazineChamberLockPublishResult::Published ||
         result == MagazineChamberLockPublishResult::AlreadyPresent)) {
        static_cast<void>(g_magazine_chamber_locks.clear_matching(
            binding, definition_address));
    }
}

void clear_matching_magazine_chamber_lock_locked(
    const DetachableMagazineWeaponProfile& profile,
    const std::int32_t weapon_index,
    const std::uint32_t definition_address) noexcept {
    static_cast<void>(g_magazine_chamber_locks.clear_matching(
        {.profile_id = profile.id, .weapon_index = weapon_index},
        definition_address));
}

[[nodiscard]] bool magazine_chamber_lock_matches_identity(
    const DetachableMagazineWeaponProfile& profile,
    const std::int32_t weapon_index,
    const std::uint32_t definition_address) noexcept {
    return g_magazine_chamber_locks.matches(
        {.profile_id = profile.id, .weapon_index = weapon_index},
        definition_address);
}

[[nodiscard]] bool magazine_chamber_lock_matches_gameplay_weapon(
    const std::int32_t weapon_index,
    GameplayWeaponIdentityRead* const one_call_identity = nullptr) noexcept {
    if (!g_enabled.load(std::memory_order_acquire) || weapon_index <= 0) {
        return false;
    }
    MagazineChamberLockSnapshot lock{};
    if (!g_magazine_chamber_locks.snapshot(weapon_index, &lock)) {
        return false;
    }
    const DetachableMagazineWeaponBinding binding = lock.binding;
    const DetachableMagazineWeaponProfile* const profile =
        find_detachable_magazine_weapon_profile(binding.profile_id);
    RuntimeWeaponDefinitionIdentity identity{};
    if (profile == nullptr || binding.weapon_index != weapon_index ||
        lock.definition_address == 0 ||
        !read_gameplay_weapon_identity(weapon_index, &identity, one_call_identity) ||
        identity.definition_address != lock.definition_address ||
        !owned_c_string_equals(identity.name, profile->internal_weapon_name)) {
        // An occupied but stale/conflicting slot is safer as an attack lock
        // than as a silently fireable weapon. Session retirement clears all
        // slots once map/connection identity changes.
        WAWVR_STEREO_DIAG_ONCE(
            "ReloadDiag persistent magazine-action slot %d failed exact live identity revalidation; attack remains blocked",
            weapon_index);
        return g_enabled.load(std::memory_order_acquire);
    }
    return g_magazine_chamber_locks.matches(
               binding, lock.definition_address) &&
        g_enabled.load(std::memory_order_acquire);
}

[[nodiscard]] BeginDecision classify_reload_begin_fallback(
    const std::int32_t gameplay_weapon_index) noexcept {
    return classify_magazine_reload_begin_fallback(
               magazine_chamber_lock_matches_gameplay_weapon(
                   gameplay_weapon_index)) ==
            MagazineReloadBeginFallbackDecision::Suppress
        ? BeginDecision::suppress
        : BeginDecision::native;
}

[[nodiscard]] bool magazine_chamber_lock_reserves_hand(
    const std::int32_t weapon_index,
    const MagazineChargingHand hand,
    GameplayWeaponIdentityRead* const one_call_identity = nullptr) noexcept {
    if (!magazine_chamber_lock_matches_gameplay_weapon(weapon_index, one_call_identity)) {
        return false;
    }
    MagazineChamberLockSnapshot lock{};
    if (!g_magazine_chamber_locks.snapshot(weapon_index, &lock)) {
        return true;
    }
    const DetachableMagazineWeaponProfile* const profile =
        find_detachable_magazine_weapon_profile(lock.binding.profile_id);
    if (profile != nullptr && profile->charging.enabled) {
        // A persistent chamber requirement no longer preselects a hand. The
        // player may keep either hand on the weapon, then the first free hand
        // whose index trigger grabs the exact handle becomes the sole
        // reserved manipulator. En-bloc actions remain automatic.
        return false;
    }
    static_cast<void>(hand);
    return true;
}

// Every gameplay-side policy revalidates the owned WeaponDef snapshot instead
// of trusting a render-published index/address pair. This closes both a stale
// command window during weapon switches and same-slot/same-address ABA reuse.
[[nodiscard]] const BoltActionWeaponProfile*
revalidate_active_gameplay_weapon(
    const std::int32_t weapon_index,
    const std::uint32_t observed_definition_address = 0,
    std::uint64_t* const validated_generation = nullptr,
    GameplayWeaponIdentityRead* const one_call_identity = nullptr) noexcept {
    if (validated_generation != nullptr) {
        *validated_generation = 0;
    }
    const std::uint64_t generation =
        g_active_weapon_publication_generation.load(
            std::memory_order_acquire);
    if (!g_enabled.load(std::memory_order_acquire) || weapon_index <= 0 ||
        (generation & 1U) != 0) {
        return nullptr;
    }
    const std::uint64_t packed =
        g_active_weapon_binding.load(std::memory_order_acquire);
    const std::uint32_t published_definition =
        g_active_weapon_definition_address.load(std::memory_order_acquire);
    BoltActionWeaponBinding binding{};
    const BoltActionWeaponProfile* const profile =
        profile_for_binding(packed, &binding);
    RuntimeWeaponDefinitionIdentity identity{};
    if (profile == nullptr || published_definition == 0 ||
        (observed_definition_address != 0 &&
         observed_definition_address != published_definition) ||
        !bolt_action_weapon_binding_matches(
            packed, profile->id, weapon_index) ||
        !read_gameplay_weapon_identity(weapon_index, &identity, one_call_identity) ||
        identity.weapon_index != weapon_index ||
        identity.definition_address != published_definition ||
        !owned_c_string_equals(
            identity.name, profile->internal_weapon_name) ||
        packed !=
            g_active_weapon_binding.load(std::memory_order_acquire) ||
        published_definition !=
            g_active_weapon_definition_address.load(
                std::memory_order_acquire) ||
        generation !=
            g_active_weapon_publication_generation.load(
                std::memory_order_acquire) ||
        !g_enabled.load(std::memory_order_acquire)) {
        return nullptr;
    }
    if (validated_generation != nullptr) {
        *validated_generation = generation;
    }
    return profile;
}

[[nodiscard]] const DetachableMagazineWeaponProfile*
revalidate_active_magazine_gameplay_weapon(
    const std::int32_t weapon_index,
    const std::uint32_t observed_definition_address = 0,
    std::uint64_t* const validated_generation = nullptr,
    GameplayWeaponIdentityRead* const one_call_identity = nullptr) noexcept {
    if (validated_generation != nullptr) {
        *validated_generation = 0;
    }
    const std::uint64_t generation =
        g_active_weapon_publication_generation.load(
            std::memory_order_acquire);
    if (!g_enabled.load(std::memory_order_acquire) || weapon_index <= 0 ||
        (generation & 1U) != 0) {
        return nullptr;
    }
    const std::uint64_t packed =
        g_active_magazine_binding.load(std::memory_order_acquire);
    const std::uint32_t published_definition =
        g_active_weapon_definition_address.load(std::memory_order_acquire);
    DetachableMagazineWeaponBinding binding{};
    const DetachableMagazineWeaponProfile* const profile =
        profile_for_magazine_binding(packed, &binding);
    RuntimeWeaponDefinitionIdentity identity{};
    if (profile == nullptr || published_definition == 0 ||
        (observed_definition_address != 0 &&
         observed_definition_address != published_definition) ||
        !detachable_magazine_weapon_binding_matches(
            packed, profile->id, weapon_index) ||
        !read_gameplay_weapon_identity(weapon_index, &identity, one_call_identity) ||
        identity.weapon_index != weapon_index ||
        identity.definition_address != published_definition ||
        !owned_c_string_equals(
            identity.name, profile->internal_weapon_name) ||
        packed !=
            g_active_magazine_binding.load(std::memory_order_acquire) ||
        published_definition !=
            g_active_weapon_definition_address.load(
                std::memory_order_acquire) ||
        generation !=
            g_active_weapon_publication_generation.load(
                std::memory_order_acquire) ||
        !g_enabled.load(std::memory_order_acquire)) {
        return nullptr;
    }
    if (validated_generation != nullptr) {
        *validated_generation = generation;
    }
    return profile;
}

[[nodiscard]] bool revalidate_active_manual_gameplay_weapon(
    const std::int32_t weapon_index,
    const std::uint32_t observed_definition_address = 0,
    std::uint64_t* const validated_generation = nullptr,
    GameplayWeaponIdentityRead* const one_call_identity = nullptr) noexcept {
    std::uint64_t bolt_generation = 0;
    if (revalidate_active_gameplay_weapon(
            weapon_index, observed_definition_address,
            &bolt_generation, one_call_identity) != nullptr) {
        if (validated_generation != nullptr) {
            *validated_generation = bolt_generation;
        }
        return true;
    }
    return revalidate_active_magazine_gameplay_weapon(
               weapon_index, observed_definition_address,
               validated_generation, one_call_identity) != nullptr;
}

[[nodiscard]] bool publish_native_commit_request_locked() noexcept {
    const std::uint64_t generation =
        g_active_weapon_publication_generation.load(
            std::memory_order_acquire);
    if (generation == 0 || (generation & 1U) != 0 ||
        g_native_commit_phase.load(std::memory_order_acquire) !=
            NativeCommitPhase::idle) {
        return false;
    }
    // The render thread is the sole request publisher under g_state_mutex.
    // Publish the token first so a command thread acquiring `requested` can
    // never observe an unkeyed handoff.
    g_native_commit_publication_generation.store(
        generation, std::memory_order_release);
    g_detachable_magazine_commit_completed_at_milliseconds.store(
        0, std::memory_order_release);
    NativeCommitPhase idle = NativeCommitPhase::idle;
    if (g_native_commit_phase.compare_exchange_strong(
            idle, NativeCommitPhase::requested,
            std::memory_order_release, std::memory_order_acquire)) {
        return true;
    }
    g_native_commit_publication_generation.store(
        0, std::memory_order_release);
    return false;
}

[[nodiscard]] bool cancel_native_commit_request_locked() noexcept {
    NativeCommitPhase requested = NativeCommitPhase::requested;
    if (g_native_commit_phase.compare_exchange_strong(
            requested, NativeCommitPhase::idle,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
        g_native_commit_publication_generation.store(
            0, std::memory_order_release);
        g_detachable_magazine_commit_completed_at_milliseconds.store(
            0, std::memory_order_release);
        return true;
    }
    // `requested` now contains the phase observed by the failed CAS. Idle is
    // also a proven pre-claim cancellation; claimed/completed is irrevocable.
    return requested == NativeCommitPhase::idle;
}

[[nodiscard]] bool consume_native_commit_completion_locked() noexcept {
    const std::uint64_t generation =
        g_active_weapon_publication_generation.load(
            std::memory_order_acquire);
    const std::uint64_t commit_generation =
        g_native_commit_publication_generation.load(
            std::memory_order_acquire);
    if (generation == 0 || (generation & 1U) != 0 ||
        commit_generation != generation) {
        NativeCommitPhase phase = g_native_commit_phase.load(
            std::memory_order_acquire);
        if (phase == NativeCommitPhase::requested ||
            phase == NativeCommitPhase::completed) {
            if (g_native_commit_phase.compare_exchange_strong(
                    phase, NativeCommitPhase::idle,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                g_native_commit_publication_generation.store(
                    0, std::memory_order_release);
                g_detachable_magazine_commit_completed_at_milliseconds.store(
                    0, std::memory_order_release);
            }
        }
        return false;
    }
    NativeCommitPhase completed = NativeCommitPhase::completed;
    if (!g_native_commit_phase.compare_exchange_strong(
            completed, NativeCommitPhase::idle,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
        return false;
    }
    g_native_commit_publication_generation.store(
        0, std::memory_order_release);
    g_detachable_magazine_commit_completed_at_milliseconds.store(
        0, std::memory_order_release);
    return true;
}

[[nodiscard]] bool
consume_detachable_magazine_commit_completion_locked() noexcept {
    if (g_native_commit_phase.load(std::memory_order_acquire) !=
        NativeCommitPhase::completed) {
        return false;
    }
    const std::uint64_t completed_at =
        g_detachable_magazine_commit_completed_at_milliseconds.load(
            std::memory_order_acquire);
    const std::uint64_t now = GetTickCount64();
    if (completed_at == 0 || now < completed_at ||
        now - completed_at <
            kDetachableMagazineCommitFanoutMilliseconds) {
        return false;
    }
    return consume_native_commit_completion_locked();
}

[[nodiscard]] bool read_material_name(
    void* material,
    const char** const name) noexcept {
    if (name == nullptr || material == nullptr ||
        !accessible_range(material, sizeof(const char*), false)) {
        return false;
    }
    std::memcpy(name, material, sizeof(*name));
    return *name != nullptr;
}

#if defined(_MSC_VER) && defined(_M_IX86)
extern "C" __declspec(naked) std::uint32_t __cdecl find_script_string(
    const char*) noexcept {
    __asm {
        mov edx, dword ptr [esp + 4]
        push 0
        call dword ptr [g_sl_find_string]
        lea esp, [esp + 4]
        ret
    }
}

extern "C" __declspec(naked) void __cdecl call_dobj_create(
    DObjModelDescription*, std::uint32_t, void*, void*,
    std::uint32_t) noexcept {
    __asm {
        push esi
        push edi
        mov eax, dword ptr [esp + 0Ch]
        mov ecx, dword ptr [esp + 10h]
        mov edi, dword ptr [esp + 14h]
        mov esi, dword ptr [esp + 18h]
        mov edx, dword ptr [esp + 1Ch]
        push edx
        push ecx
        push eax
        call dword ptr [g_dobj_create]
        lea esp, [esp + 0Ch]
        pop edi
        pop esi
        ret
    }
}

extern "C" __declspec(naked) void __cdecl call_add_dobj_to_scene(
    void*, void*, std::uint32_t, float*, std::uint32_t) noexcept {
    __asm {
        push esi
        push edi
        mov eax, dword ptr [esp + 0Ch]
        mov esi, dword ptr [esp + 10h]
        mov edi, dword ptr [esp + 14h]
        mov edx, dword ptr [esp + 18h]
        mov ecx, dword ptr [esp + 1Ch]
        xorps xmm1, xmm1
        xorps xmm2, xmm2
        xorps xmm3, xmm3
        push edx
        push edi
        push esi
        call dword ptr [g_add_dobj_to_scene]
        lea esp, [esp + 0Ch]
        pop edi
        pop esi
        ret
    }
}

extern "C" __declspec(naked) void __cdecl set_control_tag_angles(
    void*, const std::uint32_t*, std::uint32_t, const float*) noexcept {
    __asm {
        mov eax, dword ptr [esp + 0Ch]
        push dword ptr [esp + 10h]
        push dword ptr [esp + 0Ch]
        push dword ptr [esp + 0Ch]
        call dword ptr [g_set_control_tag_angles]
        lea esp, [esp + 0Ch]
        ret
    }
}
#else
std::uint32_t find_script_string(const char*) noexcept { return 0; }
void call_dobj_create(
    DObjModelDescription*, std::uint32_t, void*, void*,
    std::uint32_t) noexcept {}
void call_add_dobj_to_scene(
    void*, void*, std::uint32_t, float*, std::uint32_t) noexcept {}
void set_control_tag_angles(
    void*, const std::uint32_t*, std::uint32_t, const float*) noexcept {}
#endif

[[nodiscard]] int find_model_bone(
    const XModel& model,
    const std::uint16_t name) noexcept {
    if (name == 0 || model.bone_count == 0 || model.bone_names == nullptr ||
        !accessible_range(
            model.bone_names,
            static_cast<std::size_t>(model.bone_count) * sizeof(name),
            false)) {
        return -1;
    }
    for (int index = 0; index < model.bone_count; ++index) {
        if (model.bone_names[index] == name) {
            return index;
        }
    }
    return -1;
}

[[nodiscard]] int model_bone_parent(
    const XModel& model,
    const int bone) noexcept {
    if (bone < static_cast<int>(model.root_bone_count) ||
        bone >= static_cast<int>(model.bone_count) ||
        model.parent_list == nullptr ||
        !accessible_range(
            model.parent_list,
            static_cast<std::size_t>(
                model.bone_count - model.root_bone_count),
            false)) {
        return -1;
    }
    const int offset =
        model.parent_list[bone - model.root_bone_count];
    return offset > 0 && offset <= bone ? bone - offset : -1;
}

[[nodiscard]] bool simulator_mosin_inventory_enabled() noexcept {
    wchar_t flag[8]{};
    const DWORD flag_length = GetEnvironmentVariableW(
        L"WAWVR_SIMULATOR_EQUIP_MOSIN", flag,
        static_cast<DWORD>(std::size(flag)));
    if (flag_length != 1 || flag[0] != L'1') {
        return false;
    }
    wchar_t runtime[1024]{};
    const DWORD runtime_length = GetEnvironmentVariableW(
        L"XR_RUNTIME_JSON", runtime,
        static_cast<DWORD>(std::size(runtime)));
    return runtime_length != 0 && runtime_length < std::size(runtime) &&
        std::wcsstr(runtime, L"openxr_simulator") != nullptr;
}

[[nodiscard]] bool simulator_m1carbine_inventory_enabled() noexcept {
    wchar_t headset_flag[8]{};
    const DWORD headset_flag_length = GetEnvironmentVariableW(
        L"WAWVR_HEADSET_TEST_EQUIP_M1CARBINE", headset_flag,
        static_cast<DWORD>(std::size(headset_flag)));
    if (headset_flag_length == 1 && headset_flag[0] == L'1') {
        return true;
    }
    wchar_t generic_weapon[32]{};
    const DWORD generic_length = GetEnvironmentVariableW(
        L"WAWVR_SIMULATOR_EQUIP_MAGAZINE_WEAPON", generic_weapon,
        static_cast<DWORD>(std::size(generic_weapon)));
    wchar_t flag[8]{};
    const DWORD flag_length = GetEnvironmentVariableW(
        L"WAWVR_SIMULATOR_EQUIP_M1CARBINE", flag,
        static_cast<DWORD>(std::size(flag)));
    if ((flag_length != 1 || flag[0] != L'1') &&
        (generic_length == 0 || generic_length >= std::size(generic_weapon) ||
         std::wcscmp(generic_weapon, L"m1carbine") != 0)) {
        return false;
    }
    wchar_t runtime[1024]{};
    const DWORD runtime_length = GetEnvironmentVariableW(
        L"XR_RUNTIME_JSON", runtime,
        static_cast<DWORD>(std::size(runtime)));
    return runtime_length != 0 && runtime_length < std::size(runtime) &&
        std::wcsstr(runtime, L"openxr_simulator") != nullptr;
}

[[nodiscard]] bool simulator_detachable_magazine_probe_enabled() noexcept {
    // Diagnostic-only: leaves map, loadgame, weapon and ammo selection alone.
    wchar_t diagnostic_flag[8]{};
    const DWORD diagnostic_length = GetEnvironmentVariableW(
        L"WAWVR_SIMULATOR_MAGAZINE_DIAGNOSTICS", diagnostic_flag,
        static_cast<DWORD>(std::size(diagnostic_flag)));
    const bool diagnostic_only =
        diagnostic_length == 1 && diagnostic_flag[0] == L'1';
    wchar_t headset_flag[8]{};
    const DWORD headset_flag_length = GetEnvironmentVariableW(
        L"WAWVR_HEADSET_TEST_EQUIP_M1CARBINE", headset_flag,
        static_cast<DWORD>(std::size(headset_flag)));
    if (headset_flag_length == 1 && headset_flag[0] == L'1') {
        return true;
    }
    wchar_t garand_gl_headset_flag[8]{};
    const DWORD garand_gl_headset_flag_length = GetEnvironmentVariableW(
        L"WAWVR_HEADSET_TEST_EQUIP_M1GARAND_GL",
        garand_gl_headset_flag,
        static_cast<DWORD>(std::size(garand_gl_headset_flag)));
    if (garand_gl_headset_flag_length == 1 &&
        garand_gl_headset_flag[0] == L'1') {
        return true;
    }
    wchar_t upgraded_headset_flag[8]{};
    const DWORD upgraded_headset_flag_length = GetEnvironmentVariableW(
        L"WAWVR_HEADSET_TEST_EQUIP_UPGRADED_M1CARBINE",
        upgraded_headset_flag,
        static_cast<DWORD>(std::size(upgraded_headset_flag)));
    if (upgraded_headset_flag_length == 1 &&
        upgraded_headset_flag[0] == L'1') {
        return true;
    }
    wchar_t headset_weapon[64]{};
    const DWORD headset_weapon_length = GetEnvironmentVariableW(
        L"WAWVR_HEADSET_TEST_EQUIP_ZOMBIE_MAGAZINE_WEAPON",
        headset_weapon,
        static_cast<DWORD>(std::size(headset_weapon)));
    if (headset_weapon_length != 0 &&
        headset_weapon_length < std::size(headset_weapon)) {
        return true;
    }
    wchar_t colt_flag[8]{};
    const DWORD colt_flag_length = GetEnvironmentVariableW(
        L"WAWVR_SIMULATOR_PROBE_COLT", colt_flag,
        static_cast<DWORD>(std::size(colt_flag)));
    wchar_t m1carbine_flag[8]{};
    const DWORD m1carbine_flag_length = GetEnvironmentVariableW(
        L"WAWVR_SIMULATOR_EQUIP_M1CARBINE", m1carbine_flag,
        static_cast<DWORD>(std::size(m1carbine_flag)));
    wchar_t generic_weapon[32]{};
    const DWORD generic_length = GetEnvironmentVariableW(
        L"WAWVR_SIMULATOR_EQUIP_MAGAZINE_WEAPON", generic_weapon,
        static_cast<DWORD>(std::size(generic_weapon)));
    if (!diagnostic_only &&
        (colt_flag_length != 1 || colt_flag[0] != L'1') &&
        (m1carbine_flag_length != 1 || m1carbine_flag[0] != L'1') &&
        (generic_length == 0 ||
         generic_length >= std::size(generic_weapon))) {
        return false;
    }
    wchar_t runtime[1024]{};
    const DWORD runtime_length = GetEnvironmentVariableW(
        L"XR_RUNTIME_JSON", runtime,
        static_cast<DWORD>(std::size(runtime)));
    return runtime_length != 0 && runtime_length < std::size(runtime) &&
        std::wcsstr(runtime, L"openxr_simulator") != nullptr;
}

[[nodiscard]] bool simulator_requested_magazine_weapon_matches(
    const char* const internal_weapon_name) noexcept {
    if (internal_weapon_name == nullptr ||
        !simulator_detachable_magazine_probe_enabled()) {
        return false;
    }
    wchar_t upgraded_headset_flag[8]{};
    const DWORD upgraded_headset_flag_length = GetEnvironmentVariableW(
        L"WAWVR_HEADSET_TEST_EQUIP_UPGRADED_M1CARBINE",
        upgraded_headset_flag,
        static_cast<DWORD>(std::size(upgraded_headset_flag)));
    if (upgraded_headset_flag_length == 1 &&
        upgraded_headset_flag[0] == L'1') {
        return std::strcmp(
                   internal_weapon_name,
                   "zombie_m1carbine_upgraded") == 0;
    }
    wchar_t garand_gl_headset_flag[8]{};
    const DWORD garand_gl_headset_flag_length = GetEnvironmentVariableW(
        L"WAWVR_HEADSET_TEST_EQUIP_M1GARAND_GL",
        garand_gl_headset_flag,
        static_cast<DWORD>(std::size(garand_gl_headset_flag)));
    if (garand_gl_headset_flag_length == 1 &&
        garand_gl_headset_flag[0] == L'1') {
        return std::strcmp(internal_weapon_name, "m1garand_gl") == 0;
    }
    wchar_t headset_requested[64]{};
    const DWORD headset_length = GetEnvironmentVariableW(
        L"WAWVR_HEADSET_TEST_EQUIP_ZOMBIE_MAGAZINE_WEAPON",
        headset_requested,
        static_cast<DWORD>(std::size(headset_requested)));
    if (headset_length != 0 &&
        headset_length < std::size(headset_requested)) {
        if (std::strlen(internal_weapon_name) != headset_length) {
            return false;
        }
        for (DWORD index = 0; index < headset_length; ++index) {
            if (headset_requested[index] != static_cast<wchar_t>(
                    static_cast<unsigned char>(
                        internal_weapon_name[index]))) {
                return false;
            }
        }
        return true;
    }
    wchar_t requested[32]{};
    const DWORD length = GetEnvironmentVariableW(
        L"WAWVR_SIMULATOR_EQUIP_MAGAZINE_WEAPON", requested,
        static_cast<DWORD>(std::size(requested)));
    if (length == 0 || length >= std::size(requested)) {
        return false;
    }
    if (std::wcscmp(requested, L"zombie_magazine_inventory") == 0 ||
        std::wcscmp(requested, L"zombie_magazine_profile_sweep") == 0) {
        return std::strncmp(internal_weapon_name, "zombie_", 7) == 0;
    }
    if (std::strlen(internal_weapon_name) != length) {
        return false;
    }
    for (DWORD index = 0; index < length; ++index) {
        if (requested[index] != static_cast<wchar_t>(
                static_cast<unsigned char>(internal_weapon_name[index]))) {
            return false;
        }
    }
    return true;
}

// A bounded simulator or explicit headset-test inventory lets an unsupported
// weapon be decoded from the exact live DObj without making it active in the
// shipping registry. Normal processes without a test flag cannot enter it.
void diagnose_simulator_weapon_viewmodel(
    void* const dobj,
    const RuntimeWeaponDefinitionIdentity& identity,
    const bool permit_multiple_inventory_dumps = false) noexcept {
    static std::atomic_flag gate = ATOMIC_FLAG_INIT;
    const bool mosin = simulator_mosin_inventory_enabled();
    const bool m1carbine = simulator_m1carbine_inventory_enabled();
    const bool generic_magazine =
        simulator_requested_magazine_weapon_matches(identity.name.data());
    const char* const inventory_name = generic_magazine
        ? "MagazineWeapon"
        : m1carbine ? "M1Carbine" : "Mosin";
    if ((!mosin && !m1carbine && !generic_magazine) ||
        !accessible_range(dobj, kDObjSize, false)) {
        return;
    }
    auto* const bytes = static_cast<std::uint8_t*>(dobj);
    std::uint8_t model_count = 0;
    std::uint8_t total_bones = 0;
    XModel** models = nullptr;
    std::memcpy(&model_count, bytes + kDObjNumModelsOffset, sizeof(model_count));
    std::memcpy(&total_bones, bytes + kDObjNumBonesOffset, sizeof(total_bones));
    std::memcpy(&models, bytes + kDObjModelsOffset, sizeof(models));
    if (model_count == 0 || models == nullptr ||
        !accessible_range(
            models, static_cast<std::size_t>(model_count) * sizeof(*models),
            false)) {
        return;
    }
    // Do not consume the process-local one-shot on an early frame whose DObj
    // or model table is still being published. Once that immutable table is
    // readable, exactly one thread owns the bounded inventory dump.
    if (!permit_multiple_inventory_dumps &&
        gate.test_and_set(std::memory_order_relaxed)) {
        return;
    }
    stereo_diagnostic_log(
        "%sInventory DObj models=%u totalBones=%u", inventory_name,
        static_cast<unsigned>(model_count),
        static_cast<unsigned>(total_bones));
    const auto* const definition = reinterpret_cast<const std::uint8_t*>(
        static_cast<std::uintptr_t>(identity.definition_address));
    if (identity.definition_address != 0 &&
        accessible_range(
            definition, kWeaponDefinitionManualReloadSpan, false)) {
        std::uint32_t world_clip_model = 0;
        std::int32_t ammo_index = -1;
        std::int32_t clip_index = -1;
        std::int32_t clip_size = 0;
        std::int32_t reload_ammo_add = 0;
        std::int32_t reload_start_add = 0;
        std::int32_t bolt_action = 0;
        std::int32_t segmented_reload = 0;
        std::memcpy(
            &world_clip_model,
            definition + kWeaponDefinitionWorldClipModelOffset,
            sizeof(world_clip_model));
        std::memcpy(
            &ammo_index, definition + kWeaponDefinitionAmmoIndexOffset,
            sizeof(ammo_index));
        std::memcpy(
            &clip_index, definition + kWeaponDefinitionClipIndexOffset,
            sizeof(clip_index));
        std::memcpy(
            &clip_size, definition + kWeaponDefinitionClipSizeOffset,
            sizeof(clip_size));
        std::memcpy(
            &reload_ammo_add,
            definition + kWeaponDefinitionReloadAmmoAddOffset,
            sizeof(reload_ammo_add));
        std::memcpy(
            &reload_start_add,
            definition + kWeaponDefinitionReloadStartAddOffset,
            sizeof(reload_start_add));
        std::memcpy(
            &bolt_action, definition + kWeaponDefinitionBoltActionOffset,
            sizeof(bolt_action));
        std::memcpy(
            &segmented_reload,
            definition + kWeaponDefinitionSegmentedReloadOffset,
            sizeof(segmented_reload));
        stereo_diagnostic_log(
            "%sInventory weapon=%s definition=%08X worldClipModel=%08X ammoIndex=%d clipIndex=%d clipSize=%d reloadAmmoAdd=%d reloadStartAdd=%d boltAction=%d segmentedReload=%d",
            inventory_name, identity.name.data(),
            identity.definition_address, world_clip_model,
            ammo_index, clip_index, clip_size,
            reload_ammo_add, reload_start_add, bolt_action, segmented_reload);
    }
    for (std::size_t model_index = 0; model_index < model_count;
         ++model_index) {
        XModel* model = nullptr;
        std::memcpy(&model, models + model_index, sizeof(model));
        if (model == nullptr ||
            !accessible_range(model, sizeof(*model), false)) {
            stereo_diagnostic_log(
                "%sInventory model[%zu] unreadable", inventory_name,
                model_index);
            continue;
        }
        const char* const model_name = model->name != nullptr
            ? model->name : "<null>";
        stereo_diagnostic_log(
            "%sInventory model[%zu]=%s bones=%u roots=%u surfaces=%u",
            inventory_name, model_index, model_name,
            static_cast<unsigned>(model->bone_count),
            static_cast<unsigned>(model->root_bone_count),
            static_cast<unsigned>(model->surface_count));
        const bool selected_model = generic_magazine
            ? model->name != nullptr &&
                std::strncmp(model->name, "viewmodel_", 10) == 0 &&
                std::strstr(model->name, "player") == nullptr
            : mosin
            ? c_string_equals(
                  model->name, "viewmodel_rus_mosinnagant_rifle")
            : model->name != nullptr &&
                std::strstr(model->name, "m1carbine") != nullptr;
        if (!selected_model) {
            continue;
        }
        constexpr const char* kProbeBones[] = {
            "j_gun", "j_bolt", "j_bolt1", "j_clip", "tag_clip",
            "j_mag", "j_magazine", "tag_magazine", "tag_brass",
            "tag_flash", "tag_round1", "tag_round2", "tag_stripper",
        };
        for (const char* const bone_name : kProbeBones) {
            const std::uint32_t script_name = find_script_string(bone_name);
            const int bone = script_name <= 0xFFFF
                ? find_model_bone(
                      *model, static_cast<std::uint16_t>(script_name))
                : -1;
            const int parent = bone >= 0 ? model_bone_parent(*model, bone) : -1;
            if (bone >= 0 && model->base_matrices != nullptr &&
                accessible_range(
                    model->base_matrices,
                    static_cast<std::size_t>(model->bone_count) *
                        sizeof(DObjAnimMat),
                    false)) {
                const DObjAnimMat& bind = model->base_matrices[bone];
                stereo_diagnostic_log(
                    "%sInventory bone=%s local=%d parent=%d bind=(%.6f,%.6f,%.6f | %.6f,%.6f,%.6f,%.6f)",
                    inventory_name, bone_name, bone, parent,
                    bind.translation[0],
                    bind.translation[1], bind.translation[2],
                    bind.quaternion[0], bind.quaternion[1],
                    bind.quaternion[2], bind.quaternion[3]);
            } else {
                stereo_diagnostic_log(
                    "%sInventory bone=%s local=%d parent=%d bind=unavailable",
                    inventory_name, bone_name, bone, parent);
            }
        }
        if (model->surface_count == 0 || model->surfaces == nullptr ||
            model->material_handles == nullptr ||
            !accessible_range(
                model->surfaces,
                static_cast<std::size_t>(model->surface_count) *
                    sizeof(XSurface),
                false) ||
            !accessible_range(
                model->material_handles,
                static_cast<std::size_t>(model->surface_count) *
                    sizeof(void*),
                false)) {
            stereo_diagnostic_log(
                "%sInventory selected model surface table unreadable",
                inventory_name);
            continue;
        }
        for (std::size_t surface_index = 0;
             surface_index < model->surface_count; ++surface_index) {
            void* material = nullptr;
            std::memcpy(
                &material, model->material_handles + surface_index,
                sizeof(material));
            const char* material_name = nullptr;
            if (!read_material_name(material, &material_name)) {
                material_name = "<unreadable>";
            }
            const XSurface& surface = model->surfaces[surface_index];
            const std::size_t triangle_index_count =
                static_cast<std::size_t>(surface.triangle_count) * 3U;
            const bool vertices_readable = surface.vertices != nullptr &&
                surface.vertex_count != 0 &&
                accessible_range(
                    surface.vertices,
                    static_cast<std::size_t>(surface.vertex_count) *
                        sizeof(PackedVertex),
                    false);
            const bool triangle_indices_readable =
                surface.triangle_indices != nullptr &&
                triangle_index_count != 0 &&
                accessible_range(
                    surface.triangle_indices,
                    triangle_index_count * sizeof(std::uint16_t), false);
            const bool vertex_buffer_readable =
                surface.vertex_buffer != nullptr &&
                accessible_range(
                    surface.vertex_buffer, sizeof(void*), false);
            const bool index_buffer_readable =
                surface.index_buffer != nullptr &&
                accessible_range(
                    surface.index_buffer, sizeof(void*), false);
            constexpr std::size_t kMaximumInventoryRigidRanges = 16;
            const bool rigid_lists_readable =
                surface.rigid_vertex_list != nullptr &&
                surface.rigid_vertex_list_count != 0 &&
                surface.rigid_vertex_list_count <=
                    kMaximumInventoryRigidRanges &&
                accessible_range(
                    surface.rigid_vertex_list,
                    static_cast<std::size_t>(
                        surface.rigid_vertex_list_count) *
                        sizeof(RigidVertexList),
                    false);
            const auto* const part_bits =
                reinterpret_cast<const std::uint32_t*>(surface.part_bits);
            stereo_diagnostic_log(
                "%sInventory surface[%zu] material=%s vertices=%u triangles=%u deformed=%u rigidLists=%u baseVertex=%u baseTriangle=%u pointers(vertices=%d triangleIndices=%d vertexBuffer=%d indexBuffer=%d rigidLists=%d) partBits=(%08X,%08X,%08X,%08X)",
                inventory_name, surface_index, material_name,
                static_cast<unsigned>(surface.vertex_count),
                static_cast<unsigned>(surface.triangle_count),
                static_cast<unsigned>(surface.deformed),
                static_cast<unsigned>(surface.rigid_vertex_list_count),
                static_cast<unsigned>(surface.base_vertex_index),
                static_cast<unsigned>(surface.base_triangle_index),
                vertices_readable ? 1 : 0,
                triangle_indices_readable ? 1 : 0,
                vertex_buffer_readable ? 1 : 0,
                index_buffer_readable ? 1 : 0,
                rigid_lists_readable ? 1 : 0,
                static_cast<unsigned>(part_bits[0]),
                static_cast<unsigned>(part_bits[1]),
                static_cast<unsigned>(part_bits[2]),
                static_cast<unsigned>(part_bits[3]));
            if (!rigid_lists_readable) {
                continue;
            }

            std::array<gameplay::RigidSubmeshRange,
                       kMaximumInventoryRigidRanges>
                ranges{};
            for (std::size_t rigid_index = 0;
                 rigid_index < surface.rigid_vertex_list_count;
                 ++rigid_index) {
                const RigidVertexList& rigid =
                    surface.rigid_vertex_list[rigid_index];
                ranges[rigid_index] = {
                    .bone_id = static_cast<std::uint16_t>(
                        rigid.bone_offset >> 6U),
                    .vertex_count = rigid.vertex_count,
                    .triangle_offset = rigid.triangle_offset,
                    .triangle_count = rigid.triangle_count,
                };
            }

            std::uint32_t first_vertex = 0;
            for (std::size_t rigid_index = 0;
                 rigid_index < surface.rigid_vertex_list_count;
                 ++rigid_index) {
                const RigidVertexList& rigid =
                    surface.rigid_vertex_list[rigid_index];
                const bool bone_offset_aligned =
                    (rigid.bone_offset & 0x3FU) == 0;
                if (triangle_indices_readable) {
                    const auto inspection =
                        gameplay::inspect_exact_rigid_submesh(
                            surface.vertex_count, surface.triangle_count,
                            std::span<const gameplay::RigidSubmeshRange>{
                                ranges.data(),
                                surface.rigid_vertex_list_count},
                            rigid_index,
                            static_cast<std::uint16_t>(
                                rigid.bone_offset >> 6U),
                            std::span<const std::uint16_t>{
                                surface.triangle_indices,
                                triangle_index_count});
                    if (inspection.has_value()) {
                        stereo_diagnostic_log(
                            "%sInventory surface[%zu].rigid[%zu] rawBoneOffset=0x%04X aligned=%d bone=%u firstVertex=%u vertices=%u triangleOffset=%u triangles=%u inspect=pass inspectedFirstVertex=%u inspectedTriangleOffset=%u",
                            inventory_name, surface_index, rigid_index,
                            static_cast<unsigned>(rigid.bone_offset),
                            bone_offset_aligned ? 1 : 0,
                            static_cast<unsigned>(rigid.bone_offset >> 6U),
                            static_cast<unsigned>(first_vertex),
                            static_cast<unsigned>(rigid.vertex_count),
                            static_cast<unsigned>(rigid.triangle_offset),
                            static_cast<unsigned>(rigid.triangle_count),
                            static_cast<unsigned>(inspection->first_vertex),
                            static_cast<unsigned>(
                                inspection->triangle_offset));
                    } else {
                        stereo_diagnostic_log(
                            "%sInventory surface[%zu].rigid[%zu] rawBoneOffset=0x%04X aligned=%d bone=%u firstVertex=%u vertices=%u triangleOffset=%u triangles=%u inspect=fail",
                            inventory_name, surface_index, rigid_index,
                            static_cast<unsigned>(rigid.bone_offset),
                            bone_offset_aligned ? 1 : 0,
                            static_cast<unsigned>(rigid.bone_offset >> 6U),
                            static_cast<unsigned>(first_vertex),
                            static_cast<unsigned>(rigid.vertex_count),
                            static_cast<unsigned>(rigid.triangle_offset),
                            static_cast<unsigned>(rigid.triangle_count));
                    }
                } else {
                    stereo_diagnostic_log(
                        "%sInventory surface[%zu].rigid[%zu] rawBoneOffset=0x%04X aligned=%d bone=%u firstVertex=%u vertices=%u triangleOffset=%u triangles=%u inspect=unavailable",
                        inventory_name, surface_index, rigid_index,
                        static_cast<unsigned>(rigid.bone_offset),
                        bone_offset_aligned ? 1 : 0,
                        static_cast<unsigned>(rigid.bone_offset >> 6U),
                        static_cast<unsigned>(first_vertex),
                        static_cast<unsigned>(rigid.vertex_count),
                        static_cast<unsigned>(rigid.triangle_offset),
                        static_cast<unsigned>(rigid.triangle_count));
                }
                first_vertex += rigid.vertex_count;
            }
        }
    }
}

#if defined(_MSC_VER) && defined(_M_IX86)
[[nodiscard]] bool discover_detachable_magazine_source(
    void* dobj,
    const DetachableMagazineWeaponProfile& profile,
    MagazineSource* output) noexcept;

struct SimulatorMagazineAssetInventoryRequest final {
    const wchar_t* selector;
    const char* internal_weapon_name;
    const char* expected_viewmodel_name;
};

// The aggregate selector inventories every conventional detachable-magazine
// WeaponDef packaged by Der Riese in a single simulator-only database pass.
// It is diagnostic-only: no weapon is granted, selected, or manipulated.
static constexpr SimulatorMagazineAssetInventoryRequest
    kSimulatorMagazineAssetInventoryRequests[] = {
        {L"m1garand", "m1garand", "viewmodel_usa_m1garand_rifle"},
        {L"m1garand_bayonet", "m1garand_bayonet",
         "viewmodel_usa_m1garand_rifle_bayonet"},
        {L"m1garand_gl", "m1garand_gl",
         "viewmodel_usa_m1garand_rifle_grenade_mount"},
        {L"mosin_rifle_scoped", "mosin_rifle_scoped",
         "viewmodel_rus_mosinnagant_scoped_rifle"},
        {L"type99_lmg", "type99_lmg", "viewmodel_jap_type99_lmg"},
        {L"dp28", "dp28", "viewmodel_rus_dp28_lmg"},
        {L"fg42", "fg42", "viewmodel_ger_fg42_lmg"},
        {L"bar_bipod", "bar_bipod", "viewmodel_usa_bar_bipod_lmg"},
        {L"dp28_bipod", "dp28_bipod", "viewmodel_rus_dp28_bipod_lmg"},
        {L"fg42_bipod", "fg42_bipod", "viewmodel_ger_fg42_bipod_lmg"},
        {L"fg42_scoped", "fg42_scoped", "viewmodel_ger_fg42_scoped_lmg"},
        {L"m1carbine_bayonet", "m1carbine_bayonet",
         "viewmodel_usa_m1carbine_rifle_bayonet"},
        {L"type99_lmg_bipod", "type99_lmg_bipod",
         "viewmodel_jap_type99_bipod_lmg"},
        {L"type100_smg_nosound", "type100_smg_nosound",
         "viewmodel_jap_type100_smg"},
        {L"thompson_wet", "thompson_wet",
         "viewmodel_usa_thompson_smg_wet"},
        {L"colt_wet", "colt_wet",
         "viewmodel_usa_colt45_pistol_wet"},
        {L"zombie_magazine_inventory", "zombie_colt",
         "viewmodel_zombie_colt45_pistol"},
        {L"zombie_magazine_inventory", "zombie_colt_upgraded",
         "viewmodel_zombie_colt45_pistol_up"},
        {L"zombie_magazine_inventory", "zombie_m1carbine",
         "viewmodel_zombie_m1carbine_rifle"},
        {L"zombie_magazine_inventory", "zombie_m1carbine_upgraded",
         "viewmodel_zombie_m1carbine_rifle_up"},
        {L"zombie_magazine_inventory", "zombie_gewehr43",
         "viewmodel_zombie_g43_rifle"},
        {L"zombie_magazine_inventory", "zombie_gewehr43_upgraded",
         "viewmodel_zombie_g43_rifle_up"},
        {L"zombie_magazine_inventory", "zombie_stg44",
         "viewmodel_zombie_mp44_lmg"},
        {L"zombie_magazine_inventory", "zombie_stg44_upgraded",
         "viewmodel_zombie_mp44_lmg_up"},
        {L"zombie_magazine_inventory", "zombie_thompson",
         "viewmodel_zombie_thompson_smg"},
        {L"zombie_magazine_inventory", "zombie_thompson_upgraded",
         "viewmodel_zombie_thompson_smg_up"},
        {L"zombie_magazine_inventory", "zombie_mp40",
         "viewmodel_zombie_mp40_smg"},
        {L"zombie_magazine_inventory", "zombie_mp40_upgraded",
         "viewmodel_zombie_mp40_smg_up"},
        {L"zombie_magazine_inventory", "zombie_type100_smg",
         "viewmodel_zombie_type100_smg"},
        {L"zombie_magazine_inventory", "zombie_type100_smg_upgraded",
         "viewmodel_zombie_type100_smg_up"},
        {L"zombie_magazine_inventory", "zombie_bar",
         "viewmodel_zombie_bar_lmg"},
        {L"zombie_magazine_inventory", "zombie_bar_upgraded",
         "viewmodel_zombie_bar_lmg_up"},
        {L"zombie_magazine_inventory", "zombie_fg42",
         "viewmodel_zombie_fg42_lmg"},
        {L"zombie_magazine_inventory", "zombie_fg42_upgraded",
         "viewmodel_zombie_fg42_lmg_up"},
        {L"zombie_magazine_inventory", "zombie_ppsh",
         "viewmodel_zombie_ppsh_smg"},
        {L"zombie_magazine_inventory", "zombie_ppsh_upgraded",
         "viewmodel_zombie_ppsh_smg_up"},
    };

[[nodiscard]] std::span<const SimulatorMagazineAssetInventoryRequest>
simulator_magazine_asset_inventory_requests() noexcept {
    wchar_t requested[64]{};
    const DWORD length = GetEnvironmentVariableW(
        L"WAWVR_SIMULATOR_EQUIP_MAGAZINE_WEAPON", requested,
        static_cast<DWORD>(std::size(requested)));
    if (length == 0 || length >= std::size(requested)) {
        return {};
    }
    std::size_t first = std::size(kSimulatorMagazineAssetInventoryRequests);
    std::size_t count = 0;
    const bool zombie_profile_sweep =
        std::wcscmp(requested, L"zombie_magazine_profile_sweep") == 0;
    for (std::size_t index = 0;
         index < std::size(kSimulatorMagazineAssetInventoryRequests);
         ++index) {
        const bool request_matches =
            std::wcscmp(
                requested,
                kSimulatorMagazineAssetInventoryRequests[index].selector) ==
                0 ||
            (zombie_profile_sweep &&
             std::wcscmp(
                 kSimulatorMagazineAssetInventoryRequests[index].selector,
                 L"zombie_magazine_inventory") == 0);
        if (request_matches) {
            if (count == 0) {
                first = index;
            } else if (index != first + count) {
                return {};
            }
            ++count;
        }
    }
    if (count == 0) {
        return {};
    }
    return {kSimulatorMagazineAssetInventoryRequests + first, count};
}

[[nodiscard]] bool exact_inventory_model_topology_readable(
    const XModel& model) noexcept {
    if (model.bone_count == 0 || model.surface_count == 0 ||
        model.root_bone_count > model.bone_count ||
        model.bone_names == nullptr || model.base_matrices == nullptr ||
        model.surfaces == nullptr || model.material_handles == nullptr ||
        !accessible_range(
            model.bone_names,
            static_cast<std::size_t>(model.bone_count) *
                sizeof(*model.bone_names),
            false) ||
        !accessible_range(
            model.base_matrices,
            static_cast<std::size_t>(model.bone_count) *
                sizeof(*model.base_matrices),
            false) ||
        !accessible_range(
            model.surfaces,
            static_cast<std::size_t>(model.surface_count) *
                sizeof(*model.surfaces),
            false) ||
        !accessible_range(
            model.material_handles,
            static_cast<std::size_t>(model.surface_count) *
                sizeof(*model.material_handles),
            false)) {
        return false;
    }
    const std::size_t child_bone_count =
        static_cast<std::size_t>(model.bone_count - model.root_bone_count);
    if (child_bone_count != 0 &&
        (model.parent_list == nullptr ||
         !accessible_range(model.parent_list, child_bone_count, false))) {
        return false;
    }

    bool geometry_seen = false;
    constexpr std::uint32_t kMaximumInventoryRigidRanges = 64;
    for (std::size_t surface_index = 0;
         surface_index < model.surface_count; ++surface_index) {
        const XSurface& surface = model.surfaces[surface_index];
        if (surface.vertex_count == 0 || surface.triangle_count == 0) {
            if (surface.vertex_count != 0 || surface.triangle_count != 0) {
                return false;
            }
            continue;
        }
        const std::size_t triangle_index_count =
            static_cast<std::size_t>(surface.triangle_count) * 3U;
        if (surface.vertices == nullptr ||
            surface.triangle_indices == nullptr ||
            !accessible_range(
                surface.vertices,
                static_cast<std::size_t>(surface.vertex_count) *
                    sizeof(*surface.vertices),
                false) ||
            !accessible_range(
                surface.triangle_indices,
                triangle_index_count * sizeof(*surface.triangle_indices),
                false) ||
            surface.rigid_vertex_list_count >
                kMaximumInventoryRigidRanges ||
            (surface.rigid_vertex_list_count != 0 &&
             (surface.rigid_vertex_list == nullptr ||
              !accessible_range(
                  surface.rigid_vertex_list,
                  static_cast<std::size_t>(
                      surface.rigid_vertex_list_count) *
                      sizeof(*surface.rigid_vertex_list),
                  false)))) {
            return false;
        }
        geometry_seen = true;
    }
    return geometry_seen;
}
#endif

void diagnose_simulator_requested_magazine_asset_from_database() noexcept {
    if (!simulator_requested_magazine_asset_inventory_ready()) {
        return;
    }
#if !defined(_MSC_VER) || !defined(_M_IX86)
    return;
#else
    const std::span<const SimulatorMagazineAssetInventoryRequest> requests =
        simulator_magazine_asset_inventory_requests();
    if (requests.empty()) {
        WAWVR_STEREO_DIAG_ONCE(
            "MagazineWeaponInventoryDB rejected selector without an exact retail weapon/viewmodel mapping");
        return;
    }
    if (g_db_find_xasset_header == 0) {
        WAWVR_STEREO_DIAG_ONCE(
            "MagazineWeaponInventoryDB request=%s rejected because DB_FindXAssetHeader is unavailable",
            requests.front().internal_weapon_name);
        return;
    }

    // The database lookup is deliberately a process-local one-shot. Claim it
    // only after the simulator/map/selector/address gates are all satisfied,
    // immediately before entering the engine database service.
    static std::atomic_flag db_inventory_claimed = ATOMIC_FLAG_INIT;
    if (db_inventory_claimed.test_and_set(std::memory_order_acq_rel)) {
        return;
    }
    const auto db_find = reinterpret_cast<DbFindXAssetHeaderFunction>(
        g_db_find_xasset_header);
    for (const SimulatorMagazineAssetInventoryRequest& request : requests) {
        void* const header = db_find(
            kWeaponAssetType, request.internal_weapon_name, false, 0);
        if (header == nullptr ||
            !accessible_range(
                header, kWeaponDefinitionManualReloadSpan, false)) {
            stereo_diagnostic_log(
                "MagazineWeaponInventoryDB request=%s rejected missing-or-unreadable WeaponDef",
                request.internal_weapon_name);
            continue;
        }

        const auto* const definition =
            static_cast<const std::uint8_t*>(header);
        std::uint32_t internal_name_address = 0;
        std::uint32_t viewmodel_address = 0;
        std::memcpy(
            &internal_name_address, definition,
            sizeof(internal_name_address));
        std::memcpy(
            &viewmodel_address,
            definition + kWeaponDefinitionGunXModel0Offset,
            sizeof(viewmodel_address));
        const auto* const internal_name = reinterpret_cast<const char*>(
            static_cast<std::uintptr_t>(internal_name_address));
        auto* const model = reinterpret_cast<XModel*>(
            static_cast<std::uintptr_t>(viewmodel_address));
        if (!c_string_equals(internal_name, request.internal_weapon_name)) {
            stereo_diagnostic_log(
                "MagazineWeaponInventoryDB request=%s rejected non-exact WeaponDef identity",
                request.internal_weapon_name);
            continue;
        }
        if (model == nullptr ||
            !accessible_range(model, sizeof(*model), false) ||
            !c_string_equals(model->name, request.expected_viewmodel_name)) {
            stereo_diagnostic_log(
                "MagazineWeaponInventoryDB request=%s rejected non-exact viewmodel expected=%s actual=%s",
                request.internal_weapon_name,
                request.expected_viewmodel_name,
                model != nullptr &&
                        accessible_range(model, sizeof(*model), false) &&
                        model->name != nullptr
                    ? model->name
                    : "<unavailable>");
            continue;
        }
        if (!exact_inventory_model_topology_readable(*model)) {
            stereo_diagnostic_log(
                "MagazineWeaponInventoryDB request=%s model=%s rejected unreadable-or-empty topology",
                request.internal_weapon_name,
                request.expected_viewmodel_name);
            continue;
        }

        RuntimeWeaponDefinitionIdentity identity{};
        identity.weapon_index = -1;
        identity.registered_count = 1;
        identity.definition_address = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(header));
        const std::size_t identity_length =
            std::strlen(request.internal_weapon_name);
        if (identity_length >= identity.name.size()) {
            stereo_diagnostic_log(
                "MagazineWeaponInventoryDB request rejected overlong exact identity");
            continue;
        }
        std::memcpy(
            identity.name.data(), request.internal_weapon_name,
            identity_length + 1U);

        alignas(16) std::array<std::uint8_t, kDObjSize> synthetic_dobj{};
        XModel* model_slot = model;
        XModel** model_table = &model_slot;
        synthetic_dobj[kDObjNumModelsOffset] = 1;
        synthetic_dobj[kDObjNumBonesOffset] = model->bone_count;
        std::memcpy(
            synthetic_dobj.data() + kDObjModelsOffset,
            &model_table, sizeof(model_table));

        const DetachableMagazineWeaponProfile* const profile =
            find_detachable_magazine_weapon_profile_by_identity(
                request.internal_weapon_name,
                request.expected_viewmodel_name);
        if (profile != nullptr) {
            std::uint32_t world_clip_model = 0;
            std::int32_t clip_size = 0;
            std::memcpy(
                &world_clip_model,
                definition + kWeaponDefinitionWorldClipModelOffset,
                sizeof(world_clip_model));
            std::memcpy(
                &clip_size,
                definition + kWeaponDefinitionClipSizeOffset,
                sizeof(clip_size));
            MagazineSource validated_source{};
            if (world_clip_model != 0 ||
                clip_size != profile->expected_clip_size ||
                !discover_detachable_magazine_source(
                    synthetic_dobj.data(), *profile,
                    &validated_source)) {
                stereo_diagnostic_log(
                    "MagazineWeaponProfileDB request=%s rejected exact profile=%u clip=%d expected=%d worldClip=%08X",
                    request.internal_weapon_name,
                    static_cast<unsigned>(profile->id), clip_size,
                    profile->expected_clip_size, world_clip_model);
            } else {
                stereo_diagnostic_log(
                    "MagazineWeaponProfileDB request=%s accepted exact profile=%u magazine-pieces=%u charging-pieces=%u",
                    request.internal_weapon_name,
                    static_cast<unsigned>(profile->id),
                    static_cast<unsigned>(validated_source.piece_count),
                    static_cast<unsigned>(
                        validated_source.charging_piece_count));
            }
        }
        stereo_diagnostic_log(
            "MagazineWeaponInventoryDB request=%s accepted definition=%08X model=%s bones=%u surfaces=%u",
            request.internal_weapon_name, identity.definition_address,
            request.expected_viewmodel_name,
            static_cast<unsigned>(model->bone_count),
            static_cast<unsigned>(model->surface_count));
        diagnose_simulator_weapon_viewmodel(
            synthetic_dobj.data(), identity, requests.size() > 1);
    }
#endif
}

[[nodiscard]] bool read_dobj_model_slot(
    void* const dobj,
    XModel*** const slot,
    XModel** const model) noexcept {
    if (slot == nullptr || model == nullptr ||
        !accessible_range(dobj, kDObjSize, false)) {
        return false;
    }
    auto* const bytes = static_cast<std::uint8_t*>(dobj);
    std::uint8_t count = 0;
    XModel** models = nullptr;
    std::memcpy(&count, bytes + kDObjNumModelsOffset, sizeof(count));
    std::memcpy(&models, bytes + kDObjModelsOffset, sizeof(models));
    if (count == 0 || models == nullptr ||
        !accessible_range(models, sizeof(*models), true)) {
        return false;
    }
    XModel* current = nullptr;
    std::memcpy(&current, models, sizeof(current));
    if (current == nullptr ||
        !accessible_range(current, sizeof(*current), false)) {
        return false;
    }
    *slot = models;
    *model = current;
    return true;
}

[[nodiscard]] bool discover_bolt_action_source(
    void* const dobj,
    const BoltActionWeaponProfile& profile,
    ClipSource* const output) noexcept {
    if (output == nullptr || !accessible_range(dobj, kDObjSize, false)) {
        return false;
    }
    *output = {};
    auto* const bytes = static_cast<std::uint8_t*>(dobj);
    std::uint8_t model_count = 0;
    std::uint8_t total_bones = 0;
    XModel** models = nullptr;
    std::memcpy(&model_count, bytes + kDObjNumModelsOffset, sizeof(model_count));
    std::memcpy(&total_bones, bytes + kDObjNumBonesOffset, sizeof(total_bones));
    std::memcpy(&models, bytes + kDObjModelsOffset, sizeof(models));
    if (model_count < 2 || models == nullptr ||
        !accessible_range(
            models, static_cast<std::size_t>(model_count) * sizeof(*models),
            false)) {
        return false;
    }

    std::size_t preceding_bones = 0;
    XModel* selected = nullptr;
    for (std::size_t index = 0; index < model_count; ++index) {
        XModel* candidate = nullptr;
        std::memcpy(&candidate, models + index, sizeof(candidate));
        if (candidate == nullptr ||
            !accessible_range(candidate, sizeof(*candidate), false)) {
            return false;
        }
        if (c_string_equals(
                candidate->name, profile.viewmodel_model_name)) {
            selected = candidate;
            break;
        }
        preceding_bones += candidate->bone_count;
    }
    if (selected == nullptr || selected->bone_count == 0 ||
        preceding_bones >= total_bones || preceding_bones >= 0xFE ||
        selected->surface_count == 0 ||
        selected->surfaces == nullptr ||
        selected->material_handles == nullptr ||
        !accessible_range(
            selected->surfaces,
            static_cast<std::size_t>(selected->surface_count) *
                sizeof(XSurface),
            false) ||
        !accessible_range(
            selected->material_handles,
            static_cast<std::size_t>(selected->surface_count) *
                sizeof(void*),
            false)) {
        return false;
    }

    if (profile.moving_bolt_tag_count == 0 ||
        profile.moving_bolt_tag_count > kMaximumMovingBoltTags) {
        return false;
    }
    std::array<MovingBoltSource, kMaximumMovingBoltTags> moving_bolts{};
    for (std::size_t index = 0;
         index < profile.moving_bolt_tag_count; ++index) {
        const std::uint32_t tag =
            find_script_string(profile.moving_bolt_tag_names[index]);
        const int local = tag <= 0xFFFF
            ? find_model_bone(*selected, static_cast<std::uint16_t>(tag))
            : -1;
        const int parent = local >= 0
            ? model_bone_parent(*selected, local) : -1;
        if (tag == 0 || tag > 0xFFFF || local < 0 || parent < 0 ||
            preceding_bones + static_cast<std::size_t>(local) >=
                total_bones ||
            preceding_bones + static_cast<std::size_t>(local) >= 0xFE ||
            preceding_bones + static_cast<std::size_t>(parent) >=
                total_bones ||
            preceding_bones + static_cast<std::size_t>(parent) >= 0xFE) {
            return false;
        }
        moving_bolts[index] = {
            .tag = static_cast<std::uint16_t>(tag),
            .parent_bone = static_cast<std::uint8_t>(
                preceding_bones + static_cast<std::size_t>(parent)),
            .bone = static_cast<std::uint8_t>(
                preceding_bones + static_cast<std::size_t>(local)),
        };
    }

    std::array<std::uint8_t,
               kMaximumExpectedMultiRigidRoundBoneTags>
        expected_multi_rigid_round_bones{};
    for (std::size_t index = 0;
         index < profile.expected_multi_rigid_round_bone_tag_count;
         ++index) {
        const std::uint32_t tag = find_script_string(
            profile.expected_multi_rigid_round_bone_tag_names[index]);
        const int local = tag <= 0xFFFF
            ? find_model_bone(*selected, static_cast<std::uint16_t>(tag))
            : -1;
        if (local < 0 || local >= selected->bone_count ||
            preceding_bones + static_cast<std::size_t>(local) >=
                total_bones ||
            preceding_bones + static_cast<std::size_t>(local) >= 128) {
            return false;
        }
        expected_multi_rigid_round_bones[index] =
            static_cast<std::uint8_t>(local);
    }

    int feed_device_local_bone = -1;
    if (profile.feed_device_bone_tag_name != nullptr) {
        const std::uint32_t tag =
            find_script_string(profile.feed_device_bone_tag_name);
        feed_device_local_bone = tag <= 0xFFFF
            ? find_model_bone(*selected, static_cast<std::uint16_t>(tag))
            : -1;
        if (tag == 0 || tag > 0xFFFF || feed_device_local_bone < 0 ||
            feed_device_local_bone >= selected->bone_count ||
            preceding_bones +
                    static_cast<std::size_t>(feed_device_local_bone) >=
                total_bones ||
            preceding_bones +
                    static_cast<std::size_t>(feed_device_local_bone) >=
                128) {
            return false;
        }
    }

    struct SurfaceMatch final {
        std::uint8_t surface{0xFF};
        std::array<std::uint8_t, kMaximumFeedHiddenBones> local_bones{};
        std::uint8_t local_bone_count{};
        wawvr::xr::Vec3f center{};
    };
    const auto inspect = [&](const std::uint8_t surface_index,
                             const char* const material_name,
                             const bool allow_multiple_rigid_lists,
                             SurfaceMatch* const match) noexcept {
        if (match == nullptr || surface_index >= selected->surface_count) {
            return false;
        }
        void* material = nullptr;
        std::memcpy(
            &material, selected->material_handles + surface_index,
            sizeof(material));
        const char* name = nullptr;
        const XSurface& surface = selected->surfaces[surface_index];
        if (!read_material_name(material, &name) ||
            !c_string_equals(name, material_name) || surface.deformed ||
            surface.vertex_count == 0 || surface.vertices == nullptr ||
            surface.rigid_vertex_list_count == 0 ||
            (!allow_multiple_rigid_lists &&
             surface.rigid_vertex_list_count != 1) ||
            surface.rigid_vertex_list_count >
                kMaximumFeedHiddenBones ||
            surface.rigid_vertex_list == nullptr ||
            !accessible_range(
                surface.vertices,
                static_cast<std::size_t>(surface.vertex_count) *
                    sizeof(PackedVertex),
                false) ||
            !accessible_range(
                surface.rigid_vertex_list,
                static_cast<std::size_t>(
                    surface.rigid_vertex_list_count) *
                    sizeof(RigidVertexList),
                false)) {
            return false;
        }
        std::size_t rigid_vertex_count = 0;
        for (std::size_t rigid_index = 0;
             rigid_index < surface.rigid_vertex_list_count;
             ++rigid_index) {
            const RigidVertexList& rigid =
                surface.rigid_vertex_list[rigid_index];
            const std::size_t local_bone = rigid.bone_offset >> 6U;
            if ((rigid.bone_offset & 0x3FU) != 0 ||
                local_bone >= selected->bone_count ||
                rigid.vertex_count == 0 ||
                rigid_vertex_count + rigid.vertex_count >
                    surface.vertex_count ||
                preceding_bones + local_bone >= total_bones ||
                preceding_bones + local_bone >= 128) {
                return false;
            }
            for (std::size_t peer = 0; peer < rigid_index; ++peer) {
                if (match->local_bones[peer] == local_bone) {
                    return false;
                }
            }
            match->local_bones[rigid_index] =
                static_cast<std::uint8_t>(local_bone);
            rigid_vertex_count += rigid.vertex_count;
        }
        if (rigid_vertex_count != surface.vertex_count) {
            return false;
        }
        float minimums[3] = {
            (std::numeric_limits<float>::max)(),
            (std::numeric_limits<float>::max)(),
            (std::numeric_limits<float>::max)()};
        float maximums[3] = {
            (std::numeric_limits<float>::lowest)(),
            (std::numeric_limits<float>::lowest)(),
            (std::numeric_limits<float>::lowest)()};
        for (std::uint16_t vertex = 0; vertex < surface.vertex_count;
             ++vertex) {
            for (int component = 0; component < 3; ++component) {
                const float value = surface.vertices[vertex].xyz[component];
                if (!std::isfinite(value)) {
                    return false;
                }
                minimums[component] =
                    (std::min)(minimums[component], value);
                maximums[component] =
                    (std::max)(maximums[component], value);
            }
        }
        match->surface = surface_index;
        match->local_bone_count = static_cast<std::uint8_t>(
            surface.rigid_vertex_list_count);
        match->center = {
            0.5F * (minimums[0] + maximums[0]),
            0.5F * (minimums[1] + maximums[1]),
            0.5F * (minimums[2] + maximums[2]),
        };
        return true;
    };

    SurfaceMatch clip{};
    std::array<SurfaceMatch, kMaximumAuthoredFeedRoundSurfaces> rounds{};
    std::size_t clip_match_count = 0;
    std::size_t round_match_count = 0;
    std::size_t multi_rigid_round_match_count = 0;
    const auto exact_expected_multi_rigid_bones =
        [&](const SurfaceMatch& match) noexcept {
            if (match.local_bone_count !=
                profile.expected_multi_rigid_round_bone_tag_count) {
                return false;
            }
            for (std::size_t expected = 0;
                 expected <
                     profile.expected_multi_rigid_round_bone_tag_count;
                 ++expected) {
                bool found = false;
                for (std::size_t actual = 0;
                     actual < match.local_bone_count; ++actual) {
                    found = found ||
                        match.local_bones[actual] ==
                            expected_multi_rigid_round_bones[expected];
                }
                if (!found) {
                    return false;
                }
            }
            return true;
        };
    for (std::size_t surface = 0; surface < selected->surface_count;
         ++surface) {
        void* material = nullptr;
        std::memcpy(
            &material, selected->material_handles + surface,
            sizeof(material));
        const char* material_name = nullptr;
        const bool material_readable =
            read_material_name(material, &material_name);
        const bool exact_clip_material = material_readable &&
            c_string_equals(
                material_name, profile.feed_device_material_name);
        const bool exact_round_material = material_readable &&
            c_string_equals(material_name, profile.round_material_name);
        SurfaceMatch clip_candidate{};
        if (exact_clip_material) {
            if (!inspect(
                    static_cast<std::uint8_t>(surface),
                    profile.feed_device_material_name,
                    false,
                    &clip_candidate)) {
                WAWVR_STEREO_DIAG_ONCE(
                    "ReloadDiag %s exact feed-device material occurred on a malformed surface",
                    profile.diagnostic_name);
                return false;
            }
            if (clip_match_count == 0) {
                clip = clip_candidate;
            }
            ++clip_match_count;
        }
        SurfaceMatch round{};
        if (exact_round_material) {
            if (!inspect(
                    static_cast<std::uint8_t>(surface),
                    profile.round_material_name,
                    profile.allow_multi_rigid_round_surfaces,
                    &round)) {
                WAWVR_STEREO_DIAG_ONCE(
                    "ReloadDiag %s exact round material occurred on a malformed surface",
                    profile.diagnostic_name);
                return false;
            }
            if (round.local_bone_count > 1) {
                if (!exact_expected_multi_rigid_bones(round)) {
                    WAWVR_STEREO_DIAG_ONCE(
                        "ReloadDiag %s multi-rigid round surface rejected: authored bone set does not match the exact profile recipe",
                        profile.diagnostic_name);
                    return false;
                }
                ++multi_rigid_round_match_count;
            }
            if (round_match_count < rounds.size()) {
                rounds[round_match_count] = round;
            }
            ++round_match_count;
        }
    }
    const std::size_t expected_multi_rigid_surface_count =
        profile.expected_multi_rigid_round_bone_tag_count == 0 ? 0 : 1;
    if (clip_match_count != 1 || round_match_count !=
            profile.authored_round_surface_count ||
        multi_rigid_round_match_count !=
            expected_multi_rigid_surface_count) {
        WAWVR_STEREO_DIAG_ONCE(
            "ReloadDiag %s feed asset recipe rejected clip-surfaces=%zu round-surfaces=%zu multi-rigid-round-surfaces=%zu expected=1/%u/%zu",
            profile.diagnostic_name, clip_match_count, round_match_count,
            multi_rigid_round_match_count,
            static_cast<unsigned>(profile.authored_round_surface_count),
            expected_multi_rigid_surface_count);
        return false;
    }
    if (feed_device_local_bone >= 0 &&
        (clip.local_bone_count != 1 ||
         clip.local_bones[0] != feed_device_local_bone)) {
        WAWVR_STEREO_DIAG_ONCE(
            "ReloadDiag %s feed-device anchor rejected tag=%s local=%d actual=%u",
            profile.diagnostic_name,
            profile.feed_device_bone_tag_name,
            feed_device_local_bone,
            clip.local_bone_count == 1
                ? static_cast<unsigned>(clip.local_bones[0])
                : 0xFFU);
        return false;
    }
    WAWVR_STEREO_DIAG_ONCE(
        "ReloadDiag %s feed asset recipe matched clip-surfaces=%zu round-surfaces=%zu",
        profile.diagnostic_name, clip_match_count, round_match_count);
    const auto round_distance = [&](const SurfaceMatch& round) noexcept {
        const float x = round.center.x - clip.center.x;
        const float y = round.center.y - clip.center.y;
        const float z = round.center.z - clip.center.z;
        return x * x + y * y + z * z;
    };
    if (profile.detached_round_bone_tag_name != nullptr) {
        const std::uint32_t detached_round_string =
            find_script_string(profile.detached_round_bone_tag_name);
        const int detached_round_local = detached_round_string <= 0xFFFF
            ? find_model_bone(
                  *selected,
                  static_cast<std::uint16_t>(detached_round_string))
            : -1;
        std::size_t exact_match_count = 0;
        std::size_t exact_match_index = 0;
        for (std::size_t index = 0; index < round_match_count; ++index) {
            if (detached_round_local >= 0 &&
                rounds[index].local_bone_count == 1 &&
                rounds[index].local_bones[0] == detached_round_local) {
                exact_match_index = index;
                ++exact_match_count;
            }
        }
        if (exact_match_count != 1) {
            WAWVR_STEREO_DIAG_ONCE(
                "ReloadDiag %s detached round anchor rejected tag=%s local=%d matches=%zu",
                profile.diagnostic_name,
                profile.detached_round_bone_tag_name,
                detached_round_local, exact_match_count);
            return false;
        }
        std::swap(rounds[0], rounds[exact_match_index]);
    } else {
        for (std::size_t index = 1; index < round_match_count; ++index) {
            if (round_distance(rounds[index]) < round_distance(rounds[0])) {
                std::swap(rounds[0], rounds[index]);
            }
        }
    }
    const auto global_bone = [&](const std::uint8_t local) noexcept {
        return static_cast<std::uint8_t>(preceding_bones + local);
    };
    output->model = selected;
    output->clip_surface = clip.surface;
    output->round_surface = rounds[0].surface;
    // The detached renderer currently clones one rigid clip surface plus the
    // nearest one-rigid-list round cluster. Other exact material matches may
    // span several rigid bones (the Mosin's two loose-round props); hide all
    // of those authored bones while leaving them out of the carried clip.
    if (clip.local_bone_count != 1 ||
        rounds[0].local_bone_count != 1) {
        return false;
    }
    const auto append_hide_bones = [&](const SurfaceMatch& match) noexcept {
        for (std::size_t bone_index = 0;
             bone_index < match.local_bone_count; ++bone_index) {
            if (output->hide_bone_count >= output->hide_bones.size()) {
                return false;
            }
            output->hide_bones[output->hide_bone_count++] =
                global_bone(match.local_bones[bone_index]);
        }
        return true;
    };
    if (!append_hide_bones(clip)) {
        return false;
    }
    for (std::size_t index = 0; index < round_match_count; ++index) {
        if (!append_hide_bones(rounds[index])) {
            return false;
        }
    }
    output->root_bone = static_cast<std::uint8_t>(preceding_bones);
    output->moving_bolts = moving_bolts;
    output->moving_bolt_count = profile.moving_bolt_tag_count;
    // Resolve each rigid moving-bolt surface once while the complete model,
    // bone, material, and feed-device recipe is already being validated. The
    // render/update hot paths retain these exact pointers instead of rescanning
    // every XSurface once per eye and once again at pre-skin consumption.
    for (std::size_t moving_index = 0;
         moving_index < output->moving_bolt_count; ++moving_index) {
        const XSurface* matched_surface = nullptr;
        std::uint8_t matched_surface_index = 0xFF;
        std::size_t match_count = 0;
        for (std::size_t surface_index = 0;
             surface_index < selected->surface_count; ++surface_index) {
            const XSurface* const candidate =
                selected->surfaces + surface_index;
            if (!is_cached_bolt_surface(
                    *output, moving_index, candidate)) {
                continue;
            }
            matched_surface = candidate;
            matched_surface_index =
                static_cast<std::uint8_t>(surface_index);
            ++match_count;
        }
        if (match_count != 1 || matched_surface == nullptr ||
            matched_surface_index == 0xFF) {
            WAWVR_STEREO_DIAG_ONCE(
                "ReloadDiag %s moving bolt piece %zu rigid surface discovery rejected count=%zu (expected exactly one)",
                profile.diagnostic_name, moving_index, match_count);
            return false;
        }
        output->moving_bolts[moving_index].surface = matched_surface;
        output->moving_bolts[moving_index].surface_index =
            matched_surface_index;
    }
    for (std::size_t index = 0; index < output->hide_bone_count; ++index) {
        if (output->hide_bones[index] >= 128) {
            return false;
        }
        for (std::size_t peer = index + 1;
             peer < output->hide_bone_count; ++peer) {
            if (output->hide_bones[index] == output->hide_bones[peer]) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool inspect_detachable_magazine_mesh_piece(
    XModel* model,
    std::uint8_t magazine_bone,
    const DetachableMagazineMeshPieceRecipe& recipe,
    InspectedMagazineMeshPiece* output) noexcept;

[[nodiscard]] bool inspect_detachable_charging_mesh_piece(
    XModel* model,
    std::uint8_t handle_bone,
    const DetachableMagazineMeshPieceRecipe& recipe,
    InspectedMagazineMeshPiece* output) noexcept;

[[nodiscard]] bool discover_detachable_charging_handle(
    XModel* const model,
    const std::size_t preceding_bones,
    const std::uint8_t total_bones,
    const DetachableMagazineWeaponProfile& profile,
    MagazineSource* const output) noexcept {
    if (model == nullptr || output == nullptr) {
        return false;
    }
    const auto& recipe = profile.charging;
    if (!recipe.enabled) {
        output->charging_handle = {};
        output->charging_surface = nullptr;
        output->charging_piece_count = 0;
        output->charging_surfaces.fill(nullptr);
        output->charging_rigid_ranges.fill(0);
        return true;
    }
    if (model->bone_count != recipe.expected_model_bone_count ||
        model->surface_count != recipe.expected_model_surface_count ||
        model->bone_names == nullptr || model->base_matrices == nullptr ||
        model->surfaces == nullptr || model->material_handles == nullptr ||
        preceding_bones + recipe.handle_bone_index >= total_bones ||
        preceding_bones + recipe.parent_bone_index >= total_bones ||
        preceding_bones + recipe.handle_bone_index >= 128U ||
        preceding_bones + recipe.parent_bone_index >= 128U) {
        return false;
    }
    const std::uint32_t script_name =
        find_script_string(recipe.handle_bone_tag_name);
    const int local_bone = script_name != 0 && script_name <= 0xFFFFU
        ? find_model_bone(*model, static_cast<std::uint16_t>(script_name))
        : -1;
    if (local_bone != recipe.handle_bone_index ||
        model->bone_names[recipe.handle_bone_index] != script_name ||
        model_bone_parent(*model, local_bone) !=
            recipe.parent_bone_index) {
        return false;
    }

    const DObjAnimMat& bind = model->base_matrices[recipe.handle_bone_index];
    constexpr float kBindPoseTolerance = 0.0001F;
    const auto& expected = recipe.bind_pose;
    const float translation_delta = (std::max)({
        std::abs(bind.translation[0] - expected.translation.x),
        std::abs(bind.translation[1] - expected.translation.y),
        std::abs(bind.translation[2] - expected.translation.z),
    });
    const float quaternion_same_sign = (std::max)({
        std::abs(bind.quaternion[0] - expected.orientation.x),
        std::abs(bind.quaternion[1] - expected.orientation.y),
        std::abs(bind.quaternion[2] - expected.orientation.z),
        std::abs(bind.quaternion[3] - expected.orientation.w),
    });
    const float quaternion_opposite_sign = (std::max)({
        std::abs(bind.quaternion[0] + expected.orientation.x),
        std::abs(bind.quaternion[1] + expected.orientation.y),
        std::abs(bind.quaternion[2] + expected.orientation.z),
        std::abs(bind.quaternion[3] + expected.orientation.w),
    });
    if (translation_delta > kBindPoseTolerance ||
        (std::min)(quaternion_same_sign, quaternion_opposite_sign) >
            kBindPoseTolerance) {
        return false;
    }

    const std::size_t piece_count =
        detachable_magazine_charging_mesh_piece_count(recipe);
    if (piece_count == 0 ||
        piece_count > output->charging_surfaces.size()) {
        return false;
    }
    for (std::size_t piece_index = 0; piece_index < piece_count;
         ++piece_index) {
        const auto piece_recipe =
            detachable_magazine_charging_mesh_piece(recipe, piece_index);
        InspectedMagazineMeshPiece inspected{};
        if (!inspect_detachable_charging_mesh_piece(
                model, recipe.handle_bone_index, piece_recipe,
                &inspected)) {
            return false;
        }
        output->charging_surfaces[piece_index] = inspected.surface;
        output->charging_rigid_ranges[piece_index] =
            piece_recipe.source_rigid_subrange_index;
    }

    output->charging_handle = {
        .tag = static_cast<std::uint16_t>(script_name),
        .parent_bone = static_cast<std::uint8_t>(
            preceding_bones + recipe.parent_bone_index),
        .bone = static_cast<std::uint8_t>(
            preceding_bones + recipe.handle_bone_index),
    };
    output->charging_piece_count = static_cast<std::uint8_t>(piece_count);
    output->charging_surface = output->charging_surfaces[0];
    return true;
}

[[nodiscard]] bool discover_magazine_feed_door(
    XModel* const model, const std::size_t preceding_bones,
    const std::uint8_t total_bones,
    const DetachableMagazineWeaponProfile& profile,
    MagazineSource* const output) noexcept {
    if (!profile.feed_door.enabled) {
        output->feed_door = {};
        output->feed_door_surface = nullptr;
        return true;
    }
    const auto& door = profile.feed_door;
    const auto tag = find_script_string(door.bone_tag_name);
    if (tag == 0 || tag > 0xFFFFU ||
        preceding_bones + door.bone_index >= total_bones ||
        preceding_bones + door.bone_index >= 128U ||
        model->bone_names[door.bone_index] != tag ||
        model_bone_parent(*model, door.bone_index) != door.parent_bone_index) {
        return false;
    }
    const auto& bind = model->base_matrices[door.bone_index];
    const auto& expected = door.closed_pose;
    const float q[4]{expected.orientation.x, expected.orientation.y,
                     expected.orientation.z, expected.orientation.w};
    const float t[3]{expected.translation.x, expected.translation.y,
                     expected.translation.z};
    for (int i = 0; i < 3; ++i) {
        if (std::abs(bind.translation[i] - t[i]) > 0.0001F) return false;
    }
    float same = 0.0F, opposite = 0.0F;
    for (int i = 0; i < 4; ++i) {
        same = (std::max)(same, std::abs(bind.quaternion[i] - q[i]));
        opposite = (std::max)(opposite, std::abs(bind.quaternion[i] + q[i]));
    }
    if ((std::min)(same, opposite) > 0.0001F) return false;
    InspectedMagazineMeshPiece inspected{};
    if (!inspect_detachable_charging_mesh_piece(
            model, door.bone_index, door.piece, &inspected)) {
        return false;
    }
    output->feed_door = {
        .tag = static_cast<std::uint16_t>(tag),
        .parent_bone = static_cast<std::uint8_t>(preceding_bones + door.parent_bone_index),
        .bone = static_cast<std::uint8_t>(preceding_bones + door.bone_index),
    };
    output->feed_door_surface = inspected.surface;
    return true;
}

[[nodiscard]] bool inspect_detachable_magazine_mesh_piece(
    XModel* const model,
    const std::uint8_t magazine_bone,
    const DetachableMagazineMeshPieceRecipe& recipe,
    InspectedMagazineMeshPiece* const output) noexcept {
    if (output == nullptr || model == nullptr || model->surfaces == nullptr ||
        model->material_handles == nullptr ||
        recipe.source_surface_index >= model->surface_count) {
        return false;
    }
    *output = {};
    XSurface& surface = model->surfaces[recipe.source_surface_index];
    // Later model surfaces legitimately carry nonzero authored base indices.
    // The detached asset builder normalizes those fields after copying the
    // exact CPU-side rigid slice, so they are identity data rather than a
    // reason to reject otherwise exact magazine/charging geometry.
    if (surface.deformed ||
        surface.vertex_count != recipe.source_surface_vertex_count ||
        surface.triangle_count != recipe.source_surface_triangle_count ||
        surface.vertices == nullptr || surface.triangle_indices == nullptr ||
        surface.index_buffer == nullptr ||
        surface.rigid_vertex_list == nullptr ||
        surface.rigid_vertex_list_count !=
            recipe.source_surface_rigid_subrange_count ||
        !accessible_range(
            surface.vertices,
            static_cast<std::size_t>(surface.vertex_count) *
                sizeof(PackedVertex), false) ||
        !accessible_range(
            surface.triangle_indices,
            static_cast<std::size_t>(surface.triangle_count) * 3U *
                sizeof(std::uint16_t), false) ||
        !accessible_range(
            surface.rigid_vertex_list,
            static_cast<std::size_t>(surface.rigid_vertex_list_count) *
                sizeof(RigidVertexList), false)) {
        return false;
    }

    void* material = nullptr;
    std::memcpy(
        &material, model->material_handles + recipe.source_surface_index,
        sizeof(material));
    const char* material_name = nullptr;
    if (!read_material_name(material, &material_name) ||
        !c_string_equals(material_name, recipe.material_name)) {
        return false;
    }

    constexpr std::size_t kMaximumRigidRanges = 16;
    if (surface.rigid_vertex_list_count > kMaximumRigidRanges) {
        return false;
    }
    std::array<gameplay::RigidSubmeshRange, kMaximumRigidRanges> ranges{};
    for (std::size_t index = 0;
         index < surface.rigid_vertex_list_count; ++index) {
        const RigidVertexList& rigid = surface.rigid_vertex_list[index];
        if ((rigid.bone_offset & 0x3FU) != 0) {
            return false;
        }
        ranges[index] = {
            .bone_id = static_cast<std::uint16_t>(rigid.bone_offset >> 6U),
            .vertex_count = rigid.vertex_count,
            .triangle_offset = rigid.triangle_offset,
            .triangle_count = rigid.triangle_count,
        };
    }
    const auto inspection = gameplay::inspect_exact_rigid_submesh(
        surface.vertex_count, surface.triangle_count,
        std::span<const gameplay::RigidSubmeshRange>{
            ranges.data(), surface.rigid_vertex_list_count},
        recipe.source_rigid_subrange_index, magazine_bone,
        std::span<const std::uint16_t>{
            surface.triangle_indices,
            static_cast<std::size_t>(surface.triangle_count) * 3U});
    if (!inspection.has_value() ||
        inspection->first_vertex != recipe.vertex_offset ||
        inspection->vertex_count != recipe.vertex_count ||
        inspection->triangle_offset != recipe.triangle_offset ||
        inspection->triangle_count != recipe.triangle_count) {
        return false;
    }
    output->surface = &surface;
    output->material = material;
    output->rigid = *inspection;
    return true;
}

[[nodiscard]] bool inspect_detachable_charging_mesh_piece(
    XModel* const model,
    const std::uint8_t handle_bone,
    const DetachableMagazineMeshPieceRecipe& recipe,
    InspectedMagazineMeshPiece* const output) noexcept {
    if (!inspect_detachable_magazine_mesh_piece(
            model, handle_bone, recipe, output) || output == nullptr ||
        output->surface == nullptr) {
        return false;
    }
    const XSurface& surface = *output->surface;
    std::array<std::uint32_t, 4> expected_part_bits{};
    for (std::size_t index = 0;
         index < surface.rigid_vertex_list_count; ++index) {
        const RigidVertexList& rigid = surface.rigid_vertex_list[index];
        if ((rigid.bone_offset & 0x3FU) != 0) {
            return false;
        }
        const std::uint32_t bone = rigid.bone_offset >> 6U;
        if (bone >= 128U) {
            return false;
        }
        expected_part_bits[bone >> 5U] |=
            0x80000000U >> (bone & 31U);
    }
    const auto* const actual_part_bits =
        reinterpret_cast<const std::uint32_t*>(surface.part_bits);
    for (std::size_t word = 0; word < expected_part_bits.size(); ++word) {
        if (actual_part_bits[word] != expected_part_bits[word]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool discover_detachable_magazine_source(
    void* const dobj,
    const DetachableMagazineWeaponProfile& profile,
    MagazineSource* const output) noexcept {
    if (output == nullptr || !accessible_range(dobj, kDObjSize, false)) {
        return false;
    }
    if (!validate_detachable_magazine_weapon_profile(profile)) {
        return false;
    }
    *output = {};
    auto* const bytes = static_cast<std::uint8_t*>(dobj);
    std::uint8_t model_count = 0;
    std::uint8_t total_bones = 0;
    XModel** models = nullptr;
    std::memcpy(&model_count, bytes + kDObjNumModelsOffset, sizeof(model_count));
    std::memcpy(&total_bones, bytes + kDObjNumBonesOffset, sizeof(total_bones));
    std::memcpy(&models, bytes + kDObjModelsOffset, sizeof(models));
    if (model_count == 0 || models == nullptr ||
        !accessible_range(
            models, static_cast<std::size_t>(model_count) * sizeof(*models),
            false)) {
        return false;
    }

    std::size_t preceding_bones = 0;
    XModel* selected = nullptr;
    for (std::size_t index = 0; index < model_count; ++index) {
        XModel* candidate = nullptr;
        std::memcpy(&candidate, models + index, sizeof(candidate));
        if (candidate == nullptr ||
            !accessible_range(candidate, sizeof(*candidate), false)) {
            return false;
        }
        if (c_string_equals(candidate->name, profile.viewmodel_model_name)) {
            selected = candidate;
            break;
        }
        preceding_bones += candidate->bone_count;
    }
    const auto& recipe = profile.mesh;
    if (selected == nullptr ||
        selected->bone_count != recipe.expected_model_bone_count ||
        selected->surface_count != recipe.expected_model_surface_count ||
        preceding_bones >= total_bones ||
        preceding_bones + recipe.magazine_bone_index >= total_bones ||
        preceding_bones + recipe.magazine_bone_index >= 128U ||
        selected->bone_names == nullptr || selected->surfaces == nullptr ||
        selected->material_handles == nullptr ||
        selected->base_matrices == nullptr ||
        !accessible_range(
            selected->bone_names,
            static_cast<std::size_t>(selected->bone_count) *
                sizeof(std::uint16_t), false) ||
        !accessible_range(
            selected->surfaces,
            static_cast<std::size_t>(selected->surface_count) *
                sizeof(XSurface), false) ||
        !accessible_range(
            selected->material_handles,
            static_cast<std::size_t>(selected->surface_count) *
                sizeof(void*), false) ||
        !accessible_range(
            selected->base_matrices,
            static_cast<std::size_t>(selected->bone_count) *
                sizeof(DObjAnimMat), false)) {
        return false;
    }

    const std::uint32_t script_name =
        find_script_string(profile.magazine_bone_tag_name);
    const int local_bone = script_name != 0 && script_name <= 0xFFFFU
        ? find_model_bone(*selected, static_cast<std::uint16_t>(script_name))
        : -1;
    if (local_bone != recipe.magazine_bone_index ||
        selected->bone_names[recipe.magazine_bone_index] != script_name) {
        return false;
    }
    const std::uint32_t insertion_name =
        profile.insertion_anchor_bone_tag_name != nullptr
        ? find_script_string(profile.insertion_anchor_bone_tag_name)
        : script_name;
    const std::uint8_t insertion_bone =
        profile.insertion_anchor_bone_tag_name != nullptr
        ? profile.insertion_anchor_bone_index : recipe.magazine_bone_index;
    if (insertion_name == 0 || insertion_name > 0xFFFFU ||
        insertion_bone >= selected->bone_count ||
        selected->bone_names[insertion_bone] != insertion_name) {
        return false;
    }
    const DObjAnimMat& bind =
        selected->base_matrices[recipe.magazine_bone_index];
    constexpr float kBindPoseTolerance = 0.0001F;
    const auto& expected_bind = recipe.bind_pose;
    const bool translation_matches =
        std::abs(bind.translation[0] - expected_bind.translation.x) <=
            kBindPoseTolerance &&
        std::abs(bind.translation[1] - expected_bind.translation.y) <=
            kBindPoseTolerance &&
        std::abs(bind.translation[2] - expected_bind.translation.z) <=
            kBindPoseTolerance;
    const float quaternion_same_sign = (std::max)({
        std::abs(bind.quaternion[0] - expected_bind.orientation.x),
        std::abs(bind.quaternion[1] - expected_bind.orientation.y),
        std::abs(bind.quaternion[2] - expected_bind.orientation.z),
        std::abs(bind.quaternion[3] - expected_bind.orientation.w),
    });
    const float quaternion_opposite_sign = (std::max)({
        std::abs(bind.quaternion[0] + expected_bind.orientation.x),
        std::abs(bind.quaternion[1] + expected_bind.orientation.y),
        std::abs(bind.quaternion[2] + expected_bind.orientation.z),
        std::abs(bind.quaternion[3] + expected_bind.orientation.w),
    });
    if (!translation_matches ||
        (std::min)(quaternion_same_sign, quaternion_opposite_sign) >
            kBindPoseTolerance) {
        return false;
    }

    constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
    std::uint64_t fingerprint = kFnvOffset;
    const std::size_t piece_count =
        detachable_magazine_mesh_piece_count(recipe);
    if (piece_count == 0 ||
        piece_count > kMaximumDetachableMagazineMeshPieces) {
        return false;
    }
    output->piece_count = static_cast<std::uint8_t>(piece_count);
    for (std::size_t piece_index = 0; piece_index < piece_count;
         ++piece_index) {
        const auto piece_recipe =
            detachable_magazine_mesh_piece(profile, piece_index);
        InspectedMagazineMeshPiece inspected{};
        if (!inspect_detachable_magazine_mesh_piece(
                selected, recipe.magazine_bone_index, piece_recipe,
                &inspected)) {
            return false;
        }
        output->piece_surfaces[piece_index] =
            piece_recipe.source_surface_index;
        output->piece_rigid_ranges[piece_index] =
            piece_recipe.source_rigid_subrange_index;
        fingerprint = append_asset_fingerprint(
            fingerprint,
            inspected.surface->vertices + inspected.rigid.first_vertex,
            static_cast<std::size_t>(inspected.rigid.vertex_count) *
                sizeof(PackedVertex));
        fingerprint = append_asset_fingerprint(
            fingerprint,
            inspected.surface->triangle_indices +
                static_cast<std::size_t>(
                    inspected.rigid.triangle_offset) * 3U,
            static_cast<std::size_t>(inspected.rigid.triangle_count) * 3U *
                sizeof(std::uint16_t));
        fingerprint = append_asset_fingerprint(
            fingerprint, &inspected.material, sizeof(inspected.material));
    }
    fingerprint = append_asset_fingerprint(
        fingerprint, &bind, sizeof(bind));
    if (fingerprint == 0) {
        return false;
    }

    output->model = selected;
    output->bone_tag = static_cast<std::uint16_t>(script_name);
    output->insertion_anchor_tag = static_cast<std::uint16_t>(insertion_name);
    output->root_bone = static_cast<std::uint8_t>(preceding_bones);
    output->local_bone = recipe.magazine_bone_index;
    output->global_bone = static_cast<std::uint8_t>(
        preceding_bones + recipe.magazine_bone_index);
    output->surface = recipe.source_surface_index;
    output->rigid_range = recipe.source_rigid_subrange_index;
    output->asset_fingerprint = fingerprint;
    return discover_detachable_charging_handle(
        selected, preceding_bones, total_bones, profile, output) &&
        discover_magazine_feed_door(
            selected, preceding_bones, total_bones, profile, output);
}

[[nodiscard]] bool magazine_source_piece_identity_matches(
    const MagazineSource& left,
    const MagazineSource& right) noexcept {
    if (left.piece_count == 0 || left.piece_count != right.piece_count ||
        left.piece_count > kMaximumDetachableMagazineMeshPieces) {
        return false;
    }
    for (std::size_t index = 0; index < left.piece_count; ++index) {
        if (left.piece_surfaces[index] != right.piece_surfaces[index] ||
            left.piece_rigid_ranges[index] !=
                right.piece_rigid_ranges[index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool magazine_charging_source_piece_identity_matches(
    const MagazineSource& left,
    const MagazineSource& right) noexcept {
    if (left.charging_piece_count != right.charging_piece_count ||
        left.charging_piece_count >
            kMaximumDetachableMagazineChargingMeshPieces) {
        return false;
    }
    if (left.charging_piece_count == 0) {
        return left.charging_surface == nullptr &&
            right.charging_surface == nullptr;
    }
    for (std::size_t index = 0;
         index < left.charging_piece_count; ++index) {
        if (left.charging_surfaces[index] !=
                right.charging_surfaces[index] ||
            left.charging_rigid_ranges[index] !=
                right.charging_rigid_ranges[index]) {
            return false;
        }
    }
    return left.charging_surface == left.charging_surfaces[0] &&
        right.charging_surface == right.charging_surfaces[0];
}

[[nodiscard]] bool magazine_source_piece_recipe_matches(
    const DetachableMagazineWeaponProfile& profile,
    const MagazineSource& source) noexcept {
    const std::size_t expected_piece_count =
        detachable_magazine_mesh_piece_count(profile.mesh);
    if (source.piece_count != expected_piece_count ||
        source.piece_count == 0 ||
        source.piece_count > kMaximumDetachableMagazineMeshPieces ||
        source.surface != source.piece_surfaces[0] ||
        source.rigid_range != source.piece_rigid_ranges[0]) {
        return false;
    }
    for (std::size_t index = 0; index < source.piece_count; ++index) {
        const auto piece = detachable_magazine_mesh_piece(profile, index);
        if (source.piece_surfaces[index] != piece.source_surface_index ||
            source.piece_rigid_ranges[index] !=
                piece.source_rigid_subrange_index) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool magazine_charging_source_piece_recipe_matches(
    const DetachableMagazineWeaponProfile& profile,
    const MagazineSource& source) noexcept {
    if (!profile.charging.enabled) {
        if (source.charging_piece_count != 0 ||
            source.charging_surface != nullptr) {
            return false;
        }
        for (std::size_t index = 0;
             index < source.charging_surfaces.size(); ++index) {
            if (source.charging_surfaces[index] != nullptr ||
                source.charging_rigid_ranges[index] != 0) {
                return false;
            }
        }
        return true;
    }
    const std::size_t expected_piece_count =
        detachable_magazine_charging_mesh_piece_count(profile.charging);
    if (source.model == nullptr || source.model->surfaces == nullptr ||
        source.charging_piece_count != expected_piece_count ||
        source.charging_piece_count == 0 ||
        source.charging_piece_count > source.charging_surfaces.size() ||
        source.charging_surface != source.charging_surfaces[0]) {
        return false;
    }
    for (std::size_t index = 0;
         index < source.charging_piece_count; ++index) {
        const auto piece = detachable_magazine_charging_mesh_piece(
            profile.charging, index);
        if (source.charging_surfaces[index] !=
                source.model->surfaces + piece.source_surface_index ||
            source.charging_rigid_ranges[index] !=
                piece.source_rigid_subrange_index) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool is_cached_magazine_charging_surface(
    const MagazineSource& source,
    const XSurface* const surface) noexcept {
    if (surface == nullptr || source.charging_piece_count == 0 ||
        source.charging_piece_count > source.charging_surfaces.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < source.charging_piece_count; ++index) {
        if (source.charging_surfaces[index] == surface) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool cached_magazine_source_matches_dobj(
    void* const dobj,
    const DetachableMagazineWeaponProfile& profile,
    const MagazineSource& source) noexcept {
    // This is one synchronous read-only DObj/model/bone traversal. It invokes
    // no engine callback and retains no access facts after returning.
    WeaponIdentitySnapshotReader memory;
    const auto readable = [&memory](const void* address, std::size_t size) noexcept {
        return memory.readable(reinterpret_cast<std::uintptr_t>(address), size);
    };
    if (source.model == nullptr || source.root_bone >= 128U ||
        source.global_bone >= 128U ||
        !readable(dobj, kDObjSize) ||
        !readable(source.model, sizeof(*source.model)) ||
        !c_string_equals(source.model->name, profile.viewmodel_model_name, &memory)) {
        return false;
    }
    auto* const bytes = static_cast<std::uint8_t*>(dobj);
    std::uint8_t model_count = 0;
    std::uint8_t total_bones = 0;
    XModel** models = nullptr;
    std::memcpy(&model_count, bytes + kDObjNumModelsOffset, sizeof(model_count));
    std::memcpy(&total_bones, bytes + kDObjNumBonesOffset, sizeof(total_bones));
    std::memcpy(&models, bytes + kDObjModelsOffset, sizeof(models));
    if (model_count == 0 || source.global_bone >= total_bones ||
        models == nullptr ||
        !readable(
            models, static_cast<std::size_t>(model_count) * sizeof(*models))) {
        return false;
    }
    std::size_t preceding_bones = 0;
    bool attached = false;
    for (std::size_t index = 0; index < model_count; ++index) {
        XModel* candidate = nullptr;
        std::memcpy(&candidate, models + index, sizeof(candidate));
        if (candidate == source.model) {
            attached = true;
            break;
        }
        if (candidate == nullptr ||
            !readable(candidate, sizeof(*candidate))) {
            return false;
        }
        preceding_bones += candidate->bone_count;
    }
    const bool magazine_matches =
        attached && preceding_bones == source.root_bone &&
        magazine_source_piece_recipe_matches(profile, source) &&
        source.local_bone == profile.mesh.magazine_bone_index &&
        source.global_bone == preceding_bones + source.local_bone &&
        source.model->bone_names != nullptr &&
        readable(
            source.model->bone_names,
            static_cast<std::size_t>(source.model->bone_count) *
                sizeof(std::uint16_t)) &&
        source.model->bone_names[source.local_bone] == source.bone_tag &&
        source.model->bone_names[
            profile.insertion_anchor_bone_tag_name != nullptr
                ? profile.insertion_anchor_bone_index : source.local_bone] ==
            source.insertion_anchor_tag;
    if (!magazine_matches) {
        return false;
    }
    if (profile.feed_door.enabled) {
        const auto& door = profile.feed_door;
        if (source.feed_door_surface !=
                source.model->surfaces + door.piece.source_surface_index ||
            source.feed_door.bone != preceding_bones + door.bone_index ||
            source.feed_door.parent_bone != preceding_bones + door.parent_bone_index ||
            source.model->bone_names[door.bone_index] != source.feed_door.tag) {
            return false;
        }
    } else if (source.feed_door.tag != 0 || source.feed_door_surface != nullptr) {
        return false;
    }
    if (!profile.charging.enabled) {
        return magazine_charging_source_piece_recipe_matches(
                   profile, source) &&
            source.charging_handle.tag == 0;
    }
    const auto& recipe = profile.charging;
    return magazine_charging_source_piece_recipe_matches(profile, source) &&
        source.charging_handle.tag != 0 &&
        source.charging_handle.bone ==
            preceding_bones + recipe.handle_bone_index &&
        source.charging_handle.parent_bone ==
            preceding_bones + recipe.parent_bone_index &&
        source.model->bone_names[recipe.handle_bone_index] ==
            source.charging_handle.tag;
}

[[nodiscard]] bool capture_magazine_dobj_witness(
    void* const dobj,
    const MagazineSource& source,
    RuntimeMagazineVisualState::DObjWitness* const output) noexcept {
    if (output == nullptr || source.model == nullptr ||
        !accessible_range(dobj, kDObjSize, false) ||
        !accessible_range(source.model, sizeof(*source.model), false)) {
        return false;
    }
    auto* const bytes = static_cast<std::uint8_t*>(dobj);
    std::uint8_t model_count = 0;
    std::uint8_t total_bones = 0;
    XModel** models = nullptr;
    std::memcpy(&model_count, bytes + kDObjNumModelsOffset,
                sizeof(model_count));
    std::memcpy(&total_bones, bytes + kDObjNumBonesOffset,
                sizeof(total_bones));
    std::memcpy(&models, bytes + kDObjModelsOffset, sizeof(models));
    if (model_count == 0 || models == nullptr ||
        source.global_bone >= total_bones ||
        !accessible_range(
            models, static_cast<std::size_t>(model_count) * sizeof(*models),
            false)) {
        return false;
    }

    RuntimeMagazineVisualState::DObjWitness witness{};
    witness.dobj = dobj;
    witness.model_array = models;
    witness.model_count = model_count;
    witness.total_bones = total_bones;
    for (std::size_t index = 0; index < model_count; ++index) {
        XModel* candidate = nullptr;
        std::memcpy(&candidate, models + index, sizeof(candidate));
        witness.ordered_models[index] = candidate;
        if (candidate == source.model) {
            witness.selected_model_index = static_cast<std::uint8_t>(index);
        }
    }
    if (witness.selected_model_index == 0xFF) {
        return false;
    }
    witness.selected_name = source.model->name;
    witness.selected_bone_count = source.model->bone_count;
    witness.selected_root_bone_count = source.model->root_bone_count;
    witness.selected_surface_count = source.model->surface_count;
    witness.selected_bone_names = source.model->bone_names;
    witness.selected_base_matrices = source.model->base_matrices;
    witness.selected_surfaces = source.model->surfaces;
    witness.selected_material_handles = source.model->material_handles;
    witness.valid = true;
    *output = witness;
    return true;
}

[[nodiscard]] bool magazine_dobj_witness_matches(
    void* const dobj,
    const MagazineSource& source,
    const RuntimeMagazineVisualState::DObjWitness& witness) noexcept {
    if (!witness.valid || witness.dobj != dobj || source.model == nullptr ||
        witness.selected_model_index >= witness.model_count ||
        !accessible_range(dobj, kDObjSize, false)) {
        return false;
    }
    auto* const bytes = static_cast<std::uint8_t*>(dobj);
    std::uint8_t model_count = 0;
    std::uint8_t total_bones = 0;
    XModel** models = nullptr;
    std::memcpy(&model_count, bytes + kDObjNumModelsOffset,
                sizeof(model_count));
    std::memcpy(&total_bones, bytes + kDObjNumBonesOffset,
                sizeof(total_bones));
    std::memcpy(&models, bytes + kDObjModelsOffset, sizeof(models));
    if (model_count != witness.model_count ||
        total_bones != witness.total_bones ||
        models != witness.model_array || models == nullptr ||
        source.global_bone >= total_bones ||
        !accessible_range(
            models, static_cast<std::size_t>(model_count) * sizeof(*models),
            false) ||
        std::memcmp(
            models, witness.ordered_models.data(),
            static_cast<std::size_t>(model_count) * sizeof(*models)) != 0 ||
        witness.ordered_models[witness.selected_model_index] != source.model ||
        !accessible_range(source.model, sizeof(*source.model), false)) {
        return false;
    }
    return source.model->name == witness.selected_name &&
        source.model->bone_count == witness.selected_bone_count &&
        source.model->root_bone_count == witness.selected_root_bone_count &&
        source.model->surface_count == witness.selected_surface_count &&
        source.model->bone_names == witness.selected_bone_names &&
        source.model->base_matrices == witness.selected_base_matrices &&
        source.model->surfaces == witness.selected_surfaces &&
        source.model->material_handles == witness.selected_material_handles;
}

[[nodiscard]] bool set_authored_magazine_hidden_locked(
    const bool hidden) noexcept {
    RuntimeMagazineVisualState& visual = g_magazine_visual_state;
    // The steady loaded/idle path does not own an authored hide bit. Avoid
    // revalidating the weapon definition, DObj, persistent part bits, and live
    // part bits every held frame when there is nothing to restore. Every path
    // that takes ownership sets authored_hidden before returning, so a false
    // value is a complete no-op proof for the visible request.
    if (!hidden && !visual.authored_hidden) {
        return true;
    }
    if (g_weapon_info_array == 0 || !visual.supported ||
        visual.weapon_index <= 0 || visual.source.global_bone >= 128U) {
        return !hidden && !visual.authored_hidden;
    }
    auto* const info = reinterpret_cast<std::uint8_t*>(
        g_weapon_info_array +
        static_cast<std::size_t>(visual.weapon_index) * kWeaponInfoStride);
    if (!accessible_range(
            info, kWeaponInfoPartBitsOffset + 4U * sizeof(std::uint32_t),
            true)) {
        return false;
    }
    void* owned = nullptr;
    std::memcpy(&owned, info, sizeof(owned));
    const DetachableMagazineWeaponProfile* const active_profile =
        revalidate_active_magazine_gameplay_weapon(
            visual.weapon_index, visual.weapon_definition_address);
    const bool owns_original = owned != nullptr &&
        owned == visual.viewmodel_dobj &&
        accessible_range(owned, kDObjSize, true);
    // Replacement validation is irrelevant once the exact original DObj
    // already passed its writable-range check. Rewalking all native model
    // names/bones here on every hidden frame duplicated the source traversal.
    const bool owns_exact_replacement = !owns_original && owned != nullptr &&
        active_profile != nullptr && active_profile->id == visual.profile_id &&
        accessible_range(owned, kDObjSize, true) &&
        cached_magazine_source_matches_dobj(
            owned, *active_profile, visual.source);
    if (active_profile == nullptr ||
        active_profile->id != visual.profile_id ||
        (owned != nullptr && !owns_original && !owns_exact_replacement)) {
        // A reused map-local weaponInfo slot is no longer the saved Colt. Do
        // not copy its old j_clip state into an unrelated DObj or persistent
        // part-bit array. The outgoing object is no longer renderable, so its
        // ownership can be retired without a stale write.
        if (!hidden) {
            visual.authored_hidden = false;
            return true;
        }
        return false;
    }
    auto* const persistent = reinterpret_cast<std::uint32_t*>(
        info + kWeaponInfoPartBitsOffset);
    const std::size_t word = visual.source.global_bone >> 5U;
    const std::uint32_t mask =
        0x80000000U >> (visual.source.global_bone & 31U);
    if (hidden) {
        if (!owns_original) {
            return false;
        }
        auto* const live = reinterpret_cast<std::uint32_t*>(
            static_cast<std::uint8_t*>(visual.viewmodel_dobj) +
            kT4DObjHidePartBitsOffset);
        if (!visual.authored_hidden) {
            visual.persistent_was_hidden = (persistent[word] & mask) != 0;
            visual.live_was_hidden = (live[word] & mask) != 0;
        }
        persistent[word] |= mask;
        live[word] |= mask;
        visual.authored_hidden = true;
        return true;
    }
    if (!visual.authored_hidden) {
        return true;
    }
    if (visual.persistent_was_hidden) {
        persistent[word] |= mask;
    } else {
        persistent[word] &= ~mask;
    }

    bool restored_live = false;
    if (owns_original) {
        auto* const live = reinterpret_cast<std::uint32_t*>(
            static_cast<std::uint8_t*>(owned) +
            kT4DObjHidePartBitsOffset);
        if (visual.live_was_hidden) {
            live[word] |= mask;
        } else {
            live[word] &= ~mask;
        }
        restored_live = true;
    } else {
        // The engine may rebuild a weapon DObj while a manual transaction is
        // active. Its persistent part bits are still the authoritative slot,
        // but the replacement DObj can already have copied the temporary hide
        // bit. Restore that new live copy only after proving the same exact
        // profiled model/bone recipe; never write through a stale old pointer.
        if (owns_exact_replacement) {
            auto* const live = reinterpret_cast<std::uint32_t*>(
                static_cast<std::uint8_t*>(owned) +
                kT4DObjHidePartBitsOffset);
            if (visual.persistent_was_hidden) {
                live[word] |= mask;
            } else {
                live[word] &= ~mask;
            }
            restored_live = true;
        }
    }
    visual.authored_hidden = false;
    if (!restored_live && owned != nullptr) {
        WAWVR_STEREO_DIAG_ONCE(
            "ReloadDiag restored detachable-magazine persistent visibility without writing an unproven replacement DObj");
    }
    return true;
}

[[nodiscard]] BoltActionAmmoSnapshot read_bolt_action_ammo(
    const void* const player_state,
    const RuntimeWeaponDefinitionIdentity& identity) noexcept {
    BoltActionAmmoSnapshot snapshot{};
    if (player_state == nullptr ||
        !accessible_range(player_state, kPlayerStateMinimumSpan, false) ||
        identity.definition_address == 0 ||
        !accessible_range(
            reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(identity.definition_address)),
            kWeaponDefinitionManualReloadSpan, false)) {
        return snapshot;
    }

    const auto* const definition = reinterpret_cast<const std::uint8_t*>(
        static_cast<std::uintptr_t>(identity.definition_address));
    const auto* const player = static_cast<const std::uint8_t*>(player_state);
    std::int32_t weapon_state = 0;
    std::int32_t ammo_index = -1;
    std::int32_t clip_index = -1;
    std::int32_t clip_size = 0;
    std::int32_t reload_ammo_add = 0;
    std::int32_t reload_start_add = 0;
    std::int32_t bolt_action = 0;
    std::int32_t segmented_reload = 0;
    std::memcpy(
        &weapon_state, player + kPlayerStateWeaponStateOffset,
        sizeof(weapon_state));
    std::memcpy(
        &ammo_index, definition + kWeaponDefinitionAmmoIndexOffset,
        sizeof(ammo_index));
    std::memcpy(
        &clip_index, definition + kWeaponDefinitionClipIndexOffset,
        sizeof(clip_index));
    std::memcpy(
        &clip_size, definition + kWeaponDefinitionClipSizeOffset,
        sizeof(clip_size));
    std::memcpy(
        &reload_ammo_add,
        definition + kWeaponDefinitionReloadAmmoAddOffset,
        sizeof(reload_ammo_add));
    std::memcpy(
        &reload_start_add,
        definition + kWeaponDefinitionReloadStartAddOffset,
        sizeof(reload_start_add));
    std::memcpy(
        &bolt_action, definition + kWeaponDefinitionBoltActionOffset,
        sizeof(bolt_action));
    std::memcpy(
        &segmented_reload,
        definition + kWeaponDefinitionSegmentedReloadOffset,
        sizeof(segmented_reload));
    if (ammo_index < 0 || clip_index < 0 ||
        ammo_index >= static_cast<std::int32_t>(kMaximumPlayerAmmoPools) ||
        clip_index >= static_cast<std::int32_t>(kMaximumPlayerAmmoPools)) {
        return snapshot;
    }

    std::int32_t reserve = 0;
    std::int32_t loaded = 0;
    std::memcpy(
        &reserve,
        player + kPlayerStateAmmoOffset +
            static_cast<std::size_t>(ammo_index) * sizeof(reserve),
        sizeof(reserve));
    std::memcpy(
        &loaded,
        player + kPlayerStateClipAmmoOffset +
            static_cast<std::size_t>(clip_index) * sizeof(loaded),
        sizeof(loaded));
    snapshot = {
        .valid = true,
        .definition_address = identity.definition_address,
        .ammo_index = ammo_index,
        .clip_index = clip_index,
        .commit = {
            .weapon_state = weapon_state,
            .clip_size = clip_size,
            .reserve = reserve,
            .loaded = loaded,
            .reload_ammo_add = reload_ammo_add,
            .reload_start_add = reload_start_add,
            .bolt_action = bolt_action,
            .segmented_reload = segmented_reload,
            .physical_clip_capacity = kManualStripperClipCapacity,
        },
    };
    return snapshot;
}

[[nodiscard]] DetachableMagazineAmmoSnapshot
read_detachable_magazine_ammo(
    const ManualReloadViewmodelContext& context,
    const RuntimeWeaponDefinitionIdentity& identity,
    const DetachableMagazineWeaponProfile& profile,
    std::array<std::int32_t, 8>* const diagnostic = nullptr) noexcept {
    DetachableMagazineAmmoSnapshot snapshot{};
    if (context.player_state == nullptr ||
        !accessible_range(
            context.player_state, kPlayerStateMinimumSpan, false) ||
        identity.definition_address == 0 ||
        !accessible_range(
            reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(identity.definition_address)),
            kWeaponDefinitionManualReloadSpan, false)) {
        if (diagnostic != nullptr) {
            (*diagnostic)[0] = 0;
        }
        return snapshot;
    }
    const auto* const definition = reinterpret_cast<const std::uint8_t*>(
        static_cast<std::uintptr_t>(identity.definition_address));
    std::uint32_t world_clip_model = 0;
    std::int32_t ammo_index = -1;
    std::int32_t clip_index = -1;
    std::int32_t clip_size = 0;
    std::int32_t reload_ammo_add = 0;
    std::int32_t reload_start_add = 0;
    std::int32_t bolt_action = 1;
    std::int32_t segmented_reload = 1;
    std::memcpy(
        &world_clip_model,
        definition + kWeaponDefinitionWorldClipModelOffset,
        sizeof(world_clip_model));
    std::memcpy(
        &ammo_index, definition + kWeaponDefinitionAmmoIndexOffset,
        sizeof(ammo_index));
    std::memcpy(
        &clip_index, definition + kWeaponDefinitionClipIndexOffset,
        sizeof(clip_index));
    std::memcpy(
        &clip_size, definition + kWeaponDefinitionClipSizeOffset,
        sizeof(clip_size));
    std::memcpy(
        &reload_ammo_add,
        definition + kWeaponDefinitionReloadAmmoAddOffset,
        sizeof(reload_ammo_add));
    std::memcpy(
        &reload_start_add,
        definition + kWeaponDefinitionReloadStartAddOffset,
        sizeof(reload_start_add));
    std::memcpy(
        &bolt_action, definition + kWeaponDefinitionBoltActionOffset,
        sizeof(bolt_action));
    std::memcpy(
        &segmented_reload,
        definition + kWeaponDefinitionSegmentedReloadOffset,
        sizeof(segmented_reload));
    if (diagnostic != nullptr) {
        (*diagnostic)[0] = 1;
        (*diagnostic)[1] = static_cast<std::int32_t>(world_clip_model);
        (*diagnostic)[2] = ammo_index;
        (*diagnostic)[3] = clip_index;
        (*diagnostic)[4] = clip_size;
        (*diagnostic)[5] = bolt_action;
        (*diagnostic)[6] = segmented_reload;
    }
    if (!profile.requires_embedded_viewmodel_magazine ||
        world_clip_model != 0 || bolt_action != 0 ||
        segmented_reload != 0 || reload_ammo_add != 0 ||
        reload_start_add != 0 ||
        clip_size != profile.expected_clip_size ||
        ammo_index < 0 || clip_index < 0 ||
        ammo_index >= static_cast<std::int32_t>(kMaximumPlayerAmmoPools) ||
        clip_index >= static_cast<std::int32_t>(kMaximumPlayerAmmoPools)) {
        return snapshot;
    }
    const auto* const player =
        static_cast<const std::uint8_t*>(context.player_state);
    std::int32_t reserve = 0;
    std::int32_t loaded = 0;
    std::memcpy(
        &reserve,
        player + kPlayerStateAmmoOffset +
            static_cast<std::size_t>(ammo_index) * sizeof(reserve),
        sizeof(reserve));
    std::memcpy(
        &loaded,
        player + kPlayerStateClipAmmoOffset +
            static_cast<std::size_t>(clip_index) * sizeof(loaded),
        sizeof(loaded));
    if (diagnostic != nullptr) {
        // Pack the small live ammo counts into one diagnostic word. The
        // simulator probe only needs to distinguish empty/full/reserve and
        // both values are non-negative retail ammo counts.
        (*diagnostic)[7] =
            (std::clamp(reserve, 0, 0x7fff) << 16) |
            std::clamp(loaded, 0, 0xffff);
    }
    if (reserve < 0 || loaded < 0 || loaded > clip_size) {
        return snapshot;
    }
    snapshot = {
        .valid = true,
        .ammo_index = ammo_index,
        .clip_index = clip_index,
        .clip_size = clip_size,
        .reserve = reserve,
        .loaded = loaded,
    };
    return snapshot;
}

[[nodiscard]] bool detachable_magazine_can_reload(
    const DetachableMagazineAmmoSnapshot& snapshot) noexcept {
    return snapshot.valid && snapshot.reserve > 0 && snapshot.loaded >= 0 &&
        snapshot.loaded < snapshot.clip_size;
}

void log_simulator_detachable_magazine_commit_snapshot(
    const char* const point,
    const void* const player_state) noexcept {
    if (!simulator_detachable_magazine_probe_enabled() || point == nullptr ||
        !accessible_range(player_state, kPlayerStateMinimumSpan, false)) {
        return;
    }
    const auto* const player = static_cast<const std::uint8_t*>(player_state);
    std::int32_t weapon = 0;
    std::int32_t weapon_state = 0;
    std::memcpy(
        &weapon, player + kPlayerStateWeaponOffset, sizeof(weapon));
    std::memcpy(
        &weapon_state, player + kPlayerStateWeaponStateOffset,
        sizeof(weapon_state));

    std::int32_t ammo_index = -1;
    std::int32_t clip_index = -1;
    std::int32_t reserve = -1;
    std::int32_t loaded = -1;
    const std::uint32_t definition_address =
        g_active_weapon_definition_address.load(std::memory_order_acquire);
    const auto* const definition = reinterpret_cast<const std::uint8_t*>(
        static_cast<std::uintptr_t>(definition_address));
    if (definition_address != 0 &&
        accessible_range(
            definition, kWeaponDefinitionManualReloadSpan, false)) {
        std::memcpy(
            &ammo_index, definition + kWeaponDefinitionAmmoIndexOffset,
            sizeof(ammo_index));
        std::memcpy(
            &clip_index, definition + kWeaponDefinitionClipIndexOffset,
            sizeof(clip_index));
    }
    if (ammo_index >= 0 && clip_index >= 0 &&
        ammo_index < static_cast<std::int32_t>(kMaximumPlayerAmmoPools) &&
        clip_index < static_cast<std::int32_t>(kMaximumPlayerAmmoPools)) {
        std::memcpy(
            &reserve,
            player + kPlayerStateAmmoOffset +
                static_cast<std::size_t>(ammo_index) * sizeof(reserve),
            sizeof(reserve));
        std::memcpy(
            &loaded,
            player + kPlayerStateClipAmmoOffset +
                static_cast<std::size_t>(clip_index) * sizeof(loaded),
            sizeof(loaded));
    }
    stereo_diagnostic_log(
        "MagazineProbe commit %s ps=%p phase=%u weapon=%d state=%d pool=%d/%d ammo=%d/%d",
        point, player_state,
        static_cast<unsigned>(
            g_native_commit_phase.load(std::memory_order_acquire)),
        weapon, weapon_state, ammo_index, clip_index, reserve, loaded);
}

[[nodiscard]] bool hide_authored_clip(
    void* const dobj,
    const ClipSource& source,
    const std::int32_t weapon_index) noexcept {
    if (g_weapon_info_array == 0 || source.model == nullptr ||
        source.hide_bone_count < 2 ||
        source.hide_bone_count > source.hide_bones.size() ||
        weapon_index <= 0 ||
        !accessible_range(dobj, kDObjSize, true)) {
        return false;
    }
    auto* const info = reinterpret_cast<std::uint8_t*>(
        g_weapon_info_array +
        static_cast<std::size_t>(weapon_index) * kWeaponInfoStride);
    if (!accessible_range(
            info, kWeaponInfoPartBitsOffset + 4 * sizeof(std::uint32_t),
            true)) {
        return false;
    }
    void* owned = nullptr;
    std::memcpy(&owned, info, sizeof(owned));
    if (owned != dobj) {
        return false;
    }
    auto* const persistent = reinterpret_cast<std::uint32_t*>(
        info + kWeaponInfoPartBitsOffset);
    auto* const live = reinterpret_cast<std::uint32_t*>(
        static_cast<std::uint8_t*>(dobj) +
        kT4DObjHidePartBitsOffset);
    for (std::size_t index = 0; index < source.hide_bone_count; ++index) {
        const std::uint8_t bone = source.hide_bones[index];
        if (bone >= 128) {
            return false;
        }
        const std::size_t word = bone >> 5U;
        const std::uint32_t mask = 0x80000000U >> (bone & 31U);
        persistent[word] |= mask;
        live[word] |= mask;
    }
    return true;
}

void unpack_unit_vector(
    const std::uint32_t packed,
    float value[3]) noexcept {
    std::uint8_t bytes[4]{};
    std::memcpy(bytes, &packed, sizeof(bytes));
    const float scale =
        (static_cast<float>(bytes[3]) + 192.0F) / 32385.0F;
    value[0] = (static_cast<float>(bytes[0]) - 127.0F) * scale;
    value[1] = (static_cast<float>(bytes[1]) - 127.0F) * scale;
    value[2] = (static_cast<float>(bytes[2]) - 127.0F) * scale;
}

[[nodiscard]] std::uint32_t pack_unit_vector(
    const wawvr::xr::Vec3f& input) noexcept {
    const double length = std::sqrt(
        static_cast<double>(input.x) * input.x +
        static_cast<double>(input.y) * input.y +
        static_cast<double>(input.z) * input.z);
    if (!std::isfinite(length) || length <= 1.0e-8) {
        return 0;
    }
    const float normalized[3] = {
        static_cast<float>(input.x / length),
        static_cast<float>(input.y / length),
        static_cast<float>(input.z / length),
    };
    float best_error = (std::numeric_limits<float>::max)();
    std::uint32_t best = 0;
    for (unsigned exponent = 0; exponent < 256; ++exponent) {
        std::uint8_t bytes[4]{};
        bytes[3] = static_cast<std::uint8_t>(exponent);
        const float scale =
            32385.0F / (static_cast<float>(bytes[3]) + 192.0F);
        for (int component = 0; component < 3; ++component) {
            const int encoded = static_cast<int>(
                normalized[component] * scale + 127.5F);
            bytes[component] = static_cast<std::uint8_t>(
                (std::clamp)(encoded, 0, 255));
        }
        std::uint32_t candidate = 0;
        std::memcpy(&candidate, bytes, sizeof(candidate));
        float decoded[3]{};
        unpack_unit_vector(candidate, decoded);
        const double decoded_length = std::sqrt(
            static_cast<double>(decoded[0]) * decoded[0] +
            static_cast<double>(decoded[1]) * decoded[1] +
            static_cast<double>(decoded[2]) * decoded[2]);
        if (!std::isfinite(decoded_length) || decoded_length <= 0.0 ||
            std::abs(decoded_length - 1.0) >= 0.0010001) {
            continue;
        }
        const float error = static_cast<float>(std::abs(
            decoded[0] / decoded_length * normalized[0] +
            decoded[1] / decoded_length * normalized[1] +
            decoded[2] / decoded_length * normalized[2] - 1.0));
        if (error < best_error) {
            best_error = error;
            best = candidate;
        }
    }
    return best;
}

[[nodiscard]] bool rebase_vertex(
    const PackedVertex& source,
    const DObjAnimMat& bind,
    PackedVertex* const output) noexcept {
    if (output == nullptr) {
        return false;
    }
    const wawvr::xr::Quaternionf quaternion = wawvr::xr::Normalize({
        bind.quaternion[0], bind.quaternion[1],
        bind.quaternion[2], bind.quaternion[3],
    });
    if (!std::isfinite(quaternion.x) || !std::isfinite(quaternion.y) ||
        !std::isfinite(quaternion.z) || !std::isfinite(quaternion.w)) {
        return false;
    }
    const auto inverse = wawvr::xr::Conjugate(quaternion);
    *output = source;
    const auto position = wawvr::xr::Rotate(
        inverse,
        {source.xyz[0] - bind.translation[0],
         source.xyz[1] - bind.translation[1],
         source.xyz[2] - bind.translation[2]});
    if (!finite_vector(position)) {
        return false;
    }
    output->xyz[0] = position.x;
    output->xyz[1] = position.y;
    output->xyz[2] = position.z;
    for (std::uint32_t PackedVertex::* field :
         {&PackedVertex::normal, &PackedVertex::tangent}) {
        float decoded[3]{};
        unpack_unit_vector(source.*field, decoded);
        const auto rotated = wawvr::xr::Rotate(
            inverse, {decoded[0], decoded[1], decoded[2]});
        const std::uint32_t packed = pack_unit_vector(rotated);
        if (packed == 0) {
            return false;
        }
        output->*field = packed;
    }
    return true;
}

[[nodiscard]] bool build_clip_asset(
    const ClipSource& source,
    ClipAsset* const asset) {
    if (asset == nullptr || source.model == nullptr ||
        source.clip_surface >= source.model->surface_count ||
        source.round_surface >= source.model->surface_count ||
        source.clip_surface == source.round_surface ||
        source.model->surfaces == nullptr ||
        source.model->material_handles == nullptr ||
        source.model->base_matrices == nullptr ||
        !accessible_range(
            source.model->base_matrices,
            static_cast<std::size_t>(source.model->bone_count) *
                sizeof(DObjAnimMat),
            false)) {
        return false;
    }
    *asset = {};
    asset->source = source.model;
    asset->clip_surface = source.clip_surface;
    asset->round_surface = source.round_surface;
    const std::array<std::uint8_t, 2> surface_indices{
        source.round_surface, source.clip_surface};
    std::array<std::uint8_t, 2> bones{0xFF, 0xFF};
    for (std::size_t index = 0; index < surface_indices.size(); ++index) {
        const XSurface& original =
            source.model->surfaces[surface_indices[index]];
        if (original.vertex_count == 0 || original.vertex_count > 32767 ||
            original.vertices == nullptr || original.triangle_count == 0 ||
            original.rigid_vertex_list_count != 1 ||
            original.rigid_vertex_list == nullptr ||
            !accessible_range(
                original.vertices,
                static_cast<std::size_t>(original.vertex_count) *
                    sizeof(PackedVertex),
                false) ||
            !accessible_range(
                original.rigid_vertex_list, sizeof(RigidVertexList), false)) {
            return false;
        }
        const RigidVertexList& rigid = original.rigid_vertex_list[0];
        const std::size_t bone = rigid.bone_offset >> 6U;
        if ((rigid.bone_offset & 0x3FU) != 0 ||
            bone >= source.model->bone_count ||
            rigid.vertex_count != original.vertex_count) {
            return false;
        }
        bones[index] = static_cast<std::uint8_t>(bone);
        std::memcpy(
            &asset->materials[index],
            source.model->material_handles + surface_indices[index],
            sizeof(void*));
        asset->vertices[index].assign(
            original.vertices, original.vertices + original.vertex_count);
        asset->blend[index].assign(original.vertex_count, 0);
        asset->surfaces[index] = original;
        auto& cloned = asset->surfaces[index];
        cloned.deformed = true;
        cloned.vertices = asset->vertices[index].data();
        cloned.vertex_info.grouped_vertex_count[0] =
            static_cast<std::int16_t>(original.vertex_count);
        cloned.vertex_info.grouped_vertex_count[1] = 0;
        cloned.vertex_info.grouped_vertex_count[2] = 0;
        cloned.vertex_info.grouped_vertex_count[3] = 0;
        cloned.vertex_info.blend = asset->blend[index].data();
        cloned.rigid_vertex_list_count = 0;
        cloned.rigid_vertex_list = nullptr;
        cloned.part_bits[0] = static_cast<std::int32_t>(0x80000000U);
        cloned.part_bits[1] = 0;
        cloned.part_bits[2] = 0;
        cloned.part_bits[3] = 0;
    }

    const DObjAnimMat clip_bind = source.model->base_matrices[bones[1]];
    float minimums[3] = {
        (std::numeric_limits<float>::max)(),
        (std::numeric_limits<float>::max)(),
        (std::numeric_limits<float>::max)()};
    float maximums[3] = {
        (std::numeric_limits<float>::lowest)(),
        (std::numeric_limits<float>::lowest)(),
        (std::numeric_limits<float>::lowest)()};
    for (auto& vertices : asset->vertices) {
        for (auto& vertex : vertices) {
            PackedVertex rebased{};
            if (!rebase_vertex(vertex, clip_bind, &rebased)) {
                return false;
            }
            vertex = rebased;
            for (int component = 0; component < 3; ++component) {
                minimums[component] =
                    (std::min)(minimums[component], vertex.xyz[component]);
                maximums[component] =
                    (std::max)(maximums[component], vertex.xyz[component]);
            }
        }
    }
    float center[3]{};
    for (int component = 0; component < 3; ++component) {
        center[component] =
            0.5F * (minimums[component] + maximums[component]);
    }
    for (auto& vertices : asset->vertices) {
        for (auto& vertex : vertices) {
            for (int component = 0; component < 3; ++component) {
                vertex.xyz[component] -= center[component];
            }
        }
    }

    asset->base_matrices.assign(1, DObjAnimMat{});
    asset->base_matrices[0].quaternion[3] = 1.0F;
    asset->base_matrices[0].translation_weight = 2.0F;
    asset->model = *source.model;
    asset->model.bone_count = 1;
    asset->model.root_bone_count = 1;
    asset->model.surface_count = 2;
    asset->model.parent_list = nullptr;
    asset->model.base_matrices = asset->base_matrices.data();
    asset->model.surfaces = asset->surfaces.data();
    asset->model.material_handles = asset->materials.data();
    float radius_squared = 0.0F;
    for (auto& lod : asset->model.lod_info) {
        lod.surface_count = 2;
        lod.surface_index = 0;
        lod.part_bits[0] = static_cast<std::int32_t>(0x80000000U);
        lod.part_bits[1] = 0;
        lod.part_bits[2] = 0;
        lod.part_bits[3] = 0;
    }
    for (int component = 0; component < 3; ++component) {
        asset->model.minimums[component] =
            minimums[component] - center[component] - 0.25F;
        asset->model.maximums[component] =
            maximums[component] - center[component] + 0.25F;
        radius_squared += (std::max)(
            asset->model.minimums[component] *
                asset->model.minimums[component],
            asset->model.maximums[component] *
                asset->model.maximums[component]);
    }
    asset->model.radius = std::sqrt(radius_squared);

    DObjModelDescription description{};
    description.model = &asset->model;
    call_dobj_create(
        &description, 1, nullptr, asset->dobj.data(),
        kSyntheticDObjCreationEntity);
    XModel** created_slot = nullptr;
    XModel* created_model = nullptr;
    asset->ready = read_dobj_model_slot(
        asset->dobj.data(), &created_slot, &created_model) &&
        created_model == &asset->model;
    return asset->ready;
}

[[nodiscard]] bool build_magazine_asset(
    const MagazineSource& source,
    const DetachableMagazineWeaponProfile& profile,
    MagazineAsset* const asset) {
    const std::size_t piece_count =
        detachable_magazine_mesh_piece_count(profile.mesh);
    if (asset == nullptr || source.model == nullptr || piece_count == 0 ||
        piece_count > kMaximumDetachableMagazineMeshPieces ||
        source.piece_count != piece_count ||
        !magazine_source_piece_recipe_matches(profile, source) ||
        source.local_bone != profile.mesh.magazine_bone_index ||
        source.model->surfaces == nullptr ||
        source.model->material_handles == nullptr ||
        source.model->base_matrices == nullptr ||
        !accessible_range(
            source.model->base_matrices,
            static_cast<std::size_t>(source.model->bone_count) *
                sizeof(DObjAnimMat), false)) {
        return false;
    }

    std::array<InspectedMagazineMeshPiece,
               kMaximumDetachableMagazineMeshPieces>
        inspected_pieces{};
    for (std::size_t piece_index = 0; piece_index < piece_count;
         ++piece_index) {
        const auto piece_recipe =
            detachable_magazine_mesh_piece(profile, piece_index);
        if (source.piece_surfaces[piece_index] !=
                piece_recipe.source_surface_index ||
            source.piece_rigid_ranges[piece_index] !=
                piece_recipe.source_rigid_subrange_index ||
            !inspect_detachable_magazine_mesh_piece(
                source.model, source.local_bone, piece_recipe,
                &inspected_pieces[piece_index])) {
            return false;
        }
    }

    *asset = {};
    asset->source = source.model;
    asset->piece_count = static_cast<std::uint8_t>(piece_count);
    const DObjAnimMat bind = source.model->base_matrices[source.local_bone];
    float minimums[3]{
        (std::numeric_limits<float>::max)(),
        (std::numeric_limits<float>::max)(),
        (std::numeric_limits<float>::max)()};
    float maximums[3]{
        (std::numeric_limits<float>::lowest)(),
        (std::numeric_limits<float>::lowest)(),
        (std::numeric_limits<float>::lowest)()};
    for (std::size_t piece_index = 0; piece_index < piece_count;
         ++piece_index) {
        const auto& inspected = inspected_pieces[piece_index];
        XSurface& original = *inspected.surface;
        const std::size_t selected_first = inspected.rigid.first_vertex;
        const std::size_t selected_count = inspected.rigid.vertex_count;
        const std::size_t selected_end = selected_first + selected_count;
        const std::size_t triangle_prefix_count =
            static_cast<std::size_t>(inspected.rigid.triangle_offset) +
            inspected.rigid.triangle_count;
        if (selected_end > original.vertex_count || selected_end > 32767U ||
            triangle_prefix_count > original.triangle_count ||
            triangle_prefix_count >
                (std::numeric_limits<std::uint16_t>::max)()) {
            return false;
        }

        auto& vertices = asset->vertices[piece_index];
        auto& blend = asset->blend[piece_index];
        vertices.assign(selected_end, PackedVertex{});
        blend.assign(selected_end, 0);
        if ((reinterpret_cast<std::uintptr_t>(vertices.data()) & 0x0FU) !=
            0) {
            return false;
        }
        asset->materials[piece_index] = inspected.material;
        for (std::size_t index = 0; index < selected_count; ++index) {
            PackedVertex rebased{};
            if (!rebase_vertex(
                    original.vertices[selected_first + index], bind,
                    &rebased)) {
                return false;
            }
            vertices[selected_first + index] = rebased;
            for (int component = 0; component < 3; ++component) {
                minimums[component] = (std::min)(
                    minimums[component], rebased.xyz[component]);
                maximums[component] = (std::max)(
                    maximums[component], rebased.xyz[component]);
            }
        }
        // Cached rendering preserves the surface's authored D3D index-buffer
        // base. Preserve the selected prefix and collapse only vertices
        // preceding this piece's rigid range.
        for (std::size_t index = 0; index < selected_first; ++index) {
            vertices[index] = vertices[selected_first];
        }

        XSurface& detached = asset->surfaces[piece_index];
        detached = original;
        detached.deformed = true;
        detached.vertex_count =
            static_cast<std::uint16_t>(vertices.size());
        detached.triangle_count =
            static_cast<std::uint16_t>(triangle_prefix_count);
        detached.base_triangle_index = original.base_triangle_index;
        detached.base_vertex_index = 0;
        detached.triangle_indices = original.triangle_indices;
        detached.vertices = vertices.data();
        // Weighted CPU skinning consumes the custom vertices and writes a
        // dynamic VB. Retaining the static source VB would use weapon-space
        // coordinates; the authored index buffer remains valid for the kept
        // prefix.
        detached.vertex_buffer = nullptr;
        detached.index_buffer = original.index_buffer;
        detached.vertex_info.grouped_vertex_count[0] =
            static_cast<std::int16_t>(vertices.size());
        detached.vertex_info.grouped_vertex_count[1] = 0;
        detached.vertex_info.grouped_vertex_count[2] = 0;
        detached.vertex_info.grouped_vertex_count[3] = 0;
        detached.vertex_info.blend = blend.data();
        detached.rigid_vertex_list_count = 0;
        detached.rigid_vertex_list = nullptr;
        detached.part_bits[0] = static_cast<std::int32_t>(0x80000000U);
        detached.part_bits[1] = 0;
        detached.part_bits[2] = 0;
        detached.part_bits[3] = 0;
    }

    asset->base_matrices.assign(1, DObjAnimMat{});
    asset->base_matrices[0].quaternion[3] = 1.0F;
    asset->base_matrices[0].translation_weight = 2.0F;
    asset->model = *source.model;
    asset->model.bone_count = 1;
    asset->model.root_bone_count = 1;
    asset->model.surface_count = static_cast<std::uint8_t>(piece_count);
    asset->model.parent_list = nullptr;
    asset->model.base_matrices = asset->base_matrices.data();
    asset->model.surfaces = asset->surfaces.data();
    asset->model.material_handles = asset->materials.data();
    float radius_squared = 0.0F;
    for (auto& lod : asset->model.lod_info) {
        lod.surface_count = static_cast<std::uint16_t>(piece_count);
        lod.surface_index = 0;
        lod.part_bits[0] = static_cast<std::int32_t>(0x80000000U);
        lod.part_bits[1] = 0;
        lod.part_bits[2] = 0;
        lod.part_bits[3] = 0;
    }
    for (int component = 0; component < 3; ++component) {
        asset->model.minimums[component] =
            minimums[component] - 0.25F;
        asset->model.maximums[component] =
            maximums[component] + 0.25F;
        radius_squared += (std::max)(
            asset->model.minimums[component] *
                asset->model.minimums[component],
            asset->model.maximums[component] *
                asset->model.maximums[component]);
    }
    asset->model.radius = std::sqrt(radius_squared);

    DObjModelDescription description{};
    description.model = &asset->model;
    asset->dobj_create_attempted = true;
    call_dobj_create(
        &description, 1, nullptr, asset->dobj.data(),
        kSyntheticDObjCreationEntity);
    XModel** created_slot = nullptr;
    XModel* created_model = nullptr;
    asset->ready = read_dobj_model_slot(
        asset->dobj.data(), &created_slot, &created_model) &&
        created_model == &asset->model;
    return asset->ready;
}

[[nodiscard]] bool axis_to_angles(
    const wawvr::xr::Basis3f& axis,
    float angles[3]) noexcept {
    if (angles == nullptr || !valid_basis(axis)) {
        return false;
    }
    constexpr double kDegrees = 57.29577951308232;
    const double horizontal = std::sqrt(
        static_cast<double>(axis.forward.x) * axis.forward.x +
        static_cast<double>(axis.forward.y) * axis.forward.y);
    angles[0] = static_cast<float>(
        std::atan2(-static_cast<double>(axis.forward.z), horizontal) *
        kDegrees);
    angles[1] = horizontal <= 1.0e-8
        ? 0.0F
        : static_cast<float>(
              std::atan2(
                  static_cast<double>(axis.forward.y),
                  static_cast<double>(axis.forward.x)) * kDegrees);
    angles[2] = static_cast<float>(
        std::atan2(
            static_cast<double>(axis.left.z),
            static_cast<double>(axis.up.z)) * kDegrees);
    return std::isfinite(angles[0]) && std::isfinite(angles[1]) &&
           std::isfinite(angles[2]);
}

[[nodiscard]] bool render_clip(
    ClipAsset* const asset,
    const wawvr::xr::EnginePose& hand_world,
    const wawvr::xr::Vec3f& lighting_origin,
    const void* const viewmodel_pose) noexcept {
    if (asset == nullptr || !asset->ready ||
        !finite_vector(hand_world.position) || !valid_basis(hand_world.axis) ||
        !accessible_range(viewmodel_pose, kPoseSize, false)) {
        return false;
    }
    std::memcpy(asset->pose.data(), viewmodel_pose, kPoseSize);
    const float origin[3] = {
        hand_world.position.x, hand_world.position.y, hand_world.position.z};
    float angles[3]{};
    if (!axis_to_angles(hand_world.axis, angles)) {
        return false;
    }
    std::memcpy(asset->pose.data() + kPoseOriginOffset, origin, sizeof(origin));
    std::memcpy(asset->pose.data() + kPoseAnglesOffset, angles, sizeof(angles));
    float light[3] = {
        lighting_origin.x, lighting_origin.y, lighting_origin.z};
    std::uint32_t scene_entity = kNoSyntheticSceneLease;
    if (!find_available_synthetic_scene_slot(
            g_scene_index_pointers, &scene_entity)) {
        return false;
    }
    call_add_dobj_to_scene(
        asset->pose.data(), asset->dobj.data(), scene_entity, light,
        kClipRenderFlags);
    return true;
}

[[nodiscard]] bool render_magazine(
    MagazineAsset* const asset,
    const wawvr::xr::EnginePose& world,
    const wawvr::xr::Vec3f& lighting_origin,
    const void* const viewmodel_pose) noexcept {
    if (asset == nullptr || !asset->ready ||
        !finite_vector(world.position) || !valid_basis(world.axis) ||
        !accessible_range(viewmodel_pose, kPoseSize, false)) {
        return false;
    }
    std::memcpy(asset->pose.data(), viewmodel_pose, kPoseSize);
    const float origin[3]{
        world.position.x, world.position.y, world.position.z};
    float angles[3]{};
    if (!axis_to_angles(world.axis, angles)) {
        return false;
    }
    std::memcpy(asset->pose.data() + kPoseOriginOffset, origin, sizeof(origin));
    std::memcpy(asset->pose.data() + kPoseAnglesOffset, angles, sizeof(angles));
    float light[3]{
        lighting_origin.x, lighting_origin.y, lighting_origin.z};
    std::uint32_t scene_entity = kNoSyntheticSceneLease;
    if (!find_available_synthetic_scene_slot(
            g_scene_index_pointers, &scene_entity)) {
        return false;
    }
    call_add_dobj_to_scene(
        asset->pose.data(), asset->dobj.data(), scene_entity, light,
        kClipRenderFlags);
    return true;
}

[[nodiscard]] ClipAsset* find_or_build_clip_asset(
    const BoltActionWeaponProfileId profile_id,
    const ClipSource& source,
    const std::uint64_t source_generation) {
    for (auto& cached : g_clip_asset_cache) {
        if (cached.occupied && cached.profile_id == profile_id &&
            cached.source_generation == source_generation &&
            cached.source_model == source.model && source.model != nullptr &&
            cached.source_surfaces == source.model->surfaces &&
            cached.source_material_handles == source.model->material_handles &&
            cached.source_base_matrices == source.model->base_matrices &&
            cached.clip_surface == source.clip_surface &&
            cached.round_surface == source.round_surface) {
            return &cached.asset;
        }
    }

    CachedClipAsset* slot = nullptr;
    for (auto& cached : g_clip_asset_cache) {
        if (!cached.occupied) {
            slot = &cached;
            break;
        }
    }
    if (source.model == nullptr || source_generation == 0) {
        return nullptr;
    }
    if (slot == nullptr) {
        g_clip_asset_cache.emplace_back();
        slot = &g_clip_asset_cache.back();
    }

    // Publish the cache key only after the build returns. If allocation throws,
    // the outer runtime guard fails closed and this never-submitted slot can be
    // rebuilt safely on a later frame.
    if (!build_clip_asset(source, &slot->asset)) {
        slot->asset.failed = true;
        return nullptr;
    }
    slot->profile_id = profile_id;
    slot->source_generation = source_generation;
    slot->source_model = source.model;
    slot->source_surfaces = source.model->surfaces;
    slot->source_material_handles = source.model->material_handles;
    slot->source_base_matrices = source.model->base_matrices;
    slot->clip_surface = source.clip_surface;
    slot->round_surface = source.round_surface;
    slot->occupied = true;
    return &slot->asset;
}

[[nodiscard]] bool cached_magazine_asset_matches_source(
    const CachedMagazineAsset& cached,
    const DetachableMagazineWeaponProfileId profile_id,
    const MagazineSource& source) noexcept {
    if (!cached.occupied || cached.profile_id != profile_id ||
        source.model == nullptr || source.model->surfaces == nullptr ||
        cached.source_model != source.model ||
        cached.source_surfaces != source.model->surfaces ||
        cached.source_material_handles != source.model->material_handles ||
        cached.source_base_matrices != source.model->base_matrices ||
        cached.piece_count != source.piece_count || source.piece_count == 0 ||
        source.piece_count > kMaximumDetachableMagazineMeshPieces ||
        cached.asset_fingerprint != source.asset_fingerprint) {
        return false;
    }
    for (std::size_t index = 0; index < source.piece_count; ++index) {
        const std::uint8_t surface_index = source.piece_surfaces[index];
        if (surface_index >= source.model->surface_count ||
            cached.source_piece_surfaces[index] != surface_index ||
            cached.source_rigid_ranges[index] !=
                source.piece_rigid_ranges[index]) {
            return false;
        }
        const XSurface& live_surface =
            source.model->surfaces[surface_index];
        if (cached.source_vertices[index] != live_surface.vertices ||
            cached.source_triangle_indices[index] !=
                live_surface.triangle_indices ||
            cached.source_vertex_buffers[index] !=
                live_surface.vertex_buffer ||
            cached.source_index_buffers[index] != live_surface.index_buffer ||
            cached.source_zone_handles[index] != live_surface.zone_handle) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] MagazineAsset* find_or_build_magazine_asset(
    const DetachableMagazineWeaponProfile& profile,
    const MagazineSource& source) {
    for (auto& cached : g_magazine_asset_cache) {
        if (cached_magazine_asset_matches_source(
                cached, profile.id, source)) {
            return &cached.asset;
        }
    }
    if (source.model == nullptr || source.asset_fingerprint == 0 ||
        source.piece_count == 0 ||
        source.piece_count > kMaximumDetachableMagazineMeshPieces) {
        return nullptr;
    }
    CachedMagazineAsset* slot = nullptr;
    for (auto& cached : g_magazine_asset_cache) {
        if (!cached.occupied) {
            slot = &cached;
            break;
        }
    }
    if (slot == nullptr) {
        g_magazine_asset_cache.emplace_back();
        slot = &g_magazine_asset_cache.back();
    }
    const bool built = build_magazine_asset(source, profile, &slot->asset);
    if (!built) {
        slot->asset.failed = true;
        // DObjCreate may allocate engine-owned process-lifetime state even if
        // its postcondition cannot be confirmed. Preserve and identity-key that
        // node instead of overwriting its DObj storage on the next frame. The
        // matching failed entry prevents repeated creation attempts.
        if (!slot->asset.dobj_create_attempted) {
            return nullptr;
        }
    }
    if (source.model == nullptr || source.model->surfaces == nullptr) {
        return nullptr;
    }
    slot->profile_id = profile.id;
    slot->source_model = source.model;
    slot->source_surfaces = source.model->surfaces;
    slot->source_material_handles = source.model->material_handles;
    slot->source_base_matrices = source.model->base_matrices;
    slot->piece_count = source.piece_count;
    for (std::size_t index = 0; index < source.piece_count; ++index) {
        const std::uint8_t surface_index = source.piece_surfaces[index];
        if (surface_index >= source.model->surface_count) {
            return nullptr;
        }
        const XSurface& source_surface =
            source.model->surfaces[surface_index];
        slot->source_vertices[index] = source_surface.vertices;
        slot->source_triangle_indices[index] =
            source_surface.triangle_indices;
        slot->source_vertex_buffers[index] = source_surface.vertex_buffer;
        slot->source_index_buffers[index] = source_surface.index_buffer;
        slot->source_piece_surfaces[index] = surface_index;
        slot->source_rigid_ranges[index] =
            source.piece_rigid_ranges[index];
        slot->source_zone_handles[index] = source_surface.zone_handle;
    }
    slot->asset_fingerprint = source.asset_fingerprint;
    slot->occupied = true;
    return built ? &slot->asset : nullptr;
}

[[nodiscard]] bool read_bone_matrix(
    void* const dobj,
    const std::uint8_t bone,
    DObjAnimMat* const matrix) noexcept {
    if (matrix == nullptr || bone >= 0xFE ||
        !accessible_range(dobj, kDObjSize, false)) {
        return false;
    }
    auto* const bytes = static_cast<std::uint8_t*>(dobj);
    std::uint8_t bone_count = 0;
    DObjAnimMat* matrices = nullptr;
    std::memcpy(&bone_count, bytes + kDObjNumBonesOffset, sizeof(bone_count));
    std::memcpy(&matrices, bytes + kDObjSkelMatOffset, sizeof(matrices));
    if (bone >= bone_count || matrices == nullptr ||
        !accessible_range(
            matrices,
            (static_cast<std::size_t>(bone) + 1) * sizeof(DObjAnimMat),
            false)) {
        return false;
    }
    *matrix = matrices[bone];
    return true;
}

[[nodiscard]] bool normalize_quaternion(
    const float input[4],
    float output[4]) noexcept {
    if (input == nullptr || output == nullptr ||
        !std::isfinite(input[0]) || !std::isfinite(input[1]) ||
        !std::isfinite(input[2]) || !std::isfinite(input[3])) {
        return false;
    }
    const double length_squared =
        static_cast<double>(input[0]) * input[0] +
        static_cast<double>(input[1]) * input[1] +
        static_cast<double>(input[2]) * input[2] +
        static_cast<double>(input[3]) * input[3];
    if (!std::isfinite(length_squared) || length_squared < 1.0e-8) {
        return false;
    }
    const float inverse_length =
        static_cast<float>(1.0 / std::sqrt(length_squared));
    for (int component = 0; component < 4; ++component) {
        output[component] = input[component] * inverse_length;
    }
    return true;
}

void multiply_quaternions(
    const float left[4],
    const float right[4],
    float output[4]) noexcept {
    output[0] = left[3] * right[0] + left[0] * right[3] +
        left[1] * right[2] - left[2] * right[1];
    output[1] = left[3] * right[1] - left[0] * right[2] +
        left[1] * right[3] + left[2] * right[0];
    output[2] = left[3] * right[2] + left[0] * right[1] -
        left[1] * right[0] + left[2] * right[3];
    output[3] = left[3] * right[3] - left[0] * right[0] -
        left[1] * right[1] - left[2] * right[2];
}

void rotate_by_quaternion(
    const float quaternion[4],
    const float input[3],
    float output[3]) noexcept {
    const float tx = 2.0F *
        (quaternion[1] * input[2] - quaternion[2] * input[1]);
    const float ty = 2.0F *
        (quaternion[2] * input[0] - quaternion[0] * input[2]);
    const float tz = 2.0F *
        (quaternion[0] * input[1] - quaternion[1] * input[0]);
    output[0] = input[0] + quaternion[3] * tx +
        (quaternion[1] * tz - quaternion[2] * ty);
    output[1] = input[1] + quaternion[3] * ty +
        (quaternion[2] * tx - quaternion[0] * tz);
    output[2] = input[2] + quaternion[3] * tz +
        (quaternion[0] * ty - quaternion[1] * tx);
}

[[nodiscard]] bool capture_bolt_relative_pose(
    void* const dobj,
    const std::uint8_t parent_bone,
    const std::uint8_t bolt_bone,
    BoltRelativePose* const relative) noexcept {
    if (relative == nullptr) {
        return false;
    }
    DObjAnimMat parent{};
    DObjAnimMat bolt{};
    float parent_quaternion[4]{};
    float bolt_quaternion[4]{};
    if (!read_bone_matrix(dobj, parent_bone, &parent) ||
        !read_bone_matrix(dobj, bolt_bone, &bolt) ||
        !normalize_quaternion(parent.quaternion, parent_quaternion) ||
        !normalize_quaternion(bolt.quaternion, bolt_quaternion)) {
        *relative = {};
        return false;
    }
    const float inverse_parent[4]{
        -parent_quaternion[0], -parent_quaternion[1],
        -parent_quaternion[2], parent_quaternion[3],
    };
    multiply_quaternions(
        inverse_parent, bolt_quaternion, relative->quaternion);
    const float delta[3]{
        bolt.translation[0] - parent.translation[0],
        bolt.translation[1] - parent.translation[1],
        bolt.translation[2] - parent.translation[2],
    };
    rotate_by_quaternion(inverse_parent, delta, relative->translation);
    relative->translation_weight = bolt.translation_weight;
    relative->valid = std::isfinite(relative->translation[0]) &&
        std::isfinite(relative->translation[1]) &&
        std::isfinite(relative->translation[2]);
    return relative->valid;
}

[[nodiscard]] bool compose_bolt_from_relative(
    const DObjAnimMat& parent,
    const BoltRelativePose& relative,
    DObjAnimMat* const bolt) noexcept {
    if (bolt == nullptr || !relative.valid) {
        return false;
    }
    float parent_quaternion[4]{};
    if (!normalize_quaternion(parent.quaternion, parent_quaternion)) {
        return false;
    }
    multiply_quaternions(
        parent_quaternion, relative.quaternion, bolt->quaternion);
    float normalized_bolt[4]{};
    if (!normalize_quaternion(bolt->quaternion, normalized_bolt)) {
        return false;
    }
    std::memcpy(
        bolt->quaternion, normalized_bolt, sizeof(normalized_bolt));
    float rotated_translation[3]{};
    rotate_by_quaternion(
        parent_quaternion, relative.translation, rotated_translation);
    for (int component = 0; component < 3; ++component) {
        bolt->translation[component] =
            parent.translation[component] + rotated_translation[component];
    }
    bolt->translation_weight = relative.translation_weight;
    return std::isfinite(bolt->translation[0]) &&
           std::isfinite(bolt->translation[1]) &&
           std::isfinite(bolt->translation[2]);
}

[[nodiscard]] bool apply_magazine_feed_door_pose(
    const std::span<DObjAnimMat> matrices,
    const DetachableMagazineWeaponProfile& profile,
    const MagazineSource& source, const bool open) noexcept {
    if (!profile.feed_door.enabled || source.feed_door_surface == nullptr ||
        matrices.data() == nullptr ||
        source.feed_door.bone >= matrices.size() ||
        source.feed_door.parent_bone >= matrices.size()) {
        return false;
    }
    const auto& pose = open ? profile.feed_door.open_pose : profile.feed_door.closed_pose;
    BoltRelativePose relative{};
    relative.quaternion[0] = pose.orientation.x;
    relative.quaternion[1] = pose.orientation.y;
    relative.quaternion[2] = pose.orientation.z;
    relative.quaternion[3] = pose.orientation.w;
    relative.translation[0] = pose.translation.x;
    relative.translation[1] = pose.translation.y;
    relative.translation[2] = pose.translation.z;
    relative.translation_weight = 2.0F;
    relative.valid = true;
    return compose_bolt_from_relative(matrices[source.feed_door.parent_bone],
                                      relative, &matrices[source.feed_door.bone]);
}

[[nodiscard]] bool calculate_controlled_linear_action_pose(
    const std::span<const DObjAnimMat> matrices,
    const float travel_units,
    const std::uint8_t root_bone,
    const MovingBoltSource& source,
    const float fraction,
    const BoltRelativePose& closed_relative,
    DObjAnimMat* const output) noexcept {
    if (output == nullptr || !closed_relative.valid) {
        return false;
    }
    DObjAnimMat current_root{};
    DObjAnimMat current_parent{};
    DObjAnimMat controlled{};
    if (!copy_controlled_action_matrices(
            matrices, root_bone, source.parent_bone, source.bone,
            &current_root, &current_parent, &controlled)) {
        return false;
    }
    if (!compose_bolt_from_relative(
            current_parent, closed_relative, &controlled)) {
        return false;
    }
    Kar98BoltVector travel{};
    if (!calculate_linear_action_parent_travel(
            travel_units, current_root.quaternion, fraction, &travel)) {
        return false;
    }
    controlled.translation[0] += travel.x;
    controlled.translation[1] += travel.y;
    controlled.translation[2] += travel.z;
    controlled.translation_weight = 2.0F;
    *output = controlled;
    return std::isfinite(output->translation[0]) &&
           std::isfinite(output->translation[1]) &&
           std::isfinite(output->translation[2]);
}

[[nodiscard]] bool calculate_controlled_bolt_pose(
    const std::span<const DObjAnimMat> matrices,
    const BoltActionWeaponProfile& profile,
    const std::uint8_t root_bone,
    const MovingBoltSource& source,
    const float fraction,
    const BoltRelativePose& closed_relative,
    DObjAnimMat* const output) noexcept {
    return calculate_controlled_linear_action_pose(
        matrices, profile.bolt_travel_units, root_bone, source, fraction,
        closed_relative, output);
}

[[nodiscard]] bool apply_charging_handle_pose(
    const std::span<DObjAnimMat> matrices,
    const DetachableMagazineWeaponProfile& profile,
    const MagazineSource& source,
    const float fraction,
    const BoltRelativePose& closed_relative) noexcept {
    if (!profile.charging.enabled || source.model == nullptr ||
        source.charging_piece_count == 0 ||
        source.charging_surface == nullptr) {
        return false;
    }
    DObjAnimMat controlled{};
    if (!calculate_controlled_linear_action_pose(
            matrices, profile.charging.interaction.travel_units,
            source.root_bone, source.charging_handle, fraction,
            closed_relative, &controlled)) {
        return false;
    }
    std::memcpy(
        &matrices[source.charging_handle.bone],
        &controlled, sizeof(controlled));
    return true;
}

[[nodiscard]] bool read_controlled_charging_handle_world(
    void* const viewmodel_dobj,
    const void* const viewmodel_pose,
    const DetachableMagazineWeaponProfile& profile,
    const MagazineSource& source,
    const float fraction,
    const BoltRelativePose& closed_relative,
    wawvr::xr::Vec3f* const world_position) noexcept {
    if (world_position == nullptr ||
        !accessible_range(viewmodel_dobj, kDObjSize, false)) {
        return false;
    }
    auto* const live_bytes = static_cast<std::uint8_t*>(viewmodel_dobj);
    std::uint8_t bone_count = 0;
    DObjAnimMat* live_matrices = nullptr;
    std::memcpy(
        &bone_count, live_bytes + kDObjNumBonesOffset,
        sizeof(bone_count));
    std::memcpy(
        &live_matrices, live_bytes + kDObjSkelMatOffset,
        sizeof(live_matrices));
    if (bone_count == 0 || bone_count > g_pre_skin_scratch.matrices.size() ||
        live_matrices == nullptr ||
        !accessible_range(
            live_matrices,
            static_cast<std::size_t>(bone_count) * sizeof(DObjAnimMat),
            false)) {
        return false;
    }
    std::memcpy(
        g_pre_skin_scratch.dobj.data(), viewmodel_dobj, kDObjSize);
    std::memcpy(
        g_pre_skin_scratch.matrices.data(), live_matrices,
        static_cast<std::size_t>(bone_count) * sizeof(DObjAnimMat));
    DObjAnimMat* const private_matrices =
        g_pre_skin_scratch.matrices.data();
    std::memcpy(
        g_pre_skin_scratch.dobj.data() + kDObjSkelMatOffset,
        &private_matrices, sizeof(private_matrices));
    return apply_charging_handle_pose(
               {private_matrices, bone_count}, profile, source, fraction,
               closed_relative) &&
        read_viewmodel_world_tag_position(
            g_pre_skin_scratch.dobj.data(), source.charging_handle.tag,
            viewmodel_pose, world_position);
}

[[nodiscard]] bool apply_moving_bolt_poses_to_matrices(
    const std::span<DObjAnimMat> matrices,
    const BoltActionWeaponProfile& profile,
    const ClipSource& source,
    const float fraction,
    const std::array<BoltRelativePose, kMaximumMovingBoltTags>&
        closed_relatives) noexcept {
    if (source.moving_bolt_count == 0 ||
        source.moving_bolt_count != profile.moving_bolt_tag_count ||
        source.moving_bolt_count > kMaximumMovingBoltTags ||
        matrices.data() == nullptr || matrices.empty()) {
        return false;
    }
    std::array<DObjAnimMat, kMaximumMovingBoltTags> controlled{};
    for (std::size_t index = 0; index < source.moving_bolt_count; ++index) {
        if (!calculate_controlled_bolt_pose(
                matrices, profile, source.root_bone,
                source.moving_bolts[index], fraction,
                closed_relatives[index], &controlled[index])) {
            return false;
        }
    }
    // Publish all authored bolt pieces only after every independent closed pose
    // has validated. A failure can never leave the live DObj half manual.
    for (std::size_t index = 0; index < source.moving_bolt_count; ++index) {
        std::memcpy(
            &matrices[source.moving_bolts[index].bone],
            &controlled[index], sizeof(controlled[index]));
    }
    return true;
}

[[nodiscard]] bool apply_moving_bolt_poses(
    void* const dobj,
    const BoltActionWeaponProfile& profile,
    const ClipSource& source,
    const float fraction,
    const std::array<BoltRelativePose, kMaximumMovingBoltTags>&
        closed_relatives) noexcept {
    // Fresh native range/protection checks at the boundary; no native callback
    // occurs while the individual pieces consume this same bounded span.
    if (!accessible_range(dobj, kDObjSize, false)) return false;
    auto* const bytes = static_cast<std::uint8_t*>(dobj);
    std::uint8_t bone_count = 0;
    DObjAnimMat* matrices = nullptr;
    std::memcpy(&bone_count, bytes + kDObjNumBonesOffset, sizeof(bone_count));
    std::memcpy(&matrices, bytes + kDObjSkelMatOffset, sizeof(matrices));
    if (bone_count == 0 || matrices == nullptr ||
        !accessible_range(
            matrices, static_cast<std::size_t>(bone_count) *
                sizeof(DObjAnimMat), true)) {
        return false;
    }
    return apply_moving_bolt_poses_to_matrices(
        {matrices, bone_count}, profile, source, fraction, closed_relatives);
}

[[nodiscard]] bool cached_bolt_source_matches_dobj(
    void* const dobj,
    const BoltActionWeaponProfile& profile,
    const ClipSource& source) noexcept {
    if (source.model == nullptr || source.root_bone >= 128U ||
        source.moving_bolt_count == 0 ||
        source.moving_bolt_count != profile.moving_bolt_tag_count ||
        source.moving_bolt_count > kMaximumMovingBoltTags ||
        !accessible_range(dobj, kDObjSize, false) ||
        !accessible_range(source.model, sizeof(*source.model), false) ||
        !c_string_equals(
            source.model->name, profile.viewmodel_model_name)) {
        return false;
    }
    auto* const bytes = static_cast<std::uint8_t*>(dobj);
    std::uint8_t model_count = 0;
    std::uint8_t total_bones = 0;
    XModel** models = nullptr;
    std::memcpy(
        &model_count, bytes + kDObjNumModelsOffset,
        sizeof(model_count));
    std::memcpy(
        &total_bones, bytes + kDObjNumBonesOffset,
        sizeof(total_bones));
    std::memcpy(&models, bytes + kDObjModelsOffset, sizeof(models));
    if (model_count == 0 || models == nullptr ||
        !accessible_range(
            models, static_cast<std::size_t>(model_count) * sizeof(*models),
            false)) {
        return false;
    }
    std::size_t preceding_bones = 0;
    bool attached = false;
    for (std::size_t index = 0; index < model_count; ++index) {
        XModel* candidate = nullptr;
        std::memcpy(&candidate, models + index, sizeof(candidate));
        if (candidate == source.model) {
            attached = true;
            break;
        }
        if (candidate == nullptr ||
            !accessible_range(candidate, sizeof(*candidate), false)) {
            return false;
        }
        preceding_bones += candidate->bone_count;
    }
    if (!attached || preceding_bones != source.root_bone) {
        return false;
    }
    if (source.model->bone_names == nullptr ||
        !accessible_range(
            source.model->bone_names,
            static_cast<std::size_t>(source.model->bone_count) *
                sizeof(std::uint16_t), false)) {
        return false;
    }
    for (std::size_t index = 0; index < source.moving_bolt_count; ++index) {
        const MovingBoltSource& moving = source.moving_bolts[index];
        if (moving.tag == 0 || moving.bone < preceding_bones ||
            moving.parent_bone < preceding_bones ||
            moving.bone >= total_bones ||
            moving.parent_bone >= total_bones) {
            return false;
        }
        const std::size_t local_bolt = moving.bone - preceding_bones;
        const std::size_t local_parent = moving.parent_bone - preceding_bones;
        // The exact profile tag was resolved while this source snapshot was
        // discovered. At the render-backend seam, revalidate only immutable
        // model ownership; do not call the engine script-string service from
        // the backend thread.
        if (local_bolt >= source.model->bone_count ||
            local_parent >= source.model->bone_count ||
            source.model->bone_names[local_bolt] != moving.tag ||
            model_bone_parent(
                *source.model, static_cast<int>(local_bolt)) !=
                static_cast<int>(local_parent)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool is_cached_bolt_surface(
    const ClipSource& source,
    const std::size_t moving_bolt_index,
    const XSurface* const surface) noexcept {
    if (surface == nullptr || source.model == nullptr ||
        source.model->surfaces == nullptr ||
        source.model->surface_count == 0 ||
        source.model->surface_count > 64 ||
        moving_bolt_index >= source.moving_bolt_count ||
        source.moving_bolts[moving_bolt_index].bone < source.root_bone) {
        return false;
    }
    const auto surface_count =
        static_cast<std::size_t>(source.model->surface_count);
    if (!accessible_range(
            source.model->surfaces,
            surface_count * sizeof(XSurface), false)) {
        return false;
    }
    bool belongs_to_model = false;
    for (std::size_t index = 0; index < surface_count; ++index) {
        if (surface == source.model->surfaces + index) {
            belongs_to_model = true;
            break;
        }
    }
    if (!belongs_to_model || surface->deformed ||
        surface->vertex_count == 0 ||
        surface->rigid_vertex_list_count != 1 ||
        surface->rigid_vertex_list == nullptr ||
        !accessible_range(
            surface->rigid_vertex_list, sizeof(RigidVertexList), false)) {
        return false;
    }

    const auto local_bolt = static_cast<std::uint32_t>(
        source.moving_bolts[moving_bolt_index].bone - source.root_bone);
    if (local_bolt >= source.model->bone_count || local_bolt >= 128U) {
        return false;
    }
    const std::uint32_t mask = 0x80000000U >> (local_bolt & 31U);
    const auto part_bits = reinterpret_cast<const std::uint32_t*>(
        surface->part_bits);
    const RigidVertexList& rigid = surface->rigid_vertex_list[0];
    const auto rigid_bone = static_cast<std::uint32_t>(
        rigid.bone_offset >> 6U);
    if ((rigid.bone_offset & 0x3FU) != 0 ||
        rigid.vertex_count != surface->vertex_count ||
        rigid_bone != local_bolt) {
        return false;
    }
    for (std::size_t word = 0; word < 4; ++word) {
        const std::uint32_t expected =
            word == (local_bolt >> 5U) ? mask : 0U;
        if (part_bits[word] != expected) {
            return false;
        }
    }
    return true;
}

// R_PreSkinXSurface consumes rigid matrices synchronously and retains no DObj
// pointer. Give only the active bolt surface a private DObj/matrix snapshot,
// leaving gameplay animation state and every other surface untouched.
extern "C" void* __cdecl select_kar98_bolt_pre_skin_dobj(
    void* const viewmodel_dobj,
    const XSurface* const surface) noexcept {
    if (!g_enabled.load(std::memory_order_acquire) ||
        viewmodel_dobj == nullptr || surface == nullptr) {
        return viewmodel_dobj;
    }
    // This callsite is global. Reject every world/entity surface without
    // touching the manual-reload mutex; only the one published active bolt
    // surface proceeds to the locked publication and active-identity checks
    // below. Bolt surfaces then map directly back to the exact cached source
    // slot; magazine charging surfaces retain their deeper recipe validation.
    std::size_t published_bolt_surface_index =
        g_pre_skin_bolt_surfaces.size();
    for (std::size_t index = 0;
         index < g_pre_skin_bolt_surfaces.size(); ++index) {
        if (surface == g_pre_skin_bolt_surfaces[index].load(
                           std::memory_order_acquire)) {
            published_bolt_surface_index = index;
            break;
        }
    }
    if (viewmodel_dobj !=
            g_pre_skin_viewmodel_dobj.load(std::memory_order_acquire) ||
        published_bolt_surface_index >= g_pre_skin_bolt_surfaces.size()) {
        return viewmodel_dobj;
    }

    bool sealed = false;
    const char* profile_diagnostic_name = "bolt-action";
    float fraction = 0.0F;
    bool bolt_grabbed = false;
    float translation_delta = 0.0F;
    float quaternion_delta_same_sign = 0.0F;
    float quaternion_delta_opposite_sign = 0.0F;
    try {
        const std::lock_guard<std::mutex> lock(g_state_mutex);
        const bool still_published_bolt_surface =
            published_bolt_surface_index <
                g_pre_skin_bolt_surfaces.size() &&
            surface ==
                g_pre_skin_bolt_surfaces[published_bolt_surface_index].load(
                    std::memory_order_acquire);
        const std::uint64_t magazine_packed =
            g_active_magazine_binding.load(std::memory_order_acquire);
        if (magazine_packed != 0) {
            DetachableMagazineWeaponBinding magazine_binding{};
            const DetachableMagazineWeaponProfile* const magazine_profile =
                profile_for_magazine_binding(
                    magazine_packed, &magazine_binding);
            if (!g_enabled.load(std::memory_order_acquire) ||
                !g_supported.load(std::memory_order_acquire) ||
                viewmodel_dobj != g_pre_skin_viewmodel_dobj.load(
                    std::memory_order_acquire) ||
                !still_published_bolt_surface ||
                magazine_profile == nullptr ||
                !magazine_profile->charging.enabled ||
                g_magazine_visual_state.profile_id !=
                    magazine_profile->id ||
                g_magazine_visual_state.weapon_index !=
                    magazine_binding.weapon_index ||
                g_magazine_visual_state.viewmodel_dobj != viewmodel_dobj ||
                !g_magazine_visual_state
                     .closed_charging_handle_pose_latched) {
                return viewmodel_dobj;
            }
            const MagazineSource source = g_magazine_visual_state.source;
            const bool is_feed_door = magazine_profile->feed_door.enabled &&
                surface == source.feed_door_surface;
            const bool charging_visual_active = is_feed_door ||
                magazine_profile->charging.suppress_native_pose_always ||
                g_magazine_charging_state.charge_required ||
                g_magazine_charging_state.handle_grabbed ||
                g_magazine_charging_state.spring_returning ||
                g_magazine_charging_state.awaiting_controls_release ||
                g_magazine_charging_state.handle_fraction > 0.0F;
            if (!charging_visual_active) {
                return viewmodel_dobj;
            }
            if ((!is_feed_door && !is_cached_magazine_charging_surface(source, surface)) ||
                !cached_magazine_source_matches_dobj(
                    viewmodel_dobj, *magazine_profile, source)) {
                return viewmodel_dobj;
            }
            auto* const live_bytes =
                static_cast<std::uint8_t*>(viewmodel_dobj);
            std::uint8_t bone_count = 0;
            DObjAnimMat* live_matrices = nullptr;
            std::memcpy(
                &bone_count, live_bytes + kDObjNumBonesOffset,
                sizeof(bone_count));
            std::memcpy(
                &live_matrices, live_bytes + kDObjSkelMatOffset,
                sizeof(live_matrices));
            if (bone_count == 0 ||
                bone_count > g_pre_skin_scratch.matrices.size() ||
                (is_feed_door ? source.feed_door.bone : source.charging_handle.bone) >= bone_count ||
                live_matrices == nullptr ||
                !accessible_range(viewmodel_dobj, kDObjSize, false) ||
                !accessible_range(
                    live_matrices,
                    static_cast<std::size_t>(bone_count) *
                        sizeof(DObjAnimMat), false)) {
                return viewmodel_dobj;
            }
            std::memcpy(
                g_pre_skin_scratch.dobj.data(), viewmodel_dobj, kDObjSize);
            std::memcpy(
                g_pre_skin_scratch.matrices.data(), live_matrices,
                static_cast<std::size_t>(bone_count) *
                    sizeof(DObjAnimMat));
            DObjAnimMat* const snapshot_matrices =
                g_pre_skin_scratch.matrices.data();
            std::memcpy(
                g_pre_skin_scratch.dobj.data() + kDObjSkelMatOffset,
                &snapshot_matrices, sizeof(snapshot_matrices));
            if (is_feed_door) {
                const auto stage = g_controller_state.reload.stage;
                const bool open = stage != gameplay::ReloadStage::Ready &&
                    stage != gameplay::ReloadStage::Committing;
                if (!apply_magazine_feed_door_pose(
                        {snapshot_matrices, bone_count}, *magazine_profile,
                        source, open)) return viewmodel_dobj;
                WAWVR_STEREO_DIAG_ONCE(
                    "ReloadDiag exact %s loading hatch sealed at rigid-surface consumption",
                    magazine_profile->diagnostic_name);
                return g_pre_skin_scratch.dobj.data();
            }
            if (!apply_charging_handle_pose(
                    {snapshot_matrices, bone_count}, *magazine_profile,
                    source, g_magazine_charging_state.handle_fraction,
                    g_magazine_visual_state
                        .closed_charging_handle_relative)) {
                return viewmodel_dobj;
            }
            WAWVR_STEREO_DIAG_ONCE(
                "ReloadDiag exact %s charging-handle pose sealed at rigid-surface consumption",
                magazine_profile->diagnostic_name);
            return g_pre_skin_scratch.dobj.data();
        }
        const std::uint64_t packed =
            g_active_weapon_binding.load(std::memory_order_acquire);
        BoltActionWeaponBinding binding{};
        const BoltActionWeaponProfile* const profile =
            profile_for_binding(packed, &binding);
        if (!g_enabled.load(std::memory_order_acquire) ||
            !g_supported.load(std::memory_order_acquire) ||
            viewmodel_dobj !=
                g_pre_skin_viewmodel_dobj.load(std::memory_order_acquire) ||
            !still_published_bolt_surface || profile == nullptr ||
            g_visual_state.profile_id != profile->id ||
            g_visual_state.weapon_index != binding.weapon_index ||
            g_visual_state.viewmodel_dobj != viewmodel_dobj ||
            !g_visual_state.closed_bolt_pose_latched) {
            return viewmodel_dobj;
        }
        profile_diagnostic_name = profile->diagnostic_name;

        const ClipSource source = g_visual_state.source;
        const std::size_t surface_bolt_index =
            published_bolt_surface_index;
        if (surface_bolt_index >= source.moving_bolt_count ||
            source.moving_bolts[surface_bolt_index].surface != surface) {
            return viewmodel_dobj;
        }

        auto* const live_bytes =
            static_cast<std::uint8_t*>(viewmodel_dobj);
        std::uint8_t bone_count = 0;
        DObjAnimMat* live_matrices = nullptr;
        std::memcpy(
            &bone_count, live_bytes + kDObjNumBonesOffset,
            sizeof(bone_count));
        std::memcpy(
            &live_matrices, live_bytes + kDObjSkelMatOffset,
            sizeof(live_matrices));
        if (bone_count == 0 || bone_count > g_pre_skin_scratch.matrices.size() ||
            source.moving_bolts[surface_bolt_index].bone >= bone_count ||
            live_matrices == nullptr ||
            !accessible_range(viewmodel_dobj, kDObjSize, false) ||
            !accessible_range(
                live_matrices,
                static_cast<std::size_t>(bone_count) *
                    sizeof(DObjAnimMat),
                false)) {
            return viewmodel_dobj;
        }

        std::memcpy(
            g_pre_skin_scratch.dobj.data(), viewmodel_dobj, kDObjSize);
        std::memcpy(
            g_pre_skin_scratch.matrices.data(), live_matrices,
            static_cast<std::size_t>(bone_count) *
                sizeof(DObjAnimMat));
        DObjAnimMat* const snapshot_matrices =
            g_pre_skin_scratch.matrices.data();
        std::memcpy(
            g_pre_skin_scratch.dobj.data() + kDObjSkelMatOffset,
            &snapshot_matrices, sizeof(snapshot_matrices));

        const std::uint8_t surface_bolt_bone =
            source.moving_bolts[surface_bolt_index].bone;
        const DObjAnimMat live_bolt = live_matrices[surface_bolt_bone];
        fraction = g_bolt_state.bolt_fraction;
        bolt_grabbed = g_bolt_state.bolt_grabbed;
        if (!apply_moving_bolt_poses_to_matrices(
                {snapshot_matrices, bone_count}, *profile, source, fraction,
                g_visual_state.closed_bolt_relatives)) {
            return viewmodel_dobj;
        }
        const DObjAnimMat& sealed_bolt =
            snapshot_matrices[surface_bolt_bone];
        for (int component = 0; component < 3; ++component) {
            translation_delta = (std::max)(
                translation_delta,
                std::abs(
                    live_bolt.translation[component] -
                    sealed_bolt.translation[component]));
        }
        for (int component = 0; component < 4; ++component) {
            quaternion_delta_same_sign = (std::max)(
                quaternion_delta_same_sign,
                std::abs(
                    live_bolt.quaternion[component] -
                    sealed_bolt.quaternion[component]));
            quaternion_delta_opposite_sign = (std::max)(
                quaternion_delta_opposite_sign,
                std::abs(
                    live_bolt.quaternion[component] +
                    sealed_bolt.quaternion[component]));
        }
        sealed = true;
    } catch (...) {
        return viewmodel_dobj;
    }

    if (!sealed) {
        return viewmodel_dobj;
    }
    WAWVR_STEREO_DIAG_ONCE(
        "ReloadDiag %s rigid bolt surfaces now consume a private all-piece manual-pose snapshot at R_PreSkinXSurface",
        profile_diagnostic_name);

    // q and -q encode the same rotation. Record a shot only after native pose
    // output actually differs from the manual surface snapshot; otherwise an
    // unchanged scene pass immediately after Bullet_Fire could consume the
    // generation before the delayed rechamber pose reaches this seam.
    const float quaternion_delta = (std::min)(
        quaternion_delta_same_sign, quaternion_delta_opposite_sign);
    constexpr float kDiagnosticPoseDeltaEpsilon = 0.0001F;
    if (bolt_grabbed || fraction > kDiagnosticPoseDeltaEpsilon ||
        (translation_delta <= kDiagnosticPoseDeltaEpsilon &&
         quaternion_delta <= kDiagnosticPoseDeltaEpsilon)) {
        return g_pre_skin_scratch.dobj.data();
    }

    const std::uint64_t generation =
        g_local_shot_generation.load(std::memory_order_acquire);
    std::uint64_t logged =
        g_preskin_logged_generation.load(std::memory_order_acquire);
    while (generation > logged &&
           !g_preskin_logged_generation.compare_exchange_weak(
               logged, generation, std::memory_order_acq_rel,
               std::memory_order_acquire)) {
    }
    if (generation != 0 && generation > logged) {
        stereo_diagnostic_log(
            "ReloadDiag shot %llu sealed at rigid bolt consumption (fraction=%.3f live-translation-delta=%.4f live-quaternion-delta=%.4f)",
            static_cast<unsigned long long>(generation), fraction,
            translation_delta, quaternion_delta);
    }
    return g_pre_skin_scratch.dobj.data();
}

[[nodiscard]] bool receiver_position(
    const ManualReloadViewmodelContext& context,
    const BoltActionWeaponProfile& profile,
    const wawvr::xr::Vec3f& clip_probe,
    wawvr::xr::Vec3f* const receiver) noexcept {
    if (receiver == nullptr) {
        return false;
    }
    ManualReloadPoint calculated{};
    float segment_fraction = 0.0F;
    if (!calculate_manual_reload_top_feed_receiver(
            {
                profile.receiver_segment_minimum,
                profile.receiver_segment_maximum,
                profile.receiver_top_offset,
            },
            {clip_probe.x, clip_probe.y, clip_probe.z},
            {context.rifle_grip_world.x, context.rifle_grip_world.y,
             context.rifle_grip_world.z},
            {context.muzzle_world.x, context.muzzle_world.y,
             context.muzzle_world.z},
            {context.weapon_axis.up.x, context.weapon_axis.up.y,
             context.weapon_axis.up.z},
            &calculated, &segment_fraction)) {
        return false;
    }
    *receiver = {calculated.x, calculated.y, calculated.z};
    return true;
}

[[nodiscard]] bool bolt_handle_position(
    const ManualReloadViewmodelContext& context,
    const BoltActionWeaponProfile& profile,
    const wawvr::xr::Vec3f& closed_grip_offset,
    const float bolt_fraction,
    wawvr::xr::Vec3f* const closed_anchor,
    wawvr::xr::Vec3f* const current_handle) noexcept {
    if (closed_anchor == nullptr || current_handle == nullptr ||
        !finite_vector(context.rifle_grip_world) ||
        !finite_vector(closed_grip_offset) ||
        !valid_basis(context.weapon_axis) || !std::isfinite(bolt_fraction)) {
        return false;
    }
    const wawvr::xr::Vec3f offset =
        compose(context.weapon_axis, closed_grip_offset);
    *closed_anchor = {
        context.rifle_grip_world.x + offset.x,
        context.rifle_grip_world.y + offset.y,
        context.rifle_grip_world.z + offset.z,
    };
    *current_handle = {
        closed_anchor->x - context.weapon_axis.forward.x *
            profile.bolt_travel_units * bolt_fraction,
        closed_anchor->y - context.weapon_axis.forward.y *
            profile.bolt_travel_units * bolt_fraction,
        closed_anchor->z - context.weapon_axis.forward.z *
            profile.bolt_travel_units * bolt_fraction,
    };
    return finite_vector(*closed_anchor) && finite_vector(*current_handle);
}

[[nodiscard]] bool charging_handle_position(
    const ManualReloadViewmodelContext& context,
    const DetachableMagazineWeaponProfile& profile,
    const wawvr::xr::Vec3f& closed_grip_offset,
    const float handle_fraction,
    wawvr::xr::Vec3f* const closed_anchor,
    wawvr::xr::Vec3f* const current_handle) noexcept {
    if (!profile.charging.enabled || closed_anchor == nullptr ||
        current_handle == nullptr ||
        !finite_vector(context.rifle_grip_world) ||
        !finite_vector(closed_grip_offset) ||
        !valid_basis(context.weapon_axis) ||
        !std::isfinite(handle_fraction)) {
        return false;
    }
    const wawvr::xr::Vec3f offset =
        compose(context.weapon_axis, closed_grip_offset);
    *closed_anchor = {
        context.rifle_grip_world.x + offset.x,
        context.rifle_grip_world.y + offset.y,
        context.rifle_grip_world.z + offset.z,
    };
    const float travel = profile.charging.interaction.travel_units *
        std::clamp(handle_fraction, 0.0F, 1.0F);
    *current_handle = {
        closed_anchor->x - context.weapon_axis.forward.x * travel,
        closed_anchor->y - context.weapon_axis.forward.y * travel,
        closed_anchor->z - context.weapon_axis.forward.z * travel,
    };
    return finite_vector(*closed_anchor) && finite_vector(*current_handle);
}

void reject_claimed_bolt_action_commit() noexcept {
    g_bolt_action_commit_witness = {};
    NativeCommitPhase claimed = NativeCommitPhase::claimed;
    if (g_native_commit_phase.compare_exchange_strong(
            claimed, NativeCommitPhase::idle,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
        g_native_commit_publication_generation.store(
            0, std::memory_order_release);
        g_detachable_magazine_commit_completed_at_milliseconds.store(
            0, std::memory_order_release);
    }
}

extern "C" int __cdecl prepare_manual_bolt_action_commit(
    void* const player_state) noexcept {
    g_bolt_action_commit_witness = {};
    if (!g_enabled.load(std::memory_order_acquire) ||
        g_native_commit_phase.load(std::memory_order_acquire) !=
            NativeCommitPhase::claimed ||
        !accessible_range(player_state, kPlayerStateMinimumSpan, false)) {
        reject_claimed_bolt_action_commit();
        return 0;
    }

    std::int32_t weapon = 0;
    std::memcpy(
        &weapon,
        static_cast<const std::uint8_t*>(player_state) +
            kPlayerStateWeaponOffset,
        sizeof(weapon));
    std::uint64_t validated_generation = 0;
    const BoltActionWeaponProfile* const profile =
        revalidate_active_gameplay_weapon(
            weapon, 0, &validated_generation);
    RuntimeWeaponDefinitionIdentity identity{};
    const std::uint64_t commit_generation =
        g_native_commit_publication_generation.load(
            std::memory_order_acquire);
    if (profile == nullptr || validated_generation == 0 ||
        validated_generation != commit_generation ||
        !read_runtime_sp_weapon_definition(weapon, &identity) ||
        identity.weapon_index != weapon ||
        identity.definition_address !=
            g_active_weapon_definition_address.load(
                std::memory_order_acquire) ||
        !owned_c_string_equals(
            identity.name, profile->internal_weapon_name)) {
        reject_claimed_bolt_action_commit();
        return 0;
    }

    const BoltActionAmmoSnapshot ammo =
        read_bolt_action_ammo(player_state, identity);
    const ManualStripperClipCommitPlan plan = ammo.valid
        ? plan_manual_stripper_clip_commit(ammo.commit)
        : ManualStripperClipCommitPlan{};
    if (!plan.valid || plan.native_reload_clip_calls <= 0 ||
        plan.native_reload_clip_calls > kManualStripperClipCapacity) {
        reject_claimed_bolt_action_commit();
        return 0;
    }

    g_bolt_action_commit_witness = {
        .generation = validated_generation,
        .player_state = player_state,
        .weapon_index = weapon,
        .definition_address = identity.definition_address,
        .ammo_index = ammo.ammo_index,
        .clip_index = ammo.clip_index,
        .clip_size = ammo.commit.clip_size,
        .expected_reserve =
            ammo.commit.reserve - plan.rounds_to_transfer,
        .expected_loaded =
            ammo.commit.loaded + plan.rounds_to_transfer,
    };
    return plan.native_reload_clip_calls;
}

extern "C" int __cdecl verify_manual_bolt_action_commit(
    void* const player_state) noexcept {
    const BoltActionCommitWitness witness =
        g_bolt_action_commit_witness;
    bool verified = witness.generation != 0 &&
        player_state == witness.player_state &&
        g_enabled.load(std::memory_order_acquire) &&
        g_native_commit_phase.load(std::memory_order_acquire) ==
            NativeCommitPhase::claimed &&
        g_native_commit_publication_generation.load(
            std::memory_order_acquire) == witness.generation &&
        accessible_range(player_state, kPlayerStateMinimumSpan, false);
    std::int32_t weapon = 0;
    if (verified) {
        std::memcpy(
            &weapon,
            static_cast<const std::uint8_t*>(player_state) +
                kPlayerStateWeaponOffset,
            sizeof(weapon));
    }

    std::uint64_t validated_generation = 0;
    const BoltActionWeaponProfile* profile = nullptr;
    RuntimeWeaponDefinitionIdentity identity{};
    if (verified) {
        profile = revalidate_active_gameplay_weapon(
            weapon, witness.definition_address, &validated_generation);
        verified = profile != nullptr &&
            weapon == witness.weapon_index &&
            validated_generation == witness.generation &&
            read_runtime_sp_weapon_definition(weapon, &identity) &&
            identity.definition_address == witness.definition_address &&
            owned_c_string_equals(
                identity.name, profile->internal_weapon_name);
    }

    BoltActionAmmoSnapshot ammo{};
    if (verified) {
        ammo = read_bolt_action_ammo(player_state, identity);
        verified = ammo.valid &&
            ammo.ammo_index == witness.ammo_index &&
            ammo.clip_index == witness.clip_index &&
            ammo.commit.clip_size == witness.clip_size &&
            ammo.commit.reserve == witness.expected_reserve &&
            ammo.commit.loaded == witness.expected_loaded;
    }
    g_bolt_action_commit_witness = {};
    if (!verified) {
        WAWVR_STEREO_DIAG_ONCE(
            "ReloadDiag rejected a physical stripper-clip commit whose bounded native calls did not produce the exact guarded ammo transfer");
        reject_claimed_bolt_action_commit();
        return 0;
    }
    return 1;
}

extern "C" int __cdecl classify_manual_reload_begin(
    void* const player_state) noexcept {
    if (!g_enabled.load(std::memory_order_acquire) ||
        !accessible_range(player_state, kPlayerStateMinimumSpan, false)) {
        return static_cast<int>(BeginDecision::native);
    }
    if (g_automatic_reload.load(std::memory_order_acquire)) {
        return static_cast<int>(BeginDecision::native);
    }
    std::int32_t weapon = 0;
    std::memcpy(
        &weapon,
        static_cast<const std::uint8_t*>(player_state) +
            kPlayerStateWeaponOffset,
        sizeof(weapon));
    std::uint64_t validated_generation = 0;
    const BoltActionWeaponProfile* const bolt_profile =
        revalidate_active_gameplay_weapon(
            weapon, 0, &validated_generation);
    const DetachableMagazineWeaponProfile* const magazine_profile =
        bolt_profile == nullptr
        ? revalidate_active_magazine_gameplay_weapon(
              weapon, 0, &validated_generation)
        : nullptr;
    if (bolt_profile == nullptr && magazine_profile == nullptr) {
        return static_cast<int>(classify_reload_begin_fallback(weapon));
    }
    if (validated_generation !=
            g_active_weapon_publication_generation.load(
                std::memory_order_acquire) ||
        (validated_generation & 1U) != 0) {
        return static_cast<int>(classify_reload_begin_fallback(weapon));
    }
    if (!g_supported.load(std::memory_order_acquire)) {
        if (magazine_profile != nullptr &&
            classify_reload_begin_fallback(weapon) ==
                BeginDecision::native) {
            // A detachable transaction is always cancellable. If its exact
            // visual source disappears, restore ordinary native reload rather
            // than trapping a non-bolt weapon behind a stale manual gate. An
            // exact empty-chamber lock is not cancellable presentation state,
            // however, so it suppresses this fallback until physical charging
            // or session retirement clears that keyed lock.
            return static_cast<int>(BeginDecision::native);
        }
        // The identity is still the active physical rifle, but a transient
        // render/tag gap makes a clip commit unsafe. Preserve the transaction
        // and suppress native reload until visual ownership returns.
        return static_cast<int>(BeginDecision::suppress);
    }
    if (g_reload_clip == 0 || g_start_weapon_anim == 0) {
        return static_cast<int>(BeginDecision::suppress);
    }
    if (validated_generation !=
            g_active_weapon_publication_generation.load(
                std::memory_order_acquire) ||
        !g_enabled.load(std::memory_order_acquire)) {
        return static_cast<int>(classify_reload_begin_fallback(weapon));
    }
    const std::uint64_t commit_generation =
        g_native_commit_publication_generation.load(
            std::memory_order_acquire);
    if (magazine_profile != nullptr &&
        g_native_commit_phase.load(std::memory_order_acquire) !=
            NativeCommitPhase::idle) {
        log_simulator_detachable_magazine_commit_snapshot(
            "classify", player_state);
    }
    if (commit_generation == 0 ||
        commit_generation != validated_generation) {
        NativeCommitPhase requested = NativeCommitPhase::requested;
        if (g_native_commit_phase.compare_exchange_strong(
                requested, NativeCommitPhase::idle,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            g_native_commit_publication_generation.store(
                0, std::memory_order_release);
            g_detachable_magazine_commit_completed_at_milliseconds.store(
                0, std::memory_order_release);
        }
        return static_cast<int>(BeginDecision::suppress);
    }
    if (magazine_profile != nullptr &&
        g_native_commit_phase.load(std::memory_order_acquire) ==
            NativeCommitPhase::completed) {
        // The reload bit is replayed for prediction and authoritative local
        // simulation. PM_ReloadClip is idempotent once this Colt magazine is
        // full, so allow the second pass while the short fanout window lives.
        return static_cast<int>(BeginDecision::commit_detachable_magazine);
    }
    NativeCommitPhase expected = NativeCommitPhase::requested;
    const bool claimed = g_native_commit_phase.compare_exchange_strong(
        expected, NativeCommitPhase::claimed,
        std::memory_order_acq_rel, std::memory_order_acquire);
    if (!claimed) {
        return static_cast<int>(BeginDecision::suppress);
    }
    return static_cast<int>(
        magazine_profile != nullptr
            ? BeginDecision::commit_detachable_magazine
            : BeginDecision::commit);
}

extern "C" void __cdecl record_manual_reload_commit(
    void* const player_state) noexcept {
    if (g_active_magazine_binding.load(std::memory_order_acquire) != 0) {
        log_simulator_detachable_magazine_commit_snapshot(
            "record", player_state);
    }
    std::int32_t weapon = 0;
    std::uint64_t validated_generation = 0;
    const std::uint64_t commit_generation =
        g_native_commit_publication_generation.load(
            std::memory_order_acquire);
    const bool player_readable =
        accessible_range(player_state, kPlayerStateMinimumSpan, false);
    if (player_readable) {
        std::memcpy(
            &weapon,
            static_cast<const std::uint8_t*>(player_state) +
                kPlayerStateWeaponOffset,
            sizeof(weapon));
    }
    const bool correlated = player_readable &&
        revalidate_active_manual_gameplay_weapon(
            weapon, 0, &validated_generation) &&
        commit_generation != 0 &&
        commit_generation == validated_generation &&
        validated_generation ==
            g_active_weapon_publication_generation.load(
                std::memory_order_acquire) &&
        g_enabled.load(std::memory_order_acquire);
    NativeCommitPhase expected = NativeCommitPhase::claimed;
    if (!correlated) {
        if (g_native_commit_phase.compare_exchange_strong(
                expected, NativeCommitPhase::idle,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            g_native_commit_publication_generation.store(
                0, std::memory_order_release);
            g_detachable_magazine_commit_completed_at_milliseconds.store(
                0, std::memory_order_release);
        }
        return;
    }
    if (g_native_commit_phase.compare_exchange_strong(
            expected, NativeCommitPhase::completed,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
        const std::uint64_t packed =
            g_active_weapon_binding.load(std::memory_order_acquire);
        BoltActionWeaponBinding binding{};
        const BoltActionWeaponProfile* const profile =
            profile_for_binding(packed, &binding);
        if (profile != nullptr) {
            WAWVR_STEREO_DIAG_ONCE(
                "ReloadDiag physical %s stripper clip transferred its guarded five-round capacity through bounded PM_ReloadClip calls",
                profile->diagnostic_name);
        } else {
            const std::uint64_t magazine_packed =
                g_active_magazine_binding.load(
                    std::memory_order_acquire);
            DetachableMagazineWeaponBinding magazine_binding{};
            const DetachableMagazineWeaponProfile* const magazine_profile =
                profile_for_magazine_binding(
                    magazine_packed, &magazine_binding);
            g_detachable_magazine_commit_completed_at_milliseconds.store(
                GetTickCount64(), std::memory_order_release);
            WAWVR_STEREO_DIAG_ONCE(
                "ReloadDiag physical %s magazine committed once per predicted/authoritative player state through PM_ReloadClip",
                magazine_profile != nullptr
                    ? magazine_profile->diagnostic_name
                    : "detachable");
        }
    }
}

extern "C" std::int32_t __cdecl select_manual_kar98_rechamber_visual_anim(
    const void* const player_state,
    const std::int32_t native_anim) noexcept {
    std::int32_t weapon = 0;
    if (accessible_range(player_state, kPlayerStateMinimumSpan, false)) {
        std::memcpy(
            &weapon,
            static_cast<const std::uint8_t*>(player_state) +
                kPlayerStateWeaponOffset,
            sizeof(weapon));
    }
    std::uint64_t validated_generation = 0;
    const BoltActionWeaponProfile* const profile =
        revalidate_active_gameplay_weapon(
            weapon, 0, &validated_generation);
    const bool weapon_supported = profile != nullptr &&
        validated_generation ==
            g_active_weapon_publication_generation.load(
                std::memory_order_acquire);
    const std::int32_t selected = profile != nullptr
        ? select_bolt_action_rechamber_visual_anim(
              *profile, g_enabled.load(std::memory_order_acquire),
              g_cycle_lock.load(std::memory_order_acquire), weapon_supported,
              native_anim)
        : native_anim;
    if (selected != native_anim &&
        (!g_enabled.load(std::memory_order_acquire) ||
         validated_generation !=
             g_active_weapon_publication_generation.load(
                 std::memory_order_acquire))) {
        return native_anim;
    }
    if (selected != native_anim) {
        WAWVR_STEREO_DIAG_ONCE(
            "ReloadDiag suppressed T4's automatic %s rechamber visual while preserving native state and timing",
            profile->diagnostic_name);
    }
    return selected;
}

#if defined(_MSC_VER) && defined(_M_IX86)
extern "C" __declspec(naked) void
manual_reload_rechamber_visual_anim_bridge() noexcept {
    __asm {
        // Exact PM_Weapon_CheckForRechamber call ABI at 0x41E805:
        //   EAX     = playerState_s*
        //   [ESP+4] = selected native animation (4 hip / 7 ADS)
        // Preserve the complete caller state while the policy replaces only
        // that stack argument, then tail-call PM_StartWeaponAnim so its
        // toggle bit, return address, and caller cleanup remain stock.
        pushfd
        pushad
        mov ebx, esp
        sub esp, 20Fh
        and esp, 0FFFFFFF0h
        fxsave [esp]
        push dword ptr [ebx + 28h]
        push dword ptr [ebx + 1Ch]
        call select_manual_kar98_rechamber_visual_anim
        add esp, 8
        mov dword ptr [ebx + 28h], eax
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        jmp dword ptr [g_start_weapon_anim]
    }
}

extern "C" __declspec(naked) void
manual_reload_pre_skin_surface_bridge() noexcept {
    __asm {
        // Exact stock ABI at R_SkinSceneDObjModels+0x248:
        //   [ESP+4] = DObj*, [ESP+8] = XSurface*
        //   ECX     = GfxModelSurfaceInfo*
        //   EDX     = numSkinnedVerts*
        //   EDI     = output surface record
        // Preserve the register arguments, replace only the DObj stack
        // argument with the helper's original-or-private-snapshot result,
        // then tail-call native so its return and caller cleanup are exact.
        push ecx
        push edx
        push dword ptr [esp + 10h]
        push dword ptr [esp + 10h]
        call select_kar98_bolt_pre_skin_dobj
        add esp, 8
        pop edx
        pop ecx
        mov dword ptr [esp + 4], eax
        jmp dword ptr [g_original_pre_skin_surface]
    }
}

extern "C" __declspec(naked) void manual_reload_begin_bridge() noexcept {
    __asm {
        pushfd
        pushad
        mov ebx, esp
        sub esp, 20Fh
        and esp, 0FFFFFFF0h
        fxsave [esp]
        push dword ptr [ebx + 04h]
        call classify_manual_reload_begin
        lea esp, [esp + 4]
        mov edx, eax
        fxrstor [esp]
        mov esp, ebx
        mov dword ptr [esp + 1Ch], edx
        popad
        popfd

        cmp eax, 3
        je manual_magazine_commit
        cmp eax, 2
        je manual_bolt_commit
        cmp eax, 1
        je suppress_native
        cmp dword ptr [g_original_begin_reload], 0
        jz suppress_native
        jmp dword ptr [g_original_begin_reload]

    manual_magazine_commit:
        // PM_ReloadClip deliberately transfers nothing for a reload-start
        // state when iReloadStartAdd is zero, which is exactly how the retail
        // Nacht Colt is authored. End only that synthetic state, then retain
        // the established exactly-one detachable-magazine native call.
        mov dword ptr [esi + 108h], 0
        cmp dword ptr [g_reload_clip], 0
        jz suppress_native
        cmp dword ptr [g_start_weapon_anim], 0
        jz suppress_native
        call dword ptr [g_reload_clip]
        jmp manual_commit_finalize

    manual_bolt_commit:
        cmp dword ptr [g_reload_clip], 0
        jz suppress_native
        cmp dword ptr [g_start_weapon_anim], 0
        jz suppress_native
        push esi
        call prepare_manual_bolt_action_commit
        lea esp, [esp + 4]
        test eax, eax
        jz suppress_native
        // The exact retail scoped WeaponDef adds one round per call. The pure
        // plan caps this loop at the physical five-round clip capacity.
        push eax
    manual_bolt_reload_loop:
        call dword ptr [g_reload_clip]
        dec dword ptr [esp]
        jnz manual_bolt_reload_loop
        lea esp, [esp + 4]
        push esi
        call verify_manual_bolt_action_commit
        lea esp, [esp + 4]
        test eax, eax
        jz suppress_native

    manual_commit_finalize:
        mov dword ptr [esi + 40h], 0
        mov dword ptr [esi + 44h], 0
        mov dword ptr [esi + 108h], 0
        mov dword ptr [esi + 10Ch], 0
        mov ecx, dword ptr [esi + 104h]
        mov eax, ecx
        and ecx, 1Fh
        mov edx, 1
        shl edx, cl
        sar eax, 5
        not edx
        and dword ptr [esi + eax * 4 + 81Ch], edx
        push 0
        mov eax, esi
        call dword ptr [g_start_weapon_anim]
        lea esp, [esp + 4]

        pushfd
        pushad
        mov ebx, esp
        sub esp, 20Fh
        and esp, 0FFFFFFF0h
        fxsave [esp]
        push dword ptr [ebx + 04h]
        call record_manual_reload_commit
        lea esp, [esp + 4]
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
    suppress_native:
        ret
    }
}
#endif

void clear_interaction_state_locked() noexcept {
    if (g_magazine_visual_state.authored_hidden &&
        !set_authored_magazine_hidden_locked(false)) {
        WAWVR_STEREO_DIAG_ONCE(
            "ReloadDiag could not restore the outgoing detachable magazine hide bit before state retirement");
    }
    begin_active_weapon_publication_locked();
    g_active_weapon_binding.store(0, std::memory_order_release);
    g_active_magazine_binding.store(0, std::memory_order_release);
    g_active_weapon_definition_address.store(0, std::memory_order_release);
    g_pre_skin_viewmodel_dobj.store(nullptr, std::memory_order_release);
    for (auto& surface : g_pre_skin_bolt_surfaces) {
        surface.store(nullptr, std::memory_order_release);
    }
    reset_manual_reload_controller(&g_controller_state);
    reset_kar98_bolt_action(&g_bolt_state);
    reset_magazine_charging(&g_magazine_charging_state);
    g_empty_reload_armed = false;
    g_magazine_charging_last_update_milliseconds = 0;
    g_visual_state.supported = false;
    g_visual_state.profile_id = BoltActionWeaponProfileId::None;
    g_visual_state.weapon_index = 0;
    g_visual_state.weapon_definition_address = 0;
    g_visual_state.viewmodel_dobj = nullptr;
    g_visual_state.source = {};
    g_visual_state.source_surfaces = nullptr;
    g_visual_state.source_material_handles = nullptr;
    g_visual_state.source_base_matrices = nullptr;
    g_visual_state.asset = nullptr;
    g_visual_state.closed_bolt_relatives = {};
    g_visual_state.closed_bolt_grip_offset = {};
    g_visual_state.closed_bolt_pose_latched = false;
    g_visual_state.closed_bolt_grip_offset_valid = false;
    g_magazine_visual_state = {};
    g_supported.store(false, std::memory_order_release);
    g_manual_active.store(false, std::memory_order_release);
    g_cycle_lock.store(false, std::memory_order_release);
    g_reserve_right_grip.store(false, std::memory_order_release);
    g_reserve_left_grip.store(false, std::memory_order_release);
    g_block_new_left_trigger_action.store(false, std::memory_order_release);
    g_shot_pending.store(false, std::memory_order_release);
    g_native_commit_phase.store(
        NativeCommitPhase::idle, std::memory_order_release);
    g_native_commit_publication_generation.store(
        0, std::memory_order_release);
    g_detachable_magazine_commit_completed_at_milliseconds.store(
        0, std::memory_order_release);
    advance_clip_asset_source_generation_locked();
    finish_active_weapon_publication_locked();
}

void clear_runtime_state() noexcept {
    const std::lock_guard<std::mutex> lock(g_state_mutex);
    clear_interaction_state_locked();
}

void clear_runtime_state_if_epoch(
    const std::uint64_t expected_epoch) noexcept {
    const std::lock_guard<std::mutex> lock(g_state_mutex);
    if (g_enabled.load(std::memory_order_acquire) &&
        g_manual_reload_session_epoch.load(std::memory_order_acquire) ==
            expected_epoch) {
        clear_interaction_state_locked();
    }
}

void observe_manual_reload_command_time_locked(
    const std::int32_t command_time) noexcept {
    if (!g_enabled.load(std::memory_order_acquire) || command_time < 0) {
        return;
    }
    const std::int32_t previous =
        g_last_player_command_time.load(std::memory_order_acquire);
    // Prediction can temporarily expose an older nextSnap playerState. Treat
    // commandTime only as a backup for an in-place checkpoint rewind: require
    // two consecutive low samples whose second is nondecreasing.
    const bool low_sample = previous >= 0 &&
        static_cast<std::int64_t>(command_time) + 1000 < previous;
    const std::int32_t candidate =
        g_command_time_regression_candidate.load(std::memory_order_acquire);
    const bool session_restarted = low_sample && candidate >= 0 &&
        command_time >= candidate;
    if (low_sample && !session_restarted) {
        g_command_time_regression_candidate.store(
            command_time, std::memory_order_release);
        return;
    }
    g_command_time_regression_candidate.store(-1, std::memory_order_release);
    if (session_restarted) {
        g_magazine_chamber_locks.clear_all();
        clear_interaction_state_locked();
        g_manual_reload_session_epoch.fetch_add(
            1, std::memory_order_acq_rel);
        stereo_diagnostic_log(
            "ReloadDiag playerState commandTime regressed %d -> %d; retired persistent manual chamber requirements for the new SP session",
            previous, command_time);
    }
    if (session_restarted || command_time > previous) {
        g_last_player_command_time.store(
            command_time, std::memory_order_release);
    }
}

void observe_manual_reload_session(const void* const player_state) noexcept {
    if (!accessible_range(player_state, sizeof(std::int32_t), false)) {
        return;
    }
    std::int32_t command_time = -1;
    std::memcpy(
        &command_time,
        static_cast<const std::uint8_t*>(player_state) +
            kPlayerStateCommandTimeOffset,
        sizeof(command_time));
    if (command_time < 0) {
        return;
    }
    try {
        const std::lock_guard<std::mutex> lock(g_state_mutex);
        observe_manual_reload_command_time_locked(command_time);
    } catch (...) {
        // An unreadable session boundary must not mutate current gameplay.
    }
}

void mark_runtime_temporarily_unavailable_locked() noexcept {
    if (g_active_magazine_binding.load(std::memory_order_acquire) != 0) {
        DetachableMagazineWeaponBinding binding{};
        const auto* const profile = profile_for_magazine_binding(
            g_active_magazine_binding.load(std::memory_order_acquire),
            &binding);
        const bool automatic_en_bloc =
            profile != nullptr && profile->charging.enabled &&
            profile->charging.interaction.control_policy ==
                MagazineChargingControlPolicy::EnBlocAutomatic;
        const bool persistent_action_lock =
            profile != nullptr &&
            magazine_chamber_lock_matches_identity(
                *profile, binding.weapon_index,
                g_active_weapon_definition_address.load(
                    std::memory_order_acquire));
        if (automatic_en_bloc && !persistent_action_lock) {
            // A partial-clip A reload has no gameplay-persistent chamber
            // requirement. If its viewmodel/focus disappears, cancel it and
            // restore the still-loaded rifle instead of preserving a phantom
            // locked-open action across the gap.
            clear_interaction_state_locked();
            return;
        }
        if (!g_magazine_charging_state.charge_required &&
            !g_magazine_charging_state.spring_returning &&
            !g_magazine_charging_state.awaiting_controls_release) {
            // An ordinary detachable-magazine transaction is cancellable and
            // must restore its authored j_clip immediately.
            clear_interaction_state_locked();
            return;
        }
        // Once an empty reload has seated ammunition, focus loss is not proof
        // that a round was chambered. Cancel only feed-device/render ownership
        // and retain the exact keyed attack lock.
        static_cast<void>(set_authored_magazine_hidden_locked(false));
        reset_manual_reload_controller(&g_controller_state);
        g_empty_reload_armed = false;
        g_magazine_charging_last_update_milliseconds = 0;
        if (g_magazine_charging_state.handle_grabbed) {
            g_magazine_charging_state.handle_grabbed = false;
            g_magazine_charging_state.fully_opened = false;
            g_magazine_charging_state.spring_returning = false;
            if (profile != nullptr && profile->charging.enabled) {
                g_magazine_charging_state.handle_fraction =
                    profile->charging.interaction
                        .locked_open_offset_units /
                    profile->charging.interaction.travel_units;
            }
        }
        g_magazine_charging_state.input_owned = false;
        static_cast<void>(cancel_native_commit_request_locked());
        begin_active_weapon_publication_locked();
        g_pre_skin_viewmodel_dobj.store(nullptr, std::memory_order_release);
        for (auto& surface : g_pre_skin_bolt_surfaces) {
            surface.store(nullptr, std::memory_order_release);
        }
        g_supported.store(false, std::memory_order_release);
        g_manual_active.store(false, std::memory_order_release);
        g_cycle_lock.store(true, std::memory_order_release);
        const bool exact_charging_profile =
            profile != nullptr && profile->charging.enabled;
        const bool charging_reserves_hand = exact_charging_profile &&
            !automatic_en_bloc &&
            g_magazine_charging_state.manipulating_hand_selected;
        g_reserve_right_grip.store(
            !exact_charging_profile ||
                (charging_reserves_hand &&
                 !g_magazine_charging_state.manipulating_left_hand),
            std::memory_order_release);
        g_reserve_left_grip.store(
            !exact_charging_profile ||
                (charging_reserves_hand &&
                 g_magazine_charging_state.manipulating_left_hand),
            std::memory_order_release);
        g_block_new_left_trigger_action.store(
            charging_reserves_hand &&
                g_magazine_charging_state.manipulating_left_hand,
            std::memory_order_release);
        finish_active_weapon_publication_locked();
        return;
    }
    // Missing post-pose data, focus, or a chest holster is not proof that the
    // chamber requirement disappeared. Disable only render-dependent commit;
    // preserve the exact gameplay identity, physical bolt state, and its attack
    // lock. Retire pre-skin pointers so a rejected/stale surface cannot keep
    // consuming the previous manual pose while presentation is unavailable.
    bool had_visual_publication =
        g_pre_skin_viewmodel_dobj.load(std::memory_order_acquire) != nullptr;
    for (const auto& surface : g_pre_skin_bolt_surfaces) {
        had_visual_publication = had_visual_publication ||
            surface.load(std::memory_order_acquire) != nullptr;
    }
    if (had_visual_publication ||
        g_supported.load(std::memory_order_acquire)) {
        begin_active_weapon_publication_locked();
        g_pre_skin_viewmodel_dobj.store(nullptr, std::memory_order_release);
        for (auto& surface : g_pre_skin_bolt_surfaces) {
            surface.store(nullptr, std::memory_order_release);
        }
        g_supported.store(false, std::memory_order_release);
        g_block_new_left_trigger_action.store(
            g_bolt_state.manipulating_hand_selected &&
                g_bolt_state.manipulating_left_hand,
            std::memory_order_release);
        finish_active_weapon_publication_locked();
        return;
    }
    g_supported.store(false, std::memory_order_release);
    g_block_new_left_trigger_action.store(
        g_bolt_state.manipulating_hand_selected &&
            g_bolt_state.manipulating_left_hand,
        std::memory_order_release);
}

void mark_runtime_temporarily_unavailable(
    const std::uint64_t expected_epoch) noexcept {
    const std::lock_guard<std::mutex> lock(g_state_mutex);
    if (!g_enabled.load(std::memory_order_acquire) ||
        g_manual_reload_session_epoch.load(std::memory_order_acquire) !=
            expected_epoch) {
        return;
    }
    mark_runtime_temporarily_unavailable_locked();
}

void update_detachable_magazine_viewmodel_locked(
    void* const viewmodel_dobj,
    const void* const viewmodel_pose,
    const ManualReloadViewmodelContext& context,
    const RuntimeWeaponDefinitionIdentity& identity,
    const DetachableMagazineWeaponProfile& profile) {
    const auto retire_current_magazine_viewmodel_fail_closed = []() noexcept {
        if (g_active_magazine_binding.load(std::memory_order_acquire) != 0) {
            // An armed empty-reload requirement belongs to the exact gameplay
            // binding, not to a particular render DObj. Temporary asset/tag
            // failures must retire render ownership without making a refilled
            // rifle fireable. The helper performs an ordinary full clear when
            // no chamber requirement exists.
            mark_runtime_temporarily_unavailable_locked();
        } else {
            clear_interaction_state_locked();
        }
    };
    const std::uint64_t desired_binding =
        pack_detachable_magazine_weapon_binding(
            {profile.id, context.weapon_index});
    if (desired_binding == 0) {
        clear_interaction_state_locked();
        return;
    }
    const std::uint64_t previous_magazine_binding =
        g_active_magazine_binding.load(std::memory_order_acquire);
    bool binding_transition =
        g_active_weapon_binding.load(std::memory_order_acquire) != 0 ||
        previous_magazine_binding != desired_binding;
    const bool switching_from_bolt =
        g_active_weapon_binding.load(std::memory_order_acquire) != 0;
    const bool exact_magazine_identity_changed =
        previous_magazine_binding != 0 &&
        (previous_magazine_binding != desired_binding ||
         g_magazine_visual_state.weapon_definition_address !=
             identity.definition_address);
    const bool exact_magazine_dobj_rebuilt =
        previous_magazine_binding == desired_binding &&
        g_magazine_visual_state.weapon_definition_address ==
            identity.definition_address &&
        g_magazine_visual_state.viewmodel_dobj != viewmodel_dobj;
    if (switching_from_bolt || exact_magazine_identity_changed ||
        exact_magazine_dobj_rebuilt) {
        if (exact_magazine_dobj_rebuilt &&
            (g_magazine_charging_state.charge_required ||
             g_magazine_charging_state.spring_returning ||
             g_magazine_charging_state.awaiting_controls_release)) {
            // DObj recreation is common across native viewmodel transitions;
            // preserve the exact weapon-keyed chamber requirement and rebuild
            // only its render attachment below.
            mark_runtime_temporarily_unavailable_locked();
        } else {
            clear_interaction_state_locked();
        }
        binding_transition = true;
    }

    MagazineSource source{};
    // Mesh discovery fingerprints every authored magazine vertex and index.
    // Reuse that immutable source only while the exact gameplay binding and
    // DObj/profile ownership still pass the existing fail-closed validator.
    // Dynamic well/charging tags, ammo, input, and poses remain evaluated
    // below on every frame.
    const bool cached_source_ready = !binding_transition &&
        previous_magazine_binding == desired_binding &&
        g_magazine_visual_state.supported &&
        g_magazine_visual_state.profile_id == profile.id &&
        g_magazine_visual_state.weapon_index == context.weapon_index &&
        g_magazine_visual_state.weapon_definition_address ==
            identity.definition_address &&
        g_magazine_visual_state.viewmodel_dobj == viewmodel_dobj &&
        cached_magazine_source_matches_dobj(
            viewmodel_dobj, profile, g_magazine_visual_state.source);
    if (cached_source_ready) {
        source = g_magazine_visual_state.source;
    } else if (!discover_detachable_magazine_source(
                   viewmodel_dobj, profile, &source)) {
        WAWVR_STEREO_DIAG_ONCE(
            "ReloadDiag %s rejected the current exact viewmodel magazine recipe",
            profile.diagnostic_name);
        retire_current_magazine_viewmodel_fail_closed();
        return;
    }
    if (!binding_transition &&
        previous_magazine_binding == desired_binding &&
        (g_magazine_visual_state.source.model != source.model ||
         !magazine_source_piece_identity_matches(
             g_magazine_visual_state.source, source) ||
         !magazine_charging_source_piece_identity_matches(
             g_magazine_visual_state.source, source) ||
         g_magazine_visual_state.source.global_bone !=
              source.global_bone ||
          g_magazine_visual_state.source.insertion_anchor_tag !=
              source.insertion_anchor_tag ||
          g_magazine_visual_state.source.feed_door_surface !=
              source.feed_door_surface ||
          g_magazine_visual_state.source.feed_door.tag != source.feed_door.tag ||
          g_magazine_visual_state.source.charging_handle.tag !=
              source.charging_handle.tag ||
          g_magazine_visual_state.source.charging_handle.parent_bone !=
              source.charging_handle.parent_bone ||
          g_magazine_visual_state.source.charging_handle.bone !=
              source.charging_handle.bone ||
          g_magazine_visual_state.source.asset_fingerprint !=
              source.asset_fingerprint ||
          g_magazine_visual_state.viewmodel_dobj != viewmodel_dobj)) {
        retire_current_magazine_viewmodel_fail_closed();
        binding_transition = true;
    }

    MagazineAsset* const asset = find_or_build_magazine_asset(
        profile, source);
    if (asset == nullptr || !asset->ready) {
        WAWVR_STEREO_DIAG_ONCE(
            "ReloadDiag %s exact rigid magazine asset build failed",
            profile.diagnostic_name);
        retire_current_magazine_viewmodel_fail_closed();
        return;
    }

    const auto& actions = context.controller.frame.actions;
    const auto& left = actions.hands[
        static_cast<std::uint32_t>(wawvr::xr::Hand::Left)];
    const auto& right = actions.hands[
        static_cast<std::uint32_t>(wawvr::xr::Hand::Right)];
    const bool begin_reload_pressed_edge =
        right.primary.active && right.primary.current &&
        right.primary.changed;
    const bool source_needs_dynamic_tag_validation =
        !cached_source_ready || binding_transition;
    const gameplay::ReloadStage reload_stage_before_update =
        g_controller_state.reload.stage;
    const bool reload_transaction_present =
        reload_stage_before_update != gameplay::ReloadStage::Ready;
    // A Ready -> FeedDeviceAvailable update cannot grab or insert in the same
    // call, but retain the old fail-closed tag validation on the initiating A
    // edge before any authored piece is hidden.
    const bool possible_reload_transition = begin_reload_pressed_edge;

    wawvr::xr::EnginePose insertion_guide{};
    bool insertion_guide_valid = false;
    const bool well_tag_validation_required =
        source_needs_dynamic_tag_validation || reload_transaction_present ||
        possible_reload_transition;
    if (well_tag_validation_required) {
        // The authored well tag is used only after a magazine has already been
        // taken from the belt: it drives proximity and orientation assist.
        // Evaluating it while Ready/Available/Committing needlessly forces
        // viewmodel skeleton work during ordinary held-weapon aiming.
        wawvr::xr::Vec3f well_origin{};
        if (!read_viewmodel_world_tag_position(
                viewmodel_dobj, source.insertion_anchor_tag, viewmodel_pose,
                &well_origin)) {
            WAWVR_STEREO_DIAG_ONCE(
                "ReloadDiag %s j_clip magazine-well evaluation failed",
                profile.diagnostic_name);
            retire_current_magazine_viewmodel_fail_closed();
            return;
        }
        const wawvr::xr::EnginePose well_anchor{
            .position = well_origin,
            .axis = context.weapon_axis,
        };
        if (!apply_local_pose(
                well_anchor, profile.insertion_guide_pose_from_anchor,
                &insertion_guide)) {
            retire_current_magazine_viewmodel_fail_closed();
            return;
        }
        insertion_guide_valid = true;
    }

    std::array<std::int32_t, 8> reload_diagnostic{};
    const DetachableMagazineAmmoSnapshot ammo =
        read_detachable_magazine_ammo(
            context, identity, profile,
            simulator_detachable_magazine_probe_enabled()
                ? &reload_diagnostic : nullptr);
    const bool can_reload = detachable_magazine_can_reload(ammo);

    const bool persistent_action_lock = profile.charging.enabled &&
        magazine_chamber_lock_matches_identity(
            profile, context.weapon_index, identity.definition_address);
    const bool automatic_en_bloc = profile.charging.enabled &&
        profile.charging.interaction.control_policy ==
            MagazineChargingControlPolicy::EnBlocAutomatic;
    const bool exact_en_bloc_empty = automatic_en_bloc && ammo.valid &&
        ammo.loaded == 0;
    const bool charging_state_present =
        g_magazine_charging_state.charge_required ||
        g_magazine_charging_state.handle_grabbed ||
        g_magazine_charging_state.spring_returning ||
        g_magazine_charging_state.awaiting_controls_release ||
        g_magazine_charging_state.handle_fraction > 0.0F ||
        g_empty_reload_armed;
    const bool charging_tag_validation_required = profile.charging.enabled &&
        (source_needs_dynamic_tag_validation || reload_transaction_present ||
         possible_reload_transition || persistent_action_lock ||
         charging_state_present || exact_en_bloc_empty);
    if (charging_tag_validation_required) {
        // The evaluated position is a fail-closed audit only. Actual physical
        // charging uses the separately calibrated grip-local handle anchor.
        // Avoid forcing this tag's skeleton evaluation in loaded Ready idle.
        wawvr::xr::Vec3f audited_charging_handle_world{};
        if (!read_viewmodel_world_tag_position(
                viewmodel_dobj, source.charging_handle.tag, viewmodel_pose,
                &audited_charging_handle_world)) {
            WAWVR_STEREO_DIAG_ONCE(
                "ReloadDiag %s j_bolt charging-handle evaluation failed",
                profile.diagnostic_name);
            retire_current_magazine_viewmodel_fail_closed();
            return;
        }
    }
    const bool charging_pose_owned = profile.charging.enabled &&
        (profile.charging.suppress_native_pose_always ||
         persistent_action_lock ||
         g_magazine_charging_state.charge_required ||
         g_magazine_charging_state.handle_grabbed ||
         g_magazine_charging_state.spring_returning ||
         g_magazine_charging_state.awaiting_controls_release);
    void* const expected_pre_skin_dobj =
        charging_pose_owned || profile.feed_door.enabled ? viewmodel_dobj : nullptr;
    const auto expected_pre_skin_surface =
        [&source, &profile, charging_pose_owned](const std::size_t index) noexcept
            -> const XSurface* {
        if (profile.feed_door.enabled && index == source.charging_piece_count) {
            return source.feed_door_surface;
        }
        return charging_pose_owned && index < source.charging_piece_count
            ? source.charging_surfaces[index]
            : nullptr;
    };

    bool publication_changed =
        g_active_weapon_binding.load(std::memory_order_acquire) != 0 ||
        g_active_magazine_binding.load(std::memory_order_acquire) !=
            desired_binding ||
        g_active_weapon_definition_address.load(
            std::memory_order_acquire) != identity.definition_address ||
        g_pre_skin_viewmodel_dobj.load(std::memory_order_acquire) !=
            expected_pre_skin_dobj;
    for (std::size_t index = 0;
         index < g_pre_skin_bolt_surfaces.size(); ++index) {
        publication_changed = publication_changed ||
            g_pre_skin_bolt_surfaces[index].load(
                std::memory_order_acquire) !=
                expected_pre_skin_surface(index);
    }
    if (publication_changed) {
        begin_active_weapon_publication_locked();
    }
    g_magazine_visual_state.supported = true;
    g_magazine_visual_state.profile_id = profile.id;
    g_magazine_visual_state.weapon_index = context.weapon_index;
    g_magazine_visual_state.weapon_registered_count =
        identity.registered_count;
    g_magazine_visual_state.weapon_definition_address =
        identity.definition_address;
    g_magazine_visual_state.viewmodel_dobj = viewmodel_dobj;
    g_magazine_visual_state.source = source;
    g_magazine_visual_state.asset = asset;
    if (!capture_magazine_dobj_witness(
            viewmodel_dobj, source,
            &g_magazine_visual_state.dobj_witness)) {
        WAWVR_STEREO_DIAG_ONCE(
            "ReloadDiag %s could not latch an exact DObj/model witness",
            profile.diagnostic_name);
        retire_current_magazine_viewmodel_fail_closed();
        return;
    }
    if (profile.charging.enabled &&
        !g_magazine_visual_state.closed_charging_handle_pose_latched) {
        const auto& bind = profile.charging.bind_pose;
        BoltRelativePose closed{};
        closed.quaternion[0] = bind.orientation.x;
        closed.quaternion[1] = bind.orientation.y;
        closed.quaternion[2] = bind.orientation.z;
        closed.quaternion[3] = bind.orientation.w;
        closed.translation[0] = bind.translation.x +
            profile.charging.closed_translation_offset.x;
        closed.translation[1] = bind.translation.y +
            profile.charging.closed_translation_offset.y;
        closed.translation[2] = bind.translation.z +
            profile.charging.closed_translation_offset.z;
        closed.translation_weight =
            source.model->base_matrices[
                profile.charging.handle_bone_index]
                .translation_weight;
        closed.valid = true;
        g_magazine_visual_state.closed_charging_handle_relative = closed;
        g_magazine_visual_state.closed_charging_handle_pose_latched = true;
    }
    if (profile.charging.enabled && persistent_action_lock &&
        !g_magazine_charging_state.charge_required) {
        // Re-equipping the same loaded-but-uncharged rifle restores the
        // physical lock independently of its newly created viewmodel DObj.
        reset_magazine_charging(&g_magazine_charging_state);
        g_magazine_charging_state.charge_required = true;
        g_magazine_charging_state.handle_fraction =
            profile.charging.interaction.locked_open_offset_units /
            profile.charging.interaction.travel_units;
        g_magazine_charging_state.awaiting_controls_release =
            profile.charging.interaction.control_policy ==
                MagazineChargingControlPolicy::ManualPullRelease;
        g_magazine_charging_last_update_milliseconds = 0;
        WAWVR_STEREO_DIAG_ONCE(
            "ReloadDiag exact %s chamber lock restored after re-equipping the same uncharged weapon",
            profile.diagnostic_name);
    }
    g_active_weapon_binding.store(0, std::memory_order_release);
    g_active_magazine_binding.store(
        desired_binding, std::memory_order_release);
    g_active_weapon_definition_address.store(
        identity.definition_address, std::memory_order_release);
    g_pre_skin_viewmodel_dobj.store(
        expected_pre_skin_dobj, std::memory_order_release);
    for (std::size_t index = 0;
         index < g_pre_skin_bolt_surfaces.size(); ++index) {
        g_pre_skin_bolt_surfaces[index].store(
            expected_pre_skin_surface(index),
            std::memory_order_release);
    }
    if (publication_changed) {
        finish_active_weapon_publication_locked();
    }

    if (binding_transition) {
        stereo_diagnostic_log(
            "ReloadDiag exact %s detachable-magazine profile bound to map-local weapon index %d (definition=%08X asset=%s bone=%u pieces=%u charging-pieces=%u primary-surface=%u primary-rigid=%u)",
            profile.diagnostic_name, context.weapon_index,
            identity.definition_address, identity.name.data(),
            static_cast<unsigned>(source.global_bone),
            static_cast<unsigned>(source.piece_count),
            static_cast<unsigned>(source.charging_piece_count),
            static_cast<unsigned>(source.surface),
            static_cast<unsigned>(source.rigid_range));
    }

    wawvr::xr::EnginePose left_world{};
    wawvr::xr::Vec3f left_head_local{};
    const bool left_pose_valid = current_hand_pose(
        context, wawvr::xr::Hand::Left,
        &left_world, &left_head_local);
    wawvr::xr::EnginePose magazine_world{};
    const bool magazine_pose_valid = left_pose_valid && apply_local_pose(
        left_world, profile.held_pose_from_controller, &magazine_world);
    const bool input_owned =
        actions.focused &&
        controller_frame_is_current(context.controller, GetTickCount64());
    wawvr::xr::EnginePose right_world{};
    wawvr::xr::Vec3f right_head_local{};
    const bool right_pose_valid = current_hand_pose(
        context, wawvr::xr::Hand::Right,
        &right_world, &right_head_local);
    static_cast<void>(right_head_local);
    const float right_action_squeeze = right.squeeze.active &&
            std::isfinite(right.squeeze.current)
        ? std::clamp(right.squeeze.current, 0.0F, 1.0F)
        : 0.0F;
    const float left_action_squeeze = left.squeeze.active &&
            std::isfinite(left.squeeze.current)
        ? std::clamp(left.squeeze.current, 0.0F, 1.0F)
        : 0.0F;
    const bool right_action_squeeze_held =
        context.right_rifle_gripped ||
        right_action_squeeze >= kSqueezeEngage;
    const bool left_action_squeeze_held =
        context.left_rifle_gripped ||
        left_action_squeeze >= kSqueezeEngage;

    if (profile.charging.enabled &&
        !g_magazine_visual_state
             .closed_charging_handle_grip_offset_valid &&
        g_magazine_visual_state.closed_charging_handle_pose_latched) {
        wawvr::xr::Vec3f audited_closed_handle_world{};
        if (!read_controlled_charging_handle_world(
                viewmodel_dobj, viewmodel_pose, profile, source, 0.0F,
                g_magazine_visual_state.closed_charging_handle_relative,
                &audited_closed_handle_world)) {
            WAWVR_STEREO_DIAG_ONCE(
                "ReloadDiag %s audited closed j_bolt charging anchor evaluation failed",
                profile.diagnostic_name);
            retire_current_magazine_viewmodel_fail_closed();
            return;
        }
        const wawvr::xr::Vec3f handle_from_grip{
            audited_closed_handle_world.x - context.rifle_grip_world.x,
            audited_closed_handle_world.y - context.rifle_grip_world.y,
            audited_closed_handle_world.z - context.rifle_grip_world.z,
        };
        wawvr::xr::Vec3f closed_grip_offset{};
        if (decompose(
                context.weapon_axis, handle_from_grip,
                &closed_grip_offset)) {
            g_magazine_visual_state.closed_charging_handle_grip_offset =
                closed_grip_offset;
            g_magazine_visual_state
                .closed_charging_handle_grip_offset_valid = true;
            WAWVR_STEREO_DIAG_ONCE(
                "ReloadDiag %s closed j_bolt charging anchor derived from the audited bind pose in a private skeleton snapshot (grip-local=%.3f,%.3f,%.3f)",
                profile.diagnostic_name, closed_grip_offset.x,
                closed_grip_offset.y, closed_grip_offset.z);
        }
    }

    wawvr::xr::Vec3f closed_charging_anchor{};
    wawvr::xr::Vec3f current_charging_handle{};
    const bool charging_anchor_valid = profile.charging.enabled &&
        g_magazine_visual_state
            .closed_charging_handle_grip_offset_valid &&
        charging_handle_position(
            context, profile,
            g_magazine_visual_state
                .closed_charging_handle_grip_offset,
            g_magazine_charging_state.handle_fraction,
            &closed_charging_anchor, &current_charging_handle);
    const auto action_hand_sample = [&context, &closed_charging_anchor,
                                     &current_charging_handle, &profile,
                                     charging_anchor_valid](
        const wawvr::xr::EnginePose& hand_world,
        const bool hand_pose_valid,
        float* const forward_coordinate,
        bool* const near_handle) noexcept {
        *forward_coordinate = 0.0F;
        *near_handle = false;
        if (!hand_pose_valid || !charging_anchor_valid) {
            return;
        }
        const wawvr::xr::Vec3f from_closed_anchor{
            hand_world.position.x - closed_charging_anchor.x,
            hand_world.position.y - closed_charging_anchor.y,
            hand_world.position.z - closed_charging_anchor.z,
        };
        *forward_coordinate =
            dot(from_closed_anchor, context.weapon_axis.forward);
        *near_handle =
            distance(hand_world.position, current_charging_handle) <=
            profile.charging.grab_radius_units;
    };
    float right_action_forward_coordinate = 0.0F;
    float left_action_forward_coordinate = 0.0F;
    bool right_hand_near_charging_handle = false;
    bool left_hand_near_charging_handle = false;
    action_hand_sample(
        right_world, right_pose_valid, &right_action_forward_coordinate,
        &right_hand_near_charging_handle);
    action_hand_sample(
        left_world, left_pose_valid, &left_action_forward_coordinate,
        &left_hand_near_charging_handle);
    const float squeeze = left.squeeze.active &&
            std::isfinite(left.squeeze.current)
        ? std::clamp(left.squeeze.current, 0.0F, 1.0F)
        : 0.0F;
    const bool squeeze_held = g_controller_state.feed_grip_was_held
        ? squeeze >= kSqueezeRelease
        : squeeze >= kSqueezeEngage;
    const bool in_feed_zone = magazine_pose_valid &&
        manual_reload_left_waist_contains({
            left_head_local.x, left_head_local.y, left_head_local.z});
    const bool in_insertion_zone = insertion_guide_valid &&
        magazine_pose_valid &&
        distance(magazine_world.position, insertion_guide.position) <=
            profile.insertion_radius_units;
    if (simulator_detachable_magazine_probe_enabled() &&
        (binding_transition || right.primary.changed)) {
        stereo_diagnostic_log(
            "MagazineProbe geometry profile=%s guide=%d/%.3f,%.3f,%.3f clip=%d/%.3f,%.3f,%.3f handle=%d/%.3f,%.3f,%.3f forward=%.4f,%.4f,%.4f heldLocal=%.3f,%.3f,%.3f",
            profile.diagnostic_name, insertion_guide_valid ? 1 : 0,
            insertion_guide.position.x, insertion_guide.position.y,
            insertion_guide.position.z, magazine_pose_valid ? 1 : 0,
            magazine_world.position.x, magazine_world.position.y,
            magazine_world.position.z, charging_anchor_valid ? 1 : 0,
            current_charging_handle.x, current_charging_handle.y,
            current_charging_handle.z, context.weapon_axis.forward.x,
            context.weapon_axis.forward.y, context.weapon_axis.forward.z,
            profile.held_pose_from_controller.translation.x,
            profile.held_pose_from_controller.translation.y,
            profile.held_pose_from_controller.translation.z);
        stereo_diagnostic_log(
            "MagazineProbe input bind=%d weapon=%d seq=%llu focused=%d owned=%d A=%d/%d/%d leftPose=%d waist=%d squeeze=%.2f held=%d insert=%d canReload=%d fields=%d,%d,%d,%d,%d,%d,%d ammo=%d/%d",
            binding_transition ? 1 : 0, context.weapon_index,
            static_cast<unsigned long long>(actions.sequence),
            actions.focused ? 1 : 0, input_owned ? 1 : 0,
            right.primary.active ? 1 : 0,
            right.primary.current ? 1 : 0,
            right.primary.changed ? 1 : 0,
            magazine_pose_valid ? 1 : 0, in_feed_zone ? 1 : 0,
            squeeze, squeeze_held ? 1 : 0,
            in_insertion_zone ? 1 : 0, can_reload ? 1 : 0,
            reload_diagnostic[0], reload_diagnostic[1],
            reload_diagnostic[2], reload_diagnostic[3],
            reload_diagnostic[4], reload_diagnostic[5],
            reload_diagnostic[6],
            (reload_diagnostic[7] >> 16) & 0x7fff,
            reload_diagnostic[7] & 0xffff);
    }

    const bool native_commit_completed =
        consume_detachable_magazine_commit_completion_locked();
    ManualReloadControllerUpdate update =
        update_manual_reload_controller(
            {
                .input_owned = input_owned,
                .action_sequence = actions.sequence,
                .profile_kind = profile.reload_kind,
                .weapon_supported = true,
                .can_reload = can_reload,
                .reset_requested = false,
                .native_action_open = true,
                .native_commit_completed = native_commit_completed,
                .begin_reload_pressed_edge =
                    begin_reload_pressed_edge,
                .feed_grip_held = squeeze_held,
                .feed_pose_valid = magazine_pose_valid,
                .hand_in_feed_device_zone = in_feed_zone,
                .hand_in_insertion_zone = in_insertion_zone,
            },
            &g_controller_state);

    if (update.reload.event == gameplay::ReloadEvent::ReloadArmed) {
        g_empty_reload_armed = profile.charging.enabled && ammo.valid &&
            ammo.loaded == 0;
        if (g_empty_reload_armed) {
            WAWVR_STEREO_DIAG_ONCE(
                "ReloadDiag %s empty magazine transaction latched a post-insertion manual charge requirement",
            profile.diagnostic_name);
        }
    }
    const bool late_empty_snapshot_before_commit =
        profile.charging.enabled && !g_empty_reload_armed &&
        update.reload.manual_reload_active &&
        update.reload.event != gameplay::ReloadEvent::CommitCompleted &&
        ammo.valid && ammo.loaded == 0;
    if (late_empty_snapshot_before_commit) {
        // A final shot and the A edge can straddle command/render sampling. A
        // later exact zero-clip snapshot monotonically upgrades this same
        // in-flight transaction; it is never downgraded before commit.
        g_empty_reload_armed = true;
        WAWVR_STEREO_DIAG_ONCE(
            "ReloadDiag %s in-flight reload upgraded to empty after a later exact zero-clip snapshot",
            profile.diagnostic_name);
    }
    // Establish the chamber lock at the empty reload's A edge, while manual
    // reload ownership is already active. Waiting for the delayed native
    // commit acknowledgement would leave a focus/viewmodel-gap window in which
    // a refilled rifle could become fireable before the render thread consumed
    // CommitCompleted. A pull with no cartridge cannot clear this lock.
    const bool en_bloc_reload_open = automatic_en_bloc &&
        update.reload.event == gameplay::ReloadEvent::ReloadArmed;
    const bool arm_charge = profile.charging.enabled &&
        !g_magazine_charging_state.charge_required &&
        (exact_en_bloc_empty || en_bloc_reload_open ||
         (g_empty_reload_armed && update.reload.manual_reload_active));
    if (arm_charge && (!automatic_en_bloc || exact_en_bloc_empty)) {
        publish_magazine_chamber_lock_locked(
            profile, context.weapon_index, identity.definition_address);
    }
    const bool native_cancel_confirmed = update.reload.request_native_cancel
        ? cancel_native_commit_request_locked()
        : g_native_commit_phase.load(std::memory_order_acquire) ==
              NativeCommitPhase::idle;
    const bool cancel_confirmed_empty_charge = !automatic_en_bloc &&
        g_empty_reload_armed &&
        (update.reload.event == gameplay::ReloadEvent::Cancelled ||
         update.reload.request_native_cancel) &&
        !native_commit_completed && native_cancel_confirmed &&
        ammo.valid && ammo.loaded == 0;
    if (cancel_confirmed_empty_charge) {
        reset_magazine_charging(&g_magazine_charging_state);
        clear_matching_magazine_chamber_lock_locked(
            profile, context.weapon_index, identity.definition_address);
    }
    const bool cancel_confirmed_partial_en_bloc_charge =
        automatic_en_bloc && !exact_en_bloc_empty &&
        (update.reload.event == gameplay::ReloadEvent::Cancelled ||
         update.reload.request_native_cancel) &&
        !native_commit_completed && native_cancel_confirmed;
    if (cancel_confirmed_partial_en_bloc_charge) {
        reset_magazine_charging(&g_magazine_charging_state);
    }
    if (update.reload.event == gameplay::ReloadEvent::CommitCompleted ||
        update.reload.event == gameplay::ReloadEvent::Cancelled ||
        update.reload.request_native_cancel) {
        g_empty_reload_armed = false;
    }

    const std::uint64_t charging_now = GetTickCount64();
    float charging_delta_seconds = 0.0F;
    if (g_magazine_charging_last_update_milliseconds != 0 &&
        charging_now >= g_magazine_charging_last_update_milliseconds) {
        charging_delta_seconds = std::clamp(
            static_cast<float>(
                charging_now -
                g_magazine_charging_last_update_milliseconds) /
                1000.0F,
            0.0F, 0.25F);
    }
    g_magazine_charging_last_update_milliseconds = charging_now;
    MagazineChargingUpdate charging_update{};
    if (profile.charging.enabled) {
        charging_update = update_magazine_charging(
            profile.charging.interaction,
            {
                .enabled = true,
                .focused = input_owned,
                .weapon_supported = true,
                .reset_requested = false,
                .action_sequence = actions.sequence,
                .delta_seconds = charging_delta_seconds,
                .arm_charge = arm_charge,
                .cartridge_available = ammo.valid && ammo.loaded > 0,
                .automatic_spring_release = automatic_en_bloc &&
                    g_magazine_charging_state.charge_required &&
                    ammo.valid && ammo.loaded > 0 &&
                    !update.reload.manual_reload_active,
                .left_rifle_gripped = context.left_rifle_gripped,
                .right_hand_pose_valid = right_pose_valid,
                .right_hand_near_handle =
                    right_hand_near_charging_handle,
                .right_grip_held = right_action_squeeze_held,
                .right_trigger_active = right.trigger.active,
                .right_trigger_value = right.trigger.current,
                .right_hand_forward_coordinate =
                    right_action_forward_coordinate,
                .right_rifle_gripped = context.right_rifle_gripped,
                .left_hand_pose_valid = left_pose_valid,
                .left_hand_near_handle =
                    left_hand_near_charging_handle,
                .left_grip_held = left_action_squeeze_held,
                .left_trigger_active = left.trigger.active,
                .left_trigger_value = left.trigger.current,
                .left_hand_forward_coordinate =
                    left_action_forward_coordinate,
            },
            &g_magazine_charging_state);
    }

    if (charging_update.event == MagazineChargingEvent::ChargeRequired) {
        WAWVR_STEREO_DIAG_ONCE(
            automatic_en_bloc
                ? "ReloadDiag %s en-bloc reload locked its action fully open (fraction=%.6f)"
                : "ReloadDiag %s empty reload armed a persistent chamber requirement at the native empty-open charging position (fraction=%.6f)",
            profile.diagnostic_name, charging_update.handle_fraction);
    } else if (charging_update.event == MagazineChargingEvent::Grabbed) {
        WAWVR_STEREO_DIAG_ONCE(
            "ReloadDiag %s index trigger latched the %s charging handle while the opposite hand retained the weapon",
            charging_update.manipulating_left_hand ? "left" : "right",
            profile.diagnostic_name);
    } else if (charging_update.event ==
               MagazineChargingEvent::FullyOpened) {
        WAWVR_STEREO_DIAG_ONCE(
            "ReloadDiag %s charging handle reached the exact native full-rear endpoint",
            profile.diagnostic_name);
    } else if (charging_update.event ==
               MagazineChargingEvent::SpringReleased) {
        WAWVR_STEREO_DIAG_ONCE(
            "ReloadDiag %s charging handle released into its %.6f-second profiled spring return",
            profile.diagnostic_name,
            profile.charging.interaction.spring_return_seconds);
    } else if (charging_update.event == MagazineChargingEvent::Charged) {
        clear_matching_magazine_chamber_lock_locked(
            profile, context.weapon_index, identity.definition_address);
        WAWVR_STEREO_DIAG_ONCE(
            automatic_en_bloc
                ? "ReloadDiag %s loaded en-bloc clip released the action; persistent attack lock cleared"
                : "ReloadDiag %s manual post-reload action completed; persistent attack lock cleared",
            profile.diagnostic_name);
    } else if (charging_update.event == MagazineChargingEvent::Released &&
               charging_update.charge_required) {
        WAWVR_STEREO_DIAG_ONCE(
            "ReloadDiag %s charging gesture released before valid completion; empty-open lock preserved",
            profile.diagnostic_name);
    }

    if (simulator_detachable_magazine_probe_enabled() &&
        (right.trigger.changed || left.trigger.changed || arm_charge ||
         charging_update.event != MagazineChargingEvent::None)) {
        const bool diagnostic_left =
            g_magazine_charging_state.manipulating_hand_selected &&
            g_magazine_charging_state.manipulating_left_hand;
        const auto& diagnostic_actions = diagnostic_left ? left : right;
        const bool diagnostic_retaining_grip = diagnostic_left
            ? context.right_rifle_gripped : context.left_rifle_gripped;
        const bool diagnostic_squeeze = diagnostic_left
            ? left_action_squeeze_held : right_action_squeeze_held;
        const bool diagnostic_pose = diagnostic_left
            ? left_pose_valid : right_pose_valid;
        const bool diagnostic_near = diagnostic_left
            ? left_hand_near_charging_handle
            : right_hand_near_charging_handle;
        const float diagnostic_forward = diagnostic_left
            ? left_action_forward_coordinate
            : right_action_forward_coordinate;
        stereo_diagnostic_log(
            "MagazineProbe charge seq=%llu hand=%s trigger=%d/%.3f/%d event=%u required=%d grabbed=%d spring=%d fraction=%.6f block=%d retainingGrip=%d manipulatingGrip=%d pose=%d near=%d handForward=%.3f handle=%.3f,%.3f,%.3f ammo=%d/%d",
            static_cast<unsigned long long>(actions.sequence),
            diagnostic_left ? "left" : "right",
            diagnostic_actions.trigger.active ? 1 : 0,
            diagnostic_actions.trigger.current,
            diagnostic_actions.trigger.changed ? 1 : 0,
            static_cast<unsigned>(charging_update.event),
            charging_update.charge_required ? 1 : 0,
            charging_update.handle_grabbed ? 1 : 0,
            g_magazine_charging_state.spring_returning ? 1 : 0,
            charging_update.handle_fraction,
            charging_update.block_attack ? 1 : 0,
            diagnostic_retaining_grip ? 1 : 0,
            diagnostic_squeeze ? 1 : 0,
            diagnostic_pose ? 1 : 0,
            diagnostic_near ? 1 : 0,
            diagnostic_forward,
            current_charging_handle.x, current_charging_handle.y,
            current_charging_handle.z,
            ammo.valid ? ammo.reserve : -1,
            ammo.valid ? ammo.loaded : -1);
    }

    if (simulator_detachable_magazine_probe_enabled() &&
        (right.primary.changed || update.reload.event !=
             gameplay::ReloadEvent::None ||
         update.feed_grip_pressed_edge || update.feed_grip_released_edge)) {
        stereo_diagnostic_log(
            "MagazineProbe update stage=%u event=%u Aedge=%d gripEdges=%d/%d open=%d commit=%d cancel=%d hide=%d detached=%d",
            static_cast<unsigned>(update.reload.stage),
            static_cast<unsigned>(update.reload.event),
            update.begin_reload_pressed_edge ? 1 : 0,
            update.feed_grip_pressed_edge ? 1 : 0,
            update.feed_grip_released_edge ? 1 : 0,
            update.reload.request_native_action_open ? 1 : 0,
            update.reload.request_native_commit ? 1 : 0,
            update.reload.request_native_cancel ? 1 : 0,
            update.reload.hide_authored_feed_device ? 1 : 0,
            update.reload.render_detached_feed_device ? 1 : 0);
    }

    if (update.reload.request_native_action_open) {
        WAWVR_STEREO_DIAG_ONCE(
            "ReloadDiag A ejected the %s magazine without starting T4's right-hand reload animation",
            profile.diagnostic_name);
    }
    if (update.reload.event == gameplay::ReloadEvent::FeedDeviceGrabbed) {
        WAWVR_STEREO_DIAG_ONCE(
            "ReloadDiag left grip drew a fresh %s magazine from the off-hand hip",
            profile.diagnostic_name);
    }
    if (update.reload.request_native_commit) {
        static_cast<void>(publish_native_commit_request_locked());
        WAWVR_STEREO_DIAG_ONCE(
            "ReloadDiag tracked %s magazine released in j_clip well; exactly-one ammo transfer requested",
            profile.diagnostic_name);
    }
    // Cancellation was already attempted atomically before deciding whether
    // the empty chamber lock could be cleared. Never retry after a claimed
    // native transfer and reinterpret its later phase as pre-commit idle.

    if (!set_authored_magazine_hidden_locked(
            update.reload.hide_authored_feed_device ||
            exact_en_bloc_empty || profile.hide_authored_feed_device_always)) {
        WAWVR_STEREO_DIAG_ONCE(
            "ReloadDiag %s could not apply reversible j_clip visibility ownership",
            profile.diagnostic_name);
        retire_current_magazine_viewmodel_fail_closed();
        return;
    }
    if (update.reload.render_detached_feed_device) {
        if (!magazine_pose_valid) {
            WAWVR_STEREO_DIAG_ONCE(
                "ReloadDiag detached %s magazine lost its current left-hand pose; transaction cancelled and authored visibility restored",
                profile.diagnostic_name);
            retire_current_magazine_viewmodel_fail_closed();
            return;
        }
        if (update.reload.orientation_source ==
            gameplay::ReloadOrientationSource::InsertionGuide) {
            if (!insertion_guide_valid) {
                WAWVR_STEREO_DIAG_ONCE(
                    "ReloadDiag %s requested insertion orientation without a validated j_clip guide",
                    profile.diagnostic_name);
                retire_current_magazine_viewmodel_fail_closed();
                return;
            }
            // Position remains the current left-controller sample; only the
            // orientation becomes forgiving near the magazine well.
            magazine_world.axis = insertion_guide.axis;
        }
        if (!render_magazine(
                asset, magazine_world, context.camera_origin,
                viewmodel_pose)) {
            WAWVR_STEREO_DIAG_ONCE(
                "ReloadDiag detached %s magazine render preflight failed; transaction cancelled and authored visibility restored",
                profile.diagnostic_name);
            retire_current_magazine_viewmodel_fail_closed();
            return;
        }
        WAWVR_STEREO_DIAG_ONCE(
            "ReloadDiag detached %s magazine rendered from the current left-glove pose",
            profile.diagnostic_name);
    }

    g_supported.store(true, std::memory_order_release);
    if (charging_update.block_attack) {
        // Establish the chamber lock before retiring an empty magazine
        // transaction so no command sample can observe both gates false.
        g_cycle_lock.store(true, std::memory_order_release);
        g_manual_active.store(
            update.reload.manual_reload_active,
            std::memory_order_release);
    } else {
        // On begin, establish manual ownership before clearing any prior
        // control-release lock.
        g_manual_active.store(
            update.reload.manual_reload_active,
            std::memory_order_release);
        g_cycle_lock.store(false, std::memory_order_release);
    }
    g_reserve_right_grip.store(
        charging_update.reserve_right_grip,
        std::memory_order_release);
    g_reserve_left_grip.store(
        update.reload.manual_reload_active ||
            charging_update.reserve_left_grip,
        std::memory_order_release);
    const bool left_action_candidate = profile.charging.enabled &&
        !automatic_en_bloc && charging_update.charge_required &&
        !g_magazine_charging_state.spring_returning &&
        !g_magazine_charging_state.awaiting_controls_release &&
        context.right_rifle_gripped &&
        !left_action_squeeze_held && left_pose_valid &&
        left_hand_near_charging_handle &&
        std::isfinite(left_action_forward_coordinate);
    g_block_new_left_trigger_action.store(
        update.reload.manual_reload_active ||
            charging_update.reserve_left_grip || left_action_candidate,
        std::memory_order_release);
    g_shot_pending.store(false, std::memory_order_release);
}

}  // namespace

bool resolve_t4_script_string(
    const char* const name,
    std::uint16_t* const value) noexcept {
    if (value == nullptr) {
        return false;
    }
    *value = 0;
    if (!g_enabled.load(std::memory_order_acquire) || g_sl_find_string == 0 ||
        name == nullptr || *name == '\0') {
        return false;
    }
    const std::uint32_t resolved = find_script_string(name);
    if (resolved == 0 || resolved > 0xFFFFu) {
        return false;
    }
    *value = static_cast<std::uint16_t>(resolved);
    return true;
}

ManualReloadRuntimeInstallResult install_manual_reload_runtime(
    const wawvr::t4::ValidatedBindings& bindings) noexcept {
    ManualReloadRuntimeInstallResult result{};
    if (g_installed.load(std::memory_order_acquire)) {
        result.status = ManualReloadRuntimeStatus::already_installed;
        return result;
    }
    g_headset_svt40_selection_state.store(
        HeadsetSvt40SelectionState::unknown,
        std::memory_order_release);
    g_headset_m1garand_gl_selection_state.store(
        HeadsetM1GarandGlSelectionState::unknown,
        std::memory_order_release);
#if !defined(_MSC_VER) || !defined(_M_IX86)
    static_cast<void>(bindings);
    result.status = ManualReloadRuntimeStatus::not_applicable;
    return result;
#else
    std::array<wchar_t, 8> automatic_reload_value{};
    const DWORD automatic_reload_length = GetEnvironmentVariableW(
        kAutomaticReloadEnvironmentVariable, automatic_reload_value.data(),
        static_cast<DWORD>(automatic_reload_value.size()));
    g_automatic_reload.store(
        automatic_reload_length > 0 &&
            automatic_reload_length < automatic_reload_value.size() &&
            automatic_reload_setting_enabled(std::wstring_view{
                automatic_reload_value.data(), automatic_reload_length}),
        std::memory_order_release);
    if (select_t4_layout_family(bindings.profile()) !=
        T4LayoutFamily::single_player_1_7_1263) {
        result.status = ManualReloadRuntimeStatus::not_applicable;
        return result;
    }
    const auto module = bindings.module();
    const auto request_context = module.address(
        kReloadRequestContextRva, kReloadRequestContext.size());
    const auto empty_context = module.address(
        kReloadEmptyContextRva, kReloadEmptyContext.size());
    const auto rechamber_visual_context = module.address(
        kRechamberVisualAnimContextRva,
        kRechamberVisualAnimContext.size());
    const auto pre_skin_context = module.address(
        kPreSkinSurfaceContextRva,
        kPreSkinSurfaceContext.size());
    const auto pre_skin_surface = module.address(
        kPreSkinXSurfaceRva, kPreSkinXSurfacePrologue.size());
    const auto begin = module.address(
        kBeginWeaponReloadRva,
        kBeginReloadEpilogueOffset + kBeginReloadEpilogue.size());
    const auto reload_clip = module.address(
        kReloadClipRva,
        kReloadClipEpilogueOffset + kReloadClipEpilogue.size());
    const auto start_anim = module.address(
        kStartWeaponAnimRva, kStartWeaponAnimSentinel.size());
    const auto dobj_create = module.address(
        kDObjCreateRva,
        kDObjCreateEpilogueOffset + kDObjCreateEpilogue.size());
    const auto add_scene = module.address(
        kAddDObjToSceneRva,
        kAddDObjSceneIndexStoreOffset + kAddDObjSceneIndexStore.size());
    const auto control_tag = module.address(
        kSetControlTagAnglesRva, kSetControlTagAnglesPrologue.size());
    const auto sl_find = module.address(
        kSlFindStringRva, kSlFindStringSentinel.size());
    const auto db_find_xasset_header = module.address(
        kDbFindXAssetHeaderRva, 1);
    const auto weapon_info = module.address(
        kWeaponInfoArrayRva,
        kWeaponInfoStride);
    const auto viewmodel_world_origin = module.address(
        kViewmodelWorldOriginRva, sizeof(wawvr::xr::Vec3f));
    const auto viewmodel_pose = module.address(
        kViewmodelPoseRva, kPoseSize);
    const auto scene_xmodel_index_pointer = module.address(
        kSceneXModelIndexPointerRva, sizeof(std::uintptr_t));
    const auto scene_dobj_index_pointer = module.address(
        kSceneDObjIndexPointerRva, sizeof(std::uintptr_t));
    if (!request_context.has_value() || !empty_context.has_value() ||
        !rechamber_visual_context.has_value() ||
        !pre_skin_context.has_value() ||
        !pre_skin_surface.has_value() ||
        !begin.has_value() || !reload_clip.has_value() ||
        !start_anim.has_value() || !dobj_create.has_value() ||
        !add_scene.has_value() || !control_tag.has_value() ||
        !sl_find.has_value() || !db_find_xasset_header.has_value() ||
        !weapon_info.has_value() ||
        !viewmodel_world_origin.has_value() || !viewmodel_pose.has_value() ||
        !scene_xmodel_index_pointer.has_value() ||
        !scene_dobj_index_pointer.has_value()) {
        result.status = ManualReloadRuntimeStatus::address_out_of_range;
        return result;
    }
    auto* const request_bytes =
        reinterpret_cast<std::uint8_t*>(*request_context);
    auto* const empty_bytes =
        reinterpret_cast<std::uint8_t*>(*empty_context);
    auto* const rechamber_visual_bytes =
        reinterpret_cast<std::uint8_t*>(*rechamber_visual_context);
    auto* const pre_skin_bytes =
        reinterpret_cast<std::uint8_t*>(*pre_skin_context);
    auto* const request_call = request_bytes + kReloadRequestCallOffset;
    auto* const empty_call = empty_bytes + kReloadEmptyCallOffset;
    auto* const rechamber_visual_call =
        rechamber_visual_bytes + kRechamberVisualAnimCallOffset;
    auto* const pre_skin_call =
        pre_skin_bytes + kPreSkinSurfaceCallOffset;
    if (!context_matches(
            request_bytes, request_call, kReloadRequestContext,
            kReloadRequestCallOffset) ||
        !context_matches(
            empty_bytes, empty_call, kReloadEmptyContext,
            kReloadEmptyCallOffset) ||
        !context_matches(
            rechamber_visual_bytes, rechamber_visual_call,
            kRechamberVisualAnimContext,
            kRechamberVisualAnimCallOffset) ||
        !context_matches(
            pre_skin_bytes, pre_skin_call,
            kPreSkinSurfaceContext,
            kPreSkinSurfaceCallOffset) ||
        decode_rel32(request_call) != *begin ||
        decode_rel32(empty_call) != *begin ||
        decode_rel32(rechamber_visual_call) != *start_anim ||
        decode_rel32(pre_skin_call) != *pre_skin_surface ||
        !bytes_equal(
            reinterpret_cast<const void*>(*pre_skin_surface),
            kPreSkinXSurfacePrologue) ||
        !bytes_equal(reinterpret_cast<const void*>(*begin),
                     kBeginReloadPrologue) ||
        !bytes_equal(
            reinterpret_cast<const void*>(
                *begin + kBeginReloadEpilogueOffset),
            kBeginReloadEpilogue) ||
        !bytes_equal(reinterpret_cast<const void*>(*reload_clip),
                     kReloadClipPrologue) ||
        !bytes_equal(
            reinterpret_cast<const void*>(
                *reload_clip + kReloadClipEpilogueOffset),
            kReloadClipEpilogue) ||
        !bytes_equal(reinterpret_cast<const void*>(*start_anim),
                     kStartWeaponAnimSentinel) ||
        !bytes_equal(reinterpret_cast<const void*>(*dobj_create),
                     kDObjCreatePrologue) ||
        !bytes_equal(
            reinterpret_cast<const void*>(
                *dobj_create + kDObjCreateEpilogueOffset),
            kDObjCreateEpilogue) ||
        !bytes_equal(reinterpret_cast<const void*>(*add_scene),
                     kAddDObjToScenePrologue) ||
        !bytes_equal(
            reinterpret_cast<const void*>(
                *add_scene + kAddDObjSceneIndexStoreOffset),
            kAddDObjSceneIndexStore) ||
        !bytes_equal(reinterpret_cast<const void*>(*control_tag),
                     kSetControlTagAnglesPrologue) ||
        !bytes_equal(reinterpret_cast<const void*>(*sl_find),
                     kSlFindStringSentinel)) {
        result.status = ManualReloadRuntimeStatus::fingerprint_mismatch;
        return result;
    }

    const auto hook = reinterpret_cast<std::uintptr_t>(
        &manual_reload_begin_bridge);
    const auto rechamber_visual_hook = reinterpret_cast<std::uintptr_t>(
        &manual_reload_rechamber_visual_anim_bridge);
    const auto pre_skin_hook = reinterpret_cast<std::uintptr_t>(
        &manual_reload_pre_skin_surface_bridge);
    const std::array<PeerThreadPatchRange, 4> ranges{{
        {reinterpret_cast<std::uintptr_t>(request_call), 5},
        {reinterpret_cast<std::uintptr_t>(empty_call), 5},
        {reinterpret_cast<std::uintptr_t>(rechamber_visual_call), 5},
        {reinterpret_cast<std::uintptr_t>(pre_skin_call), 5},
    }};
    SuspendedPeerThreads peers;
    PeerThreadQuiesceResult quiesce{};
    if (!peers.suspend(ranges, &quiesce)) {
        result.status = ManualReloadRuntimeStatus::thread_suspend_failed;
        result.system_error = quiesce.system_error;
        return result;
    }
    g_original_begin_reload = *begin;
    g_original_pre_skin_surface = *pre_skin_surface;
    g_reload_clip = *reload_clip;
    g_start_weapon_anim = *start_anim;
    g_dobj_create = *dobj_create;
    g_add_dobj_to_scene = *add_scene;
    g_set_control_tag_angles = *control_tag;
    g_sl_find_string = *sl_find;
    g_db_find_xasset_header = *db_find_xasset_header;
    g_weapon_info_array = *weapon_info;
    g_viewmodel_world_origin = *viewmodel_world_origin;
    g_viewmodel_pose = *viewmodel_pose;
    g_scene_index_pointers = {
        *scene_xmodel_index_pointer, *scene_dobj_index_pointer};
    const auto restore_original_calls = [&]() noexcept {
        // Keep every original target published even if Windows reports a
        // failure after writing an E8. A partially retained bridge therefore
        // still calls native safely while g_enabled remains false.
        bool restored = true;
        DWORD rollback_error = 0;
        const auto restore_site = [&](
            std::uint8_t* const site,
            const std::uintptr_t target) noexcept {
            DWORD site_error = 0;
            const bool site_restored =
                patch_call(site, target, &site_error);
            if (!site_restored && rollback_error == 0) {
                rollback_error = site_error != 0
                    ? site_error
                    : ERROR_WRITE_FAULT;
            }
            restored = site_restored && restored;
        };
        restore_site(pre_skin_call, *pre_skin_surface);
        restore_site(rechamber_visual_call, *start_anim);
        restore_site(empty_call, *begin);
        restore_site(request_call, *begin);
        if (!restored && result.system_error == 0) {
            result.system_error = rollback_error;
        }
        return restored &&
               decode_rel32(pre_skin_call) == *pre_skin_surface &&
               decode_rel32(rechamber_visual_call) == *start_anim &&
               decode_rel32(empty_call) == *begin &&
               decode_rel32(request_call) == *begin;
    };
    DWORD patch_error = 0;
    if (!patch_call(request_call, hook, &patch_error)) {
        result.system_error = static_cast<std::uint32_t>(patch_error);
        if (!restore_original_calls() && result.system_error == 0) {
            result.system_error = ERROR_WRITE_FAULT;
        }
        result.status = ManualReloadRuntimeStatus::patch_failed;
        return result;
    }
    if (!patch_call(empty_call, hook, &patch_error)) {
        result.system_error = static_cast<std::uint32_t>(patch_error);
        if (!restore_original_calls() && result.system_error == 0) {
            result.system_error = ERROR_WRITE_FAULT;
        }
        result.status = ManualReloadRuntimeStatus::patch_failed;
        return result;
    }
    if (!patch_call(
            rechamber_visual_call, rechamber_visual_hook,
            &patch_error)) {
        result.system_error = static_cast<std::uint32_t>(patch_error);
        if (!restore_original_calls() && result.system_error == 0) {
            result.system_error = ERROR_WRITE_FAULT;
        }
        result.status = ManualReloadRuntimeStatus::patch_failed;
        return result;
    }
    if (!patch_call(pre_skin_call, pre_skin_hook, &patch_error)) {
        result.system_error = static_cast<std::uint32_t>(patch_error);
        if (!restore_original_calls() && result.system_error == 0) {
            result.system_error = ERROR_WRITE_FAULT;
        }
        result.status = ManualReloadRuntimeStatus::patch_failed;
        return result;
    }
    if (decode_rel32(request_call) != hook ||
        decode_rel32(empty_call) != hook ||
        decode_rel32(rechamber_visual_call) != rechamber_visual_hook ||
        decode_rel32(pre_skin_call) != pre_skin_hook) {
        if (!restore_original_calls() && result.system_error == 0) {
            result.system_error = ERROR_WRITE_FAULT;
        }
        result.status = ManualReloadRuntimeStatus::patch_failed;
        return result;
    }
    // No bridge can do mod work while g_enabled is false, so release peers
    // before state initialization and diagnostics. In particular, never hold a
    // suspended thread across filesystem/CRT logging because it may own the
    // logger mutex this bootstrap thread is about to acquire.
    peers.resume();
    clear_runtime_state();
    g_enabled.store(true, std::memory_order_release);
    g_installed.store(true, std::memory_order_release);
    result.status = ManualReloadRuntimeStatus::installed;
    WAWVR_STEREO_DIAG_ONCE(
        "ReloadDiag exact SP manual reload bridges installed at 0x1FACD/0x2092C with automatic rechamber visual suppression at 0x1E805 and private rigid-surface bolt-pose seal at 0x31E288");
    if (g_automatic_reload.load(std::memory_order_acquire)) {
        WAWVR_STEREO_DIAG_ONCE(
            "ReloadDiag automatic reload enabled; A uses the complete native reload path and physical feed interactions are bypassed");
    }
    return result;
#endif
}

void service_manual_reload_simulator_asset_inventory() noexcept {
    if (!g_enabled.load(std::memory_order_acquire)) {
        return;
    }
    diagnose_simulator_requested_magazine_asset_from_database();
}

HeadsetSvt40SelectionState headset_svt40_selection_state() noexcept {
    return g_headset_svt40_selection_state.load(std::memory_order_acquire);
}

void observe_headset_svt40_weapon_identity(
    const std::array<char, 64>& internal_name,
    const bool identity_valid) noexcept {
    if (!identity_valid) {
        return;
    }
    g_headset_svt40_selection_state.store(
        owned_c_string_equals(internal_name, "svt40")
            ? HeadsetSvt40SelectionState::svt40_selected
            : HeadsetSvt40SelectionState::other_weapon,
        std::memory_order_release);
}

HeadsetM1GarandGlSelectionState
headset_m1garand_gl_selection_state() noexcept {
    return g_headset_m1garand_gl_selection_state.load(
        std::memory_order_acquire);
}

void observe_headset_m1garand_gl_weapon_identity(
    const std::array<char, 64>& internal_name,
    const bool identity_valid) noexcept {
    if (!identity_valid) {
        return;
    }
    const bool rifle_selected =
        owned_c_string_equals(internal_name, "m1garand_gl");
    const bool launcher_selected =
        owned_c_string_equals(internal_name, "m7_launcher");
    g_headset_m1garand_gl_selection_state.store(
        rifle_selected
            ? HeadsetM1GarandGlSelectionState::m1garand_gl_selected
            : launcher_selected
                ? HeadsetM1GarandGlSelectionState::m7_launcher_selected
                : HeadsetM1GarandGlSelectionState::other_weapon,
        std::memory_order_release);
}

void update_manual_reload_viewmodel(
    void* const viewmodel_dobj,
    const void* const viewmodel_pose,
    const ManualReloadViewmodelContext& context) noexcept {
    if (!g_enabled.load(std::memory_order_acquire)) {
        return;
    }
    // Publish the already validated rendered-weapon identity before any of the
    // steady-state fast paths below can return. The headset SVT test selector
    // uses this read-only witness to stop cycling only when the exact SVT-40
    // is visibly held; keeping it ahead of the magazine-idle shortcut avoids
    // leaving the selector permanently unknown for an otherwise idle weapon.
    observe_headset_svt40_weapon_identity(
        context.weapon_internal_name,
        context.weapon_index > 0 && context.weapon_definition_valid);
    observe_headset_m1garand_gl_weapon_identity(
        context.weapon_internal_name,
        context.weapon_index > 0 && context.weapon_definition_valid);
    const std::uint64_t update_session_epoch =
        g_manual_reload_session_epoch.load(std::memory_order_acquire);
    // This option is immutable for the process and install publishes it only
    // after clear_runtime_state(). No physical state can predate this gate, so
    // avoid weapon identity, DObj discovery, cache, and gameplay-update work;
    // retain only the lightweight session observer.
    if (g_automatic_reload.load(std::memory_order_acquire)) {
        observe_manual_reload_session(context.player_state);
        return;
    }

    // Loaded detachable-magazine weapons spend most of their lives in Ready.
    // Once the exact weapon/DObj/source has been validated, no model, ammo,
    // hand-pose, tag, or asset work can affect that steady state until an A
    // edge, identity change, reload transaction, or charging requirement. Keep
    // controller edge baselines current, but bypass the expensive rediscovery
    // path during ordinary held-weapon aiming.
    bool serviced_steady_magazine_idle = false;
    bool observed_session_in_idle_lock = false;
    const auto& idle_actions = context.controller.frame.actions;
    const auto& idle_left = idle_actions.hands[
        static_cast<std::uint32_t>(wawvr::xr::Hand::Left)];
    const auto& idle_right = idle_actions.hands[
        static_cast<std::uint32_t>(wawvr::xr::Hand::Right)];
    const bool idle_a_edge = idle_right.primary.active &&
        idle_right.primary.current && idle_right.primary.changed;
    const bool idle_input_owned = idle_actions.focused &&
        controller_frame_is_current(context.controller, GetTickCount64());
    const bool idle_context_valid = viewmodel_dobj != nullptr &&
        viewmodel_pose != nullptr && context.weapon_index > 0 &&
        context.weapon_definition_valid &&
        context.weapon_registered_count > 0 &&
        context.weapon_definition_address != 0 &&
        context.muzzle_valid && finite_vector(context.muzzle_world) &&
        finite_vector(context.rifle_grip_world) &&
        valid_basis(context.weapon_axis);
    if (idle_input_owned && idle_context_valid && !idle_a_edge &&
        context.player_command_time_valid) {
        try {
            const std::lock_guard<std::mutex> lock(g_state_mutex);
            observe_manual_reload_command_time_locked(
                context.player_command_time);
            observed_session_in_idle_lock = true;
            const std::uint64_t packed =
                g_active_magazine_binding.load(std::memory_order_acquire);
            DetachableMagazineWeaponBinding binding{};
            const DetachableMagazineWeaponProfile* const idle_profile =
                profile_for_magazine_binding(packed, &binding);
            RuntimeMagazineVisualState& visual = g_magazine_visual_state;
            const bool charging_idle =
                !g_magazine_charging_state.charge_required &&
                !g_magazine_charging_state.handle_grabbed &&
                !g_magazine_charging_state.fully_opened &&
                !g_magazine_charging_state.spring_returning &&
                !g_magazine_charging_state.awaiting_controls_release &&
                g_magazine_charging_state.handle_fraction == 0.0F &&
                g_magazine_charging_state.spring_return_elapsed_seconds ==
                    0.0F;
            const bool exact_cached_binding =
                g_enabled.load(std::memory_order_acquire) &&
                g_manual_reload_session_epoch.load(
                    std::memory_order_acquire) == update_session_epoch &&
                idle_profile != nullptr &&
                (!idle_profile->charging.enabled ||
                 idle_profile->charging.interaction.control_policy !=
                     MagazineChargingControlPolicy::EnBlocAutomatic) &&
                binding.weapon_index == context.weapon_index &&
                g_active_weapon_binding.load(std::memory_order_acquire) == 0 &&
                visual.supported && visual.profile_id == idle_profile->id &&
                visual.weapon_index == context.weapon_index &&
                visual.weapon_registered_count ==
                    context.weapon_registered_count &&
                visual.weapon_definition_address ==
                    context.weapon_definition_address &&
                std::string_view{context.weapon_internal_name.data()} ==
                    idle_profile->internal_weapon_name &&
                visual.viewmodel_dobj == viewmodel_dobj &&
                visual.asset != nullptr && visual.asset->ready &&
                g_active_weapon_definition_address.load(
                    std::memory_order_acquire) ==
                    context.weapon_definition_address &&
                g_active_weapon_publication_generation.load(
                    std::memory_order_acquire) != 0 &&
                (g_active_weapon_publication_generation.load(
                     std::memory_order_acquire) & 1U) == 0 &&
                g_supported.load(std::memory_order_acquire) &&
                !g_manual_active.load(std::memory_order_acquire) &&
                !g_cycle_lock.load(std::memory_order_acquire) &&
                !g_shot_pending.load(std::memory_order_acquire) &&
                g_controller_state.input_owned &&
                g_controller_state.reload.stage ==
                    gameplay::ReloadStage::Ready &&
                g_native_commit_phase.load(std::memory_order_acquire) ==
                    NativeCommitPhase::idle &&
                g_native_commit_publication_generation.load(
                    std::memory_order_acquire) == 0 &&
                g_detachable_magazine_commit_completed_at_milliseconds.load(
                    std::memory_order_acquire) == 0 &&
                !visual.authored_hidden && !g_empty_reload_armed &&
                charging_idle &&
                (!idle_profile->charging.enabled ||
                 (visual.closed_charging_handle_pose_latched &&
                  visual.closed_charging_handle_grip_offset_valid)) &&
                !magazine_chamber_lock_matches_identity(
                    *idle_profile, binding.weapon_index,
                    visual.weapon_definition_address) &&
                magazine_dobj_witness_matches(
                    viewmodel_dobj, visual.source,
                    visual.dobj_witness);
            if (exact_cached_binding) {
                const float feed_squeeze = idle_left.squeeze.active &&
                        std::isfinite(idle_left.squeeze.current)
                    ? std::clamp(
                          idle_left.squeeze.current, 0.0F, 1.0F)
                    : 0.0F;
                const bool feed_squeeze_held =
                    g_controller_state.feed_grip_was_held
                    ? feed_squeeze >= kSqueezeRelease
                    : feed_squeeze >= kSqueezeEngage;
                const ManualReloadControllerUpdate baseline =
                    update_manual_reload_controller(
                        {
                            .input_owned = true,
                            .action_sequence = idle_actions.sequence,
                            .profile_kind = idle_profile->reload_kind,
                            .weapon_supported = true,
                            .can_reload = false,
                            .reset_requested = false,
                            .native_action_open = true,
                            .native_commit_completed = false,
                            .begin_reload_pressed_edge = false,
                            .feed_grip_held = feed_squeeze_held,
                            .feed_pose_valid = false,
                            .hand_in_feed_device_zone = false,
                            .hand_in_insertion_zone = false,
                        },
                        &g_controller_state);
                if (baseline.reload.stage == gameplay::ReloadStage::Ready &&
                    baseline.reload.event == gameplay::ReloadEvent::None) {
                    if (idle_profile->charging.enabled) {
                        const float idle_right_squeeze =
                            idle_right.squeeze.active &&
                                std::isfinite(idle_right.squeeze.current)
                            ? std::clamp(
                                  idle_right.squeeze.current, 0.0F, 1.0F)
                            : 0.0F;
                        const float idle_left_squeeze =
                            idle_left.squeeze.active &&
                                std::isfinite(idle_left.squeeze.current)
                            ? std::clamp(
                                  idle_left.squeeze.current, 0.0F, 1.0F)
                            : 0.0F;
                        static_cast<void>(update_magazine_charging(
                            idle_profile->charging.interaction,
                            {
                                .enabled = true,
                                .focused = true,
                                .weapon_supported = true,
                                .reset_requested = false,
                                .action_sequence = idle_actions.sequence,
                                .delta_seconds = 0.0F,
                                .arm_charge = false,
                                .cartridge_available = true,
                                .automatic_spring_release = false,
                                .left_rifle_gripped =
                                    context.left_rifle_gripped,
                                .right_hand_pose_valid = false,
                                .right_hand_near_handle = false,
                                .right_grip_held =
                                    context.right_rifle_gripped ||
                                    idle_right_squeeze >= kSqueezeEngage,
                                .right_trigger_active =
                                    idle_right.trigger.active,
                                .right_trigger_value =
                                    idle_right.trigger.current,
                                .right_hand_forward_coordinate = 0.0F,
                                .right_rifle_gripped =
                                    context.right_rifle_gripped,
                                .left_hand_pose_valid = false,
                                .left_hand_near_handle = false,
                                .left_grip_held =
                                    context.left_rifle_gripped ||
                                    idle_left_squeeze >= kSqueezeEngage,
                                .left_trigger_active =
                                    idle_left.trigger.active,
                                .left_trigger_value =
                                    idle_left.trigger.current,
                                .left_hand_forward_coordinate = 0.0F,
                            },
                            &g_magazine_charging_state));
                    }
                    g_magazine_charging_last_update_milliseconds =
                        GetTickCount64();
                    serviced_steady_magazine_idle = true;
                }
            }
        } catch (...) {
            // Any stale cache, invalid pointer, or lock-time failure falls
            // through to the existing complete fail-closed validation path.
        }
    }
    if (!observed_session_in_idle_lock) {
        observe_manual_reload_session(context.player_state);
    }
    if (serviced_steady_magazine_idle) {
        return;
    }

    RuntimeWeaponDefinitionIdentity identity{};
    const bool identity_valid = context.weapon_index > 0 &&
        read_runtime_sp_weapon_definition(context.weapon_index, &identity);
    observe_headset_svt40_weapon_identity(identity.name, identity_valid);
    observe_headset_m1garand_gl_weapon_identity(identity.name, identity_valid);
    const BoltActionWeaponProfile* const profile = identity_valid
        ? find_bolt_action_weapon_profile_by_internal_name(
              std::string_view{identity.name.data()})
        : nullptr;
    const bool registered_magazine_identity = identity_valid &&
        has_detachable_magazine_weapon_profile_for_internal_name(
            std::string_view{identity.name.data()});
    const DetachableMagazineWeaponProfile* const magazine_profile =
        registered_magazine_identity
        ? detachable_magazine_profile_for_viewmodel_dobj(
              identity.name.data(), viewmodel_dobj)
        : nullptr;
    if (simulator_detachable_magazine_probe_enabled()) {
        static std::atomic<std::uint64_t> last_identity_signature{
            std::numeric_limits<std::uint64_t>::max()};
        const std::uint64_t signature =
            (static_cast<std::uint64_t>(
                 static_cast<std::uint32_t>(context.weapon_index)) << 32) |
            identity.definition_address;
        std::uint64_t previous =
            last_identity_signature.load(std::memory_order_acquire);
        if (previous != signature &&
            last_identity_signature.compare_exchange_strong(
                previous, signature, std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            stereo_diagnostic_log(
                "MagazineProbe identity weapon=%d valid=%d definition=%08X name=%s boltProfile=%d magazineProfile=%d dobj=%p pose=%p muzzle=%d axis=%d",
                context.weapon_index, identity_valid ? 1 : 0,
                identity.definition_address,
                identity_valid ? identity.name.data() : "<unavailable>",
                profile != nullptr ? 1 : 0,
                magazine_profile != nullptr ? 1 : 0,
                viewmodel_dobj, viewmodel_pose,
                context.muzzle_valid ? 1 : 0,
                valid_basis(context.weapon_axis) ? 1 : 0);
        }
    }
    if (context.weapon_index > 0 && !identity_valid) {
        WAWVR_STEREO_DIAG_ONCE(
            "ReloadDiag map-local weapon identity unavailable for index %d; manual bolt path remains native",
            context.weapon_index);
    }
    if (context.weapon_index > 0 && identity_valid && profile == nullptr &&
        !registered_magazine_identity) {
        if (simulator_detachable_magazine_probe_enabled() ||
            owned_c_string_equals(identity.name, "mosin_rifle") ||
            owned_c_string_equals(identity.name, "m1carbine") ||
            owned_c_string_equals(identity.name, "m1garand")) {
            diagnose_simulator_weapon_viewmodel(viewmodel_dobj, identity);
        }
        WAWVR_STEREO_DIAG_ONCE(
            "ReloadDiag exact unsupported weapon asset at map-local index %d: %s",
            context.weapon_index, identity.name.data());
        // An exactly identified unsupported weapon retires the previous
        // physical cycle. Blank/chest or transiently unreadable frames do not.
        clear_runtime_state_if_epoch(update_session_epoch);
        return;
    }
    if (registered_magazine_identity && magazine_profile == nullptr &&
        viewmodel_dobj != nullptr) {
        WAWVR_STEREO_DIAG_ONCE(
            "ReloadDiag registered magazine weapon %s rejected the current exact DObj model identity; manual reload remains fail-closed",
            identity.name.data());
    }
    if (viewmodel_dobj == nullptr || viewmodel_pose == nullptr ||
        (profile == nullptr && magazine_profile == nullptr) ||
        !context.muzzle_valid || !finite_vector(context.muzzle_world) ||
        !finite_vector(context.rifle_grip_world) ||
        !valid_basis(context.weapon_axis)) {
        mark_runtime_temporarily_unavailable(update_session_epoch);
        return;
    }

    try {
        const std::lock_guard<std::mutex> lock(g_state_mutex);
        if (!g_enabled.load(std::memory_order_acquire)) {
            return;
        }
        if (g_manual_reload_session_epoch.load(std::memory_order_acquire) !=
            update_session_epoch) {
            return;
        }
        if (magazine_profile != nullptr) {
            update_detachable_magazine_viewmodel_locked(
                viewmodel_dobj, viewmodel_pose, context, identity,
                *magazine_profile);
            return;
        }
        const std::uint64_t desired_binding =
            pack_bolt_action_weapon_binding(
                {profile->id, context.weapon_index});
        std::uint64_t previous_binding =
            g_active_weapon_binding.load(std::memory_order_acquire);
        if (desired_binding == 0) {
            clear_interaction_state_locked();
            return;
        }
        bool binding_transition = previous_binding != desired_binding;
        if (g_active_magazine_binding.load(std::memory_order_acquire) != 0) {
            clear_interaction_state_locked();
            previous_binding = 0;
            binding_transition = true;
        }
        if (previous_binding != 0 &&
            (previous_binding != desired_binding ||
             g_visual_state.weapon_definition_address !=
                 identity.definition_address ||
             g_visual_state.viewmodel_dobj != viewmodel_dobj)) {
            clear_interaction_state_locked();
            binding_transition = true;
        }
        ClipSource source{};
        // Full bolt discovery scans the model, material, surface, and feed
        // geometry recipe. Once bound, reuse it only under the same exact
        // definition/DObj/profile identity and the existing fail-closed DObj
        // ownership check. Live bolt tags, ammo, controller state, and poses
        // are still refreshed below on every frame.
        const bool cached_source_ready = !binding_transition &&
            previous_binding == desired_binding &&
            g_visual_state.supported &&
            g_visual_state.profile_id == profile->id &&
            g_visual_state.weapon_index == context.weapon_index &&
            g_visual_state.weapon_definition_address ==
                identity.definition_address &&
            g_visual_state.viewmodel_dobj == viewmodel_dobj &&
            cached_bolt_source_matches_dobj(
                viewmodel_dobj, *profile, g_visual_state.source);
        if (cached_source_ready) {
            source = g_visual_state.source;
        } else if (!discover_bolt_action_source(
                       viewmodel_dobj, *profile, &source)) {
            WAWVR_STEREO_DIAG_ONCE(
                "ReloadDiag %s profile rejected current viewmodel asset recipe",
                profile->diagnostic_name);
            clear_interaction_state_locked();
            return;
        }
        if (!binding_transition && previous_binding == desired_binding &&
            (g_visual_state.source.model != source.model ||
             g_visual_state.source_surfaces != source.model->surfaces ||
             g_visual_state.source_material_handles !=
                 source.model->material_handles ||
             g_visual_state.source_base_matrices !=
                 source.model->base_matrices)) {
            clear_interaction_state_locked();
            binding_transition = true;
        }
        bool moving_bolt_layout_changed =
            g_visual_state.source.moving_bolt_count !=
                source.moving_bolt_count;
        for (std::size_t index = 0;
             !moving_bolt_layout_changed &&
                 index < source.moving_bolt_count;
             ++index) {
            const MovingBoltSource& previous =
                g_visual_state.source.moving_bolts[index];
            const MovingBoltSource& current = source.moving_bolts[index];
            moving_bolt_layout_changed = previous.tag != current.tag ||
                previous.parent_bone != current.parent_bone ||
                previous.bone != current.bone ||
                previous.surface != current.surface ||
                previous.surface_index != current.surface_index;
        }
        if (g_visual_state.source.model != source.model ||
            g_visual_state.source.root_bone != source.root_bone ||
            moving_bolt_layout_changed) {
            g_visual_state.closed_bolt_relatives = {};
            g_visual_state.closed_bolt_grip_offset = {};
            g_visual_state.closed_bolt_pose_latched = false;
            g_visual_state.closed_bolt_grip_offset_valid = false;
        }
        ClipAsset* const clip_asset =
            find_or_build_clip_asset(
                profile->id, source,
                g_clip_asset_source_generation);
        if (clip_asset == nullptr || !clip_asset->ready) {
            WAWVR_STEREO_DIAG_ONCE(
                "ReloadDiag %s detached feed asset build failed",
                profile->diagnostic_name);
            clear_interaction_state_locked();
            return;
        }
        std::array<const XSurface*, kMaximumMovingBoltTags> bolt_surfaces{};
        std::size_t total_bolt_surface_count = 0;
        for (std::size_t moving_index = 0;
             moving_index < source.moving_bolt_count; ++moving_index) {
            const MovingBoltSource& moving =
                source.moving_bolts[moving_index];
            const bool exact_cached_surface = source.model != nullptr &&
                source.model->surfaces != nullptr &&
                moving.surface_index < source.model->surface_count &&
                moving.surface ==
                    source.model->surfaces + moving.surface_index &&
                is_cached_bolt_surface(
                    source, moving_index, moving.surface);
            if (!exact_cached_surface) {
                WAWVR_STEREO_DIAG_ONCE(
                    "ReloadDiag %s cached moving bolt piece %zu no longer matches its exact rigid surface",
                    profile->diagnostic_name, moving_index);
                clear_interaction_state_locked();
                return;
            }
            bolt_surfaces[moving_index] = moving.surface;
            ++total_bolt_surface_count;
        }
        // The moving bolt pieces are siblings of the grip and muzzle tags.
        // Force each through T4's native tag path before any skel.mat access so
        // every parent/child pair is a final composite matrix. The first tag is
        // the physical grab anchor; additional tags are visual assembly pieces.
        wawvr::xr::Vec3f evaluated_bolt_world{};
        for (std::size_t index = 0;
             index < source.moving_bolt_count; ++index) {
            wawvr::xr::Vec3f evaluated{};
            if (!read_viewmodel_world_tag_position(
                    viewmodel_dobj, source.moving_bolts[index].tag,
                    viewmodel_pose, &evaluated)) {
                WAWVR_STEREO_DIAG_ONCE(
                    "ReloadDiag %s moving bolt tag %zu evaluation failed",
                    profile->diagnostic_name, index);
                clear_interaction_state_locked();
                return;
            }
            if (index == 0) {
                evaluated_bolt_world = evaluated;
            }
        }
        // Do not persistently hide authored geometry until every detached
        // asset, rigid-bolt, and evaluated-tag check above has succeeded. A new
        // profile that fails closed must remain visually native as well.
        if (!hide_authored_clip(
                viewmodel_dobj, source, context.weapon_index)) {
            WAWVR_STEREO_DIAG_ONCE(
                "ReloadDiag %s profile could not own authored feed surfaces for map-local index %d",
                profile->diagnostic_name, context.weapon_index);
            clear_interaction_state_locked();
            return;
        }
        bool publication_changed =
            g_active_weapon_binding.load(std::memory_order_acquire) !=
                desired_binding ||
            g_active_weapon_definition_address.load(
                std::memory_order_acquire) != identity.definition_address ||
            g_pre_skin_viewmodel_dobj.load(std::memory_order_acquire) !=
                viewmodel_dobj;
        for (std::size_t index = 0;
             index < g_pre_skin_bolt_surfaces.size(); ++index) {
            const XSurface* const expected =
                index < source.moving_bolt_count
                ? bolt_surfaces[index] : nullptr;
            publication_changed = publication_changed ||
                g_pre_skin_bolt_surfaces[index].load(
                    std::memory_order_acquire) != expected;
        }
        if (publication_changed) {
            begin_active_weapon_publication_locked();
        }
        g_visual_state.supported = true;
        g_visual_state.profile_id = profile->id;
        g_visual_state.weapon_index = context.weapon_index;
        g_visual_state.weapon_definition_address =
            identity.definition_address;
        g_visual_state.viewmodel_dobj = viewmodel_dobj;
        g_visual_state.source = source;
        g_visual_state.source_surfaces = source.model->surfaces;
        g_visual_state.source_material_handles =
            source.model->material_handles;
        g_visual_state.source_base_matrices = source.model->base_matrices;
        g_visual_state.asset = clip_asset;
        for (std::size_t index = 0;
             index < g_pre_skin_bolt_surfaces.size(); ++index) {
            g_pre_skin_bolt_surfaces[index].store(
                index < source.moving_bolt_count
                    ? bolt_surfaces[index] : nullptr,
                std::memory_order_release);
        }
        g_pre_skin_viewmodel_dobj.store(
            viewmodel_dobj, std::memory_order_release);
        g_active_weapon_definition_address.store(
            identity.definition_address, std::memory_order_release);
        g_active_magazine_binding.store(0, std::memory_order_release);
        g_active_weapon_binding.store(desired_binding, std::memory_order_release);
        if (publication_changed) {
            finish_active_weapon_publication_locked();
        }
        if (binding_transition) {
            stereo_diagnostic_log(
                "ReloadDiag exact %s profile bound to map-local weapon index %d (definition=%08X asset=%s)",
                profile->diagnostic_name, context.weapon_index,
                identity.definition_address, identity.name.data());
            stereo_diagnostic_log(
                "ReloadDiag %s moving bolt assembly discovered (root=%u pieces=%u primary-parent=%u primary-bolt=%u rigid-surfaces=%zu)",
                profile->diagnostic_name,
                static_cast<unsigned>(source.root_bone),
                static_cast<unsigned>(source.moving_bolt_count),
                static_cast<unsigned>(source.moving_bolts[0].parent_bone),
                static_cast<unsigned>(source.moving_bolts[0].bone),
                total_bolt_surface_count);
        }

        wawvr::xr::EnginePose right_world{};
        wawvr::xr::Vec3f right_head_local{};
        const bool right_pose_valid = current_hand_pose(
            context, wawvr::xr::Hand::Right,
            &right_world, &right_head_local);
        wawvr::xr::EnginePose left_world{};
        wawvr::xr::Vec3f left_head_local{};
        const bool left_pose_valid = current_hand_pose(
            context, wawvr::xr::Hand::Left,
            &left_world, &left_head_local);
        static_cast<void>(left_head_local);
        const auto& actions = context.controller.frame.actions;
        const auto& right = actions.hands[
            static_cast<std::uint32_t>(wawvr::xr::Hand::Right)];
        const auto& left = actions.hands[
            static_cast<std::uint32_t>(wawvr::xr::Hand::Left)];
        const bool input_owned =
            actions.focused &&
            controller_frame_is_current(context.controller, GetTickCount64());
        const float right_squeeze = right.squeeze.active &&
                                      std::isfinite(right.squeeze.current)
            ? std::clamp(right.squeeze.current, 0.0F, 1.0F)
            : 0.0F;
        const float left_squeeze = left.squeeze.active &&
                                      std::isfinite(left.squeeze.current)
            ? std::clamp(left.squeeze.current, 0.0F, 1.0F)
            : 0.0F;
        const bool right_squeeze_held =
            context.right_rifle_gripped ||
            right_squeeze >= kSqueezeEngage;
        const bool left_squeeze_held =
            context.left_rifle_gripped ||
            left_squeeze >= kSqueezeEngage;
        // Stripper-clip feed remains intentionally right-hand/right-waist.
        const bool feed_squeeze_held = g_controller_state.feed_grip_was_held
            ? right_squeeze >= kSqueezeRelease
            : right_squeeze >= kSqueezeEngage;
        wawvr::xr::EnginePose clip_world = right_world;
        if (right_pose_valid) {
            const auto clip_offset = compose(
                right_world.axis, profile->feed_device_hand_offset);
            clip_world.position = {
                right_world.position.x + clip_offset.x,
                right_world.position.y + clip_offset.y,
                right_world.position.z + clip_offset.z,
            };
        }
        wawvr::xr::Vec3f receiver{};
        const bool receiver_valid = right_pose_valid && receiver_position(
            context, *profile, clip_world.position, &receiver);
        const bool in_receiver =
            receiver_valid &&
            distance(clip_world.position, receiver) <=
                profile->insertion_radius_units;

        wawvr::xr::Vec3f closed_bolt_anchor{};
        wawvr::xr::Vec3f current_bolt_handle{};
        const bool bolt_anchor_valid =
            g_visual_state.closed_bolt_grip_offset_valid &&
            bolt_handle_position(
                context, *profile,
                g_visual_state.closed_bolt_grip_offset,
                g_bolt_state.bolt_fraction,
                &closed_bolt_anchor, &current_bolt_handle);
        const auto bolt_hand_sample = [&context, &closed_bolt_anchor,
                                       &current_bolt_handle, profile,
                                       bolt_anchor_valid](
            const wawvr::xr::EnginePose& hand_world,
            const bool hand_pose_valid,
            float* const forward_coordinate,
            bool* const near_bolt) noexcept {
            *forward_coordinate = 0.0F;
            *near_bolt = false;
            if (!hand_pose_valid || !bolt_anchor_valid) {
                return;
            }
            const wawvr::xr::Vec3f from_closed_anchor{
                hand_world.position.x - closed_bolt_anchor.x,
                hand_world.position.y - closed_bolt_anchor.y,
                hand_world.position.z - closed_bolt_anchor.z,
            };
            *forward_coordinate =
                dot(from_closed_anchor, context.weapon_axis.forward);
            *near_bolt =
                distance(hand_world.position, current_bolt_handle) <=
                profile->bolt_grab_radius_units;
        };
        float right_hand_forward_coordinate = 0.0F;
        float left_hand_forward_coordinate = 0.0F;
        bool right_hand_near_bolt = false;
        bool left_hand_near_bolt = false;
        bolt_hand_sample(
            right_world, right_pose_valid, &right_hand_forward_coordinate,
            &right_hand_near_bolt);
        bolt_hand_sample(
            left_world, left_pose_valid, &left_hand_forward_coordinate,
            &left_hand_near_bolt);

        // Do not clear this publication until the bolt state and its command
        // lock have been committed below. A shot arriving during an older
        // render therefore remains visible to both gates and the next frame.
        const bool shot_fired =
            g_shot_pending.load(std::memory_order_acquire);
        const Kar98BoltActionUpdate bolt_update = update_bolt_action(
            *profile,
            {
                .enabled = true,
                .focused = input_owned,
                .weapon_supported = true,
                .reset_requested = false,
                .action_sequence = actions.sequence,
                .shot_fired = shot_fired,
                .left_rifle_gripped = context.left_rifle_gripped,
                .right_hand_pose_valid = right_pose_valid,
                .right_hand_near_bolt = right_hand_near_bolt,
                .right_grip_held = right_squeeze_held,
                .right_trigger_active = right.trigger.active,
                .right_trigger_value = right.trigger.current,
                .right_hand_forward_coordinate =
                    right_hand_forward_coordinate,
                .right_rifle_gripped = context.right_rifle_gripped,
                .left_hand_pose_valid = left_pose_valid,
                .left_hand_near_bolt = left_hand_near_bolt,
                .left_grip_held = left_squeeze_held,
                .left_trigger_active = left.trigger.active,
                .left_trigger_value = left.trigger.current,
                .left_hand_forward_coordinate =
                    left_hand_forward_coordinate,
            },
            &g_bolt_state);

        const bool bolt_with_left_hand =
            g_bolt_state.manipulating_hand_selected &&
            g_bolt_state.manipulating_left_hand;
        const auto& bolt_hand_actions = bolt_with_left_hand ? left : right;
        const bool bolt_hand_pose_valid =
            bolt_with_left_hand ? left_pose_valid : right_pose_valid;
        const bool bolt_hand_grip_held =
            bolt_with_left_hand ? left_squeeze_held : right_squeeze_held;
        const bool bolt_opposite_retained = bolt_with_left_hand
            ? context.right_rifle_gripped : context.left_rifle_gripped;
        const float bolt_hand_forward_coordinate = bolt_with_left_hand
            ? left_hand_forward_coordinate : right_hand_forward_coordinate;

        if (bolt_update.event == Kar98BoltEvent::Grabbed) {
            WAWVR_STEREO_DIAG_ONCE(
                "ReloadDiag %s trigger latched the physical %s bolt while the opposite hand retained the rifle (hand-forward=%.3f)",
                bolt_with_left_hand ? "left" : "right",
                profile->diagnostic_name, bolt_hand_forward_coordinate);
        } else if (bolt_update.trigger_pressed_edge &&
                   bolt_update.cycle_required &&
                   !bolt_update.bolt_grabbed) {
            WAWVR_STEREO_DIAG_ONCE(
                "ReloadDiag physical %s bolt grab missed (right-distance=%.3f left-distance=%.3f handle=%.3f,%.3f,%.3f retained=%d/%d grips=%d/%d poses=%d/%d)",
                profile->diagnostic_name,
                distance(right_world.position, current_bolt_handle),
                distance(left_world.position, current_bolt_handle),
                current_bolt_handle.x, current_bolt_handle.y,
                current_bolt_handle.z,
                context.right_rifle_gripped ? 1 : 0,
                context.left_rifle_gripped ? 1 : 0,
                right_squeeze_held ? 1 : 0,
                left_squeeze_held ? 1 : 0,
                right_pose_valid ? 1 : 0,
                left_pose_valid ? 1 : 0);
        } else if (bolt_update.event == Kar98BoltEvent::FullyOpened) {
            WAWVR_STEREO_DIAG_ONCE(
                "ReloadDiag physical %s bolt reached the full-open endpoint",
                profile->diagnostic_name);
        } else if (bolt_update.event == Kar98BoltEvent::FullyClosed) {
            WAWVR_STEREO_DIAG_ONCE(
                "ReloadDiag physical %s bolt reached the closed endpoint after a full cycle",
                profile->diagnostic_name);
        } else if (bolt_update.event == Kar98BoltEvent::Released &&
                   bolt_update.cycle_required) {
            WAWVR_STEREO_DIAG_ONCE(
                "ReloadDiag physical %s bolt released before cycle completion (hand=%s fraction=%.3f trigger=%.3f trigger-active=%d grip=%d opposite-retained=%d pose-valid=%d input-owned=%d hand-forward-finite=%d action-sequence=%llu)",
                profile->diagnostic_name,
                bolt_with_left_hand ? "left" : "right",
                bolt_update.bolt_fraction,
                bolt_hand_actions.trigger.current,
                bolt_hand_actions.trigger.active ? 1 : 0,
                bolt_hand_grip_held ? 1 : 0,
                bolt_opposite_retained ? 1 : 0,
                bolt_hand_pose_valid ? 1 : 0,
                input_owned ? 1 : 0,
                std::isfinite(bolt_hand_forward_coordinate) ? 1 : 0,
                static_cast<unsigned long long>(actions.sequence));
        }
        if (bolt_update.bolt_grabbed &&
            std::abs(
                bolt_hand_forward_coordinate -
                g_bolt_state.grab_start_coordinate) >= 0.25F) {
            WAWVR_STEREO_DIAG_ONCE(
                "ReloadDiag physical %s bolt tracked %s-hand travel (movement=%.3f fraction=%.3f)",
                profile->diagnostic_name,
                bolt_with_left_hand ? "left" : "right",
                bolt_hand_forward_coordinate -
                    g_bolt_state.grab_start_coordinate,
                bolt_update.bolt_fraction);
        }

        const BoltActionAmmoSnapshot bolt_ammo =
            read_bolt_action_ammo(context.player_state, identity);
        const ManualStripperClipCommitPlan bolt_commit_plan =
            bolt_ammo.valid
            ? plan_manual_stripper_clip_commit(bolt_ammo.commit)
            : ManualStripperClipCommitPlan{};
        const bool native_commit_completed =
            consume_native_commit_completion_locked();
        ManualReloadControllerUpdate update =
            update_manual_reload_controller(
                {
                    .input_owned = input_owned,
                    .action_sequence = actions.sequence,
                    .profile_kind = profile->reload_kind,
                    .weapon_supported = true,
                    .can_reload = bolt_commit_plan.valid,
                    .reset_requested = false,
                    .native_action_open = bolt_update.action_open,
                    .native_commit_completed = native_commit_completed,
                    .begin_reload_pressed_edge =
                        bolt_update.event == Kar98BoltEvent::Grabbed &&
                        bolt_update.begin_reload_gesture,
                    .feed_grip_held = feed_squeeze_held,
                    .feed_pose_valid =
                        right_pose_valid && bolt_update.action_open,
                    .hand_in_feed_device_zone =
                        right_pose_valid && bolt_update.action_open &&
                        manual_reload_right_waist_contains({
                            right_head_local.x,
                            right_head_local.y,
                            right_head_local.z,
                        }),
                    .hand_in_insertion_zone =
                        bolt_update.action_open && in_receiver,
                },
                &g_controller_state);

        if (update.reload.request_native_action_open) {
            WAWVR_STEREO_DIAG_ONCE(
                "ReloadDiag %s manual feed transaction armed by a physical bolt grab",
                profile->diagnostic_name);
        }
        if (update.reload.request_native_commit) {
            static_cast<void>(publish_native_commit_request_locked());
            WAWVR_STEREO_DIAG_ONCE(
                "ReloadDiag right-hand stripper clip reached the %s top-feed receiver; bounded full-clip ammo transfer requested while bolt remains open",
                profile->diagnostic_name);
        }
        if (update.reload.request_native_cancel) {
            static_cast<void>(cancel_native_commit_request_locked());
        }

        const Kar98BoltVisualDecision bolt_visual =
            decide_bolt_action_bolt_visual(
                *profile,
                g_visual_state.closed_bolt_pose_latched,
                bolt_update.bolt_fraction, bolt_update.block_attack);
        if (bolt_visual.capture_closed_pose) {
            std::array<BoltRelativePose, kMaximumMovingBoltTags>
                captured_poses{};
            wawvr::xr::Vec3f captured_grip_offset{};
            bool poses_captured = true;
            for (std::size_t index = 0;
                 index < source.moving_bolt_count; ++index) {
                poses_captured = poses_captured &&
                    capture_bolt_relative_pose(
                        viewmodel_dobj,
                        source.moving_bolts[index].parent_bone,
                        source.moving_bolts[index].bone,
                        &captured_poses[index]);
            }
            const wawvr::xr::Vec3f bolt_from_grip{
                evaluated_bolt_world.x - context.rifle_grip_world.x,
                evaluated_bolt_world.y - context.rifle_grip_world.y,
                evaluated_bolt_world.z - context.rifle_grip_world.z,
            };
            const bool grip_offset_captured =
                poses_captured && decompose(
                    context.weapon_axis, bolt_from_grip,
                    &captured_grip_offset);
            if (grip_offset_captured) {
                g_visual_state.closed_bolt_relatives = captured_poses;
                g_visual_state.closed_bolt_grip_offset =
                    captured_grip_offset;
                g_visual_state.closed_bolt_pose_latched = true;
                g_visual_state.closed_bolt_grip_offset_valid = true;
                WAWVR_STEREO_DIAG_ONCE(
                    "ReloadDiag evaluated primary j_bolt owns the physical grab anchor while %u moving piece(s) retain independent closed poses (grip-local=%.3f,%.3f,%.3f)",
                    static_cast<unsigned>(source.moving_bolt_count),
                    g_visual_state.closed_bolt_grip_offset.x,
                    g_visual_state.closed_bolt_grip_offset.y,
                    g_visual_state.closed_bolt_grip_offset.z);
            }
        }
        if (bolt_visual.apply_manual_pose &&
            g_visual_state.closed_bolt_pose_latched) {
            // Never substitute the current native fire/rechamber matrix when
            // the pre-shot closed pose is unavailable. That fallback is the
            // detached pose we are explicitly replacing.
            const bool applied = apply_moving_bolt_poses(
                viewmodel_dobj, *profile, source,
                bolt_update.bolt_fraction,
                g_visual_state.closed_bolt_relatives);
            if (applied) {
                WAWVR_STEREO_DIAG_ONCE(
                    "ReloadDiag evaluated composite moving bolt assembly replaced the native fire/rechamber pose transactionally");
            }
        }

        if (bolt_update.bolt_fraction <= profile->bolt_closed_threshold &&
            !bolt_update.bolt_grabbed &&
            (bolt_update.event == Kar98BoltEvent::FullyClosed ||
             bolt_update.event == Kar98BoltEvent::Released)) {
            reset_manual_reload_controller(&g_controller_state);
            update = {};
            static_cast<void>(cancel_native_commit_request_locked());
        }

        const bool render_detached =
            update.reload.render_detached_feed_device && right_pose_valid;
        if (render_detached) {
            // Position always follows this exact OpenXR sample. Close to the
            // top-feed opening only orientation is assisted; no cached or
            // mirrored position can create the old opposite-motion bug.
            if (update.reload.orientation_source ==
                gameplay::ReloadOrientationSource::InsertionGuide) {
                clip_world.axis = context.weapon_axis;
            }
            if (render_clip(
                    g_visual_state.asset, clip_world,
                    context.camera_origin, viewmodel_pose)) {
                WAWVR_STEREO_DIAG_ONCE(
                    "ReloadDiag detached %s stripper clip rendered in the current right-glove pinch pose",
                    profile->diagnostic_name);
            }
        }

        g_supported.store(true, std::memory_order_release);
        g_manual_active.store(
            update.reload.manual_reload_active,
            std::memory_order_release);
        g_cycle_lock.store(
            bolt_update.block_attack,
            std::memory_order_release);
        g_reserve_right_grip.store(
            update.reload.manual_reload_active ||
                bolt_update.reserve_right_grip,
            std::memory_order_release);
        g_reserve_left_grip.store(
            bolt_update.reserve_left_grip,
            std::memory_order_release);
        const bool left_action_candidate =
            context.right_rifle_gripped && !left_squeeze_held &&
            left_pose_valid && left_hand_near_bolt &&
            std::isfinite(left_hand_forward_coordinate);
        g_block_new_left_trigger_action.store(
            bolt_update.reserve_left_grip || left_action_candidate,
            std::memory_order_release);
        if (shot_fired) {
            // The cycle lock is already published true by the update above.
            // A second local supported-rifle shot cannot race this clear because the
            // command hook blocks attack from the first notification onward.
            g_shot_pending.store(false, std::memory_order_release);
        }
    } catch (...) {
        clear_runtime_state_if_epoch(update_session_epoch);
    }
}

namespace {

bool force_reload_button_from_identity(
    const std::int32_t weapon_index,
    GameplayWeaponIdentityRead* const one_call_identity) noexcept {
    if (g_automatic_reload.load(std::memory_order_acquire) ||
        !g_enabled.load(std::memory_order_acquire) ||
        !g_supported.load(std::memory_order_acquire) ||
        g_native_commit_phase.load(std::memory_order_acquire) !=
            NativeCommitPhase::requested) {
        return false;
    }
    std::uint64_t validated_generation = 0;
    if (!revalidate_active_manual_gameplay_weapon(
            weapon_index, 0, &validated_generation, one_call_identity)) {
        return false;
    }
    return g_enabled.load(std::memory_order_acquire) &&
        g_supported.load(std::memory_order_acquire) &&
        g_native_commit_phase.load(std::memory_order_acquire) ==
            NativeCommitPhase::requested &&
        validated_generation ==
            g_native_commit_publication_generation.load(
                std::memory_order_acquire) &&
        validated_generation ==
            g_active_weapon_publication_generation.load(
                std::memory_order_acquire);
}

bool blocks_attack_from_identity(
    const std::int32_t weapon_index,
    GameplayWeaponIdentityRead* const one_call_identity) noexcept {
    if (g_automatic_reload.load(std::memory_order_acquire)) {
        return false;
    }
    if (magazine_chamber_lock_matches_gameplay_weapon(weapon_index, one_call_identity)) {
        return true;
    }
    if (!g_enabled.load(std::memory_order_acquire) ||
        !(g_manual_active.load(std::memory_order_acquire) ||
          g_cycle_lock.load(std::memory_order_acquire) ||
          g_shot_pending.load(std::memory_order_acquire))) {
        return false;
    }
    std::uint64_t validated_generation = 0;
    if (!revalidate_active_manual_gameplay_weapon(
            weapon_index, 0, &validated_generation, one_call_identity)) {
        return false;
    }
    return g_enabled.load(std::memory_order_acquire) &&
        (g_manual_active.load(std::memory_order_acquire) ||
         g_cycle_lock.load(std::memory_order_acquire) ||
         g_shot_pending.load(std::memory_order_acquire)) &&
        validated_generation ==
            g_active_weapon_publication_generation.load(
                std::memory_order_acquire);
}

bool reserves_right_grip_from_identity(
    const std::int32_t weapon_index,
    GameplayWeaponIdentityRead* const one_call_identity) noexcept {
    if (g_automatic_reload.load(std::memory_order_acquire)) {
        return false;
    }
    if (magazine_chamber_lock_reserves_hand(
            weapon_index, MagazineChargingHand::Right, one_call_identity)) {
        return true;
    }
    if (!g_enabled.load(std::memory_order_acquire) ||
        !g_reserve_right_grip.load(std::memory_order_acquire)) {
        return false;
    }
    std::uint64_t validated_generation = 0;
    if (!revalidate_active_manual_gameplay_weapon(
            weapon_index, 0, &validated_generation, one_call_identity)) {
        return false;
    }
    return g_enabled.load(std::memory_order_acquire) &&
        g_reserve_right_grip.load(std::memory_order_acquire) &&
        validated_generation ==
            g_active_weapon_publication_generation.load(
                std::memory_order_acquire);
}

bool reserves_left_grip_from_identity(
    const std::int32_t weapon_index,
    GameplayWeaponIdentityRead* const one_call_identity) noexcept {
    if (g_automatic_reload.load(std::memory_order_acquire)) {
        return false;
    }
    if (magazine_chamber_lock_reserves_hand(
            weapon_index, MagazineChargingHand::Left, one_call_identity)) {
        return true;
    }
    if (!g_enabled.load(std::memory_order_acquire) ||
        !g_reserve_left_grip.load(std::memory_order_acquire)) {
        return false;
    }
    std::uint64_t validated_generation = 0;
    if (!revalidate_active_manual_gameplay_weapon(
            weapon_index, 0, &validated_generation, one_call_identity)) {
        return false;
    }
    return g_enabled.load(std::memory_order_acquire) &&
        g_reserve_left_grip.load(std::memory_order_acquire) &&
        validated_generation ==
            g_active_weapon_publication_generation.load(
                std::memory_order_acquire);
}

bool blocks_new_left_trigger_from_identity(
    const std::int32_t weapon_index,
    GameplayWeaponIdentityRead* const one_call_identity) noexcept {
    if (g_automatic_reload.load(std::memory_order_acquire) ||
        !g_enabled.load(std::memory_order_acquire) ||
        !g_block_new_left_trigger_action.load(std::memory_order_acquire)) {
        return false;
    }
    std::uint64_t validated_generation = 0;
    if (!revalidate_active_manual_gameplay_weapon(
            weapon_index, 0, &validated_generation, one_call_identity)) {
        return false;
    }
    return g_enabled.load(std::memory_order_acquire) &&
        g_block_new_left_trigger_action.load(std::memory_order_acquire) &&
        validated_generation ==
            g_active_weapon_publication_generation.load(
                std::memory_order_acquire);
}

}  // namespace

ManualReloadGameplayPolicy read_manual_reload_gameplay_policy(
    const std::int32_t weapon_index) noexcept {
    GameplayWeaponIdentityRead identity(weapon_index);
    return {
        .force_reload_button = force_reload_button_from_identity(weapon_index, &identity),
        .blocks_attack = blocks_attack_from_identity(weapon_index, &identity),
        .reserves_right_grip = reserves_right_grip_from_identity(weapon_index, &identity),
        .reserves_left_grip = reserves_left_grip_from_identity(weapon_index, &identity),
        .blocks_new_left_trigger_action = blocks_new_left_trigger_from_identity(weapon_index, &identity),
    };
}

bool manual_reload_force_reload_button(const std::int32_t weapon_index) noexcept {
    return force_reload_button_from_identity(weapon_index, nullptr);
}

bool manual_reload_blocks_attack(const std::int32_t weapon_index) noexcept {
    return blocks_attack_from_identity(weapon_index, nullptr);
}

bool manual_reload_reserves_right_grip(const std::int32_t weapon_index) noexcept {
    return reserves_right_grip_from_identity(weapon_index, nullptr);
}

bool manual_reload_reserves_left_grip(const std::int32_t weapon_index) noexcept {
    return reserves_left_grip_from_identity(weapon_index, nullptr);
}

bool manual_reload_blocks_new_left_trigger_action(const std::int32_t weapon_index) noexcept {
    return blocks_new_left_trigger_from_identity(weapon_index, nullptr);
}

void manual_reload_notify_local_shot(
    const std::int32_t weapon_index,
    const std::uint32_t weapon_definition_address) noexcept {
    if (g_automatic_reload.load(std::memory_order_acquire) ||
        !g_enabled.load(std::memory_order_acquire)) {
        return;
    }
    try {
        // Profile/model switching clears this same state under the mutex. Keep
        // validation and publication indivisible so a shot from the outgoing
        // weapon cannot republish a permanent lock after its binding is gone.
        const std::lock_guard<std::mutex> lock(g_state_mutex);
        const BoltActionWeaponProfile* const profile =
            revalidate_active_gameplay_weapon(
                weapon_index, weapon_definition_address);
        if (!g_enabled.load(std::memory_order_acquire) ||
            profile == nullptr || weapon_definition_address == 0 ||
            weapon_definition_address !=
                g_visual_state.weapon_definition_address ||
            g_visual_state.profile_id != profile->id ||
            g_visual_state.weapon_index != weapon_index) {
            return;
        }
        g_local_shot_generation.fetch_add(1, std::memory_order_acq_rel);
        g_shot_pending.store(true, std::memory_order_release);
        g_cycle_lock.store(true, std::memory_order_release);
        // A shot requires a cycle, but it does not choose the hand. Weapon
        // ownership remains untouched until the first free hand deliberately
        // presses its index trigger at the physical action.
        g_reserve_right_grip.store(false, std::memory_order_release);
        g_reserve_left_grip.store(false, std::memory_order_release);
        g_block_new_left_trigger_action.store(false, std::memory_order_release);
    } catch (...) {
        // A failed mutex acquisition must leave the native firing path intact.
    }
}

void manual_reload_observe_connection_state(
    const bool state_valid,
    const std::int32_t connection_state,
    const std::int32_t active_connection_state) noexcept {
    if (!state_valid || active_connection_state < 0) {
        return;
    }
    const std::int32_t previous =
        g_last_valid_connection_state.exchange(
            connection_state, std::memory_order_acq_rel);
    if (previous != active_connection_state ||
        connection_state == active_connection_state) {
        return;
    }
    try {
        const std::lock_guard<std::mutex> lock(g_state_mutex);
        g_magazine_chamber_locks.clear_all();
        g_last_player_command_time.store(-1, std::memory_order_release);
        g_command_time_regression_candidate.store(
            -1, std::memory_order_release);
        clear_interaction_state_locked();
        g_manual_reload_session_epoch.fetch_add(
            1, std::memory_order_acq_rel);
        stereo_diagnostic_log(
            "ReloadDiag SP connection left active state %d for %d; retired persistent manual chamber requirements for the completed presentation epoch",
            active_connection_state, connection_state);
    } catch (...) {
        // The independent gameplay gate remains conservative if retirement
        // cannot acquire the render-state mutex on this Present.
    }
}

void request_manual_reload_shutdown() noexcept {
    g_enabled.store(false, std::memory_order_release);
    g_headset_svt40_selection_state.store(
        HeadsetSvt40SelectionState::unknown,
        std::memory_order_release);
    g_headset_m1garand_gl_selection_state.store(
        HeadsetM1GarandGlSelectionState::unknown,
        std::memory_order_release);
    g_manual_reload_session_epoch.fetch_add(1, std::memory_order_acq_rel);
    // This loader-lock-safe path cannot establish the table's required
    // stop-publishers barrier. Readers are already gated by g_enabled and the
    // process-lifetime table is discarded with the module. Orderly shutdown
    // clears it under g_state_mutex below.
    g_last_player_command_time.store(-1, std::memory_order_release);
    g_command_time_regression_candidate.store(-1, std::memory_order_release);
    g_last_valid_connection_state.store(
        (std::numeric_limits<std::int32_t>::min)(),
        std::memory_order_release);
    g_active_weapon_binding.store(0, std::memory_order_release);
    g_active_magazine_binding.store(0, std::memory_order_release);
    g_active_weapon_definition_address.store(0, std::memory_order_release);
    // This can be called under the Windows loader lock. Publish a fail-closed
    // gate without waiting for the render-thread state mutex; process-lifetime
    // assets are deliberately left intact until process teardown.
    g_supported.store(false, std::memory_order_release);
    g_manual_active.store(false, std::memory_order_release);
    g_cycle_lock.store(false, std::memory_order_release);
    g_reserve_right_grip.store(false, std::memory_order_release);
    g_reserve_left_grip.store(false, std::memory_order_release);
    g_block_new_left_trigger_action.store(false, std::memory_order_release);
    g_shot_pending.store(false, std::memory_order_release);
    g_native_commit_phase.store(
        NativeCommitPhase::idle, std::memory_order_release);
    g_native_commit_publication_generation.store(
        0, std::memory_order_release);
    g_detachable_magazine_commit_completed_at_milliseconds.store(
        0, std::memory_order_release);
}

void request_manual_reload_orderly_shutdown() noexcept {
    try {
        {
            const std::lock_guard<std::mutex> lock(g_state_mutex);
            if (g_enabled.load(std::memory_order_acquire)) {
                // Keep identity publication valid until reversible authored
                // magazine visibility has been restored under the same lock.
                clear_interaction_state_locked();
            }
            g_magazine_chamber_locks.clear_all();
            g_enabled.store(false, std::memory_order_release);
        }
    } catch (...) {
        // Fall through to the loader-safe atomic gate if orderly cleanup ever
        // fails; native gameplay must remain available.
    }
    request_manual_reload_shutdown();
}

const char* manual_reload_runtime_status_name(
    const ManualReloadRuntimeStatus status) noexcept {
    switch (status) {
    case ManualReloadRuntimeStatus::installed: return "installed";
    case ManualReloadRuntimeStatus::already_installed:
        return "already-installed";
    case ManualReloadRuntimeStatus::not_applicable: return "not-applicable";
    case ManualReloadRuntimeStatus::dependency_unavailable:
        return "dependency-unavailable";
    case ManualReloadRuntimeStatus::rejected_wrong_profile:
        return "rejected-wrong-profile";
    case ManualReloadRuntimeStatus::address_out_of_range:
        return "address-out-of-range";
    case ManualReloadRuntimeStatus::fingerprint_mismatch:
        return "fingerprint-mismatch";
    case ManualReloadRuntimeStatus::thread_suspend_failed:
        return "thread-suspend-failed";
    case ManualReloadRuntimeStatus::patch_failed: return "patch-failed";
    }
    return "unknown";
}

}  // namespace wawvr::mod
