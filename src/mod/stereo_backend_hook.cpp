#include "stereo_backend_hook.hpp"
#include "virtual_query_timing.hpp"
#include "resident_page_access.hpp"

#include "draw_surface_validation_logic.hpp"
#include "peer_thread_quiescence.hpp"
#include "performance_timing.hpp"
#include "physical_scope_draw_filter.hpp"
#include "present_hook.hpp"
#include "smoke_surface_snapshot.hpp"
#include "stereo_diagnostics.hpp"
#include "stereo_backend_eye_order_logic.hpp"
#include "stereo_emissive_viewport_logic.hpp"
#include "stereo_frame_broker.hpp"
#include "t4_layout_selector.hpp"

#include <windows.h>
#include <d3d9.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace wawvr::mod {
namespace {

static_assert(sizeof(void*) == 4, "The exact T4 backend hook is x86-only");

constexpr std::size_t kCallSize = 5;
constexpr std::size_t kDrawSurfsDetourSize = 7;
constexpr std::size_t kDrawSurfsTrampolineSize =
    kDrawSurfsDetourSize + kCallSize;

template <std::size_t Size>
struct ExactSentinel final {
    std::uintptr_t address{};
    std::array<std::uint8_t, Size> expected{};
};

struct StereoBackendExecutableProfile final {
    const char* name{};
    std::uintptr_t synchronous_draw_call_address{};
    std::uintptr_t smp_draw_call_address{};
    std::uintptr_t setup_clear_call_address{};
    std::uintptr_t scene_clear_call_address{};
    std::uintptr_t draw_3d_internal_address{};
    std::uintptr_t clear_screen_address{};
    std::uintptr_t standard_emissive_callback_push_address{};
    std::uintptr_t standard_emissive_callback_address{};
    std::uintptr_t back_end_data_pointer_address{};
    std::uintptr_t draw_surfs_address{};
    ExactSentinel<7> draw_surfs_entry{};
    std::array<std::uint8_t, kCallSize> expected_synchronous_draw_call{};
    std::array<std::uint8_t, kCallSize> expected_smp_draw_call{};
    std::array<std::uint8_t, kCallSize> expected_setup_clear_call{};
    std::array<std::uint8_t, kCallSize> expected_scene_clear_call{};
    std::array<std::uint8_t, 14> expected_draw_3d_internal_entry{};
    std::array<std::uint8_t, 16> expected_clear_screen_entry{};
    std::array<std::uint8_t, kCallSize>
        expected_standard_emissive_callback_push{};
    std::array<std::uint8_t, 16>
        expected_standard_emissive_callback_entry{};
    ExactSentinel<58> standard_emissive_callback_tail{};
    ExactSentinel<12> view_stride{};
    ExactSentinel<16> split_lit_primary{};
    ExactSentinel<19> split_lit_secondary{};
    ExactSentinel<16> lit_list{};
    ExactSentinel<16> emissive_list{};
    ExactSentinel<22> decal_list{};
    ExactSentinel<14> list_metadata{};
    ExactSentinel<6> point_light_base{};
    ExactSentinel<9> point_light_info{};
    ExactSentinel<9> point_light_stride{};
    ExactSentinel<6> point_light_count{};
    ExactSentinel<46> emissive_spot_layout{};
};

constexpr StereoBackendExecutableProfile kSpBackendProfile{
    .name = "T4 SP 1.7.1263",
    .synchronous_draw_call_address = 0x006FC112u,
    .smp_draw_call_address = 0x006FC4F8u,
    .setup_clear_call_address = 0x006E8439u,
    .scene_clear_call_address = 0x006E858Du,
    .draw_3d_internal_address = 0x006E8B90u,
    .clear_screen_address = 0x0072BB30u,
    .standard_emissive_callback_push_address = 0x006E7E30u,
    .standard_emissive_callback_address = 0x006E7D00u,
    .back_end_data_pointer_address = 0x03DCB4CCu,
    .draw_surfs_address = 0x006F8A30u,
    .draw_surfs_entry = {0x006F8A30u, {
        0x8B, 0x44, 0x24, 0x08, 0x83, 0xEC, 0x24,
    }},
    .expected_synchronous_draw_call = {0xE8, 0x79, 0xCA, 0xFE, 0xFF},
    .expected_smp_draw_call = {0xE8, 0x93, 0xC6, 0xFE, 0xFF},
    .expected_setup_clear_call = {0xE8, 0xF2, 0x36, 0x04, 0x00},
    .expected_scene_clear_call = {0xE8, 0x9E, 0x35, 0x04, 0x00},
    .expected_draw_3d_internal_entry = {
        0x51, 0xA1, 0x30, 0x85, 0x65, 0x04, 0x83,
        0xE8, 0x00, 0x55, 0x8B, 0x6C, 0x24, 0x0C,
    },
    .expected_clear_screen_entry = {
        0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xC0, 0x83, 0xEC,
        0x34, 0x53, 0x8A, 0x5D, 0x0C, 0x56, 0x8B, 0xF0,
    },
    .expected_standard_emissive_callback_push = {
        0x68, 0x00, 0x7D, 0x6E, 0x00,
    },
    .expected_standard_emissive_callback_entry = {
        0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x0F, 0x57,
        0xC0, 0x56, 0x8B, 0x75, 0x0C, 0xF3, 0x0F, 0x10,
    },
    .standard_emissive_callback_tail = {0x006E7D82u, {
        0x68, 0xF0, 0xEF, 0x89, 0x00, 0x6A, 0xFF, 0xE8,
        0x22, 0x0A, 0x0E, 0x00, 0x8B, 0x7D, 0x10, 0x8B,
        0x45, 0x08, 0x57, 0x56, 0x05, 0xD8, 0x65, 0x00,
        0x00, 0x50, 0x6A, 0x00, 0xE8, 0x8D, 0x0C, 0x01,
        0x00, 0x83, 0xC4, 0x10, 0xE8, 0xFF, 0x09, 0x0E,
        0x00, 0x8B, 0xB7, 0x90, 0x00, 0x00, 0x00, 0xE8,
        0x6A, 0xE5, 0xFF, 0xFF, 0x5F, 0x5E, 0x8B, 0xE5,
        0x5D, 0xC3,
    }},
    .view_stride = {0x006DCEEBu, {
        0x69, 0xDB, 0x80, 0x6D, 0x00, 0x00,
        0x03, 0x98, 0xD4, 0x4D, 0x14, 0x00,
    }},
    .split_lit_primary = {0x006DDADEu, {
        0x6A, 0x2C, 0x8D, 0xBB, 0x54, 0x65, 0x00, 0x00,
        0x6A, 0x00, 0x57, 0xE8, 0x52, 0x24, 0x0D, 0x00,
    }},
    .split_lit_secondary = {0x006DDB78u, {
        0x6A, 0x2C, 0x8D, 0xB3, 0x80, 0x65, 0x00, 0x00,
        0x6A, 0x00, 0x56, 0x89, 0x57, 0x04, 0xE8, 0xB5,
        0x23, 0x0D, 0x00,
    }},
    .lit_list = {0x006DDCAFu, {
        0x6A, 0x2C, 0x8D, 0xB3, 0xAC, 0x65, 0x00, 0x00,
        0x6A, 0x00, 0x56, 0xE8, 0x81, 0x22, 0x0D, 0x00,
    }},
    .emissive_list = {0x006DDD4Au, {
        0x6A, 0x2C, 0x8D, 0x83, 0xD8, 0x65, 0x00, 0x00,
        0x6A, 0x00, 0x50, 0xE8, 0xE6, 0x21, 0x0D, 0x00,
    }},
    .decal_list = {0x006DDE3Au, {
        0x6A, 0x2C, 0x8D, 0xB3, 0x04, 0x66, 0x00, 0x00,
        0x6A, 0x00, 0x56, 0x89, 0x83, 0xDC, 0x65, 0x00,
        0x00, 0xE8, 0xF0, 0x20, 0x0D, 0x00,
    }},
    .list_metadata = {0x006DDAF9u, {
        0x89, 0x57, 0x08, 0x89, 0x5F, 0x0C, 0xD9,
        0x06, 0xD9, 0x5F, 0x14, 0x83, 0xC4, 0x0C,
    }},
    .point_light_base = {0x00728A38u, {
        0x81, 0xC3, 0xCC, 0x53, 0x00, 0x00,
    }},
    .point_light_info = {0x00728A61u, {
        0x6A, 0x2C, 0x8D, 0x43, 0xE4, 0x6A, 0x00, 0x50, 0xE8,
    }},
    .point_light_stride = {0x00728B6Au, {
        0x89, 0x4B, 0xE4, 0x89, 0x43, 0xE8, 0x83, 0xC3, 0x6C,
    }},
    .point_light_count = {0x006DDC2Bu, {
        0x89, 0x83, 0xA0, 0x55, 0x00, 0x00,
    }},
    .emissive_spot_layout = {0x006DC64Cu, {
        0x8D, 0xBD, 0xA8, 0x55, 0x00, 0x00,
        0xB9, 0x10, 0x00, 0x00, 0x00,
        0xBE, 0x78, 0x26, 0xD5, 0x03,
        0xF3, 0xA5,
        0x89, 0x9D, 0xA4, 0x55, 0x00, 0x00,
        0x89, 0x9D, 0xE8, 0x55, 0x00, 0x00,
        0x89, 0x9D, 0xEC, 0x55, 0x00, 0x00,
        0xC7, 0x85, 0xF0, 0x55, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    }},
};

constexpr StereoBackendExecutableProfile kMpBackendProfile{
    .name = "T4 MP 1.7.1263",
    .synchronous_draw_call_address = 0x006D7242u,
    .smp_draw_call_address = 0x006D762Cu,
    .setup_clear_call_address = 0x006BA3C9u,
    .scene_clear_call_address = 0x006BA51Du,
    .draw_3d_internal_address = 0x006BAB20u,
    .clear_screen_address = 0x006FDC70u,
    .standard_emissive_callback_push_address = 0x006B9DC0u,
    .standard_emissive_callback_address = 0x006B9C90u,
    .back_end_data_pointer_address = 0x10CF9B5Cu,
    // The MP R_DrawSurfs entry has not been pinned. Physical weapon scopes
    // currently run only through the verified SP WeaponDef/viewmodel path.
    .draw_surfs_address = 0,
    .draw_surfs_entry = {},
    .expected_synchronous_draw_call = {0xE8, 0xD9, 0x38, 0xFE, 0xFF},
    .expected_smp_draw_call = {0xE8, 0xEF, 0x34, 0xFE, 0xFF},
    .expected_setup_clear_call = {0xE8, 0xA2, 0x38, 0x04, 0x00},
    .expected_scene_clear_call = {0xE8, 0x4E, 0x37, 0x04, 0x00},
    .expected_draw_3d_internal_entry = {
        0x51, 0xA1, 0x28, 0x93, 0x2F, 0x11, 0x83,
        0xE8, 0x00, 0x55, 0x8B, 0x6C, 0x24, 0x0C,
    },
    .expected_clear_screen_entry = {
        0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xC0, 0x83, 0xEC,
        0x34, 0x53, 0x8A, 0x5D, 0x0C, 0x56, 0x8B, 0xF0,
    },
    .expected_standard_emissive_callback_push = {
        0x68, 0x90, 0x9C, 0x6B, 0x00,
    },
    .expected_standard_emissive_callback_entry = {
        0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x0F, 0x57,
        0xC0, 0x56, 0x8B, 0x75, 0x0C, 0xF3, 0x0F, 0x10,
    },
    .standard_emissive_callback_tail = {0x006B9D12u, {
        0x68, 0xC0, 0x24, 0x89, 0x00, 0x6A, 0xFF, 0xE8,
        0xDA, 0x99, 0x10, 0x00, 0x8B, 0x7D, 0x10, 0x8B,
        0x45, 0x08, 0x57, 0x56, 0x05, 0xD8, 0x65, 0x00,
        0x00, 0x50, 0x6A, 0x00, 0xE8, 0x4D, 0x9E, 0x01,
        0x00, 0x83, 0xC4, 0x10, 0xE8, 0xC3, 0x99, 0x10,
        0x00, 0x8B, 0xB7, 0x90, 0x00, 0x00, 0x00, 0xE8,
        0xCA, 0xE5, 0xFF, 0xFF, 0x5F, 0x5E, 0x8B, 0xE5,
        0x5D, 0xC3,
    }},
    .view_stride = {0x006B586Bu, {
        0x69, 0xDB, 0x80, 0x6D, 0x00, 0x00,
        0x03, 0x98, 0xD4, 0x4D, 0x14, 0x00,
    }},
    .split_lit_primary = {0x006B6590u, {
        0x6A, 0x2C, 0x8D, 0xBB, 0x54, 0x65, 0x00, 0x00,
        0x6A, 0x00, 0x57, 0xE8, 0xE0, 0x28, 0x0F, 0x00,
    }},
    .split_lit_secondary = {0x006B662Au, {
        0x6A, 0x2C, 0x8D, 0xB3, 0x80, 0x65, 0x00, 0x00,
        0x6A, 0x00, 0x56, 0x89, 0x57, 0x04, 0xE8, 0x43,
        0x28, 0x0F, 0x00,
    }},
    .lit_list = {0x006B675Fu, {
        0x6A, 0x2C, 0x8D, 0xB3, 0xAC, 0x65, 0x00, 0x00,
        0x6A, 0x00, 0x56, 0xE8, 0x11, 0x27, 0x0F, 0x00,
    }},
    .emissive_list = {0x006B67FAu, {
        0x6A, 0x2C, 0x8D, 0x83, 0xD8, 0x65, 0x00, 0x00,
        0x6A, 0x00, 0x50, 0xE8, 0x76, 0x26, 0x0F, 0x00,
    }},
    .decal_list = {0x006B68EAu, {
        0x6A, 0x2C, 0x8D, 0xB3, 0x04, 0x66, 0x00, 0x00,
        0x6A, 0x00, 0x56, 0x89, 0x83, 0xDC, 0x65, 0x00,
        0x00, 0xE8, 0x80, 0x25, 0x0F, 0x00,
    }},
    .list_metadata = {0x006B65ABu, {
        0x89, 0x57, 0x08, 0x89, 0x5F, 0x0C, 0xD9,
        0x06, 0xD9, 0x5F, 0x14, 0x83, 0xC4, 0x0C,
    }},
    .point_light_base = {0x00701D78u, {
        0x81, 0xC3, 0xCC, 0x53, 0x00, 0x00,
    }},
    .point_light_info = {0x00701DA1u, {
        0x6A, 0x2C, 0x8D, 0x43, 0xE4, 0x6A, 0x00, 0x50, 0xE8,
    }},
    .point_light_stride = {0x00701EAAu, {
        0x89, 0x4B, 0xE4, 0x89, 0x43, 0xE8, 0x83, 0xC3, 0x6C,
    }},
    .point_light_count = {0x006B66DBu, {
        0x89, 0x83, 0xA0, 0x55, 0x00, 0x00,
    }},
    .emissive_spot_layout = {0x006B4F7Cu, {
        0x8D, 0xBD, 0xA8, 0x55, 0x00, 0x00,
        0xB9, 0x10, 0x00, 0x00, 0x00,
        0xBE, 0x78, 0x07, 0x9E, 0x10,
        0xF3, 0xA5,
        0x89, 0x9D, 0xA4, 0x55, 0x00, 0x00,
        0x89, 0x9D, 0xE8, 0x55, 0x00, 0x00,
        0x89, 0x9D, 0xEC, 0x55, 0x00, 0x00,
        0xC7, 0x85, 0xF0, 0x55, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    }},
};

[[nodiscard]] const StereoBackendExecutableProfile*
configured_backend_profile() noexcept {
    wawvr::t4::ExecutableLayoutId layout{};
    if (!get_configured_renderer_layout(&layout)) {
        return nullptr;
    }
    switch (t4_layout_family_from_id(layout)) {
    case T4LayoutFamily::single_player_1_7_1263:
        return &kSpBackendProfile;
    case T4LayoutFamily::multiplayer_1_7_1263:
        return &kMpBackendProfile;
    case T4LayoutFamily::unsupported:
        return nullptr;
    }
    return nullptr;
}

constexpr std::size_t kViewInfoIndexOffset = 0x144DCC;
constexpr std::size_t kViewInfoCountOffset = 0x144DD0;
constexpr std::size_t kViewInfoPointerOffset = 0x144DD4;
constexpr std::size_t kViewInfoStride = 0x6D80;
// Scope-active frontend order is physical optic, left eye, right eye.
// The frontend restores the optic's temporary entity visibility before eyes.
constexpr std::uint32_t kPhysicalScopeViewIndex = 0u;
constexpr std::size_t kViewOriginOffset = 0x100;
constexpr std::size_t kSceneViewportOffset = 0x154;
constexpr std::size_t kScissorViewportOffset = 0x174;
constexpr std::size_t kRendererDeviceOffset = 0x90;
constexpr std::size_t kRendererCachedViewportOffset = 0x1104;
constexpr std::size_t kSourceSceneViewportOffset = 0x1358;
constexpr std::size_t kSourceViewportBehaviorOffset = 0x1378;
constexpr std::size_t kSourceRenderTargetWidthOffset = 0x137C;
constexpr std::size_t kSourceRenderTargetHeightOffset = 0x1380;
constexpr std::size_t kSourceViewportDirtyOffset = 0x1384;
constexpr std::size_t kSourceClipLookupScaleOffset = 0x0C50;
constexpr std::size_t kSourceClipLookupOffsetOffset = 0x0C60;
constexpr std::size_t kSourceRawFloatZCodeImageOffset = 0x0FE0;
constexpr std::size_t kSourceRawFloatZSamplerStateOffset = 0x1034;
constexpr std::size_t kGfxImageTextureOffset = 0x04;
constexpr std::size_t kGfxImageWidthOffset = 0x18;
constexpr std::size_t kGfxImageHeightOffset = 0x1A;
constexpr std::size_t kDrawListViewInfoOffset = 0x0C;
constexpr std::size_t kDrawListViewOriginOffset = 0x14;
constexpr std::size_t kDrawListLightOffset = 0x24;
constexpr std::size_t kDrawListCameraViewOffset = 0x28;
constexpr std::size_t kIsRenderingFullScreenOffset = 0x13A4;
constexpr std::size_t kNeedsFloatZOffset = 0x13A8;
constexpr std::size_t kPointLightPartitionOffset = 0x5370;
constexpr std::size_t kPointLightInfoOffset = 0x40;
constexpr std::size_t kPointLightPartitionStride = 0x6C;
constexpr std::size_t kPointLightCountOffset = 0x55A0;
constexpr std::size_t kEmissiveSpotLightIndexOffset = 0x55A4;
constexpr std::size_t kEmissiveSpotDrawSurfCountOffset = 0x55E8;
constexpr std::size_t kEmissiveSpotDrawSurfsOffset = 0x55EC;
constexpr std::size_t kEmissiveSpotLightCountOffset = 0x55F0;
constexpr std::size_t kFullSceneViewMeshOffset = 0x6550;
constexpr std::size_t kQuadMeshDataSize = 0x30;
constexpr std::size_t kRequiredRetailBackEndBytes =
    kRetailGfxEntitiesOffset +
    kRetailGfxEntityCount * kRetailGfxEntityStride;
constexpr std::uint32_t kMaximumPointLightPartitions = 4;
constexpr std::uint32_t kMaximumDrawSurfCount = 131072;
constexpr std::size_t kDrawSurfSize = 8;
constexpr std::array<std::size_t, 5> kCoreDrawListOffsets = {
    0x6554, 0x6580, 0x65AC, 0x65D8, 0x6604,
};
static_assert(
    kPointLightPartitionOffset +
            kMaximumPointLightPartitions * kPointLightPartitionStride ==
        0x5520);
static_assert(kEmissiveSpotDrawSurfsOffset + sizeof(void*) <= kViewInfoStride);
using Draw3DInternalFunction = void(__cdecl*)(const void*);
using StandardEmissiveCallback = void(__cdecl*)(
    const void*, void*, void*, std::uintptr_t, std::uintptr_t);

struct RetailCmdBufContext final {
    void* source{};
    void* state{};
};

struct RetailDrawSurfListInfo final {
    const std::uint64_t* draw_surfs{};
    std::uint32_t draw_surf_count{};
    std::array<std::byte, 0x24> remaining{};
};

static_assert(sizeof(RetailCmdBufContext) == 8);
static_assert(sizeof(RetailDrawSurfListInfo) == 0x2C);

using DrawSurfsFunction = void(__cdecl*)(
    void*, const RetailDrawSurfListInfo*, RetailCmdBufContext);

std::atomic<bool> g_installed{false};
std::atomic<std::uintptr_t> g_original_standard_emissive_callback{0};
std::atomic<std::uintptr_t> g_draw_surfs_trampoline{0};
thread_local bool g_suppress_secondary_full_clear = false;
std::atomic<bool> g_physical_scope_draw_pass{false};
struct PhysicalScopeOwnerSlot final {
    std::atomic<std::uint32_t> sequence{0};
    std::atomic<std::uintptr_t> owner{0};
    std::atomic<std::uintptr_t> back_end_data{0};
};
std::array<PhysicalScopeOwnerSlot, 2> g_physical_scope_owner_slots{};
thread_local std::uint32_t g_diagnostic_eye_phase = 0;
thread_local std::uint32_t g_diagnostic_primary_clear_calls = 0;
thread_local std::uint32_t g_diagnostic_secondary_clear_calls = 0;
thread_local const void* g_emissive_callback_view = nullptr;
std::atomic<std::uint32_t> g_emissive_viewport_logged_phases{0};
std::atomic<std::uint32_t> g_emissive_occlusion_sampled_phases{0};

struct FxStateTelemetry final {
    bool initialized{};
    std::uint32_t emissive_draw_surf_count{};
    std::uint32_t point_light_count{};
    std::uint32_t emissive_spot_light_count{};
    std::uint32_t fullscreen_flag{};
    std::uint8_t needs_float_z{};
    ULONGLONG next_log_milliseconds{};
};

thread_local FxStateTelemetry g_fx_state_telemetry{};

void publish_physical_scope_owner_slot(
    PhysicalScopeOwnerSlot& slot,
    const std::uintptr_t owner,
    const std::uintptr_t back_end_data) noexcept {
    // The backend is the only producer. Make the generation odd while the
    // independently atomic pointers are changing so draw workers can reject a
    // mixed-frame snapshot instead of pairing an old owner with new data.
    slot.sequence.fetch_add(1, std::memory_order_seq_cst);
    slot.owner.store(0, std::memory_order_seq_cst);
    slot.back_end_data.store(back_end_data, std::memory_order_seq_cst);
    slot.owner.store(owner, std::memory_order_seq_cst);
    slot.sequence.fetch_add(1, std::memory_order_seq_cst);
}

[[nodiscard]] bool read_physical_scope_owner_slot(
    const PhysicalScopeOwnerSlot& slot,
    std::uintptr_t* const owner,
    std::uintptr_t* const back_end_data) noexcept {
    if (owner == nullptr || back_end_data == nullptr) {
        return false;
    }
    for (std::uint32_t attempt = 0; attempt < 3; ++attempt) {
        const std::uint32_t before =
            slot.sequence.load(std::memory_order_seq_cst);
        if ((before & 1u) != 0u) {
            continue;
        }
        const std::uintptr_t candidate_owner =
            slot.owner.load(std::memory_order_seq_cst);
        const std::uintptr_t candidate_data =
            slot.back_end_data.load(std::memory_order_seq_cst);
        const std::uint32_t after =
            slot.sequence.load(std::memory_order_seq_cst);
        if (before == after && (after & 1u) == 0u) {
            *owner = candidate_owner;
            *back_end_data = candidate_data;
            return true;
        }
    }
    return false;
}

void clear_physical_scope_owner_slots() noexcept {
    for (auto& slot : g_physical_scope_owner_slots) {
        publish_physical_scope_owner_slot(slot, 0, 0);
    }
}

void __cdecl stereo_backend_draw_thunk(const void* selected_view) noexcept;
void __cdecl stereo_emissive_scissor_thunk(
    const void* view, void* source, void* state,
    std::uintptr_t unused_first,
    std::uintptr_t unused_second) noexcept;
void __cdecl stereo_draw_surfs_thunk(
    void* prepass_state,
    const RetailDrawSurfListInfo* info,
    RetailCmdBufContext context) noexcept;
bool __cdecl should_suppress_secondary_clear() noexcept;
const char* validate_draw_surf_span(
    const void* draw_surfs, std::uint32_t count,
    DrawSurfaceAccess access = DrawSurfaceAccess::inspect) noexcept;

#if defined(_MSC_VER) && defined(_M_IX86)
// The T4 clear helper receives its color pointer in EAX and leaves four cdecl
// arguments on the caller's stack. This exact naked bridge preserves EAX,
// tail-calls the stock helper normally, and returns directly only for the
// second eye while our backend wrapper is active.
__declspec(naked) void stereo_clear_thunk_sp() noexcept {
    __asm {
        push eax
        call should_suppress_secondary_clear
        test al, al
        pop eax
        jnz suppress_clear
        mov edx, 0072BB30h
        jmp edx
    suppress_clear:
        ret
    }
}

__declspec(naked) void stereo_clear_thunk_mp() noexcept {
    __asm {
        push eax
        call should_suppress_secondary_clear
        test al, al
        pop eax
        jnz suppress_clear
        mov edx, 006FDC70h
        jmp edx
    suppress_clear:
        ret
    }
}
#else
#error The exact T4 clear bridge requires the 32-bit MSVC ABI.
#endif

struct PatchSite final {
    std::uintptr_t address{};
    std::array<std::uint8_t, kCallSize> original{};
    const void* replacement_target{};
};

std::array<std::uint8_t, kCallSize> relative_call(
    const std::uintptr_t address,
    const void* const target) noexcept {
    std::array<std::uint8_t, kCallSize> result{};
    result[0] = 0xE8;
    const auto next = static_cast<std::uint32_t>(address + kCallSize);
    const auto destination = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(target));
    const std::uint32_t displacement = destination - next;
    std::memcpy(result.data() + 1, &displacement, sizeof(displacement));
    return result;
}

std::array<std::uint8_t, kCallSize> absolute_push(
    const void* const target) noexcept {
    std::array<std::uint8_t, kCallSize> result{};
    result[0] = 0x68;
    const auto destination = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(target));
    std::memcpy(result.data() + 1, &destination, sizeof(destination));
    return result;
}

std::array<std::uint8_t, kCallSize> relative_jump(
    const std::uintptr_t address,
    const void* const target) noexcept {
    auto result = relative_call(address, target);
    result[0] = 0xE9;
    return result;
}

std::array<std::uint8_t, kDrawSurfsDetourSize> draw_surfs_detour(
    const StereoBackendExecutableProfile& profile) noexcept {
    std::array<std::uint8_t, kDrawSurfsDetourSize> result{};
    if (profile.draw_surfs_entry.address == 0) {
        return result;
    }
    const auto jump = relative_jump(
        profile.draw_surfs_entry.address,
        reinterpret_cast<const void*>(&stereo_draw_surfs_thunk));
    std::copy(jump.begin(), jump.end(), result.begin());
    std::fill(
        result.begin() + static_cast<std::ptrdiff_t>(jump.size()),
        result.end(), std::uint8_t{0x90});
    return result;
}

bool ensure_draw_surfs_trampoline(
    const StereoBackendExecutableProfile& profile) noexcept {
    if (profile.draw_surfs_entry.address == 0) {
        return true;
    }
    if (g_draw_surfs_trampoline.load(std::memory_order_acquire) != 0) {
        return true;
    }
    void* const allocation = VirtualAlloc(
        nullptr, kDrawSurfsTrampolineSize,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (allocation == nullptr) {
        return false;
    }
    auto* const bytes = static_cast<std::uint8_t*>(allocation);
    std::memcpy(
        bytes, profile.draw_surfs_entry.expected.data(),
        kDrawSurfsDetourSize);
    const auto return_jump = relative_jump(
        reinterpret_cast<std::uintptr_t>(bytes) + kDrawSurfsDetourSize,
        reinterpret_cast<const void*>(
            profile.draw_surfs_entry.address + kDrawSurfsDetourSize));
    std::memcpy(
        bytes + kDrawSurfsDetourSize,
        return_jump.data(), return_jump.size());
    DWORD old_protection = 0;
    if (!VirtualProtect(
            allocation, kDrawSurfsTrampolineSize,
            PAGE_EXECUTE_READ, &old_protection)) {
        VirtualFree(allocation, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(
        GetCurrentProcess(), allocation, kDrawSurfsTrampolineSize);
    g_draw_surfs_trampoline.store(
        reinterpret_cast<std::uintptr_t>(allocation),
        std::memory_order_release);
    return true;
}

std::array<PatchSite, 4> patch_sites(
    const StereoBackendExecutableProfile& profile) noexcept {
    const void* const clear_thunk =
        &profile == &kSpBackendProfile
            ? reinterpret_cast<const void*>(&stereo_clear_thunk_sp)
            : reinterpret_cast<const void*>(&stereo_clear_thunk_mp);
    return {{
        {profile.synchronous_draw_call_address,
         profile.expected_synchronous_draw_call,
         reinterpret_cast<const void*>(&stereo_backend_draw_thunk)},
        {profile.smp_draw_call_address, profile.expected_smp_draw_call,
         reinterpret_cast<const void*>(&stereo_backend_draw_thunk)},
        {profile.setup_clear_call_address, profile.expected_setup_clear_call,
         clear_thunk},
        {profile.scene_clear_call_address, profile.expected_scene_clear_call,
         clear_thunk},
    }};
}

PatchSite emissive_callback_patch_site(
    const StereoBackendExecutableProfile& profile) noexcept {
    return {
        profile.standard_emissive_callback_push_address,
        profile.expected_standard_emissive_callback_push,
        reinterpret_cast<const void*>(&stereo_emissive_scissor_thunk),
    };
}

bool protection_is_readable(const DWORD protection) noexcept {
    const DWORD access = protection & 0xffu;
    return access == PAGE_READONLY || access == PAGE_READWRITE ||
           access == PAGE_WRITECOPY || access == PAGE_EXECUTE_READ ||
           access == PAGE_EXECUTE_READWRITE ||
           access == PAGE_EXECUTE_WRITECOPY;
}

bool protection_is_writable(const DWORD protection) noexcept {
    const DWORD access = protection & 0xffu;
    return access == PAGE_READWRITE || access == PAGE_WRITECOPY ||
           access == PAGE_EXECUTE_READWRITE ||
           access == PAGE_EXECUTE_WRITECOPY;
}

bool accessible_range(
    const void* const address,
    const std::size_t size,
    const bool writable = false) noexcept {
    return resident_page_access(address, size, writable,
        &grouped_virtual_query<VirtualQueryTimingGroup::stereo_backend>);
}

enum class WritableSpanRejectReason : std::uint8_t {
    none,
    invalid_argument,
    address_overflow,
    query_or_gap,
    not_committed,
    guarded_or_noaccess,
    not_readable,
    not_writable,
    region_limit,
};

struct WritableSpanValidation final {
    WritableSpanRejectReason reason{WritableSpanRejectReason::none};
    std::size_t regions{};
    std::uintptr_t cursor{};
    MEMORY_BASIC_INFORMATION memory{};
};

const char* describe_span_rejection(
    const WritableSpanRejectReason reason) noexcept {
    switch (reason) {
    case WritableSpanRejectReason::none:
        return "none";
    case WritableSpanRejectReason::invalid_argument:
        return "invalid-argument";
    case WritableSpanRejectReason::address_overflow:
        return "address-overflow";
    case WritableSpanRejectReason::query_or_gap:
        return "query-or-gap";
    case WritableSpanRejectReason::not_committed:
        return "not-committed";
    case WritableSpanRejectReason::guarded_or_noaccess:
        return "guarded-or-noaccess";
    case WritableSpanRejectReason::not_readable:
        return "not-readable";
    case WritableSpanRejectReason::not_writable:
        return "not-writable";
    case WritableSpanRejectReason::region_limit:
        return "region-limit";
    }
    return "unknown";
}

bool accessible_writable_span(
    const void* const address,
    const std::size_t size,
    WritableSpanValidation* const output) noexcept {
    constexpr std::size_t kMaximumRegions = 512;
    WritableSpanValidation result{};
    result.cursor = reinterpret_cast<std::uintptr_t>(address);
    if (address == nullptr || size == 0) {
        result.reason = WritableSpanRejectReason::invalid_argument;
        if (output != nullptr) {
            *output = result;
        }
        return false;
    }

    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    if (size > std::numeric_limits<std::uintptr_t>::max() - begin) {
        result.reason = WritableSpanRejectReason::address_overflow;
        if (output != nullptr) {
            *output = result;
        }
        return false;
    }
    const std::uintptr_t end = begin + size;
    std::uintptr_t cursor = begin;

    while (cursor < end) {
        result.cursor = cursor;
        if (result.regions >= kMaximumRegions) {
            result.reason = WritableSpanRejectReason::region_limit;
            if (output != nullptr) {
                *output = result;
            }
            return false;
        }

        MEMORY_BASIC_INFORMATION memory{};
        const SIZE_T queried = grouped_virtual_query<
            VirtualQueryTimingGroup::stereo_backend>(
            reinterpret_cast<const void*>(cursor), &memory,
            sizeof(memory));
        result.memory = memory;
        const auto region_begin =
            reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
        const bool region_end_valid =
            memory.RegionSize <=
            std::numeric_limits<std::uintptr_t>::max() - region_begin;
        const std::uintptr_t region_end = region_end_valid
            ? region_begin + memory.RegionSize
            : region_begin;
        if (queried != sizeof(memory) || !region_end_valid ||
            region_begin > cursor || cursor >= region_end) {
            result.reason = WritableSpanRejectReason::query_or_gap;
        } else if (memory.State != MEM_COMMIT) {
            result.reason = WritableSpanRejectReason::not_committed;
        } else if (
            (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
            result.reason = WritableSpanRejectReason::guarded_or_noaccess;
        } else if (!protection_is_readable(memory.Protect)) {
            result.reason = WritableSpanRejectReason::not_readable;
        } else if (!protection_is_writable(memory.Protect)) {
            result.reason = WritableSpanRejectReason::not_writable;
        }
        if (result.reason != WritableSpanRejectReason::none) {
            if (output != nullptr) {
                *output = result;
            }
            return false;
        }

        ++result.regions;
        cursor = region_end < end ? region_end : end;
    }

    result.cursor = end;
    if (output != nullptr) {
        *output = result;
    }
    return true;
}

template <typename T>
T read_value(const std::byte* const base, const std::size_t offset) noexcept {
    T result{};
    std::memcpy(&result, base + offset, sizeof(result));
    return result;
}

template <typename T>
void write_value(
    std::byte* const base,
    const std::size_t offset,
    const T& value) noexcept {
    std::memcpy(base + offset, &value, sizeof(value));
}

[[nodiscard]] bool fx_stereo_diagnostics_enabled() noexcept {
    static const bool enabled = []() noexcept {
        wchar_t value[2]{};
        return GetEnvironmentVariableW(
                   L"WAWVR_FX_STEREO_DIAGNOSTICS", value, 2) == 1 &&
            value[0] == L'1';
    }();
    return enabled;
}

[[nodiscard]] bool force_emissive_viewport_requested() noexcept {
    static const bool requested = []() noexcept {
        wchar_t value[2]{};
        return GetEnvironmentVariableW(
                   L"WAWVR_FORCE_EMISSIVE_VIEWPORT", value, 2) == 1 &&
            value[0] == L'1';
    }();
    return requested;
}

[[nodiscard]] bool emissive_occlusion_diagnostics_enabled() noexcept {
    static const bool enabled = []() noexcept {
        wchar_t value[2]{};
        return GetEnvironmentVariableW(
                   L"WAWVR_FX_OCCLUSION_DIAGNOSTICS", value, 2) == 1 &&
            value[0] == L'1';
    }();
    return enabled;
}

struct EmissiveDeviceStateSnapshot final {
    std::uint32_t successful_reads{};
    DWORD z_enable{};
    DWORD z_write_enable{};
    DWORD z_function{};
    DWORD alpha_test_enable{};
    DWORD alpha_function{};
    DWORD alpha_reference{};
    DWORD alpha_blend_enable{};
    DWORD source_blend{};
    DWORD destination_blend{};
    DWORD blend_operation{};
    DWORD stencil_enable{};
    DWORD color_write_enable{};
    DWORD cull_mode{};
    DWORD clip_plane_enable{};
};

void read_emissive_device_state(
    IDirect3DDevice9* const device,
    EmissiveDeviceStateSnapshot* const snapshot) noexcept {
    if (device == nullptr || snapshot == nullptr) {
        return;
    }
    const auto read_state = [device, snapshot](
                                const D3DRENDERSTATETYPE state,
                                DWORD* const value,
                                const std::uint32_t bit) noexcept {
        if (SUCCEEDED(device->GetRenderState(state, value))) {
            snapshot->successful_reads |= bit;
        }
    };
    read_state(D3DRS_ZENABLE, &snapshot->z_enable, 1u << 0u);
    read_state(
        D3DRS_ZWRITEENABLE, &snapshot->z_write_enable, 1u << 1u);
    read_state(D3DRS_ZFUNC, &snapshot->z_function, 1u << 2u);
    read_state(
        D3DRS_ALPHATESTENABLE, &snapshot->alpha_test_enable, 1u << 3u);
    read_state(D3DRS_ALPHAFUNC, &snapshot->alpha_function, 1u << 4u);
    read_state(
        D3DRS_ALPHAREF, &snapshot->alpha_reference, 1u << 5u);
    read_state(
        D3DRS_ALPHABLENDENABLE, &snapshot->alpha_blend_enable, 1u << 6u);
    read_state(D3DRS_SRCBLEND, &snapshot->source_blend, 1u << 7u);
    read_state(
        D3DRS_DESTBLEND, &snapshot->destination_blend, 1u << 8u);
    read_state(D3DRS_BLENDOP, &snapshot->blend_operation, 1u << 9u);
    read_state(
        D3DRS_STENCILENABLE, &snapshot->stencil_enable, 1u << 10u);
    read_state(
        D3DRS_COLORWRITEENABLE, &snapshot->color_write_enable,
        1u << 11u);
    read_state(D3DRS_CULLMODE, &snapshot->cull_mode, 1u << 12u);
    read_state(
        D3DRS_CLIPPLANEENABLE, &snapshot->clip_plane_enable,
        1u << 13u);
}

[[nodiscard]] std::array<std::uint32_t, 16>
emissive_surface_type_histogram(
    const RetailDrawSurfListInfo& info) noexcept {
    std::array<std::uint32_t, 16> result{};
    for (std::uint32_t index = 0; index < info.draw_surf_count; ++index) {
        const std::uint32_t type = static_cast<std::uint32_t>(
            (info.draw_surfs[index] >> 54u) & 0xFu);
        ++result[type];
    }
    return result;
}

bool draw_with_emissive_occlusion_diagnostics(
    const DrawSurfsFunction original,
    void* const prepass_state,
    const RetailDrawSurfListInfo* const info,
    const RetailCmdBufContext context) noexcept {
    if (!emissive_occlusion_diagnostics_enabled() ||
        g_diagnostic_eye_phase == 0u ||
        g_diagnostic_eye_phase > 2u || info == nullptr ||
        info->draw_surf_count < 128u || context.state == nullptr ||
        !accessible_range(
            context.state, kRendererDeviceOffset + sizeof(void*))) {
        return false;
    }

    // The ordinary draw route does not inspect native surface data. This
    // opt-in diagnostic does, so validate its complete span immediately
    // before the histogram instead of querying every native draw list.
    if (validate_draw_surf_span(
            info->draw_surfs, info->draw_surf_count) != nullptr) {
        return false;
    }
    const auto histogram = emissive_surface_type_histogram(*info);
    // Retail WaW dispatches dynamic code-mesh FX as type 11 and particle
    // clouds as type 13. Waiting for a dense FX list avoids spending the
    // one-shot query on loading-screen/UI emissives before the smoke scene.
    if (histogram[11] < 32u && histogram[13] < 8u) {
        return false;
    }

    const std::uint32_t phase_bit =
        1u << (g_diagnostic_eye_phase - 1u);
    if ((g_emissive_occlusion_sampled_phases.fetch_or(
             phase_bit, std::memory_order_acq_rel) & phase_bit) != 0u) {
        return false;
    }

    const auto* const state_bytes = static_cast<const std::byte*>(
        context.state);
    IDirect3DDevice9* const device = read_value<IDirect3DDevice9*>(
        state_bytes, kRendererDeviceOffset);
    if (device == nullptr) {
        stereo_diagnostic_log(
            "StereoDiag backend.emissive-occlusion: phase=%u count=%u rejected=null-device type11=%u type13=%u",
            g_diagnostic_eye_phase, info->draw_surf_count,
            histogram[11], histogram[13]);
        return false;
    }

    EmissiveDeviceStateSnapshot before{};
    EmissiveDeviceStateSnapshot after{};
    read_emissive_device_state(device, &before);

    struct PendingFxQuery final {
        IDirect3DQuery9* query{};
        std::uint32_t surface_type{};
        HRESULT begin_result{E_FAIL};
        HRESULT end_result{E_FAIL};
    };
    // The emissive list is material sorted. Preserve its exact draw order and
    // draw every surface once, but split only at FX/non-FX or FX-type
    // boundaries so the queries cannot count unrelated emissive geometry.
    constexpr std::size_t kMaximumPendingFxQueries = 64;
    std::array<PendingFxQuery, kMaximumPendingFxQueries> pending{};
    std::size_t pending_count = 0;
    std::uint32_t queried_type11_surfaces = 0;
    std::uint32_t queried_type13_surfaces = 0;
    std::uint32_t unqueried_fx_spans = 0;
    HRESULT first_create_failure = S_OK;

    std::uint32_t segment_begin = 0;
    while (segment_begin < info->draw_surf_count) {
        const std::uint32_t segment_type = static_cast<std::uint32_t>(
            (info->draw_surfs[segment_begin] >> 54u) & 0xFu);
        const bool segment_is_fx =
            segment_type == 11u || segment_type == 13u;
        std::uint32_t segment_end = segment_begin + 1u;
        while (segment_end < info->draw_surf_count) {
            const std::uint32_t candidate_type = static_cast<std::uint32_t>(
                (info->draw_surfs[segment_end] >> 54u) & 0xFu);
            const bool candidate_is_fx =
                candidate_type == 11u || candidate_type == 13u;
            if (candidate_is_fx != segment_is_fx ||
                (segment_is_fx && candidate_type != segment_type)) {
                break;
            }
            ++segment_end;
        }

        RetailDrawSurfListInfo segment = *info;
        segment.draw_surfs = info->draw_surfs + segment_begin;
        segment.draw_surf_count = segment_end - segment_begin;
        if (!segment_is_fx || pending_count >= pending.size()) {
            if (segment_is_fx) {
                ++unqueried_fx_spans;
            }
            original(prepass_state, &segment, context);
            segment_begin = segment_end;
            continue;
        }

        IDirect3DQuery9* query = nullptr;
        const HRESULT create_result = device->CreateQuery(
            D3DQUERYTYPE_OCCLUSION, &query);
        if (FAILED(create_result) || query == nullptr) {
            if (SUCCEEDED(first_create_failure)) {
                first_create_failure = create_result;
            }
            ++unqueried_fx_spans;
            original(prepass_state, &segment, context);
            segment_begin = segment_end;
            continue;
        }

        PendingFxQuery& record = pending[pending_count++];
        record.query = query;
        record.surface_type = segment_type;
        record.begin_result = query->Issue(D3DISSUE_BEGIN);
        original(prepass_state, &segment, context);
        if (SUCCEEDED(record.begin_result)) {
            record.end_result = query->Issue(D3DISSUE_END);
        }
        if (segment_type == 11u) {
            queried_type11_surfaces += segment.draw_surf_count;
        } else {
            queried_type13_surfaces += segment.draw_surf_count;
        }
        segment_begin = segment_end;
    }
    read_emissive_device_state(device, &after);

    std::uint64_t type11_visible_samples = 0;
    std::uint64_t type13_visible_samples = 0;
    std::uint32_t type11_completed_queries = 0;
    std::uint32_t type13_completed_queries = 0;
    std::uint32_t timed_out_queries = 0;
    std::uint32_t total_polls = 0;
    const ULONGLONG deadline = GetTickCount64() + 250u;
    for (std::size_t index = 0; index < pending_count; ++index) {
        PendingFxQuery& record = pending[index];
        DWORD visible_samples = 0;
        HRESULT data_result = E_FAIL;
        if (SUCCEEDED(record.end_result)) {
            do {
                data_result = record.query->GetData(
                    &visible_samples, sizeof(visible_samples),
                    D3DGETDATA_FLUSH);
                ++total_polls;
                if (data_result != S_FALSE) {
                    break;
                }
                SwitchToThread();
            } while (GetTickCount64() < deadline);
        }
        if (data_result == S_OK) {
            if (record.surface_type == 11u) {
                type11_visible_samples += visible_samples;
                ++type11_completed_queries;
            } else {
                type13_visible_samples += visible_samples;
                ++type13_completed_queries;
            }
        } else {
            ++timed_out_queries;
        }
        record.query->Release();
        record.query = nullptr;
    }

    stereo_diagnostic_log(
        "StereoDiag backend.emissive-occlusion: phase=%u count=%u type11=(surfs=%u queried=%u complete=%u samples=%llu) type13=(surfs=%u queried=%u complete=%u samples=%llu) pending=%zu unqueriedSpans=%u timedOut=%u polls=%u createHr=0x%08lX before=(ok=0x%X z=%lu zw=%lu zf=%lu at=%lu af=%lu ar=%lu blend=%lu src=%lu dst=%lu op=%lu stencil=%lu color=0x%lX cull=%lu clip=0x%lX) after=(ok=0x%X z=%lu zw=%lu zf=%lu at=%lu af=%lu ar=%lu blend=%lu src=%lu dst=%lu op=%lu stencil=%lu color=0x%lX cull=%lu clip=0x%lX)",
        g_diagnostic_eye_phase, info->draw_surf_count,
        histogram[11], queried_type11_surfaces,
        type11_completed_queries,
        static_cast<unsigned long long>(type11_visible_samples),
        histogram[13], queried_type13_surfaces,
        type13_completed_queries,
        static_cast<unsigned long long>(type13_visible_samples),
        pending_count, unqueried_fx_spans, timed_out_queries, total_polls,
        static_cast<unsigned long>(first_create_failure),
        before.successful_reads,
        static_cast<unsigned long>(before.z_enable),
        static_cast<unsigned long>(before.z_write_enable),
        static_cast<unsigned long>(before.z_function),
        static_cast<unsigned long>(before.alpha_test_enable),
        static_cast<unsigned long>(before.alpha_function),
        static_cast<unsigned long>(before.alpha_reference),
        static_cast<unsigned long>(before.alpha_blend_enable),
        static_cast<unsigned long>(before.source_blend),
        static_cast<unsigned long>(before.destination_blend),
        static_cast<unsigned long>(before.blend_operation),
        static_cast<unsigned long>(before.stencil_enable),
        static_cast<unsigned long>(before.color_write_enable),
        static_cast<unsigned long>(before.cull_mode),
        static_cast<unsigned long>(before.clip_plane_enable),
        after.successful_reads,
        static_cast<unsigned long>(after.z_enable),
        static_cast<unsigned long>(after.z_write_enable),
        static_cast<unsigned long>(after.z_function),
        static_cast<unsigned long>(after.alpha_test_enable),
        static_cast<unsigned long>(after.alpha_function),
        static_cast<unsigned long>(after.alpha_reference),
        static_cast<unsigned long>(after.alpha_blend_enable),
        static_cast<unsigned long>(after.source_blend),
        static_cast<unsigned long>(after.destination_blend),
        static_cast<unsigned long>(after.blend_operation),
        static_cast<unsigned long>(after.stencil_enable),
        static_cast<unsigned long>(after.color_write_enable),
        static_cast<unsigned long>(after.cull_mode),
        static_cast<unsigned long>(after.clip_plane_enable));
    return true;
}

StereoEmissiveViewport read_emissive_viewport(
    const std::byte* const base,
    const std::size_t offset) noexcept {
    return {
        read_value<std::int32_t>(base, offset),
        read_value<std::int32_t>(base, offset + sizeof(std::int32_t)),
        read_value<std::int32_t>(base, offset + sizeof(std::int32_t) * 2u),
        read_value<std::int32_t>(base, offset + sizeof(std::int32_t) * 3u),
    };
}

class ScopedEmissiveCallbackView final {
public:
    explicit ScopedEmissiveCallbackView(const void* const view) noexcept
        : previous_(g_emissive_callback_view) {
        g_emissive_callback_view = view;
    }

    ~ScopedEmissiveCallbackView() {
        g_emissive_callback_view = previous_;
    }

    ScopedEmissiveCallbackView(const ScopedEmissiveCallbackView&) = delete;
    ScopedEmissiveCallbackView& operator=(
        const ScopedEmissiveCallbackView&) = delete;

private:
    const void* previous_{};
};

void inspect_and_force_stereo_emissive_viewport(
    const void* const view,
    const void* const source,
    const void* const state,
    const std::uint32_t draw_surf_count) noexcept {
    const bool diagnostics = fx_stereo_diagnostics_enabled();
    const bool force_requested = force_emissive_viewport_requested();
    if (g_diagnostic_eye_phase == 0u ||
        (!diagnostics && !force_requested) ||
        !accessible_range(
            view, kScissorViewportOffset + sizeof(std::int32_t) * 4u) ||
        !accessible_range(
            source, kSourceViewportDirtyOffset + sizeof(std::uint8_t)) ||
        !accessible_range(
            state, kRendererCachedViewportOffset +
                sizeof(std::int32_t) * 4u)) {
        return;
    }

    const auto* const view_bytes = static_cast<const std::byte*>(view);
    const auto* const source_bytes = static_cast<const std::byte*>(source);
    const auto* const state_bytes = static_cast<const std::byte*>(state);
    IDirect3DDevice9* const device = read_value<IDirect3DDevice9*>(
        state_bytes, kRendererDeviceOffset);
    if (device == nullptr) {
        return;
    }

    const StereoEmissiveViewport expected = read_emissive_viewport(
        view_bytes, kSceneViewportOffset);
    const StereoEmissiveViewport view_scissor = read_emissive_viewport(
        view_bytes, kScissorViewportOffset);
    const StereoEmissiveViewport source_viewport = read_emissive_viewport(
        source_bytes, kSourceSceneViewportOffset);
    const StereoEmissiveViewport cached_viewport = read_emissive_viewport(
        state_bytes, kRendererCachedViewportOffset);
    const std::uint32_t source_behavior = read_value<std::uint32_t>(
        source_bytes, kSourceViewportBehaviorOffset);
    const std::uint32_t source_target_width = read_value<std::uint32_t>(
        source_bytes, kSourceRenderTargetWidthOffset);
    const std::uint32_t source_target_height = read_value<std::uint32_t>(
        source_bytes, kSourceRenderTargetHeightOffset);
    const std::uint8_t source_dirty = read_value<std::uint8_t>(
        source_bytes, kSourceViewportDirtyOffset);

    D3DVIEWPORT9 hardware_before{};
    const HRESULT get_viewport_result = device->GetViewport(&hardware_before);
    RECT hardware_scissor{};
    const HRESULT get_scissor_result = device->GetScissorRect(
        &hardware_scissor);
    DWORD scissor_enabled = 0;
    const HRESULT get_scissor_enable_result = device->GetRenderState(
        D3DRS_SCISSORTESTENABLE, &scissor_enabled);

    std::uint32_t target_width = source_target_width;
    std::uint32_t target_height = source_target_height;
    IDirect3DSurface9* render_target = nullptr;
    D3DSURFACE_DESC target_desc{};
    const HRESULT get_target_result = device->GetRenderTarget(
        0u, &render_target);
    HRESULT get_target_desc_result = E_FAIL;
    if (SUCCEEDED(get_target_result) && render_target != nullptr) {
        get_target_desc_result = render_target->GetDesc(&target_desc);
        if (SUCCEEDED(get_target_desc_result)) {
            target_width = target_desc.Width;
            target_height = target_desc.Height;
        }
        render_target->Release();
    }

    StereoEmissiveViewport hardware_viewport{};
    if (SUCCEEDED(get_viewport_result) &&
        hardware_before.X <=
            static_cast<DWORD>(std::numeric_limits<std::int32_t>::max()) &&
        hardware_before.Y <=
            static_cast<DWORD>(std::numeric_limits<std::int32_t>::max()) &&
        hardware_before.Width <=
            static_cast<DWORD>(std::numeric_limits<std::int32_t>::max()) &&
        hardware_before.Height <=
            static_cast<DWORD>(std::numeric_limits<std::int32_t>::max())) {
        hardware_viewport = {
            static_cast<std::int32_t>(hardware_before.X),
            static_cast<std::int32_t>(hardware_before.Y),
            static_cast<std::int32_t>(hardware_before.Width),
            static_cast<std::int32_t>(hardware_before.Height),
        };
    }

    const bool should_force = SUCCEEDED(get_viewport_result) &&
        should_force_stereo_emissive_viewport(
            force_requested, g_diagnostic_eye_phase, expected,
            hardware_viewport, target_width, target_height);
    HRESULT force_result = S_FALSE;
    D3DVIEWPORT9 hardware_after = hardware_before;
    HRESULT get_after_result = get_viewport_result;
    if (should_force) {
        const D3DVIEWPORT9 desired{
            static_cast<DWORD>(expected.x),
            static_cast<DWORD>(expected.y),
            static_cast<DWORD>(expected.width),
            static_cast<DWORD>(expected.height),
            hardware_before.MinZ,
            hardware_before.MaxZ,
        };
        force_result = device->SetViewport(&desired);
        if (SUCCEEDED(force_result)) {
            get_after_result = device->GetViewport(&hardware_after);
        }
    }

    const std::uint32_t phase_bit = g_diagnostic_eye_phase < 32u
        ? 1u << (g_diagnostic_eye_phase - 1u)
        : 0u;
    const bool first_phase_log = phase_bit != 0u &&
        (g_emissive_viewport_logged_phases.fetch_or(
             phase_bit, std::memory_order_relaxed) & phase_bit) == 0u;
    if (diagnostics && first_phase_log) {
        stereo_diagnostic_log(
            "StereoDiag backend.emissive-viewport: phase=%u drawSurfCount=%u expected=(%d,%d %dx%d) viewScissor=(%d,%d %dx%d) source=(%d,%d %dx%d behavior=%u dirty=%u rt=%ux%u) cached=(%d,%d %dx%d) hardwareBefore=(%lu,%lu %lux%lu z=%.3f..%.3f hr=0x%08lX) scissor=(%ld,%ld %ldx%ld enabled=%lu hr=0x%08lX/0x%08lX) targetHr=0x%08lX/0x%08lX forceRequested=%u forceApplied=%u forceHr=0x%08lX hardwareAfter=(%lu,%lu %lux%lu hr=0x%08lX)",
            g_diagnostic_eye_phase, draw_surf_count,
            expected.x, expected.y, expected.width, expected.height,
            view_scissor.x, view_scissor.y,
            view_scissor.width, view_scissor.height,
            source_viewport.x, source_viewport.y,
            source_viewport.width, source_viewport.height,
            source_behavior, static_cast<unsigned int>(source_dirty),
            target_width, target_height,
            cached_viewport.x, cached_viewport.y,
            cached_viewport.width, cached_viewport.height,
            static_cast<unsigned long>(hardware_before.X),
            static_cast<unsigned long>(hardware_before.Y),
            static_cast<unsigned long>(hardware_before.Width),
            static_cast<unsigned long>(hardware_before.Height),
            hardware_before.MinZ, hardware_before.MaxZ,
            static_cast<unsigned long>(get_viewport_result),
            hardware_scissor.left, hardware_scissor.top,
            hardware_scissor.right - hardware_scissor.left,
            hardware_scissor.bottom - hardware_scissor.top,
            static_cast<unsigned long>(scissor_enabled),
            static_cast<unsigned long>(get_scissor_result),
            static_cast<unsigned long>(get_scissor_enable_result),
            static_cast<unsigned long>(get_target_result),
            static_cast<unsigned long>(get_target_desc_result),
            force_requested ? 1u : 0u, should_force ? 1u : 0u,
            static_cast<unsigned long>(force_result),
            static_cast<unsigned long>(hardware_after.X),
            static_cast<unsigned long>(hardware_after.Y),
            static_cast<unsigned long>(hardware_after.Width),
            static_cast<unsigned long>(hardware_after.Height),
            static_cast<unsigned long>(get_after_result));
    }
}

bool apply_stereo_emissive_scissor(
    const void* const view,
    const void* const state) noexcept {
    if (g_diagnostic_eye_phase == 0 ||
        !accessible_range(
            view, kScissorViewportOffset + sizeof(LONG) * 4u) ||
        !accessible_range(
            state, kRendererDeviceOffset + sizeof(void*))) {
        return false;
    }

    const auto* const view_bytes =
        static_cast<const std::byte*>(view);
    const LONG x = read_value<LONG>(view_bytes, kScissorViewportOffset);
    const LONG y = read_value<LONG>(
        view_bytes, kScissorViewportOffset + sizeof(LONG));
    const LONG width = read_value<LONG>(
        view_bytes, kScissorViewportOffset + sizeof(LONG) * 2u);
    const LONG height = read_value<LONG>(
        view_bytes, kScissorViewportOffset + sizeof(LONG) * 3u);
    if (x < 0 || y < 0 || width <= 0 || height <= 0 ||
        width > std::numeric_limits<LONG>::max() - x ||
        height > std::numeric_limits<LONG>::max() - y) {
        return false;
    }

    const auto* const state_bytes =
        static_cast<const std::byte*>(state);
    IDirect3DDevice9* const device = read_value<IDirect3DDevice9*>(
        state_bytes, kRendererDeviceOffset);
    if (device == nullptr) {
        return false;
    }

    const RECT scissor = {x, y, x + width, y + height};
    const HRESULT enable_result = device->SetRenderState(
        D3DRS_SCISSORTESTENABLE, TRUE);
    if (FAILED(enable_result)) {
        WAWVR_STEREO_DIAG_ONCE(
            "Stereo emissive scissor repair could not enable scissoring: phase=%u hr=0x%08lX",
            g_diagnostic_eye_phase,
            static_cast<unsigned long>(enable_result));
        return false;
    }
    const HRESULT rectangle_result = device->SetScissorRect(&scissor);
    if (FAILED(rectangle_result)) {
        device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        WAWVR_STEREO_DIAG_ONCE(
            "Stereo emissive scissor repair could not set the eye rectangle: phase=%u hr=0x%08lX",
            g_diagnostic_eye_phase,
            static_cast<unsigned long>(rectangle_result));
        return false;
    }
    return true;
}

void __cdecl stereo_emissive_scissor_thunk(
    const void* const view,
    void* const source,
    void* const state,
    const std::uintptr_t unused_first,
    const std::uintptr_t unused_second) noexcept {
    const auto original_address =
        g_original_standard_emissive_callback.load(std::memory_order_acquire);
    if (original_address == 0) {
        return;
    }
    if (g_diagnostic_eye_phase != 0 &&
        !apply_stereo_emissive_scissor(view, state)) {
        WAWVR_STEREO_DIAG_ONCE(
            "Stereo emissive scissor repair rejected an invalid eye/state record: phase=%u view=%p state=%p",
            g_diagnostic_eye_phase, view, state);
    }

    ScopedEmissiveCallbackView active_view(view);
    const auto original = reinterpret_cast<StandardEmissiveCallback>(
        original_address);
    original(
        view, source, state, unused_first, unused_second);
}

bool bytes_equal(
    const std::uintptr_t address,
    const std::uint8_t* const expected,
    const std::size_t size) noexcept {
    const auto* const current =
        reinterpret_cast<const std::uint8_t*>(address);
    return accessible_range(current, size) &&
           std::memcmp(current, expected, size) == 0;
}

bool decoded_call_targets(
    const std::uintptr_t address,
    const std::uintptr_t target) noexcept {
    std::int32_t displacement = 0;
    std::memcpy(
        &displacement, reinterpret_cast<const void*>(address + 1),
        sizeof(displacement));
    const auto decoded = static_cast<std::uintptr_t>(
        static_cast<std::uint32_t>(address + kCallSize + displacement));
    return decoded == target;
}

template <std::size_t Size>
bool sentinel_equal(const ExactSentinel<Size>& sentinel) noexcept {
    return bytes_equal(
        sentinel.address, sentinel.expected.data(), sentinel.expected.size());
}

bool verify_layout_sentinels(
    const StereoBackendExecutableProfile& profile) noexcept {
    return sentinel_equal(profile.standard_emissive_callback_tail) &&
           sentinel_equal(profile.view_stride) &&
           sentinel_equal(profile.split_lit_primary) &&
           sentinel_equal(profile.split_lit_secondary) &&
           sentinel_equal(profile.lit_list) &&
           sentinel_equal(profile.emissive_list) &&
           sentinel_equal(profile.decal_list) &&
           sentinel_equal(profile.list_metadata) &&
           sentinel_equal(profile.point_light_base) &&
           sentinel_equal(profile.point_light_info) &&
           sentinel_equal(profile.point_light_stride) &&
           sentinel_equal(profile.point_light_count) &&
           sentinel_equal(profile.emissive_spot_layout);
}

bool verify_original_profile() noexcept {
    const auto* const profile = configured_backend_profile();
    if (profile == nullptr) {
        return false;
    }
    const auto sites = patch_sites(*profile);
    for (const auto& site : sites) {
        if (!bytes_equal(
                site.address, site.original.data(), site.original.size())) {
            return false;
        }
    }
    if (profile->draw_surfs_entry.address != 0 &&
        !sentinel_equal(profile->draw_surfs_entry)) {
        return false;
    }
    const auto callback_site = emissive_callback_patch_site(*profile);
    if (!bytes_equal(
            callback_site.address, callback_site.original.data(),
            callback_site.original.size())) {
        return false;
    }
    return decoded_call_targets(
               profile->synchronous_draw_call_address,
               profile->draw_3d_internal_address) &&
           decoded_call_targets(
               profile->smp_draw_call_address,
               profile->draw_3d_internal_address) &&
           decoded_call_targets(
               profile->setup_clear_call_address,
               profile->clear_screen_address) &&
           decoded_call_targets(
               profile->scene_clear_call_address,
               profile->clear_screen_address) &&
           bytes_equal(
               profile->draw_3d_internal_address,
               profile->expected_draw_3d_internal_entry.data(),
               profile->expected_draw_3d_internal_entry.size()) &&
           bytes_equal(
               profile->clear_screen_address,
               profile->expected_clear_screen_entry.data(),
               profile->expected_clear_screen_entry.size()) &&
           bytes_equal(
               profile->standard_emissive_callback_address,
               profile->expected_standard_emissive_callback_entry.data(),
               profile->expected_standard_emissive_callback_entry.size()) &&
           verify_layout_sentinels(*profile);
}

bool verify_replacement_ownership() noexcept {
    const auto* const profile = configured_backend_profile();
    if (profile == nullptr) {
        return false;
    }
    const auto sites = patch_sites(*profile);
    for (const auto& site : sites) {
        const auto replacement =
            relative_call(site.address, site.replacement_target);
        if (!bytes_equal(
                site.address, replacement.data(), replacement.size())) {
            return false;
        }
    }
    const auto callback_site = emissive_callback_patch_site(*profile);
    const auto callback_replacement =
        absolute_push(callback_site.replacement_target);
    if (profile->draw_surfs_entry.address != 0) {
        const auto detour = draw_surfs_detour(*profile);
        if (!bytes_equal(
                profile->draw_surfs_entry.address,
                detour.data(), detour.size()) ||
            g_draw_surfs_trampoline.load(std::memory_order_acquire) == 0) {
            return false;
        }
    }
    return bytes_equal(
               callback_site.address, callback_replacement.data(),
               callback_replacement.size()) &&
           verify_layout_sentinels(*profile) &&
           bytes_equal(
               profile->draw_3d_internal_address,
               profile->expected_draw_3d_internal_entry.data(),
               profile->expected_draw_3d_internal_entry.size()) &&
           bytes_equal(
               profile->clear_screen_address,
               profile->expected_clear_screen_entry.data(),
               profile->expected_clear_screen_entry.size()) &&
           bytes_equal(
               profile->standard_emissive_callback_address,
               profile->expected_standard_emissive_callback_entry.data(),
               profile->expected_standard_emissive_callback_entry.size());
}

[[nodiscard]] bool suspend_backend_patch_threads(
    SuspendedPeerThreads* const suspended) noexcept {
    const auto* const profile = configured_backend_profile();
    if (suspended == nullptr || profile == nullptr) {
        return false;
    }
    const auto sites = patch_sites(*profile);
    std::array<PeerThreadPatchRange, 7> patch_ranges{};
    for (std::size_t index = 0; index < sites.size(); ++index) {
        patch_ranges[index] = {sites[index].address, kCallSize};
    }
    patch_ranges[sites.size()] = {
        profile->standard_emissive_callback_push_address, kCallSize};
    patch_ranges[sites.size() + 1] = {
        profile->draw_surfs_entry.address,
        profile->draw_surfs_entry.address != 0
            ? kDrawSurfsDetourSize
            : 0};
    const std::uintptr_t trampoline =
        g_draw_surfs_trampoline.load(std::memory_order_acquire);
    patch_ranges[sites.size() + 2] = {
        trampoline,
        trampoline != 0 ? kDrawSurfsTrampolineSize : 0};
    return suspended->suspend(patch_ranges);
}

class WritablePatchPages final {
public:
    bool Acquire() noexcept {
        const auto* const profile = configured_backend_profile();
        if (profile == nullptr) {
            return false;
        }
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        const auto page_size = static_cast<std::uintptr_t>(info.dwPageSize);
        if (page_size == 0) {
            return false;
        }
        pages_[0].address = reinterpret_cast<void*>(
            profile->synchronous_draw_call_address & ~(page_size - 1u));
        pages_[1].address = reinterpret_cast<void*>(
            profile->setup_clear_call_address & ~(page_size - 1u));
        pages_[2].address = reinterpret_cast<void*>(
            profile->standard_emissive_callback_push_address &
            ~(page_size - 1u));
        const std::uintptr_t fourth_patch_address =
            profile->draw_surfs_entry.address != 0
                ? profile->draw_surfs_entry.address
                : profile->standard_emissive_callback_push_address;
        pages_[3].address = reinterpret_cast<void*>(
            fourth_patch_address & ~(page_size - 1u));
        for (auto& page : pages_) {
            if (!VirtualProtect(
                    page.address, page_size, PAGE_EXECUTE_READWRITE,
                    &page.old_protection)) {
                Restore(page_size);
                return false;
            }
            ++acquired_;
        }
        page_size_ = page_size;
        return true;
    }

    ~WritablePatchPages() { Restore(page_size_); }

private:
    struct Page final {
        void* address{};
        DWORD old_protection{};
    };

    void Restore(const std::uintptr_t page_size) noexcept {
        while (acquired_ != 0) {
            auto& page = pages_[--acquired_];
            DWORD ignored = 0;
            VirtualProtect(
                page.address, page_size, page.old_protection, &ignored);
        }
    }

    std::array<Page, 4> pages_{};
    std::size_t acquired_{};
    std::uintptr_t page_size_{};
};

bool write_all_patches(const bool install) noexcept {
    const auto* const profile = configured_backend_profile();
    if (profile == nullptr) {
        return false;
    }
    const auto sites = patch_sites(*profile);
    const auto callback_site = emissive_callback_patch_site(*profile);
    const auto callback_replacement =
        absolute_push(callback_site.replacement_target);
    const auto draw_surfs_replacement = draw_surfs_detour(*profile);
    WritablePatchPages pages;
    if (!pages.Acquire()) {
        return false;
    }
    for (const auto& site : sites) {
        const auto replacement =
            relative_call(site.address, site.replacement_target);
        const auto& expected = install ? site.original : replacement;
        if (std::memcmp(
                reinterpret_cast<const void*>(site.address),
                expected.data(), expected.size()) != 0) {
            return false;
        }
    }
    const auto& expected_callback =
        install ? callback_site.original : callback_replacement;
    if (std::memcmp(
            reinterpret_cast<const void*>(callback_site.address),
            expected_callback.data(), expected_callback.size()) != 0) {
        return false;
    }
    if (profile->draw_surfs_entry.address != 0) {
        const auto& expected_draw_surfs = install
            ? profile->draw_surfs_entry.expected
            : draw_surfs_replacement;
        if (std::memcmp(
                reinterpret_cast<const void*>(
                    profile->draw_surfs_entry.address),
                expected_draw_surfs.data(),
                expected_draw_surfs.size()) != 0) {
            return false;
        }
    }
    for (const auto& site : sites) {
        const auto replacement =
            relative_call(site.address, site.replacement_target);
        const auto& desired = install ? replacement : site.original;
        std::memcpy(
            reinterpret_cast<void*>(site.address), desired.data(),
            desired.size());
    }
    const auto& desired_callback =
        install ? callback_replacement : callback_site.original;
    std::memcpy(
        reinterpret_cast<void*>(callback_site.address),
        desired_callback.data(), desired_callback.size());
    if (profile->draw_surfs_entry.address != 0) {
        const auto& desired_draw_surfs = install
            ? draw_surfs_replacement
            : profile->draw_surfs_entry.expected;
        std::memcpy(
            reinterpret_cast<void*>(profile->draw_surfs_entry.address),
            desired_draw_surfs.data(), desired_draw_surfs.size());
    }
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    return true;
}

const char* validate_draw_surf_span(
    const void* const draw_surfs,
    const std::uint32_t count,
    const DrawSurfaceAccess access) noexcept {
    return validate_draw_surface_access(
        draw_surfs, count, kMaximumDrawSurfCount, kDrawSurfSize, access,
        [](const void* address, const std::size_t size) noexcept {
            return accessible_range(address, size);
        });
}

void __cdecl stereo_draw_surfs_thunk(
    void* const prepass_state,
    const RetailDrawSurfListInfo* const info,
    const RetailCmdBufContext context) noexcept {
    const auto* const profile = configured_backend_profile();
    const std::uintptr_t trampoline =
        g_draw_surfs_trampoline.load(std::memory_order_acquire);
    if (profile == nullptr || profile->draw_surfs_address == 0 ||
        trampoline == 0) {
        return;
    }
    const auto original = reinterpret_cast<DrawSurfsFunction>(
        trampoline);
    WAWVR_STEREO_DIAG_ONCE(
        "ScopeDiag R_DrawSurfs detour entered: source=%p state=%p prepass=%p info=%p",
        context.source, context.state, prepass_state, info);
    // The mod only consumes this descriptor for an emissive repair or a
    // registered scope draw. Pointer identity and owned atomic slots suffice
    // to reject ordinary lists before querying any native descriptor memory.
    // Uncertain/changing slots take the fully validated path below.
    const auto* const active_emissive_view =
        static_cast<const std::byte*>(g_emissive_callback_view);
    const bool emissive_draw = active_emissive_view != nullptr &&
        reinterpret_cast<const std::byte*>(info) ==
            active_emissive_view + kCoreDrawListOffsets[3];
    bool scope_slots_proven_empty = true;
    for (const auto& slot : g_physical_scope_owner_slots) {
        std::uintptr_t owner = 0;
        std::uintptr_t data = 0;
        if (!read_physical_scope_owner_slot(slot, &owner, &data) ||
            owner != 0 || data != 0) {
            scope_slots_proven_empty = false;
            break;
        }
    }
    if (!needs_mod_draw_surface_inspection(
            emissive_draw,
            g_physical_scope_draw_pass.load(std::memory_order_acquire),
            scope_slots_proven_empty)) {
        original(prepass_state, info, context);
        return;
    }
    if (info == nullptr || !accessible_range(info, sizeof(*info))) {
        WAWVR_STEREO_DIAG_ONCE(
            "ScopeDiag R_DrawSurfs rejected unreadable info: info=%p",
            info);
        original(prepass_state, info, context);
        return;
    }
    if (const char* const reason = validate_draw_surf_span(
            info->draw_surfs, info->draw_surf_count,
            DrawSurfaceAccess::native_passthrough);
        reason != nullptr) {
        WAWVR_STEREO_DIAG_ONCE(
            "ScopeDiag R_DrawSurfs rejected draw span: info=%p surfaces=%p count=%u reason=%s",
            info, info->draw_surfs, info->draw_surf_count, reason);
        original(prepass_state, info, context);
        return;
    }
    if (info->draw_surf_count == 0) {
        WAWVR_STEREO_DIAG_ONCE(
            "ScopeDiag R_DrawSurfs ignored empty list: info=%p owner=%p cameraView=%u",
            info,
            read_value<const void*>(
                reinterpret_cast<const std::byte*>(info),
                kDrawListViewInfoOffset),
            read_value<std::uint32_t>(
                reinterpret_cast<const std::byte*>(info),
                kDrawListCameraViewOffset));
        original(prepass_state, info, context);
        return;
    }
    const auto* const info_bytes =
        reinterpret_cast<const std::byte*>(info);
    const auto* const draw_list_owner = read_value<const std::byte*>(
        info_bytes, kDrawListViewInfoOffset);
    const std::uint32_t draw_list_camera_view =
        read_value<std::uint32_t>(
            info_bytes, kDrawListCameraViewOffset);
    const auto* const emissive_view = static_cast<const std::byte*>(
        g_emissive_callback_view);
    if (emissive_view != nullptr &&
        info_bytes == emissive_view + kCoreDrawListOffsets[3]) {
        // The retail emissive callback switches to the scene render target
        // after the draw-call wrapper prepared the source viewport. D3D9 may
        // reset its hardware viewport while the renderer's cached viewport
        // still says that the correct packed eye is active. Reapply the
        // scissor at the actual draw seam, then measure/optionally repair the
        // device viewport before R_DrawSurfs performs its cached comparison.
        if (!apply_stereo_emissive_scissor(emissive_view, context.state)) {
            WAWVR_STEREO_DIAG_ONCE(
                "Stereo emissive draw-seam scissor repair rejected an invalid eye/state record: phase=%u view=%p state=%p",
                g_diagnostic_eye_phase, emissive_view, context.state);
        }
        inspect_and_force_stereo_emissive_viewport(
            emissive_view, context.source, context.state,
            info->draw_surf_count);
    }
    const std::byte* data = nullptr;
    for (const auto& slot : g_physical_scope_owner_slots) {
        std::uintptr_t owner_snapshot = 0;
        std::uintptr_t data_snapshot = 0;
        if (!read_physical_scope_owner_slot(
                slot, &owner_snapshot, &data_snapshot)) {
            continue;
        }
        const auto* const cached_scope_owner =
            reinterpret_cast<const std::byte*>(owner_snapshot);
        if (draw_list_owner == cached_scope_owner &&
            cached_scope_owner != nullptr) {
            data = reinterpret_cast<const std::byte*>(
                data_snapshot);
            break;
        }
    }
    constexpr LONG kPhysicalScopePanelPixels = 1024;
    const bool scope_viewport =
        draw_list_owner != nullptr &&
        data != nullptr &&
        accessible_range(
            draw_list_owner,
            kScissorViewportOffset + sizeof(LONG) * 4u) &&
        read_value<LONG>(
            draw_list_owner, kScissorViewportOffset + sizeof(LONG) * 2u) ==
            kPhysicalScopePanelPixels &&
        read_value<LONG>(
            draw_list_owner, kScissorViewportOffset + sizeof(LONG) * 3u) ==
            kPhysicalScopePanelPixels;
    if (!scope_viewport) {
        if (emissive_view != nullptr &&
            info_bytes == emissive_view + kCoreDrawListOffsets[3] &&
            draw_with_emissive_occlusion_diagnostics(
                original, prepass_state, info, context)) {
            if ((g_diagnostic_eye_phase == 1u ||
                 g_diagnostic_eye_phase == 2u) &&
                context.state != nullptr &&
                accessible_range(
                    context.state, kRendererDeviceOffset + sizeof(void*))) {
                const auto* const state_bytes =
                    static_cast<const std::byte*>(context.state);
                IDirect3DDevice9* const device =
                    read_value<IDirect3DDevice9*>(
                        state_bytes, kRendererDeviceOffset);
                const auto* const source_bytes =
                    static_cast<const std::byte*>(context.source);
                if (device != nullptr && source_bytes != nullptr &&
                    accessible_range(
                        source_bytes,
                        kSourceRawFloatZSamplerStateOffset +
                            sizeof(std::uint8_t))) {
                    const auto* const image = read_value<const std::byte*>(
                        source_bytes, kSourceRawFloatZCodeImageOffset);
                    if (image != nullptr && accessible_range(
                            image,
                            kGfxImageHeightOffset + sizeof(std::uint16_t))) {
                        auto* const base_texture =
                            read_value<IDirect3DBaseTexture9*>(
                                image, kGfxImageTextureOffset);
                        IDirect3DTexture9* floatz_texture = nullptr;
                        const HRESULT query_result = base_texture != nullptr
                            ? base_texture->QueryInterface(
                                  __uuidof(IDirect3DTexture9),
                                  reinterpret_cast<void**>(&floatz_texture))
                            : E_POINTER;
                        const auto scale = read_value<std::array<float, 4>>(
                            source_bytes, kSourceClipLookupScaleOffset);
                        const auto offset = read_value<std::array<float, 4>>(
                            source_bytes, kSourceClipLookupOffsetOffset);
                        stereo_diagnostic_log(
                            "StereoDiag smoke-floatz binding: image=%p imageSize=%ux%u texture=%p queryHr=0x%08lX samplerState=%u lookupScale=(%.9g,%.9g,%.9g,%.9g) lookupOffset=(%.9g,%.9g,%.9g,%.9g) dirty=%u",
                            image,
                            static_cast<unsigned>(read_value<std::uint16_t>(
                                image, kGfxImageWidthOffset)),
                            static_cast<unsigned>(read_value<std::uint16_t>(
                                image, kGfxImageHeightOffset)),
                            base_texture,
                            static_cast<unsigned long>(query_result),
                            static_cast<unsigned>(read_value<std::uint8_t>(
                                source_bytes,
                                kSourceRawFloatZSamplerStateOffset)),
                            scale[0], scale[1], scale[2], scale[3],
                            offset[0], offset[1], offset[2], offset[3],
                            static_cast<unsigned>(read_value<std::uint8_t>(
                                source_bytes, kSourceViewportDirtyOffset)));
                        if (floatz_texture != nullptr) {
                            capture_smoke_floatz_after_eye(
                                device, floatz_texture,
                                g_diagnostic_eye_phase);
                            floatz_texture->Release();
                        }
                    } else {
                        stereo_diagnostic_log(
                            "StereoDiag smoke-floatz rejected image binding: image=%p",
                            image);
                    }
                }
                if (g_diagnostic_eye_phase == 2u) {
                    capture_smoke_after_right_emissive(device);
                }
            }
            return;
        }
        original(prepass_state, info, context);
        return;
    }
    // Scope filtering below reads each surface, unlike the native-only
    // branch above. Keep its full bounds/protection query at that boundary;
    // no validation state is reused by a later draw-list invocation.
    if (const char* const reason = validate_draw_surf_span(
            info->draw_surfs, info->draw_surf_count);
        reason != nullptr) {
        WAWVR_STEREO_DIAG_ONCE(
            "ScopeDiag dedicated draw list rejected draw span: info=%p surfaces=%p count=%u reason=%s",
            info, info->draw_surfs, info->draw_surf_count, reason);
        original(prepass_state, info, context);
        return;
    }
    WAWVR_STEREO_DIAG_ONCE(
        "ScopeDiag dedicated draw list identified: info=%p owner=%p cameraView=%u count=%u phase=%u",
        info, draw_list_owner, draw_list_camera_view,
        info->draw_surf_count,
        g_physical_scope_draw_pass.load(std::memory_order_acquire)
            ? 1u
            : 0u);

    WritableSpanValidation scope_data_span{};
    if (!accessible_writable_span(
            data, kRequiredRetailBackEndBytes, &scope_data_span)) {
        WAWVR_STEREO_DIAG_ONCE(
            "ScopeDiag dedicated draw list rejected cached backend data: data=%p reason=%s regions=%zu cursor=%p",
            data, describe_span_rejection(scope_data_span.reason),
            scope_data_span.regions,
            reinterpret_cast<const void*>(scope_data_span.cursor));
        original(prepass_state, info, context);
        return;
    }
    const std::span<const std::byte> back_end_data{
        data, kRequiredRetailBackEndBytes};

    std::uint32_t segment_begin = 0;
    std::uint32_t suppressed_count = 0;
    bool emitted_segment = false;
    for (std::uint32_t index = 0;
         index < info->draw_surf_count; ++index) {
        if (!is_depth_hacked_viewmodel_draw_surface(
                info->draw_surfs[index], back_end_data)) {
            continue;
        }
        if (index > segment_begin) {
            RetailDrawSurfListInfo visible = *info;
            visible.draw_surfs = info->draw_surfs + segment_begin;
            visible.draw_surf_count = index - segment_begin;
            original(prepass_state, &visible, context);
            emitted_segment = true;
        }
        segment_begin = index + 1u;
        ++suppressed_count;
    }

    if (suppressed_count == 0) {
        original(prepass_state, info, context);
        return;
    }
    if (segment_begin < info->draw_surf_count) {
        RetailDrawSurfListInfo visible = *info;
        visible.draw_surfs = info->draw_surfs + segment_begin;
        visible.draw_surf_count = info->draw_surf_count - segment_begin;
        original(prepass_state, &visible, context);
        emitted_segment = true;
    }
    if (!emitted_segment) {
        RetailDrawSurfListInfo empty = *info;
        empty.draw_surf_count = 0;
        original(prepass_state, &empty, context);
    }
    WAWVR_STEREO_DIAG_ONCE(
        "ScopeDiag suppressed %u depth-hacked viewmodel draw surfaces from the dedicated scope camera",
        suppressed_count);
}

const char* validate_draw_list_reference(
    const std::byte* const list,
    const std::byte* const expected_owner) noexcept {
    if (read_value<const void*>(list, kDrawListViewInfoOffset) !=
        expected_owner) {
        return "owner-mismatch";
    }
    return validate_draw_surf_span(
        read_value<const void*>(list, 0),
        read_value<std::uint32_t>(list, sizeof(void*)));
}

void rebase_draw_list_reference(
    std::byte* const first_list,
    const std::byte* const final_list,
    const std::byte* const first_view) noexcept {
    // T4's scalar fields below are the non-owning draw-list references and
    // metadata that remain valid only in the final generated view. Preserve
    // the earlier eye's opaque +0x10 field and all owning renderer structures.
    write_value(
        first_list, 0, read_value<const void*>(final_list, 0));
    write_value(
        first_list, sizeof(void*),
        read_value<std::uint32_t>(final_list, sizeof(void*)));
    write_value(
        first_list, 0x08,
        read_value<std::uint32_t>(final_list, 0x08));
    const void* const first_owner = first_view;
    write_value(first_list, kDrawListViewInfoOffset, first_owner);
    std::memcpy(
        first_list + kDrawListViewOriginOffset,
        first_view + kViewOriginOffset, sizeof(float) * 4u);
    write_value(
        first_list, kDrawListLightOffset,
        read_value<const void*>(final_list, kDrawListLightOffset));
    write_value(
        first_list, kDrawListCameraViewOffset,
        read_value<std::uint32_t>(
            final_list, kDrawListCameraViewOffset));
}

void log_fx_state_change(
    const std::byte* const final,
    const std::uint32_t point_light_count,
    const std::uint32_t emissive_spot_light_count) noexcept {
    // This is deliberately a startup-only diagnostic switch. The state can
    // change several times per second during authored effects; keeping that
    // telemetry enabled in ordinary play adds render-thread file I/O and can
    // itself create the frame-time spikes it is meant to investigate.
    static const bool diagnostics_enabled = []() noexcept {
        wchar_t value[2]{};
        return GetEnvironmentVariableW(
                   L"WAWVR_FX_STEREO_DIAGNOSTICS", value, 2) == 1 &&
            value[0] == L'1';
    }();
    if (!diagnostics_enabled) {
        return;
    }

    const std::uint32_t emissive_draw_surf_count =
        read_value<std::uint32_t>(
            final + kCoreDrawListOffsets[3], sizeof(void*));
    const std::uint32_t fullscreen_flag =
        read_value<std::uint32_t>(final, kIsRenderingFullScreenOffset);
    const std::uint8_t needs_float_z =
        read_value<std::uint8_t>(final, kNeedsFloatZOffset);
    auto& previous = g_fx_state_telemetry;
    const bool changed = !previous.initialized ||
        previous.emissive_draw_surf_count != emissive_draw_surf_count ||
        previous.point_light_count != point_light_count ||
        previous.emissive_spot_light_count !=
            emissive_spot_light_count ||
        previous.fullscreen_flag != fullscreen_flag ||
        previous.needs_float_z != needs_float_z;
    if (!changed) {
        return;
    }

    const ULONGLONG now = GetTickCount64();
    const bool auxiliary_light_activated =
        (point_light_count != 0 && previous.point_light_count == 0) ||
        (emissive_spot_light_count != 0 &&
         previous.emissive_spot_light_count == 0);
    if (previous.initialized && !auxiliary_light_activated &&
        now < previous.next_log_milliseconds) {
        return;
    }

    stereo_diagnostic_log(
        "StereoDiag backend.fx-state: emissiveDrawSurfCount=%u pointLightCount=%u emissiveSpotLightCount=%u needsFloatZ=%u isRenderingFullScreen=%u",
        emissive_draw_surf_count, point_light_count,
        emissive_spot_light_count,
        static_cast<unsigned int>(needs_float_z), fullscreen_flag);
    previous.initialized = true;
    previous.emissive_draw_surf_count = emissive_draw_surf_count;
    previous.point_light_count = point_light_count;
    previous.emissive_spot_light_count = emissive_spot_light_count;
    previous.fullscreen_flag = fullscreen_flag;
    previous.needs_float_z = needs_float_z;
    previous.next_log_milliseconds = now + 200;
}

bool validate_and_rebase_draw_lists(
    std::byte* const first,
    const std::byte* const final) noexcept {
    for (std::size_t list_index = 0;
         list_index < kCoreDrawListOffsets.size(); ++list_index) {
        const std::size_t offset = kCoreDrawListOffsets[list_index];
        const auto* const final_list = final + offset;
        const auto final_owner = read_value<const void*>(
            final_list, kDrawListViewInfoOffset);
        const std::uint32_t count =
            read_value<std::uint32_t>(final_list, sizeof(void*));
        const auto draw_surfs = read_value<const void*>(final_list, 0);
        const char* const reason =
            validate_draw_list_reference(final_list, final);
        if (reason != nullptr) {
            WAWVR_STEREO_DIAG_ONCE(
                "StereoDiag backend.list rejected: ordinal=%zu offset=+0x%zX reason=%s count=%u owner=%p expectedOwner=%p surfaces=%p",
                list_index, offset, reason, count, final_owner, final,
                draw_surfs);
            return false;
        }
    }

    const std::uint32_t point_light_count =
        read_value<std::uint32_t>(final, kPointLightCountOffset);
    if (point_light_count > kMaximumPointLightPartitions) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag backend.aux-list rejected: pointLightCount=%u cap=%u final=%p",
            point_light_count, kMaximumPointLightPartitions, final);
        return false;
    }
    for (std::uint32_t point_light_index = 0;
         point_light_index < point_light_count; ++point_light_index) {
        const std::size_t partition_offset =
            kPointLightPartitionOffset +
            static_cast<std::size_t>(point_light_index) *
                kPointLightPartitionStride;
        const auto* const final_info =
            final + partition_offset + kPointLightInfoOffset;
        const auto final_owner = read_value<const void*>(
            final_info, kDrawListViewInfoOffset);
        const auto final_light = read_value<const void*>(
            final_info, kDrawListLightOffset);
        const void* const expected_light = final + partition_offset;
        const std::uint32_t count = read_value<std::uint32_t>(
            final_info, sizeof(void*));
        const auto draw_surfs = read_value<const void*>(final_info, 0);
        const char* reason =
            validate_draw_list_reference(final_info, final);
        if (reason == nullptr && final_light != expected_light) {
            reason = "light-owner-mismatch";
        }
        if (reason != nullptr) {
            WAWVR_STEREO_DIAG_ONCE(
                "StereoDiag backend.point-light rejected: ordinal=%u info=+0x%zX reason=%s count=%u owner=%p expectedOwner=%p light=%p expectedLight=%p surfaces=%p",
                point_light_index,
                partition_offset + kPointLightInfoOffset, reason, count,
                final_owner, final, final_light, expected_light, draw_surfs);
            return false;
        }
    }

    const std::uint32_t emissive_spot_light_count =
        read_value<std::uint32_t>(final, kEmissiveSpotLightCountOffset);
    if (emissive_spot_light_count > 1u) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag backend.emissive-spot rejected: lightCount=%u cap=1 final=%p",
            emissive_spot_light_count, final);
        return false;
    }
    const std::uint32_t emissive_spot_draw_surf_count =
        emissive_spot_light_count != 0u
            ? read_value<std::uint32_t>(
                  final, kEmissiveSpotDrawSurfCountOffset)
            : 0u;
    const void* const emissive_spot_draw_surfs =
        emissive_spot_light_count != 0u
            ? read_value<const void*>(
                  final, kEmissiveSpotDrawSurfsOffset)
            : nullptr;
    if (const char* const reason = validate_draw_surf_span(
            emissive_spot_draw_surfs,
            emissive_spot_draw_surf_count);
        reason != nullptr) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag backend.emissive-spot rejected: reason=%s lightCount=%u drawSurfCount=%u surfaces=%p final=%p",
            reason, emissive_spot_light_count,
            emissive_spot_draw_surf_count, emissive_spot_draw_surfs, final);
        return false;
    }

    log_fx_state_change(
        final, point_light_count, emissive_spot_light_count);

    WAWVR_STEREO_DIAG_ONCE(
        "StereoDiag backend.lists accepted: first=%p final=%p counts=[%u,%u,%u,%u,%u] surfaces=[%p,%p,%p,%p,%p]",
        first, final,
        read_value<std::uint32_t>(final + kCoreDrawListOffsets[0], sizeof(void*)),
        read_value<std::uint32_t>(final + kCoreDrawListOffsets[1], sizeof(void*)),
        read_value<std::uint32_t>(final + kCoreDrawListOffsets[2], sizeof(void*)),
        read_value<std::uint32_t>(final + kCoreDrawListOffsets[3], sizeof(void*)),
        read_value<std::uint32_t>(final + kCoreDrawListOffsets[4], sizeof(void*)),
        read_value<const void*>(final + kCoreDrawListOffsets[0], 0),
        read_value<const void*>(final + kCoreDrawListOffsets[1], 0),
        read_value<const void*>(final + kCoreDrawListOffsets[2], 0),
        read_value<const void*>(final + kCoreDrawListOffsets[3], 0),
        read_value<const void*>(final + kCoreDrawListOffsets[4], 0));
    WAWVR_STEREO_DIAG_ONCE(
        "StereoDiag backend.aux-lists accepted: first=%p final=%p pointLightCount=%u emissiveSpotLightCount=%u emissiveSpotDrawSurfCount=%u emissiveSpotDrawSurfs=%p",
        first, final, point_light_count, emissive_spot_light_count,
        emissive_spot_draw_surf_count, emissive_spot_draw_surfs);

    for (const std::size_t offset : kCoreDrawListOffsets) {
        rebase_draw_list_reference(
            first + offset, final + offset, first);
    }
    for (std::uint32_t point_light_index = 0;
         point_light_index < point_light_count; ++point_light_index) {
        const std::size_t info_offset =
            kPointLightPartitionOffset +
            static_cast<std::size_t>(point_light_index) *
                kPointLightPartitionStride +
            kPointLightInfoOffset;
        rebase_draw_list_reference(
            first + info_offset, final + info_offset, first);
    }
    write_value(first, kPointLightCountOffset, point_light_count);
    write_value(
        first, kEmissiveSpotLightIndexOffset,
        read_value<std::uint32_t>(final, kEmissiveSpotLightIndexOffset));
    write_value(
        first, kEmissiveSpotLightCountOffset,
        emissive_spot_light_count);
    write_value(
        first, kEmissiveSpotDrawSurfCountOffset,
        emissive_spot_draw_surf_count);
    write_value(
        first, kEmissiveSpotDrawSurfsOffset,
        emissive_spot_draw_surfs);
    write_value(
        first, kNeedsFloatZOffset,
        read_value<std::uint8_t>(final, kNeedsFloatZOffset));

    if (point_light_count != 0u || emissive_spot_light_count != 0u) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag backend.active-aux-lighting rebased: pointLightCount=%u emissiveSpotLightCount=%u emissiveSpotDrawSurfCount=%u",
            point_light_count, emissive_spot_light_count,
            emissive_spot_draw_surf_count);
    }
    return true;
}

class BackendViewIndexRestore final {
public:
    BackendViewIndexRestore(
        std::byte* const data,
        const std::uint32_t saved) noexcept
        : data_(data), saved_(saved) {}
    ~BackendViewIndexRestore() {
        write_value(data_, kViewInfoIndexOffset, saved_);
        g_suppress_secondary_full_clear = false;
        g_physical_scope_draw_pass.store(false, std::memory_order_release);
        g_diagnostic_eye_phase = 0;
    }

private:
    std::byte* data_{};
    std::uint32_t saved_{};
};

[[nodiscard]] bool depth_hack_diagnostic_disabled_by_environment() noexcept {
    static const bool disabled = []() noexcept {
        wchar_t value[2]{};
        return GetEnvironmentVariableW(
                   L"WAWVR_DISABLE_VIEWMODEL_DEPTH_HACK", value, 2) == 1 &&
            value[0] == L'1';
    }();
    return disabled;
}

[[nodiscard]] bool reverse_backend_eye_order_requested() noexcept {
    static const bool requested = []() noexcept {
        wchar_t value[2]{};
        return GetEnvironmentVariableW(
                   L"WAWVR_REVERSE_BACKEND_EYE_ORDER", value, 2) == 1 &&
            value[0] == L'1';
    }();
    return requested;
}

class ScopedDepthHackDiagnosticDisable final {
public:
    ScopedDepthHackDiagnosticDisable(
        std::byte* const data,
        const bool enabled) noexcept
        : data_(data) {
        if (!enabled || data_ == nullptr) {
            return;
        }
        WritableSpanValidation span{};
        if (!accessible_writable_span(
                data_, kRequiredRetailBackEndBytes, &span)) {
            WAWVR_STEREO_DIAG_ONCE(
                "WeaponDiag depth-hack A/B rejected inaccessible retail backend entity span");
            return;
        }
        if (!clear_retail_depth_hack_flags(
                std::span<std::byte>{data_, kRequiredRetailBackEndBytes},
                &snapshot_)) {
            WAWVR_STEREO_DIAG_ONCE(
                "WeaponDiag depth-hack A/B failed to clear retail render flags");
            return;
        }
        active_ = true;
        WAWVR_STEREO_DIAG_ONCE(
            "WeaponDiag depth-hack A/B active: temporarily cleared bit 0x2 on %u first-person GfxEntity records",
            snapshot_.cleared_count);
        if (snapshot_.cleared_count != 0) {
            // The first stereo frame can precede weapon submission. Keep a
            // separate once-site so the log proves that the A/B eventually
            // touched a live first-person entity rather than merely scanning
            // an empty backend entity table.
            WAWVR_STEREO_DIAG_ONCE(
                "WeaponDiag depth-hack A/B confirmed on live first-person entities: cleared=%u",
                snapshot_.cleared_count);
        }
    }

    ScopedDepthHackDiagnosticDisable(
        const ScopedDepthHackDiagnosticDisable&) = delete;
    ScopedDepthHackDiagnosticDisable& operator=(
        const ScopedDepthHackDiagnosticDisable&) = delete;

    ~ScopedDepthHackDiagnosticDisable() {
        if (!active_) {
            return;
        }
        if (!restore_retail_depth_hack_flags(
                std::span<std::byte>{data_, kRequiredRetailBackEndBytes},
                snapshot_)) {
            WAWVR_STEREO_DIAG_ONCE(
                "WeaponDiag depth-hack A/B failed to restore retail render flags");
        }
    }

private:
    std::byte* data_{};
    RetailDepthHackFlagSnapshot snapshot_{};
    bool active_{};
};

// Retail R_SetFullSceneViewMesh bakes the absolute scene-viewport origin into
// each quad. R_DepthPrepassCallback then draws that quad after installing the
// same absolute D3D viewport/scissor. R_Set2D is viewport-local, so the packed
// right eye's x origin is applied twice and its FloatZ far-clear quad is
// clipped completely outside the render target. Reuse the matching x/y-zero
// eye quad only while that view is drawn; the mesh dimensions and UVs are
// identical, and the view record is restored before the backend advances.
class ScopedViewportLocalFloatZClearMesh final {
public:
    ScopedViewportLocalFloatZClearMesh(
        const bool enabled,
        std::byte* const views,
        const std::uint32_t view_count,
        std::byte* const view,
        const std::uint32_t view_index) noexcept
        : view_(view) {
        if (!enabled || views == nullptr || view_ == nullptr ||
            view_count < 2u || view_index + 1u != view_count ||
            read_value<std::uint8_t>(view_, kNeedsFloatZOffset) == 0u) {
            return;
        }

        original_mesh_ = read_value<std::byte*>(
            view_, kFullSceneViewMeshOffset);
        if (original_mesh_ == nullptr ||
            !accessible_range(original_mesh_, kQuadMeshDataSize)) {
            original_mesh_ = nullptr;
            return;
        }
        const auto original_rect = read_value<std::array<float, 4>>(
            original_mesh_, 0u);
        const auto scene_viewport =
            read_value<std::array<std::int32_t, 4>>(
                view_, kSceneViewportOffset);
        if (scene_viewport[0] <= 0 || scene_viewport[1] != 0 ||
            scene_viewport[2] <= 0 || scene_viewport[3] <= 0 ||
            original_rect[0] != static_cast<float>(scene_viewport[0]) ||
            original_rect[1] != static_cast<float>(scene_viewport[1]) ||
            original_rect[2] != static_cast<float>(scene_viewport[2]) ||
            original_rect[3] != static_cast<float>(scene_viewport[3])) {
            WAWVR_STEREO_DIAG_ONCE(
                "StereoDiag FloatZ clear mesh correction rejected: index=%u mesh rect does not match scene viewport",
                view_index);
            original_mesh_ = nullptr;
            return;
        }
        const std::uint32_t candidate_index = view_count - 2u;
        std::byte* const candidate_view =
            views + static_cast<std::size_t>(candidate_index) *
                kViewInfoStride;
        const auto candidate_viewport =
            read_value<std::array<std::int32_t, 4>>(
                candidate_view, kSceneViewportOffset);
        std::byte* const candidate_mesh = read_value<std::byte*>(
            candidate_view, kFullSceneViewMeshOffset);
        if (read_value<std::uint8_t>(
                candidate_view, kNeedsFloatZOffset) != 0u &&
            candidate_mesh != nullptr &&
            candidate_mesh != original_mesh_ &&
            accessible_range(candidate_mesh, kQuadMeshDataSize)) {
            const auto candidate_rect =
                read_value<std::array<float, 4>>(candidate_mesh, 0u);
            if (candidate_viewport[0] == 0 &&
                candidate_viewport[1] == 0 &&
                candidate_viewport[2] == scene_viewport[2] &&
                candidate_viewport[3] == scene_viewport[3] &&
                scene_viewport[0] == candidate_viewport[2] &&
                candidate_rect[0] ==
                    static_cast<float>(candidate_viewport[0]) &&
                candidate_rect[1] ==
                    static_cast<float>(candidate_viewport[1]) &&
                candidate_rect[2] ==
                    static_cast<float>(candidate_viewport[2]) &&
                candidate_rect[3] ==
                    static_cast<float>(candidate_viewport[3])) {
                write_value(
                    view_, kFullSceneViewMeshOffset, candidate_mesh);
                active_ = true;
                WAWVR_STEREO_DIAG_ONCE(
                    "StereoDiag FloatZ clear mesh corrected: index=%u original=%p rect=(%.3f,%.3f %.3fx%.3f) localIndex=%u local=%p rect=(%.3f,%.3f %.3fx%.3f)",
                    view_index, original_mesh_,
                    original_rect[0], original_rect[1],
                    original_rect[2], original_rect[3],
                    candidate_index, candidate_mesh,
                    candidate_rect[0], candidate_rect[1],
                    candidate_rect[2], candidate_rect[3]);
                return;
            }
        }

        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag FloatZ clear mesh correction rejected: index=%u original=%p rect=(%.3f,%.3f %.3fx%.3f) no matching viewport-local mesh",
            view_index, original_mesh_,
            original_rect[0], original_rect[1],
            original_rect[2], original_rect[3]);
        original_mesh_ = nullptr;
    }

    ScopedViewportLocalFloatZClearMesh(
        const ScopedViewportLocalFloatZClearMesh&) = delete;
    ScopedViewportLocalFloatZClearMesh& operator=(
        const ScopedViewportLocalFloatZClearMesh&) = delete;

    ~ScopedViewportLocalFloatZClearMesh() {
        if (active_) {
            write_value(
                view_, kFullSceneViewMeshOffset, original_mesh_);
        }
    }

private:
    std::byte* view_{};
    std::byte* original_mesh_{};
    bool active_{};
};

void __cdecl stereo_backend_draw_thunk(
    const void* const selected_view) noexcept {
    const auto* const profile = configured_backend_profile();
    if (profile == nullptr) {
        return;
    }
    const auto original = reinterpret_cast<Draw3DInternalFunction>(
        profile->draw_3d_internal_address);

    std::uint64_t frame_id = 0;
    wawvr::xr::StereoSourceLayout layout{};
    if (!stereo_backend_hook_ready()) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag backend.confirm rejected: hook installed=%u replacement/profile ownership not ready",
            g_installed.load(std::memory_order_acquire) ? 1u : 0u);
        original(selected_view);
        return;
    }
    if (!try_get_staged_stereo_frame(&frame_id, &layout)) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag backend.confirm rejected: no staged broker frame selectedView=%p",
            selected_view);
        original(selected_view);
        return;
    }
    if (!layout.physical_scope.active) {
        // Invalidate stale scope ownership before any later backend validation
        // can fail and fall back to the stock draw path.
        clear_physical_scope_owner_slots();
    }
    if (!accessible_range(
            reinterpret_cast<const void*>(
                profile->back_end_data_pointer_address),
            sizeof(void*))) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag backend.confirm rejected: stagedFrame=%llu backEndData global %p unreadable selectedView=%p",
            static_cast<unsigned long long>(frame_id),
            reinterpret_cast<const void*>(
                profile->back_end_data_pointer_address),
            selected_view);
        original(selected_view);
        return;
    }

    std::byte* data = nullptr;
    std::memcpy(
        &data,
        reinterpret_cast<const void*>(
            profile->back_end_data_pointer_address),
        sizeof(data));
    WritableSpanValidation data_span{};
    if (!accessible_writable_span(
            data, kViewInfoPointerOffset + sizeof(void*), &data_span)) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag backend.data-span rejected: stagedFrame=%llu begin=%p size=0x%zX reason=%s acceptedRegions=%zu cursor=%p BaseAddress=%p AllocationBase=%p RegionSize=0x%zX State=0x%08lX Protect=0x%08lX",
            static_cast<unsigned long long>(frame_id), data,
            kViewInfoPointerOffset + sizeof(void*),
            describe_span_rejection(data_span.reason), data_span.regions,
            reinterpret_cast<const void*>(data_span.cursor),
            data_span.memory.BaseAddress, data_span.memory.AllocationBase,
            static_cast<std::size_t>(data_span.memory.RegionSize),
            static_cast<unsigned long>(data_span.memory.State),
            static_cast<unsigned long>(data_span.memory.Protect));
        original(selected_view);
        return;
    }
    WAWVR_STEREO_DIAG_ONCE(
        "StereoDiag backend.data-span accepted: stagedFrame=%llu begin=%p size=0x%zX regions=%zu",
        static_cast<unsigned long long>(frame_id), data,
        kViewInfoPointerOffset + sizeof(void*), data_span.regions);
    const std::uint32_t count =
        read_value<std::uint32_t>(data, kViewInfoCountOffset);
    const std::uint32_t expected_count =
        layout.physical_scope.active ? 3u : 2u;
    if (count != expected_count) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag backend.confirm rejected: stagedFrame=%llu data=%p backendCount=%u expected=%u scope=%u backendIndex=%u views=%p selectedView=%p",
            static_cast<unsigned long long>(frame_id), data, count,
            expected_count, layout.physical_scope.active ? 1u : 0u,
            read_value<std::uint32_t>(data, kViewInfoIndexOffset),
            read_value<void*>(data, kViewInfoPointerOffset), selected_view);
        original(selected_view);
        return;
    }

    const std::uint32_t saved_index =
        read_value<std::uint32_t>(data, kViewInfoIndexOffset);
    std::byte* views = read_value<std::byte*>(data, kViewInfoPointerOffset);
    if (saved_index != expected_count - 1u) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag backend.confirm rejected: stagedFrame=%llu data=%p backendCount=%u backendIndex=%u expected=%u views=%p selectedView=%p",
            static_cast<unsigned long long>(frame_id), data, count,
            saved_index, expected_count - 1u, views, selected_view);
        original(selected_view);
        return;
    }
    WritableSpanValidation views_span{};
    if (!accessible_writable_span(
            views, kViewInfoStride * expected_count, &views_span)) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag backend.views-span rejected: stagedFrame=%llu begin=%p size=0x%zX reason=%s acceptedRegions=%zu cursor=%p BaseAddress=%p AllocationBase=%p RegionSize=0x%zX State=0x%08lX Protect=0x%08lX",
            static_cast<unsigned long long>(frame_id), views,
            kViewInfoStride * expected_count,
            describe_span_rejection(views_span.reason), views_span.regions,
            reinterpret_cast<const void*>(views_span.cursor),
            views_span.memory.BaseAddress, views_span.memory.AllocationBase,
            static_cast<std::size_t>(views_span.memory.RegionSize),
            static_cast<unsigned long>(views_span.memory.State),
            static_cast<unsigned long>(views_span.memory.Protect));
        original(selected_view);
        return;
    }
    WAWVR_STEREO_DIAG_ONCE(
        "StereoDiag backend.views-span accepted: stagedFrame=%llu begin=%p size=0x%zX regions=%zu",
        static_cast<unsigned long long>(frame_id), views,
        kViewInfoStride * expected_count, views_span.regions);
    std::byte* const final =
        views + static_cast<std::size_t>(expected_count - 1u) *
            kViewInfoStride;
    if (selected_view != final) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag backend.confirm rejected: stagedFrame=%llu selectedView=%p expectedFinal=%p first=%p data=%p count=%u index=%u",
            static_cast<unsigned long long>(frame_id), selected_view, final,
            views, data, expected_count, expected_count - 1u);
        original(selected_view);
        return;
    }
    for (std::uint32_t view_index = 0;
         view_index + 1u < expected_count; ++view_index) {
        std::byte* const view =
            views + static_cast<std::size_t>(view_index) * kViewInfoStride;
        if (!validate_and_rebase_draw_lists(view, final)) {
            WAWVR_STEREO_DIAG_ONCE(
                "StereoDiag backend.confirm rejected: stagedFrame=%llu core/auxiliary draw-list validation/rebase failed viewIndex=%u view=%p final=%p",
                static_cast<unsigned long long>(frame_id), view_index, view,
                final);
            original(selected_view);
            return;
        }
    }
    if (layout.physical_scope.active) {
        const std::uintptr_t owner =
            reinterpret_cast<std::uintptr_t>(
                views + static_cast<std::size_t>(kPhysicalScopeViewIndex) *
                    kViewInfoStride);
        std::size_t slot = 0;
        std::uintptr_t first_owner = 0;
        std::uintptr_t first_data = 0;
        std::uintptr_t second_owner = 0;
        std::uintptr_t second_data = 0;
        static_cast<void>(read_physical_scope_owner_slot(
            g_physical_scope_owner_slots[0], &first_owner, &first_data));
        static_cast<void>(read_physical_scope_owner_slot(
            g_physical_scope_owner_slots[1], &second_owner, &second_data));
        if (first_owner != owner &&
            (second_owner == owner || first_owner != 0)) {
            slot = 1;
        }
        publish_physical_scope_owner_slot(
            g_physical_scope_owner_slots[slot], owner,
            reinterpret_cast<std::uintptr_t>(data));
    }

    const bool depth_hack_diagnostic =
        depth_hack_diagnostic_disabled_by_environment();
    if (depth_hack_diagnostic && layout.physical_scope.active) {
        WAWVR_STEREO_DIAG_ONCE(
            "WeaponDiag depth-hack A/B withheld while a physical scope view is active");
    }
    ScopedDepthHackDiagnosticDisable depth_hack_disable{
        data, depth_hack_diagnostic && !layout.physical_scope.active};

    WAWVR_STEREO_DIAG_ONCE(
        "StereoDiag backend.multi-view accepted: frame=%llu data=%p views=%p count=%u scope=%u selected=%p",
        static_cast<unsigned long long>(frame_id), data, views,
        expected_count, layout.physical_scope.active ? 1u : 0u,
        selected_view);
    BackendViewIndexRestore restore(data, saved_index);
    g_diagnostic_primary_clear_calls = 0;
    g_diagnostic_secondary_clear_calls = 0;
    const bool batch_timing_enabled =
        performance_timing_diagnostics_enabled();
    const auto batch_timing_started = batch_timing_enabled
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    const StereoBackendEyeOrder eye_order =
        select_stereo_backend_eye_order(
            expected_count,
            reverse_backend_eye_order_requested(),
            layout.physical_scope.active);
    if (eye_order.count != expected_count) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag backend.eye-order rejected: frame=%llu count=%u selectedCount=%u",
            static_cast<unsigned long long>(frame_id), expected_count,
            eye_order.count);
        original(selected_view);
        return;
    }
    if (eye_order.reversed) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag backend.eye-order diagnostic active: frame=%llu drawOrder=[1,0] actual view indices preserved",
            static_cast<unsigned long long>(frame_id));
    } else if (reverse_backend_eye_order_requested() &&
               layout.physical_scope.active) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag backend.eye-order diagnostic withheld while a physical scope view is active");
    }
    for (std::uint32_t draw_ordinal = 0;
         draw_ordinal < expected_count; ++draw_ordinal) {
        const std::uint32_t view_index =
            eye_order.view_indices[draw_ordinal];
        std::byte* const view =
            views + static_cast<std::size_t>(view_index) * kViewInfoStride;
        write_value(data, kViewInfoIndexOffset, view_index);
        // The first draw owns the packed-target clear even when this
        // diagnostic reverses the physical eye order. Every later eye must
        // preserve pixels already written by an earlier draw.
        g_suppress_secondary_full_clear = draw_ordinal != 0u;
        g_physical_scope_draw_pass.store(
            layout.physical_scope.active &&
                view_index == kPhysicalScopeViewIndex,
            std::memory_order_release);
        g_diagnostic_eye_phase = view_index + 1u;
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag backend.view call begin: frame=%llu view=%p ordinal=%u index=%u count=%u clearSuppress=%u reversed=%u",
            static_cast<unsigned long long>(frame_id), view, draw_ordinal,
            view_index, expected_count, draw_ordinal != 0u ? 1u : 0u,
            eye_order.reversed ? 1u : 0u);
        ScopedViewportLocalFloatZClearMesh local_floatz_clear_mesh{
            profile == &kSpBackendProfile,
            views, expected_count, view, view_index};
        {
            ScopedPerformanceTiming timing(
                PerformanceTimingPhase::stereo_backend_view);
            original(view);
        }
        g_diagnostic_eye_phase = 0;
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag backend.view call complete: frame=%llu view=%p ordinal=%u index=%u primaryClearCalls=%u suppressedClearCalls=%u",
            static_cast<unsigned long long>(frame_id), view, draw_ordinal,
            view_index,
            g_diagnostic_primary_clear_calls,
            g_diagnostic_secondary_clear_calls);
    }
    if (batch_timing_enabled) {
        record_performance_timing(
            PerformanceTimingPhase::stereo_backend_batch,
            std::chrono::steady_clock::now() - batch_timing_started);
    }
    g_suppress_secondary_full_clear = false;
    g_physical_scope_draw_pass.store(false, std::memory_order_release);

    mark_stereo_frame_rendered(frame_id, layout);
    WAWVR_STEREO_DIAG_ONCE(
        "StereoDiag backend.confirm mark requested: frame=%llu all %u view calls returned",
        static_cast<unsigned long long>(frame_id), expected_count);
}

bool __cdecl should_suppress_secondary_clear() noexcept {
    if (g_diagnostic_eye_phase == 1u) {
        ++g_diagnostic_primary_clear_calls;
    } else if (g_diagnostic_eye_phase == 2u) {
        ++g_diagnostic_secondary_clear_calls;
    }
    return g_suppress_secondary_full_clear;
}

} // namespace

StereoBackendHookResult install_stereo_backend_hook() noexcept {
    const auto* const profile = configured_backend_profile();
    if (profile == nullptr) {
        return StereoBackendHookResult::profile_mismatch;
    }
    if (g_installed.load(std::memory_order_acquire)) {
        return stereo_backend_hook_ready()
                   ? StereoBackendHookResult::already_installed
                   : StereoBackendHookResult::profile_mismatch;
    }
    if (!verify_original_profile()) {
        return StereoBackendHookResult::profile_mismatch;
    }
    if (!ensure_draw_surfs_trampoline(*profile)) {
        return StereoBackendHookResult::patch_write_failed;
    }
    g_original_standard_emissive_callback.store(
        profile->standard_emissive_callback_address,
        std::memory_order_release);
    SuspendedPeerThreads suspended;
    if (!suspend_backend_patch_threads(&suspended)) {
        g_original_standard_emissive_callback.store(
            0, std::memory_order_release);
        return StereoBackendHookResult::thread_suspend_failed;
    }
    if (!verify_original_profile()) {
        g_original_standard_emissive_callback.store(
            0, std::memory_order_release);
        return StereoBackendHookResult::profile_mismatch;
    }
    if (!write_all_patches(true)) {
        g_original_standard_emissive_callback.store(
            0, std::memory_order_release);
        return StereoBackendHookResult::patch_write_failed;
    }
    g_installed.store(true, std::memory_order_release);
    return StereoBackendHookResult::installed;
}

StereoBackendHookResult restore_stereo_backend_hook() noexcept {
    if (!g_installed.load(std::memory_order_acquire)) {
        return StereoBackendHookResult::already_installed;
    }
    SuspendedPeerThreads suspended;
    if (!suspend_backend_patch_threads(&suspended)) {
        return StereoBackendHookResult::thread_suspend_failed;
    }
    if (!verify_replacement_ownership()) {
        return StereoBackendHookResult::profile_mismatch;
    }
    if (!write_all_patches(false)) {
        return StereoBackendHookResult::patch_write_failed;
    }
    g_installed.store(false, std::memory_order_release);
    g_suppress_secondary_full_clear = false;
    g_physical_scope_draw_pass.store(false, std::memory_order_release);
    clear_physical_scope_owner_slots();
    return StereoBackendHookResult::installed;
}

bool stereo_backend_hook_installed() noexcept {
    return g_installed.load(std::memory_order_acquire);
}

bool stereo_backend_hook_ready() noexcept {
    return g_installed.load(std::memory_order_acquire) &&
           verify_replacement_ownership();
}

} // namespace wawvr::mod
